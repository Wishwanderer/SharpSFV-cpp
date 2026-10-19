// SharpSFV (lean C++ rewrite) - SharpSFV.ini.
// Port of src/core/AppSettings.* (same keys, same values, same file layout).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: saving, and creating the file with defaults when it is missing.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "../base/Str.h"
#include "../base/Vec.h"
#include "Types.h"

namespace sfv {

struct ColumnOrderEntry {
    Str tag; // "Name", "Hash", ...
    int displayIndex = 0;
};

class AppSettings {
public:
    explicit AppSettings(StrView iniDirectory); // folder holding SharpSFV.ini

    // UI preferences
    bool showTimeTab = false;
    bool showFilterPanel = false;
    bool showThroughputStats = false;
    bool showAdvancedBar = false;
    bool showFullPaths = false;
    bool showHashCol = true;
    bool showExpectedHashCol = true;
    bool lockColumns = true;

    // Operation defaults
    PathStorageMode pathStorageMode = PathStorageMode::Relative;
    ProcessingMode processingMode = ProcessingMode::Auto;
    bool enableChecksumComments = false;
    Str pathPrefix;
    Str includePattern;
    Str excludePattern;
    bool scanRecursive = true;
    Str customSignature = "L33T";
    HashType defaultAlgo = HashType::XXHASH3;
    Crc32ByteOrder crc32ByteOrder = Crc32ByteOrder::Standard;
    DataConsent dataStorageConsent = DataConsent::NotAsked;

    // Window geometry
    int windowWidth = 800, windowHeight = 600;
    int windowX = 0, windowY = 0;
    bool hasCustomLocation = false;
    int splitterDistance = -1;
    Vec<ColumnOrderEntry> columnOrder;

    const Str& IniPath() const { return iniPath_; }
    void Load();                    // silently keeps defaults for anything unreadable
    void ParseText(StrView content);

    // Writes every setting as currently held (what Load read, plus any changes). The GUI's
    // window state is written back as it was read. False if the file could not be written.
    bool SaveCurrent() const;
    // Writes SharpSFV.ini with the current values (the defaults, when nothing was read) if
    // the file does not exist yet; an existing file is never touched. True if it was created.
    bool CreateIfMissing() const;
    Str Serialize() const; // the file's text (exposed for tests)

private:
    void ParseColumnOrder(StrView value);
    Str iniPath_;
};

} // namespace sfv
