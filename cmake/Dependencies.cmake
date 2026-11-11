# Third-party libraries, downloaded on the first configure into third_party/
# (git-ignored) at pinned versions, checked against SHA-256 hashes. Only the
# files the build needs are extracted. A library already present in
# third_party/<name> (placed by hand, e.g. for offline builds) is used as is.
#
#   raylib 6.2   zlib/libpng   https://github.com/raysan5/raylib  (also provides
#                              stb_truetype and sinfl in src/external)
#   Clay 0.14    zlib          https://github.com/nicbarker/clay
#   yyjson 0.10.0 MIT          https://github.com/ibireme/yyjson
include_guard(GLOBAL)

set(GIFU_THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)

# gifu_dependency(NAME URL SHA256 TOP_FOLDER PATTERN...)
function(gifu_dependency name version url sha256 top)
    set(dir ${GIFU_THIRD_PARTY_DIR}/${name})
    set(stamp ${dir}/.version)
    if(EXISTS ${dir})
        if(EXISTS ${stamp})
            file(READ ${stamp} have)
            string(STRIP "${have}" have)
            if(have STREQUAL version)
                return()
            endif()
            message(STATUS "${name}: ${have} -> ${version}")
            file(REMOVE_RECURSE ${dir})
        else()
            message(STATUS "${name}: using third_party/${name} as found (not downloaded by the build)")
            return()
        endif()
    endif()
    set(archive ${CMAKE_BINARY_DIR}/_downloads/${name}-${version}.tar.gz)
    message(STATUS "${name}: downloading ${version} from ${url}")
    file(DOWNLOAD ${url} ${archive} EXPECTED_HASH SHA256=${sha256} STATUS result TLS_VERIFY ON)
    list(GET result 0 code)
    if(NOT code EQUAL 0)
        list(GET result 1 why)
        file(REMOVE ${archive})
        message(FATAL_ERROR "${name}: download failed (${why}). Download ${url} yourself and extract "
                            "it into third_party/${name} (see README.md, \"Building\").")
    endif()
    set(tmp ${CMAKE_BINARY_DIR}/_downloads/${name}-extract)
    file(REMOVE_RECURSE ${tmp})
    list(TRANSFORM ARGN PREPEND "${top}/" OUTPUT_VARIABLE patterns)
    file(ARCHIVE_EXTRACT INPUT ${archive} DESTINATION ${tmp} PATTERNS ${patterns})
    file(RENAME ${tmp}/${top} ${dir})
    file(WRITE ${stamp} "${version}\n")
    file(REMOVE_RECURSE ${tmp})
    file(REMOVE ${archive})
endfunction()

gifu_dependency(raylib 6.2
    https://github.com/raysan5/raylib/archive/refs/tags/6.2.tar.gz
    df7a13129b1bec9e73a6962efff170beba310ee6eab8f94cd000b7316b4db5ee
    raylib-6.2 src LICENSE)
gifu_dependency(clay 0.14
    https://github.com/nicbarker/clay/archive/refs/tags/v0.14.tar.gz
    ee8f6477020dd72afe8cf6f8d3ab6855980028289a2b677a1423d17aab983585
    clay-0.14 clay.h LICENSE.md)
gifu_dependency(yyjson 0.10.0
    https://github.com/ibireme/yyjson/archive/refs/tags/0.10.0.tar.gz
    0d901cb2c45c5586e3f3a4245e58c2252d6b24bf4b402723f6179523d389b165
    yyjson-0.10.0 src LICENSE)

# Clay tracks only 10 scroll containers; past that, clip containers silently stop
# scrolling (no offset, no scrollbar). Gifu's panels, tabs and dialogs need more.
set(_clay_h ${GIFU_THIRD_PARTY_DIR}/clay/clay.h)
if(EXISTS ${_clay_h})
    file(READ ${_clay_h} _clay_text)
    set(_clay_from "scrollContainerDatas = Clay__ScrollContainerDataInternalArray_Allocate_Arena(10, arena);")
    set(_clay_to "scrollContainerDatas = Clay__ScrollContainerDataInternalArray_Allocate_Arena(256, arena); /* Gifu: was 10 */")
    string(FIND "${_clay_text}" "${_clay_from}" _clay_at)
    if(NOT _clay_at EQUAL -1)
        string(REPLACE "${_clay_from}" "${_clay_to}" _clay_text "${_clay_text}")
        file(WRITE ${_clay_h} "${_clay_text}")
        message(STATUS "clay: scroll container limit raised to 256")
    endif()
endif()
