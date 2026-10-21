// openscanner-helper — the privileged helper skeleton (row #170).
//
// Runs as a launchd/SMAppService daemon (plist: launchd/), claims the scanner
// on start, serves the ping/claim/release/status protocol over XPC, and
// releases on quit and on unplug. It carries no streaming path: nothing in
// this binary opens a UVC or HID interface.
//
// `--once` is the operator gate's first half: claim, log, release, exit,
// without installing anything.
//
// Architecture: docs/recomp/FULL-INDEPENDENCE.md §5 L3 and §6 Phase D.

#include <dispatch/dispatch.h>
#include <os/log.h>
#include <signal.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "device_claim.h"
#include "helper_service.h"
#include "helper_xpc.h"

namespace {

constexpr const char* kDefaultServiceName = "org.openscanner.helper";

os_log_t g_log = nullptr;

void LogLine(const std::string& line) {
    os_log_with_type(g_log ? g_log : OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT, "%{public}s", line.c_str());
    std::fprintf(stderr, "helper: %s\n", line.c_str());
}

void Usage() {
    std::fprintf(stderr,
                 "usage: openscanner-helper [--claim iokit|null] [--seize] "
                 "[--service-name NAME] [--once]\n"
                 "  --claim iokit   open the scanner's USB device exclusively (default)\n"
                 "  --claim null    refuse every claim (a run that touched nothing)\n"
                 "  --seize         take the device from its current holder instead of\n"
                 "                  refusing; off by default so a running vendor daemon\n"
                 "                  is never disturbed\n"
                 "  --service-name  Mach service to listen on (default %s)\n"
                 "  --once          claim, log, release, exit (no listener, no install)\n",
                 kDefaultServiceName);
}

std::string StateLine(const char* what, const openscanner::helper::Reply& reply) {
    return std::string(what) + ": ok=" + (reply.ok ? "true" : "false") + " state=" +
           openscanner::helper::ClaimStateName(reply.state) + " (" + reply.detail + ")";
}

}  // namespace

int main(int argc, char** argv) {
    std::string claim_kind = "iokit";
    std::string service_name = kDefaultServiceName;
    bool seize = false;
    bool once = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            Usage();
            return 0;
        } else if (arg == "--once") {
            once = true;
        } else if (arg == "--seize") {
            seize = true;
        } else if (arg == "--claim" && i + 1 < argc) {
            claim_kind = argv[++i];
        } else if (arg == "--service-name" && i + 1 < argc) {
            service_name = argv[++i];
        } else {
            std::fprintf(stderr, "helper: unknown argument '%s'\n", arg.c_str());
            Usage();
            return 2;
        }
    }

    g_log = os_log_create("org.openscanner.helper", "lifecycle");

    std::unique_ptr<openscanner::helper::DeviceClaim> claim;
    if (claim_kind == "null") {
        claim = std::make_unique<openscanner::helper::NullDeviceClaim>();
    } else if (claim_kind == "iokit") {
        claim = openscanner::helper::MakeIokitDeviceClaim(seize);
    } else {
        std::fprintf(stderr, "helper: unknown --claim backend '%s'\n", claim_kind.c_str());
        return 2;
    }

    openscanner::helper::HelperService service(std::move(claim), &LogLine);

    if (once) {
        const openscanner::helper::Reply started = service.Start();
        LogLine(StateLine("once: claim", started));
        const openscanner::helper::Reply stopped = service.Stop();
        LogLine(StateLine("once: release", stopped));
        return started.ok ? 0 : 1;
    }

    std::string listener_error;
    std::unique_ptr<openscanner::helper::Listener> listener =
        openscanner::helper::MakeMachServiceListener(service_name.c_str(), &service, &listener_error);
    if (!listener) {
        LogLine("start: no XPC listener: " + listener_error);
        return 4;
    }

    // Claim on start. A refusal is logged and served: the app can ask for a
    // claim once the obstruction (usually the vendor daemon) is gone.
    const openscanner::helper::Reply started = service.Start();
    LogLine(StateLine("start: claim", started));

    signal(SIGTERM, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    dispatch_queue_t signals = dispatch_queue_create("org.openscanner.helper.signals", DISPATCH_QUEUE_SERIAL);
    dispatch_source_t sources[2] = {
        dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGTERM, 0, signals),
        dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGINT, 0, signals),
    };
    openscanner::helper::HelperService* service_ptr = &service;
    for (dispatch_source_t source : sources) {
        if (!source) continue;
        dispatch_source_set_event_handler(source, ^{
            LogLine("quit: signal");
            const openscanner::helper::Reply stopped = service_ptr->Stop();
            LogLine(StateLine("quit: release", stopped));
            std::exit(stopped.ok ? 0 : 1);
        });
        dispatch_resume(source);
    }

    LogLine("start: serving (no data path; this build carries no streaming)");
    // XPC, the signal sources and the USB interest handler each have their own
    // queue; the main thread only has to stay alive.
    dispatch_main();
    return 0;
}
