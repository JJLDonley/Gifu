# Third-party notices

Gifu is licensed under the GNU General Public License v3.0 (see `LICENSE`).
It is a reimplementation of LizzieYzy (https://github.com/yzyray/lizzieyzy, GPL-3.0)
and is built with / ships the following components. The libraries are downloaded
at build time into `third_party/` (`cmake/Dependencies.cmake`); release archives
include their license texts in `licenses/`.

| Component | Version | License | Where |
|---|---|---|---|
| raylib | 6.2 | zlib/libpng | https://github.com/raysan5/raylib (`third_party/raylib/LICENSE`) |
| Clay | 0.14 | zlib | https://github.com/nicbarker/clay (`third_party/clay/LICENSE.md`) |
| yyjson | 0.10.0 | MIT | https://github.com/ibireme/yyjson (`third_party/yyjson/LICENSE`) |
| stb_truetype (bundled with raylib) | 1.26 | Public domain / MIT | end of `third_party/raylib/src/external/stb_truetype.h` |
| sinfl by Micha Mettke (bundled with raylib) | | MIT or public domain | end of `third_party/raylib/src/external/sinfl.h` |
| Noto Sans CJK (SC, TC, JP, KR) | 2.004 | SIL OFL 1.1 | `assets/fonts/licenses/NotoSansCJK-LICENSE.txt` |
| Open Sans | (from LizzieYzy) | SIL OFL 1.1 (current releases; earlier releases Apache-2.0) | `assets/fonts/licenses/OpenSans-LICENSE.txt` |
| Source Code Pro | (from LizzieYzy) | SIL OFL 1.1, Reserved Font Name "Source" | `assets/fonts/licenses/SourceCodePro-LICENSE.txt` |
| Board texture `assets/textures/board-wood.png` | | from LizzieYzy (GPL-3.0) | |
| Leela (engine and network), by Gian-Carlo Pascutto | 0.11.0 (commit afe9099), modified | MIT | https://github.com/gcp/Leela, in `leela/` (`leela/LICENSE`) |
| OpenCL headers and C++ bindings (Khronos) | v2026.05.29 | Apache-2.0 | https://github.com/KhronosGroup/OpenCL-Headers, OpenCL-CLHPP, in `leela/opencl/` |

The screen board reader (`src/core/board_reader.c`) adapts the stone recognition of
readboard (https://github.com/yzyray/readboard_java, GPL-3.0), the board sync tool
bundled with LizzieYzy.

KataGo (https://github.com/lightvector/KataGo, MIT) and its networks
(https://katagotraining.org) are not bundled in release archives; place them in a
`katago/` folder next to the program (see README.md).
