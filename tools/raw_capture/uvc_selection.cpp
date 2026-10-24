// The UVC alternate-setting choice (row #197). See uvc_selection.h.

#include "uvc_selection.h"

#include <algorithm>

namespace raw_capture {
namespace {

const ProbeInterface* FindAlternate(const std::vector<ProbeInterface>& interfaces, int interface_number,
                                    int alternate_setting) {
    for (const ProbeInterface& interface : interfaces) {
        if (interface.number == interface_number && interface.alternate_setting == alternate_setting) {
            return &interface;
        }
    }
    return nullptr;
}

bool CarriesEndpoint(const ProbeInterface& interface, int endpoint_address) {
    for (const ProbeEndpoint& endpoint : interface.endpoints) {
        if (endpoint.address == endpoint_address) return true;
    }
    return false;
}

bool HasInEndpoint(const ProbeInterface& interface) {
    for (const ProbeEndpoint& endpoint : interface.endpoints) {
        if ((endpoint.address & 0x80) != 0) return true;
    }
    return false;
}

std::string AlternatesText(const std::vector<const ProbeInterface*>& alternates) {
    std::string text;
    for (const ProbeInterface* interface : alternates) {
        if (!text.empty()) text += ", ";
        text += "alt " + std::to_string(interface->alternate_setting) + " (" +
                std::to_string(interface->endpoints.size()) + " endpoint(s))";
    }
    return text;
}

}  // namespace

AlternateSelection ChooseStreamingAlternate(const std::vector<ProbeInterface>& interfaces,
                                            int interface_number, int endpoint_address,
                                            int current_alternate, int preferred_alternate) {
    AlternateSelection selection;
    std::vector<const ProbeInterface*> alternates;
    for (const ProbeInterface& interface : interfaces) {
        if (interface.number == interface_number) alternates.push_back(&interface);
    }
    if (alternates.empty()) {
        selection.error = "interface " + std::to_string(interface_number) +
                          " is not in the configuration descriptor";
        return selection;
    }
    const ProbeInterface* current =
        FindAlternate(interfaces, interface_number, current_alternate);
    selection.current_carries_endpoint =
        current != nullptr && CarriesEndpoint(*current, endpoint_address);

    std::vector<const ProbeInterface*> with_endpoint;
    for (const ProbeInterface* interface : alternates) {
        if (CarriesEndpoint(*interface, endpoint_address)) with_endpoint.push_back(interface);
    }
    if (with_endpoint.empty()) {
        selection.error = "no alternate setting of interface " + std::to_string(interface_number) +
                          " carries endpoint 0x" + std::to_string(endpoint_address) + " (" +
                          AlternatesText(alternates) + ")";
        return selection;
    }

    const ProbeInterface* chosen = nullptr;
    const ProbeInterface* preferred = FindAlternate(interfaces, interface_number, preferred_alternate);
    if (preferred != nullptr && CarriesEndpoint(*preferred, endpoint_address)) {
        chosen = preferred;
        if (current_alternate == chosen->alternate_setting) {
            selection.note = "the interface is already at alt " +
                             std::to_string(chosen->alternate_setting) + ", which carries the endpoint";
        }
    } else {
        if (preferred != nullptr) {
            selection.note = "the table's alt " + std::to_string(preferred_alternate) +
                             " does not carry endpoint 0x" + std::to_string(endpoint_address) +
                             "; using the alternate setting that does";
        }
        // Prefer an IN alternate (streaming data) over an OUT one.
        for (const ProbeInterface* interface : with_endpoint) {
            if (HasInEndpoint(*interface) && CarriesEndpoint(*interface, endpoint_address)) {
                chosen = interface;
                break;
            }
        }
        if (chosen == nullptr) chosen = with_endpoint.front();
    }

    selection.alternate_setting = chosen->alternate_setting;
    selection.needs_select = chosen->alternate_setting != current_alternate;
    if (!selection.needs_select && selection.note.empty()) {
        selection.note = "the interface is already at alt " +
                         std::to_string(chosen->alternate_setting) + ", which carries the endpoint";
    }
    return selection;
}

int SelectInterfaceCandidate(const std::vector<InterfaceCandidate>& candidates, long want_vendor,
                             long want_product, long want_interface) {
    for (size_t i = 0; i < candidates.size(); ++i) {
        const InterfaceCandidate& candidate = candidates[i];
        if (candidate.vendor_id == want_vendor && candidate.product_id == want_product &&
            candidate.interface_number == want_interface) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace raw_capture
