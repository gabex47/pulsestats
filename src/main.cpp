#include "pulse/display.hpp"
#include "pulse/stats.hpp"
#include "pulse/terminal.hpp"

#include <poll.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

volatile std::sig_atomic_t stop_signal = 0;
volatile std::sig_atomic_t resized = 0;

void request_stop(const int signal) {
    stop_signal = signal;
}

void request_resize(int) {
    resized = 1;
}

std::string printable_argument(const std::string_view value) {
    std::string result;
    for (std::size_t index = 0; index < value.size() && index < 80; ++index) {
        const auto ch = static_cast<unsigned char>(value[index]);
        result += ch >= 32 && ch <= 126 ? static_cast<char>(ch) : '?';
    }
    if (value.size() > 80) {
        result += "…";
    }
    return result;
}

bool parse_interval(const char* input, double& output) {
    if (!input || !*input) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const double seconds = std::strtod(input, &end);
    if (errno != 0 || end == input || *end != '\0' || !std::isfinite(seconds) ||
        seconds < 0.25 || seconds > 60.0) {
        return false;
    }
    output = seconds;
    return true;
}

bool install_handlers() {
    struct sigaction action {};
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    struct sigaction resize_action {};
    resize_action.sa_handler = request_resize;
    sigemptyset(&resize_action.sa_mask);
    return sigaction(SIGINT, &action, nullptr) == 0 &&
           sigaction(SIGTERM, &action, nullptr) == 0 &&
           sigaction(SIGHUP, &action, nullptr) == 0 &&
           sigaction(SIGQUIT, &action, nullptr) == 0 &&
           sigaction(SIGWINCH, &resize_action, nullptr) == 0;
}

void wait_until(const std::chrono::steady_clock::time_point deadline,
                pulse::TerminalScreen& screen, const pulse::SystemInfo& info,
                const pulse::SystemStats& stats, const double interval) {
    while (stop_signal == 0) {
        if (resized != 0) {
            resized = 0;
            screen.draw(pulse::render_dashboard(info, stats, pulse::terminal_size(), interval));
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            return;
        }
        const int wait_ms = static_cast<int>(std::min<std::int64_t>(remaining, 60000));
        if (poll(nullptr, 0, wait_ms) < 0 && errno != EINTR) {
            return;
        }
    }
}

int run_live(const double interval_seconds) {
    if (!install_handlers()) {
        std::cerr << "Pulse: unable to install signal handlers.\n";
        return 1;
    }
    const pulse::SystemInfo info = pulse::collect_system_info();
    const auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(interval_seconds));
    pulse::TerminalScreen screen(std::cout);
    while (stop_signal == 0) {
        const auto deadline = std::chrono::steady_clock::now() + interval;
        const auto stats = pulse::collect_system_stats();
        if (stop_signal != 0) {
            break;
        }
        resized = 0;
        screen.draw(pulse::render_dashboard(info, stats, pulse::terminal_size(),
                                            interval_seconds));
        wait_until(deadline, screen, info, stats, interval_seconds);
    }
    return stop_signal == SIGINT || stop_signal == 0 ? 0 : 128 + stop_signal;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc >= 2) {
        const std::string_view first(argv[1]);
        if (first == "help" || first == "--help") {
            if (argc != 2) {
                std::cerr << "Pulse: too many arguments. Run 'pulse help' for usage.\n";
                return 2;
            }
            std::cout << pulse::help_text();
            return 0;
        }
        if (first == "--version") {
            if (argc != 2) {
                std::cerr << "Pulse: too many arguments. Run 'pulse help' for usage.\n";
                return 2;
            }
            std::cout << "Pulse v" PULSE_VERSION "\n";
            return 0;
        }
    }

    int index = 1;
    if (index < argc && std::string_view(argv[index]) == "stats") {
        ++index;
    }
    double interval_seconds = 1.0;
    bool interval_seen = false;
    while (index < argc) {
        const std::string_view argument(argv[index]);
        if (argument == "-i" || argument == "--interval") {
            if (interval_seen || index + 1 >= argc ||
                !parse_interval(argv[index + 1], interval_seconds)) {
                std::cerr << "Pulse: interval must be a number from 0.25 to 60 seconds.\n";
                return 2;
            }
            interval_seen = true;
            index += 2;
            continue;
        }
        if (index > 1) {
            std::cerr << "Pulse: too many arguments. Run 'pulse help' for usage.\n";
        } else {
            std::cerr << "Pulse: unknown command '" << printable_argument(argument)
                      << "'. Run 'pulse help' for usage.\n";
        }
        return 2;
    }
    return run_live(interval_seconds);
}
