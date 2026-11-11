# Gifu

[![Build](https://github.com/JJLDonley/Gifu/actions/workflows/build.yml/badge.svg)](https://github.com/JJLDonley/Gifu/actions/workflows/build.yml)

A fast, cross-platform KataGo GUI written in C (Clay + raylib) — a modern rebuild of
LizzieYzy. Live analysis with candidates, ownership and win-rate graph; whole-game
analysis with a per-player report; engine-vs-engine and two-engine comparison; four
themes (Light, Dark, Cream, Coffee Paper); five UI languages; data-driven layout,
keymap, themes and engines.

## Running

1. Start `gifu` (or `gifu.exe`). On first run the **Engine setup** dialog offers to
   download KataGo (OpenCL, CPU or CUDA build), the strongest network from
   katagotraining.org and the human SL network into `katago/` next to the program. You can
   reopen it later from Engine → Download engines…. Without a window:
   `gifu --install all [--backend opencl|eigenavx2|eigen|cuda12.8-cudnn9.8.0]`.
   (Downloads use the system `curl`, which ships with Windows 10+ and Linux.)
2. Open an SGF with Ctrl+O or drag it onto the window.
3. For human-like play and evaluation, pick the "KataGo · Human style" engine in Settings
   and choose its strength (20k–9d, Pro).

On Linux, to add Gifu to your application menu, copy `share/applications/gifu.desktop`
to `~/.local/share/applications/` (set `Exec=` to the full path of `gifu`) and
`share/icons/hicolor/256x256/apps/gifu.png` to `~/.local/share/icons/hicolor/256x256/apps/`.

Updates: Help → Check for updates (Gifu also looks once a day; Settings → Updates turns
that off or switches to the Alpha channel for test versions). Nothing is downloaded or
installed until you choose Update and then Restart; the download is checked against the
release's SHA-256 sums, and `katago/` and your settings are kept. Without a window:
`gifu --update [--channel stable|alpha]`.

Batch analysis without a window:

    gifu --analyze games/*.sgf --out analyzed --set analysis.game_visits=200

## Customising

Everything user-facing is data in `assets/` and can be overridden from the config
folder (`%APPDATA%\Gifu` on Windows, `~/.config/gifu` on Linux):
`config.kv`, `keymap.kv`, `engines.kv` (incl. remote engines via `launcher = ssh ...`),
`layout.lisp` (hot-reloaded), `themes/*.theme`.

## Building

Needs CMake 3.24+, Ninja and GCC or Clang (MSVC is not supported); an internet
connection the first time.

```sh
cmake --preset linux-release        # or msys2-release (Windows, MSYS2 UCRT64 shell)
cmake --build build/linux-release
```

The first configure downloads the libraries Gifu builds with into `third_party/`
(pinned versions, checked against SHA-256 hashes; see `cmake/Dependencies.cmake`).
To build offline, download the archives listed in `third_party/README.md` and
extract them there yourself. Cross-compiling for Windows from Linux/WSL with
llvm-mingw (`$LLVM_MINGW` or `~/.local/share/toolchains/llvm-mingw`): `windows-release`
preset. Release packages: `python3 tools/package.py`. Every push is built on Linux and
on Windows (MSYS2) by GitHub Actions. Releases: `python3 tools/release.py alpha|beta|stable --push`
makes the next tag; `v0.2.0` publishes a release, `v0.2.0-alpha.1` a pre-release that only
the Alpha update channel offers.

## Credits

Gifu is a re-imagining of [LizzieYzy](https://github.com/yzyray/lizzieyzy) by yzyray
(GPL-3.0); its screen board reader adapts the stone recognition of
[readboard](https://github.com/yzyray/readboard_java). Analysis is done by
[KataGo](https://github.com/lightvector/KataGo) by lightvector (MIT) with networks from
[katagotraining.org](https://katagotraining.org); Gifu downloads them, it does not
bundle them. Built with [raylib](https://github.com/raysan5/raylib) (zlib),
[Clay](https://github.com/nicbarker/clay) (zlib), [yyjson](https://github.com/ibireme/yyjson)
(MIT), and stb_truetype and sinfl (public domain / MIT, bundled with raylib). Fonts:
Noto Sans CJK, Open Sans and Source Code Pro (SIL OFL 1.1). Details and license texts:
`THIRD_PARTY_NOTICES.md`.

License: GPL-3.0 (see `LICENSE`).
