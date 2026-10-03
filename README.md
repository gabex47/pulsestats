# Pulse

Pulse is an open-source, lightweight native terminal utility for system information. It is currently beginning Phase 1 of development.

## Build

On macOS, with CMake and a C++17 compiler:

```sh
cmake -S . -B build
cmake --build build
```

## Run

```sh
./build/pulse
./build/pulse stats
./build/pulse help
```

Run `ctest --test-dir build --output-on-failure` to run the tests.
