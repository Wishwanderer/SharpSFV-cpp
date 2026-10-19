# SharpSFV Lite

Copyright (C) 2025 L33T (original C# SharpSFV).

* Modified 2026-10-06: added. SharpSFV Lite is built from the lean command-line code.

Licensed under the GNU GPL v3 (`../../LICENSE.txt`), like the original.

`SharpSFV-Lite.exe` is a 64-bit, verify-only console program. An Inno Setup 7.1+ installer
runs it after installation to check the installed files against one combined checksum list.

* The installer draws the UI.
* The tool reports through a status file and a log file.
* It takes Pause and Stop through two named events.

It follows the spec "SharpSFV Lite: Inno Setup verification tool spec" (2026-10-06). This file
summarises that spec and records how the tool was built and tested.

| | |
| :--- | :--- |
| Size | 58,880 bytes, about 26 KB compressed (the full CLI: 106,496 bytes, 44 KB compressed) |
| Imports | `kernel32.dll`, `bcrypt.dll` (MD5 through Windows CNG), `msvcrt.dll` |
| Algorithms | CRC-32 (`.sfv`), MD5 (`.md5`), xxHash3 (`.xxh3`) |
| Writes | Only the status file and the log file it is given |

## Command line

```
SharpSFV-Lite.exe <checksum list> [-base <folder>] [-status <file>] [-log <file>]
                  [-control <name>] [-threads <1-12>] [-mode auto|hdd|ssd] [-algo crc32|md5|xxh3]
```

| Option | Default | Meaning |
| :--- | :--- | :--- |
| `-base` | The list's folder | Folder the list's paths are relative to (pass `{app}`) |
| `-status` | None (console mode) | Status file, rewritten every 200 ms |
| `-log` | None | One line per finished file, plus a header and a summary |
| `-control` | None | Base name of the events `Local\<name>.pause` and `Local\<name>.stop` |
| `-threads` | SSD: processor count, at most 12 | Most files hashed at once; also the number of worker rows |
| `-mode` | `auto` | `auto` asks Windows whether the drive has a seek penalty. HDD always uses 1 worker |
| `-algo` | From the extension | Only for lists not named `.sfv`, `.md5` or `.xxh3` |

Without `-status`, it prints readable progress and the problem lines, for testing from a command
prompt (Ctrl+C stops it). With `-status`, it prints nothing.

## Status file

The status file is UTF-8 with a byte-order mark and Windows line endings. Each write replaces the
whole file in one step, so a reader never sees half a file.

```
SharpSFV-Lite status 1
state RUNNING
seq 1841
progress 437 18402 20120
counts 18399 1 1 1
bytes 8613847040 19712004096
time 74 96
worker 1 820 Data\textures.pak
worker 2 115 Data\audio\music bank.bank
worker 3 -
exit -
```

| Line | Values |
| :--- | :--- |
| `state` | `SCANNING`, `RUNNING`, `PAUSED`, `DONE`, `STOPPED` or `FAILED`. `PAUSED` appears once every worker has stopped |
| `seq` | Goes up on every write. If it stops changing while not finished, the tool has died |
| `progress` | Overall ‰ (by bytes), files finished, files in the list |
| `counts` | OK, BAD, MISSING, ERROR |
| `bytes` | Bytes hashed, total bytes of the files present |
| `time` | Elapsed seconds (pauses not counted), remaining seconds or `-` |
| `worker` | Row number, that file's ‰, path as written in the list. `-` = idle. The row count never changes during a run |
| `logerror` | Only if writing the log failed. Verification carries on |
| `error` | Only with `FAILED`: the reason |
| `exit` | The exit code once finished, else `-` |

## Pause, Stop and Setup exiting

* **The events:** both sides call `CreateEventW` on the same two manual-reset events, so it
  doesn't matter which side comes first. Setting `.pause` pauses; clearing it resumes. Setting
  `.stop` stops.
* **Pause:** each worker finishes the block it is reading (512 KB on SSD, 8 MB on HDD), then
  waits. Under Wine, `PAUSED` appeared about 120 ms after the event was set.
* **Stop:** files in progress are not counted. The log ends with
  `Stopped by user. N files not checked.`, and the exit code is 3.
* **Setup exiting:** if the process that started the tool exits, the tool stops in the same way
  and logs `Stopped because Setup exited.`

## Log

The log is UTF-8 with a byte-order mark. MISSING lines come first, from the size scan. The other
lines follow in the order files finish:

```
OK       Bin\game.exe
BAD      Data\core.pak   expected 5E6F7A8B...   actual 0F1E2D3C...
MISSING  Data\hd\tex07.pak
ERROR    Data\hd\tex09.pak   cannot open: The process cannot access the file because ...
```

BAD lines show both values in upper-case hex. For CRC-32, the actual value is always in standard
(QuickSFV) byte order. Either byte order in the list counts as OK, as in the full CLI.

## Exit codes

| Code | Meaning |
| :--- | :--- |
| 0 | Every file is OK |
| 1 | At least one file is BAD, MISSING or ERROR |
| 2 | Could not start: bad arguments, list unreadable or empty, status or log file not writable |
| 3 | Stopped (the Stop event, Setup exiting, or Ctrl+C in console mode) |

## Building

The tool is part of the lean build (see `../README.md`). With the Clang toolchain:

```
cmake -S lean -B build-lean-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/clang-mingw-x86_64.cmake
cmake --build build-lean-win --target SharpSFV-Lite sfvlite_tests sfvlite_host
```

* **With MinGW GCC** it builds too, at 87,552 bytes.
* **The smallest-archive options** (`-DSFV_LEAN_NO_UNWIND=ON -DSFV_HASH_OPT=-Oz`) give
  49,664 bytes, about 23 KB compressed.
* **The Linux test build** adds `sfvlite` and `sfvlite_tests`. There, `-control <name>` watches
  the files `$TMPDIR/<name>.pause` and `.stop` instead of events.

## Code

| File | What it does |
| :--- | :--- |
| `lite/LiteVerify.*` | Options, reading the list, the size scan, the workers, counters, timing, the status file, the log |
| `lite/LitePlatform.*` | Sleeping, local time, error messages, replacing a file in one step, the events, the parent process |
| `app/LiteMain.cpp` | The entry point, with no C runtime start-up code |
| `tests/LiteTests.cpp` | Unit tests |
| `tests/LiteHost.cpp` | A stand-in for the Inno script |
| `tests/diff_lite.sh` | Checks that verdicts match the full CLI |

The run loop is separate from the shared engine, so the full programs are unchanged. It uses the
same parser, hashers and comparison, so the verdicts are the same.

## How it was checked

| Check | Result |
| :--- | :--- |
| Unit tests (`sfvlite_tests`) | 227 pass on Linux, also under AddressSanitizer and ThreadSanitizer (no warnings). 236 pass under Wine, with both the Clang and GCC builds, adding the ERROR verdict (a locked file) and the named events |
| Same verdicts as the full CLI (`tests/diff_lite.sh`) | 12/12 lists on Linux and under Wine, each in HDD and 12-worker SSD mode |
| Stand-in for the Inno script (`sfvlite_host`, under Wine, 400 MB) | All scenarios pass: run to the end; Pause (bytes and elapsed time hold for 1 s, then resume); Stop (`STOPPED`, exit 3); Setup exiting (`STOPPED`, its own log line). Every run: `seq` only goes up, states only move forward, worker rows stay constant, and the exit line matches the exit code |
| Speed against the full CLI (2 GB, cached, Wine, 2 cores) | Same in SSD mode (CRC-32: 0.27 s against 0.27 s; MD5: 1.99 s against 1.93 s). About 50 ms more per run in HDD mode, from the 50 ms polling loop |

Under Wine, run the tests with `LANG=C.UTF-8`. With the POSIX locale, Wine cannot create files
with non-English names.

Still to confirm in a real Inno Setup script:

* that `LoadStringsFromFile` reads the UTF-8 status file correctly;
* that a 200 ms timer keeps a page with 12 worker rows smooth.
