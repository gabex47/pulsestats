#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <util.h>

#include <cerrno>
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>

namespace {

bool read_until(const int descriptor, std::string& output,
                const std::string_view needle, const std::size_t start) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (output.find(needle, start) != std::string::npos) {
            return true;
        }
        pollfd event{descriptor, POLLIN | POLLHUP, 0};
        const int ready = poll(&event, 1, 100);
        if (ready < 0 && errno != EINTR) {
            return false;
        }
        if (ready > 0 && (event.revents & (POLLIN | POLLHUP)) != 0) {
            char buffer[4096];
            const auto bytes = read(descriptor, buffer, sizeof(buffer));
            if (bytes > 0) {
                output.append(buffer, static_cast<std::size_t>(bytes));
                if (output.size() > 100000) {
                    return false;
                }
            } else if (bytes == 0 || (errno != EINTR && errno != EAGAIN)) {
                return false;
            }
        }
    }
    return false;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        return 1;
    }
    winsize size{};
    size.ws_col = 80;
    size.ws_row = 24;
    int master = -1;
    int slave = -1;
    if (openpty(&master, &slave, nullptr, nullptr, &size) != 0) {
        std::cerr << "Could not create test terminal\n";
        return 1;
    }
    const pid_t child = fork();
    if (child == -1) {
        close(master);
        close(slave);
        return 1;
    }
    if (child == 0) {
        close(master);
        if (dup2(slave, STDIN_FILENO) < 0 || dup2(slave, STDOUT_FILENO) < 0 ||
            dup2(slave, STDERR_FILENO) < 0) {
            _exit(127);
        }
        close(slave);
        execl(argv[1], argv[1], static_cast<char*>(nullptr));
        _exit(127);
    }
    close(slave);
    std::string output;
    bool success = read_until(master, output, "\x1b[H\x1b[2KPULSE", 0);
    if (success) {
        const std::size_t start = output.size();
        size.ws_col = 120;
        size.ws_row = 30;
        success = ioctl(master, TIOCSWINSZ, &size) == 0 &&
                  kill(child, SIGWINCH) == 0 &&
                  read_until(master, output, "\x1b[H\x1b[2K╭", start);
    }
    if (success) {
        const std::size_t start = output.size();
        size.ws_col = 50;
        size.ws_row = 12;
        success = ioctl(master, TIOCSWINSZ, &size) == 0 &&
                  kill(child, SIGWINCH) == 0 &&
                  read_until(master, output, "\x1b[H\x1b[2KPULSE", start);
    }
    if (success) {
        success = kill(child, SIGINT) == 0 &&
                  read_until(master, output, "\x1b[?25h\x1b[?1049l", 0);
    }
    if (!success) {
        kill(child, SIGKILL);
    }
    int status = 0;
    waitpid(child, &status, 0);
    close(master);
    if (!success || !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
        output.find('\n') != std::string::npos) {
        std::cerr << "Resize, in-place redraw, or terminal cleanup failed\n";
        return 1;
    }
    return 0;
}
