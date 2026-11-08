#!/usr/bin/env bash
# run_foit_test.sh - builds ONLY the small command-line tool (no SDL, no viewer) and checks your character files.
#
#   tools/run_foit_test.sh /path/to/folder/with/foit_art_artpt  [state]  [out_dir]
#
# The folder may hold any number of characters: <name>.foit  <name>.art  <name>.art-pt  <name>_1p.dds
# (they can be in sub-folders; every .foit found is parsed).  Nothing is modified or copied anywhere.
# Needs: cmake, a C++17 compiler (macOS: xcode-select --install ; brew install cmake).
set -euo pipefail
DATA="${1:?usage: run_foit_test.sh <folder with .foit/.art/.art-pt> [state=WalkFwd] [out_dir=./foit_out]}"
STATE="${2:-WalkFwd}"
OUT="${3:-./foit_out}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build_foit"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DZDROID_BUILD_VIEWER=OFF -DZDROID_BUILD_TESTS=OFF >/dev/null
cmake --build "$BUILD" --target zdroid_foit -j 4 >/dev/null
mkdir -p "$OUT"
echo "== parse + image validation (every .foit under $DATA)"
"$BUILD/zdroid_foit" "$DATA" | tee "$OUT/report.txt"
# a few PNGs of the first character found, to look at with your own eyes (pivot = magenta cross, red hit, green hurt, blue push)
FIRST="$(find "$DATA" -name '*.foit' | sort | head -1)"
if [ -n "$FIRST" ]; then
  echo "== sample PNGs from $(basename "$FIRST") -> $OUT"
  "$BUILD/zdroid_foit" "$FIRST" --frames 1,137 --state "$STATE" --chains --out "$OUT" | tee -a "$OUT/report.txt"
fi
echo
echo "Done. Send me $OUT/report.txt (and the PNGs if something looks wrong)."
