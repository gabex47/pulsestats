# Pulse

Pulse v0.1.4 is an open-source, lightweight native terminal system monitor for macOS on Apple Silicon. Its live, resize-aware dashboard shows your system at a glance without a background service or third-party dependencies.

Requirements: macOS, CMake 3.20 or newer, and a C++17 compiler (such as Apple Clang from Xcode Command Line Tools). No third-party libraries are needed.

## Build

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

## Test

```sh
ctest --test-dir build-release --output-on-failure
```

## Use

```sh
./build-release/pulse
./build-release/pulse stats
./build-release/pulse --interval 0.5
./build-release/pulse stats -i 2
./build-release/pulse --no-color
./build-release/pulse help
./build-release/pulse --help
./build-release/pulse --version
```

The dashboard refreshes in place every second by default. `-i` / `--interval` accepts 0.25–60 seconds. Press Ctrl+C to exit; Pulse restores the original terminal screen and cursor. Color is automatic; `--no-color` or `NO_COLOR=1` disables it. Non-interactive output (pipes or `TERM=dumb`) prints one plain ASCII snapshot and exits.

Pulse shows device and OS details when available, plus CPU usage, memory, startup-volume storage, battery state on portable Macs, load averages, and process count. Memory used is estimated from active, wired, and physically compressed pages, not Activity Monitor's available-memory figure. Disk used is total capacity minus space available to the user, which can include APFS reserved space. Unavailable values appear as a dash; uptime is omitted when boot time is unavailable. Temperature and system-wide GPU utilization remain unavailable without reliable readings.
