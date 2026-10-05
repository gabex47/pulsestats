#include "pulse/display.hpp"
#include "pulse/stats.hpp"
#include "pulse/terminal.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {

bool require(const bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

bool contains(const std::string& output, const std::string_view text) {
    return output.find(text) != std::string::npos;
}

bool frame_fits(const std::string& frame, const pulse::TerminalSize size) {
    std::size_t lines = 1, columns = 0;
    for (const char character : frame) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte == '\n') {
            if (columns >= size.columns) return false;
            ++lines;
            columns = 0;
        } else if ((byte & 0xc0U) != 0x80U) {
            ++columns;
        }
    }
    return columns < size.columns && lines <= size.rows;
}

}  // namespace

int main() {
    constexpr std::uint64_t gib = 1024ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t mib = 1024ULL * 1024ULL;
    bool passed = true;
    passed &= require(pulse::usage_percent({50, 100}) == 50.0, "Normal usage failed");
    passed &= require(!pulse::usage_percent({0, 0}), "Zero total should be unavailable");
    passed &= require(!pulse::usage_percent({101, 100}), "Used > total should be unavailable");
    passed &= require(pulse::usage_percent({0, 100}) == 0.0, "Zero used failed");
    passed &= require(pulse::usage_percent({100, 100}) == 100.0, "Full usage failed");

    pulse::SystemInfo info;
    info.os_version = "27.0.1";
    info.host_model = "Mac14,7";
    info.cpu_model = "Apple M2";
    info.kernel = "Darwin 27.0.0";
    info.architecture = "arm64";
    info.hostname = "test-mac";
    info.shell = "/bin/zsh";
    info.terminal = "Terminal.app";

    pulse::SystemStats sample;
    sample.cpu_percent = 23.4;
    sample.memory = pulse::Usage{5 * gib, 8 * gib};
    sample.disk = pulse::Usage{113 * gib, 228 * gib};
    sample.battery = pulse::BatteryStats{91.0, true, true};
    sample.load_average = std::array<double, 3>{1.42, 1.31, 1.20};
    sample.process_count = 387;

    const pulse::RenderOptions plain{false, true, true};
    std::string frame = pulse::render_dashboard(info, sample, {120, 30}, 1.0, plain);
    passed &= require(frame_fits(frame, {120, 30}), "Wide frame overflows terminal");
    passed &= require(contains(frame, "▁▂▇▂▁  PULSE  v0.1.4") &&
                      contains(frame, "SYSTEM") && contains(frame, "LIVE METRICS"),
                      "Wide identity or structure missing");
    passed &= require(contains(frame, "macOS 27.0.1") && contains(frame, "Apple M2") &&
                      contains(frame, "Darwin 27.0.0"), "Static info missing");
    passed &= require(contains(frame, "CPU        ████░") && contains(frame, "23%"),
                      "CPU bar incorrect");
    passed &= require(contains(frame, "5.0 / 8.0 GiB") && contains(frame, "63%"),
                      "Memory metric incorrect");
    passed &= require(contains(frame, "113.0 / 228.0 GiB  (startup)"),
                      "Startup disk detail missing");
    passed &= require(contains(frame, "Battery") && contains(frame, "91%") &&
                      contains(frame, "Charging"), "Battery metric incorrect");
    passed &= require(contains(frame, "1.42  1.31  1.20") &&
                      contains(frame, "Processes  387"), "Load/process count missing");
    passed &= require(contains(frame, "Temp       —") && contains(frame, "GPU        —"),
                      "Unavailable sensors missing");
    passed &= require(contains(frame, "LIVE") && contains(frame, "1s refresh"),
                      "Live footer missing");

    pulse::SystemStats saturated = sample;
    saturated.cpu_percent = 100.0;
    saturated.memory = pulse::Usage{8 * gib, 8 * gib};
    saturated.disk = pulse::Usage{0, 228 * gib};
    frame = pulse::render_dashboard(info, saturated, {120, 30}, 1.0, plain);
    passed &= require(frame_fits(frame, {120, 30}) &&
                      contains(frame, "████████████████  100%") &&
                      contains(frame, "░░░░░░░░░░░░░░░░  0%"),
                      "Extreme bars rendered incorrectly");
    const auto high_usage = pulse::render_dashboard(info, saturated, {120, 30}, 1.0,
                                                     {true, true, true});
    passed &= require(contains(high_usage, "\x1b[31m") &&
                      contains(high_usage, "\x1b[32m"),
                      "Usage colors do not reflect severity");
    saturated.battery = pulse::BatteryStats{10.0, false, false};
    const auto low_battery = pulse::render_dashboard(info, saturated, {120, 30}, 1.0,
                                                     {true, true, true});
    passed &= require(contains(low_battery, "\x1b[31m10%"),
                      "Low battery warning color missing");

    frame = pulse::render_dashboard(info, sample, {80, 24}, 0.5, plain);
    passed &= require(frame_fits(frame, {80, 24}) && contains(frame, "SYSTEM") &&
                      contains(frame, "0.5s refresh"), "Medium layout failed");
    frame = pulse::render_dashboard(info, sample, {60, 20}, 0.25, plain);
    passed &= require(frame_fits(frame, {60, 20}) && contains(frame, "(startup)"),
                      "Narrow-medium layout failed");
    frame = pulse::render_dashboard(info, sample, {50, 12}, 1.0, plain);
    passed &= require(frame_fits(frame, {50, 12}) && contains(frame, "CPU") &&
                      contains(frame, "Memory") && contains(frame, "Disk") &&
                      contains(frame, "Ctrl+C") && !contains(frame, "(sta"),
                      "Compact layout failed");
    frame = pulse::render_dashboard(info, sample, {50, 24}, 1.0, plain);
    passed &= require(frame_fits(frame, {50, 24}) && contains(frame, "SYSTEM") &&
                      contains(frame, "Apple M2"),
                      "Tall narrow layout hid system identity");
    frame = pulse::render_dashboard(info, sample, {30, 8}, 1.0, plain);
    passed &= require(frame_fits(frame, {30, 8}) && contains(frame, "CPU") &&
                      contains(frame, "Memory") && contains(frame, "GiB") &&
                      contains(frame, "Processes") &&
                      contains(frame, "Ctrl+C"),
                      "Small layout failed");
    passed &= require(frame_fits(pulse::render_dashboard(info, sample, {2, 1}, 1.0,
                                                         plain), {2, 1}),
                      "Tiny terminal overflowed");

    info.hostname = std::string(200, 'x') + "\x1b[31m";
    frame = pulse::render_dashboard(info, sample, {120, 30}, 1.0, plain);
    passed &= require(frame_fits(frame, {120, 30}) && !contains(frame, "\x1b"),
                      "Long or hostile hostname broke rendering");
    const auto colored = pulse::render_dashboard(info, sample, {120, 30}, 1.0,
                                                  {true, true, true});
    passed &= require(contains(colored, "\x1b[96m") && contains(colored, "\x1b[95m") &&
                      contains(colored, "\x1b[32m"), "Semantic colors missing");
    const auto ascii = pulse::render_dashboard(info, sample, {80, 24}, 1.0,
                                                {false, false, false});
    passed &= require(contains(ascii, "__/\\__  PULSE") && contains(ascii, "Snapshot") &&
                      !contains(ascii, "\x1b") && !contains(ascii, "LIVE") &&
                      !contains(ascii, "█"), "ASCII snapshot fallback failed");

    sample.battery.reset();
    sample.cpu_percent.reset();
    sample.memory = pulse::Usage{9 * gib, 8 * gib};
    sample.disk.reset();
    sample.load_average.reset();
    sample.process_count.reset();
    frame = pulse::render_dashboard(info, sample, {80, 24}, 1.0, plain);
    passed &= require(contains(frame, "CPU        —") &&
                      contains(frame, "Memory     —") && contains(frame, "Disk       —") &&
                      contains(frame, "Load       —") && contains(frame, "Processes  —"),
                      "Unavailable values rendered badly");
    passed &= require(!contains(frame, "(startup)") && !contains(frame, "Battery"),
                      "Unavailable disk or absent battery mislabeled");

    sample.memory = pulse::Usage{256 * mib, 512 * mib};
    sample.disk = pulse::Usage{0, 0};
    frame = pulse::render_dashboard(info, sample, {80, 24}, 1.0, plain);
    passed &= require(contains(frame, "256.0 / 512.0 MiB") &&
                      contains(frame, "Disk       —"), "MiB or zero-total formatting failed");
    sample.memory = pulse::Usage{0, 0};
    sample.disk = pulse::Usage{2 * gib, gib};
    frame = pulse::render_dashboard(info, sample, {80, 24}, 1.0, plain);
    passed &= require(contains(frame, "Memory     —") &&
                      contains(frame, "Disk       —"), "Invalid usage was not hidden");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (const double invalid : {-1.0, 101.0, nan, infinity, -infinity}) {
        sample.cpu_percent = invalid;
        sample.gpu_percent = invalid;
        sample.battery = pulse::BatteryStats{invalid, false, false};
        frame = pulse::render_dashboard(info, sample, {120, 30}, 1.0, plain);
        passed &= require(contains(frame, "CPU        —") && contains(frame, "GPU        —") &&
                          contains(frame, "Battery    —"), "Invalid percent rendered as a value");
    }
    for (const double invalid : {nan, infinity, -infinity, 1e300}) {
        sample.temperature_celsius = invalid;
        passed &= require(contains(pulse::render_dashboard(info, sample, {120, 30}, 1.0,
                                                            plain), "Temp       —"),
                          "Invalid temperature rendered as a value");
    }

    std::ostringstream terminal_output;
    {
        pulse::TerminalScreen terminal(terminal_output);
        terminal.draw("first\nstale\n");
        terminal.draw("second\n");
    }
    passed &= require(terminal_output.str() ==
                          "\x1b[?1049h\x1b[?25l"
                          "\x1b[H\x1b[2Kfirst\x1b[E\x1b[2Kstale\x1b[J"
                          "\x1b[H\x1b[2Ksecond\x1b[J"
                          "\x1b[0m\x1b[?25h\x1b[?1049l",
                      "Terminal redraw or cleanup failed");
    std::ostringstream interrupted_output;
    {
        pulse::TerminalScreen terminal(interrupted_output);
        interrupted_output.setstate(std::ios::badbit);
    }
    passed &= require(contains(interrupted_output.str(),
                               "\x1b[0m\x1b[?25h\x1b[?1049l"),
                      "Terminal cleanup suppressed by a stream error");

    const pulse::SystemStats live = pulse::collect_system_stats();
    passed &= require(live.memory.has_value() && live.disk.has_value(),
                      "Live memory or disk statistics unavailable");
    if (live.memory) passed &= require(pulse::usage_percent(*live.memory).has_value(),
                                       "Invalid live memory statistics");
    if (live.disk) passed &= require(pulse::usage_percent(*live.disk).has_value(),
                                     "Invalid live disk statistics");
    if (live.cpu_percent) {
        passed &= require(std::isfinite(*live.cpu_percent) && *live.cpu_percent >= 0.0 &&
                          *live.cpu_percent <= 100.0, "Invalid live CPU percentage");
    }
    if (live.battery) {
        passed &= require(std::isfinite(live.battery->percent) &&
                          live.battery->percent >= 0.0 && live.battery->percent <= 100.0,
                          "Invalid live battery percentage");
    }
    return passed ? 0 : 1;
}
