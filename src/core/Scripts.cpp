// SharpSFV (C++ port) - generated Windows batch scripts.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Scripts.h"

namespace sfv {

namespace {
constexpr const char* kNl = "\r\n";

std::string Header(const char* banner) {
    std::string s = "@echo off";
    s += kNl;
    s += "chcp 65001 >nul";
    s += kNl;
    s += std::string("echo ") + banner;
    s += kNl;
    return s;
}
} // namespace

std::string BatchQuote(std::string_view path) {
    std::string r = "\"";
    for (char c : path) {
        if (c == '%') r += "%%";
        else r += c;
    }
    r += '"';
    return r;
}

std::string DeleteBadFilesScript(const std::vector<std::string>& paths) {
    std::string s = Header("Deleting BAD files...");
    for (const auto& p : paths) s += "del " + BatchQuote(p) + kNl;
    s += std::string("pause") + kNl;
    return s;
}

std::string CopyDuplicatesScript(const std::vector<std::vector<std::string>>& groups) {
    std::string s = Header("Copying duplicates (Source: 1st file -^> Dest: duplicates)...");
    for (const auto& g : groups) {
        for (std::size_t i = 1; i < g.size(); ++i) s += "copy /y " + BatchQuote(g[0]) + " " + BatchQuote(g[i]) + kNl;
    }
    s += std::string("pause") + kNl;
    return s;
}

std::string DeleteDuplicatesScript(const std::vector<std::vector<std::string>>& groups) {
    std::string s = Header("Deleting duplicate files (Keeping 1st, deleting rest)...");
    for (const auto& g : groups) {
        for (std::size_t i = 1; i < g.size(); ++i) s += "del " + BatchQuote(g[i]) + kNl;
    }
    s += std::string("pause") + kNl;
    return s;
}

} // namespace sfv
