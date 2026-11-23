# Builds the user-mode package program from source and runs it under the
# checked-in kernel. The program uses <kr232/cons.h>, <kr232/gfx.h>,
# <kr232/fb.h> and <kr232/input.h> and never names a call number, so a failure
# here is a failure of a package rather than of a test.
#
# Required: KRC KRLD VM KERNEL RUNTIME INCLUDE FIXTURE WORK FRAME_HASH

if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED VM OR NOT DEFINED KERNEL)
    message(FATAL_ERROR "KRC, KRLD, VM and KERNEL are required")
endif()
if(NOT DEFINED RUNTIME OR NOT DEFINED INCLUDE OR NOT DEFINED FIXTURE OR
   NOT DEFINED WORK OR NOT DEFINED FRAME_HASH)
    message(FATAL_ERROR "RUNTIME, INCLUDE, FIXTURE, WORK and FRAME_HASH are required")
endif()

file(MAKE_DIRECTORY "${WORK}")

set(sources
    "${RUNTIME}/crt0_user.s"
    "${RUNTIME}/syscall6.s"
    "${RUNTIME}/cons_user.c"
    "${RUNTIME}/gfx_user.c"
    "${RUNTIME}/fb_user.c"
    "${RUNTIME}/input_user.c"
    "${FIXTURE}")

set(objects "")
foreach(source IN LISTS sources)
    get_filename_component(name "${source}" NAME_WE)
    set(object "${WORK}/${name}.o")
    execute_process(COMMAND "${KRC}" -c --target kr232 -I "${INCLUDE}"
                            "${source}" -o "${object}"
                    RESULT_VARIABLE compiled ERROR_VARIABLE compile_error)
    if(NOT compiled EQUAL 0)
        message(FATAL_ERROR "compiling ${source} failed:\n${compile_error}")
    endif()
    list(APPEND objects "${object}")
endforeach()

set(image "${WORK}/packages.k232")
execute_process(COMMAND "${KRLD}" --base=0x08000000 --entry=_start ${objects}
                        -o "${image}"
                RESULT_VARIABLE linked ERROR_VARIABLE link_error)
if(NOT linked EQUAL 0)
    message(FATAL_ERROR "linking the package program failed:\n${link_error}")
endif()

set(run_args --transcript --frame-hash)
if(DEFINED INPUT AND NOT INPUT STREQUAL "")
    # Console input the program reads back through cons_getc.
    list(APPEND run_args --input "${INPUT}")
endif()

execute_process(COMMAND "${VM}" vm "${KERNEL}" --user "${image}"
                        ${run_args} --max-steps 200000
                OUTPUT_VARIABLE output ERROR_VARIABLE error
                RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the package program exited ${result}:\n${output}${error}")
endif()
if(NOT output MATCHES "console:\npackages")
    message(FATAL_ERROR "the package program did not print through cons_puts:\n${output}")
endif()
if(NOT output MATCHES "frame ([0-9a-f]+)")
    message(FATAL_ERROR "the emulator printed no frame hash:\n${output}${error}")
endif()
string(REGEX REPLACE ".*frame ([0-9a-f]+).*" "\\1" measured "${output}")
if(NOT measured STREQUAL FRAME_HASH)
    message(FATAL_ERROR "expected frame ${FRAME_HASH}, got ${measured}\n${output}")
endif()
if(DEFINED EXPECT_CONSOLE AND NOT EXPECT_CONSOLE STREQUAL "")
    if(NOT output MATCHES "console:\n${EXPECT_CONSOLE}")
        message(FATAL_ERROR
            "expected console text '${EXPECT_CONSOLE}' in:\n${output}")
    endif()
endif()
message(STATUS "the packages printed, drew ${measured} and exited 0")
