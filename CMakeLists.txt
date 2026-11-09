cmake_minimum_required(VERSION 3.24)
project(CataGui VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CATAGUI_SANITIZE "Build with ASan/UBSan (gcc/clang only)" OFF)
option(CATAGUI_BUILD_TESTS "Build unit tests" ON)

# Toolchains: GCC/Clang only (Linux gcc, MSYS2 UCRT64 gcc/clang, llvm-mingw cross).
if(MSVC)
    message(FATAL_ERROR "CataGui builds with GCC or Clang (Linux, MSYS2, llvm-mingw); MSVC is not supported.")
endif()

# ---------------------------------------------------------------------------
# Third party: raylib (built directly from vendored sources, no system GL dev
# packages needed: GLFW dlopens libGL / opengl32 at runtime).
# ---------------------------------------------------------------------------
set(RAYLIB_SRC ${CMAKE_SOURCE_DIR}/third_party/raylib/src)
add_library(raylib STATIC
    ${RAYLIB_SRC}/rcore.c
    ${RAYLIB_SRC}/rshapes.c
    ${RAYLIB_SRC}/rtextures.c
    ${RAYLIB_SRC}/rtext.c
    ${RAYLIB_SRC}/rmodels.c
    ${RAYLIB_SRC}/raudio.c
    ${RAYLIB_SRC}/rglfw.c)
target_include_directories(raylib SYSTEM PUBLIC ${RAYLIB_SRC})
target_include_directories(raylib PRIVATE ${RAYLIB_SRC}/external/glfw/include)
target_compile_definitions(raylib PUBLIC PLATFORM_DESKTOP GRAPHICS_API_OPENGL_33)
target_compile_options(raylib PRIVATE -w)
set_target_properties(raylib PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)
if(WIN32)
    target_link_libraries(raylib PUBLIC winmm gdi32 user32 shell32 opengl32)
else()
    target_compile_definitions(raylib PRIVATE _GLFW_X11 _GNU_SOURCE)
    target_link_libraries(raylib PUBLIC X11 m dl pthread)
endif()

# Clay + yyjson + stb_truetype
add_library(third_party_misc STATIC
    third_party/yyjson/yyjson.c
    src/third_party_impl.c)
target_include_directories(third_party_misc SYSTEM PUBLIC
    third_party/clay
    third_party/yyjson
    third_party/stb)
target_compile_options(third_party_misc PRIVATE -w)

# ---------------------------------------------------------------------------
# Warnings for our own code
# ---------------------------------------------------------------------------
function(catagui_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow -Wno-missing-field-initializers
        -Wno-unused-parameter -Wstrict-prototypes -Wvla -Wimplicit-fallthrough)
    if(WIN32)
        target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    else()
        target_compile_definitions(${target} PRIVATE _GNU_SOURCE)
        if(CATAGUI_SANITIZE)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

# Windows: fully static binaries (no libgcc / libwinpthread / libc++ DLLs to ship).
if(WIN32)
    add_link_options(-static)
endif()

# ---------------------------------------------------------------------------
# catagui_core: everything that does not touch the window (unit-testable)
# ---------------------------------------------------------------------------
add_library(catagui_core STATIC
    src/base/arena.c
    src/base/str.c
    src/base/vec.c
    src/base/os.c
    src/base/log.c
    src/base/json.c
    src/base/thread.c
    src/core/coord.c
    src/core/board.c
    src/core/game_tree.c
    src/core/sgf.c
    src/engine/process.c
    src/engine/gtp_client.c
    src/engine/kata_analysis.c
    src/engine/position.c
    src/engine/katago_engine.c
    src/engine/engine_profile.c
    src/app/commands.c
    src/app/keymap.c
    src/app/config.c
    src/app/i18n.c)
target_include_directories(catagui_core PUBLIC src)
target_link_libraries(catagui_core PUBLIC third_party_misc)
if(WIN32)
    target_link_libraries(catagui_core PUBLIC shell32 ole32 uuid)
else()
    target_link_libraries(catagui_core PUBLIC m pthread)
endif()
catagui_warnings(catagui_core)

# ---------------------------------------------------------------------------
# catagui: the desktop app
# ---------------------------------------------------------------------------
add_executable(catagui
    src/main.c
    src/app/app.c
    src/app/file_dialog.c
    src/ui/font_cache.c
    src/ui/theme.c
    src/ui/background.c
    src/ui/clay_render.c
    src/ui/ui.c
    src/ui/widgets.c
    src/ui/text_input.c
    src/ui/form.c
    src/ui/variation_tree.c
    src/ui/board_view.c
    src/ui/winrate_graph.c
    src/ui/panels.c
    src/ui/layout.c)
target_link_libraries(catagui PRIVATE catagui_core raylib)
catagui_warnings(catagui)
if(WIN32)
    target_link_libraries(catagui PRIVATE comdlg32)
    # GUI subsystem in Release (no console window), console in Debug for logs.
    target_link_options(catagui PRIVATE $<$<CONFIG:Release>:-mwindows>)
endif()

# Assets are loaded from <exe_dir>/assets, so mirror them next to the binary.
add_custom_command(TARGET catagui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:catagui>/assets)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(CATAGUI_BUILD_TESTS)
    enable_testing()
    # Tests read assets from the source tree. When cross-compiling from WSL the
    # Windows binaries need the drive path (/mnt/c/x -> C:/x).
    set(CATAGUI_TEST_SOURCE_DIR ${CMAKE_SOURCE_DIR})
    if(CMAKE_CROSSCOMPILING AND WIN32 AND CMAKE_SOURCE_DIR MATCHES "^/mnt/([a-z])/(.*)$")
        string(TOUPPER ${CMAKE_MATCH_1} drive)
        set(CATAGUI_TEST_SOURCE_DIR "${drive}:/${CMAKE_MATCH_2}")
    endif()
    add_executable(catagui_tests
        tests/test_main.c
        tests/test_str.c
        tests/test_board.c
        tests/test_sgf.c
        tests/test_analysis.c
        tests/test_data.c)
    target_link_libraries(catagui_tests PRIVATE catagui_core)
    target_compile_definitions(catagui_tests PRIVATE CATAGUI_SOURCE_DIR="${CATAGUI_TEST_SOURCE_DIR}")
    catagui_warnings(catagui_tests)
    add_test(NAME unit COMMAND catagui_tests)

    add_executable(catagui_engine_test tests/engine_smoke.c)
    target_link_libraries(catagui_engine_test PRIVATE catagui_core)
    target_compile_definitions(catagui_engine_test PRIVATE CATAGUI_SOURCE_DIR="${CATAGUI_TEST_SOURCE_DIR}")
    catagui_warnings(catagui_engine_test)
    add_test(NAME engine_smoke COMMAND catagui_engine_test)
    set_tests_properties(engine_smoke PROPERTIES LABELS engine TIMEOUT 1000)
endif()
