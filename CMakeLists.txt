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
    third_party/stb       # stb_truetype.h (our own copy; raylib compiles its copy static)
    third_party/sinfl)    # raw deflate for zip extraction (symbols renamed)
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
    src/base/text_encoding.c
    src/base/sexpr.c
    src/base/kv.c
    src/base/zip.c
    src/base/codec.c
    src/base/image.c
    src/core/coord.c
    src/core/board.c
    src/core/game_tree.c
    src/core/sgf.c
    src/core/game_report.c
    src/core/tsumego_frame.c
    src/core/board_reader.c
    src/core/sgf_analysis.c
    src/engine/process.c
    src/engine/line_channel.c
    src/engine/gtp_client.c
    src/engine/kata_analysis.c
    src/engine/position.c
    src/engine/katago_engine.c
    src/engine/katago_tuning.c
    src/engine/katago_analyzer.c
    src/engine/game_job.c
    src/engine/http.c
    src/engine/rank_estimator.c
    src/engine/engine_profile.c
    src/app/commands.c
    src/app/keymap.c
    src/app/config.c
    src/app/i18n.c
    src/app/installer.c
    src/app/screen_capture.c
    src/app/board_sync.c
    src/app/app_sync.c)
target_include_directories(catagui_core PUBLIC src)
target_link_libraries(catagui_core PUBLIC third_party_misc)
if(WIN32)
    target_link_libraries(catagui_core PUBLIC shell32 ole32 uuid gdi32 user32)   # gdi32: screen capture
else()
    target_link_libraries(catagui_core PUBLIC m pthread X11)   # X11: screen capture
endif()
catagui_warnings(catagui_core)

# ---------------------------------------------------------------------------
# catagui: the desktop app
# ---------------------------------------------------------------------------
add_executable(catagui
    src/main.c
    src/app/app.c
    src/app/file_dialog.c
    src/app/headless.c
    src/ui/font_cache.c
    src/ui/theme.c
    src/ui/background.c
    src/ui/clay_render.c
    src/ui/ui.c
    src/ui/widgets.c
    src/ui/anim.c
    src/ui/icons.c
    src/ui/text_input.c
    src/ui/form.c
    src/ui/setup.c
    src/ui/variation_tree.c
    src/ui/board_view.c
    src/ui/winrate_graph.c
    src/ui/panels.c
    src/ui/deep_report.c
    src/ui/theme_editor.c
    src/ui/text_edit.c
    src/ui/layout_model.c
    src/ui/layout_editor.c
    src/ui/layout_drag.c
    src/ui/area_select.c
    src/ui/layout.c)
target_link_libraries(catagui PRIVATE catagui_core raylib)
catagui_warnings(catagui)
if(WIN32)
    enable_language(RC)
    target_sources(catagui PRIVATE packaging/windows/catagui.rc)
    # windres does not report the icon as a dependency: rebuild when it changes.
    set_source_files_properties(packaging/windows/catagui.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/packaging/windows/catagui.ico)
    target_link_libraries(catagui PRIVATE comdlg32)
    # GUI subsystem in Release (no console window), console in Debug for logs.
    target_link_options(catagui PRIVATE $<$<CONFIG:Release>:-mwindows>)
endif()

# Assets are loaded from <exe_dir>/assets, so mirror them next to the binary
# on every build (not only when the executable relinks).
add_custom_target(catagui_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:catagui>/assets
    COMMENT "Syncing assets")
add_dependencies(catagui catagui_assets)

# ---------------------------------------------------------------------------
# Install / packaging (cmake --install <build> --prefix <dir>; see tools/package.py)
# ---------------------------------------------------------------------------
install(TARGETS catagui RUNTIME DESTINATION .)
install(DIRECTORY assets/ DESTINATION assets)
install(FILES LICENSE README.md THIRD_PARTY_NOTICES.md DESTINATION .)
if(NOT WIN32)
    # Desktop entry + icon: copy to ~/.local/share/applications and
    # ~/.local/share/icons/hicolor/256x256/apps (see README).
    install(FILES packaging/linux/catagui.desktop DESTINATION share/applications)
    install(FILES assets/icons/catagui.png DESTINATION share/icons/hicolor/256x256/apps)
endif()
install(FILES third_party/raylib/LICENSE DESTINATION licenses RENAME raylib-LICENSE.txt)
install(FILES third_party/clay/LICENSE.md DESTINATION licenses RENAME clay-LICENSE.md)
install(FILES third_party/yyjson/LICENSE DESTINATION licenses RENAME yyjson-LICENSE.txt)

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
        tests/test_data.c
        tests/test_installer.c
        tests/test_theme.c
        tests/test_layout.c
        tests/test_interop.c
        tests/test_text_edit.c
    tests/test_image.c
    tests/test_board_reader.c
        src/ui/theme.c
        src/ui/layout_model.c
        src/ui/text_edit.c)   # pure data code: needs raylib's Color type, not the library
    target_link_libraries(catagui_tests PRIVATE catagui_core)
    target_include_directories(catagui_tests PRIVATE third_party/raylib/src)
    target_compile_definitions(catagui_tests PRIVATE CATAGUI_SOURCE_DIR="${CATAGUI_TEST_SOURCE_DIR}")
    catagui_warnings(catagui_tests)
    add_test(NAME unit COMMAND catagui_tests)

    add_executable(catagui_engine_test tests/engine_smoke.c)
    target_link_libraries(catagui_engine_test PRIVATE catagui_core)
    target_compile_definitions(catagui_engine_test PRIVATE CATAGUI_SOURCE_DIR="${CATAGUI_TEST_SOURCE_DIR}")
    catagui_warnings(catagui_engine_test)
    add_test(NAME engine_smoke COMMAND catagui_engine_test)
    set_tests_properties(engine_smoke PROPERTIES LABELS engine TIMEOUT 1000)

    # Headless batch analysis of a small game through the KataGo JSON engine.
    add_test(NAME headless_analyze
             COMMAND catagui --analyze ${CATAGUI_TEST_SOURCE_DIR}/tests/data/sample_cjk.sgf
                     --out ${CMAKE_BINARY_DIR}/analyzed --set analysis.game_visits=20)
    set_tests_properties(headless_analyze PROPERTIES LABELS engine TIMEOUT 1000 PASS_REGULAR_EXPRESSION "DONE: 1 game")
endif()
