# Rebuilds every checked-in kr232 fixture from its source and compares the bytes.
#
# A fixture is an image of a program whose source is in the tree: if the two
# drift, the tests keep passing against an old program and nothing says so. The
# kernel has had this row since it was added; this one covers the rest.
#
# Required: KRC, KRLD, FIXTURE_DIR, WORK

if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED FIXTURE_DIR OR NOT DEFINED WORK)
    message(FATAL_ERROR "KRC, KRLD, FIXTURE_DIR and WORK are required")
endif()
file(MAKE_DIRECTORY "${WORK}")

# source|image|link arguments; an empty field is dropped by some CMake policies,
# so "none" marks the fixtures that need no extra arguments.
set(fixtures
    "user-exit42.s|user-exit42.k232|--base=0x08000000"
    "user-calls.s|user-calls.k232|--base=0x08000000"
    "user-echo.s|user-echo.k232|--base=0x08000000"
    "user-text.s|user-text.k232|--base=0x08000000"
    "blank-flip.s|blank-flip.k232|none")

set(failures "")
foreach(entry IN LISTS fixtures)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 source)
    list(GET fields 1 image)
    list(GET fields 2 link_args)
    string(REPLACE ".s" "" stem "${source}")

    execute_process(COMMAND "${KRC}" -c --target kr232
                            "${FIXTURE_DIR}/${source}" -o "${WORK}/${stem}.o"
                    RESULT_VARIABLE compiled ERROR_VARIABLE compile_error)
    if(NOT compiled EQUAL 0)
        message(FATAL_ERROR "compiling ${source} failed:\n${compile_error}")
    endif()

    if(link_args STREQUAL "none")
        execute_process(COMMAND "${KRLD}" "--source-root=${FIXTURE_DIR}" --entry=_start "${WORK}/${stem}.o"
                                -o "${WORK}/${image}"
                        RESULT_VARIABLE linked ERROR_VARIABLE link_error)
    else()
        execute_process(COMMAND "${KRLD}" "${link_args}" "--source-root=${FIXTURE_DIR}" --entry=_start
                                "${WORK}/${stem}.o" -o "${WORK}/${image}"
                        RESULT_VARIABLE linked ERROR_VARIABLE link_error)
    endif()
    if(NOT linked EQUAL 0)
        message(FATAL_ERROR "linking ${image} failed:\n${link_error}")
    endif()

    file(SHA256 "${FIXTURE_DIR}/${image}" checked_in)
    file(SHA256 "${WORK}/${image}" rebuilt)
    if(NOT checked_in STREQUAL rebuilt)
        string(APPEND failures
            "  ${image} does not match ${source}: rebuild it with\n"
            "    krc -c --target kr232 ${source} -o ${stem}.o\n"
            "    krld ${link_args} --entry=_start ${stem}.o -o ${image}\n")
    endif()
endforeach()

if(NOT failures STREQUAL "")
    message(FATAL_ERROR "fixtures do not match their sources:\n${failures}")
endif()
message(STATUS "all fixture images match their sources")
