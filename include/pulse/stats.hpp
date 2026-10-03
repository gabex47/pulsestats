#pragma once

#include <cstdint>
#include <optional>

namespace pulse {

struct Usage {
    std::uint64_t used_bytes;
    std::uint64_t total_bytes;
};

struct SystemStats {
    std::optional<double> cpu_percent;
    std::optional<double> temperature_celsius;
    std::optional<Usage> memory;
    std::optional<Usage> disk;
    std::optional<double> gpu_percent;
};

std::optional<double> usage_percent(const Usage& usage);
SystemStats collect_system_stats();

}  // namespace pulse
