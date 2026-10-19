// SharpSFV (lean C++ rewrite) - SharpSFV.ini (see Settings.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: saving, and creating the file with defaults when it is missing.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Settings.h"

#include "../base/FileSystem.h"
#include "../base/Path.h"
#include "../base/Text.h"

namespace sfv {

AppSettings::AppSettings(StrView iniDirectory) : iniPath_(path::Combine(iniDirectory, "SharpSFV.ini")) {}

void AppSettings::Load() {
    if (!fs::FileExists(iniPath_)) return;
    Str bytes;
    if (!fs::ReadAllBytes(iniPath_, bytes)) return;
    ParseText(text::DecodeTextFile(bytes));
}

void AppSettings::ParseText(StrView content) {
    using text::EqualsIgnoreCase;
    int w = 800, h = 600, x = 0, y = 0;
    bool foundX = false, foundY = false;

    for (const Str& line : text::SplitLines(content)) {
        if (text::IsNullOrWhiteSpace(line) || line.StartsWith('[')) continue;
        const usize eq = line.Find('=');
        if (eq == kNpos) continue;

        const Str key(text::Trim(line.Sub(0, eq)));
        const Str val(text::Trim(line.Sub(eq + 1)));
        auto is = [&](const char* k) { return EqualsIgnoreCase(key, k); };
        const bool on = val == "1";

        if (is("TimeTab")) showTimeTab = on;
        else if (is("ShowThroughputStats")) showThroughputStats = on;
        else if (is("ShowFilterPanel")) showFilterPanel = on;
        else if (is("ShowAdvancedBar")) showAdvancedBar = on;
        else if (is("ShowFullPaths")) showFullPaths = on;
        else if (is("ShowHashCol")) showHashCol = on;
        else if (is("ShowExpectedHashCol")) showExpectedHashCol = on;
        else if (is("LockColumns")) lockColumns = on;
        else if (is("PathStorageMode")) { if (auto v = ParsePathStorageMode(val)) pathStorageMode = *v; }
        else if (is("UseAbsolutePaths")) { if (on) pathStorageMode = PathStorageMode::Absolute; } // legacy
        else if (is("ProcessingMode")) { if (auto v = ParseProcessingMode(val)) processingMode = *v; }
        else if (is("OptimizeForHDD")) { if (on) processingMode = ProcessingMode::HDD; } // legacy
        else if (is("EnableChecksumComments")) enableChecksumComments = on;
        else if (is("PathPrefix")) pathPrefix = val;
        else if (is("IncludePattern")) includePattern = val;
        else if (is("ExcludePattern")) excludePattern = val;
        else if (is("ScanRecursive")) scanRecursive = on;
        else if (is("Signature")) customSignature = val;
        else if (is("ColumnOrder")) ParseColumnOrder(val);
        else if (is("DefaultAlgo")) { if (auto v = ParseHashType(val)) defaultAlgo = *v; }
        else if (is("Crc32ByteOrder")) { if (auto v = ParseCrc32ByteOrder(val)) crc32ByteOrder = *v; }
        else if (is("DataStorageConsent")) { if (auto v = ParseDataConsent(val)) dataStorageConsent = *v; }
        else if (is("WindowSizeW")) text::TryParseInt32(val, w); // int.TryParse: failure writes 0
        else if (is("WindowSizeH")) text::TryParseInt32(val, h);
        else if (is("WindowPosX")) { if (text::TryParseInt32(val, x)) foundX = true; }
        else if (is("WindowPosY")) { if (text::TryParseInt32(val, y)) foundY = true; }
        else if (is("SplitterPos")) {
            int d = 0;
            if (text::TryParseInt32(val, d)) splitterDistance = d;
        }
    }

    windowWidth = w;
    windowHeight = h;
    if (foundX && foundY) {
        windowX = x;
        windowY = y;
        hasCustomLocation = true;
    }
}

void AppSettings::ParseColumnOrder(StrView value) {
    columnOrder.Clear();
    for (const Str& pair : text::Split(value, ",", true)) {
        const Vec<Str> kv = text::Split(pair, ":", false);
        int idx = 0;
        if (kv.Size() == 2 && text::TryParseInt32(kv[1], idx)) {
            bool updated = false;
            for (ColumnOrderEntry& e : columnOrder) {
                if (e.tag == kv[0]) {
                    e.displayIndex = idx;
                    updated = true;
                    break;
                }
            }
            if (!updated) columnOrder.Push(ColumnOrderEntry{kv[0], idx});
        }
    }
}

// AppSettings::Serialize of the standard build with SaveState = CurrentState(): the same
// 27 keys in the same order, so the C# build, the standard build and this one read each
// other's files.
Str AppSettings::Serialize() const {
    auto b = [](bool v) { return v ? "1" : "0"; };
    Str o;
    auto line = [&](StrView l) {
        o += l;
        o += fs::kNewLine;
    };
    Str colOrder;
    for (usize i = 0; i < columnOrder.Size(); ++i) {
        if (i) colOrder += ',';
        colOrder += columnOrder[i].tag + ":" + ToStr(columnOrder[i].displayIndex);
    }
    line("[SharpSFV]");
    line(Str("TimeTab=") + b(showTimeTab));
    line(Str("ShowThroughputStats=") + b(showThroughputStats));
    line(Str("PathStorageMode=") + ToString(pathStorageMode));
    line(Str("UseAbsolutePaths=") + b(pathStorageMode == PathStorageMode::Absolute));
    line(Str("ShowFilterPanel=") + b(showFilterPanel));
    line(Str("ShowAdvancedBar=") + b(showAdvancedBar));
    line(Str("EnableChecksumComments=") + b(enableChecksumComments));
    line("PathPrefix=" + pathPrefix);
    line("IncludePattern=" + includePattern);
    line("ExcludePattern=" + excludePattern);
    line(Str("ScanRecursive=") + b(scanRecursive));
    line(Str("ProcessingMode=") + ToString(processingMode));
    line(Str("ShowFullPaths=") + b(showFullPaths));
    line(Str("ShowHashCol=") + b(showHashCol));
    line(Str("ShowExpectedHashCol=") + b(showExpectedHashCol));
    line(Str("LockColumns=") + b(lockColumns));
    line("ColumnOrder=" + colOrder);
    line("Signature=" + customSignature);
    line(Str("DefaultAlgo=") + ToString(defaultAlgo));
    line("WindowSizeW=" + ToStr(windowWidth));
    line("WindowSizeH=" + ToStr(windowHeight));
    if (hasCustomLocation) {
        line("WindowPosX=" + ToStr(windowX));
        line("WindowPosY=" + ToStr(windowY));
    }
    line("SplitterPos=" + ToStr(splitterDistance));
    // Keys added by the C++ port (after the original 25; the C# build ignores them).
    line(Str("Crc32ByteOrder=") + ToString(crc32ByteOrder));
    line(Str("DataStorageConsent=") + ToString(dataStorageConsent));
    return o;
}

bool AppSettings::SaveCurrent() const {
    fs::TextWriter w;
    if (!w.Open(iniPath_)) return false;
    w.Write(Serialize());
    return w.Close();
}

bool AppSettings::CreateIfMissing() const {
    if (iniPath_.Empty() || fs::FileExists(iniPath_) || fs::DirectoryExists(iniPath_)) return false;
    return SaveCurrent();
}

} // namespace sfv
