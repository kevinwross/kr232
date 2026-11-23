# Runs the emulator over an image and compares its frame hash with a golden
# file. The gate that depends on this is plan §6 item 4: the hash must match a
# checked-in value, not one recomputed on the fly.
if(NOT DEFINED EXE OR NOT DEFINED IMAGE OR NOT DEFINED GOLDEN)
    message(FATAL_ERROR "EXE, IMAGE and GOLDEN are required")
endif()

file(READ "${GOLDEN}" expected)
string(STRIP "${expected}" expected)

execute_process(
    COMMAND "${EXE}" vm "${IMAGE}" --max-steps 200 --frame-hash
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
message(STATUS "frame hash ${expected} matches the golden file")
