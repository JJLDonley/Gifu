#!/bin/sh
# Release notes for tag $1: the commits since the previous release tag (one line
# each), shown on the release page and in Gifu's Updates dialog.
set -e
tag="$1"
prev="$(git describe --tags --abbrev=0 --match 'v*' "${tag}^" 2>/dev/null || true)"
range="${prev:+${prev}..}${tag}"
echo "## Changes${prev:+ since ${prev#v}}"
echo
git log --no-merges --format='- %s' "$range"
