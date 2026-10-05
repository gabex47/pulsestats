#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <util.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::size_t occurrences(const std::string& text, const std::string_view token) {
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(token, position)) != std::string::npos) {
        ++count;
        position += token.size();
    }
    return count;
}

bool run_live(const char* binary, const std::vector<const char*>& arguments,
              const int frames_before_interrupt) {
    winsize size{};
    size.ws_col = 80;
    size.ws_row = 24;
    int master = -1;
    int slave = -1;
    if (openpty(&master, &slave, nullptr, nullptr, &size) != 0) {
        std::cerr << "Could not create test terminal\n";
        return false;
    }

    const pid_t child = fork();
    if (child == -1) {
        close(master);
        close(slave);
        std::cerr << "Could not start Pulse\n";
        return false;
    }
    if (child == 0) {
        close(master);
        if (dup2(slave, STDIN_FILENO) == -1 ||
            dup2(slave, STDOUT_FILENO) == -1 ||
            dup2(slave, STDERR_FILENO) == -1) {
            _exit(127);
        }
        close(slave);
        setenv("TERM", "xterm-256color", 1);
        unsetenv("NO_COLOR");
        std::vector<char*> child_args;
        child_args.push_back(const_cast<char*>(binary));
        for (const char* argument : arguments) {
            child_args.push_back(const_cast<char*>(argument));
        }
        child_args.push_back(nullptr);
        execv(binary, child_args.data());
        _exit(127);
    }

    close(slave);
    std::string output;
    std::string error;
    bool sent_interrupt = false;
    bool exited = false;
    bool end_of_output = false;
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);

    while (std::chrono::steady_clock::now() < deadline && !(exited && end_of_output)) {
        struct pollfd descriptor { master, POLLIN | POLLHUP, 0 };
        const int ready = poll(&descriptor, 1, 100);
        if (ready == -1 && errno != EINTR) {
            error = "Could not read Pulse output";
            break;
        }
        if (ready > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0) {
            char buffer[1024];
            const ssize_t bytes = read(master, buffer, sizeof(buffer));
            if (bytes > 0) {
                output.append(buffer, static_cast<std::size_t>(bytes));
            } else if (bytes == 0 || errno == EIO) {
                end_of_output = true;
            } else if (errno != EINTR) {
                error = "Could not read Pulse output";
                break;
            }
        }

        const bool ready_to_interrupt = frames_before_interrupt == 0
            ? occurrences(output, "\x1b[?25l") >= 1
            : occurrences(output, "\x1b[H\x1b[2K") >=
                  static_cast<std::size_t>(frames_before_interrupt);
        if (!sent_interrupt && ready_to_interrupt) {
            if (kill(child, SIGINT) != 0) {
                error = "Pulse exited before SIGINT";
                break;
            }
            sent_interrupt = true;
        }
        if (!exited) {
            const pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) {
                exited = true;
            } else if (waited == -1 && errno != EINTR) {
                error = "Could not wait for Pulse";
                break;
            }
        }
        if (output.size() > 16384) {
            error = "Pulse emitted too much output before SIGINT";
            break;
        }
        if (exited && !sent_interrupt) {
            error = "Pulse exited before entering live mode";
            break;
        }
    }

    if (!exited) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
    }
    close(master);

    if (error.empty() && (!exited || !end_of_output)) {
        error = "Pulse did not stop promptly after SIGINT";
    }
    if (error.empty() && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)) {
        error = "Pulse did not exit successfully after SIGINT";
    }
    if (error.empty() &&
        (occurrences(output, "\x1b[?1049h") != 1 ||
         occurrences(output, "\x1b[?25l") != 1 ||
         (frames_before_interrupt > 0 && occurrences(output, "\x1b[H\x1b[2K") <
              static_cast<std::size_t>(frames_before_interrupt)) ||
         occurrences(output, "\x1b[?25h") != 1 ||
         occurrences(output, "\x1b[?1049l") != 1)) {
        error = "Redraw or terminal cleanup sequences are missing";
    }
    const std::string cleanup = "\x1b[0m\x1b[?25h\x1b[?1049l";
    if (error.empty() &&
        (output.size() < cleanup.size() ||
         output.compare(output.size() - cleanup.size(), cleanup.size(), cleanup) != 0 ||
         (frames_before_interrupt >= 2 &&
          output.find("\x1b[J\x1b[H") == std::string::npos))) {
        error = "Frame boundary or final terminal state is incorrect";
    }
    if (error.empty() && frames_before_interrupt > 0 &&
        (output.find("CPU") == std::string::npos ||
         output.find("Ctrl+C") == std::string::npos)) {
        error = "Live statistics were not displayed";
    }
    if (error.empty() && frames_before_interrupt > 0 && arguments.size() >= 2 &&
        (std::string_view(arguments[0]) == "-i" ||
         std::string_view(arguments[0]) == "--interval") &&
        output.find(std::string(arguments[1]) + "s refresh") == std::string::npos) {
        error = "Requested refresh interval was not displayed";
    }
    if (error.empty() && frames_before_interrupt > 0 && arguments.size() >= 3 &&
        (std::string_view(arguments[1]) == "-i" ||
         std::string_view(arguments[1]) == "--interval") &&
        output.find(std::string(arguments[2]) + "s refresh") == std::string::npos) {
        error = "Requested refresh interval was not displayed";
    }
    if (error.empty() && !arguments.empty() &&
        std::string_view(arguments[0]) == "--no-color" &&
        (output.find("\x1b[96m") != std::string::npos ||
         output.find("\x1b[95m") != std::string::npos ||
         output.find("\x1b[32m") != std::string::npos)) {
        error = "--no-color emitted ANSI colors";
    }
    if (!error.empty()) {
        std::cerr << error << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Expected Pulse binary and optional arguments\n";
        return 1;
    }
    const bool interrupt_early = argc == 3 && std::string_view(argv[2]) == "--early";
    const bool interrupt_after_one = argc == 3 && std::string_view(argv[2]) == "--long";
    std::vector<const char*> arguments;
    if (interrupt_after_one) {
        arguments = {"--interval", "60"};
    } else if (!interrupt_early) {
        for (int index = 2; index < argc; ++index) {
            arguments.push_back(argv[index]);
        }
    }
    return run_live(argv[1], arguments, interrupt_early ? 0 : interrupt_after_one ? 1 : 2)
               ? 0 : 1;
}
