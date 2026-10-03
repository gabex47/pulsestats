#include "pulse/display.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace pulse {

std::optional<double> usage_percent(const Usage& usage) {
    if (usage.total_bytes == 0 || usage.used_bytes > usage.total_bytes) {
        return std::nullopt;
    }
    return 100.0 * static_cast<double>(usage.used_bytes) /
           static_cast<double>(usage.total_bytes);
}

namespace {

constexpr std::string_view version = "v" PULSE_VERSION;

std::size_t width(const std::string_view value) {
    std::size_t count = 0;
    for (const char byte : value) {
        const auto ch = static_cast<unsigned char>(byte);
        if ((ch & 0xc0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

std::string repeat(const std::string_view glyph, const std::size_t count) {
    std::string result;
    result.reserve(glyph.size() * count);
    for (std::size_t i = 0; i < count; ++i) {
        result += glyph;
    }
    return result;
}

std::string fit(const std::string_view value, const std::size_t columns) {
    if (columns == 0) {
        return {};
    }
    if (width(value) <= columns) {
        return std::string(value);
    }
    std::string result;
    std::size_t count = 0;
    for (std::size_t index = 0; index < value.size() && count + 1 < columns;) {
        const std::size_t begin = index++;
        while (index < value.size() &&
               (static_cast<unsigned char>(value[index]) & 0xc0U) == 0x80U) {
            ++index;
        }
        result.append(value.substr(begin, index - begin));
        ++count;
    }
    result += "…";
    return result;
}

std::string padded(const std::string_view value, const std::size_t columns) {
    std::string result = fit(value, columns);
    result.append(columns - width(result), ' ');
    return result;
}

// Never let device names or environment variables inject terminal controls.
std::string safe_text(const std::string_view raw) {
    std::string result;
    result.reserve(raw.size());
    for (const char byte : raw) {
        const auto ch = static_cast<unsigned char>(byte);
        result += ch >= 32 && ch <= 126 ? static_cast<char>(ch) : '?';
    }
    return result;
}

bool valid_percent(const std::optional<double> value) {
    return value && std::isfinite(*value) && *value >= 0.0 && *value <= 100.0;
}

std::string percent_text(const std::optional<double> value) {
    return valid_percent(value)
               ? std::to_string(static_cast<int>(std::lround(*value))) + "%"
               : "N/A";
}

bool uses_gib(const std::uint64_t total) {
    return total >= 1024ULL * 1024ULL * 1024ULL;
}

std::string quantity(const std::uint64_t bytes, const bool gib) {
    const double divisor = gib ? 1024.0 * 1024.0 * 1024.0 : 1024.0 * 1024.0;
    std::ostringstream output;
    output << std::fixed << std::setprecision(1) <<
        static_cast<double>(bytes) / divisor;
    return output.str();
}

std::string usage_text(const Usage& usage) {
    const bool gib = uses_gib(usage.total_bytes);
    return quantity(usage.used_bytes, gib) + " / " +
           quantity(usage.total_bytes, gib) + (gib ? " GiB" : " MiB");
}

std::string meter(const std::optional<double> value, const std::string_view detail = {}) {
    if (!valid_percent(value)) {
        return "N/A";
    }
    constexpr std::size_t length = 10;
    const auto filled = static_cast<std::size_t>(std::lround(*value * length / 100.0));
    std::string result = repeat("█", filled) + repeat("░", length - filled) +
                         "  " + padded(percent_text(value), 4);
    if (!detail.empty()) {
        result += "  ";
        result += detail;
    }
    return result;
}

std::string usage_meter(const std::optional<Usage>& usage, const bool startup,
                        const bool bars) {
    if (!usage) {
        return "N/A";
    }
    const auto percentage = usage_percent(*usage);
    if (!percentage) {
        return "N/A";
    }
    std::string result = bars ? meter(percentage, usage_text(*usage))
                              : percent_text(percentage) + "  " + usage_text(*usage);
    if (startup) {
        result += "  (startup)";
    }
    return result;
}

std::string row(const std::string_view label, const std::string_view value) {
    return padded(label, 10) + std::string(value);
}

std::string uptime_text(const std::optional<std::uint64_t> boot_seconds) {
    if (!boot_seconds) {
        return {};
    }
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    if (now < 0 || static_cast<std::uint64_t>(now) < *boot_seconds) {
        return {};
    }
    const auto minutes = (static_cast<std::uint64_t>(now) - *boot_seconds) / 60;
    return (minutes >= 1440 ? std::to_string(minutes / 1440) + "d " : "") +
           std::to_string((minutes / 60) % 24) + "h " +
           std::to_string(minutes % 60) + "m";
}

std::string load_text(const std::optional<std::array<double, 3>>& values) {
    if (!values || !std::isfinite((*values)[0]) || !std::isfinite((*values)[1]) ||
        !std::isfinite((*values)[2]) || (*values)[0] < 0 || (*values)[1] < 0 ||
        (*values)[2] < 0) {
        return "N/A";
    }
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << (*values)[0] << "  "
           << (*values)[1] << "  " << (*values)[2];
    return output.str();
}

std::string battery_state(const BatteryStats& battery) {
    return battery.charging && *battery.charging ? "Charging"
                            : battery.on_ac_power && *battery.on_ac_power ? "On AC"
                            : battery.on_ac_power && !*battery.on_ac_power
                                  ? "On battery" : "State unknown";
}

std::string battery_text(const BatteryStats& battery, const bool bars) {
    if (!valid_percent(battery.percent)) {
        return "N/A";
    }
    return bars ? meter(battery.percent, battery_state(battery))
                : percent_text(battery.percent) + "  " + battery_state(battery);
}

std::vector<std::string> system_rows(const SystemInfo& info,
                                     const bool include_hostname) {
    std::vector<std::string> lines;
    lines.push_back(row("OS", "macOS" +
        (info.os_version ? " " + safe_text(*info.os_version) : std::string{})));
    const auto add = [&](const std::string_view label,
                         const std::optional<std::string>& value) {
        if (value && !value->empty()) {
            lines.push_back(row(label, safe_text(*value)));
        }
    };
    add("DEVICE", info.host_model);
    add("KERNEL", info.kernel);
    add("ARCH", info.architecture);
    if (include_hostname) {
        add("HOSTNAME", info.hostname);
    }
    add("CPU CHIP", info.cpu_model);
    if (info.shell && !info.shell->empty()) {
        const auto slash = info.shell->find_last_of('/');
        lines.push_back(row("SHELL", safe_text(info.shell->substr(
            slash == std::string::npos ? 0 : slash + 1))));
    }
    add("TERMINAL", info.terminal);
    const auto uptime = uptime_text(info.boot_time_seconds);
    if (!uptime.empty()) {
        lines.push_back(row("UPTIME", uptime));
    }
    return lines;
}

std::vector<std::string> activity_rows(const SystemStats& stats, const bool bars,
                                        const bool concise) {
    std::vector<std::string> lines;
    lines.push_back(row("CPU", bars ? meter(stats.cpu_percent)
                                    : percent_text(stats.cpu_percent)));
    lines.push_back(row("MEMORY", usage_meter(stats.memory, false, bars)));
    if (!concise && stats.memory && usage_percent(*stats.memory)) {
        const bool gib = uses_gib(stats.memory->total_bytes);
        lines.push_back(row("RAM LEFT", quantity(stats.memory->total_bytes -
                                                stats.memory->used_bytes, gib) +
                                         (gib ? " GiB est." : " MiB est.")));
    }
    lines.push_back(row("DISK", usage_meter(stats.disk, true, bars)));
    if (stats.battery) {
        lines.push_back(row("BATTERY", battery_text(*stats.battery, bars)));
    }
    lines.push_back(row("LOAD", load_text(stats.load_average)));
    lines.push_back(row("PROCESSES", stats.process_count
                                            ? std::to_string(*stats.process_count) : "N/A"));
    if (!concise) {
        lines.push_back(row("TEMP", stats.temperature_celsius &&
                std::isfinite(*stats.temperature_celsius) &&
                *stats.temperature_celsius >= -100.0 && *stats.temperature_celsius <= 250.0
                ? std::to_string(static_cast<int>(std::lround(*stats.temperature_celsius))) + "°C"
                : "N/A"));
        lines.push_back(row("GPU", percent_text(stats.gpu_percent)));
    }
    return lines;
}

std::string interval_text(const double seconds) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(3) << seconds;
    std::string result = output.str();
    while (result.back() == '0') {
        result.pop_back();
    }
    if (result.back() == '.') {
        result.pop_back();
    }
    return result + 's';
}

std::string joined_frame(const std::vector<std::string>& lines, const TerminalSize size) {
    std::ostringstream output;
    const std::size_t count = std::min(lines.size(), std::max<std::size_t>(size.rows, 1));
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) {
            output << '\n';
        }
        output << fit(lines[index], size.columns > 1 ? size.columns - 1 : 1);
    }
    return output.str();
}

}  // namespace

std::string render_dashboard(const SystemInfo& info, const SystemStats& stats,
                             const TerminalSize size, const double interval_seconds) {
    const bool wide = size.columns >= 108 && size.rows >= 17;
    const auto system = system_rows(info, !wide);
    const auto activity = activity_rows(stats, size.columns >= 80, size.rows < 18);
    const std::string footer = size.columns < 40
        ? interval_text(interval_seconds) + " refresh  ·  Ctrl+C"
        : "Refresh " + interval_text(interval_seconds) + "  ·  Ctrl+C to exit";
    std::vector<std::string> lines;
    if (wide) {
        const std::size_t box_width = std::min<std::size_t>(size.columns - 1, 110);
        const std::size_t left_width = 43;
        const std::size_t right_width = box_width - 6 - left_width;
        const auto border = [&](const std::string_view left, const std::string_view right) {
            return std::string(left) + repeat("─", box_width - 2) + std::string(right);
        };
        const auto full = [&](const std::string_view value) {
            return "│ " + padded(value, box_width - 4) + " │";
        };
        lines.push_back(border("╭", "╮"));
        const std::string identity = info.hostname ? safe_text(*info.hostname) : "macOS";
        const std::string title = "PULSE  " + std::string(version);
        const std::size_t gap = box_width > width(title) + width(identity) + 8
                                    ? box_width - width(title) - width(identity) - 4 : 2;
        lines.push_back(full(title + std::string(gap, ' ') + identity));
        lines.push_back(border("├", "┤"));
        lines.push_back("│ " + padded("SYSTEM", left_width) + "  " +
                        padded("PERFORMANCE", right_width) + " │");
        const std::size_t count = std::max(system.size(), activity.size());
        for (std::size_t index = 0; index < count; ++index) {
            lines.push_back("│ " + padded(index < system.size() ? system[index] : "", left_width) +
                            "  " + padded(index < activity.size() ? activity[index] : "",
                                            right_width) + " │");
        }
        lines.push_back(border("├", "┤"));
        lines.push_back(full(footer));
        lines.push_back(border("╰", "╯"));
        if (lines.size() <= size.rows) {
            return joined_frame(lines, size);
        }
        lines.clear();
    }

    lines.push_back("PULSE  " + std::string(version));
    if (size.rows >= 12 && size.columns >= 50) {
        lines.push_back(repeat("─", std::min<std::size_t>(size.columns > 1 ? size.columns - 1 : 1, 48)));
        const std::size_t available_system = size.rows > activity.size() + 6
                                                 ? size.rows - activity.size() - 6 : 0;
        if (available_system > 0) {
            lines.push_back("SYSTEM");
            lines.insert(lines.end(), system.begin(),
                         system.begin() + static_cast<std::ptrdiff_t>(
                             std::min(system.size(), available_system)));
            lines.push_back("");
        }
        lines.push_back("PERFORMANCE");
        lines.insert(lines.end(), activity.begin(), activity.end());
    } else {
        lines.push_back(row("CPU", percent_text(stats.cpu_percent)));
        lines.push_back(row("MEMORY", stats.memory
                                          ? percent_text(usage_percent(*stats.memory)) : "N/A"));
        lines.push_back(row("DISK", stats.disk
                                        ? percent_text(usage_percent(*stats.disk)) : "N/A"));
        if (stats.battery) {
            lines.push_back(row("BATTERY", percent_text(stats.battery->percent)));
        }
    }
    if (lines.size() + 1 > size.rows) {
        lines.resize(size.rows > 1 ? size.rows - 1 : 1);
    }
    if (size.rows > 1) {
        lines.push_back(footer);
    }
    return joined_frame(lines, size);
}

std::string render_stats(const SystemStats& stats) {
    return render_dashboard(SystemInfo{}, stats, {80, 24}, 1.0);
}

std::string help_text() {
    return "Pulse v" PULSE_VERSION " - lightweight live system monitor\n"
           "\n"
           "Usage:\n"
           "  pulse [--interval SECONDS]\n"
           "  pulse stats [--interval SECONDS]\n"
           "  pulse help\n"
           "  pulse --help\n"
           "  pulse --version\n"
           "\n"
           "Commands:\n"
           "  stats            Open the live dashboard (also the default)\n"
           "  help             Show this help message\n"
           "\n"
           "Options:\n"
           "  -i, --interval  Refresh every 0.25 to 60 seconds (default: 1)\n"
           "\n"
           "Press Ctrl+C to exit the dashboard.\n";
}

}  // namespace pulse
