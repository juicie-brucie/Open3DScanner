#include "openscanner/worker_process.h"

#include <cstdio>
#include <vector>

#if defined(_WIN32)

#include <windows.h>

namespace openscanner {

bool RunSelfAsWorker(const std::vector<std::string>& extra_args, int* out_exit_code, std::string* out_reason,
                      int timeout_seconds) {
    wchar_t self_path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, self_path, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        if (out_reason != nullptr) *out_reason = "GetModuleFileNameW failed";
        return false;
    }

    std::wstring cmd_line = L"\"" + std::wstring(self_path) + L"\"";
    for (const auto& arg : extra_args) {
        // Minimal quoting: our own args never contain embedded quotes/spaces
        // today (flag + a filesystem path); revisit if that changes.
        cmd_line += L" \"" + std::wstring(arg.begin(), arg.end()) + L"\"";
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    std::vector<wchar_t> mutable_cmd(cmd_line.begin(), cmd_line.end());
    mutable_cmd.push_back(L'\0');

    if (!CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, /*bInheritHandles=*/TRUE, 0, nullptr, nullptr,
                         &si, &pi)) {
        if (out_reason != nullptr) *out_reason = "CreateProcessW failed, GetLastError=" + std::to_string(GetLastError());
        return false;
    }

    DWORD wait_result = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeout_seconds) * 1000);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, /*uExitCode=*/1);
        WaitForSingleObject(pi.hProcess, INFINITE);  // Reap it; TerminateProcess is async.
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (out_reason != nullptr) {
            *out_reason = "child timed out after " + std::to_string(timeout_seconds) + "s and was force-killed";
        }
        return false;
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // NTSTATUS exception codes (e.g. 0xC0000005 ACCESS_VIOLATION) have their
    // top two bits set to indicate an error/exception termination rather
    // than a normal exit code.
    if ((exit_code & 0xC0000000) == 0xC0000000) {
        if (out_reason != nullptr) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "child crashed, exit code 0x%08lX", exit_code);
            *out_reason = buf;
        }
        return false;
    }

    if (out_exit_code != nullptr) *out_exit_code = static_cast<int>(exit_code);
    return true;
}

}  // namespace openscanner

#else  // POSIX

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace openscanner {
namespace {

bool GetSelfExecutablePath(std::string* out_path) {
#if defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) {
        return false;
    }
    char resolved[PATH_MAX];
    if (realpath(buf, resolved) == nullptr) {
        return false;
    }
    *out_path = resolved;
    return true;
#else
    // Linux and other POSIX systems that expose /proc/self/exe. Untested on
    // this project's current targets (macOS/Windows); revisit if/when Linux
    // is actually supported.
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return false;
    }
    buf[n] = '\0';
    *out_path = buf;
    return true;
#endif
}

}  // namespace

bool RunSelfAsWorker(const std::vector<std::string>& extra_args, int* out_exit_code, std::string* out_reason,
                      int timeout_seconds) {
    std::string self_path;
    if (!GetSelfExecutablePath(&self_path)) {
        if (out_reason != nullptr) *out_reason = "could not resolve own executable path";
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (out_reason != nullptr) *out_reason = std::string("fork failed: ") + strerror(errno);
        return false;
    }

    if (pid == 0) {
        // Child: re-exec as a fresh process image running the same binary.
        // Callers who need DYLD_INSERT_LIBRARIES (macOS) set it via plain
        // setenv() before calling this — it's inherited across fork()
        // automatically, no special handling needed here. (See
        // live_capture_test/main.cpp and vulkan_disable_interpose.c for
        // why this project needs that.)
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(self_path.c_str()));
        for (const auto& arg : extra_args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(self_path.c_str(), argv.data());
        // execv only returns on failure.
        std::perror("execv");
        _exit(127);
    }

    // Poll rather than block on waitpid(..., 0): that call has no timeout
    // parameter, and we need to be able to reclaim a worker that's still
    // alive but not making progress (see the "no further logging at all"
    // case documented in worker_process.h), not just one that's exited or
    // been killed by a signal.
    int status = 0;
    bool exited = false;
    const int kPollIntervalMs = 100;
    const int kMaxPolls = (timeout_seconds * 1000) / kPollIntervalMs;
    for (int poll_count = 0; poll_count < kMaxPolls; ++poll_count) {
        pid_t wait_ret = waitpid(pid, &status, WNOHANG);
        if (wait_ret < 0 && errno == EINTR) {
            continue;  // Interrupted by a signal, not a failure; poll again.
        }
        if (wait_ret < 0) {
            // Don't leave the worker running unsupervised with nobody left
            // to enforce the timeout.
            std::string err = strerror(errno);
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            if (out_reason != nullptr) *out_reason = "waitpid failed: " + err;
            return false;
        }
        if (wait_ret == pid) {
            exited = true;
            break;
        }
        usleep(kPollIntervalMs * 1000);
    }

    if (!exited) {
        kill(pid, SIGKILL);
        // Reap it — SIGKILL is asynchronous.
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        if (out_reason != nullptr) {
            *out_reason = "child timed out after " + std::to_string(timeout_seconds) + "s and was force-killed";
        }
        return false;
    }

    if (WIFSIGNALED(status)) {
        if (out_reason != nullptr) {
            *out_reason = std::string("child killed by signal ") + strsignal(WTERMSIG(status));
        }
        return false;
    }

    if (out_exit_code != nullptr) *out_exit_code = WEXITSTATUS(status);
    return true;
}

}  // namespace openscanner

#endif
