// Streams a synthetic, animated point cloud over a local WebSocket so the
// web UI's rendering/latency pipeline can be built and measured before it
// is wired to a real scan session. Swap the frame source for real OBScan
// capture data later; the wire format and transport stay the same.
//
// Usage: scan_server [port]   (default port: 8787)

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "openscanner/ws_broadcast_server.h"

namespace {

constexpr int kPointGridSize = 48;        // kPointGridSize^2 points on a sphere.
constexpr double kFrameIntervalMs = 1000.0 / 30.0;  // ~30 Hz.

struct Vec3 {
    float x, y, z;
};

std::vector<Vec3> MakeSpherePoints(int grid_size) {
    std::vector<Vec3> points;
    points.reserve(static_cast<size_t>(grid_size) * grid_size);
    for (int i = 0; i < grid_size; ++i) {
        double theta = M_PI * (static_cast<double>(i) + 0.5) / grid_size;  // 0..pi
        for (int j = 0; j < grid_size; ++j) {
            double phi = 2.0 * M_PI * static_cast<double>(j) / grid_size;  // 0..2pi
            float x = static_cast<float>(std::sin(theta) * std::cos(phi));
            float y = static_cast<float>(std::cos(theta));
            float z = static_cast<float>(std::sin(theta) * std::sin(phi));
            points.push_back({x, y, z});
        }
    }
    return points;
}

void RotateY(std::vector<Vec3>* points, double angle_rad) {
    float c = static_cast<float>(std::cos(angle_rad));
    float s = static_cast<float>(std::sin(angle_rad));
    for (auto& p : *points) {
        float x = p.x * c + p.z * s;
        float z = -p.x * s + p.z * c;
        p.x = x;
        p.z = z;
    }
}

double NowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(system_clock::now().time_since_epoch()).count();
}

void AppendU32(std::vector<uint8_t>* buf, uint32_t v) {
    buf->push_back(static_cast<uint8_t>(v & 0xFF));
    buf->push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf->push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf->push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void AppendF64(std::vector<uint8_t>* buf, double v) {
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(v), "double must be 8 bytes");
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i) {
        buf->push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
    }
}

void AppendF32(std::vector<uint8_t>* buf, float v) {
    uint32_t bits;
    static_assert(sizeof(bits) == sizeof(v), "float must be 4 bytes");
    std::memcpy(&bits, &v, sizeof(bits));
    AppendU32(buf, bits);
}

}  // namespace

int main(int argc, char** argv) {
    uint16_t port = argc > 1 ? static_cast<uint16_t>(std::atoi(argv[1])) : 8787;

    openscanner::WsBroadcastServer server;
    if (!server.Start(port)) {
        std::fprintf(stderr, "Failed to start WebSocket server on port %u (port in use?)\n", port);
        return 1;
    }
    std::printf("Streaming synthetic point-cloud frames on ws://0.0.0.0:%u\n", port);
    std::printf("Open ui/index.html in a browser to view it.\n");

    std::vector<Vec3> base_points = MakeSpherePoints(kPointGridSize);
    uint32_t frame_id = 0;
    double angle = 0.0;

    while (true) {
        auto tick_start = std::chrono::steady_clock::now();

        std::vector<Vec3> frame_points = base_points;
        RotateY(&frame_points, angle);
        angle += 0.02;

        std::vector<uint8_t> buf;
        buf.reserve(4 + 8 + 4 + frame_points.size() * 3 * 4);
        AppendU32(&buf, frame_id++);
        AppendF64(&buf, NowMs());
        AppendU32(&buf, static_cast<uint32_t>(frame_points.size()));
        for (const auto& p : frame_points) {
            AppendF32(&buf, p.x);
            AppendF32(&buf, p.y);
            AppendF32(&buf, p.z);
        }

        if (server.ConnectedClientCount() > 0) {
            server.BroadcastBinary(buf.data(), buf.size());
        }

        auto elapsed = std::chrono::steady_clock::now() - tick_start;
        auto sleep_for = std::chrono::milliseconds(static_cast<int64_t>(kFrameIntervalMs)) -
                          std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        if (sleep_for > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(sleep_for);
        }
    }
}
