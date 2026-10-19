// SharpSFV (C++ port) - checksum file writers.
// The original has three slightly different writers; all three are kept so output stays
// identical in each situation:
//   WriteSaveAs     <- PerformSaveAction        (File > Save As..., also used by the CLI)
//   WriteCreateMode <- SaveResultsToFile        (Explorer "Create Checksum" mini mode)
//   WriteJobFile    <- SaveChecksumFileForJob   (Job Queue mode)
// Lines are "<HEX> *<path>", UTF-8 without BOM, platform newline (CRLF on Windows).
// CRC-32 values are written in settings.crc32ByteOrder.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "AppSettings.h"
#include "FileStore.h"
#include "Types.h"

#include <string>

namespace sfv {

// Returns false (with error) if the file could not be written.
bool WriteSaveAs(const std::string& outPath, const FileStore& store, const AppSettings& settings, HashType algo,
                 std::string* error);

bool WriteCreateMode(const std::string& outPath, const FileStore& store, const AppSettings& settings, HashType algo,
                     std::string* error);

// Returns true when the job had errors (BAD/ERROR items) or the file could not be written,
// matching the original's "hasErrors" result.
bool WriteJobFile(const std::string& outPath, const FileStore& store, const AppSettings& settings, HashType algo);

} // namespace sfv
