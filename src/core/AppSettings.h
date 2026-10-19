// SharpSFV (C++ port) - persistent settings (SharpSFV.ini next to the executable).
// Ported from AppSettings.cs. Same keys, same value format, same tolerant parsing, so an
// existing SharpSFV.ini keeps working with the C++ build and vice versa.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-04: CreateIfMissing (the CLI writes a default SharpSFV.ini).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Types.h"

#include <string>
#include <utility>
#include <vector>

namespace sfv {

struct WindowRect {
    int x = 0, y = 0, width = 800, height = 600;
    bool hasPosition = true; // false: WindowPosX/Y are not written (window opens centred)
};

class AppSettings {
public:
    // iniDirectory: folder holding SharpSFV.ini (the executable's folder in the app).
    explicit AppSettings(const std::string& iniDirectory);

    // --- UI preferences
    bool showTimeTab = false;
    bool showFilterPanel = false;
    bool showThroughputStats = false;
    bool showAdvancedBar = false;
    bool showFullPaths = false;
    bool showHashCol = true;
    bool showExpectedHashCol = true;
    bool lockColumns = true;

    // --- Operation defaults
    PathStorageMode pathStorageMode = PathStorageMode::Relative;
    ProcessingMode processingMode = ProcessingMode::Auto;
    bool enableChecksumComments = false;
    std::string pathPrefix;
    std::string includePattern;
    std::string excludePattern;
    bool scanRecursive = true;
    std::string customSignature = "L33T";
    HashType defaultAlgo = HashType::XXHASH3;
    // Byte order for CRC-32 values written to new .sfv files (verification accepts both).
    // Standard = QuickSFV/RapidCRC/7-Zip compatible; Reversed = what the C# SharpSFV wrote.
    Crc32ByteOrder crc32ByteOrder = Crc32ByteOrder::Standard;

    // Permission to store data in %LOCALAPPDATA%\SharpSFV (see DataStorage).
    DataConsent dataStorageConsent = DataConsent::NotAsked;

    // --- Window geometry
    int windowWidth = 800, windowHeight = 600;
    int windowX = 0, windowY = 0;
    bool hasCustomLocation = false;
    int splitterDistance = -1;

    // Column tag -> DisplayIndex ("Name:0,Hash:1,Status:2"). Insertion order preserved.
    std::vector<std::pair<std::string, int>> columnOrder;

    const std::string& IniPath() const { return iniPath_; }

    // Parses the INI file; silently ignores anything it cannot read (defaults stay).
    void Load();

    // What the C# Save(Form, bool, HashType, int, ListView) took from the UI.
    struct SaveState {
        bool timeTabEnabled = false;
        HashType currentAlgo = HashType::XXHASH3;
        int splitterDistance = -1;
        WindowRect bounds;
        std::vector<std::pair<std::string, int>> columnOrder; // empty -> "ColumnOrder="
    };
    bool Save(const SaveState& state) const;

    // SaveState built from the values currently held (what Load read, plus any changes):
    // lets non-GUI code (the CLI) update one setting without disturbing the others.
    SaveState CurrentState() const;
    bool SaveCurrent() const { return Save(CurrentState()); }
    // Writes SharpSFV.ini with the current values (the defaults, when nothing was read) if
    // the file does not exist yet; an existing file is never touched. True if it was created.
    bool CreateIfMissing() const;

    // Deletes the INI file.
    void ResetToDefaults() const;

    // Serialized INI text for the given state (exposed for tests).
    std::string Serialize(const SaveState& state) const;
    void ParseText(const std::string& content);

private:
    void ParseColumnOrder(const std::string& value);
    std::string iniPath_;
};

} // namespace sfv
