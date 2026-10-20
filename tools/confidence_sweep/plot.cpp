// The sweep's plot (row #159).
//
// An SVG written by hand: the plot has to be reproducible byte for byte and
// to need no plotting library, so the curve can be diffed and committed.

#include "harness.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace openscanner {
namespace confidence {
namespace {

struct Panel {
    std::string label;
    std::string unit;
    std::vector<double> confidence;
    std::vector<double> random_mean;
    std::vector<double> random_low;
    std::vector<double> random_high;
};

std::string num(double v, const char* format = "%.6g") {
    char buf[64];
    std::snprintf(buf, sizeof(buf), format, v);
    return buf;
}

// A log axis: the accuracy spans orders of magnitude between a nearly clean
// cloud and a badly registered one, and a linear axis would flatten the part
// the experiment is about. The floor keeps a zero (or a negative, which only
// the random control's band can produce) on the axis.
double log_pos(double v, double floor_value) {
    if (!(v > floor_value)) return std::log10(floor_value);
    return std::log10(v);
}

std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '&') {
            out += "&amp;";
        } else if (c == '<') {
            out += "&lt;";
        } else if (c == '>') {
            out += "&gt;";
        } else {
            out += c;
        }
    }
    return out;
}

}  // namespace

bool write_svg(const std::string& path, const std::vector<SweepRow>& rows, const std::string& title,
               const std::string& subtitle, std::string* error) {
    if (rows.empty()) {
        if (error) *error = "no sweep rows to plot";
        return false;
    }
    const double width = 940.0;
    const double height = 720.0;
    const double margin_left = 92.0;
    const double margin_right = 24.0;
    const double panel_height = 250.0;
    const double panel_gap = 74.0;
    const double margin_top = 88.0;

    Panel rmse{"RMSE", "m", {}, {}, {}, {}};
    Panel p95{"p95 distance", "m", {}, {}, {}, {}};
    double lo = 1e300, hi = -1e300;
    for (const SweepRow& r : rows) {
        rmse.confidence.push_back(r.rmse);
        rmse.random_mean.push_back(r.rmse_random_mean);
        rmse.random_low.push_back(r.rmse_random_mean - r.rmse_random_std);
        rmse.random_high.push_back(r.rmse_random_mean + r.rmse_random_std);
        p95.confidence.push_back(r.p95);
        p95.random_mean.push_back(r.p95_random_mean);
        p95.random_low.push_back(p95.random_mean.back());
        p95.random_high.push_back(p95.random_mean.back());
        for (double v : {r.rmse, r.rmse_random_mean, r.p95, r.p95_random_mean}) {
            if (v > 0.0) {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        }
    }
    if (!(hi > 0.0)) {
        lo = 1e-6;
        hi = 1e-3;
    }
    const double floor_value = std::max(lo * 0.5, 1e-12);
    const double log_lo = log_pos(floor_value, floor_value);
    const double log_hi = log_pos(hi * 1.5, floor_value);
    const double span = std::max(log_hi - log_lo, 1e-9);
    std::ostringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << num(width, "%.0f") << "\" height=\""
        << num(height, "%.0f") << "\" viewBox=\"0 0 " << num(width, "%.0f") << " " << num(height, "%.0f")
        << "\">\n";
    svg << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";
    svg << "<text x=\"" << num(margin_left, "%.0f") << "\" y=\"32\" font-family=\"Helvetica,Arial,sans-serif\" "
        << "font-size=\"18\" fill=\"#111111\">" << escape(title) << "</text>\n";
    svg << "<text x=\"" << num(margin_left, "%.0f") << "\" y=\"54\" font-family=\"Helvetica,Arial,sans-serif\" "
        << "font-size=\"12\" fill=\"#555555\">" << escape(subtitle) << "</text>\n";

    auto panel_path = [&](const Panel& panel, double top, bool band) {
        const double plot_w = width - margin_left - margin_right;
        auto x_of = [&](double fraction) { return margin_left + fraction * plot_w; };
        auto y_of = [&](double value) {
            const double t = (log_pos(value, floor_value) - log_lo) / span;
            return top + panel_height - t * panel_height;
        };
        std::ostringstream out;
        // The random control's +/- 1 standard deviation band.
        if (band && panel.random_low.size() > 1) {
            out << "<polygon points=\"";
            for (std::size_t i = 0; i < panel.random_high.size(); ++i) {
                const double f = rows[i].retained_fraction;
                out << num(x_of(f), "%.2f") << "," << num(y_of(panel.random_high[i]), "%.2f") << " ";
            }
            for (std::size_t i = panel.random_low.size(); i-- > 0;) {
                const double f = rows[i].retained_fraction;
                out << num(x_of(f), "%.2f") << "," << num(y_of(panel.random_low[i]), "%.2f") << " ";
            }
            out << "\" fill=\"#d1495b\" fill-opacity=\"0.16\" stroke=\"none\"/>\n";
        }
        // Gridlines and the y-axis ticks.
        for (int i = 0; i <= 4; ++i) {
            const double t = static_cast<double>(i) / 4.0;
            const double y = top + panel_height - t * panel_height;
            const double value = std::pow(10.0, log_lo + t * span);
            svg << "<line x1=\"" << num(margin_left, "%.0f") << "\" y1=\"" << num(y, "%.2f") << "\" x2=\""
                << num(width - margin_right, "%.0f") << "\" y2=\"" << num(y, "%.2f")
                << "\" stroke=\"#dddddd\" stroke-width=\"1\"/>\n";
            svg << "<text x=\"" << num(margin_left - 8.0, "%.0f") << "\" y=\"" << num(y + 4.0, "%.2f")
                << "\" font-family=\"Helvetica,Arial,sans-serif\" font-size=\"11\" fill=\"#555555\" "
                << "text-anchor=\"end\">" << num(value, "%.3g") << "</text>\n";
        }
        // The x-axis ticks.
        for (int i = 0; i <= 5; ++i) {
            const double f = static_cast<double>(i) / 5.0;
            const double x = x_of(f);
            svg << "<line x1=\"" << num(x, "%.2f") << "\" y1=\"" << num(top + panel_height, "%.2f") << "\" x2=\""
                << num(x, "%.2f") << "\" y2=\"" << num(top + panel_height + 4.0, "%.2f")
                << "\" stroke=\"#999999\" stroke-width=\"1\"/>\n";
            svg << "<text x=\"" << num(x, "%.2f") << "\" y=\"" << num(top + panel_height + 18.0, "%.2f")
                << "\" font-family=\"Helvetica,Arial,sans-serif\" font-size=\"11\" fill=\"#555555\" "
                << "text-anchor=\"middle\">" << num(f, "%.2f") << "</text>\n";
        }
        svg << "<rect x=\"" << num(margin_left, "%.0f") << "\" y=\"" << num(top, "%.0f") << "\" width=\""
            << num(plot_w, "%.0f") << "\" height=\"" << num(panel_height, "%.0f")
            << "\" fill=\"none\" stroke=\"#333333\" stroke-width=\"1\"/>\n";
        svg << "<text x=\"" << num(margin_left, "%.0f") << "\" y=\"" << num(top - 16.0, "%.0f")
            << "\" font-family=\"Helvetica,Arial,sans-serif\" font-size=\"13\" fill=\"#111111\">" << escape(panel.label)
            << " (" << escape(panel.unit) << ", log)</text>\n";
        svg << "<text x=\"" << num(width - margin_right, "%.0f") << "\" y=\"" << num(top + panel_height + 34.0, "%.0f")
            << "\" font-family=\"Helvetica,Arial,sans-serif\" font-size=\"12\" fill=\"#555555\" "
            << "text-anchor=\"end\">retained fraction</text>\n";
        // The two curves.
        auto curve = [&](const std::vector<double>& values, const char* colour, const char* dash) {
            out << "<polyline points=\"";
            for (std::size_t i = 0; i < values.size(); ++i) {
                out << num(x_of(rows[i].retained_fraction), "%.2f") << "," << num(y_of(values[i]), "%.2f") << " ";
            }
            out << "\" fill=\"none\" stroke=\"" << colour << "\" stroke-width=\"2.2\"";
            if (dash && dash[0]) out << " stroke-dasharray=\"" << dash << "\"";
            out << "/>\n";
        };
        curve(panel.random_mean, "#d1495b", "6,4");
        curve(panel.confidence, "#2e6f9e", "");
        return out.str();
    };

    svg << panel_path(rmse, margin_top, true);
    svg << panel_path(p95, margin_top + panel_height + panel_gap, false);

    // The legend.
    const double legend_y = margin_top + 2 * panel_height + panel_gap + 46.0;
    svg << "<line x1=\"" << num(margin_left, "%.0f") << "\" y1=\"" << num(legend_y - 4.0, "%.0f") << "\" x2=\""
        << num(margin_left + 40.0, "%.0f") << "\" y2=\"" << num(legend_y - 4.0, "%.0f")
        << "\" stroke=\"#2e6f9e\" stroke-width=\"2.2\"/>\n";
    svg << "<text x=\"" << num(margin_left + 50.0, "%.0f") << "\" y=\"" << num(legend_y, "%.0f")
        << "\" font-family=\"Helvetica,Arial,sans-serif\" font-size=\"12\" fill=\"#111111\">confidence retention"
        << "</text>\n";
    svg << "<line x1=\"" << num(margin_left + 230.0, "%.0f") << "\" y1=\"" << num(legend_y - 4.0, "%.0f") << "\" x2=\""
        << num(margin_left + 270.0, "%.0f") << "\" y2=\"" << num(legend_y - 4.0, "%.0f")
        << "\" stroke=\"#d1495b\" stroke-width=\"2.2\" stroke-dasharray=\"6,4\"/>\n";
    svg << "<text x=\"" << num(margin_left + 280.0, "%.0f") << "\" y=\"" << num(legend_y, "%.0f")
        << "\" font-family=\"Helvetica,Arial,sans-serif\" font-size=\"12\" fill=\"#111111\">random retention (mean"
        << "</text>\n";
    svg << "<rect x=\"" << num(margin_left + 470.0, "%.0f") << "\" y=\"" << num(legend_y - 12.0, "%.0f")
        << "\" width=\"18\" height=\"10\" fill=\"#d1495b\" fill-opacity=\"0.16\"/>\n";
    svg << "<text x=\"" << num(margin_left + 496.0, "%.0f") << "\" y=\"" << num(legend_y, "%.0f")
        << "\" font-family=\"Helvetica,Arial,sans-serif\" font-size=\"12\" fill=\"#111111\">random retention "
        << "(+/- 1 s.d.)</text>\n";
    svg << "</svg>\n";

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    out << svg.str();
    if (!out) {
        if (error) *error = "the write of " + path + " failed";
        return false;
    }
    return true;
}

}  // namespace confidence
}  // namespace openscanner
