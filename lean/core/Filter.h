// SharpSFV (lean C++ rewrite) - include/exclude file-name patterns.
// Port of src/core/FileFilter.* (FitsMask / IsFileAllowed from the C# version).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "../base/Str.h"

namespace sfv {

bool FitsMask(StrView fileName, StrView mask);
bool IsFileAllowed(StrView fileName, StrView includePattern, StrView excludePattern);

} // namespace sfv
