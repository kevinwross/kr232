# Rebuilds every checked-in kr232 fixture from its source and compares the bytes.
#
# A fixture is an image of a program whose source is in the tree: if the two
# drift, the tests keep passing against an old program and nothing says so. The
# kernel has had this row since it was added; this one covers the rest.
#
# Every compiler input is named relative to ROOT and krc runs there, because the
# source path is embedded in the object: an absolute path made the image differ
# between two hosts building identical sources, so the row could only pass on the
# machine that generated the fixture.
#
# Required: ROOT, KRC, KRLD, FIXTURE_DIR, WORK

if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED FIXTURE_DIR OR NOT DEFINED WORK
   OR NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT, KRC, KRLD, FIXTURE_DIR and WORK are required")
endif()
file(MAKE_DIRECTORY "${WORK}")
file(RELATIVE_PATH fixtures "${ROOT}" "${FIXTURE_DIR}")

function(compile out source)
    execute_process(COMMAND "${KRC}" -c --target kr232 ${ARGN} "${source}" -o "${out}"
                    WORKING_DIRECTORY "${ROOT}"
                    RESULT_VARIABLE state ERROR_VARIABLE error)
    if(NOT state EQUAL 0)
        message(FATAL_ERROR "compiling ${source} failed:\n${error}")
    endif()
endfunction()

function(compare image)
    file(SHA256 "${FIXTURE_DIR}/${image}" checked_in)
    file(SHA256 "${WORK}/${image}" rebuilt)
    if(NOT checked_in STREQUAL rebuilt)
        set(FAILURES
            "${FAILURES}  ${image} does not match its source: rebuild it with "
            "regenerate.ps1 and commit both\n" PARENT_SCOPE)
    endif()
endfunction()

set(FAILURES "")

# One source, one image, no extra arguments: these are flat kernel-mode images.
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
    list(GET fields 2 link_args)
    string(REPLACE ".s" "" stem "${source}")
    compile("${WORK}/${stem}.o" "${fixtures}/${source}")
    if(link_args STREQUAL "none")
        execute_process(COMMAND "${KRLD}" --entry=_start
                                "${WORK}/${stem}.o" -o "${WORK}/${image}"
                        RESULT_VARIABLE state ERROR_VARIABLE error)
    else()
        execute_process(COMMAND "${KRLD}" "${link_args}"
                                --entry=_start "${WORK}/${stem}.o" -o "${WORK}/${image}"
                        RESULT_VARIABLE state ERROR_VARIABLE error)
    endif()
    if(NOT state EQUAL 0)
        message(FATAL_ERROR "linking ${image} failed:\n${error}")
    endif()
    set(FAILURES "${FAILURES}" PARENT_SCOPE)
    compare("${image}")
endforeach()

# user-calls.c is a user-mode program: it needs the startup and the variadic stub,
# because it checks the STATUS each kernel call returns.
if(DEFINED RUNTIME)
    file(RELATIVE_PATH runtime "${ROOT}" "${RUNTIME}")
    compile("${WORK}/crt0_user.o" "${runtime}/crt0_user.s")
    compile("${WORK}/syscall6.o" "${runtime}/syscall6.s")
    compile("${WORK}/user-calls.o" "${fixtures}/user-calls.c")
    execute_process(COMMAND "${KRLD}" --base=0x08000000
                            --entry=_start "${WORK}/user-calls.o" "${WORK}/crt0_user.o"
                            "${WORK}/syscall6.o" -o "${WORK}/user-calls.kr232"
                    RESULT_VARIABLE state ERROR_VARIABLE error)
    if(NOT state EQUAL 0)
        message(FATAL_ERROR "linking user-calls.kr232 failed:\n${error}")
    endif()
    compare("user-calls.kr232")
endif()

if(NOT FAILURES STREQUAL "")
    message(FATAL_ERROR "fixtures do not match their sources:\n${FAILURES}")
endif()
message(STATUS "all fixture images match their sources")
