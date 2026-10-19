#!/usr/bin/env bash
# Part of the SharpSFV C++ port (GPL-3.0). Test tooling, added 2026-10-04.
#
# Runs one interaction script against two builds of SharpSFV.exe ($GT/lean and $GT/std) in a
# 1280x820 Wine virtual desktop on Xvfb :99, and compares the screenshots pixel by pixel.
#   act.sh <name> <ini text|-> <script file> [program arguments...]
# Script lines: sleep N | key K... | type TEXT | click X Y | dclick X Y | rclick X Y |
#               shot TAG | sh COMMAND ($SIDE is lean or std)
# In scripts and arguments, @GT@ is the test folder and @WGT@ the same folder as a Windows path.
set -u
NAME="$1"; INI="$2"; SCRIPT="$3"; shift 3
export DISPLAY=:99 WINEDEBUG=-all LANG=C.UTF-8 LC_ALL=C.UTF-8
WGT=$(winepath -w "$GT" 2>/dev/null)
ARGS=()
for a in "$@"; do ARGS+=("${a//@WGT@/$WGT}"); done
mkdir -p "$GT/shots"
for side in lean std; do
  if [ "$INI" != "-" ]; then printf '%b' "$INI" > "$GT/$side/SharpSFV.ini"
  else printf '[SharpSFV]\r\nDataStorageConsent=Declined\r\n' > "$GT/$side/SharpSFV.ini"; fi
  (cd "$GT/$side" && wine explorer /desktop=sfv,1280x820 "$WGT\\$side\\SharpSFV.exe" "${ARGS[@]}" >/dev/null 2>&1 &)
  sleep 4
  DESK=$(xdotool search --name "sfv - Wine desktop" 2>/dev/null | head -1)
  while IFS= read -r raw; do
    line=${raw//@GT@/$GT}; line=${line//@WGT@/$WGT}
    set -- $line
    cmd="${1:-}"; shift || true
    case "$cmd" in
      sleep) sleep "$1" ;;
      key) xdotool windowfocus --sync "$DESK" 2>/dev/null; for k in "$@"; do xdotool key --delay 60 "$k"; sleep 0.3; done ;;
      type) xdotool windowfocus --sync "$DESK" 2>/dev/null; xdotool type --delay 40 "${line#type }"; sleep 0.3 ;;
      click) xdotool mousemove "$1" "$2" click 1; sleep 0.4 ;;
      dclick) xdotool mousemove "$1" "$2" click --repeat 2 1; sleep 0.4 ;;
      rclick) xdotool mousemove "$1" "$2" click 3; sleep 0.5 ;;
      sh) SIDE=$side bash -c "${line#sh }" ;;
      shot) import -display :99 -window root "$GT/shots/${NAME}_$1_$side.png" ;;
      *) ;;
    esac
  done < "$SCRIPT"
  wineserver -k 2>/dev/null; sleep 0.5
done
for f in "$GT"/shots/${NAME}_*_lean.png; do
  tag=${f#"$GT"/shots/${NAME}_}; tag=${tag%_lean.png}
  echo "$NAME/$tag: differing pixels = $(compare -metric AE "$f" "$GT/shots/${NAME}_${tag}_std.png" "$GT/shots/${NAME}_${tag}_diff.png" 2>&1)"
done
