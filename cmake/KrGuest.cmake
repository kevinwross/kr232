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
    set(candidates_krc "")
    set(candidates_krld "")

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
    set(compiler_root "${CMAKE_CURRENT_SOURCE_DIR}/../krcompiler")
    if(IS_DIRECTORY "${compiler_root}/build")
        file(GLOB krc_builds LIST_DIRECTORIES true "${compiler_root}/build/*")
        list(SORT krc_builds ORDER DESCENDING)
        foreach(tree IN LISTS krc_builds)
            foreach(config Debug Release)
                if(EXISTS "${tree}/host/bin/${config}/krc")
                    list(APPEND candidates_krc "${tree}/host/bin/${config}/krc")
                endif()
                if(EXISTS "${tree}/host/bin/${config}/krc.exe")
                    list(APPEND candidates_krc "${tree}/host/bin/${config}/krc.exe")
                endif()
                if(EXISTS "${tree}/host/bin/${config}/krld")
                    list(APPEND candidates_krld "${tree}/host/bin/${config}/krld")
                endif()
                if(EXISTS "${tree}/host/bin/${config}/krld.exe")
                    list(APPEND candidates_krld "${tree}/host/bin/${config}/krld.exe")
                endif()
            endforeach()
        endforeach()
    endif()

    find_program(KRG_KRC_ON_PATH NAMES krc PATHS ENV PATH NO_DEFAULT_PATH)
    find_program(KRG_KRLD_ON_PATH NAMES krld PATHS ENV PATH NO_DEFAULT_PATH)
    if(KRG_KRC_ON_PATH)
        list(APPEND candidates_krc "${KRG_KRC_ON_PATH}")
    endif()
    if(KRG_KRLD_ON_PATH)
        list(APPEND candidates_krld "${KRG_KRLD_ON_PATH}")
    endif()

    foreach(kind krc krld)
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
                "no ${kind} was found. Set KRG_${kind} (or this repository's own "
                "variable) to an existing ${kind}, build the sibling krcompiler so "
                "its build tree can be used, or put ${kind} on PATH. Tried: "
                "${candidates_${kind}}")
        endif()
        set(KRG_${kind_upper} "${found}" PARENT_SCOPE)
    endforeach()
endfunction()
