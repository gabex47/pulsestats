#include "pulse/display.hpp"
#include "pulse/stats.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>

namespace {

bool require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

bool contains(const std::string& output, const std::string& text) {
    return output.find(text) != std::string::npos;
}

bool percent_is(const pulse::Usage usage, const std::optional<double> expected) {
    return pulse::usage_percent(usage) == expected;
}

}  // namespace

int main() {
    constexpr std::uint64_t gib = 1024ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t mib = 1024ULL * 1024ULL;

    bool passed = true;
    passed &= require(percent_is({50, 100}, 50.0), "Normal usage percentage failed");
    passed &= require(percent_is({0, 0}, std::nullopt), "Zero total should be unavailable");
    passed &= require(percent_is({101, 100}, std::nullopt), "Used > total should be unavailable");
    passed &= require(percent_is({0, 100}, 0.0), "Zero used percentage failed");
    passed &= require(percent_is({100, 100}, 100.0), "Full usage percentage failed");

    pulse::SystemStats sample;
    sample.cpu_percent = 23.4;
    sample.memory = pulse::Usage{5 * gib, 8 * gib};
    sample.disk = pulse::Usage{113 * gib, 228 * gib};
    std::string output = pulse::render_stats(sample);
    passed &= require(contains(output, "CPU      23%"), "CPU formatting failed");
    passed &= require(contains(output, "TEMP     N/A"), "Temperature fallback failed");
    passed &= require(contains(output, "MEMORY   5.0 GiB / 8.0 GiB  (63%)"),
                      "Memory formatting failed");
    passed &= require(contains(output, "DISK     113.0 GiB / 228.0 GiB  (50%)  (startup)"),
                      "Disk formatting failed");
    passed &= require(contains(output, "GPU      N/A"), "GPU fallback failed");

    sample.cpu_percent = std::nullopt;
    sample.memory = pulse::Usage{9 * gib, 8 * gib};
    sample.disk = std::nullopt;
    output = pulse::render_stats(sample);
    passed &= require(contains(output, "CPU      N/A"), "Unavailable CPU fallback failed");
    passed &= require(contains(output, "MEMORY   N/A"), "Invalid memory fallback failed");
    passed &= require(contains(output, "DISK     N/A\n"), "Unavailable disk fallback failed");
    passed &= require(!contains(output, "(startup)"), "Unavailable disk has startup label");

    sample.memory = pulse::Usage{0, 0};
    sample.disk = pulse::Usage{2 * gib, gib};
    output = pulse::render_stats(sample);
    passed &= require(contains(output, "MEMORY   N/A"), "Zero memory total fallback failed");
    passed &= require(contains(output, "DISK     N/A\n"), "Invalid disk fallback failed");
    passed &= require(!contains(output, "(startup)"), "Invalid disk has startup label");

    sample.memory = pulse::Usage{256 * mib, 512 * mib};
    sample.disk = pulse::Usage{0, 0};
    output = pulse::render_stats(sample);
    passed &= require(contains(output, "MEMORY   256.0 MiB / 512.0 MiB  (50%)"),
                      "MiB formatting failed");
    passed &= require(contains(output, "DISK     N/A\n"), "Zero disk total fallback failed");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (const double invalid : {-1.0, 101.0, nan, infinity, -infinity}) {
        sample.cpu_percent = invalid;
        sample.gpu_percent = invalid;
        output = pulse::render_stats(sample);
        passed &= require(contains(output, "CPU      N/A"), "Invalid CPU percentage fallback failed");
        passed &= require(contains(output, "GPU      N/A"), "Invalid GPU percentage fallback failed");
    }
    for (const double invalid : {nan, infinity, -infinity}) {
        sample.temperature_celsius = invalid;
        passed &= require(contains(pulse::render_stats(sample), "TEMP     N/A"),
                          "Non-finite temperature fallback failed");
    }

    const pulse::SystemStats live = pulse::collect_system_stats();
    passed &= require(live.memory.has_value(), "Memory statistics unavailable");
    passed &= require(live.disk.has_value(), "Disk statistics unavailable");
    if (live.memory) {
        passed &= require(pulse::usage_percent(*live.memory).has_value(),
                          "Invalid live memory statistics");
    }
    if (live.disk) {
        passed &= require(pulse::usage_percent(*live.disk).has_value(),
                          "Invalid live disk statistics");
    }
    if (live.cpu_percent) {
        passed &= require(std::isfinite(*live.cpu_percent) && *live.cpu_percent >= 0.0 &&
                              *live.cpu_percent <= 100.0,
                          "Invalid live CPU statistics");
    }
    return passed ? 0 : 1;
}
