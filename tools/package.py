#!/usr/bin/env python3
"""Build release binaries and assemble distributable archives in dist/.

Usage (from the repository root, on Linux/WSL):
    python3 tools/package.py [linux] [windows] [msys2] [appimage] [appdir]

Builds the `linux-release` and/or `windows-release` (llvm-mingw cross) presets
(`msys2`: the Windows package from the native `msys2-release` preset, as CI does),
installs each into dist/Gifu-<version>-<platform>/ and archives it
(.tar.xz for Linux, .zip for Windows). KataGo is not bundled.

`appimage` builds dist/Gifu-<version>-x86_64.AppImage with appimagetool
(from PATH, $APPIMAGETOOL, or downloaded once into build/tools/). Inside the
AppImage, KataGo is installed to ~/.local/share/Gifu/katago (the image is
read-only).
"""
import os, re, shutil, stat, subprocess, sys, tarfile, urllib.request, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(ROOT, "dist")
CMAKE = shutil.which("cmake") or os.path.expanduser("~/.local/bin/cmake")

def version():
    text = open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8").read()
    return re.search(r"project\(Gifu VERSION ([0-9.]+)", text).group(1)

def run(*args):
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=ROOT, check=True)

def package(platform, preset=None):
    preset = preset or f"{platform}-release"
    run(CMAKE, "--preset", preset)
    run(CMAKE, "--build", os.path.join(ROOT, "build", preset), "--target", "gifu")
    name = f"Gifu-{version()}-{platform}-x64"
    stage = os.path.join(DIST, name)
    shutil.rmtree(stage, ignore_errors=True)
    run(CMAKE, "--install", os.path.join(ROOT, "build", preset), "--prefix", stage)
    os.makedirs(os.path.join(stage, "katago", "networks"), exist_ok=True)
    with open(os.path.join(stage, "katago", "PUT_KATAGO_HERE.txt"), "w") as f:
        f.write("Place the KataGo release for your platform here (see README.md and assets/data/engines.kv),\n"
                "e.g. katago/windows-opencl/katago.exe + cfg files, and a network in katago/networks/.\n")
    if platform == "windows":
        archive = stage + ".zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
            for base, _, files in os.walk(stage):
                for fn in files:
                    full = os.path.join(base, fn)
                    z.write(full, os.path.join(name, os.path.relpath(full, stage)))
    else:
        archive = stage + ".tar.xz"
        with tarfile.open(archive, "w:xz") as t:
            t.add(stage, arcname=name)
    size = os.path.getsize(archive) / (1024 * 1024)
    print(f"-> {os.path.relpath(archive, ROOT)} ({size:.1f} MB)")

APPIMAGETOOL_URL = "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"

def appimagetool():
    tool = os.environ.get("APPIMAGETOOL") or shutil.which("appimagetool")
    if tool:
        return tool
    tool = os.path.join(ROOT, "build", "tools", "appimagetool-x86_64.AppImage")
    if not os.path.exists(tool):
        os.makedirs(os.path.dirname(tool), exist_ok=True)
        print(f"+ download {APPIMAGETOOL_URL}", flush=True)
        urllib.request.urlretrieve(APPIMAGETOOL_URL, tool)
        os.chmod(tool, os.stat(tool).st_mode | stat.S_IEXEC)
    return tool

def package_appimage(pack=True):
    preset = "linux-release"
    run(CMAKE, "--preset", preset)
    run(CMAKE, "--build", os.path.join(ROOT, "build", preset), "--target", "gifu")
    appdir = os.path.join(DIST, "Gifu.AppDir")
    shutil.rmtree(appdir, ignore_errors=True)
    app = os.path.join(appdir, "usr", "lib", "gifu")   # the program finds assets/ next to itself
    run(CMAKE, "--install", os.path.join(ROOT, "build", preset), "--prefix", app)
    with open(os.path.join(appdir, "AppRun"), "w") as f:
        f.write('#!/bin/sh\nHERE="$(dirname "$(readlink -f "$0")")"\nexec "$HERE/usr/lib/gifu/gifu" "$@"\n')
    os.chmod(os.path.join(appdir, "AppRun"), 0o755)
    shutil.copy(os.path.join(ROOT, "packaging", "linux", "gifu.desktop"), os.path.join(appdir, "gifu.desktop"))
    shutil.copy(os.path.join(ROOT, "assets", "icons", "gifu.png"), os.path.join(appdir, "gifu.png"))
    print(f"-> {os.path.relpath(appdir, ROOT)} (AppDir)")
    if not pack:
        return
    out = os.path.join(DIST, f"Gifu-{version()}-x86_64.AppImage")
    env = dict(os.environ, ARCH="x86_64")
    # --appimage-extract-and-run: works without FUSE (containers, WSL).
    cmd = [appimagetool(), "--appimage-extract-and-run", "--no-appstream", appdir, out]
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True, env=env)
    size = os.path.getsize(out) / (1024 * 1024)
    print(f"-> {os.path.relpath(out, ROOT)} ({size:.1f} MB)")

if __name__ == "__main__":
    targets = sys.argv[1:] or ["linux", "windows"]
    os.makedirs(DIST, exist_ok=True)
    for t in targets:
        if t == "appimage":
            package_appimage()
        elif t == "appdir":   # the AppImage's contents only (no appimagetool needed)
            package_appimage(pack=False)
        elif t == "msys2":    # Windows package built natively in an MSYS2 UCRT64 shell (CI)
            package("windows", preset="msys2-release")
        else:
            package(t)
