#!/bin/sh
# Release notes for tag $1, shown on the release page and in Gifu's Updates dialog.
# Written for players: release_notes/<tag>.md when it exists (write it before tagging).
# Otherwise the commits since the previous release tag, with their full messages (not only the
# titles), < and > escaped so GitHub does not take them for HTML.
set -e
tag="$1"
if [ -f "release_notes/${tag}.md" ]; then
    cat "release_notes/${tag}.md"
    exit 0
fi
prev="$(git describe --tags --abbrev=0 --match 'v*' "${tag}^" 2>/dev/null || true)"
range="${prev:+${prev}..}${tag}"
echo "## Changes${prev:+ since ${prev#v}}"
echo
git log --no-merges --format='%s%n%b%x1e' "$range" |
    grep -v -E '^(Co-Authored-By|Claude-Session):' |
    sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' |
    awk 'BEGIN { RS = "\036" } NF { n = split($0, l, "\n"); first = 1
         for (i = 1; i <= n; i++) { if (l[i] == "") continue
             if (first) { print "- **" l[i] "**"; first = 0 } else print "  " l[i] }
         print "" }'
