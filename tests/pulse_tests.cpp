#include "pulse/display.hpp"
#include "pulse/stats.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

bool require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

}  // namespace

int main() {
    constexpr std::uint64_t gib = 1024ULL * 1024ULL * 1024ULL;
    pulse::SystemStats sample;
    sample.cpu_percent = 23.4;
    sample.memory = pulse::Usage{5 * gib, 8 * gib};
    sample.disk = pulse::Usage{113 * gib, 228 * gib};
    const std::string output = pulse::render_stats(sample);

    if (!require(output.find("CPU      23%") != std::string::npos, "CPU formatting failed") ||
        !require(output.find("TEMP     N/A") != std::string::npos, "Temperature fallback failed") ||
        !require(output.find("MEMORY   5.0 GiB / 8.0 GiB  (63%)") != std::string::npos,
                 "Memory formatting failed") ||
        !require(output.find("DISK     113.0 GiB / 228.0 GiB  (50%)  (startup)") !=
                     std::string::npos, "Disk formatting failed") ||
        !require(output.find("GPU      N/A") != std::string::npos, "GPU fallback failed")) {
        return 1;
    }

    sample.memory = pulse::Usage{9 * gib, 8 * gib};
    sample.disk = std::nullopt;
    if (!require(pulse::render_stats(sample).find("MEMORY   N/A") != std::string::npos,
                 "Invalid usage fallback failed") ||
        !require(pulse::render_stats(sample).find("DISK     N/A\n") != std::string::npos,
                 "Unavailable disk fallback failed")) {
        return 1;
    }

    const pulse::SystemStats live = pulse::collect_system_stats();
    if (!require(live.memory.has_value(), "Memory statistics unavailable") ||
        !require(live.disk.has_value(), "Disk statistics unavailable") ||
        !require(pulse::usage_percent(*live.memory).has_value(), "Invalid memory statistics") ||
        !require(pulse::usage_percent(*live.disk).has_value(), "Invalid disk statistics")) {
        return 1;
    }
    return 0;
}
