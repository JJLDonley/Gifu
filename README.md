# Gifu

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

Batch analysis without a window:

    gifu --analyze games/*.sgf --out analyzed --set analysis.game_visits=200

## Customising

Everything user-facing is data in `assets/` and can be overridden from the config
folder (`%APPDATA%\Gifu` on Windows, `~/.config/gifu` on Linux):
`config.kv`, `keymap.kv`, `engines.kv` (incl. remote engines via `launcher = ssh ...`),
`layout.lisp` (hot-reloaded), `themes/*.theme`.

## Building

See `CLAUDE.md` (build presets for Linux gcc, llvm-mingw cross-compiles from WSL, MSYS2)
and `PLAN.md` (architecture and roadmap). Packages: `python3 tools/package.py`.

License: GPL-3.0 (see `LICENSE`, `THIRD_PARTY_NOTICES.md`).
