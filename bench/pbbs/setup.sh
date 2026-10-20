#!/usr/bin/env bash
# Prepares a work directory for comparing parlay's scheduler against
# taskparts (mainline and minimal) on the PBBS benchmarks:
#   $WORK/pbbsbench        copy of pbbsbench, whose parlay is $WORK/parlay-overlay
#   $WORK/parlay-overlay   parlaylib's headers plus the taskparts plugin
#   $WORK/taskparts-main   headers of taskparts' main branch
# Nothing outside of $WORK is modified.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TASKPARTS="$(cd "$HERE/../.." && pwd)"
PBBS="${PBBS:-$HOME/proj/pbbsbench}"
PARLAYLIB="${PARLAYLIB:-$HOME/proj/parlaylib}"
WORK="${1:?usage: setup.sh WORK_DIR}"

mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"

rsync -a --exclude .git "$PBBS/" "$WORK/pbbsbench/"

# parlaylib at the commit that pbbsbench pins as its submodule, since
# pbbsbench doesn't build against newer parlaylib APIs
PARLAYLIB_REV="${PARLAYLIB_REV:-$(git -C "$PBBS" ls-tree HEAD parlaylib | awk '{print $3}')}"
rm -rf "$WORK/parlay-overlay"
mkdir -p "$WORK/parlay-overlay"
git -C "$PARLAYLIB" archive "$PARLAYLIB_REV" include | tar -x -C "$WORK/parlay-overlay"
cp "$HERE/taskparts.h" "$WORK/parlay-overlay/include/parlay/internal/scheduler_plugins/"
python3 - "$WORK/parlay-overlay/include/parlay/parallel.h" <<'PY'
import sys
path = sys.argv[1]
src = open(path).read()
old = '#if defined(PARLAY_CILKPLUS)'
new = ('#if defined(PARLAY_TASKPARTS)\n'
       '#include "internal/scheduler_plugins/taskparts.h"  // IWYU pragma: keep, export\n\n'
       '#elif defined(PARLAY_CILKPLUS)')
if new not in src:
    assert src.count(old) == 1
    open(path, 'w').write(src.replace(old, new))
PY

# benchmarks are built as one translation unit, which needs include guards
for h in "$WORK"/pbbsbench/benchmarks/*/bench/*.h; do
  if ! grep -q -e "pragma once" -e "^#ifndef" "$h"; then
    printf '#pragma once\n' | cat - "$h" > "$h.tmp" && mv "$h.tmp" "$h"
  fi
done

rm -rf "$WORK/pbbsbench/parlaylib"
ln -s "$WORK/parlay-overlay" "$WORK/pbbsbench/parlaylib"

rm -rf "$WORK/taskparts-main"
mkdir -p "$WORK/taskparts-main"
git -C "$TASKPARTS" archive main include | tar -x -C "$WORK/taskparts-main"

echo "parlaylib $(git -C "$PARLAYLIB" rev-parse --short "$PARLAYLIB_REV")" > "$WORK/versions.txt"
echo "pbbsbench $(git -C "$PBBS" rev-parse --short HEAD)" >> "$WORK/versions.txt"
echo "taskparts-main $(git -C "$TASKPARTS" rev-parse --short main)" >> "$WORK/versions.txt"
echo "taskparts-minimal $(git -C "$TASKPARTS" rev-parse --short HEAD)" >> "$WORK/versions.txt"
cat "$WORK/versions.txt"
