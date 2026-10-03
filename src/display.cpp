#include "pulse/display.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace pulse {

std::optional<double> usage_percent(const Usage& usage) {
    if (usage.total_bytes == 0 || usage.used_bytes > usage.total_bytes) {
        return std::nullopt;
    }
    return 100.0 * static_cast<double>(usage.used_bytes) /
           static_cast<double>(usage.total_bytes);
}

namespace {

std::string format_percent(const std::optional<double> value) {
    if (!value || !std::isfinite(*value) || *value < 0.0 || *value > 100.0) {
        return "N/A";
    }
    return std::to_string(static_cast<int>(std::lround(*value))) + "%";
}

std::string format_temperature(const std::optional<double> value) {
    if (!value || !std::isfinite(*value)) {
        return "N/A";
    }
    return std::to_string(static_cast<int>(std::lround(*value))) + "\xC2\xB0" "C";
}

std::string format_usage(const std::optional<Usage>& usage) {
    if (!usage || !usage_percent(*usage)) {
        return "N/A";
    }

    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
    constexpr double mib = 1024.0 * 1024.0;
    const bool use_gib = usage->total_bytes >= static_cast<std::uint64_t>(gib);
    const double divisor = use_gib ? gib : mib;
    const char* unit = use_gib ? "GiB" : "MiB";

    std::ostringstream output;
    output << std::fixed << std::setprecision(1)
           << static_cast<double>(usage->used_bytes) / divisor << ' ' << unit
           << " / " << static_cast<double>(usage->total_bytes) / divisor << ' '
           << unit << "  (" << format_percent(usage_percent(*usage)) << ')';
    return output.str();
}

void append_row(std::ostringstream& output, const std::string_view label,
                const std::string& value) {
    output << std::left << std::setw(9) << label << value << '\n';
}

}  // namespace

std::string render_stats(const SystemStats& stats) {
    std::ostringstream output;
    output << "Pulse\n-----\n";
    append_row(output, "CPU", format_percent(stats.cpu_percent));
    append_row(output, "TEMP", format_temperature(stats.temperature_celsius));
    append_row(output, "MEMORY", format_usage(stats.memory));
    std::string disk = format_usage(stats.disk);
    if (disk != "N/A") {
        disk += "  (startup)";
    }
    append_row(output, "DISK", disk);
    append_row(output, "GPU", format_percent(stats.gpu_percent));
    return output.str();
}

std::string help_text() {
    return "Pulse - lightweight system information\n"
           "\n"
           "Usage:\n"
           "  pulse\n"
           "  pulse stats\n"
           "  pulse help\n"
           "  pulse --help\n"
           "\n"
           "Commands:\n"
           "  stats  Show a system statistics snapshot\n"
           "  help   Show this help message\n";
}

}  // namespace pulse
