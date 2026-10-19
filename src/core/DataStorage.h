// SharpSFV (C++ port) - local data storage and the user's consent to it.
// New in the C++ port (not in the C# original).
//
// SharpSFV keeps per-user data (currently only DriveType.ini) in
//     %LOCALAPPDATA%\SharpSFV  (folder)
// (the local, non-roaming AppData folder: volume GUIDs mean nothing on another PC, so the
// data must not roam with the profile). Nothing is read from or written to that folder
// until the user has agreed through the consent message box. The decision itself is kept
// in SharpSFV.ini next to the executable (DataStorageConsent=Allowed|Declined), the
// app's existing settings file, so that saying "No" stores nothing in AppData.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the C++20 port.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "AppSettings.h"
#include "Types.h"

#include <string>
#include <vector>

namespace sfv {

class DataStorage {
public:
    // %LOCALAPPDATA%\SharpSFV (empty if the folder cannot be determined).
    static std::string DefaultDirectory();

    // `settings` must outlive this object; consent is read from and saved to it.
    explicit DataStorage(AppSettings& settings, std::string directory = DefaultDirectory());

    DataConsent Consent() const { return settings_.dataStorageConsent; }
    bool Allowed() const { return Consent() == DataConsent::Allowed && !dir_.empty(); }
    const std::string& Directory() const { return dir_; }
    std::string DriveTypeFile() const; // <dir>\DriveType.ini

    // Records the decision in SharpSFV.ini. Withdrawing (Declined) also deletes everything
    // SharpSFV stored in the folder; the deleted paths are returned in `removed`.
    // Returns false (with error) if SharpSFV.ini could not be saved.
    bool SetConsent(DataConsent c, std::vector<std::string>* removed = nullptr, std::string* error = nullptr);

    // Text of the consent prompt (shared by the GUI and the command line).
    static std::string ConsentTitle();
    std::string ConsentMessage() const;

    // Shows the prompt and records the answer. On Windows this is a Yes/No message box
    // (owner may be null; "No" is the default button). Elsewhere it asks on the console.
    // Returns the decision (unchanged if it could not be recorded).
    DataConsent Ask(void* ownerWindow = nullptr, std::string* error = nullptr);

private:
    AppSettings& settings_;
    std::string dir_;
};

} // namespace sfv
