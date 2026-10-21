#pragma once
// Row #179 — which daemon the host talks to.
//
//   vendor  the Creality RPCServer daemon on its own ports (protocol-spec §1.1).
//           This is the default and the product's current behaviour.
//   shim    our own tools/raptor_shim on a separate pair of ports, so both can
//           listen at once and the switch is the only thing that changes.
//
// The switch is explicit: a name, parsed exactly, logged, and never inferred.
// Nothing in this header (or the code that reads it) starts, stops or replaces
// a daemon — the operator or the installer does that. Selecting the shim only
// changes the port map the caller dials.
//
// The product does not read this yet: with the switch absent every caller is
// on `vendor`, which is today's behaviour to the byte. The tools
// (raptor_client --daemon shim) are its first consumer.

#include <cstdint>
#include <string>

namespace openscanner {

inline constexpr uint16_t kVendorIrPort = 10013;
inline constexpr uint16_t kVendorDepthPort = 10010;

// The shim's own defaults (tools/raptor_shim --ir-port / --depth-port). They
// are deliberately not the vendor's ports: a switch that needs the vendor
// daemon stopped first would be a replacement, not a switch.
inline constexpr uint16_t kShimIrPort = 20013;
inline constexpr uint16_t kShimDepthPort = 20010;

enum class DaemonKind { kVendor, kShim };

struct DaemonSelection {
    DaemonKind kind = DaemonKind::kVendor;
    uint16_t ir_port = kVendorIrPort;
    uint16_t depth_port = kVendorDepthPort;

    bool is_shim() const { return kind == DaemonKind::kShim; }
    const char* name() const { return kind == DaemonKind::kShim ? "shim" : "vendor"; }
};

inline DaemonSelection VendorDaemon() { return DaemonSelection{}; }

inline DaemonSelection ShimDaemon() {
    DaemonSelection selection;
    selection.kind = DaemonKind::kShim;
    selection.ir_port = kShimIrPort;
    selection.depth_port = kShimDepthPort;
    return selection;
}

// "vendor" or "shim", exactly. Returns false for anything else — a typo is a
// refusal, not a silent fall back to the vendor daemon.
inline bool ParseDaemonName(const std::string& name, DaemonSelection* out) {
    if (name == "vendor" || name == "shim") {
        if (out) *out = (name == "shim") ? ShimDaemon() : VendorDaemon();
        return true;
    }
    return false;
}

// The switch's one log line: what was selected, its ports, and the fact that
// the other daemon was not touched.
inline std::string DescribeDaemon(const DaemonSelection& selection) {
    if (selection.is_shim()) {
        return "daemon=shim (OpenScanner RPC shim, IR " + std::to_string(selection.ir_port) + " / depth " +
               std::to_string(selection.depth_port) +
               "; the vendor daemon is neither stopped nor replaced)";
    }
    return "daemon=vendor (Creality RPCServer, IR " + std::to_string(selection.ir_port) + " / depth " +
           std::to_string(selection.depth_port) + "; the product default)";
}

// The flag's accepted values, for a usage/error line.
inline const char* DaemonNames() { return "vendor|shim"; }

}  // namespace openscanner
