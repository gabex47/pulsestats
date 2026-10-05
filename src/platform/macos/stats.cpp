#include "pulse/stats.hpp"

#include <mach/host_info.h>
#include <mach/mach.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace pulse {
namespace {

std::optional<host_cpu_load_info_data_t> cpu_ticks(const host_t host) {
    host_cpu_load_info_data_t ticks{};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(host, HOST_CPU_LOAD_INFO,
                        reinterpret_cast<host_info_t>(&ticks), &count) != KERN_SUCCESS ||
        count < HOST_CPU_LOAD_INFO_COUNT) {
        return std::nullopt;
    }
    return ticks;
}

std::optional<double> cpu_usage(const host_cpu_load_info_data_t& before,
                                const host_cpu_load_info_data_t& after) {
    std::uint64_t total = 0;
    std::uint64_t idle = 0;
    for (int state = 0; state < CPU_STATE_MAX; ++state) {
        // Mach CPU ticks are 32-bit counters; unsigned subtraction handles wraparound.
        const std::uint32_t previous = before.cpu_ticks[state];
        const std::uint32_t current = after.cpu_ticks[state];
        const std::uint32_t delta = current - previous;
        total += delta;
        if (state == CPU_STATE_IDLE) {
            idle = delta;
        }
    }
    if (total == 0) {
        return std::nullopt;
    }
    return 100.0 * static_cast<double>(total - idle) / static_cast<double>(total);
}

std::optional<Usage> memory_usage(const host_t host) {
    host_basic_info_data_t basic{};
    mach_msg_type_number_t basic_count = HOST_BASIC_INFO_COUNT;
    if (host_info(host, HOST_BASIC_INFO, reinterpret_cast<host_info_t>(&basic),
                  &basic_count) != KERN_SUCCESS ||
        basic_count < HOST_BASIC_INFO_COUNT || basic.max_mem == 0) {
        return std::nullopt;
    }

    vm_statistics64_data_t vm{};
    mach_msg_type_number_t vm_count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(host, HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm), &vm_count) != KERN_SUCCESS ||
        vm_count < HOST_VM_INFO64_COUNT) {
        return std::nullopt;
    }

    vm_size_t page_size = 0;
    if (host_page_size(host, &page_size) != KERN_SUCCESS || page_size == 0) {
        return std::nullopt;
    }

    const std::uint64_t used_pages = static_cast<std::uint64_t>(vm.active_count) +
                                     static_cast<std::uint64_t>(vm.wire_count) +
                                     static_cast<std::uint64_t>(vm.compressor_page_count);
    if (used_pages > std::numeric_limits<std::uint64_t>::max() / page_size) {
        return std::nullopt;
    }
    const Usage usage{used_pages * page_size, basic.max_mem};
    return usage.used_bytes <= usage.total_bytes ? std::optional<Usage>(usage)
                                                  : std::nullopt;
}

std::optional<Usage> disk_usage() {
    struct statfs fs {};
    if (statfs("/", &fs) != 0 || fs.f_bsize == 0 || fs.f_blocks == 0 ||
        fs.f_bsize == std::numeric_limits<decltype(fs.f_bsize)>::max() ||
        fs.f_blocks == std::numeric_limits<decltype(fs.f_blocks)>::max() ||
        fs.f_bavail == std::numeric_limits<decltype(fs.f_bavail)>::max() ||
        fs.f_bavail > fs.f_blocks ||
        fs.f_blocks > std::numeric_limits<std::uint64_t>::max() / fs.f_bsize) {
        return std::nullopt;
    }

    const std::uint64_t block_size = fs.f_bsize;
    // Available space reflects the shared startup APFS capacity on modern macOS.
    return Usage{(fs.f_blocks - fs.f_bavail) * block_size,
                 fs.f_blocks * block_size};
}

std::optional<std::string> sysctl_string(const char* name) {
    std::size_t size = 0;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size < 2 || size > 4096) {
        return std::nullopt;
    }
    std::string value(size, '\0');
    if (sysctlbyname(name, value.data(), &size, nullptr, 0) != 0 || size == 0) {
        return std::nullopt;
    }
    value.resize(size);
    if (value.back() == '\0') {
        value.pop_back();
    }
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

std::optional<std::uint64_t> boot_time() {
    timeval boot{};
    std::size_t size = sizeof(boot);
    if (sysctlbyname("kern.boottime", &boot, &size, nullptr, 0) != 0 ||
        size != sizeof(boot) || boot.tv_sec <= 0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(boot.tv_sec);
}

std::optional<std::uint32_t> process_count() {
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL};
    std::size_t bytes = 0;
    if (sysctl(mib, 3, nullptr, &bytes, nullptr, 0) != 0 || bytes == 0 ||
        bytes > 16 * 1024 * 1024) {
        return std::nullopt;
    }
    // The process list may grow between the size and data calls.
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::size_t capacity = bytes + 32 * sizeof(kinfo_proc);
        if (capacity > 16 * 1024 * 1024) {
            return std::nullopt;
        }
        std::vector<unsigned char> data(capacity);
        bytes = capacity;
        if (sysctl(mib, 3, data.data(), &bytes, nullptr, 0) == 0) {
            if (bytes == 0 || bytes % sizeof(kinfo_proc) != 0 ||
                bytes / sizeof(kinfo_proc) > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            return static_cast<std::uint32_t>(bytes / sizeof(kinfo_proc));
        }
        if (sysctl(mib, 3, nullptr, &bytes, nullptr, 0) != 0) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<BatteryStats> battery_stats() {
    const CFTypeRef snapshot = IOPSCopyPowerSourcesInfo();
    if (!snapshot) {
        return std::nullopt;
    }
    const CFArrayRef sources = IOPSCopyPowerSourcesList(snapshot);
    if (!sources) {
        CFRelease(snapshot);
        return std::nullopt;
    }
    std::optional<BatteryStats> result;
    for (CFIndex index = 0; index < CFArrayGetCount(sources); ++index) {
        const CFDictionaryRef description = IOPSGetPowerSourceDescription(
            snapshot, CFArrayGetValueAtIndex(sources, index));
        if (!description) {
            continue;
        }
        const auto type = static_cast<CFStringRef>(CFDictionaryGetValue(
            description, CFSTR(kIOPSTypeKey)));
        if (!type || CFGetTypeID(type) != CFStringGetTypeID() ||
            !CFEqual(type, CFSTR(kIOPSInternalBatteryType))) {
            continue;
        }
        const auto current = static_cast<CFNumberRef>(CFDictionaryGetValue(
            description, CFSTR(kIOPSCurrentCapacityKey)));
        const auto maximum = static_cast<CFNumberRef>(CFDictionaryGetValue(
            description, CFSTR(kIOPSMaxCapacityKey)));
        double charge = 0.0;
        double capacity = 0.0;
        if (!current || !maximum || CFGetTypeID(current) != CFNumberGetTypeID() ||
            CFGetTypeID(maximum) != CFNumberGetTypeID() ||
            !CFNumberGetValue(current, kCFNumberDoubleType, &charge) ||
            !CFNumberGetValue(maximum, kCFNumberDoubleType, &capacity) ||
            !std::isfinite(charge) || !std::isfinite(capacity) ||
            capacity <= 0.0 || charge < 0.0 || charge > capacity) {
            continue;
        }
        const auto charging_value = static_cast<CFBooleanRef>(CFDictionaryGetValue(
            description, CFSTR(kIOPSIsChargingKey)));
        std::optional<bool> charging;
        if (charging_value && CFGetTypeID(charging_value) == CFBooleanGetTypeID()) {
            charging = CFBooleanGetValue(charging_value);
        }
        const auto power = static_cast<CFStringRef>(CFDictionaryGetValue(
            description, CFSTR(kIOPSPowerSourceStateKey)));
        std::optional<bool> on_ac;
        if (power && CFGetTypeID(power) == CFStringGetTypeID()) {
            if (CFEqual(power, CFSTR(kIOPSACPowerValue))) {
                on_ac = true;
            } else if (CFEqual(power, CFSTR(kIOPSBatteryPowerValue))) {
                on_ac = false;
            }
        }
        result = BatteryStats{100.0 * charge / capacity, charging, on_ac};
        break;
    }
    CFRelease(sources);
    CFRelease(snapshot);
    return result;
}

void collect_extra_stats(SystemStats& stats) {
    double loads[3]{};
    if (getloadavg(loads, 3) == 3 && std::isfinite(loads[0]) &&
        std::isfinite(loads[1]) && std::isfinite(loads[2]) &&
        loads[0] >= 0.0 && loads[1] >= 0.0 && loads[2] >= 0.0) {
        stats.load_average = std::array<double, 3>{loads[0], loads[1], loads[2]};
    }
    stats.process_count = process_count();
    stats.battery = battery_stats();
}

}  // namespace

SystemInfo collect_system_info() {
    SystemInfo info;
    info.os_version = sysctl_string("kern.osproductversion");
    info.host_model = sysctl_string("hw.model");
    info.cpu_model = sysctl_string("machdep.cpu.brand_string");
    info.boot_time_seconds = boot_time();

    utsname name{};
    if (uname(&name) == 0) {
        info.kernel = std::string(name.sysname) + " " + name.release;
        info.architecture = name.machine;
        info.hostname = name.nodename;
    }
    const char* shell = std::getenv("SHELL");
    if (shell && *shell) {
        info.shell = shell;
    }
    // TERM describes capabilities (for example, xterm-256color), not the emulator.
    const char* term = std::getenv("TERM_PROGRAM");
    if (term && *term) {
        info.terminal = term;
    }
    return info;
}

SystemStats collect_system_stats() {
    static std::optional<host_cpu_load_info_data_t> previous_ticks;
    SystemStats stats;
    const host_t host = mach_host_self();
    if (host == MACH_PORT_NULL) {
        previous_ticks.reset();
        stats.disk = disk_usage();
        collect_extra_stats(stats);
        return stats;
    }

    const auto current_ticks = cpu_ticks(host);
    const auto sample_started = std::chrono::steady_clock::now();
    stats.memory = memory_usage(host);
    stats.disk = disk_usage();

    if (!current_ticks) {
        previous_ticks.reset();
    } else if (previous_ticks) {
        stats.cpu_percent = cpu_usage(*previous_ticks, *current_ticks);
        if (stats.cpu_percent) {
            previous_ticks = current_ticks;
        }
    } else {
        // Prime the first frame; later frames use the longer live refresh interval.
        std::this_thread::sleep_until(sample_started + std::chrono::milliseconds(200));
        const auto next_ticks = cpu_ticks(host);
        if (next_ticks) {
            stats.cpu_percent = cpu_usage(*current_ticks, *next_ticks);
            previous_ticks = stats.cpu_percent ? next_ticks : current_ticks;
        }
    }

    mach_port_deallocate(mach_task_self(), host);
    collect_extra_stats(stats);
    return stats;
}

}  // namespace pulse
