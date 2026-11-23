# Runs the checked-in kernel with a user program and checks the host status.
#
# The kernel is guest code that only krc can build, so the image is checked in;
# when KRC and KRLD are given the kernel is also rebuilt from source and must
# match the checked-in file byte for byte, which is how drift is caught.
#
# Required: VM, KERNEL, PROGRAM, EXPECT_EXIT
# Optional: KRC, KRLD, WORK -- rebuild and compare

if(NOT DEFINED VM OR NOT DEFINED KERNEL OR NOT DEFINED PROGRAM OR NOT DEFINED EXPECT_EXIT)
    message(FATAL_ERROR "VM, KERNEL, PROGRAM and EXPECT_EXIT are required")
endif()
foreach(path IN ITEMS "${VM}" "${KERNEL}" "${PROGRAM}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "not found: ${path}")
    endif()
endforeach()

set(image "${KERNEL}")
if(DEFINED KRC AND DEFINED KRLD)
    if(NOT EXISTS "${KRC}" OR NOT EXISTS "${KRLD}")
        message(FATAL_ERROR "KRC and KRLD must exist when they are given")
    endif()
    file(MAKE_DIRECTORY "${WORK}")
    execute_process(COMMAND "${KRC}" -c --target kr232
                            "${CMAKE_CURRENT_LIST_DIR}/../targets/kr232/kernel/kernel.s"
                            -o "${WORK}/kernel.o"
                    RESULT_VARIABLE compiled ERROR_VARIABLE compile_error)
    if(NOT compiled EQUAL 0)
        message(FATAL_ERROR "building the kernel failed:\n${compile_error}")
    endif()
    execute_process(COMMAND "${KRLD}" --entry=_start "${WORK}/kernel.o"
                            -o "${WORK}/kernel.k232"
                    RESULT_VARIABLE linked ERROR_VARIABLE link_error)
    if(NOT linked EQUAL 0)
        message(FATAL_ERROR "linking the kernel failed:\n${link_error}")
    endif()
    file(SHA256 "${KERNEL}" checked_in)
    file(SHA256 "${WORK}/kernel.k232" rebuilt)
    if(NOT checked_in STREQUAL rebuilt)
        message(FATAL_ERROR
            "the kernel in the tree does not match the source: rebuild "
            "tests/fixtures/kernel.k232 with krc and commit it")
    endif()
    set(image "${WORK}/kernel.k232")
endif()

set(optional_args)
    # A golden that equals the blank committed frame is not a drawing.
    if(DEFINED BLANK_IMAGE AND NOT BLANK_IMAGE STREQUAL "")
        execute_process(COMMAND "${VM}" vm "${BLANK_IMAGE}" --frame-hash
                        OUTPUT_VARIABLE blank_output RESULT_VARIABLE blank_code)
        if(NOT blank_code EQUAL 0 OR NOT blank_output MATCHES "frame ([0-9a-f]+)")
            message(FATAL_ERROR "cannot measure the blank frame: ${blank_output}")
        endif()
        if(CMAKE_MATCH_1 STREQUAL FRAME_HASH)
            message(FATAL_ERROR
                "the golden ${FRAME_HASH} is the blank committed frame: nothing was drawn")
        endif()
    endif()
if(DEFINED FRAME_HASH AND NOT FRAME_HASH STREQUAL "")
    list(APPEND optional_args --frame-hash)
endif()

execute_process(COMMAND "${VM}" vm "${image}" --user "${PROGRAM}" --max-steps 200000 ${optional_args}
                OUTPUT_VARIABLE output ERROR_VARIABLE error
                RESULT_VARIABLE result)
if(NOT result EQUAL EXPECT_EXIT)
    message(FATAL_ERROR "expected exit ${EXPECT_EXIT}, got ${result}\n${output}${error}")
endif()
message(STATUS "the kernel entered the program and returned ${result}")
