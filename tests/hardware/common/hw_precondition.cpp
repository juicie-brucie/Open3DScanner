#include "hw_precondition.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/wait.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#endif

namespace hwtest {
namespace {

// The scanner's USB vendor id. The SDK's own detection watches this id; see
// README.md, "Device enumeration returns 0 devices ... — resolved".
constexpr int kDeviceVendorId = 0x2BC5;

// The device wording is literal in the skip reason (the Windows lane greps
// for it), so it is spelled out here and not built from the id.
constexpr const char* kDeviceReason = "no VID_2BC5 device enumerated";

// The process names that can host the vendor's RPC daemon on this platform.
//
// macOS: the RPC server is Creality's root daemon (/Library/Creation/
// RPCServer, launched by the com.creality.RPCServer LaunchAgent) and the
// CrealityScan app is that daemon's GUI client — see
// docs/recomp/FULL-INDEPENDENCE.md §1 (L3). The proxy accepts either
// process, because it must not report a false skip on a host where the
// daemon runs and the app is closed. It is a process proxy, not a service
// query: it says "a vendor RPC host process exists", nothing more.
#if defined(_WIN32)
constexpr const char* const kDaemonHostProcesses[] = {"CrealityScan.exe"};
#else
constexpr const char* const kDaemonHostProcesses[] = {"CrealityScan", "RPCServer"};
#endif

enum class Lookup {
    kFound,
    kNotFound,
    kFailed,
};

// Looks for a process by its exact executable name. Never a path substring.
Lookup ProcessIsRunning(const std::string& process_name) {
#if defined(_WIN32)
    // tasklist is the platform's process list; the image name is matched
    // exactly by the filter.
    const std::string cmd = "tasklist /FI \"IMAGENAME eq " + process_name + "\" /NH 2>NUL";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) return Lookup::kFailed;
    char line[512];
    bool found = false;
    while (std::fgets(line, sizeof(line), pipe) != nullptr) {
        if (std::strstr(line, process_name.c_str()) != nullptr) {
            found = true;
            break;
        }
    }
    const int rc = pclose(pipe);
    if (rc == -1) return Lookup::kFailed;
    return found ? Lookup::kFound : Lookup::kNotFound;
#else
    // pgrep -x matches the executable name exactly (never a path substring).
    // Its exit status is the answer: 0 found, 1 not found, anything else
    // means the lookup itself failed.
    const std::string cmd = "pgrep -x " + process_name + " >/dev/null 2>&1";
    const int rc = std::system(cmd.c_str());
    if (rc == -1) return Lookup::kFailed;
    if (!WIFEXITED(rc)) return Lookup::kFailed;
    const int status = WEXITSTATUS(rc);
    if (status == 0) return Lookup::kFound;
    if (status == 1) return Lookup::kNotFound;
    return Lookup::kFailed;
#endif
}

// Probes the vendor's RPC daemon host. `detail` names the process that
// answered, so the record can say which one it was.
Lookup ProbeDaemon(std::string* detail) {
    bool lookup_failed = false;
    for (const char* process_name : kDaemonHostProcesses) {
        switch (ProcessIsRunning(process_name)) {
            case Lookup::kFound:
                *detail = std::string(process_name) + " running";
                return Lookup::kFound;
            case Lookup::kNotFound:
                break;
            case Lookup::kFailed:
                lookup_failed = true;
                break;
        }
    }
    if (lookup_failed) {
        *detail = "process lookup failed";
        return Lookup::kFailed;
    }
    *detail = "not running";
    return Lookup::kNotFound;
}

#if defined(__APPLE__)
// Enumerates the USB devices the OS knows about and looks for the scanner's
// vendor id. The OS enumeration is the ground truth here: the SDK's own
// watchers start inside obscan_context_create() and report through the
// library, not to us (see docs/recon/findings/plan7-detection-path.md).
Lookup ProbeDevice(std::string* detail) {
    CFMutableDictionaryRef matching = IOServiceMatching(kIOUSBDeviceClassName);
    if (matching == nullptr) {
        *detail = "IOServiceMatching failed";
        return Lookup::kFailed;
    }
    io_iterator_t iterator = IO_OBJECT_NULL;
    // IOServiceGetMatchingServices consumes the matching dictionary's
    // reference (IOKitLib.h), so it is not released here.
    if (IOServiceGetMatchingServices(kIOMasterPortDefault, matching, &iterator) != KERN_SUCCESS) {
        *detail = "IOServiceGetMatchingServices failed";
        return Lookup::kFailed;
    }
    bool found = false;
    io_object_t service = IO_OBJECT_NULL;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        CFTypeRef vendor = IORegistryEntryCreateCFProperty(service, CFSTR("idVendor"), kCFAllocatorDefault, 0);
        if (vendor != nullptr) {
            if (CFGetTypeID(vendor) == CFNumberGetTypeID()) {
                int value = 0;
                if (CFNumberGetValue(static_cast<CFNumberRef>(vendor), kCFNumberIntType, &value) &&
                    value == kDeviceVendorId) {
                    found = true;
                }
            }
            CFRelease(vendor);
        }
        IOObjectRelease(service);
        if (found) break;
    }
    IOObjectRelease(iterator);
    if (found) {
        *detail = "VID 0x2BC5 enumerated";
        return Lookup::kFound;
    }
    *detail = kDeviceReason;
    return Lookup::kNotFound;
}
#elif defined(_WIN32)
// Lists the connected devices and looks for the scanner's vendor id in the
// instance id ("USB\VID_2BC5&PID_...").
Lookup ProbeDevice(std::string* detail) {
    FILE* pipe = popen("pnputil /enum-devices /connected 2>NUL", "r");
    if (pipe == nullptr) {
        *detail = "pnputil failed";
        return Lookup::kFailed;
    }
    char line[1024];
    bool found = false;
    while (std::fgets(line, sizeof(line), pipe) != nullptr) {
        if (std::strstr(line, "VID_2BC5") != nullptr) {
            found = true;
            break;
        }
    }
    const int rc = pclose(pipe);
    if (rc == -1) {
        *detail = "pnputil failed";
        return Lookup::kFailed;
    }
    if (found) {
        *detail = "VID 0x2BC5 enumerated";
        return Lookup::kFound;
    }
    *detail = kDeviceReason;
    return Lookup::kNotFound;
}
#else
Lookup ProbeDevice(std::string* detail) {
    *detail = "no device probe for this platform";
    return Lookup::kFailed;
}
#endif

// Applies the test-only override of a probe's verdict, if one is set. An
// override replaces the probe's own wording too, so the reason line never
// contradicts itself.
PreconditionState ApplyOverride(const char* env_name, PreconditionState probed, std::string* detail,
                                 const char* what) {
    const char* value = std::getenv(env_name);
    if (value == nullptr || *value == '\0') return probed;
    if (std::strcmp(value, "present") == 0) {
        *detail = "present (forced)";
        return PreconditionState::kSatisfied;
    }
    if (std::strcmp(value, "absent") == 0) {
        *detail = "not present (forced)";
        return PreconditionState::kMissing;
    }
    if (std::strcmp(value, "unknown") == 0) {
        *detail = "probe indeterminate (forced)";
        return PreconditionState::kIndeterminate;
    }
    std::printf("[probe] ignoring unknown %s override '%s' (want present|absent|unknown)\n", what, value);
    std::fflush(stdout);
    return probed;
}

PreconditionState StateOf(Lookup lookup) {
    switch (lookup) {
        case Lookup::kFound: return PreconditionState::kSatisfied;
        case Lookup::kNotFound: return PreconditionState::kMissing;
        case Lookup::kFailed: return PreconditionState::kIndeterminate;
    }
    return PreconditionState::kIndeterminate;
}

const char* StateName(PreconditionState state) {
    switch (state) {
        case PreconditionState::kSatisfied: return "satisfied";
        case PreconditionState::kMissing: return "missing";
        case PreconditionState::kIndeterminate: return "indeterminate";
    }
    return "indeterminate";
}

}  // namespace

const char* DaemonHostName() {
#if defined(_WIN32)
    return "CrealityScan.exe";
#else
    return "CrealityScan";
#endif
}

PreconditionReport ProbePreconditions(const std::string& checks_label) {
    PreconditionReport report;

    std::string daemon_detail;
    report.daemon = StateOf(ProbeDaemon(&daemon_detail));
    report.daemon_detail = daemon_detail;

    std::string device_detail;
    report.device = StateOf(ProbeDevice(&device_detail));
    report.device_detail = device_detail;

    report.daemon = ApplyOverride("OPENSCANNER_HW_DAEMON_STATE", report.daemon, &report.daemon_detail, "daemon");
    report.device = ApplyOverride("OPENSCANNER_HW_DEVICE_STATE", report.device, &report.device_detail, "device");

    const std::string tail = "; " + checks_label + " NOT exercised.";
    if (report.daemon == PreconditionState::kMissing) {
        report.reason = std::string("precondition not met: ") + DaemonHostName() +
                        " (RPC daemon host) is not running" + tail;
    } else if (report.device == PreconditionState::kMissing) {
        report.reason = std::string("precondition not met: ") + kDeviceReason + tail;
    } else if (report.daemon == PreconditionState::kIndeterminate ||
               report.device == PreconditionState::kIndeterminate) {
        report.reason = std::string("precondition probe failed (") + DaemonHostName() + ": " + report.daemon_detail +
                        "; VID_2BC5: " + report.device_detail + ")" + tail;
    }
    return report;
}

int RunPreconditionProbeMode(const std::string& checks_label) {
    const PreconditionReport report = ProbePreconditions(checks_label);
    std::printf("[probe] daemon: %s (%s)\n", StateName(report.daemon), report.daemon_detail.c_str());
    std::printf("[probe] device: %s (%s)\n", StateName(report.device), report.device_detail.c_str());
    if (report.satisfied()) {
        std::printf("[probe] preconditions satisfied\n");
        std::fflush(stdout);
        return 0;
    }
    std::printf("[probe] %s\n", report.reason.c_str());
    std::fflush(stdout);
    return kPreconditionSkipExitCode;
}

}  // namespace hwtest
