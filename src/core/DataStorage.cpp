// SharpSFV (C++ port) - local data storage and the user's consent to it.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the C++20 port.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "DataStorage.h"

#include "Console.h"
#include "FileSystem.h"
#include "PathUtil.h"
#include "Text.h"

#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sfv {

namespace {
// Every file SharpSFV may create in the data folder. Withdrawing consent deletes these.
constexpr const char* kStoredFiles[] = {"DriveType.ini", "DriveType.ini.tmp"};
} // namespace

std::string DataStorage::DefaultDirectory() {
    const std::string base = fs::LocalAppDataDirectory();
    return base.empty() ? std::string() : path::Combine(base, "SharpSFV");
}

DataStorage::DataStorage(AppSettings& settings, std::string directory) : settings_(settings), dir_(std::move(directory)) {}

std::string DataStorage::DriveTypeFile() const { return dir_.empty() ? std::string() : path::Combine(dir_, "DriveType.ini"); }

bool DataStorage::SetConsent(DataConsent c, std::vector<std::string>* removed, std::string* error) {
    const DataConsent previous = settings_.dataStorageConsent;
    settings_.dataStorageConsent = c;
    if (!settings_.SaveCurrent()) {
        settings_.dataStorageConsent = previous;
        if (error) *error = "Could not save " + settings_.IniPath() + " (is the program folder writable?)";
        return false;
    }
    if (c == DataConsent::Declined && !dir_.empty()) {
        fs::CrossProcessLock lock("SharpSFV_DataStorage", dir_);
        for (const char* name : kStoredFiles) {
            const std::string f = path::Combine(dir_, name);
            if (fs::FileExists(f) && fs::RemoveFile(f) && removed) removed->push_back(f);
        }
        // Remove the folder too if nothing else lives there (the self-installer, if used,
        // keeps SharpSFV.exe in the same folder; that is left alone).
        fs::RemoveDirectoryIfEmpty(dir_);
    }
    return true;
}

std::string DataStorage::ConsentTitle() { return "SharpSFV - Local Data Storage"; }

std::string DataStorage::ConsentMessage() const {
    const std::string file = DriveTypeFile().empty() ? std::string("(local app data folder)\\SharpSFV\\DriveType.ini")
                                                     : DriveTypeFile();
    // No hard line breaks inside paragraphs: the message box wraps text to its own width.
    return "SharpSFV would like to save a small settings file on this computer:\n"
           "\n" + file + "\n"
           "\n"
           "For each drive you check files on, it records:\n"
           "\u2022 the drive's volume GUID, serial number, label and drive letter\n"
           "\u2022 whether to read it as an SSD (in parallel) or an HDD (one file at a time)\n"
           "\u2022 whether that came from automatic detection or from your own setting\n"
           "\n"
           "Each drive is then detected only once, and your own setting for a drive is always used. "
           "This helps with encrypted drives (such as VeraCrypt) that can't be detected reliably.\n"
           "\n"
           "The file stays on this computer. You can withdraw permission at any time, which deletes it. "
           "If you choose No, nothing is stored: drives are detected every time, and the Processing Mode "
           "option (Auto / HDD / SSD) still works.\n"
           "\n"
           "Allow SharpSFV to store this file?";
}

DataConsent DataStorage::Ask(void* ownerWindow, std::string* error) {
#ifdef _WIN32
    const std::wstring msg = text::Widen(ConsentMessage());
    const std::wstring title = text::Widen(ConsentTitle());
    const int r = MessageBoxW(static_cast<HWND>(ownerWindow), msg.c_str(), title.c_str(),
                              MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_SETFOREGROUND);
    const DataConsent answer = r == IDYES ? DataConsent::Allowed : DataConsent::Declined;
#else
    (void)ownerWindow;
    console::WriteLine(ConsentTitle());
    console::WriteLine();
    console::WriteLine(ConsentMessage());
    console::Write("[y/N] ");
    char buf[64] = {};
    std::string line = std::fgets(buf, sizeof buf, stdin) ? buf : "";
    const std::string_view a = text::Trim(line);
    const DataConsent answer = (text::EqualsIgnoreCase(a, "y") || text::EqualsIgnoreCase(a, "yes"))
                                   ? DataConsent::Allowed
                                   : DataConsent::Declined;
#endif
    if (!SetConsent(answer, nullptr, error)) return Consent();
    return answer;
}

} // namespace sfv
