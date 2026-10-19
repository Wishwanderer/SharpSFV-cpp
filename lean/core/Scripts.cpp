// SharpSFV (lean C++ rewrite) - batch scripts and PATH lists (see Scripts.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Scripts.h"

#include "../base/Text.h"

namespace sfv {

namespace {
constexpr const char* kNl = "\r\n";

Str Header(const char* banner) {
    Str s = "@echo off";
    s += kNl;
    s += "chcp 65001 >nul";
    s += kNl;
    s += Str("echo ") + banner;
    s += kNl;
    return s;
}

Str Normalize(StrView entry, ExpandFn expand) {
    Str e(text::Trim(entry));
    if (e.Size() >= 2 && e[0] == '"' && e.Back() == '"') e = Str(e.Sub(1, e.Size() - 2));
    if (expand) e = expand(e);
    while (e.Size() > 3 && (e.Back() == '\\' || e.Back() == '/')) e.PopBack();
    return text::FoldCase(e);
}
} // namespace

Str BatchQuote(StrView path) {
    Str r = "\"";
    for (usize i = 0; i < path.Size(); ++i) {
        if (path[i] == '%') r += "%%";
        else r += path[i];
    }
    r += '"';
    return r;
}

Str DeleteBadFilesScript(const Vec<Str>& paths) {
    Str s = Header("Deleting BAD files...");
    for (const Str& p : paths) s += "del " + BatchQuote(p) + kNl;
    s += Str("pause") + kNl;
    return s;
}

Str CopyDuplicatesScript(const Vec<Vec<Str>>& groups) {
    Str s = Header("Copying duplicates (Source: 1st file -^> Dest: duplicates)...");
    for (const Vec<Str>& g : groups)
        for (usize i = 1; i < g.Size(); ++i) s += "copy /y " + BatchQuote(g[0]) + " " + BatchQuote(g[i]) + kNl;
    s += Str("pause") + kNl;
    return s;
}

Str DeleteDuplicatesScript(const Vec<Vec<Str>>& groups) {
    Str s = Header("Deleting duplicate files (Keeping 1st, deleting rest)...");
    for (const Vec<Str>& g : groups)
        for (usize i = 1; i < g.Size(); ++i) s += "del " + BatchQuote(g[i]) + kNl;
    s += Str("pause") + kNl;
    return s;
}

bool PathListContains(StrView list, StrView dir, ExpandFn expand) {
    const Str target = Normalize(dir, expand);
    for (const Str& e : text::Split(list, ";", false))
        if (!text::Trim(e).Empty() && Normalize(e, expand) == target) return true;
    return false;
}

Str PathListAdd(StrView list, StrView dir, ExpandFn expand) {
    if (PathListContains(list, dir, expand)) return Str(list);
    Str r(list);
    while (!r.Empty() && r.Back() == ';') r.PopBack();
    if (!r.Empty()) r += ';';
    r += dir;
    return r;
}

Str PathListRemove(StrView list, StrView dir, ExpandFn expand) {
    const Str target = Normalize(dir, expand);
    Str r;
    for (const Str& e : text::Split(list, ";", false)) {
        if (text::Trim(e).Empty() || Normalize(e, expand) == target) continue;
        if (!r.Empty()) r += ';';
        r += e;
    }
    return r;
}

} // namespace sfv
