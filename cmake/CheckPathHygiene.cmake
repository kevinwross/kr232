# Rejects a build-host source path in a built artifact (kr232-impl.md §1).
#
# A path baked into debug data makes the last builder decide what the record
# means, so every host artifact is prefix-mapped to /src and this check proves
# it. It looks for the project-relative source directories, not for every
# "C:\": a Debug MSVC binary also names its own PDB under build/, which is
# build-local and not what this guards against.
#
#   cmake -DBINARY=<file> -P cmake/CheckPathHygiene.cmake
#   cmake -DSELFTEST=ON   -P cmake/CheckPathHygiene.cmake   # must fail

if(SELFTEST)
    set(_probe "${CMAKE_CURRENT_BINARY_DIR}/hygiene-selftest.bin")
    file(WRITE "${_probe}" "C:\\Users\\kevin\\repos\\kr232\\src\\version.cpp\n")
    set(BINARY "${_probe}")
endif()

if(NOT DEFINED BINARY OR NOT EXISTS "${BINARY}")
    message(FATAL_ERROR "CheckPathHygiene: BINARY is not set or does not exist")
endif()

file(READ "${BINARY}" _hex HEX)
string(TOLOWER "${_hex}" _hex)

# Hex of the fragments that must never appear: the project's source
# directories under a host-specific prefix, and the WSL mount root.
set(_patterns
    "6b723233325c737263"              # kr232\src
    "6b723233322f737263"              # kr232/src
    "6b723233325c696e636c756465"      # kr232\include
    "6b723233322f696e636c756465"      # kr232/include
    "6b723233325c7465737473"          # kr232\tests
    "6b723233322f7465737473"          # kr232/tests
    "2f6d6e742f632f"                  # /mnt/c/
)

foreach(_pattern IN LISTS _patterns)
    string(FIND "${_hex}" "${_pattern}" _position)
    if(_position GREATER -1)
        math(EXPR _byte "${_position} / 2")
        message(FATAL_ERROR
            "CheckPathHygiene: ${BINARY} carries the build host's path at byte ${_byte} "
            "(pattern ${_pattern}). Rebuild with the prefix-map flags of kr232-impl.md §1.")
    endif()
endforeach()

message(STATUS "CheckPathHygiene: ${BINARY} carries no build-host path")
