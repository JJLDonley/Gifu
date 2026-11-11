# Third-party notices

CataGui is licensed under the GNU General Public License v3.0 (see `LICENSE`).
It is a reimplementation of LizzieYzy (https://github.com/yzyray/lizzieyzy, GPL-3.0)
and bundles the following components:

| Component | Version | License | Where |
|---|---|---|---|
| raylib | 6.2 | zlib/libpng | `third_party/raylib/LICENSE` |
| Clay | 0.14 | zlib | `third_party/clay/LICENSE.md` |
| yyjson | 0.10.0 | MIT | `third_party/yyjson/LICENSE` |
| stb_truetype | 1.26 | Public domain / MIT | header of `third_party/stb/stb_truetype.h` |
| sinfl (Micha Mettke) | | MIT or public domain | end of `third_party/sinfl/sinfl.h` |
| Noto Sans CJK (SC, TC, JP, KR) | 2.004 | SIL OFL 1.1 | `assets/fonts/licenses/NotoSansCJK-LICENSE.txt` |
| Open Sans | (from LizzieYzy) | SIL OFL 1.1 (current releases; earlier releases Apache-2.0) | `assets/fonts/licenses/OpenSans-LICENSE.txt` |
| Source Code Pro | (from LizzieYzy) | SIL OFL 1.1, Reserved Font Name "Source" | `assets/fonts/licenses/SourceCodePro-LICENSE.txt` |
| Board texture `assets/textures/board-wood.png` | | from LizzieYzy (GPL-3.0) | |

The screen board reader (`src/core/board_reader.c`) adapts the stone recognition of
readboard (https://github.com/yzyray/readboard_java, GPL-3.0), the board sync tool
bundled with LizzieYzy.

KataGo (https://github.com/lightvector/KataGo, MIT) and its networks
(https://katagotraining.org) are not bundled in release archives; place them in a
`katago/` folder next to the program (see README.md).
