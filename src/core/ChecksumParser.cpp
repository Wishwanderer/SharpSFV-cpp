// SharpSFV (C++ port) - checksum file parsing.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "ChecksumParser.h"

#include "FileSystem.h"
#include "Hasher.h"
#include "PathUtil.h"
#include "Text.h"

namespace sfv {

namespace {
bool IsHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
} // namespace

HashType DetectAlgoFromExtension(std::string_view checksumPath, HashType fallback) {
    const std::string_view ext = path::GetExtension(checksumPath);
    if (text::EqualsIgnoreCase(ext, ".sfv")) return HashType::Crc32;
    if (text::EqualsIgnoreCase(ext, ".md5")) return HashType::MD5;
    if (text::EqualsIgnoreCase(ext, ".sha1")) return HashType::SHA1;
    if (text::EqualsIgnoreCase(ext, ".sha256")) return HashType::SHA256;
    if (text::EqualsIgnoreCase(ext, ".xxh3")) return HashType::XXHASH3;
    return fallback;
}

bool IsVerificationExtension(std::string_view p) {
    const std::string_view ext = path::GetExtension(p);
    for (const char* e : {".xxh3", ".xxh", ".sum", ".md5", ".sfv", ".sha1", ".sha256", ".txt"})
        if (text::EqualsIgnoreCase(ext, e)) return true;
    return false;
}

bool MatchHashFirst(std::string_view line, std::string& hash, std::string& fileName) {
    // ^\s*([0-9a-fA-F]+)\s+\*?(.*?)\s*$
    const std::string_view afterLead = text::TrimStart(line);
    std::size_t j = 0;
    while (j < afterLead.size() && IsHex(afterLead[j])) ++j;
    if (j == 0) return false;

    const std::string_view rest = afterLead.substr(j);
    const std::string_view afterWs = text::TrimStart(rest);
    if (afterWs.size() == rest.size()) return false; // \s+ needs at least one whitespace

    std::string_view name = afterWs;
    if (name.starts_with('*')) name.remove_prefix(1);

    hash.assign(afterLead.substr(0, j));
    fileName.assign(text::Trim(name)); // (.*?)\s*$ then .Trim() in the caller
    return true;
}

bool MatchNameFirst(std::string_view line, std::string& fileName, std::string& hash) {
    // ^\s*(.*?)\s+([0-9a-fA-F]{8})\s*$
    const std::string_view t = text::TrimEnd(line);
    if (t.size() < 8) return false;
    const std::string_view h = t.substr(t.size() - 8);
    for (char c : h)
        if (!IsHex(c)) return false;

    const std::string_view before = t.substr(0, t.size() - 8);
    const std::string_view beforeTrimmed = text::TrimEnd(before);
    if (beforeTrimmed.size() == before.size()) return false; // \s+ needs at least one whitespace

    fileName.assign(text::Trim(beforeTrimmed));
    hash.assign(h);
    return true;
}

ParsedChecksumFile ParseChecksumText(std::string_view content, HashType algo) {
    ParsedChecksumFile result;
    std::string hashA, nameA, hashB, nameB;

    for (const std::string& line : text::SplitLines(content)) {
        const std::string_view trimmed = text::TrimStart(line);
        if (trimmed.starts_with("//")) {
            result.comments.append(text::Trim(trimmed.substr(2)));
            result.comments.append(fs::kNewLine);
            continue;
        }
        if (text::IsNullOrWhiteSpace(line) || trimmed.starts_with(';')) continue;

        std::string expected, fileName;
        const bool a = MatchHashFirst(line, hashA, nameA);
        const bool b = MatchNameFirst(line, nameB, hashB);

        // SFV usually puts the file name first, other formats put the hash first.
        if (algo == HashType::Crc32 && b) {
            fileName = nameB;
            expected = hashB;
        } else if (a) {
            expected = hashA;
            fileName = nameA;
        }

        if (!expected.empty()) {
            ChecksumEntry e;
            if (TryFromHex(expected, e.expectedHash)) {
                e.fileName = std::move(fileName);
                result.entries.push_back(std::move(e));
            }
        }
    }
    return result;
}

std::optional<ParsedChecksumFile> ParseChecksumFile(const std::string& p, HashType algo, std::string* error) {
    auto bytes = fs::ReadAllBytes(p, error);
    if (!bytes) return std::nullopt;
    return ParseChecksumText(text::DecodeTextFile(*bytes), algo);
}

} // namespace sfv
