#!/usr/bin/env python3
"""Creates the next release tag; pushing it makes GitHub build and publish the release.

    python3 tools/release.py alpha     # v<version>-alpha.<n+1>  (pre-release: alpha channel only)
    python3 tools/release.py beta      # v<version>-beta.<n+1>   (pre-release)
    python3 tools/release.py stable    # v<version>              (every user)
    add --push to push the tag right away (otherwise the command is printed)

<version> is project(VERSION) in CMakeLists.txt; bump it there (and commit) before
the first alpha of a new version. A stable tag cannot be made twice.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()


def project_version():
    text = open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8").read()
    return re.search(r"project\(Gifu VERSION ([0-9.]+)", text).group(1)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1 or args[0] not in ("alpha", "beta", "stable"):
        print(__doc__)
        return 2
    kind, push = args[0], "--push" in sys.argv
    if git("status", "--porcelain", "--untracked-files=no"):
        print("Commit your changes first: the tag marks the current commit.")
        return 1
    version = project_version()
    tags = set(git("tag", "--list", f"v{version}*").split())
    if f"v{version}" in tags:
        print(f"v{version} is already released: raise project(VERSION) in CMakeLists.txt first.")
        return 1
    if kind == "stable":
        tag = f"v{version}"
    else:
        numbers = [int(m.group(1)) for t in tags if (m := re.fullmatch(rf"v{re.escape(version)}-{kind}\.(\d+)", t))]
        tag = f"v{version}-{kind}.{max(numbers, default=0) + 1}"
    git("tag", "-a", tag, "-m", f"Gifu {tag[1:]}")
    print(f"tagged {tag} on {git('rev-parse', '--short', 'HEAD')}")
    if push:
        subprocess.run(["git", "push", "origin", tag], cwd=ROOT, check=True)
        print("pushed: GitHub Actions builds and publishes the release")
    else:
        print(f"push it to publish:  git push origin {tag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
