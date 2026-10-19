// SharpSFV (lean C++ rewrite) - enumerations shared by the engine, CLI and GUI.
// Port of src/core/Types.* (same names, same text forms, same parsing).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: JobStatus (the GUI's job queue).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "../base/Str.h"

namespace sfv {

enum class HashType : int { XXHASH3 = 0, Crc32 = 1, MD5 = 2, SHA1 = 3, SHA256 = 4 };
enum class ItemStatus : u8 { Queued = 0, Pending = 1, OK = 2, Bad = 3, Missing = 4, Error = 5 };
enum class ProcessingMode : int { Auto = 0, HDD = 1, SSD = 2 };
enum class PathStorageMode : int { Relative = 0, Absolute = 1 };
enum class Crc32ByteOrder : int { Standard = 0, Reversed = 1 };
enum class CrcMatch : u8 { None = 0, Standard = 1, Reversed = 2 };
enum class DataConsent : int { NotAsked = 0, Allowed = 1, Declined = 2 };
enum class JobStatus : int { Queued = 0, Scanning = 1, InProgress = 2, Paused = 3, Done = 4, Error = 5 };

const char* ToString(HashType v);
const char* ToString(ItemStatus v);
const char* ToString(ProcessingMode v);
const char* ToString(PathStorageMode v);
const char* ToString(Crc32ByteOrder v);
const char* ToString(DataConsent v);
const char* ToString(JobStatus v);

// Name (case-insensitive) or number, as the C# Enum.TryParse; no value when unknown.
Opt<HashType> ParseHashType(StrView s);
Opt<ProcessingMode> ParseProcessingMode(StrView s);
Opt<PathStorageMode> ParsePathStorageMode(StrView s);
Opt<Crc32ByteOrder> ParseCrc32ByteOrder(StrView s); // also BigEndian/QuickSFV/LittleEndian/SharpSFV...
Opt<DataConsent> ParseDataConsent(StrView s);

const char* ExtensionForAlgo(HashType t); // ".sfv", ".md5", ...
int DigestSize(HashType t);

} // namespace sfv
