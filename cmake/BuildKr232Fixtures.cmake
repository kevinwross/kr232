# Builds every kr232 test fixture from its source into OUT.
#
# The fixtures are images of programs whose sources sit beside them in
# tests/fixtures, plus the kernel from targets/kr232/kernel. Building them here
# means a clean tree has every image the tests read, instead of carrying the
# binaries in the repository.
#
# Every compiler input is named relative to ROOT and krc runs there, because the
# source path is embedded in the object: an absolute path made the image depend
# on the machine that built it.
#
# The two CLI probes are flat images, not containers, and each names its load
# base because the flat loader sets PC to that base: halt.bin is loaded at the
# user base, kernel-draw.bin at 0x104, which is where its code sat when it was a
# hand-encoded file.
#
# Required: ROOT, KRC, KRLD, RUNTIME, OUT

foreach(required ROOT KRC KRLD RUNTIME OUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUT}")
file(RELATIVE_PATH runtime "${ROOT}" "${RUNTIME}")
set(fixtures "tests/fixtures")

function(compile out source)
    execute_process(COMMAND "${KRC}" -c --target kr232 ${ARGN} "${source}" -o "${out}"
                    WORKING_DIRECTORY "${ROOT}"
                    RESULT_VARIABLE state ERROR_VARIABLE error)
    if(NOT state EQUAL 0)
        message(FATAL_ERROR "compiling ${source} failed:\n${error}")
    endif()
endfunction()

function(link image)
    execute_process(COMMAND "${KRLD}" ${ARGN} -o "${OUT}/${image}"
                    RESULT_VARIABLE state ERROR_VARIABLE error)
    if(NOT state EQUAL 0)
        message(FATAL_ERROR "linking ${image} failed:\n${error}")
    endif()
    message(STATUS "built ${image}")
endfunction()

# One source, one image, no extra arguments: kernel-mode containers.
set(simple
    "user-exit42.s|user-exit42.kr232|--base=0x08000000"
    "user-echo.s|user-echo.kr232|--base=0x08000000"
    "user-text.s|user-text.kr232|--base=0x08000000"
    "blank-flip.s|blank-flip.kr232|none"
    "kernel-gate.s|kernel-gate.bin|none")

foreach(entry IN LISTS simple)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 source)
    list(GET fields 1 image)
    list(GET fields 2 base)
    get_filename_component(stem "${source}" NAME_WE)
    compile("${OUT}/${stem}.o" "${fixtures}/${source}")
    if(base STREQUAL "none")
        link("${image}" --entry=_start "${OUT}/${stem}.o")
    else()
        link("${image}" "${base}" --entry=_start "${OUT}/${stem}.o")
    endif()
endforeach()

# The two flat CLI probes. Their load base is part of the image's contract.
get_filename_component(halt_stem "halt.s" NAME_WE)
compile("${OUT}/${halt_stem}.o" "${fixtures}/halt.s")
link("halt.bin" --flat --base=0x08000000 --entry=_start "${OUT}/${halt_stem}.o")

get_filename_component(draw_stem "kernel-draw.s" NAME_WE)
compile("${OUT}/${draw_stem}.o" "${fixtures}/kernel-draw.s")
link("kernel-draw.bin" --flat --base=0x104 --entry=_start "${OUT}/${draw_stem}.o")

# A user-mode program that checks every kernel call's STATUS: it needs the
# startup and the variadic stub.
compile("${OUT}/crt0_user.o" "${runtime}/crt0_user.s")
compile("${OUT}/syscall6.o" "${runtime}/syscall6.s")
compile("${OUT}/user-calls.o" "${fixtures}/user-calls.c")
link("user-calls.kr232" --base=0x08000000 --entry=_start
     "${OUT}/user-calls.o" "${OUT}/crt0_user.o" "${OUT}/syscall6.o")

# The kernel is a fixture like the rest.
compile("${OUT}/kernel.o" "targets/kr232/kernel/kernel.s")
link("kernel.kr232" --entry=_start "${OUT}/kernel.o")
