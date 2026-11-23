cmake_minimum_required(VERSION 3.28)
project(kr232 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KR232_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(KR232_BUILD_TESTS "Build the unit tests" ON)
option(KR232_LAYOUT_SELFTEST
    "Add a targets/ file to a host target, so the layout guard must fail the configure" OFF)

add_library(kr232_warnings INTERFACE)
if(MSVC)
    target_compile_options(kr232_warnings INTERFACE /W4 /permissive- /Zc:__cplusplus)
    if(KR232_WARNINGS_AS_ERRORS)
        target_compile_options(kr232_warnings INTERFACE /WX)
    endif()
    # kr232-impl.md §1: no build-host path in any artifact, so a debug record is
    # the same whichever host built it. krdbg resolves /src through its source root.
    # /pathmap is accepted only together with /experimental:deterministic;
    # without it cl warns D9007 and silently ignores the mapping.
    target_compile_options(kr232_warnings INTERFACE
        /experimental:deterministic
        "/pathmap:${CMAKE_SOURCE_DIR}=/src")
else()
    target_compile_options(kr232_warnings INTERFACE -Wall -Wextra -Wpedantic -ffp-contract=off)
    if(KR232_WARNINGS_AS_ERRORS)
        target_compile_options(kr232_warnings INTERFACE -Werror)
    endif()
    target_compile_options(kr232_warnings INTERFACE
        "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=/src"
        "-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=/src")
endif()

# Host code only. Guest code lives under targets/kr232/ and is compiled by krc;
# kr232_guard_host_sources() fails the configure if a host target ever names it.
add_library(kr232_core STATIC
    src/version.cpp
)
target_include_directories(kr232_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(kr232_core PRIVATE kr232_warnings)

add_executable(kr232-emu src/main.cpp)
# kr232_warnings carries the prefix-map flags, so every target with sources of
# its own links it. Linking it into kr232_core alone left main.cpp unmapped: the
# Linux binary then carried its build-host path and the hygiene check failed.
target_link_libraries(kr232-emu PRIVATE kr232_core kr232_warnings)

if(KR232_LAYOUT_SELFTEST)
    message(STATUS "kr232: layout selftest is on; adding targets/kr232/probe.cpp to kr232_core on purpose")
    target_sources(kr232_core PRIVATE targets/kr232/probe.cpp)
endif()

# A host target's source list must never name a file under targets/. The
# symptom of a miss is a confusing link error, or worse, guest code compiled by
# the host compiler that only misbehaves under the VM (kr232-impl.md §1).
function(kr232_guard_host_sources target)
    get_target_property(_sources ${target} SOURCES)
    if(NOT _sources)
        return()
    endif()
    foreach(_source IN LISTS _sources)
        get_filename_component(_absolute "${_source}" ABSOLUTE)
        file(RELATIVE_PATH _relative "${CMAKE_SOURCE_DIR}" "${_absolute}")
        if(_relative MATCHES "(^|/)targets/")
            message(FATAL_ERROR
                "${target} lists '${_source}', which is guest code under targets/. "
                "Host targets never compile guest code; krc does. See kr232-impl.md §1.")
        endif()
    endforeach()
endfunction()

kr232_guard_host_sources(kr232_core)
kr232_guard_host_sources(kr232-emu)

if(KR232_BUILD_TESTS)
    enable_testing()

    add_executable(kr232_tests
        tests/main.cpp
        tests/test_version.cpp
    )
    target_include_directories(kr232_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(kr232_tests PRIVATE kr232_core kr232_warnings)

    add_test(NAME kr232_version COMMAND kr232_tests)
    set_tests_properties(kr232_version PROPERTIES LABELS "unit")

    add_test(NAME kr232_path_hygiene
        COMMAND ${CMAKE_COMMAND}
            "-DBINARY=$<TARGET_FILE:kr232-emu>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckPathHygiene.cmake")
    set_tests_properties(kr232_path_hygiene PROPERTIES LABELS "hygiene")

    # The check is only worth having if it can fail: this one is fed a path on
    # purpose and passes only by failing.
    add_test(NAME kr232_path_hygiene_selftest
        COMMAND ${CMAKE_COMMAND} -DSELFTEST=ON
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckPathHygiene.cmake")
    set_tests_properties(kr232_path_hygiene_selftest PROPERTIES
        WILL_FAIL TRUE LABELS "hygiene")
endif()
