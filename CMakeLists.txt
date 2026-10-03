cmake_minimum_required(VERSION 3.20)
project(Pulse LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR "Pulse currently supports macOS only")
endif()

add_library(pulse_core STATIC
    src/display.cpp
    src/terminal.cpp
    src/platform/macos/stats.cpp
)
target_include_directories(pulse_core PUBLIC include)
target_compile_features(pulse_core PUBLIC cxx_std_17)

add_executable(pulse src/main.cpp)
target_link_libraries(pulse PRIVATE pulse_core)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(pulse_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    target_compile_options(pulse PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(pulse_tests tests/pulse_tests.cpp)
    target_link_libraries(pulse_tests PRIVATE pulse_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(pulse_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
    add_test(NAME stats_and_display COMMAND pulse_tests)
    add_test(NAME cli COMMAND "${CMAKE_COMMAND}"
        "-DPULSE_BINARY=$<TARGET_FILE:pulse>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.cmake")

    add_executable(pulse_live_tests tests/live_test.cpp)
    target_compile_features(pulse_live_tests PRIVATE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(pulse_live_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
    add_test(NAME live_default COMMAND pulse_live_tests $<TARGET_FILE:pulse>)
    add_test(NAME live_stats COMMAND pulse_live_tests $<TARGET_FILE:pulse> stats)
    add_test(NAME live_early_interrupt COMMAND pulse_live_tests $<TARGET_FILE:pulse> --early)
endif()
