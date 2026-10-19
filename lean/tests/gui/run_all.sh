#!/usr/bin/env bash
# Part of the SharpSFV C++ port (GPL-3.0). Test tooling, added 2026-10-04.
#
# Side-by-side GUI test of the lean SharpSFV.exe against the standard build's, under Wine.
#   lean/tests/gui/run_all.sh <lean SharpSFV.exe> <standard build folder (SharpSFV.exe + SharpSFV-cli.exe)>
# Needs: wine, Xvfb, xdotool, xclip, ImageMagick. Prints the differing pixels per screenshot
# (0 = identical; timing values such as "2 ms" or a blinking caret make small differences) and
# compares every file both builds wrote.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
export GT="${GT:-/tmp/sfv-guitest}" DISPLAY=:99 WINEDEBUG=-all
rm -rf "$GT"; mkdir -p "$GT/lean" "$GT/std" "$GT/large" "$GT/ren" "$GT/bad"
cp "$1" "$GT/lean/"; cp "$2/SharpSFV.exe" "$2/SharpSFV-cli.exe" "$GT/std/"
python3 "$HERE/../../../scripts/make_corpus.py" "$GT/corpus" >/dev/null
for i in 1 2; do head -c 400000000 /dev/urandom > "$GT/large/big$i.bin"; done
pgrep -x Xvfb >/dev/null || { Xvfb :99 -screen 0 1280x900x24 >/dev/null 2>&1 & sleep 1; }
A="$HERE/act.sh"; D='@WGT@\corpus\data'
APPDATA_DIR="$(winepath -u "$(wine cmd /c echo %LOCALAPPDATA% 2>/dev/null | tr -d '\r')" 2>/dev/null)/SharpSFV"
same() { if cmp -s "$1" "$2"; then echo "files identical: $(basename "$1")"; else echo "FILES DIFFER: $1 $2"; fi; }

PANELS='[SharpSFV]\r\nDataStorageConsent=Declined\r\nShowFilterPanel=1\r\nShowAdvancedBar=1\r\nProcessingMode=SSD\r\nIncludePattern=*.bin;*.txt\r\n'
printf 'sleep 3\nshot done\n' > "$GT/s_done.txt"
$A create - "$GT/s_done.txt" "$D"
$A verify_sfv - "$GT/s_done.txt" "$D\\mixed_order.sfv"
$A verify_md5 - "$GT/s_done.txt" "$D\\mixed.md5"
$A menus - "$HERE/menus.txt"
$A panels "$PANELS" "$HERE/panels.txt" "$D"
$A inter "$PANELS" "$HERE/interact.txt" "$D"
same "$GT/saved_lean.xxh3" "$GT/saved_std.xxh3"; same "$GT/ini_after_lean.ini" "$GT/ini_after_std.ini"
$A jobs - "$HERE/jobs.txt"
for f in "$GT"/jobs_lean/*; do same "$f" "$GT/jobs_std/$(basename "$f")"; done
$A createmode - "$HERE/create.txt" -create "$D\\sub1"
same "$GT/cm_lean.xxh3" "$GT/cm_std.xxh3"; same "$GT/cmini_lean.ini" "$GT/cmini_std.ini"
rm -rf "$APPDATA_DIR"
$A drives '[SharpSFV]\r\nDataStorageConsent=Allowed\r\n' "$HERE/drives.txt" "$D\\sub1"
$A pause '[SharpSFV]\r\nDataStorageConsent=Declined\r\nDefaultAlgo=SHA256\r\nProcessingMode=HDD\r\nShowThroughputStats=1\r\n' "$HERE/pause.txt"
$A view '[SharpSFV]\r\nDataStorageConsent=Declined\r\nProcessingMode=SSD\r\nDefaultAlgo=Crc32\r\n' "$HERE/view.txt" "$D\\sub1"
same "$GT/clip_lean.txt" "$GT/clip_std.txt"; same "$GT/clipall_lean.txt" "$GT/clipall_std.txt"
cp "$GT"/corpus/data/sub1/f*.dat "$GT/ren/"
python3 - "$GT/ren" <<'PY'
import sys, zlib
d = sys.argv[1]; v = '%08X' % (zlib.crc32(open(d + '/f0.dat', 'rb').read()) & 0xffffffff)
open(d + '/rev.sfv', 'w', newline='').write('; reversed order\r\nf0.dat %s\r\nf1.dat 00000000\r\n' % (v[6:8] + v[4:6] + v[2:4] + v[0:2]))
PY
$A ren - "$HERE/ren.txt" '@WGT@\ren\rev.sfv'
same "$GT/renls_lean.txt" "$GT/renls_std.txt"
cp "$GT"/corpus/data/sub1/f*.dat "$GT/bad/"; cp "$GT/bad/f0.dat" "$GT/bad/dup.dat"
printf 'f0.dat 00000000\r\nf1.dat 11111111\r\n' > "$GT/bad/bad.sfv"
$A bad - "$HERE/bad.txt" '@WGT@\bad\bad.sfv'
same "$GT/delbad_lean.bat" "$GT/delbad_std.bat"; same "$GT/badls_lean.txt" "$GT/badls_std.txt"
$A reg - "$HERE/reg.txt"
sed -i 's/\\lean\\/\\X\\/g' "$GT/reg_lean.txt"; sed -i 's/\\std\\/\\X\\/g' "$GT/reg_std.txt"; same "$GT/reg_lean.txt" "$GT/reg_std.txt"
wine reg add 'HKCU\Control Panel\Desktop' /v LogPixels /t REG_DWORD /d 144 /f >/dev/null 2>&1; wineserver -k
$A dpi144 '[SharpSFV]\r\nDataStorageConsent=Declined\r\nShowFilterPanel=1\r\nShowAdvancedBar=1\r\n' "$HERE/dpi.txt" "$D\\sub1"
wine reg add 'HKCU\Control Panel\Desktop' /v LogPixels /t REG_DWORD /d 96 /f >/dev/null 2>&1; wineserver -k
