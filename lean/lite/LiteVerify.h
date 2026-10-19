// SharpSFV Lite - verify-only checker for installers (Inno Setup 7.1+, post-install).
// Implements the "SharpSFV Lite: Inno Setup verification tool spec" (2026-10-06):
//   SharpSFV-Lite.exe <checksum list> [-base <folder>] [-status <file>] [-log <file>]
//                     [-control <name>] [-threads <1-12>] [-mode auto|hdd|ssd] [-algo crc32|md5|xxh3]
// The installer draws the UI: the tool keeps a status file current (every 200 ms), writes a
// log line per finished file, and takes Pause / Stop through two named events.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-06: added (SharpSFV Lite, built from the lean command-line code).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
//
// The run loop is separate from core/Engine (which the full programs share) but uses the same
// checksum-list parser, hashers and digest comparison, so the verdicts are the same.
#pragma once

#include "../base/Str.h"
#include "../base/Vec.h"
#include "../core/Types.h"

namespace sfv::lite {

inline constexpr int kMaxThreads = 12;

enum ExitCode : int { kExitOk = 0, kExitProblems = 1, kExitFailed = 2, kExitStopped = 3 };

enum class DriveMode { Auto, Hdd, Ssd };

struct Options {
    Str list;       // the checksum list (required)
    Str base;       // folder the list's paths are relative to; "" = the list's folder
    Str statusPath; // "" = console mode (readable output instead of a status file)
    Str logPath;    // "" = no log
    Str control;    // base name of the Pause / Stop events; "" = none
    int threads = 0; // 0 = default (SSD: processor count, at most 12)
    DriveMode mode = DriveMode::Auto;
    bool hasAlgo = false; // -algo given (used only when the extension does not name one)
    HashType algo = HashType::Crc32;
};

// False (with a message) for an unknown option or a bad value. Whatever was parsed before
// the error stays in `out` (so a FAILED status can still be written to -status).
bool ParseArgs(const Vec<Str>& args, Options& out, Str& error);
Str UsageText();

// Where Pause and Stop come from (named events on Windows; tests set flags directly).
class Control {
public:
    virtual ~Control() = default;
    virtual void Poll(bool& pause, bool& stop) = 0; // current requests (called every ~50 ms)
    virtual bool StartedOk() const { return true; } // false: the events could not be created
};

class NoControl final : public Control {
public:
    void Poll(bool& pause, bool& stop) override { pause = false, stop = false; }
};

// Stops the run when the process that started it has gone (Windows: Setup).
class ParentWatch {
public:
    virtual ~ParentWatch() = default;
    virtual bool ParentGone() = 0;
};

class NoParentWatch final : public ParentWatch {
public:
    bool ParentGone() override { return false; }
};

// Test seam: drive detection (true = HDD).
struct Environment {
    bool (*detectHdd)(StrView folder) = nullptr; // nullptr = the real detection
};

// A FAILED status file for errors found before a run starts (bad arguments).
void WriteFailedStatus(StrView statusPath, StrView error);

// Runs one verification and returns the exit code (ExitCode).
int Run(const Options& opt, Control& control, ParentWatch& parent, const Environment& env = {});

// Helpers shared with the tests.
Str GroupThousands(i64 v);      // 20120 -> "20,120"
Str FormatHms(i64 seconds);     // 192 -> "00:03:12" (hours keep counting past 99)

} // namespace sfv::lite
