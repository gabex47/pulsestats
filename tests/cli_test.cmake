function(run_case name expected_status)
    execute_process(
        COMMAND "${PULSE_BINARY}" ${ARGN}
        RESULT_VARIABLE actual_status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE errors
    )
    if(NOT "${actual_status}" STREQUAL "${expected_status}")
        message(FATAL_ERROR "${name}: expected status ${expected_status}, got ${actual_status}: ${errors}")
    endif()
    if(expected_status EQUAL 0)
        if(name STREQUAL "help" OR name STREQUAL "--help")
            if(NOT output MATCHES "Usage:" OR NOT output MATCHES "pulse stats")
                message(FATAL_ERROR "${name}: missing help content")
            endif()
        else()
            foreach(label CPU TEMP MEMORY DISK GPU)
                if(NOT output MATCHES "${label}[ ]+.+")
                    message(FATAL_ERROR "${name}: missing ${label} row")
                endif()
            endforeach()
        endif()
    elseif(NOT errors MATCHES "Pulse: unknown command 'bogus'. Run 'pulse help' for usage.")
        message(FATAL_ERROR "${name}: missing useful error: ${errors}")
    endif()
endfunction()

run_case(default 0)
run_case(stats 0 stats)
run_case(help 0 help)
run_case(--help 0 --help)
run_case(invalid 2 bogus)
