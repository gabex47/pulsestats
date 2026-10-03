#include "pulse/stats.hpp"

#include <mach/host_info.h>
#include <mach/mach.h>
#include <sys/mount.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <thread>

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
        const auto delta = static_cast<std::uint32_t>(
            after.cpu_ticks[state] - before.cpu_ticks[state]);
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
        fs.f_bavail > fs.f_blocks ||
        fs.f_blocks > std::numeric_limits<std::uint64_t>::max() / fs.f_bsize) {
        return std::nullopt;
    }

    const std::uint64_t block_size = fs.f_bsize;
    // Available space reflects the shared startup APFS capacity on modern macOS.
    return Usage{(fs.f_blocks - fs.f_bavail) * block_size,
                 fs.f_blocks * block_size};
}

}  // namespace

SystemStats collect_system_stats() {
    SystemStats stats;
    const host_t host = mach_host_self();
    if (host == MACH_PORT_NULL) {
        stats.disk = disk_usage();
        return stats;
    }

    const auto first_ticks = cpu_ticks(host);
    const auto sample_started = std::chrono::steady_clock::now();
    stats.memory = memory_usage(host);
    stats.disk = disk_usage();

    if (first_ticks) {
        std::this_thread::sleep_until(sample_started + std::chrono::milliseconds(200));
        const auto second_ticks = cpu_ticks(host);
        if (second_ticks) {
            stats.cpu_percent = cpu_usage(*first_ticks, *second_ticks);
        }
    }

    mach_port_deallocate(mach_task_self(), host);
    return stats;
}

}  // namespace pulse
