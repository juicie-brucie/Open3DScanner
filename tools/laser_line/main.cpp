// laser_line_report — the recorded-frame pass for the laser-line module
// (row #205, split out of engine/src/recon/laser_line_test.cpp).
//
// The RCRP framing is read by the shared parser, tools/raptor_client/capture.h
// (capture::Reader); this tool adds only the reply-payload extraction (the
// get_frame envelope's Bin(2304000) at element 4) and the per-row metrics,
// tracking and PGM dump. The module itself (engine/src/recon/laser_line.h)
// stays I/O-free.
//
// Geometry: the #132 RPC IR frame is 1920x1200x1, stride 1920 (finding #194,
// docs/recon/findings/frame-geometry.md §2). This tool is for that recorded
// frame; a live/band source would need a size-agnostic path.

#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "capture.h"
#include "recon/laser_line.h"

namespace recon = openscanner::recon;

namespace {

constexpr uint32_t kFrameBytes = 2304000;  // 1920 * 1200 * 1
constexpr int kWidth = 1920;
constexpr int kHeight = 1200;
constexpr int kStride = 1920;

uint32_t Be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// The get_frame reply's 11-element array carries a Bin(2304000) at element 4.
// The envelope is our own client's; the first bin32 of that exact length is the
// frame (its marker sits in the reply's 49-byte prefix).
const uint8_t* FindFrameBin(const std::vector<uint8_t>& reply, uint32_t* len) {
    for (size_t i = 0; i + 5 <= reply.size(); ++i) {
        if (reply[i] != 0xc6) continue;  // bin32
        const uint32_t n = Be32(reply.data() + i + 1);
        if (n == kFrameBytes && i + 5 + n <= reply.size()) {
            *len = n;
            return reply.data() + i + 5;
        }
    }
    return nullptr;
}

// capture::Reader::Open takes the bundle directory (it appends /replies).
// Accept the bundle or its replies/ directory, so `--report` works with either
// the manifest's directory or the path the bundle docs show.
std::string ResolveBundleDir(const std::string& path) {
    struct stat st;
    if (stat((path + "/replies").c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return path;
    const size_t slash = path.find_last_of('/');
    const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    if (base == "replies") return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
    return path;  // Reader::Open reports the missing replies/ directory
}

struct Track {
    int last_y = 0;
    int last_x = 0;
    int length = 0;  // rows that carried this line
    int gaps = 0;    // missed rows inside its span
    std::vector<std::pair<int, int>> points;  // (y, rounded x), for the dump
};

struct FrameMetrics {
    int centres = 0;
    int rows_with_line = 0;
    int rows_with_two = 0;
    int gap_rows = 0;       // rows without a line between the first and last
    int spanned_rows = 0;
    int longest_track = 0;
    int long_tracks = 0;     // tracks spanning >= 20 rows: a persistent line
    int long_rows = 0;       // rows covered by those tracks
    int long_gaps = 0;       // missed rows inside them (breaks)
    int short_centres = 0;   // detections in tracks shorter than 5 rows: speckle
    int strength_hist[5] = {};  // 10-20, 20-40, 40-80, 80-160, 160+
    std::vector<std::pair<int, int>> long_points;
};

FrameMetrics MeasureFrame(const recon::Result& result, int height) {
    FrameMetrics m;
    m.centres = result.centre_count;
    m.rows_with_line = result.rows_with_line;
    int first = -1, last = -1;
    std::vector<Track> tracks;
    for (int y = 0; y < height; ++y) {
        const auto& centres = result.rows[static_cast<size_t>(y)].centres;
        if (!centres.empty()) {
            if (first < 0) first = y;
            last = y;
            if (centres.size() > 1) ++m.rows_with_two;
        }
        for (const recon::LineCenter& c : centres) {
            const int bucket = c.strength < 20 ? 0 : c.strength < 40 ? 1 : c.strength < 80 ? 2
                               : c.strength < 160 ? 3 : 4;
            ++m.strength_hist[bucket];
        }
        std::vector<bool> matched(tracks.size(), false);
        for (const recon::LineCenter& c : centres) {
            int best = -1;
            int best_dx = 1 << 30;
            const int cx = static_cast<int>(std::lround(c.x));
            for (size_t t = 0; t < tracks.size(); ++t) {
                if (matched[t]) continue;
                const int dy = y - tracks[t].last_y;
                if (dy < 1 || dy > 3) continue;  // allow a two-row gap
                const int dx = std::abs(cx - tracks[t].last_x);
                if (dx > 4 * dy + 2) continue;
                if (dx < best_dx) {
                    best_dx = dx;
                    best = static_cast<int>(t);
                }
            }
            if (best >= 0) {
                matched[static_cast<size_t>(best)] = true;
                Track& tr = tracks[static_cast<size_t>(best)];
                tr.gaps += (y - tr.last_y - 1);
                tr.last_y = y;
                tr.last_x = cx;
                ++tr.length;
                tr.points.emplace_back(y, cx);
            } else {
                Track tr;
                tr.last_y = y;
                tr.last_x = cx;
                tr.length = 1;
                tr.points.emplace_back(y, cx);
                tracks.push_back(std::move(tr));
                matched.push_back(true);
            }
        }
    }
    for (const Track& t : tracks) {
        m.longest_track = std::max(m.longest_track, t.length);
        if (t.length >= 20) {
            ++m.long_tracks;
            m.long_rows += t.length;
            m.long_gaps += t.gaps;
            m.long_points.insert(m.long_points.end(), t.points.begin(), t.points.end());
        }
        if (t.length < 5) m.short_centres += t.length;
    }
    if (first >= 0 && last >= first) {
        m.spanned_rows = last - first + 1;
        int with_line = 0;
        for (int y = first; y <= last; ++y) {
            if (!result.rows[static_cast<size_t>(y)].centres.empty()) ++with_line;
        }
        m.gap_rows = m.spanned_rows - with_line;
    }
    return m;
}

bool WriteOverlayPgm(const std::string& path, const recon::FrameView& frame,
                     const std::vector<std::pair<int, int>>& marks, std::string* err) {
    std::vector<uint8_t> out(static_cast<size_t>(frame.width) * frame.height);
    for (int y = 0; y < frame.height; ++y) {
        const uint8_t* src = frame.data + static_cast<size_t>(y) * frame.stride;
        for (int x = 0; x < frame.width; ++x) out[static_cast<size_t>(y) * frame.width + x] = src[x] / 2;
    }
    for (const auto& mark : marks) {
        const int cx = mark.second, cy = mark.first;
        for (int dy = -1; dy <= 1; ++dy) {
            const int yy = cy + dy;
            if (yy < 0 || yy >= frame.height) continue;
            for (int dx = -1; dx <= 1; ++dx) {
                const int xx = cx + dx;
                if (xx < 0 || xx >= frame.width) continue;
                out[static_cast<size_t>(yy) * frame.width + xx] = 255;
            }
        }
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    f << "P5\n" << frame.width << " " << frame.height << "\n255\n";
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!f) {
        if (err) *err = "cannot write " + path;
        return false;
    }
    return true;
}

void Usage() {
    std::printf(
        "usage: laser_line_report --report <bundle-or-replies-dir> [--port N] [--frames N]\n"
        "                          [--dump out.pgm] [--dump-frame N]\n"
        "  --report       an RCRP bundle dir (with replies/) or its replies/ dir\n"
        "  --port         the capture port to read (default 10013, IR)\n"
        "  --frames N     process the first N get_frame replies (default: all)\n"
        "  --dump FILE    write a PGM of frame --dump-frame with persistent-line\n"
        "                 centres overlaid (image halved)\n"
        "  --dump-frame N the frame index to dump (default 0)\n");
}

int RunReport(const std::string& path, uint16_t port, int max_frames, const std::string& dump_path,
              int dump_frame) {
    const std::string bundle = ResolveBundleDir(path);
    std::printf("== recorded frames ==\n");
    std::printf("bundle: %s (port %u)\n", bundle.c_str(), static_cast<unsigned>(port));

    capture::Reader reader;
    std::string err;
    if (!reader.Open(bundle, &err)) {
        std::fprintf(stderr, "[laser-line-report] %s\n", err.c_str());
        return 1;
    }

    uint64_t records = 0, frames = 0, processed = 0;
    long long total_centres = 0, total_rows = 0, total_two = 0, total_gap = 0, total_span = 0;
    long long total_long_tracks = 0, total_long_rows = 0, total_long_gaps = 0, total_short = 0;
    int longest_track = 0, min_rows = 1 << 30, max_rows = 0;
    bool dumped = false;
    capture::Record rec;
    while (reader.Next(&rec)) {
        ++records;
        if (rec.port != port || rec.method.rfind("ob_rpc_get_frame", 0) != 0) continue;
        const uint64_t frame_index = frames++;
        if (max_frames > 0 && frame_index >= static_cast<uint64_t>(max_frames)) continue;

        uint32_t frame_len = 0;
        const uint8_t* frame_data = FindFrameBin(rec.bytes, &frame_len);
        if (!frame_data) {
            std::fprintf(stderr, "[laser-line-report] frame %llu carries no Bin(%u)\n",
                         static_cast<unsigned long long>(frame_index), kFrameBytes);
            continue;
        }
        recon::FrameView view{frame_data, kWidth, kHeight, kStride};
        const recon::Result result = recon::ExtractLaserLines(view);
        const FrameMetrics m = MeasureFrame(result, view.height);
        ++processed;
        total_centres += m.centres;
        total_rows += m.rows_with_line;
        total_two += m.rows_with_two;
        total_gap += m.gap_rows;
        total_span += m.spanned_rows;
        total_long_tracks += m.long_tracks;
        total_long_rows += m.long_rows;
        total_long_gaps += m.long_gaps;
        total_short += m.short_centres;
        longest_track = std::max(longest_track, m.longest_track);
        min_rows = std::min(min_rows, m.rows_with_line);
        max_rows = std::max(max_rows, m.rows_with_line);
        if (frame_index < 5) {
            std::printf("frame %llu: centres=%d rows_with_line=%d/%d (%.1f%%) two_lines_rows=%d "
                        "long_tracks=%d long_rows=%d longest_track=%d "
                        "strength[10-20,20-40,40-80,80-160,160+]= %d/%d/%d/%d/%d\n",
                        static_cast<unsigned long long>(frame_index), m.centres, m.rows_with_line, kHeight,
                        100.0 * m.rows_with_line / kHeight, m.rows_with_two, m.long_tracks, m.long_rows,
                        m.longest_track, m.strength_hist[0], m.strength_hist[1], m.strength_hist[2],
                        m.strength_hist[3], m.strength_hist[4]);
        }
        if (!dump_path.empty() && frame_index == static_cast<uint64_t>(dump_frame) && !dumped) {
            if (WriteOverlayPgm(dump_path, view, m.long_points, &err)) {
                std::printf("dump: %s (frame %llu; persistent-line centres marked white, image halved)\n",
                            dump_path.c_str(), static_cast<unsigned long long>(frame_index));
                dumped = true;
            } else {
                std::fprintf(stderr, "[laser-line-report] dump: %s\n", err.c_str());
            }
        }
    }
    if (!reader.error().empty()) {
        std::fprintf(stderr, "[laser-line-report] %s\n", reader.error().c_str());
        return 1;
    }
    std::printf("records=%llu get_frame=%llu (port %u)\n", static_cast<unsigned long long>(records),
                static_cast<unsigned long long>(frames), static_cast<unsigned>(port));
    if (processed == 0) {
        std::fprintf(stderr, "[laser-line-report] no frames processed\n");
        return 1;
    }

    const double rows_mean = static_cast<double>(total_rows) / processed;
    const double centres_mean = static_cast<double>(total_centres) / processed;
    const double gap_rate = total_span > 0 ? static_cast<double>(total_gap) / total_span : 0.0;
    std::printf("== aggregate over %llu frame(s) ==\n", static_cast<unsigned long long>(processed));
    std::printf("rows_with_line: mean=%.1f/%d (%.1f%%) min=%d max=%d\n", rows_mean, kHeight,
                100.0 * rows_mean / kHeight, min_rows, max_rows);
    std::printf("centres: mean=%.1f/frame, %.2f per row-with-line\n", centres_mean,
                total_rows > 0 ? static_cast<double>(total_centres) / total_rows : 0.0);
    std::printf("rows with two or more lines: mean=%.1f/frame\n",
                static_cast<double>(total_two) / processed);
    std::printf("continuity: raw internal gap rows=%lld/%lld (%.1f%% of spanned rows), "
                "longest track=%d rows\n",
                static_cast<long long>(total_gap), static_cast<long long>(total_span), 100.0 * gap_rate,
                longest_track);
    std::printf("persistent lines (>=20 rows): mean=%.1f tracks/frame, rows covered mean=%.1f/%d, "
                "breaks inside them=%lld rows\n",
                static_cast<double>(total_long_tracks) / processed,
                static_cast<double>(total_long_rows) / processed, kHeight,
                static_cast<long long>(total_long_gaps));
    std::printf("persistence filter: %.1f%% of raw centres are in tracks >=20 rows; "
                "short-lived speckle (<5 rows)=%.1f centres/frame\n",
                total_centres > 0 ? 100.0 * static_cast<double>(total_long_rows) / total_centres : 0.0,
                static_cast<double>(total_short) / processed);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string report_path, dump_path;
    int port = 10013;
    int frames = 0;
    int dump_frame = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--report" && i + 1 < argc) {
            report_path = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--frames" && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (arg == "--dump" && i + 1 < argc) {
            dump_path = argv[++i];
        } else if (arg == "--dump-frame" && i + 1 < argc) {
            dump_frame = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            Usage();
            return 0;
        } else {
            std::fprintf(stderr, "laser_line_report: unknown argument '%s'\n", arg.c_str());
            Usage();
            return 2;
        }
    }
    if (report_path.empty()) {
        Usage();
        return 2;
    }
    if (port <= 0 || port > 65535) {
        std::fprintf(stderr, "laser_line_report: --port wants 1..65535 (got %d)\n", port);
        return 2;
    }
    if (dump_frame < 0) {
        std::fprintf(stderr, "laser_line_report: --dump-frame must not be negative\n");
        return 2;
    }
    return RunReport(report_path, static_cast<uint16_t>(port), frames, dump_path, dump_frame);
}
