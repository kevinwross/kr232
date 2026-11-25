cmake_minimum_required(VERSION 3.28)
project(kr232 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KR232_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(KR232_BUILD_TESTS "Build the unit tests" ON)

# The guest runtime, sysroot and package fixture live in the compiler checkout;
# the package row is only registered when the compiler is configured.
set(KR232_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../krcompiler/targets/kr232/runtime"
    CACHE PATH "Guest runtime sources")
set(KR232_SYSROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../krcompiler/sysroot/kr232/include"
    CACHE PATH "Guest sysroot headers")
set(KR232_PACKAGE_FIXTURE
    "${CMAKE_CURRENT_SOURCE_DIR}/../krcompiler/tests/fixtures/c/kr232_user_packages.c"
    CACHE FILEPATH "The package program the row builds")
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
        "/pathmap:${PROJECT_SOURCE_DIR}=/src")
else()
    target_compile_options(kr232_warnings INTERFACE -Wall -Wextra -Wpedantic -ffp-contract=off)
    if(KR232_WARNINGS_AS_ERRORS)
        target_compile_options(kr232_warnings INTERFACE -Werror)
    endif()
    target_compile_options(kr232_warnings INTERFACE
        "-ffile-prefix-map=${PROJECT_SOURCE_DIR}=/src"
        "-fdebug-prefix-map=${PROJECT_SOURCE_DIR}=/src")
endif()

# Host code only. Guest code lives under targets/kr232/ and is compiled by krc;
# kr232_guard_host_sources() fails the configure if a host target ever names it.
add_library(kr232_core STATIC
    src/decoder.cpp
    src/devices.cpp
    src/frame.cpp
    src/loader.cpp
    src/machine.cpp
    src/memory.cpp
    src/version.cpp
)
target_include_directories(kr232_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# The core is the machine, loader, decoder and devices without the emulator
# front end. Exporting it as `kr232::core` is what lets krdbg link one machine
# rather than compile these sources into its adapter, which is D4 of the build
# cleanup plan: the debugger and the emulator should run the same object code.
add_library(kr232::core ALIAS kr232_core)
# The exported package has to present `kr232::core`, so the target carries that
# name into the install tree; the alias above only serves this build.
set_target_properties(kr232_core PROPERTIES EXPORT_NAME core)
set_target_properties(kr232_warnings PROPERTIES EXPORT_NAME warnings)
# The core's headers use std::span, and a consumer that links kr232::core should
# not have to discover that by reading a compile error.
target_compile_features(kr232_core PUBLIC cxx_std_20)
option(KR232_INSTALL "Install kr232::core and its headers for other trees" OFF)
include(GNUInstallDirs)
if(KR232_INSTALL)
    install(TARGETS kr232_core kr232_warnings EXPORT kr232Targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(EXPORT kr232Targets
        FILE kr232Targets.cmake
        NAMESPACE kr232::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/kr232-core")
    include(CMakePackageConfigHelpers)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/kr232-core-config.cmake.in"
        "@PACKAGE_INIT@\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/kr232Targets.cmake\")\n")
    configure_package_config_file(
        "${CMAKE_CURRENT_BINARY_DIR}/kr232-core-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/kr232-core-config.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/kr232-core")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/kr232-core-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/kr232-core")
endif()
target_link_libraries(kr232_core PRIVATE kr232_warnings)

add_executable(kr232-dis src/dis.cpp)
target_link_libraries(kr232-dis PRIVATE kr232_core kr232_warnings)
target_include_directories(kr232-dis PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(kr232 src/main.cpp)
# kr232_warnings carries the prefix-map flags, so every target with sources of
# its own links it. Linking it into kr232_core alone left main.cpp unmapped: the
# Linux binary then carried its build-host path and the hygiene check failed.
target_link_libraries(kr232 PRIVATE kr232_core kr232_warnings)

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
        file(RELATIVE_PATH _relative "${PROJECT_SOURCE_DIR}" "${_absolute}")
        if(_relative MATCHES "(^|/)targets/")
            message(FATAL_ERROR
                "${target} lists '${_source}', which is guest code under targets/. "
                "Host targets never compile guest code; krc does. See kr232-impl.md §1.")
        endif()
    endforeach()
endfunction()

kr232_guard_host_sources(kr232_core)
kr232_guard_host_sources(kr232)

if(KR232_BUILD_TESTS)
    enable_testing()

# Resolve the guest toolchain before any test row is registered, so the rows that
# rebuild the kernel see the same paths the build does. See cmake/KrGuest.cmake.
# The family's toolchain option, spelled the same in every tree. This repository
# has one path -- the compiler -- so the value set is one long; `builtin` exists
# only where a tree carries its own compiler, and saying so is better than
# pretending the value is unknown.
set(KRG_TOOLCHAIN "krc" CACHE STRING "Program build path: krc (this tree has one path)")
set_property(CACHE KRG_TOOLCHAIN PROPERTY STRINGS krc)
if(NOT KRG_TOOLCHAIN STREQUAL "krc")
  message(FATAL_ERROR
    "KRG_TOOLCHAIN must be 'krc' in this tree: guest code here is built by krc, "
    "and the only other family value, 'builtin', exists in kr16, which carries "
    "its own compiler")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/KrGuest.cmake")
set(KRG_KRC "" CACHE FILEPATH "krc executable that builds the kernel")
set(KRG_KRLD "" CACHE FILEPATH "krld executable that links the kernel")
set(KRG_LEGACY_KRC "${KR232_KRC}")
set(KRG_LEGACY_KRLD "${KR232_KRLD}")
if(KR232_BUILD_KERNEL)
    krguest_find_toolchain()
endif()


    add_executable(kr232_tests
        tests/main.cpp
        tests/test_decode.cpp
        tests/test_devices.cpp
        tests/test_frame.cpp
        tests/test_loader.cpp
        tests/test_machine.cpp
        tests/test_memory.cpp
        tests/test_version.cpp
    )
    # The decoding test reads Appendix A from the document rather than a copy of
    # it, so a vector cannot drift from the test that checks it.
    target_compile_definitions(kr232_tests PRIVATE
        "KR232_ISA_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/kr232-isa.md\"")
    target_include_directories(kr232_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(kr232_tests PRIVATE kr232_core kr232_warnings)

    # The kernel is guest code the host compiler never sees, so it is checked in
    # and run here: a program's syscall exit must come back as the host status.
    add_test(NAME kr232_kernel_user
        COMMAND ${CMAKE_COMMAND}
                -DVM=$<TARGET_FILE:kr232>
                -DKERNEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kernel.kr232
                -DPROGRAM=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/user-exit42.kr232
                -DEXPECT_EXIT=42
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckKr232Kernel.cmake)
    if(KR232_BUILD_KERNEL)
        add_test(NAME kr232_kernel_matches_source
            COMMAND ${CMAKE_COMMAND}
                    -DVM=$<TARGET_FILE:kr232>
                    -DKERNEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kernel.kr232
                    -DPROGRAM=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/user-exit42.kr232
                    -DEXPECT_EXIT=42
                    -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
                    -DKRC=${KRG_KRC}
                    -DKRLD=${KRG_KRLD}
                    -DWORK=${CMAKE_BINARY_DIR}/kernel-rebuild
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckKr232Kernel.cmake)
    endif()
    # One program that calls every entry in the kernel's table, including the
    # block-pointer drawing calls; the frame it commits is the golden.
    # The kernel's read call: the program echoes whatever the console holds, so
    # the text coming back out proves the bytes reached user memory.
    # DRAW_TEXT must say it is not supported rather than answer success: the
    # program exits 0 only when STATUS bit 0 ERROR came back through the kernel.
    # Every checked-in image must equal a fresh build of its source: a stale
    # fixture keeps passing tests against a program nobody has.
    if(KR232_BUILD_KERNEL)
        add_test(NAME kr232_fixtures_match_source
            COMMAND ${CMAKE_COMMAND}
                    -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
                    -DKRC=${KRG_KRC}
                    -DKRLD=${KRG_KRLD}
                    -DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
                    -DRUNTIME=${KR232_RUNTIME_DIR}
                    -DWORK=${CMAKE_BINARY_DIR}/fixtures
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckKr232FixtureDrift.cmake)
    endif()
    add_test(NAME kr232_kernel_text_reports_error
        COMMAND ${CMAKE_COMMAND}
                -DVM=$<TARGET_FILE:kr232>
                -DKERNEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kernel.kr232
                -DPROGRAM=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/user-text.kr232
                -DEXPECT_EXIT=0
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckKr232Kernel.cmake)
    add_test(NAME kr232_kernel_read
        COMMAND ${CMAKE_COMMAND}
                -DVM=$<TARGET_FILE:kr232>
                -DKERNEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kernel.kr232
                -DPROGRAM=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/user-echo.kr232
                -DEXPECT_EXIT=0
                -DINPUT=hey!
                -DEXPECT_CONSOLE=hey!
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckKr232Kernel.cmake)
    add_test(NAME kr232_kernel_calls
        COMMAND ${CMAKE_COMMAND}
                -DVM=$<TARGET_FILE:kr232>
                -DKERNEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kernel.kr232
                -DPROGRAM=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/user-calls.kr232
                -DEXPECT_EXIT=0
                -DFRAME_HASH=f6b786175f12b425
                -DBLANK_IMAGE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/blank-flip.kr232
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckKr232Kernel.cmake)

    # The package program names no call numbers: it prints, draws, flips and
    # asks for the mode through <kr232/*.h>, so this row fails when a package
    # does rather than when a test does. It needs the compiler to build.
    if(KR232_BUILD_KERNEL)
        add_test(NAME kr232_kernel_packages
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=${KRG_KRC}
                    -DKRLD=${KRG_KRLD}
                    -DVM=$<TARGET_FILE:kr232>
                    -DKERNEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kernel.kr232
                    -DRUNTIME=${KR232_RUNTIME_DIR}
                    -DINCLUDE=${KR232_SYSROOT_DIR}
                    -DFIXTURE=${KR232_PACKAGE_FIXTURE}
                    -DWORK=${CMAKE_BINARY_DIR}/packages
                    -DFRAME_HASH=659d051c4dc232e5
                    -DINPUT=z
                    -DBLANK_IMAGE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/blank-flip.kr232
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckKr232UserPackages.cmake)
    endif()

    add_test(NAME kr232_dis_flat
        COMMAND kr232-dis "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kernel-gate.bin")
    set_tests_properties(kr232_dis_flat PROPERTIES
        LABELS "unit"
        PASS_REGULAR_EXPRESSION "ld r2, #0xfff08000")



    add_test(NAME kr232_version COMMAND kr232_tests)
    set_tests_properties(kr232_version PROPERTIES LABELS "unit")

    # The CLI path: a flat image that exits through SYS 0, and the frame hash of
    # the machine it leaves behind. The hash of an uncommitted frame is the FNV
    # offset basis, so this also pins that the flag does not invent a frame.
    add_test(NAME kr232_cli_frame_hash
        COMMAND kr232 vm "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/halt.bin"
                --base 0x08000000 --max-steps 10 --frame-hash)
    set_tests_properties(kr232_cli_frame_hash PROPERTIES
        LABELS "unit"
        PASS_REGULAR_EXPRESSION "frame cbf29ce484222325")

    # A zero budget is unlimited, so the image reaches its SYS exit instead of
    # stopping before the first instruction.
    add_test(NAME kr232_cli_zero_max_steps
        COMMAND kr232 vm "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/halt.bin"
                --base 0x08000000 --max-steps 0)
    set_tests_properties(kr232_cli_zero_max_steps PROPERTIES
        LABELS "unit"
        PASS_REGULAR_EXPRESSION "halted status")

    # The hand-encoded kernel image: two pixels into the aperture, a FLIP through
    # the framebuffer register, and a clean exit. The hash is what makes the run
    # reproducible across hosts and runs.
    add_test(NAME kr232_cli_kernel
        COMMAND kr232 vm "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kernel-draw.bin"
                --max-steps 100 --frame-hash)
    set_tests_properties(kr232_cli_kernel PROPERTIES
        LABELS "unit"
        PASS_REGULAR_EXPRESSION "frame 1628b6c37bf7a2ad")

    # Plan §6 item 4: the hash of the gate image must match the checked-in golden.
    add_test(NAME kr232_cli_kernel_golden
        COMMAND ${CMAKE_COMMAND}
            "-DEXE=$<TARGET_FILE:kr232>"
            "-DIMAGE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kernel-gate.bin"
            "-DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kernel-gate.hash"
            "-DBLANK_IMAGE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/blank-flip.kr232"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckFrameHash.cmake")
    set_tests_properties(kr232_cli_kernel_golden PROPERTIES LABELS "unit")

    add_test(NAME kr232_path_hygiene
        COMMAND ${CMAKE_COMMAND}
            "-DBINARY=$<TARGET_FILE:kr232>"
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

# The kernel is guest code: it is built by krc from the prefix, never by the host
# compiler, and the option is OFF so M1-M3 need no compiler at all.
# The kernel target uses the toolchain resolved above by cmake/KrGuest.cmake;
# this block only registers it, so there is one place that decides where krc is.
if(KR232_BUILD_KERNEL)
    set(KR232_KERNEL_IMAGE "${CMAKE_BINARY_DIR}/kernel.kr232")
    add_custom_target(kr232_kernel ALL
        # Compiled from the source root with a relative path: the assembler
        # records the path it is given in the debug section, so an absolute one
        # makes the image depend on the host that built it (E3).
        COMMAND "${KRG_KRC}" -c --target kr232
                "targets/kr232/kernel/kernel.s"
                -o "${CMAKE_BINARY_DIR}/kernel.o"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        # One tool: `krc image` links the object into the target's container, so
        # the kernel no longer needs krld as a second step (B5).
        COMMAND "${KRG_KRC}" image --target=kr232 --entry=_start
                --source-root=${CMAKE_CURRENT_SOURCE_DIR}
                "${CMAKE_BINARY_DIR}/kernel.o"
                -o "${KR232_KERNEL_IMAGE}"
        COMMENT "Building the kr232 kernel with krc from the prefix"
        VERBATIM)
endif()
