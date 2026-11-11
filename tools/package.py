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

# Pre-release label (alpha.1, beta.2, ...), set by CI from the tag; empty for stable.
LABEL = os.environ.get("GIFU_VERSION_LABEL", "").strip()

def version():
    text = open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8").read()
    core = re.search(r"project\(Gifu VERSION ([0-9.]+)", text).group(1)
    return f"{core}-{LABEL}" if LABEL else core

def configure(preset):
    run(CMAKE, "--preset", preset, f"-DGIFU_VERSION_LABEL={LABEL}")

def run(*args):
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=ROOT, check=True)

def package(platform, preset=None):
    preset = preset or f"{platform}-release"
    configure(preset)
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

INFO_PLIST = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Gifu</string>
  <key>CFBundleDisplayName</key><string>Gifu</string>
  <key>CFBundleIdentifier</key><string>io.github.jjldonley.gifu</string>
  <key>CFBundleExecutable</key><string>gifu</string>
  <key>CFBundleIconFile</key><string>gifu</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>{version}</string>
  <key>CFBundleVersion</key><string>{version}</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>CFBundleDocumentTypes</key>
  <array><dict>
    <key>CFBundleTypeName</key><string>Go game record</string>
    <key>CFBundleTypeExtensions</key><array><string>sgf</string></array>
    <key>CFBundleTypeRole</key><string>Editor</string>
  </dict></array>
</dict>
</plist>
"""

def package_macos(preset="macos-release"):
    """Gifu.app (the program, its assets and notices in Contents/MacOS), zipped with ditto."""
    configure(preset)
    run(CMAKE, "--build", os.path.join(ROOT, "build", preset), "--target", "gifu")
    stage = os.path.join(DIST, "macos")
    shutil.rmtree(stage, ignore_errors=True)
    app = os.path.join(stage, "Gifu.app")
    run(CMAKE, "--install", os.path.join(ROOT, "build", preset), "--prefix", os.path.join(app, "Contents", "MacOS"))
    with open(os.path.join(app, "Contents", "Info.plist"), "w") as f:
        f.write(INFO_PLIST.format(version=version()))
    # Icon: gifu.png -> gifu.icns with the system's sips / iconutil.
    resources = os.path.join(app, "Contents", "Resources")
    iconset = os.path.join(stage, "gifu.iconset")
    os.makedirs(resources, exist_ok=True)
    os.makedirs(iconset, exist_ok=True)
    png = os.path.join(ROOT, "assets", "icons", "gifu.png")
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            px = size * scale
            out = os.path.join(iconset, f"icon_{size}x{size}{'@2x' if scale == 2 else ''}.png")
            run("sips", "-z", str(px), str(px), png, "--out", out)
    run("iconutil", "-c", "icns", iconset, "-o", os.path.join(resources, "gifu.icns"))
    shutil.rmtree(iconset)
    # Ad-hoc signature for the whole bundle (no Apple Developer account needed): Apple
    # Silicon refuses unsigned code, and an unsigned download can be reported as
    # "damaged". It is not notarized: users approve it once in System Settings.
    run("codesign", "--force", "--deep", "--sign", "-", app)
    run("codesign", "--verify", "--deep", "--strict", "--verbose=2", app)
    archive = os.path.join(DIST, f"Gifu-{version()}-macos.zip")
    if os.path.exists(archive):
        os.remove(archive)
    run("ditto", "-c", "-k", "--keepParent", app, archive)   # keeps permissions
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
    configure(preset)
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
        elif t == "macos":    # Gifu.app, zipped (on a Mac)
            package_macos()
        elif t == "msys2":    # Windows package built natively in an MSYS2 UCRT64 shell (CI)
            package("windows", preset="msys2-release")
        else:
            package(t)
