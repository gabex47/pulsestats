#pragma once

#include <cstdint>
#include <array>
#include <optional>
#include <string>

namespace pulse {

struct Usage {
    std::uint64_t used_bytes;
    std::uint64_t total_bytes;
};

struct BatteryStats {
    double percent;
    std::optional<bool> charging;
    std::optional<bool> on_ac_power;
};

struct SystemInfo {
    std::optional<std::string> os_version;
    std::optional<std::string> host_model;
    std::optional<std::string> cpu_model;
    std::optional<std::string> kernel;
    std::optional<std::string> architecture;
    std::optional<std::string> hostname;
    std::optional<std::string> shell;
    std::optional<std::string> terminal;
    std::optional<std::uint64_t> boot_time_seconds;
};

struct SystemStats {
    std::optional<double> cpu_percent;
    std::optional<double> temperature_celsius;
    std::optional<Usage> memory;
    std::optional<Usage> disk;
    std::optional<double> gpu_percent;
    std::optional<BatteryStats> battery;
    std::optional<std::array<double, 3>> load_average;
    std::optional<std::uint32_t> process_count;
};

std::optional<double> usage_percent(const Usage& usage);
SystemStats collect_system_stats();
SystemInfo collect_system_info();

}  // namespace pulse
