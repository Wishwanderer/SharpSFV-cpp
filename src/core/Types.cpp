// SharpSFV (C++ port) - shared enums.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Types.h"

#include "Text.h"

#include <array>

namespace sfv {

namespace {
constexpr std::array<const char*, 5> kHashNames{"XXHASH3", "Crc32", "MD5", "SHA1", "SHA256"};
constexpr std::array<const char*, 6> kStatusNames{"Queued", "Pending", "OK", "Bad", "Missing", "Error"};
constexpr std::array<const char*, 3> kProcNames{"Auto", "HDD", "SSD"};
constexpr std::array<const char*, 2> kPathNames{"Relative", "Absolute"};
constexpr std::array<const char*, 6> kJobNames{"Queued", "Scanning", "InProgress", "Paused", "Done", "Error"};
constexpr std::array<const char*, 2> kCrcOrderNames{"Standard", "Reversed"};
constexpr std::array<const char*, 3> kConsentNames{"NotAsked", "Allowed", "Declined"};

template <typename E, std::size_t N>
std::optional<E> ParseEnum(std::string_view s, const std::array<const char*, N>& names) {
    s = text::Trim(s);
    for (std::size_t i = 0; i < N; ++i)
        if (text::EqualsIgnoreCase(s, names[i])) return static_cast<E>(i);
    int n = 0;
    if (text::TryParseInt32(s, n) && n >= 0 && static_cast<std::size_t>(n) < N) return static_cast<E>(n);
    return std::nullopt;
}
} // namespace

const char* ToString(HashType v) { return kHashNames.at(static_cast<std::size_t>(v)); }
const char* ToString(ItemStatus v) { return kStatusNames.at(static_cast<std::size_t>(v)); }
const char* ToString(ProcessingMode v) { return kProcNames.at(static_cast<std::size_t>(v)); }
const char* ToString(PathStorageMode v) { return kPathNames.at(static_cast<std::size_t>(v)); }
const char* ToString(JobStatus v) { return kJobNames.at(static_cast<std::size_t>(v)); }
const char* ToString(Crc32ByteOrder v) { return kCrcOrderNames.at(static_cast<std::size_t>(v)); }
const char* ToString(DataConsent v) { return kConsentNames.at(static_cast<std::size_t>(v)); }

std::optional<HashType> ParseHashType(std::string_view s) { return ParseEnum<HashType>(s, kHashNames); }
std::optional<ProcessingMode> ParseProcessingMode(std::string_view s) { return ParseEnum<ProcessingMode>(s, kProcNames); }
std::optional<PathStorageMode> ParsePathStorageMode(std::string_view s) { return ParseEnum<PathStorageMode>(s, kPathNames); }
std::optional<DataConsent> ParseDataConsent(std::string_view s) { return ParseEnum<DataConsent>(s, kConsentNames); }

std::optional<Crc32ByteOrder> ParseCrc32ByteOrder(std::string_view s) {
    if (auto v = ParseEnum<Crc32ByteOrder>(s, kCrcOrderNames)) return v;
    const std::string_view t = text::Trim(s);
    for (const char* a : {"BigEndian", "Big-Endian", "BE", "QuickSFV", "Conventional"})
        if (text::EqualsIgnoreCase(t, a)) return Crc32ByteOrder::Standard;
    for (const char* a : {"LittleEndian", "Little-Endian", "LE", "SharpSFV", ".NET", "DotNet"})
        if (text::EqualsIgnoreCase(t, a)) return Crc32ByteOrder::Reversed;
    return std::nullopt;
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
