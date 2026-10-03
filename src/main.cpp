#include "pulse/display.hpp"
#include "pulse/stats.hpp"
#include "pulse/terminal.hpp"

#include <chrono>
#include <csignal>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

volatile std::sig_atomic_t stop_signal = 0;

void request_stop(const int signal) {
    stop_signal = signal;
}

int run_live() {
    struct sigaction action {};
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, nullptr) != 0 ||
        sigaction(SIGTERM, &action, nullptr) != 0) {
        std::cerr << "Pulse: unable to install signal handlers.\n";
        return 1;
    }

    pulse::TerminalScreen screen(std::cout);
    while (stop_signal == 0) {
        const auto next_refresh = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(500);
        const auto stats = pulse::collect_system_stats();
        if (stop_signal != 0) {
            break;
        }
        screen.draw(pulse::render_stats(stats));
        std::this_thread::sleep_until(next_refresh);
    }
    return stop_signal == SIGTERM ? 128 + SIGTERM : 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc == 1) {
        return run_live();
    }

    if (argc == 2) {
        const std::string_view command(argv[1]);
        if (command == "stats") {
            return run_live();
        }
        if (command == "help" || command == "--help") {
            std::cout << pulse::help_text();
            return 0;
        }
        std::cerr << "Pulse: unknown command '" << command
                  << "'. Run 'pulse help' for usage.\n";
        return 2;
    }

    std::cerr << "Pulse: too many arguments. Run 'pulse help' for usage.\n";
    return 2;
}
