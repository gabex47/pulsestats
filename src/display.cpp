#include "pulse/display.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
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

enum class Tone { normal, accent, violet, muted, good, caution, danger };
struct Span { std::string text; Tone tone; };

std::size_t text_width(const std::string_view value) {
    std::size_t count = 0;
    for (const char byte : value) {
        if ((static_cast<unsigned char>(byte) & 0xc0U) != 0x80U) ++count;
    }
    return count;
}

std::string clip(const std::string_view value, const std::size_t columns) {
    std::size_t index = 0, count = 0;
    while (index < value.size() && count < columns) {
        ++index;
        while (index < value.size() &&
               (static_cast<unsigned char>(value[index]) & 0xc0U) == 0x80U) ++index;
        ++count;
    }
    return std::string(value.substr(0, index));
}

std::string safe_text(const std::string_view raw) {
    std::string result;
    result.reserve(raw.size());
    for (const char byte : raw) {
        const auto ch = static_cast<unsigned char>(byte);
        result += ch >= 32 && ch <= 126 ? static_cast<char>(ch) : '?';
    }
    return result;
}

std::string repeat(const std::string_view glyph, const std::size_t count) {
    std::string result;
    result.reserve(glyph.size() * count);
    for (std::size_t i = 0; i < count; ++i) result += glyph;
    return result;
}

const char* ansi(const Tone tone) {
    switch (tone) {
    case Tone::accent: return "\x1b[96m";
    case Tone::violet: return "\x1b[95m";
    case Tone::muted: return "\x1b[2m";
    case Tone::good: return "\x1b[32m";
    case Tone::caution: return "\x1b[33m";
    case Tone::danger: return "\x1b[31m";
    case Tone::normal: return "\x1b[97m";
    }
    return "";
}

struct Line {
    std::vector<Span> spans;
    Line& add(std::string value, const Tone tone = Tone::normal) {
        spans.push_back({std::move(value), tone});
        return *this;
    }
    Line& add(const Line& other) {
        spans.insert(spans.end(), other.spans.begin(), other.spans.end());
        return *this;
    }
    std::size_t width() const {
        std::size_t result = 0;
        for (const auto& span : spans) result += text_width(span.text);
        return result;
    }
    Line& pad_to(const std::size_t columns) {
        const auto current = width();
        if (current < columns) add(std::string(columns - current, ' '));
        return *this;
    }
    std::string render(const std::size_t columns, const bool color) const {
        std::string output;
        std::size_t remaining = columns;
        for (const auto& span : spans) {
            if (remaining == 0) break;
            const std::string visible = clip(span.text, remaining);
            if (visible.empty()) continue;
            if (color) output += ansi(span.tone);
            output += visible;
            if (color) output += "\x1b[0m";
            remaining -= text_width(visible);
        }
        return output;
    }
};

bool valid_percent(const std::optional<double> value) {
    return value && std::isfinite(*value) && *value >= 0.0 && *value <= 100.0;
}

std::string percent_text(const std::optional<double> value, const bool unicode) {
    return valid_percent(value)
        ? std::to_string(static_cast<int>(std::lround(*value))) + "%"
        : unicode ? "—" : "-";
}

std::string quantity(const std::uint64_t bytes, const bool gib) {
    const double divisor = gib ? 1024.0 * 1024.0 * 1024.0 : 1024.0 * 1024.0;
    std::ostringstream output;
    output << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / divisor;
    return output.str();
}

std::string usage_text(const Usage& usage) {
    const bool gib = usage.total_bytes >= 1024ULL * 1024ULL * 1024ULL;
    return quantity(usage.used_bytes, gib) + " / " +
           quantity(usage.total_bytes, gib) + (gib ? " GiB" : " MiB");
}

std::string compact_usage_text(const Usage& usage) {
    const bool gib = usage.total_bytes >= 1024ULL * 1024ULL * 1024ULL;
    const double divisor = gib ? 1024.0 * 1024.0 * 1024.0 : 1024.0 * 1024.0;
    std::ostringstream output;
    output << std::fixed << std::setprecision(0)
           << static_cast<double>(usage.used_bytes) / divisor << '/'
           << static_cast<double>(usage.total_bytes) / divisor
           << (gib ? " GiB" : " MiB");
    return output.str();
}

std::string uptime_text(const std::optional<std::uint64_t> boot_seconds) {
    if (!boot_seconds) return {};
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    if (now < 0 || static_cast<std::uint64_t>(now) < *boot_seconds) return {};
    const auto minutes = (static_cast<std::uint64_t>(now) - *boot_seconds) / 60;
    return (minutes >= 1440 ? std::to_string(minutes / 1440) + "d " : "") +
           std::to_string((minutes / 60) % 24) + "h " +
           std::to_string(minutes % 60) + "m";
}

std::string load_text(const std::optional<std::array<double, 3>>& values,
                      const bool unicode) {
    if (!values || !std::isfinite((*values)[0]) || !std::isfinite((*values)[1]) ||
        !std::isfinite((*values)[2]) || (*values)[0] < 0 || (*values)[1] < 0 ||
        (*values)[2] < 0) return unicode ? "—" : "-";
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << (*values)[0] << "  "
           << (*values)[1] << "  " << (*values)[2];
    return output.str();
}

std::string battery_state(const BatteryStats& battery) {
    return battery.charging && *battery.charging ? "Charging"
        : battery.on_ac_power && *battery.on_ac_power ? "On AC"
        : battery.on_ac_power && !*battery.on_ac_power ? "On battery"
        : "State unknown";
}

Line heading(const std::string_view label, const std::size_t width, const bool unicode) {
    Line line;
    line.add(std::string(label), Tone::accent);
    const auto used = text_width(label);
    if (width > used + 2) {
        line.add("  " + repeat(unicode ? "─" : "-", width - used - 2), Tone::muted);
    }
    return line;
}

Line system_entry(const std::string_view label, const std::string_view value) {
    Line line;
    line.add(std::string(label), Tone::muted).pad_to(13).add(safe_text(value));
    return line;
}

std::vector<Line> system_rows(const SystemInfo& info) {
    std::vector<Line> rows;
    rows.push_back(system_entry("OS", "macOS" +
        (info.os_version ? " " + safe_text(*info.os_version) : std::string{})));
    const auto add = [&](const std::string_view label,
                         const std::optional<std::string>& value) {
        if (value && !value->empty()) rows.push_back(system_entry(label, *value));
    };
    add("Device", info.host_model);
    add("CPU model", info.cpu_model);
    add("Kernel", info.kernel);
    add("Architecture", info.architecture);
    add("Hostname", info.hostname);
    if (info.shell && !info.shell->empty()) {
        const auto slash = info.shell->find_last_of('/');
        rows.push_back(system_entry("Shell", info.shell->substr(
            slash == std::string::npos ? 0 : slash + 1)));
    }
    add("Terminal", info.terminal);
    const auto uptime = uptime_text(info.boot_time_seconds);
    if (!uptime.empty()) rows.push_back(system_entry("Uptime", uptime));
    return rows;
}

Line metric(const std::string_view label, const std::optional<double> percent,
            const std::string_view detail, const std::size_t bar_width,
            const RenderOptions options, const Tone tone) {
    Line line;
    constexpr std::size_t label_width = 11;
    line.add(std::string(label), Tone::muted).pad_to(label_width);
    if (!valid_percent(percent)) {
        line.add(percent_text(percent, options.unicode), Tone::muted);
        return line;
    }
    if (bar_width > 0) {
        const auto filled = static_cast<std::size_t>(std::lround(
            *percent * static_cast<double>(bar_width) / 100.0));
        line.add(repeat(options.unicode ? "█" : "#", filled), tone);
        line.add(repeat(options.unicode ? "░" : ".", bar_width - filled), Tone::muted);
        line.add("  ");
    }
    line.add(percent_text(percent, options.unicode), tone);
    if (!detail.empty()) line.add("  " + std::string(detail), Tone::muted);
    return line;
}

Tone usage_tone(const std::optional<double> value, const double caution,
                const double danger) {
    if (!valid_percent(value)) return Tone::muted;
    return *value >= danger ? Tone::danger :
           *value >= caution ? Tone::caution : Tone::good;
}

Line simple_metric(const std::string_view label, const std::string_view value) {
    Line line;
    line.add(std::string(label), Tone::muted).pad_to(11).add(std::string(value));
    return line;
}

std::vector<Line> activity_rows(const SystemStats& stats, const std::size_t bar_width,
                                const RenderOptions options, const bool sensors,
                                const bool expanded, const std::size_t max_width) {
    std::vector<Line> rows;
    rows.push_back(metric("CPU", stats.cpu_percent, {}, bar_width, options,
                          usage_tone(stats.cpu_percent, 60.0, 85.0)));
    const auto memory = stats.memory ? usage_percent(*stats.memory) : std::nullopt;
    const auto memory_detail = memory ? usage_text(*stats.memory) : "";
    auto memory_line = metric("Memory", memory, expanded ? "" : memory_detail,
                              bar_width, options, usage_tone(memory, 70.0, 90.0));
    if (!expanded && memory && memory_line.width() > max_width) {
        memory_line = metric("Memory", memory, compact_usage_text(*stats.memory),
                             bar_width, options, usage_tone(memory, 70.0, 90.0));
    }
    if (memory_line.width() > max_width) {
        memory_line = metric("Memory", memory, {}, bar_width, options,
                             usage_tone(memory, 70.0, 90.0));
    }
    rows.push_back(std::move(memory_line));
    if (expanded && memory) rows.push_back(Line{}.add("           " + memory_detail, Tone::muted));
    const auto disk = stats.disk ? usage_percent(*stats.disk) : std::nullopt;
    const auto disk_detail = disk ? usage_text(*stats.disk) + "  (startup)" : "";
    auto disk_line = metric("Disk", disk, expanded ? "" : disk_detail,
                            bar_width, options, usage_tone(disk, 80.0, 95.0));
    if (!expanded && disk && disk_line.width() > max_width) {
        disk_line = metric("Disk", disk, usage_text(*stats.disk), bar_width, options,
                           usage_tone(disk, 80.0, 95.0));
    }
    if (!expanded && disk && disk_line.width() > max_width) {
        disk_line = metric("Disk", disk, compact_usage_text(*stats.disk), bar_width,
                           options, usage_tone(disk, 80.0, 95.0));
    }
    if (disk_line.width() > max_width) {
        disk_line = metric("Disk", disk, {}, bar_width, options,
                           usage_tone(disk, 80.0, 95.0));
    }
    rows.push_back(std::move(disk_line));
    if (expanded && disk) rows.push_back(Line{}.add("           " + disk_detail, Tone::muted));
    if (stats.battery) {
        const auto percent = std::optional<double>{stats.battery->percent};
        const Tone tone = !valid_percent(percent) ? Tone::muted :
                          *percent <= 15.0 ? Tone::danger :
                          *percent <= 30.0 ? Tone::caution : Tone::good;
        rows.push_back(metric("Battery", percent, expanded ? "" : battery_state(*stats.battery),
                              bar_width, options, tone));
        if (expanded && valid_percent(percent)) {
            rows.push_back(Line{}.add("           " + battery_state(*stats.battery), Tone::muted));
        }
    }
    rows.push_back(simple_metric("Load", load_text(stats.load_average, options.unicode)));
    rows.push_back(simple_metric("Processes", stats.process_count
        ? std::to_string(*stats.process_count) : (options.unicode ? "—" : "-")));
    if (sensors) {
        const auto temperature = stats.temperature_celsius;
        const bool valid = temperature && std::isfinite(*temperature) &&
                           *temperature >= -100.0 && *temperature <= 250.0;
        rows.push_back(simple_metric("Temp", valid
            ? std::to_string(static_cast<int>(std::lround(*temperature))) +
              (options.unicode ? "°C" : " C") : (options.unicode ? "—" : "-")));
        rows.push_back(simple_metric("GPU", percent_text(stats.gpu_percent, options.unicode)));
    }
    return rows;
}

std::string interval_text(const double seconds) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(3) << seconds;
    std::string result = output.str();
    while (result.back() == '0') result.pop_back();
    if (result.back() == '.') result.pop_back();
    return result + 's';
}

Line footer(const double interval, const RenderOptions options, const bool compact) {
    Line line;
    if (options.live) {
        line.add(options.unicode ? "●" : "*", Tone::good).add(" LIVE", Tone::good);
        line.add(compact ? "  " : "   ");
        line.add(interval_text(interval) + " refresh", Tone::muted);
        line.add(compact ? "  Ctrl+C" : "   Ctrl+C to exit", Tone::muted);
    } else {
        line.add("Snapshot", Tone::muted);
    }
    return line;
}

std::string render_lines(const std::vector<Line>& lines, const TerminalSize size,
                         const RenderOptions options, const std::size_t left_margin) {
    const auto count = std::min(lines.size(), std::max<std::size_t>(size.rows, 1));
    const auto columns = size.columns > 1 ? size.columns - 1 : 1;
    std::string frame;
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) frame += '\n';
        frame.append(left_margin, ' ');
        frame += lines[index].render(columns - std::min(columns, left_margin), options.color);
    }
    return frame;
}

}  // namespace

std::string render_dashboard(const SystemInfo& info, const SystemStats& stats,
                             const TerminalSize size, const double interval_seconds,
                             const RenderOptions options) {
    const bool wide = size.columns >= 100 && size.rows >= 20;
    const bool medium = !wide && size.columns >= 60 && size.rows >= 14;
    const auto content_width = std::min<std::size_t>(
        size.columns > 1 ? size.columns - 1 : 1, wide ? 102 : 76);
    const auto system = system_rows(info);
    const auto activity = activity_rows(stats, wide ? 16 : medium ?
                                       (size.columns >= 70 ? 12 : 8) :
                                       size.columns >= 42 ? 8 : 0,
                                       options, wide, wide,
                                       size.columns > 1 ? size.columns - 1 : 1);
    std::vector<Line> lines;
    Line logo;
    logo.add(options.unicode ? "▁▂▇▂▁" : "__/\\__", Tone::violet)
        .add("  PULSE", Tone::accent).add("  v" PULSE_VERSION, Tone::muted);
    if (wide && info.hostname) {
        const auto hostname = safe_text(*info.hostname);
        const auto available = content_width > logo.width() + 4
            ? content_width - logo.width() - 4 : 0;
        if (available > 0) {
            logo.pad_to(content_width - std::min(text_width(hostname), available));
            logo.add(clip(hostname, available), Tone::muted);
        }
    }
    lines.push_back(std::move(logo));
    if (wide || medium) {
        lines.push_back(Line{}.add(options.live ? "Live system monitor" : "System snapshot",
                                   Tone::muted));
        lines.emplace_back();
    }
    if (wide) {
        constexpr std::size_t left_width = 42;
        constexpr std::size_t gap = 4;
        const auto right_width = content_width - left_width - gap;
        Line titles = heading("SYSTEM", left_width, options.unicode);
        titles.pad_to(left_width + gap)
              .add(heading(options.live ? "LIVE METRICS" : "METRICS", right_width,
                           options.unicode));
        lines.push_back(std::move(titles));
        const auto count = std::max(system.size(), activity.size());
        for (std::size_t index = 0; index < count; ++index) {
            Line line;
            if (index < system.size()) line.add(system[index]);
            line.pad_to(left_width + gap);
            if (index < activity.size()) line.add(activity[index]);
            lines.push_back(std::move(line));
        }
    } else if (medium) {
        lines.push_back(heading(options.live ? "LIVE METRICS" : "METRICS",
                                content_width, options.unicode));
        lines.insert(lines.end(), activity.begin(), activity.end());
        const auto available = size.rows > lines.size() + 3 ? size.rows - lines.size() - 3 : 0;
        if (available >= 2) {
            lines.emplace_back();
            lines.push_back(heading("SYSTEM", content_width, options.unicode));
            lines.insert(lines.end(), system.begin(),
                         system.begin() + static_cast<std::ptrdiff_t>(
                             std::min(available, system.size())));
        }
    } else {
        const auto metric_space = size.rows > lines.size() + 1
            ? size.rows - lines.size() - 1 : 0;
        lines.insert(lines.end(), activity.begin(),
                     activity.begin() + static_cast<std::ptrdiff_t>(
                         std::min(metric_space, activity.size())));
        if (metric_space >= activity.size() + 4) {
            lines.emplace_back();
            lines.push_back(heading("SYSTEM", content_width, options.unicode));
            const auto system_space = size.rows > lines.size() + 2
                ? size.rows - lines.size() - 2 : 0;
            lines.insert(lines.end(), system.begin(),
                         system.begin() + static_cast<std::ptrdiff_t>(
                             std::min(system_space, system.size())));
        }
    }
    if (size.rows > lines.size() + 1) lines.emplace_back();
    if (size.rows > 1) lines.push_back(footer(interval_seconds, options, !wide));
    const auto left_margin = wide ? (size.columns - content_width) / 2 : 0;
    return render_lines(lines, size, options, left_margin);
}

std::string render_stats(const SystemStats& stats) {
    return render_dashboard(SystemInfo{}, stats, {80, 24}, 1.0,
                            {false, false, false});
}

std::string help_text() {
    return "Pulse v" PULSE_VERSION " - lightweight live system monitor\n"
           "\n"
           "Usage:\n"
           "  pulse [--interval SECONDS] [--no-color]\n"
           "  pulse stats [--interval SECONDS] [--no-color]\n"
           "  pulse help\n"
           "  pulse --help\n"
           "  pulse --version\n"
           "\n"
           "Commands:\n"
           "  stats            Open the live dashboard (also the default)\n"
           "  help             Show this help message\n"
           "\n"
           "Options:\n"
           "  -i, --interval   Refresh every 0.25 to 60 seconds (default: 1)\n"
           "  --no-color       Disable ANSI colors (also respects NO_COLOR)\n"
           "\n"
           "Press Ctrl+C to exit. Piped output is one plain snapshot.\n";
}

}  // namespace pulse
