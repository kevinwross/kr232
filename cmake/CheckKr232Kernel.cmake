# Runs the checked-in kernel with a user program and checks what came back.
#
# The kernel is guest code that only krc can build, so the image is checked in;
# when KRC and KRLD are given the kernel is also rebuilt from source and must
# match the checked-in file byte for byte, which is how drift is caught.
#
# Required: VM, KERNEL, PROGRAM, EXPECT_EXIT
# Optional: ROOT, KRC, KRLD, WORK -- rebuild the kernel and require it to match
#           FRAME_HASH        -- the frame the program must commit
#           BLANK_IMAGE       -- a program that commits without drawing; the
#                                golden may not equal its frame
#           INPUT             -- text queued on the console before the run
#           EXPECT_CONSOLE    -- text the console must contain

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
    # Named relative to ROOT and compiled there: the source path is embedded in
    # the object, so an absolute one made the image depend on the host that built
    # it rather than on the source.
    execute_process(COMMAND "${KRC}" -c --target kr232
                            "targets/kr232/kernel/kernel.s"
                            -o "${WORK}/kernel.o"
                    WORKING_DIRECTORY "${ROOT}"
                    RESULT_VARIABLE compiled ERROR_VARIABLE compile_error)
    if(NOT compiled EQUAL 0)
        message(FATAL_ERROR "building the kernel failed:\n${compile_error}")
    endif()
    execute_process(COMMAND "${KRLD}" --entry=_start
                            "${WORK}/kernel.o" -o "${WORK}/kernel.kr232"
                    RESULT_VARIABLE linked ERROR_VARIABLE link_error)
    if(NOT linked EQUAL 0)
        message(FATAL_ERROR "linking the kernel failed:\n${link_error}")
    endif()
    file(SHA256 "${KERNEL}" checked_in)
    file(SHA256 "${WORK}/kernel.kr232" rebuilt)
    if(NOT checked_in STREQUAL rebuilt)
        message(FATAL_ERROR
            "the kernel in the tree does not match the source: rebuild "
            "tests/fixtures/kernel.kr232 with krc and commit it")
    endif()
    set(image "${WORK}/kernel.kr232")
endif()

set(optional_args)
if(DEFINED FRAME_HASH AND NOT FRAME_HASH STREQUAL "")
    list(APPEND optional_args --frame-hash)
endif()
if(DEFINED INPUT AND NOT INPUT STREQUAL "")
    list(APPEND optional_args --input "${INPUT}")
endif()

execute_process(COMMAND "${VM}" vm "${image}" --user "${PROGRAM}" --max-steps 200000 --transcript
                        ${optional_args}
                OUTPUT_VARIABLE output ERROR_VARIABLE error
                RESULT_VARIABLE result)
if(NOT result EQUAL EXPECT_EXIT)
    message(FATAL_ERROR "expected exit ${EXPECT_EXIT}, got ${result}\n${output}${error}")
endif()

if(DEFINED EXPECT_CONSOLE AND NOT EXPECT_CONSOLE STREQUAL "")
    if(NOT output MATCHES "console:\n${EXPECT_CONSOLE}")
        message(FATAL_ERROR
            "expected console text '${EXPECT_CONSOLE}' in:\n${output}")
    endif()
endif()

if(DEFINED FRAME_HASH AND NOT FRAME_HASH STREQUAL "")
    if(NOT output MATCHES "frame ([0-9a-f]+)")
        message(FATAL_ERROR "the emulator printed no frame hash:\n${output}${error}")
    endif()
    set(measured "${CMAKE_MATCH_1}")
    if(NOT measured STREQUAL FRAME_HASH)
        message(FATAL_ERROR "expected frame ${FRAME_HASH}, got ${measured}")
    endif()
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
endif()

message(STATUS "the kernel ran the program and returned ${result}")
