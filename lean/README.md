# SharpSFV lean rewrite: Phase 4

Copyright (C) 2025 L33T (original C# SharpSFV).

* Modified 2026-10-03/04: this rewrite, part of the C++ port.
* Modified 2026-10-04: Phase 2 (parallel hashing, drive detection, drive commands, default
  `SharpSFV.ini`).
* Modified 2026-10-04: Phase 3 (the window, `SharpSFV.exe`).
* Modified 2026-10-04: Phase 4 (size: Clang + LLD with link-time optimization and identical
  code folding; faster CRC-32; smaller icon; sizes in compressed archives).
* Modified 2026-10-06: SharpSFV Lite (`SharpSFV-Lite.exe`), a verify-only tool for installers.

Licensed under the GNU GPL v3 (`../LICENSE.txt`), like the original.

This folder is a rewrite of the port that does not use the C++ standard library. On Windows it
does not use the C runtime's start-up code either. The goal is much smaller programs. The
standard port in `../src` is unchanged and still the reference.

Both programs are now complete:

* **`SharpSFV.exe`, the window:** everything the standard build's window does. Its
  `-headless` mode runs the command-line code in the same process, as the C# original did, so
  this one file covers every use.
* **`SharpSFV-cli.exe`, the console program:** everything the standard `SharpSFV-cli.exe` does.

The window was finished in Phase 3. Phase 4 is the final size pass. Built with Clang and the LLD
linker, the same source makes the window about 11% smaller and the console program about 15%
smaller (see [Size](#size)). CRC-32 is also more than twice as fast (see [Speed](#speed)). What is left is the switch-over: once
you have used it on Windows, the lean build can replace the standard one.

## SharpSFV Lite, for installers

`SharpSFV-Lite.exe` (58,880 bytes) is a verify-only tool that an Inno Setup installer runs after
installation. It uses the same parser and hashers as the full programs.

* It reads a checksum list (.sfv, .md5 or .xxh3).
* It keeps a status file current for the installer's own progress page.
* It writes a log, and takes Pause and Stop through named events.

See `lite/README.md`.

## What is included

* `base/`: the building blocks that replace the standard library:
  * strings (`Str`, `StrView`; UTF-8 inside, UTF-16 only at Windows calls);
  * a growable array (`Vec`), an optional value (`Opt`), a string set (`StrSet`) and one shared
    stable sort (`Sort`);
  * threads, locks and condition variables (`Thread`);
  * text helpers, paths, files and folders, console output, Ctrl+C handling and the command line.
* `core/`: everything that is not a window:
  * hashing (CRC-32, xxHash3, MD5, SHA-1, SHA-256);
  * filters, the file list and `SharpSFV.ini` reading and writing;
  * reading and writing checksum files (Save As, Explorer "Create Checksum", job queue);
  * the hashing/verification engine, with pause and the job-queue runner;
  * drive detection (`Drive`), DriveType.ini with the consent to store it (`DriveStore`);
  * the window's list logic: sorting, filters, duplicates (`ListModel`), the job list
    (`JobStore`) and the batch scripts and PATH editing (`Scripts`);
  * the command line (`Cli`).
* `gui/`: the window, ported file by file from `../src/gui`:
  * the main window: lifecycle and layout, menus, the virtual file list, running the engine,
    and the user's commands;
  * the dialogs (rename, disk data, About, drive classification);
  * Explorer integration (context menu, "Create Checksum" with several items selected, taskbar
    progress, "Install for Command Line").
* `app/CliMain.cpp` and `gui/GuiMain.cpp`: the two programs' entry points.
* `tests/`: five kinds of test:
  * unit tests;
  * a probe that compares results with the C# code and with the standard build;
  * a generator of random inputs for that probe;
  * a script that runs both console programs on the same files and compares the results
    (`diff_cli.sh`);
  * a script that drives both windows through the same actions under Wine and compares
    screenshots and written files (`gui/run_all.sh`).

## Phase 3: the window

`SharpSFV.exe` is a port of the standard build's window onto the lean building blocks, kept
line for line where possible:

* Every menu, panel, column, color, dialog and message is the same.
* Drag and drop, the job queue, Explorer's "Create Checksum" mini window, pause/stop and the
  Active Jobs panel all work as before, as do the bad-file and duplicate tools.
* `SharpSFV.exe -headless ...` gives the same output, files and exit codes as
  `SharpSFV-cli.exe ...`. (The standard build's `-headless` starts `SharpSFV-cli.exe`, which
  must be next to it; the lean one needs nothing else.)

Two small differences in the code, neither visible:

* The standard file list had a "summary row" that nothing ever created; it is left out.
* Progress messages from the engine to the window are merged, so at most one is waiting at any
  time, instead of being dropped while the window is busy. The window always shows the latest
  numbers either way.

## Phase 2: parallel hashing and drives

* **Parallel hashing on SSDs.** This works as it does in the standard build and the C# original:
  * One thread lists the files. Worker threads hash them.
  * **SSD:** one worker per logical processor, each reading 512 KB at a time, so many files are
    read at once.
  * **HDD:** one worker reading 8 MB at a time, so the disk heads do not jump between files.
  * The output files are identical either way.
  * Phase 1 always used the HDD strategy, so 42.5 GB on an NVMe drive took 94 s against the C#
    version's 31 s. With Phase 2 it took 32 s.
* **Drive detection.** In `-mode auto` (the default), the program asks Windows whether the
  drive has a "seek penalty". NVMe and SATA SSDs say no, so they get the SSD strategy. Each run
  prints its choice on the `Mode:` line, for example
  `Mode: SSD (Parallel) - detected now: volume reports no seek penalty [D:\]`.
* **The full set of options and commands:**
  * `-mode auto|hdd|ssd`, and `ProcessingMode=` in `SharpSFV.ini`;
  * `-datastorage`, `-drives`, `-drive-status` and `-drive-set`, with the same per-drive record
    (`DriveType.ini`) and the same consent rules as the standard build.
* **`SharpSFV.ini` is created with the default settings** when it is missing, on any run of the
  console program. An existing file is never rewritten by an ordinary run. The standard build's
  console program now does the same.

Exit codes are unchanged: 0 all good, 1 mismatches or missing files, 2 error, 3 cancelled.

## A deliberate change from the plan

The plan said to keep text as UTF-16 internally. The rewrite uses UTF-8 internally instead, as
the standard port does:

* each function could be ported line for line from the standard port, which keeps the risk of
  new bugs low;
* text is converted to UTF-16 only when calling Windows, so file names in any language work as
  before.

## Size

| Program | Size |
| :--- | ---: |
| **Lean `SharpSFV.exe`** (window, with `-headless` built in) | **231,936 bytes (227 KB)** |
| **Lean `SharpSFV-cli.exe`** | **106,496 bytes (104 KB)** |
| Lean, Phase 3 (MinGW GCC): window / console | 264,704 / 124,928 bytes |
| Standard `SharpSFV.exe` (window; `-headless` needs `SharpSFV-cli.exe`) | 387,584 bytes (379 KB) |
| Standard `SharpSFV-cli.exe` | 255,488 bytes (250 KB) |
| C# SharpSFV | about 393 KB, plus the .NET runtime |

Both programs link only DLLs that come with Windows.

* **`SharpSFV-cli.exe`:** `kernel32.dll`, `bcrypt.dll` (MD5 and SHA through Windows CNG) and
  `msvcrt.dll`. Some DLLs are loaded only when needed:
  * `advapi32.dll`, to read a drive's model name while probing it;
  * `shell32.dll`, to find the AppData folder once local data storage is allowed;
  * `user32.dll`, for the `-datastorage ask` message box.
* **`SharpSFV.exe`:** the same, plus the usual window DLLs: `user32`, `gdi32`, `comctl32`,
  `comdlg32`, `shell32`, `ole32` and `advapi32`.

### What Phase 4 changed

The sizes are those of the default build with the Clang toolchain file
(`cmake/clang-mingw-x86_64.cmake`). It produces the same kind of program as before: freestanding,
`-Oz`, the same DLLs, no global constructors.

* **The LLD linker instead of GNU ld.** GNU ld, as MinGW sets it up, keeps every function that
  has an x64 unwind entry, which is every function, so `--gc-sections` removed nothing. LLD
  drops unused functions together with their unwind entries.
* **Identical code folding** (`-Wl,--icf=all`): functions that compiled to the same machine code
  are stored once. With templates such as `Vec<T>`, there are many.
* **Link-time optimization** (`-flto`): the whole program is optimized at once. This allows
  inlining and removing code across files.
* **Joined strings are built in one step.** `a + b + c` used to make a temporary string at each
  `+`. Now it collects the pieces and allocates once (`StrCat` in `base/Str.h`). This makes the
  calling code smaller as well as faster.
* **The hashing entry points stay out of line.** Without this, link-time optimization inlined
  the hashing loops into code compiled for size, and they lost speed.
* **A smaller icon, with the same pixels.** `scripts/make_icon.py` now writes each PNG without
  row filters and compresses it with zopfli (zlib level 9 if zopfli is not installed). The icon
  went from 11,975 to 7,385 bytes. The standard build uses the same icon, so it shrinks too.

What each step is worth (bytes, window / console):

| Build | `SharpSFV.exe` | `SharpSFV-cli.exe` |
| :--- | ---: | ---: |
| MinGW GCC + GNU ld (Phase 3 set-up) | 265,728 | 124,416 |
| Clang + LLD, no link-time optimization | 247,296 | 112,640 |
| Clang + LLD + link-time optimization | 236,544 | 106,496 |
| **The same, with the smaller icon (default)** | **231,936** | **106,496** |

Options, all off by default because each costs something:

| Option | `SharpSFV.exe` | `SharpSFV-cli.exe` | Cost |
| :--- | ---: | ---: | :--- |
| `-DSFV_LEAN_NO_UNWIND=ON` (no x64 unwind tables) | 220,672 | 100,352 | Crash reports and debuggers lose the call stack |
| `-DSFV_HASH_OPT=-Oz` (hashing loops compiled for size too) | 225,792 | 100,352 | xxHash3 is about 25% slower in memory (6.7 → 5.1 GB/s). CRC-32 is unaffected |
| Both | 214,528 | 94,208 | Both of the above |
| `-DSFV_LEAN_OPT=-Os` (instead of `-Oz`) | 307,712 | 143,360 | Larger, raw and compressed |

Left out on purpose:

* **Executable packers (UPX and similar)** would roughly halve the files on disk. But the program
  is then unpacked into memory at every start, and antivirus programs often flag packed
  programs. You can still run UPX on the output yourself if you want that trade.
* **Removing the relocation table** would save under 1 KB. It would also turn off address
  randomization (ASLR).

### In a compressed archive

If the programs are shipped inside an archive, what counts is the compressed size. These were
measured with `xz -9e` and its x86 filter (BCJ), standing in for srep + lolz, which only run on
Windows. Other compressors give other absolute sizes, but builds compare the same way.

| Build | `SharpSFV.exe` | `SharpSFV-cli.exe` | Both, one solid archive |
| :--- | ---: | ---: | ---: |
| Phase 4 as first released | 101,748 | 44,380 | |
| **Default, with the smaller icon** | **97,068** | **44,368** | **117,256** |
| `SFV_LEAN_NO_UNWIND=ON` + `SFV_HASH_OPT=-Oz` | 91,620 | 40,296 | 110,320 |
| `-Os` instead of `-Oz` | 114,316 | 52,452 | |

What this shows:

* **The build applies no compression of its own, apart from the icon.** Its PNG images are
  deflate-compressed, which an archiver cannot compress further. Making them smaller saves the
  same number of bytes in the archive. Storing them uncompressed was tried too: it saves only
  about 600 more bytes compressed, and adds 270 KB to the program.
* **`-Oz` stays the best choice.** Its denser code also compresses smaller than `-Os` or `-O2`
  code.
* **Put both programs in one solid archive.** The window contains the console program's code,
  so the console program then adds about 20 KB instead of 44 KB. `SharpSFV.exe -headless` alone
  covers every command-line use. However, Windows' command prompt does not wait for a window
  program to finish, so scripts are better served by `SharpSFV-cli.exe`.
* **Tried and dropped:**
  * LLVM's machine outliner does not work with Windows x64 unwind information (LLD stops with
    an error).
  * Removing the relocation table or the version resource would each save under 1 KB.
    Removing relocations turns off ASLR, and the version resource is what Explorer's
    Properties dialog shows.

The GCC build still works and gives the same results. Its programs are 12-17% larger, and
link-time optimization stays off for it because GCC 13 for MinGW stops with an internal compiler
error under `-flto`.

## Speed

Measured under Wine on a 2-core machine, with 40 files of 50 MB each, already cached. With more
cores the SSD numbers improve further, up to the speed of the drive. Times include starting the
program.

| Algorithm | Mode | Standard CLI | Lean CLI |
| :--- | :--- | ---: | ---: |
| SHA-256 | HDD (one file at a time) | 8.40 s | **8.14 s** |
| SHA-256 | SSD (parallel) | 4.30 s | **4.16 s** |
| xxHash3 | HDD (one file at a time) | 0.58 s | **0.50 s** |
| xxHash3 | SSD (parallel) | 0.33 s | **0.25 s** |
| CRC-32 | HDD (one file at a time) | 1.14 s | **0.50 s** |
| CRC-32 | SSD (parallel) | 0.58 s | **0.23 s** |

**CRC-32** now uses the processor's carry-less multiplication instruction (PCLMULQDQ), which
every x86-64 processor since about 2010 has. It processes 64 bytes per step, using the method
from Intel's paper as zlib implements it. On processors without the instruction, it falls back to
the previous method (lookup tables, 16 bytes per step). The unit tests check that both methods
give the same result for every length up to 600 bytes, at different alignments.

This came out of Phase 4. The table method was about 25% slower when built with Clang than with
GCC, and the new method is fast with either compiler. CRC-32 is now limited by reading the data,
like xxHash3.

## How it was checked

| Check | Result |
| :--- | :--- |
| Unit tests | 8,471 pass on Linux and 8,493 on Windows code paths (under Wine, with both the Clang and the GCC build), also under AddressSanitizer. They include the window's logic: sorting, filters, duplicates, the job queue, scripts, the three file writers, pause and cancel |
| ThreadSanitizer (finds data races between threads) | No warnings, on the unit tests and on every run of the side-by-side console test |
| Comparison with the original C# code (oracle) | 49/49 identical |
| Random inputs compared with the standard build | Over 100,000 on Linux and 30,000 under Wine, all identical (Phase 4: a new 20,000 on Linux and 30,000 under Wine) |
| Both console programs on the same files (`tests/diff_cli.sh`, 94 cases) | 94/94 on Linux, with AddressSanitizer, with ThreadSanitizer and under Wine. Each case compares the files written, the exit code, the messages, the `SharpSFV.ini` left behind and `DriveType.ini` |
| `SharpSFV.exe -headless` against the standard `SharpSFV-cli.exe` (the same 94 cases) | 94/94 under Wine |
| Both windows driven through the same actions (`tests/gui/run_all.sh`, under Wine) | 37 screenshots: 33 pixel-identical. The other 4 differ only in values that change from run to run: elapsed times ("4 ms"), throughput, and the minute of a timestamp. The 12 files written along the way (Save As, Explorer "Create Checksum", job outputs, scripts, `SharpSFV.ini`, clipboard text, registry keys) are all identical |
| Phase 4 | Everything above was run again on the Clang build, and all results matched |
| Ctrl+C during parallel and sequential hashing | Exit code 3, prints "Cancelled.", no partial file |

The window scenarios cover:

* **Runs:** creating; verifying `.sfv` and `.md5` files (with comments, missing and bad
  entries, reversed CRC byte order); pause, resume and stop with the Active Jobs panel.
* **The list:** sorting, search, status and duplicate filters; context menu, remove and rename.
* **Commands:** compare with clipboard; copy entries; the bad-file tools.
* **Modes and dialogs:** the job queue; Explorer "Create Checksum" with a second instance
  forwarding its path; the Drive Classification and disk data dialogs; registering the Explorer
  context menu.
* **Settings and display:** the panels and view options, and a 144 DPI screen.

What I could not test here is real Windows: everything above ran under Wine. The standard build's
window has been used on Windows, and this one is a line-for-line port of it, including the fix for
the blank window. It still deserves your own check.

## Building

Windows, smallest programs: Clang with the LLD linker, from LLVM MinGW
(https://github.com/mstorsjo/llvm-mingw) or a Linux distribution's clang + lld + MinGW-w64:

```
cmake -S lean -B build-lean-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/clang-mingw-x86_64.cmake
cmake --build build-lean-win
```

Windows with MinGW-w64 GCC (natively, or cross-compiling from Linux; programs 12-17% larger):

```
cmake -S lean -B build-lean-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw-w64-x86_64.cmake
cmake --build build-lean-win
```

The toolchain path starts with `../` because CMake looks for a relative path next to the source
folder (`lean/`). Size options: `-DSFV_LEAN_LTO=ON|OFF` (default ON with Clang),
`-DSFV_LEAN_NO_UNWIND=ON`, `-DSFV_HASH_OPT=...` and `-DSFV_LEAN_OPT=...` (see [Size](#size)).
The smallest archive comes from:

```
cmake -S lean -B build-lean-min -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/clang-mingw-x86_64.cmake -DSFV_LEAN_NO_UNWIND=ON -DSFV_HASH_OPT=-Oz
cmake --build build-lean-min
```

This builds `SharpSFV.exe` and `SharpSFV-cli.exe`, plus `sfvlean_tests.exe` and
`sfvlean_probe.exe`. The tests and probe are ordinary C-runtime programs, so their size does not
matter.

Linux test build (the same engine code on POSIX, using OpenSSL for MD5/SHA and pthreads):

```
cmake -S lean -B build-lean -G Ninja [-DSFV_LEAN_SANITIZE=ON]
cmake --build build-lean && build-lean/sfvlean_tests
lean/tests/diff_cli.sh build-linux/SharpSFV-cli build-lean/sfvlean-cli
```

Window comparison under Wine (needs Xvfb, xdotool, xclip and ImageMagick):

```
lean/tests/gui/run_all.sh build-lean-win/SharpSFV.exe build-win
```

## How the program is built without the standard library

* **Entry point:** each program starts at its own entry point instead of the C runtime's:
  `mainCRTStartup` in `app/CliMain.cpp`, `WinMainCRTStartup` in `gui/GuiMain.cpp`.
* **Memory:** `new` and `delete` are written in `base/Base.cpp` on top of `HeapAlloc`.
* **Threads:** Windows' own `CreateThread`, `SRWLOCK` and `CONDITION_VARIABLE`, the same
  primitives the standard build wraps.
* **No exceptions, no RTTI, no global constructors:** errors are returned as values.
* **Fast hot loop:** `core/Hash.cpp` is compiled for speed (`-O2`). Everything else is compiled
  for size (`-Oz`). The CRC-32 code for PCLMULQDQ is compiled for that instruction only. The
  processor is checked once, at the first CRC-32.
