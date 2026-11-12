#!/usr/bin/env bash
# Builds the submission archive skiplist_submission.tar.gz in the project root.
# Only sources, tests, benchmarks, scripts, results and the report are included:
# no build directories, binaries or downloaded dependencies.
# Usage: scripts/make_archive.sh        (from anywhere)
set -euo pipefail
cd "$(dirname "$0")/.."

NAME=skiplist_submission
FILES=(src tests bench scripts results CMakeLists.txt README.md report.pdf)
[ -f report.tex ] && FILES+=(report.tex)

for f in "${FILES[@]}"; do
  [ -e "$f" ] || { echo "missing: $f" >&2; exit 1; }
done

rm -f "$NAME.tar.gz"
tar --transform "s,^,$NAME/," -czf "$NAME.tar.gz" "${FILES[@]}"

echo "created $NAME.tar.gz:"
tar -tzf "$NAME.tar.gz"
