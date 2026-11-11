#!/bin/sh
# CI: for a tag v<version>[-<label>], checks that <version> is project(VERSION) in
# CMakeLists.txt and exports the label (alpha.1, ...) for tools/package.py.
set -e
case "${GITHUB_REF:-}" in
    refs/tags/v*) ;;
    *) exit 0 ;;   # not a release: plain version
esac
tag="${GITHUB_REF_NAME#v}"
core="${tag%%-*}"
label=""
case "$tag" in *-*) label="${tag#*-}" ;; esac
project="$(sed -n 's/.*project(Gifu VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
if [ "$core" != "$project" ]; then
    echo "::error::tag ${GITHUB_REF_NAME} does not match project(VERSION ${project}) in CMakeLists.txt"
    exit 1
fi
echo "GIFU_VERSION_LABEL=${label}" >> "$GITHUB_ENV"
echo "release ${tag} (label '${label}')"
