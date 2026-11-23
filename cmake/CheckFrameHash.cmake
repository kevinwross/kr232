# Runs the emulator over an image and compares its frame hash with a golden
# file. The gate that depends on this is plan §6 item 4: the hash must match a
# checked-in value, not one recomputed on the fly.
if(NOT DEFINED EXE OR NOT DEFINED IMAGE OR NOT DEFINED GOLDEN)
    message(FATAL_ERROR "EXE, IMAGE and GOLDEN are required")
endif()

file(READ "${GOLDEN}" expected)
string(STRIP "${expected}" expected)

execute_process(
    # Enough steps for a drawing program to finish: a golden recorded mid-run
    # pins a frame the program never intended to commit.
    COMMAND "${EXE}" vm "${IMAGE}" --max-steps 100000 --frame-hash
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    RESULT_VARIABLE code)

if(NOT code EQUAL 0)
    message(FATAL_ERROR "the emulator exited ${code}: ${errors}")
endif()
if(NOT output MATCHES "frame ([0-9a-f]+)")
    message(FATAL_ERROR "no frame hash in the output: ${output}")
endif()
if(NOT CMAKE_MATCH_1 STREQUAL expected)
    message(FATAL_ERROR "frame hash ${CMAKE_MATCH_1} does not match golden ${expected}")
endif()

# A drawing golden that equals the frame a program commits without drawing
# anything is not a drawing: that is how two goldens were recorded against a
# device whose clip rectangle clipped every pixel away.
if(DEFINED BLANK_IMAGE AND NOT BLANK_IMAGE STREQUAL "")
    execute_process(COMMAND "${EXE}" vm "${BLANK_IMAGE}" --max-steps 200 --frame-hash
                    OUTPUT_VARIABLE blank_output RESULT_VARIABLE blank_code)
    if(NOT blank_code EQUAL 0 OR NOT blank_output MATCHES "frame ([0-9a-f]+)")
        message(FATAL_ERROR "cannot measure the blank frame: ${blank_output}")
    endif()
    if(CMAKE_MATCH_1 STREQUAL expected)
        message(FATAL_ERROR
            "the golden ${expected} is the blank committed frame: this program draws nothing")
    endif()
endif()
message(STATUS "frame hash ${expected} matches the golden file")
