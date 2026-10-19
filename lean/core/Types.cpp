// SharpSFV (lean C++ rewrite) - enumerations (see Types.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: JobStatus.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Types.h"

#include "../base/Text.h"

namespace sfv {

namespace {
const char* const kHashNames[] = {"XXHASH3", "Crc32", "MD5", "SHA1", "SHA256"};
const char* const kStatusNames[] = {"Queued", "Pending", "OK", "Bad", "Missing", "Error"};
const char* const kProcNames[] = {"Auto", "HDD", "SSD"};
const char* const kPathNames[] = {"Relative", "Absolute"};
const char* const kCrcOrderNames[] = {"Standard", "Reversed"};
const char* const kConsentNames[] = {"NotAsked", "Allowed", "Declined"};
const char* const kJobNames[] = {"Queued", "Scanning", "InProgress", "Paused", "Done", "Error"};

int ParseEnum(StrView s, const char* const* names, int count) {
    s = text::Trim(s);
    for (int i = 0; i < count; ++i)
        if (text::EqualsIgnoreCase(s, names[i])) return i;
    int n = 0;
    if (text::TryParseInt32(s, n) && n >= 0 && n < count) return n;
    return -1;
}
template <class E, int N> Opt<E> Parse(StrView s, const char* const (&names)[N]) {
    const int i = ParseEnum(s, names, N);
    if (i < 0) return {};
    return static_cast<E>(i);
}
template <int N> const char* Name(int i, const char* const (&names)[N]) { return (i >= 0 && i < N) ? names[i] : ""; }
} // namespace

const char* ToString(HashType v) { return Name(static_cast<int>(v), kHashNames); }
const char* ToString(ItemStatus v) { return Name(static_cast<int>(v), kStatusNames); }
const char* ToString(ProcessingMode v) { return Name(static_cast<int>(v), kProcNames); }
const char* ToString(PathStorageMode v) { return Name(static_cast<int>(v), kPathNames); }
const char* ToString(Crc32ByteOrder v) { return Name(static_cast<int>(v), kCrcOrderNames); }
const char* ToString(DataConsent v) { return Name(static_cast<int>(v), kConsentNames); }
const char* ToString(JobStatus v) { return Name(static_cast<int>(v), kJobNames); }

Opt<HashType> ParseHashType(StrView s) { return Parse<HashType>(s, kHashNames); }
Opt<ProcessingMode> ParseProcessingMode(StrView s) { return Parse<ProcessingMode>(s, kProcNames); }
Opt<PathStorageMode> ParsePathStorageMode(StrView s) { return Parse<PathStorageMode>(s, kPathNames); }
Opt<DataConsent> ParseDataConsent(StrView s) { return Parse<DataConsent>(s, kConsentNames); }

Opt<Crc32ByteOrder> ParseCrc32ByteOrder(StrView s) {
    if (auto v = Parse<Crc32ByteOrder>(s, kCrcOrderNames)) return v;
    const StrView t = text::Trim(s);
    static const char* const kStandard[] = {"BigEndian", "Big-Endian", "BE", "QuickSFV", "Conventional"};
    static const char* const kReversed[] = {"LittleEndian", "Little-Endian", "LE", "SharpSFV", ".NET", "DotNet"};
    for (const char* a : kStandard)
        if (text::EqualsIgnoreCase(t, a)) return Crc32ByteOrder::Standard;
    for (const char* a : kReversed)
        if (text::EqualsIgnoreCase(t, a)) return Crc32ByteOrder::Reversed;
    return {};
}

const char* ExtensionForAlgo(HashType t) {
    switch (t) {
        case HashType::Crc32: return ".sfv";
        case HashType::MD5: return ".md5";
        case HashType::SHA1: return ".sha1";
        case HashType::SHA256: return ".sha256";
        case HashType::XXHASH3:
        default: return ".xxh3";
    }
}

int DigestSize(HashType t) {
    switch (t) {
        case HashType::Crc32: return 4;
        case HashType::MD5: return 16;
        case HashType::SHA1: return 20;
        case HashType::SHA256: return 32;
        case HashType::XXHASH3:
        default: return 16;
    }
}

} // namespace sfv
