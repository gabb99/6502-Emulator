#!/usr/bin/env bash
# Fetches and runs the SingleStepTests / Tom Harte per-cycle suite.
#
#   tools/harte.sh                # all 256 opcodes (~840 MB, streamed)
#   tools/harte.sh a9 bd 91       # just these
#   KEEP=1 tools/harte.sh a9      # keep the JSON instead of deleting it
#
# Files are downloaded a batch at a time and deleted after the batch runs, so
# peak disk use stays at a few tens of megabytes rather than 840 MB.
set -uo pipefail

BASE=${BASE:-https://raw.githubusercontent.com/SingleStepTests/65x02/main/6502/v1}
RUNNER=${RUNNER:-./build/run_harte}
CACHE=${CACHE:-$(mktemp -d)}
KEEP=${KEEP:-0}
BATCH=${BATCH:-16}

if [[ ! -x "$RUNNER" ]]; then
    echo "runner not found at $RUNNER - build first:" >&2
    echo "  cmake -S . -B build && cmake --build build -j" >&2
    exit 2
fi

if [[ $# -gt 0 ]]; then
    opcodes=("$@")
else
    opcodes=()
    for n in $(seq 0 255); do opcodes+=("$(printf '%02x' "$n")"); done
fi

mkdir -p "$CACHE"
echo "cache: $CACHE   opcodes: ${#opcodes[@]}   batch: $BATCH"

failures=0
total=${#opcodes[@]}
for ((i = 0; i < total; i += BATCH)); do
    batch=("${opcodes[@]:i:BATCH}")
    for op in "${batch[@]}"; do
        [[ -s "$CACHE/$op.json" ]] || curl -sL -o "$CACHE/$op.json" "$BASE/$op.json" &
    done
    wait

    files=()
    for op in "${batch[@]}"; do files+=("$CACHE/$op.json"); done
    "$RUNNER" "${files[@]}" | grep -v '^TOTAL' || failures=$((failures + 1))

    if [[ "$KEEP" != "1" ]]; then
        for op in "${batch[@]}"; do rm -f "$CACHE/$op.json"; done
    fi
done

echo
if [[ $failures -eq 0 ]]; then
    echo "ALL PASS - $total opcodes"
else
    echo "$failures batch(es) contained failures"
fi
[[ "$KEEP" == "1" ]] || rmdir "$CACHE" 2>/dev/null
exit $((failures == 0 ? 0 : 1))
