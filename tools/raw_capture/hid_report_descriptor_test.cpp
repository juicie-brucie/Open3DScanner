// Row #189's device-free test: the HID report-descriptor parser. The fixture
// is synthetic (an input report id 5 and an output report id 6, vendor usage
// page) — the device's 25 bytes are the operator's probe run.
#include "hid_report_descriptor.h"
#include "descriptor_probe.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace rc = raw_capture;
namespace fs = std::filesystem;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

// Usage page 0xFF00; report id 5: input (8 bits x 8); report id 6: output
// (8 bits x 8). 29 synthetic bytes.
const uint8_t kWithOutput[] = {
    0x06, 0x00, 0xFF,              // usage page (vendor)
    0x09, 0x01,                    // usage 1
    0xA1, 0x01,                    // collection (application)
    0x85, 0x05,                    // report id 5
    0x15, 0x00,                    // logical minimum 0
    0x26, 0xFF, 0x00,              // logical maximum 255
    0x75, 0x08,                    // report size 8
    0x95, 0x08,                    // report count 8
    0x09, 0x01,                    // usage 1
    0x81, 0x02,                    // input (data, variable, absolute)
    0x85, 0x06,                    // report id 6
    0x09, 0x02,                    // usage 2
    0x91, 0x02,                    // output (data, variable, absolute)
    0xC0,                          // end collection
};

const uint8_t kInputOnly[] = {
    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x05, 0x75, 0x08,
    0x95, 0x08, 0x09, 0x01, 0x81, 0x02, 0xC0,
};

const rc::HidField* FindField(const rc::HidReportDescriptor& descriptor, rc::HidFieldType type) {
    for (const auto& field : descriptor.fields) {
        if (field.type == type) return &field;
    }
    return nullptr;
}

}  // namespace

int main() {
    // --- a descriptor with an output report --------------------------------
    {
        const rc::HidReportDescriptor descriptor =
            rc::ParseHidReportDescriptor(kWithOutput, sizeof(kWithOutput));
        Check(descriptor.ok(), "the synthetic descriptor parses");
        if (!descriptor.ok()) std::printf("  error: %s\n", descriptor.error.c_str());
        Check(descriptor.report_ids.size() == 2 && descriptor.report_ids[0] == 5 &&
                  descriptor.report_ids[1] == 6,
              "the report ids are 5 and 6");
        Check(descriptor.fields.size() == 2, "two fields (input + output)");

        const rc::HidField* input = FindField(descriptor, rc::HidFieldType::kInput);
        Check(input != nullptr && input->report_id == 5 && input->bit_offset == 0 &&
                  input->bit_size == 8 && input->count == 8,
              "the input field is id 5, 8 bits x 8");
        Check(input != nullptr && input->usage_page == 0xFF00 && input->usages.size() == 1 &&
                  input->usages[0] == 0x0001,
              "the input field keeps the vendor usage page and usage");
        Check(input != nullptr && input->logical_min == 0 && input->logical_max == 255,
              "the logical range is read");
        Check(input != nullptr && input->variable && !input->constant, "the input flags are read");

        const rc::HidField* output = FindField(descriptor, rc::HidFieldType::kOutput);
        Check(output != nullptr && output->report_id == 6 && output->bit_offset == 0 &&
                  output->bit_size == 8 && output->count == 8,
              "the output field is id 6, 8 bits x 8");
        Check(descriptor.has_output && !descriptor.output_ambiguous, "the descriptor has one output report");
        Check(descriptor.output_report_id == 6 && descriptor.output_report_bytes == 8,
              "the output report is id 6, 8 data bytes");
        Check(descriptor.reports.size() == 2, "two reports are summarised");
    }

    // --- the printout: raw hex, fields, and the paste line ----------------
    {
        const rc::HidReportDescriptor descriptor =
            rc::ParseHidReportDescriptor(kWithOutput, sizeof(kWithOutput));
        const std::string text = rc::FormatHidReportDescriptor(descriptor, "");
        Check(text.find("descriptor: 29 bytes:") != std::string::npos, "the raw hex is printed");
        Check(text.find("report ids: 5, 6") != std::string::npos, "the report ids are printed");
        Check(text.find("report_id=6 type=output") != std::string::npos, "the output field is printed");
        Check(text.find("control.report_id=6 control.report_length=9") != std::string::npos,
              "the paste line includes the id byte (8 data + 1)");
        Check(text.find("property->field map still needs a usage->#101-property mapping") != std::string::npos,
              "the printout does not claim a property map");
    }

    // --- input-only: the answer that matters ------------------------------
    {
        const rc::HidReportDescriptor descriptor =
            rc::ParseHidReportDescriptor(kInputOnly, sizeof(kInputOnly));
        Check(descriptor.ok(), "the input-only descriptor parses");
        Check(descriptor.has_input && !descriptor.has_output, "it is input-only");
        Check(descriptor.output_report_id == rc::kUnsetValue && !descriptor.output_ambiguous,
              "no output report is named");
        const std::string text = rc::FormatHidReportDescriptor(descriptor, "");
        Check(text.find("output: none") != std::string::npos, "the printout says there is no output report");
        Check(text.find("stays the vendor RPC properties") != std::string::npos,
              "it says the control remains the vendor RPC properties");
    }

    // --- a feature report and a range -------------------------------------
    {
        const uint8_t feature[] = {0x06, 0x00, 0xFF, 0x0A, 0x01, 0x00, 0xB1, 0x02};
        const rc::HidReportDescriptor descriptor =
            rc::ParseHidReportDescriptor(feature, sizeof(feature));
        Check(descriptor.ok() && descriptor.has_feature && !descriptor.has_output,
              "a feature report is recognised");
    }

    // --- malformed descriptors are named ----------------------------------
    {
        const uint8_t truncated[] = {0x06, 0x00};  // usage page needs 3 bytes
        const rc::HidReportDescriptor descriptor =
            rc::ParseHidReportDescriptor(truncated, sizeof(truncated));
        Check(!descriptor.ok(), "a truncated descriptor is refused");
        Check(descriptor.error.find("truncated") != std::string::npos, "the truncation is named");
    }
    {
        const rc::HidReportDescriptor descriptor = rc::ParseHidReportDescriptor(nullptr, 0);
        Check(!descriptor.ok() && descriptor.error.find("empty") != std::string::npos,
              "an empty descriptor is refused by name");
    }

    // --- the file path and its refusal ------------------------------------
    {
        std::vector<uint8_t> bytes;
        std::string error;
        Check(!rc::ReadHidReportFile("/nonexistent/row189/report.bin", &bytes, &error),
              "a missing descriptor file is refused");
        Check(error.find("not-configured") == 0, "the refusal is named");
        Check(error.find("row189") != std::string::npos, "the refusal names the path");
    }
    {
        const fs::path dir = fs::temp_directory_path() / "openscanner-row189-hid";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path path = dir / "report.bin";
        {
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(kInputOnly), sizeof(kInputOnly));
        }
        std::vector<uint8_t> bytes;
        std::string error;
        Check(rc::ReadHidReportFile(path.string(), &bytes, &error), "a saved descriptor reads back");
        Check(bytes.size() == sizeof(kInputOnly), "the saved bytes are complete");

        // The probe prints hex; a file of that text parses the same.
        const fs::path hex_path = dir / "report.txt";
        {
            std::ofstream out(hex_path);
            for (size_t i = 0; i < sizeof(kInputOnly); ++i) {
                char text[4];
                std::snprintf(text, sizeof(text), "%02X ", kInputOnly[i]);
                out << text;
            }
        }
        std::vector<uint8_t> hex_bytes;
        Check(rc::ReadHidReportFile(hex_path.string(), &hex_bytes, &error), "a hex-text descriptor reads back");
        Check(hex_bytes.size() == sizeof(kInputOnly) &&
                  std::memcmp(hex_bytes.data(), kInputOnly, sizeof(kInputOnly)) == 0,
              "the hex text decodes to the same bytes");
        fs::remove_all(dir, ec);
    }

    // --- the not-read printout --------------------------------------------
    {
        const std::string text = rc::FormatHidReportDescriptor(rc::HidReportDescriptor{},
                                                               "not-configured: the read was refused");
        Check(text.find("descriptor: not read (not-configured: the read was refused)") != std::string::npos,
              "a refused read is printed by name");
    }

    // --- the report-id helper the probe still uses ------------------------
    {
        std::string error;
        const std::vector<int> ids =
            rc::ParseHidReportIds(kWithOutput, sizeof(kWithOutput), &error);
        Check(ids.size() == 2 && ids[0] == 5 && ids[1] == 6, "ParseHidReportIds still finds 5 and 6");
    }

    std::printf("[raw-capture-hid-report] %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
