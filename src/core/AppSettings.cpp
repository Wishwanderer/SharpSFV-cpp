// SharpSFV (C++ port) - persistent settings.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-04: CreateIfMissing.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "AppSettings.h"

#include "FileSystem.h"
#include "PathUtil.h"
#include "Text.h"

namespace sfv {

AppSettings::AppSettings(const std::string& iniDirectory) : iniPath_(path::Combine(iniDirectory, "SharpSFV.ini")) {}

void AppSettings::Load() {
    if (!fs::FileExists(iniPath_)) return;
    auto bytes = fs::ReadAllBytes(iniPath_);
    if (!bytes) return;
    ParseText(text::DecodeTextFile(*bytes));
}

void AppSettings::ParseText(const std::string& content) {
    using text::EqualsIgnoreCase;
    int w = 800, h = 600, x = 0, y = 0;
    bool foundX = false, foundY = false;

    for (const std::string& line : text::SplitLines(content)) {
        if (text::IsNullOrWhiteSpace(line) || line.starts_with('[')) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key(text::Trim(std::string_view(line).substr(0, eq)));
        const std::string val(text::Trim(std::string_view(line).substr(eq + 1)));
        auto is = [&](const char* k) { return EqualsIgnoreCase(key, k); };
        const bool on = val == "1";

        // UI layout
        if (is("TimeTab")) showTimeTab = on;
        else if (is("ShowThroughputStats")) showThroughputStats = on;
        else if (is("ShowFilterPanel")) showFilterPanel = on;
        else if (is("ShowAdvancedBar")) showAdvancedBar = on;
        else if (is("ShowFullPaths")) showFullPaths = on;
        else if (is("ShowHashCol")) showHashCol = on;
        else if (is("ShowExpectedHashCol")) showExpectedHashCol = on;
        else if (is("LockColumns")) lockColumns = on;
        // Modes
        else if (is("PathStorageMode")) { if (auto v = ParsePathStorageMode(val)) pathStorageMode = *v; }
        else if (is("UseAbsolutePaths")) { if (on) pathStorageMode = PathStorageMode::Absolute; } // legacy
        else if (is("ProcessingMode")) { if (auto v = ParseProcessingMode(val)) processingMode = *v; }
        else if (is("OptimizeForHDD")) { if (on) processingMode = ProcessingMode::HDD; } // legacy
        // Advanced options
        else if (is("EnableChecksumComments")) enableChecksumComments = on;
        else if (is("PathPrefix")) pathPrefix = val;
        else if (is("IncludePattern")) includePattern = val;
        else if (is("ExcludePattern")) excludePattern = val;
        else if (is("ScanRecursive")) scanRecursive = on;
        else if (is("Signature")) customSignature = val;
        // ListView customization
        else if (is("ColumnOrder")) ParseColumnOrder(val);
        // Algorithm
        else if (is("DefaultAlgo")) { if (auto v = ParseHashType(val)) defaultAlgo = *v; }
        else if (is("Crc32ByteOrder")) { if (auto v = ParseCrc32ByteOrder(val)) crc32ByteOrder = *v; }
        else if (is("DataStorageConsent")) { if (auto v = ParseDataConsent(val)) dataStorageConsent = *v; }
        // Window geometry (int.TryParse semantics: failure writes 0)
        else if (is("WindowSizeW")) text::TryParseInt32(val, w);
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

void AppSettings::ParseColumnOrder(const std::string& value) {
    columnOrder.clear();
    for (const std::string& pair : text::Split(value, ",", true)) {
        const auto kv = text::Split(pair, ":", false);
        int idx = 0;
        if (kv.size() == 2 && text::TryParseInt32(kv[1], idx)) {
            bool updated = false;
            for (auto& [k, v] : columnOrder) {
                if (k == kv[0]) {
                    v = idx;
                    updated = true;
                    break;
                }
            }
            if (!updated) columnOrder.emplace_back(kv[0], idx);
        }
    }
}

std::string AppSettings::Serialize(const SaveState& s) const {
    const char* nl = fs::kNewLine;
    auto b = [](bool v) { return v ? "1" : "0"; };

    std::string colOrder;
    for (std::size_t i = 0; i < s.columnOrder.size(); ++i) {
        if (i) colOrder += ',';
        colOrder += s.columnOrder[i].first + ":" + std::to_string(s.columnOrder[i].second);
    }

    std::string o;
    auto line = [&](const std::string& l) {
        o += l;
        o += nl;
    };
    line("[SharpSFV]");
    line(std::string("TimeTab=") + b(s.timeTabEnabled));
    line(std::string("ShowThroughputStats=") + b(showThroughputStats));
    line(std::string("PathStorageMode=") + ToString(pathStorageMode));
    line(std::string("UseAbsolutePaths=") + b(pathStorageMode == PathStorageMode::Absolute));
    line(std::string("ShowFilterPanel=") + b(showFilterPanel));
    line(std::string("ShowAdvancedBar=") + b(showAdvancedBar));
    line(std::string("EnableChecksumComments=") + b(enableChecksumComments));
    line("PathPrefix=" + pathPrefix);
    line("IncludePattern=" + includePattern);
    line("ExcludePattern=" + excludePattern);
    line(std::string("ScanRecursive=") + b(scanRecursive));
    line(std::string("ProcessingMode=") + ToString(processingMode));
    line(std::string("ShowFullPaths=") + b(showFullPaths));
    line(std::string("ShowHashCol=") + b(showHashCol));
    line(std::string("ShowExpectedHashCol=") + b(showExpectedHashCol));
    line(std::string("LockColumns=") + b(lockColumns));
    line("ColumnOrder=" + colOrder);
    line("Signature=" + customSignature);
    line(std::string("DefaultAlgo=") + ToString(s.currentAlgo));
    line("WindowSizeW=" + std::to_string(s.bounds.width));
    line("WindowSizeH=" + std::to_string(s.bounds.height));
    if (s.bounds.hasPosition) {
        line("WindowPosX=" + std::to_string(s.bounds.x));
        line("WindowPosY=" + std::to_string(s.bounds.y));
    }
    line("SplitterPos=" + std::to_string(s.splitterDistance));
    // Keys added by the C++ port (after the original 25, so the rest of the file keeps the
    // C# layout; the C# build ignores them).
    line(std::string("Crc32ByteOrder=") + ToString(crc32ByteOrder));
    line(std::string("DataStorageConsent=") + ToString(dataStorageConsent));
    return o;
}

bool AppSettings::Save(const SaveState& state) const {
    fs::TextWriter w;
    if (!w.Open(iniPath_)) return false;
    w.Write(Serialize(state));
    return w.Close();
}

AppSettings::SaveState AppSettings::CurrentState() const {
    SaveState st;
    st.timeTabEnabled = showTimeTab;
    st.currentAlgo = defaultAlgo;
    st.splitterDistance = splitterDistance;
    st.bounds = {windowX, windowY, windowWidth, windowHeight, hasCustomLocation};
    st.columnOrder = columnOrder;
    return st;
}

bool AppSettings::CreateIfMissing() const {
    if (iniPath_.empty() || fs::FileExists(iniPath_) || fs::DirectoryExists(iniPath_)) return false;
    return SaveCurrent();
}

void AppSettings::ResetToDefaults() const {
    if (fs::FileExists(iniPath_)) fs::RemoveFile(iniPath_);
}

} // namespace sfv
