#!/usr/bin/env bash
# Part of the SharpSFV C++ port (GPL-3.0). Test tooling.
# Compares the C++ port against the C# reference oracle (tools/oracle), case by case.
#   scripts/compare_with_oracle.sh <probe binary> <oracle command...>
# Example (Linux):
#   scripts/compare_with_oracle.sh build-linux/sharpsfv_probe dotnet tools/oracle/bin/Release/net8.0/oracle.dll
set -u
PROBE="$1"; shift
ORACLE=("$@")
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
python3 "$HERE/make_corpus.py" "$WORK/corpus" >/dev/null
C="$WORK/corpus"; D="$C/data"
pass=0; fail=0

compare() { # name, then the shared argument list
    local name="$1"; shift
    "${ORACLE[@]}" "$@" >"$WORK/o.txt" 2>&1
    "$PROBE" "$@" >"$WORK/p.txt" 2>&1
    if cmp -s "$WORK/o.txt" "$WORK/p.txt"; then
        pass=$((pass + 1)); printf 'PASS  %-34s (%s lines)\n' "$name" "$(wc -l <"$WORK/o.txt")"
    else
        fail=$((fail + 1)); printf 'FAIL  %s\n' "$name"; diff "$WORK/o.txt" "$WORK/p.txt" | head -20
    fi
}

compare_stdin() { # name, stdin file, args
    local name="$1" input="$2"; shift 2
    "${ORACLE[@]}" "$@" <"$input" >"$WORK/o.txt" 2>&1
    "$PROBE" "$@" <"$input" >"$WORK/p.txt" 2>&1
    if cmp -s "$WORK/o.txt" "$WORK/p.txt"; then
        pass=$((pass + 1)); printf 'PASS  %-34s (%s lines)\n' "$name" "$(wc -l <"$WORK/o.txt")"
    else
        fail=$((fail + 1)); printf 'FAIL  %s\n' "$name"; diff "$WORK/o.txt" "$WORK/p.txt" | head -20
    fi
}

compare_create() { # name, out file name, then create args after <out>
    local name="$1" out="$2"; shift 2
    local dir; dir="$(dirname "$out")"
    "${ORACLE[@]}" create "$out" "$@" >/dev/null 2>&1; mv "$out" "$WORK/o.txt"
    "$PROBE" create "$out" "$@" >/dev/null 2>&1; mv "$out" "$WORK/p.txt"
    if cmp -s "$WORK/o.txt" "$WORK/p.txt" && [ -s "$WORK/o.txt" ]; then
        pass=$((pass + 1)); printf 'PASS  %-34s (%s lines, byte-identical)\n' "$name" "$(wc -l <"$WORK/o.txt")"
    else
        fail=$((fail + 1)); printf 'FAIL  %s\n' "$name"; diff "$WORK/o.txt" "$WORK/p.txt" | head -20
    fi
    # Keep the oracle's file and verify it with both implementations.
    cp "$WORK/o.txt" "$out"
    compare "  verify $(basename "$out")" verify "$out" XXHASH3
    rm -f "$out"
}

mapfile -t FILES < <(find "$D" -type f | LC_ALL=C sort)
for a in XXHASH3 Crc32 MD5 SHA1 SHA256; do compare "hash $a (${#FILES[@]} files)" hash "$a" "${FILES[@]}"; done

for f in standard.sfv mixed.md5 utf16.sha1 plain.sha256 list.txt; do
    for a in Crc32 MD5; do compare "parse $f as $a" parse "$a" "$D/$f"; done
done
for a in Crc32 MD5; do compare "parse stress.txt as $a" parse "$a" "$C/checksums/stress.txt"; done
compare_stdin "mask/include/exclude matrix" "$C/checksums/masks.tsv" mask
for f in default.ini full.ini weird.ini; do compare "ini $f" ini "$C/checksums/$f"; done

#                 name                     out                         algo    base     rec adv include          exclude        prefix        cmt abs paths...
compare_create "create xxh3 recursive"     "$D/checksums.xxh3"         XXHASH3 "$D"     1   0   ""               ""             ""            0   0   "$D"
compare_create "create md5 saved in parent" "$C/parent.md5"            MD5     "$D"     1   0   ""               ""             ""            0   0   "$D"
compare_create "create sha1 saved in subdir" "$D/sub1/inside.sha1"     SHA1    "$D"     1   0   ""               ""             ""            0   0   "$D"
compare_create "create filters+prefix+cmt" "$D/filtered.sfv"           Crc32   "$D"     0   1   "*.bin;*.dat,*.txt" "size_00000*;*.TXT" "incoming/iso" 1 0  "$D"
compare_create "create absolute crc32"     "$D/abs.sfv"                Crc32   "$D"     1   0   ""               ""             ""            0   1   "$D"
compare_create "create multi-input+dups"   "$D/multi.sha256"           SHA256  "$D"     1   0   ""               ""             ""            0   0   "$D/sub1" "$D/UPPER.TXT" "$D/sub1/f0.dat" "$D/zz_last"
compare_create "create non-recursive"      "$D/top.md5"                MD5     "$D"     0   1   ""               ""             ""            0   0   "$D"

for f in standard.sfv mixed_order.sfv mixed.md5 utf16.sha1 plain.sha256; do compare "verify $f" verify "$D/$f" XXHASH3; done
compare "verify list.txt (fallback MD5)" verify "$D/list.txt" MD5
compare "verify list.txt (fallback SHA1)" verify "$D/list.txt" SHA1

# ---------------------------------------------------------------- CRC-32 byte order, both directions
check() { # name, expected exit (0/1), command...
    local name="$1" want="$2"; shift 2
    "$@" >"$WORK/c.txt" 2>&1; local got=$?
    if [ "$got" -eq "$want" ]; then
        pass=$((pass + 1)); printf 'PASS  %-34s (%s)\n' "$name" "$(tail -1 "$WORK/c.txt")"
    else
        fail=$((fail + 1)); printf 'FAIL  %s (exit %s, wanted %s)\n' "$name" "$got" "$want"; tail -5 "$WORK/c.txt"
    fi
}
ARGS=(Crc32 "$D" 1 0 "" "" "" 0 0 "$D")
"$PROBE" create-standard "$C/port_standard.sfv" "${ARGS[@]}" >/dev/null 2>&1
"$PROBE" create "$C/port_reversed.sfv" "${ARGS[@]}" >/dev/null 2>&1
"${ORACLE[@]}" create "$C/csharp.sfv" "${ARGS[@]}" >/dev/null 2>&1
check "zlib accepts port standard .sfv"   0 python3 "$HERE/zlib_sfv_check.py" "$C/port_standard.sfv"
check "zlib rejects reversed .sfv"        1 python3 "$HERE/zlib_sfv_check.py" "$C/port_reversed.sfv"
check "zlib rejects C# SharpSFV .sfv"     1 python3 "$HERE/zlib_sfv_check.py" "$C/csharp.sfv"
compare "C# verifies port standard .sfv" verify "$C/port_standard.sfv" XXHASH3
compare "C# verifies port reversed .sfv" verify "$C/port_reversed.sfv" XXHASH3
compare "port verifies C# .sfv"          verify "$C/csharp.sfv" XXHASH3
"${ORACLE[@]}" verify "$C/port_standard.sfv" XXHASH3 | tail -1 | grep -q "BAD=0 MISSING=0 LEGACY=True" \
    && { pass=$((pass + 1)); echo "PASS  C# reports port standard .sfv all OK (via its endian fallback)"; } \
    || { fail=$((fail + 1)); echo "FAIL  C# did not accept port standard .sfv"; }
rm -f "$C"/port_*.sfv "$C/csharp.sfv"

echo "------------------------------------------------"
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
