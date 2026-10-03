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

bool valid_percent(const std::optional<double> value) {
    return value && std::isfinite(*value) && *value >= 0.0 && *value <= 100.0;
}

std::string format_percent(const std::optional<double> value) {
    if (!valid_percent(value)) {
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

std::string format_usage(const Usage& usage) {
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
    constexpr double mib = 1024.0 * 1024.0;
    const bool use_gib = usage.total_bytes >= static_cast<std::uint64_t>(gib);
    const double divisor = use_gib ? gib : mib;
    const char* unit = use_gib ? "GiB" : "MiB";

    std::ostringstream output;
    output << std::fixed << std::setprecision(1)
           << static_cast<double>(usage.used_bytes) / divisor
           << " / " << static_cast<double>(usage.total_bytes) / divisor << ' ' << unit;
    return output.str();
}

std::string format_meter(const double percent, const std::string_view detail = {}) {
    constexpr std::size_t width = 16;
    const auto filled = static_cast<std::size_t>(std::lround(percent * width / 100.0));
    std::string result = "[" + std::string(filled, '#') +
                         std::string(width - filled, '.') + "]  " +
                         format_percent(percent);
    if (!detail.empty()) {
        result += "  ";
        result += detail;
    }
    return result;
}

std::string format_usage_meter(const std::optional<Usage>& usage, const bool startup) {
    if (!usage) {
        return "N/A";
    }
    const auto percent = usage_percent(*usage);
    if (!percent) {
        return "N/A";
    }
    std::string result = format_meter(*percent, format_usage(*usage));
    if (startup) {
        result += "  (startup)";
    }
    return result;
}

void append_row(std::ostringstream& output, const std::string_view label,
                const std::string& value) {
    output << std::left << std::setw(9) << label << value << '\n';
}

}  // namespace

std::string render_stats(const SystemStats& stats) {
    std::ostringstream output;
    output << "Pulse\nLive system monitor\n\n";
    append_row(output, "CPU", valid_percent(stats.cpu_percent)
                                  ? format_meter(*stats.cpu_percent) : "N/A");
    append_row(output, "TEMP", format_temperature(stats.temperature_celsius));
    append_row(output, "MEMORY", format_usage_meter(stats.memory, false));
    append_row(output, "DISK", format_usage_meter(stats.disk, true));
    append_row(output, "GPU", format_percent(stats.gpu_percent));
    output << "\nRefresh ~0.5s  |  Ctrl+C to exit\n";
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
           "  stats  Show live system statistics\n"
           "  help   Show this help message\n";
}

}  // namespace pulse
