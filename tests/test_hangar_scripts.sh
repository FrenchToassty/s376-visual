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
for nn in 01 02 03 04 05 06 07 08 09 10 11 12; do
    echo "$list" | grep -q "^$nn " || bad "list missing $nn"
done
echo "$list" | grep -q 'review-now' || bad "list missing review-now"
echo "$list" | grep -q 'demo-kjfk-day' || bad "list missing demo-kjfk-day"
echo "$list" | grep -q 'skyline-water' || bad "list missing skyline-water"
if echo "$list" | grep -q 'fetch-kewr\|fetch-us-next'; then
    bad "default list must not include KEWR / US-next"
else
    ok "default list is KJFK-only"
fi
ok "list has 12 KJFK steps"

all="$(scripts/hangar-queue.sh --list --all-airports)"
echo "$all" | grep -q '^13  fetch-kewr' || bad "all-airports missing 13 fetch-kewr"
echo "$all" | grep -q '^14  fetch-us-next' || bad "all-airports missing 14 fetch-us-next"
ok "opt-in --all-airports adds KEWR / US-next"

help="$(scripts/hangar-queue.sh --help)"
echo "$help" | grep -q 'usage: scripts/hangar-queue.sh \[--from NN\] \[--only NN\] \[--list\] \[--dry-run\]' \
    || bad "usage line mismatch"
echo "$help" | grep -q -- '--all-airports' || bad "help missing --all-airports"
echo "$help" | grep -q -- '-j, --jobs' || bad "help missing --jobs"
ok "usage line"

dry="$(scripts/hangar-queue.sh --dry-run --only 01 --out /tmp/s376-queue-dry --root "$ROOT")"
echo "$dry" | grep -q 'cmake' || bad "dry 01 missing cmake"
echo "$dry" | grep -q 'MemoryMax=12G' || bad "dry 01 missing systemd-run MemoryMax=12G"
ok "dry-run 01"

dry6="$(scripts/hangar-queue.sh --dry-run --only 06 --out /tmp/s376-queue-dry --root "$ROOT")"
echo "$dry6" | grep -q -- '--jobs 16' || bad "dry 06 missing fetch --jobs 16"
echo "$dry6" | grep -q 'MemoryMax=12G' || bad "dry 06 missing MemoryMax=12G"
ok "dry-run 06 fetch --jobs"

dry2="$(scripts/hangar-queue.sh --dry-run --only 02 --out /tmp/s376-queue-dry --root "$ROOT" --kjfk /tmp/fake-kjfk)"
echo "$dry2" | grep -q 'hangar-review' || bad "dry 02 missing hangar-review"
ok "dry-run 02"

dry3="$(scripts/hangar-queue.sh --dry-run --from 08 --out /tmp/s376-queue-dry --root "$ROOT")"
echo "$dry3" | grep -q 'contact-sheet\|review-after-fetch\|hangar-review' || bad "dry from 08 missing review"
echo "$dry3" | grep -q 'demo kjfk\|--demo kjfk\|demo-kjfk' || bad "dry from 08 missing KJFK demo"
echo "$dry3" | grep -q 'manhattan\|skyline-water\|--hdg 310' || bad "dry from 08 missing skyline"
if echo "$dry3" | grep -qiE 'KEWR|fetch KEWR'; then
    bad "dry --from 08 must not fetch KEWR (opt-in only)"
fi
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
echo "$rev" | grep -q -- '--hdg 310' || bad "review dry missing Manhattan skyline"
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
