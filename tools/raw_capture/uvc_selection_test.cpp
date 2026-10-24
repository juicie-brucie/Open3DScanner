// Row #197's device-free test: choosing the UVC streaming alternate setting.
// The live device's alt-0 SET_INTERFACE was refused; the logic under test is
// that the alt carrying the endpoint is chosen and a redundant SET_INTERFACE
// is not sent.
#include "uvc_selection.h"

#include <cstdio>
#include <string>
#include <vector>

namespace rc = raw_capture;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

rc::ProbeEndpoint Endpoint(int address, rc::TransferType transfer = rc::TransferType::kBulk,
                           int max_packet = 1024) {
    rc::ProbeEndpoint endpoint;
    endpoint.address = address;
    endpoint.transfer = transfer;
    endpoint.max_packet_size = max_packet;
    return endpoint;
}

rc::ProbeInterface Alternate(int number, int alternate_setting, std::vector<rc::ProbeEndpoint> endpoints) {
    rc::ProbeInterface interface;
    interface.number = number;
    interface.alternate_setting = alternate_setting;
    interface.class_code = 0x0E;
    interface.subclass = 0x02;
    interface.video_streaming = true;
    interface.endpoints = std::move(endpoints);
    return interface;
}

}  // namespace

int main() {
    // --- the endpoint is on alt 0 and the interface is already there ------
    {
        const std::vector<rc::ProbeInterface> interfaces = {
            Alternate(1, 0, {Endpoint(0x81)}),
        };
        const rc::AlternateSelection selection =
            rc::ChooseStreamingAlternate(interfaces, 1, 0x81, /*current=*/0, /*preferred=*/0);
        Check(selection.ok(), "alt 0 with the endpoint is a valid choice");
        Check(selection.alternate_setting == 0, "it chooses alt 0");
        Check(!selection.needs_select, "no redundant SET_INTERFACE at the current alt 0");
        Check(selection.note.find("already") != std::string::npos, "the note says it is already there");
    }

    // --- the streaming alt is 1, the table's alt is 0 ---------------------
    {
        const std::vector<rc::ProbeInterface> interfaces = {
            Alternate(1, 0, {}),
            Alternate(1, 1, {Endpoint(0x81)}),
        };
        const rc::AlternateSelection selection =
            rc::ChooseStreamingAlternate(interfaces, 1, 0x81, /*current=*/0, /*preferred=*/0);
        Check(selection.ok(), "the streaming alt is found");
        Check(selection.alternate_setting == 1, "it chooses alt 1, which carries 0x81");
        Check(selection.needs_select, "SET_INTERFACE(1) is needed");
        Check(selection.note.find("does not carry") != std::string::npos,
              "the note says the table's alt lacks the endpoint");
    }

    // --- already on the streaming alt -------------------------------------
    {
        const std::vector<rc::ProbeInterface> interfaces = {
            Alternate(1, 0, {}),
            Alternate(1, 1, {Endpoint(0x81)}),
        };
        const rc::AlternateSelection selection =
            rc::ChooseStreamingAlternate(interfaces, 1, 0x81, /*current=*/1, /*preferred=*/1);
        Check(selection.ok() && selection.alternate_setting == 1, "the current streaming alt is chosen");
        Check(!selection.needs_select, "no SET_INTERFACE when already on it");
    }

    // --- the table names the wrong alt but the endpoint decides -----------
    {
        const std::vector<rc::ProbeInterface> interfaces = {
            Alternate(3, 0, {Endpoint(0x82)}),
            Alternate(3, 1, {}),
        };
        const rc::AlternateSelection selection =
            rc::ChooseStreamingAlternate(interfaces, 3, 0x82, /*current=*/1, /*preferred=*/1);
        Check(selection.ok() && selection.alternate_setting == 0, "the endpoint's alt wins over the table");
        Check(selection.needs_select, "SET_INTERFACE(0) is needed from alt 1");
    }

    // --- no alternate carries the endpoint --------------------------------
    {
        const std::vector<rc::ProbeInterface> interfaces = {
            Alternate(1, 0, {}),
            Alternate(1, 1, {Endpoint(0x01, rc::TransferType::kBulk)}),
        };
        const rc::AlternateSelection selection =
            rc::ChooseStreamingAlternate(interfaces, 1, 0x81, 0, 0);
        Check(!selection.ok(), "no alt with 0x81 is an error");
        Check(selection.error.find("0x") != std::string::npos, "the error names the endpoint");
        Check(selection.error.find("alt 0") != std::string::npos, "the error lists the alternates");
    }

    // --- the interface is not in the configuration ------------------------
    {
        const std::vector<rc::ProbeInterface> interfaces = {Alternate(1, 0, {Endpoint(0x81)})};
        const rc::AlternateSelection selection =
            rc::ChooseStreamingAlternate(interfaces, 9, 0x81, 0, 0);
        Check(!selection.ok(), "a missing interface is an error");
        Check(selection.error.find("interface 9") != std::string::npos, "the error names the interface");
    }

    // --- prefer an IN alternate when several carry the endpoint -----------
    {
        const std::vector<rc::ProbeInterface> interfaces = {
            Alternate(2, 0, {Endpoint(0x02, rc::TransferType::kBulk)}),   // OUT
            Alternate(2, 1, {Endpoint(0x82, rc::TransferType::kBulk)}),   // IN
        };
        const rc::AlternateSelection selection =
            rc::ChooseStreamingAlternate(interfaces, 2, 0x82, 0, 0);
        Check(selection.ok() && selection.alternate_setting == 1, "the IN alt carrying the endpoint wins");
    }

    // --- row #198: selecting the IOUSBHostInterface node among the enumerated
    //     candidates (the in-process replacement for the broken matching
    //     dictionary). The scanner publishes seven interfaces under one
    //     vid/pid, enumerated alongside a keyboard and a trackpad. ----------
    {
        const long kVid = 0x2BC5, kPid = 0x0C03;
        const std::vector<rc::InterfaceCandidate> candidates = {
            {0x05AC, 0x029F, 0},  // keyboard interface 0
            {kVid, kPid, 0},  {kVid, kPid, 1}, {kVid, kPid, 2}, {kVid, kPid, 3},
            {kVid, kPid, 4},  {kVid, kPid, 5}, {kVid, kPid, 6},
            {0x05AC, 0x0265, 1},  // trackpad interface 1
        };
        Check(rc::SelectInterfaceCandidate(candidates, kVid, kPid, 1) == 2,
              "interface 1 is the scanner's node, not the keyboard's interface 0/1");
        Check(rc::SelectInterfaceCandidate(candidates, kVid, kPid, 3) == 4,
              "interface 3 (the 960x600 IR stream) selects the right node");
        Check(rc::SelectInterfaceCandidate(candidates, kVid, kPid, 6) == 7,
              "the HID interface 6 selects the last scanner node");
        Check(rc::SelectInterfaceCandidate(candidates, kVid, kPid, 9) == -1,
              "an interface number no node carries is no match");
        Check(rc::SelectInterfaceCandidate(candidates, 0x1234, 0x5678, 1) == -1,
              "a different device is no match even at a present interface number");
    }

    // --- an absent property (-1) never matches a real wanted id -----------
    {
        const std::vector<rc::InterfaceCandidate> candidates = {
            {-1, -1, -1},            // a node that published none of the three
            {0x2BC5, 0x0C03, 1},
        };
        Check(rc::SelectInterfaceCandidate(candidates, 0x2BC5, 0x0C03, 1) == 1,
              "the populated node matches, not the empty one");
        Check(rc::SelectInterfaceCandidate({}, 0x2BC5, 0x0C03, 1) == -1,
              "no candidates is no match");
    }

    std::printf("[raw-capture-selection] %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
