#!/usr/bin/env python3
"""Build release binaries and assemble distributable archives in dist/.

Usage (from the repository root, on Linux/WSL):
    python3 tools/package.py [linux] [windows]

Builds the `linux-release` and/or `windows-release` (llvm-mingw cross) presets,
installs each into dist/CataGui-<version>-<platform>/ and archives it
(.tar.xz for Linux, .zip for Windows). KataGo is not bundled.
"""
import os, re, shutil, subprocess, sys, tarfile, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(ROOT, "dist")
CMAKE = shutil.which("cmake") or os.path.expanduser("~/.local/bin/cmake")

def version():
    text = open(os.path.join(ROOT, "CMakeLists.txt")).read()
    return re.search(r"project\(CataGui VERSION ([0-9.]+)", text).group(1)

def run(*args):
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=ROOT, check=True)

def package(platform):
    preset = f"{platform}-release"
    run(CMAKE, "--preset", preset)
    run(CMAKE, "--build", os.path.join(ROOT, "build", preset), "--target", "catagui")
    name = f"CataGui-{version()}-{platform}-x64"
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

if __name__ == "__main__":
    targets = sys.argv[1:] or ["linux", "windows"]
    os.makedirs(DIST, exist_ok=True)
    for t in targets:
        package(t)
