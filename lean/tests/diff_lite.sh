#!/usr/bin/env bash
# Part of the SharpSFV C++ port (GPL-3.0). Test tooling, added 2026-10-06 (SharpSFV Lite).
#
# Same verdicts as the full CLI: runs the full SharpSFV-cli and SharpSFV-Lite on the same
# checksum lists (.sfv in both byte orders, .md5, .xxh3; with comments, odd lines, Unicode
# names, damaged and missing files) and compares OK / BAD / MISSING and the exit code. The full
# CLI counts unreadable files as BAD, so Lite's BAD + ERROR is compared with its BAD. Lite runs
# twice per list: HDD mode (1 worker) and SSD mode (12 workers).
#
#   lean/tests/diff_lite.sh <full cli> <lite> [runner]      runner: e.g. "wine"
set -u
CLI="$1"; LITE="$2"; RUN="${3:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
python3 "$HERE/../../scripts/make_corpus.py" "$WORK/corpus" >/dev/null
D="$WORK/corpus/data"
pass=0; fail=0

p() { if [ -n "$RUN" ]; then winepath -w "$1" 2>/dev/null; else printf '%s' "$1"; fi; }

# Full CLI: "OK: n", "BAD: n", "MISSING: n" in its summary.
cli_counts() {
    local out code
    out=$($RUN "$CLI" "$(p "$1")" 2>&1); code=$?
    out=$(printf '%s\n' "$out" | tr -d '\r')
    local ok bad miss
    ok=$(printf '%s\n' "$out" | sed -n 's/^OK: //p' | tail -1)
    bad=$(printf '%s\n' "$out" | sed -n 's/^BAD: //p' | tail -1)
    miss=$(printf '%s\n' "$out" | sed -n 's/^MISSING: //p' | tail -1)
    echo "${ok:-?} ${bad:-?} ${miss:-?} exit=$code"
}

# Lite: the counts line of its status file (OK BAD MISSING ERROR), BAD+ERROR folded together.
lite_counts() { # list mode
    local st="$WORK/status.txt"
    rm -f "$st"
    $RUN "$LITE" "$(p "$1")" -status "$(p "$st")" -mode "$2" -threads 12 >/dev/null 2>&1; local code=$?
    local c; c=$(tr -d '\r' < "$st" | sed -n 's/^counts //p')
    set -- $c
    echo "$1 $(( $2 + $4 )) $3 exit=$code"
}

check() { # name list
    local want got1 got2
    want=$(cli_counts "$2")
    got1=$(lite_counts "$2" hdd)
    got2=$(lite_counts "$2" ssd)
    if [ "$want" = "$got1" ] && [ "$want" = "$got2" ]; then
        pass=$((pass + 1)); printf 'PASS  %-34s %s\n' "$1" "$want"
    else
        fail=$((fail + 1)); printf 'FAIL  %-34s cli [%s] lite-hdd [%s] lite-ssd [%s]\n' "$1" "$want" "$got1" "$got2"
    fi
}

# 1. The corpus's own lists (comments, odd lines, both CRC byte orders, Unicode names).
for f in "$D"/*.sfv "$D"/*.md5; do check "corpus $(basename "$f")" "$f"; done

# 2. Lists the full CLI creates, for each algorithm, then verified untouched...
for a in crc32 md5 xxh3; do
    case $a in crc32) e=sfv;; md5) e=md5;; xxh3) e=xxh3;; esac
    $RUN "$CLI" -create "$(p "$D")" -algo $a -out "$(p "$D/all.$e")" >/dev/null 2>&1
    check "created all.$e" "$D/all.$e"
done

# 3. ...and after damaging one file and deleting another.
first=$(ls "$D"/sub1/*.dat | head -1); second=$(ls "$D"/sub1/*.dat | sed -n 2p)
printf 'x' >> "$first"
rm -f "$second"
for e in sfv md5 xxh3; do check "damaged all.$e" "$D/all.$e"; done
for f in "$D"/*.sfv "$D"/*.md5; do case "$f" in */all.*) ;; *) check "damaged corpus $(basename "$f")" "$f";; esac; done

echo "------------------------------------------------"
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
