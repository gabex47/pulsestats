#include "pulse/display.hpp"
#include "pulse/stats.hpp"

#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    if (argc == 1) {
        std::cout << pulse::render_stats(pulse::collect_system_stats());
        return 0;
    }

    if (argc == 2) {
        const std::string_view command(argv[1]);
        if (command == "stats") {
            std::cout << pulse::render_stats(pulse::collect_system_stats());
            return 0;
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
