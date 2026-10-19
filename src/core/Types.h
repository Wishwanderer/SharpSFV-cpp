// SharpSFV (C++ port) - shared enums.
// Ported from HashType.cs, ItemStatus.cs, ProcessingMode.cs, PathStorageMode.cs, JobStore.cs (JobStatus).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sfv {

// Order and names match the C# enums so INI values ("DefaultAlgo=XXHASH3") and
// numeric values stay interchangeable with the original.
enum class HashType : int { XXHASH3 = 0, Crc32 = 1, MD5 = 2, SHA1 = 3, SHA256 = 4 };
enum class ItemStatus : std::uint8_t { Queued = 0, Pending = 1, OK = 2, Bad = 3, Missing = 4, Error = 5 };
enum class ProcessingMode : int { Auto = 0, HDD = 1, SSD = 2 };
enum class PathStorageMode : int { Relative = 0, Absolute = 1 };
enum class JobStatus : int { Queued = 0, Scanning = 1, InProgress = 2, Paused = 3, Done = 4, Error = 5 };

// How CRC-32 values are written as 8 hex digits.
//   Standard : the CRC value most-significant byte first, e.g. "123456789" -> CBF43926.
//              Used by QuickSFV, RapidCRC, 7-Zip, cksfv, TeraCopy and the SFV convention.
//   Reversed : least-significant byte first -> 2639F4CB. What the C# SharpSFV wrote
//              (System.IO.Hashing.Crc32.GetCurrentHash() is little-endian).
// Verification always accepts both; this only decides what new files contain.
enum class Crc32ByteOrder : int { Standard = 0, Reversed = 1 };

// Which byte order a verified CRC-32 entry turned out to use (None for other algorithms,
// or when the file did not match in either order).
enum class CrcMatch : std::uint8_t { None = 0, Standard = 1, Reversed = 2 };

// Permission to keep data in the local app-data folder (%LOCALAPPDATA%\SharpSFV).
enum class DataConsent : int { NotAsked = 0, Allowed = 1, Declined = 2 };

// Enum.ToString() equivalents.
const char* ToString(HashType v);
const char* ToString(ItemStatus v);
const char* ToString(ProcessingMode v);
const char* ToString(PathStorageMode v);
const char* ToString(JobStatus v);
const char* ToString(Crc32ByteOrder v);
const char* ToString(DataConsent v);

// Enum.TryParse(value, ignoreCase: true) equivalents. Accept the member name
// (case-insensitive) or its integer value.
std::optional<HashType> ParseHashType(std::string_view s);
std::optional<ProcessingMode> ParseProcessingMode(std::string_view s);
std::optional<PathStorageMode> ParsePathStorageMode(std::string_view s);
// Also accepts BigEndian/QuickSFV (Standard) and LittleEndian/SharpSFV/.NET (Reversed).
std::optional<Crc32ByteOrder> ParseCrc32ByteOrder(std::string_view s);
std::optional<DataConsent> ParseDataConsent(std::string_view s);

// Default file extension for an algorithm (GetExtensionForAlgo in Form1.Processing.cs).
const char* ExtensionForAlgo(HashType t);

// Digest size in bytes.
int DigestSize(HashType t);

} // namespace sfv
