#!/usr/bin/env bash
# Packs a build into dist/<name>.zip.
#
# Usage: package.sh <linux|macos|windows> <name> <file>...
#
# The zip contains one folder <name>/ with the given files, a readme.txt made
# from packaging/readme.txt (@VERSION@ is replaced with $VERSION and the
# @PLATFORM_NOTES@ line with packaging/readme-<platform>.txt) and an empty
# data/ folder for the original game files, holding only a short note.
# Set CRLF=1 to give the text files Windows line endings.
set -euo pipefail

platform="$1"
name="$2"
shift 2

notes="packaging/readme-$platform.txt"
[ -f "$notes" ] || { echo "missing $notes" >&2; exit 1; }

stage="dist/$name"
rm -rf "$stage" "dist/$name.zip"
mkdir -p "$stage"

for f in "$@"; do
    cp "$f" "$stage/"
done

awk -v notes="$notes" -v version="${VERSION:-dev}" '
    /^@PLATFORM_NOTES@$/ { while ((getline line < notes) > 0) print line; next }
    { gsub(/@VERSION@/, version); print }
' packaging/readme.txt > "$stage/readme.txt"

mkdir -p "$stage/data"
cp packaging/data-readme.txt "$stage/data/COPY_ORIGINAL_GAME_FILES_HERE.txt"

if [ "${CRLF:-0}" = 1 ]; then
    sed -i 's/$/\r/' "$stage/readme.txt" "$stage/data/COPY_ORIGINAL_GAME_FILES_HERE.txt"
fi

(cd dist && zip -r -9 "$name.zip" "$name")
echo "Created dist/$name.zip"
