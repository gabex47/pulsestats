# Pulse

Pulse is an open-source, lightweight native terminal system monitor. It is currently supported and tested on macOS with Apple Silicon.

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
./build-release/pulse help
./build-release/pulse --help
```

`pulse` and `pulse stats` refresh in place about every 500 ms. Press Ctrl+C to exit.

Pulse reports overall CPU usage, used and total memory, and space used on the startup volume. Memory used is estimated from active, wired, and physically compressed pages; it may differ from Activity Monitor. Disk used means total capacity minus space available to the user, which can include APFS reserved space. Temperature and system-wide GPU usage show `N/A` when a reliable numeric reading is unavailable.
