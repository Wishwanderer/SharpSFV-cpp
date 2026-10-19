// SharpSFV (lean C++ rewrite) - reading and writing checksum files (.sfv .md5 .sha1 ...).
// Port of src/core/ChecksumParser.* and the Save-As writer of src/core/ChecksumWriter.*.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: the create-mode and job-queue writers.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "../base/Str.h"
#include "../base/Vec.h"
#include "Settings.h"
#include "Store.h"
#include "Types.h"

namespace sfv {

struct ChecksumEntry {
    Vec<u8> expectedHash;
    Str fileName; // as written in the file (relative or absolute)
};

struct ParsedChecksumFile {
    Vec<ChecksumEntry> entries;
    Str comments; // "//" comment lines, each followed by a newline
};

HashType DetectAlgoFromExtension(StrView checksumPath, HashType fallback);
bool IsVerificationExtension(StrView path);

// The two patterns of the C# parser:
//   A  ^\s*([0-9a-fA-F]+)\s+\*?(.*?)\s*$     hash first (md5sum style)
//   B  ^\s*(.*?)\s+([0-9a-fA-F]{8})\s*$     name first (SFV style)
bool MatchHashFirst(StrView line, Str& hash, Str& fileName);
bool MatchNameFirst(StrView line, Str& fileName, Str& hash);

ParsedChecksumFile ParseChecksumText(StrView content, HashType algo);
bool ParseChecksumFile(StrView path, HashType algo, ParsedChecksumFile& out, Str* error);

// "Save As": one "HASH *path" line per finished item, paths relative to the file's folder
// (or absolute), with the optional prefix and header comments from the settings.
bool WriteSaveAs(StrView outPath, const FileStore& store, const AppSettings& settings, HashType algo, Str* error);
// Explorer "Create Checksum" (mini window): every hashed item, paths relative to the file.
bool WriteCreateMode(StrView outPath, const FileStore& store, const AppSettings& settings, HashType algo, Str* error);
// Job Queue mode: OK items only. True when the job had BAD/ERROR items or the file could not
// be written (the original's "hasErrors").
bool WriteJobFile(StrView outPath, const FileStore& store, const AppSettings& settings, HashType algo);

} // namespace sfv
