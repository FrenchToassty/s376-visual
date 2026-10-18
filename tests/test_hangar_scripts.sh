#!/usr/bin/env bash
# Syntax + dry-run coverage for hangar-review.sh and hangar-queue.sh.
# No GPU, no fetch, no network, no sudo. Never talks to the sim LAN.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

fail=0
ok() { echo "ok  $*"; }
bad() { echo "FAIL $*" >&2; fail=1; }

bash -n scripts/hangar-review.sh || bad "bash -n hangar-review"
bash -n scripts/hangar-queue.sh || bad "bash -n hangar-queue"

if command -v shellcheck >/dev/null 2>&1; then
    shellcheck -e SC2086,SC2181 scripts/hangar-review.sh scripts/hangar-queue.sh \
        || bad "shellcheck"
    ok "shellcheck"
else
    ok "shellcheck not installed (bash -n is the CI floor)"
fi

for s in scripts/hangar-review.sh scripts/hangar-queue.sh; do
    if grep -nE -- '--(bind|dest|out)[= ][^[:space:]]*(10\.1\.95|192\.168\.173|:3340|:3341)' "$s"; then
        bad "$s has a forbidden LAN dest"
    else
        ok "$s has no forbidden dest"
    fi
    grep -q -- '--offscreen' "$s" || bad "$s missing --offscreen"
done

list="$(scripts/hangar-queue.sh --list)"
echo "$list"
for nn in 01 02 03 04 05 06 07 08 09 10; do
    echo "$list" | grep -q "^$nn " || bad "list missing $nn"
done
echo "$list" | grep -q 'review-now' || bad "list missing review-now"
echo "$list" | grep -q 'fetch-us-next' || bad "list missing fetch-us-next"
ok "list has 10 steps"

help="$(scripts/hangar-queue.sh --help)"
echo "$help" | grep -q 'usage: scripts/hangar-queue.sh \[--from NN\] \[--only NN\] \[--list\] \[--dry-run\]' \
    || bad "usage line mismatch"
ok "usage line"

dry="$(scripts/hangar-queue.sh --dry-run --only 01 --out /tmp/s376-queue-dry --root "$ROOT")"
echo "$dry" | grep -q 'cmake' || bad "dry 01 missing cmake"
echo "$dry" | grep -q 'MemoryMax' || bad "dry 01 missing systemd-run MemoryMax"
ok "dry-run 01"

dry2="$(scripts/hangar-queue.sh --dry-run --only 02 --out /tmp/s376-queue-dry --root "$ROOT" --kjfk /tmp/fake-kjfk)"
echo "$dry2" | grep -q 'hangar-review' || bad "dry 02 missing hangar-review"
ok "dry-run 02"

dry3="$(scripts/hangar-queue.sh --dry-run --from 08 --out /tmp/s376-queue-dry --root "$ROOT")"
echo "$dry3" | grep -q 'contact-sheet\|review-after-fetch\|hangar-review' || bad "dry from 08 missing review"
echo "$dry3" | grep -q 'KEWR\|fetch KEWR\|kewr' || bad "dry from 08 missing later KEWR"
if echo "$dry3" | grep -q 'cmake -S'; then
    bad "dry --from 08 should not run build"
fi
ok "dry-run --from 08"

rev="$(scripts/hangar-review.sh --dry-run --cache /tmp/fake-kjfk --out /tmp/s376-review-dry --bin /tmp/fake-visual)"
echo "$rev" | grep -q 'KJFK:04R:2.99' || bad "review dry missing 2.99"
echo "$rev" | grep -q 'KJFK:04R:0.15' || bad "review dry missing 0.15"
echo "$rev" | grep -q -- '--tod 12' || bad "review dry missing day"
echo "$rev" | grep -q -- '--tod 19' || bad "review dry missing dusk"
echo "$rev" | grep -q -- '--tod 3' || bad "review dry missing night"
echo "$rev" | grep -q -- '--channel all' || bad "review dry missing wrap"
echo "$rev" | grep -q -- '--alt 12000' || bad "review dry missing 12k"
echo "$rev" | grep -q -- '--rwy-cond 2' || bad "review dry missing wet"
echo "$rev" | grep -q -- '--offscreen' || bad "review dry missing offscreen"
echo "$rev" | grep -q -- '--bench' || bad "review dry missing bench"
ok "hangar-review dry-run shot list"

if [[ -e /tmp/s376-queue-dry/STATUS.txt ]]; then
    bad "dry-run wrote STATUS.txt"
else
    ok "dry-run wrote no STATUS"
fi

if [[ "$fail" -ne 0 ]]; then
    echo "hangar script tests failed" >&2
    exit 1
fi
echo "hangar script tests ok"
