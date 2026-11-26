#!/usr/bin/env bash
# Check that the postfiatd service in each operator Compose file sets
# stop_grace_period to at least 60 s, so `docker compose stop/down`, upgrades
# and host reboots do not SIGKILL the node after Docker's 10 s default.
#
# Reads the rendered config (`docker compose config --format json`), so it
# checks what Compose will actually use. A missing stop_grace_period key is
# treated as unset (= Docker's 10 s default) and fails.
#
# Usage: scripts/test-compose-stop-grace.sh [compose-file ...]
#   default files: the three scripts/docker-compose-*.yml operator files
# Env:   MIN_GRACE_SECONDS (default 60), SERVICE (default postfiatd)
# Exit:  0 all files pass, 1 at least one file fails, 2 usage/tool error
set -uo pipefail

MIN_GRACE_SECONDS="${MIN_GRACE_SECONDS:-60}"
SERVICE="${SERVICE:-postfiatd}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ "$#" -gt 0 ]; then
    files=("$@")
else
    files=(
        "$repo_root/scripts/docker-compose-external-validator.yml"
        "$repo_root/scripts/docker-compose-rpc.yml"
        "$repo_root/scripts/docker-compose-validator.yml"
    )
fi

for tool in docker jq; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: $tool not found" >&2
        exit 2
    fi
done
if ! docker compose version >/dev/null 2>&1; then
    echo "ERROR: docker compose plugin not available" >&2
    exit 2
fi
case "$MIN_GRACE_SECONDS" in
    '' | *[!0-9]*)
        echo "ERROR: MIN_GRACE_SECONDS must be a whole number" >&2
        exit 2
        ;;
esac

# Convert a Go duration as printed by Compose (e.g. 1m0s, 90s, 1h, 1m30.5s,
# 500ms) to whole seconds (rounded down). Prints nothing for invalid input.
duration_seconds() {
    awk -v d="$1" 'BEGIN {
        if (d !~ /^([0-9]+(\.[0-9]+)?(h|m|s|ms|us|µs|ns))+$/) exit 1
        total = 0
        while (match(d, /^[0-9]+(\.[0-9]+)?/)) {
            n = substr(d, 1, RLENGTH) + 0
            d = substr(d, RLENGTH + 1)
            if (d ~ /^ms/)      { total += n / 1000;       d = substr(d, 3) }
            else if (d ~ /^us/) { total += n / 1000000;    d = substr(d, 3) }
            else if (d ~ /^µs/) { total += n / 1000000;    d = substr(d, length("µs") + 1) }
            else if (d ~ /^ns/) { total += n / 1000000000; d = substr(d, 3) }
            else if (d ~ /^h/)  { total += n * 3600;       d = substr(d, 2) }
            else if (d ~ /^m/)  { total += n * 60;         d = substr(d, 2) }
            else if (d ~ /^s/)  { total += n;              d = substr(d, 2) }
        }
        printf "%d\n", total
    }'
}

fails=0
for f in "${files[@]}"; do
    name="${f#"$repo_root"/}"
    if [ ! -f "$f" ]; then
        echo "FAIL $name: file not found"
        fails=$((fails + 1))
        continue
    fi
    # HOSTNAME/NETWORK only need to be set for rendering; values are not checked.
    if ! json="$(HOSTNAME=check NETWORK="${NETWORK:-testnet}" \
        docker compose -f "$f" config --format json 2>/dev/null)" || [ -z "$json" ]; then
        echo "FAIL $name: docker compose config could not render the file"
        fails=$((fails + 1))
        continue
    fi
    if ! jq -e --arg s "$SERVICE" '.services | has($s)' >/dev/null <<<"$json"; then
        echo "FAIL $name: service '$SERVICE' not found"
        fails=$((fails + 1))
        continue
    fi
    raw="$(jq -r --arg s "$SERVICE" '.services[$s].stop_grace_period // ""' <<<"$json")"
    if [ -z "$raw" ]; then
        echo "FAIL $name: $SERVICE stop_grace_period unset (Docker default 10s), need >= ${MIN_GRACE_SECONDS}s"
        fails=$((fails + 1))
        continue
    fi
    secs="$(duration_seconds "$raw")"
    if [ -z "$secs" ]; then
        echo "FAIL $name: $SERVICE stop_grace_period '$raw' is not a valid duration"
        fails=$((fails + 1))
        continue
    fi
    if [ "$secs" -lt "$MIN_GRACE_SECONDS" ]; then
        echo "FAIL $name: $SERVICE stop_grace_period $raw = ${secs}s < ${MIN_GRACE_SECONDS}s"
        fails=$((fails + 1))
        continue
    fi
    echo "PASS $name: $SERVICE stop_grace_period $raw = ${secs}s >= ${MIN_GRACE_SECONDS}s"
done

echo "RESULT: ${#files[@]} file(s), $fails failure(s)"
[ "$fails" -eq 0 ]
