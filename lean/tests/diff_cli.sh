#!/usr/bin/env bash
# Part of the SharpSFV C++ port (GPL-3.0). Test tooling, added 2026-10-03.
# Modified 2026-10-04: drive modes, drive / data storage commands, SharpSFV.ini comparison.
#
# Differential test of the lean SharpSFV-cli against the standard build's SharpSFV-cli:
# both run the same command lines with the same SharpSFV.ini on the same generated corpus,
# and must produce byte-identical checksum files, the same exit code, the same output
# (ignoring the progress line, the elapsed time and the minute of a timestamp) and leave the
# same SharpSFV.ini behind (both create it with the defaults when it is missing).
#
#   lean/tests/diff_cli.sh <standard cli> <lean cli> [runner]      runner: e.g. "wine"
#   NEW_PREFIX=-headless lean/tests/diff_cli.sh ... <lean SharpSFV.exe> wine   (the GUI's -headless)
set -u
OLD="$1"; NEW="$2"; RUN="${3:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
python3 "$HERE/../../scripts/make_corpus.py" "$WORK/corpus" >/dev/null
C="$WORK/corpus"; D="$C/data"
mkdir -p "$WORK/old" "$WORK/new"
cp "$OLD" "$WORK/old/cli.exe"; cp "$NEW" "$WORK/new/cli.exe"
pass=0; fail=0
# Where %LOCALAPPDATA%\SharpSFV (DriveType.ini) lives; emptied before every run.
if [ -n "$RUN" ]; then
    DATA="$(winepath -u "$($RUN cmd /c echo %LOCALAPPDATA% 2>/dev/null | tr -d '\r')" 2>/dev/null)/SharpSFV"
else
    export XDG_DATA_HOME="$WORK/xdg"; DATA="$XDG_DATA_HOME/SharpSFV"
fi
PRE=() # arguments of a command run before the one compared (e.g. -drive-set), same side

normalize() { # stdout+stderr -> comparable text
    sed -e 's/\r$//' "$1" | tr '\r' '\n' | grep -av -E '^[0-9]+/[0-9]+ \([0-9.]+%\)|^Time: [0-9]+ ms$|^\s*$|^wine: ' |
        sed -E -e 's/[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}/<date>/g' -e 's#([/\\])(old|new)([/\\])SharpSFV\.ini#\1<side>\3SharpSFV.ini#g' || true
}

# case <name> <ini text> <output file or -> <args...>
case_() {
    local name="$1" ini="$2" out="$3"; shift 3
    local results=()
    for side in old new; do
        if [ "$ini" = NONE ]; then rm -f "$WORK/$side/SharpSFV.ini"; else printf '%b' "$ini" >"$WORK/$side/SharpSFV.ini"; fi
        [ "$out" != "-" ] && rm -f "$out"
        rm -rf "$DATA"
        prep=(); [ "$side" = new ] && [ -n "${NEW_PREFIX:-}" ] && prep=("$NEW_PREFIX")
        [ ${#PRE[@]} -gt 0 ] && (cd "$C" && $RUN "$WORK/$side/cli.exe" "${prep[@]}" "${PRE[@]}" >/dev/null 2>&1)
        pre=(); [ "$side" = new ] && [ -n "${NEW_PREFIX:-}" ] && pre=("$NEW_PREFIX")
        (cd "$C" && $RUN "$WORK/$side/cli.exe" "${pre[@]}" "$@" >"$WORK/$side.out" 2>&1); echo $? >"$WORK/$side.code"
        if [ -f "$DATA/DriveType.ini" ]; then sed -E 's/[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}/<date>/' "$DATA/DriveType.ini" >"$WORK/$side.drives"; else echo "<none>" >"$WORK/$side.drives"; fi
        if [ "$out" != "-" ]; then
            if [ -f "$out" ]; then mv "$out" "$WORK/$side.file"; else echo "<no file>" >"$WORK/$side.file"; fi
        fi
        normalize "$WORK/$side.out" >"$WORK/$side.norm"
    done
    local ok=1 why=""
    cmp -s "$WORK/old.code" "$WORK/new.code" || { ok=0; why="exit $(cat "$WORK/old.code") vs $(cat "$WORK/new.code")"; }
    [ "$NAME_ONLY_EXIT" = 1 ] || cmp -s "$WORK/old.norm" "$WORK/new.norm" || { ok=0; why="$why output"; }
    if [ "$out" != "-" ]; then cmp -s "$WORK/old.file" "$WORK/new.file" || { ok=0; why="$why file"; }; fi
    cmp -s "$WORK/old/SharpSFV.ini" "$WORK/new/SharpSFV.ini" || { ok=0; why="$why SharpSFV.ini"; }
    cmp -s "$WORK/old.drives" "$WORK/new.drives" || { ok=0; why="$why DriveType.ini"; }
    if [ $ok = 1 ]; then
        pass=$((pass + 1)); printf 'PASS  %-46s exit %s%s\n' "$name" "$(cat "$WORK/new.code")" \
            "$([ "$out" != "-" ] && echo ", $(wc -l <"$WORK/new.file") lines identical")"
    else
        fail=$((fail + 1)); printf 'FAIL  %s (%s)\n' "$name" "$why"
        diff "$WORK/old.norm" "$WORK/new.norm" | head -10
        [ "$out" != "-" ] && diff "$WORK/old.file" "$WORK/new.file" | head -10
        diff "$WORK/old/SharpSFV.ini" "$WORK/new/SharpSFV.ini" | head -6
        diff "$WORK/old.drives" "$WORK/new.drives" | head -6
    fi
}
NAME_ONLY_EXIT=0
DEF='[SharpSFV]\r\nDefaultAlgo=Crc32\r\n'

# Verification of every checksum file in the corpus.
for f in standard.sfv mixed_order.sfv mixed.md5 utf16.sha1 plain.sha256; do
    case_ "verify $f" "$DEF" - "$D/$f"
done
case_ "verify list.txt -algo md5" "$DEF" - "$D/list.txt" -algo md5
case_ "verify list.txt -algo sha1" "$DEF" - "$D/list.txt" -algo SHA-1
case_ "verify list.txt (DefaultAlgo)" "$DEF" - "$D/list.txt"
case_ "verify stress.txt -algo crc32" "$DEF" - "$C/checksums/stress.txt" -algo crc32
case_ "verify relative path" "$DEF" - data/standard.sfv

# Hash and report (no file written).
for a in xxh3 crc32 md5 sha1 sha256; do case_ "hash folder -algo $a" "$DEF" - "$D" -algo "$a"; done

# Creation.
for a in xxh3 crc32 md5 sha1 sha256; do case_ "create -algo $a" "$DEF" "$D/checksums.$( [ $a = crc32 ] && echo sfv || { [ $a = xxh3 ] && echo xxh3 || echo $a; } )" -create "$D" -algo "$a"; done
case_ "create -out parent .md5"              "$DEF" "$C/parent.md5" -create "$D" -out "$C/parent.md5"
case_ "create -out subdir .sha1"             "$DEF" "$D/sub1/inside.sha1" -create "$D" -out "$D/sub1/inside.sha1"
case_ "create -crc-order reversed"           "$DEF" "$C/rev.sfv" -create "$D" -out "$C/rev.sfv" -crc-order reversed
case_ "create -crc-order QuickSFV"           "$DEF" "$C/std.sfv" -create "$D" -out "$C/std.sfv" -crc-order QuickSFV
case_ "create -algo overrides -out ext"      "$DEF" "$C/x.sfv" -create "$D" -out "$C/x.sfv" -algo md5
case_ "create multi-input + duplicates"      "$DEF" "$C/multi.sha256" -create "$D/sub1" "$D/UPPER.TXT" "$D/sub1/f0.dat" "$D/zz_last" -out "$C/multi.sha256"
case_ "create single file"                   "$DEF" "$D/c.sfv" -create "$D/c.nfo" -out "$D/c.sfv"
case_ "create relative inputs"               "$DEF" "$C/relin.sfv" -create data/sub1 data/sub2 -out relin.sfv
case_ "-out without -create writes"          "$DEF" "$C/o.md5" "$D/sub2" -out "$C/o.md5"
case_ "create skips its own output"          "$DEF" "$D/checksums.md5" -create "$D" -algo md5

# Settings from SharpSFV.ini.
ADV='[SharpSFV]\r\nShowAdvancedBar=1\r\nIncludePattern=*.bin;*.dat,*.txt\r\nExcludePattern=size_00000*;*.TXT\r\nScanRecursive=0\r\nPathPrefix=incoming/iso\r\nEnableChecksumComments=1\r\nSignature=Tester\r\nDefaultAlgo=Crc32\r\n'
case_ "ini: filters, prefix, comments, non-recursive" "$ADV" "$D/f.sfv" -create "$D" -out "$D/f.sfv"
case_ "ini: filters apply to hash-only too"  "$ADV" - "$D"
case_ "ini: absolute paths"                  '[SharpSFV]\r\nPathStorageMode=Absolute\r\nDefaultAlgo=MD5\r\n' "$C/abs.md5" -create "$D" -out "$C/abs.md5"
case_ "ini: legacy UseAbsolutePaths"         '[SharpSFV]\r\nUseAbsolutePaths=1\r\n' "$C/abs2.sfv" -create "$D/sub2" -out "$C/abs2.sfv"
case_ "ini: Crc32ByteOrder=Reversed"         '[SharpSFV]\r\nCrc32ByteOrder=Reversed\r\nDefaultAlgo=1\r\n' "$D/checksums.sfv" -create "$D"
case_ "ini: DefaultAlgo=SHA256 by number"    '[SharpSFV]\r\nDefaultAlgo=4\r\n' "$D/checksums.sha256" -create "$D"
case_ "ini: ScanRecursive=0 needs -create"   '[SharpSFV]\r\nScanRecursive=0\r\nDefaultAlgo=MD5\r\n' - "$D"
case_ "ini: ScanRecursive=0 with -create"    '[SharpSFV]\r\nScanRecursive=0\r\nDefaultAlgo=MD5\r\n' "$D/checksums.md5" -create "$D"
case_ "ini: throughput stats on"             '[SharpSFV]\r\nShowThroughputStats=1\r\nDefaultAlgo=XXHASH3\r\n' - "$D"
case_ "ini: UTF-16 file with BOM"            '\xff\xfe[\x00S\x00h\x00a\x00r\x00p\x00S\x00F\x00V\x00]\x00\r\x00\n\x00D\x00e\x00f\x00a\x00u\x00l\x00t\x00A\x00l\x00g\x00o\x00=\x00M\x00D\x005\x00' "$D/checksums.md5" -create "$D"
case_ "no ini at all"                        '' "$D/checksums.xxh3" -create "$D"

# Errors and edge cases.
case_ "missing input"                        "$DEF" - "$D/does_not_exist"
case_ "one missing, one present"             "$DEF" "$C/m.sfv" -create "$D/nope" "$D/sub2" -out "$C/m.sfv"
case_ "unknown algorithm"                    "$DEF" - "$D" -algo whirlpool
case_ "unknown -crc-order"                   "$DEF" - "$D" -crc-order sideways
case_ "missing value for -out"               "$DEF" - "$D" -out
case_ "unknown option is ignored"            "$DEF" - "$D/sub2" -frobnicate
case_ "-headless is ignored"                 "$DEF" - -headless "$D/standard.sfv"
case_ "unwritable output"                    "$DEF" - -create "$D/sub2" -out "$D/no_such_dir/x.sfv"
case_ "no common folder needs -out"          "$DEF" - -create "$D/sub1/f0.dat" /etc/hostname
case_ "no arguments (usage text)"            "$DEF" -
case_ "-help"                                "$DEF" - -help

# SharpSFV.ini is created with the defaults when missing (and never rewritten otherwise).
case_ "no SharpSFV.ini: created by a hash run" NONE - "$D/sub2"
case_ "no SharpSFV.ini: created by -help"      NONE - -help
case_ "no SharpSFV.ini: created on an error"   NONE - "$D" -algo whirlpool
case_ "existing SharpSFV.ini is left alone"    '[SharpSFV]\r\nDefaultAlgo=MD5\r\n; my notes\r\n' - "$D/sub2"

# Drive modes: parallel (SSD) and sequential (HDD) give the same files.
for a in xxh3 crc32 md5 sha1 sha256; do
    case_ "create -mode ssd -algo $a" "$DEF" "$C/ssd.$a" -create "$D" -algo "$a" -mode ssd -out "$C/ssd.$a"
done
case_ "create -mode hdd"                     "$DEF" "$C/hdd.sha1" -create "$D" -mode hdd -out "$C/hdd.sha1"
case_ "create -mode auto"                    "$DEF" "$C/auto.md5" -create "$D" -mode auto -out "$C/auto.md5"
case_ "verify -mode ssd (mixed results)"     "$DEF" - "$D/mixed_order.sfv" -mode ssd
case_ "verify -mode ssd md5"                 "$DEF" - "$D/mixed.md5" -mode SSD
case_ "verify -mode hdd"                     "$DEF" - "$D/standard.sfv" -mode hdd
case_ "verify stress.txt -mode ssd"          "$DEF" - "$C/checksums/stress.txt" -algo crc32 -mode ssd
case_ "ini: ProcessingMode=SSD"              '[SharpSFV]\r\nProcessingMode=SSD\r\nDefaultAlgo=SHA1\r\n' "$D/checksums.sha1" -create "$D"
case_ "ini: ProcessingMode=2"                '[SharpSFV]\r\nProcessingMode=2\r\n' - "$D/sub1"
case_ "ini: ProcessingMode=HDD"              '[SharpSFV]\r\nProcessingMode=HDD\r\n' - "$D/sub1"
case_ "ini: legacy OptimizeForHDD=1"         '[SharpSFV]\r\nOptimizeForHDD=1\r\n' - "$D/sub1"
case_ "-mode overrides ProcessingMode"       '[SharpSFV]\r\nProcessingMode=HDD\r\n' - "$D/sub1" -mode ssd
case_ "unknown -mode"                        "$DEF" - "$D" -mode warp
case_ "-mode without value"                  "$DEF" - "$D" -mode

# Drive and local data storage commands.
ALLOW='[SharpSFV]\r\nDataStorageConsent=Allowed\r\n'
case_ "-datastorage (status, never asked)"   "$DEF" - -datastorage
case_ "-datastorage status (allowed)"        "$ALLOW" - -datastorage status
case_ "-datastorage allow"                   "$DEF" - -datastorage allow
case_ "-datastorage deny"                    "$ALLOW" - -datastorage deny
case_ "-datastorage DENY (any case)"        "$ALLOW" - -datastorage DENY
case_ "-drives (not allowed)"                "$DEF" - -drives
case_ "-drives (allowed, none yet)"          "$ALLOW" - -drives
case_ "-drive-status ."                      "$DEF" - -drive-status .
case_ "-drive-status (allowed) -mode hdd"    "$ALLOW" - -drive-status "$D" -mode hdd
case_ "-drive-set without consent"           "$DEF" - -drive-set "$D" ssd
case_ "-drive-set bad value"                 "$ALLOW" - -drive-set "$D" fast
case_ "-drive-set missing value"             "$ALLOW" - -drive-set "$D"
case_ "-drive-set ssd"                       "$ALLOW" - -drive-set "$D" ssd
case_ "-drive-set auto"                      "$ALLOW" - -drive-set "$D" auto
case_ "allowed: first run detects and saves" "$ALLOW" - "$D/sub1"
PRE=(-drive-set "$D" hdd)
case_ "-drives after -drive-set hdd"         "$ALLOW" - -drives
case_ "run uses the drive setting"           "$ALLOW" "$C/u.sfv" -create "$D" -out "$C/u.sfv" -algo crc32
case_ "-drive-status shows the setting"      "$ALLOW" - -drive-status "$D"
case_ "-datastorage deny deletes it"         "$ALLOW" - -datastorage deny
PRE=("$D/sub1")
case_ "second run uses the stored detection" "$ALLOW" - "$D/sub2"
PRE=()

echo "------------------------------------------------"
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
