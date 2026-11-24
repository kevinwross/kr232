# Resolves the guest toolchain — `krc` and `krld` — for a VM tree.
#
# Every VM needs the same answer to "where is the compiler", and before this file
# each one answered differently: kr32 and kr16 through the installed package,
# kr232 and kr216 through per-repository cache variables with their own error
# messages. The order below is the same everywhere, and the failure names every
# step so a missing compiler is not mistaken for a misconfiguration.
#
#   1. KRG_KRC / KRG_KRLD            explicit, wins over everything
#   2. KRG_LEGACY_KRC / _KRLD        this repository's historical variable names
#   3. <sibling krcompiler>/build/*/host/bin/<Config>/krc, newest build tree first
#   4. the same two names on PATH
#
# Call krguest_find_toolchain() after setting KRG_LEGACY_KRC and KRG_LEGACY_KRLD
# if the repository has its own spelling. On success KRG_KRC and KRG_KRLD name
# existing files; otherwise the configure stops with one sentence.

function(krguest_find_toolchain)
    set(kinds krc krld)
    set(candidates_krc "")
    set(candidates_krld "")

    # An explicit path that does not exist is a mistake, not a hint: falling
    # through to the sibling build here would run a compiler the caller did not
    # name, which is the failure mode this module exists to prevent.
    if(KRG_KRC AND NOT EXISTS "${KRG_KRC}")
        message(FATAL_ERROR "KRG_KRC points at ${KRG_KRC}, which does not exist")
    endif()
    if(KRG_KRLD AND NOT EXISTS "${KRG_KRLD}")
        message(FATAL_ERROR "KRG_KRLD points at ${KRG_KRLD}, which does not exist")
    endif()
    if(KRG_LEGACY_KRC AND NOT EXISTS "${KRG_LEGACY_KRC}")
        message(FATAL_ERROR
            "the legacy krc variable points at ${KRG_LEGACY_KRC}, which does not exist")
    endif()
    if(KRG_LEGACY_KRLD AND NOT EXISTS "${KRG_LEGACY_KRLD}")
        message(FATAL_ERROR
            "the legacy krld variable points at ${KRG_LEGACY_KRLD}, which does not exist")
    endif()
    if(KRG_KRC)
        list(APPEND candidates_krc "${KRG_KRC}")
    endif()
    if(KRG_KRLD)
        list(APPEND candidates_krld "${KRG_KRLD}")
    endif()
    if(KRG_LEGACY_KRC)
        list(APPEND candidates_krc "${KRG_LEGACY_KRC}")
    endif()
    if(KRG_LEGACY_KRLD)
        list(APPEND candidates_krld "${KRG_LEGACY_KRLD}")
    endif()

    # The sibling compiler's build trees, so a developer who has just built the
    # compiler does not have to pass both paths to every VM configure.
    # Sibling build trees, most recently built first. Sorting the directory names
    # would put a stale Release tree ahead of a Debug tree that was just rebuilt,
    # which is how one row ended up running a compiler too old to know `krc image`.
    set(compiler_root "${CMAKE_CURRENT_SOURCE_DIR}/../krcompiler")
    if(IS_DIRECTORY "${compiler_root}/build")
        set(stamped "")
        file(GLOB krc_builds LIST_DIRECTORIES true "${compiler_root}/build/*")
        foreach(tree IN LISTS krc_builds)
            foreach(config Debug Release)
                foreach(tool krc krld)
                    # Only a binary this host can execute: a sibling tree built in
                    # WSL leaves a Linux krc whose mtime can be newer than the
                    # Windows one, and feeding it to CreateProcess fails with a
                    # bare exit code.
                    if(WIN32)
                        set(suffixes ".exe")
                    else()
                        set(suffixes "")
                    endif()
                    foreach(suffix IN LISTS suffixes)
                        set(path "${tree}/host/bin/${config}/${tool}${suffix}")
                        if(EXISTS "${path}")
                            file(TIMESTAMP "${path}" stamp)
                            list(APPEND stamped "${stamp}|${path}")
                        endif()
                    endforeach()
                endforeach()
            endforeach()
        endforeach()
        if(stamped)
            list(SORT stamped ORDER DESCENDING)
            foreach(entry IN LISTS stamped)
                string(FIND "${entry}" "|" bar)
                math(EXPR start "${bar} + 1")
                string(SUBSTRING "${entry}" ${start} -1 path)
                if(path MATCHES "/krld")
                    list(APPEND candidates_krld "${path}")
                else()
                    list(APPEND candidates_krc "${path}")
                endif()
            endforeach()
        endif()
    endif()

    find_program(KRG_KRC_ON_PATH NAMES krc PATHS ENV PATH NO_DEFAULT_PATH)
    find_program(KRG_KRLD_ON_PATH NAMES krld PATHS ENV PATH NO_DEFAULT_PATH)
    if(KRG_KRC_ON_PATH)
        list(APPEND candidates_krc "${KRG_KRC_ON_PATH}")
    endif()
    if(KRG_KRLD_ON_PATH)
        list(APPEND candidates_krld "${KRG_KRLD_ON_PATH}")
    endif()

    foreach(kind IN LISTS kinds)
        string(TOUPPER "${kind}" kind_upper)
        set(found "")
        foreach(candidate IN LISTS candidates_${kind})
            if(candidate AND EXISTS "${candidate}")
                set(found "${candidate}")
                break()
            endif()
        endforeach()
        if(NOT found)
            message(FATAL_ERROR
                "no ${kind} was found. Set KRG_${kind_upper} (or this repository's own "
                "variable) to an existing ${kind}, build the sibling krcompiler so "
                "its build tree can be used, or put ${kind} on PATH. Tried: "
                "${candidates_${kind}}")
        endif()
        set(KRG_${kind_upper} "${found}" PARENT_SCOPE)
    endforeach()
endfunction()
