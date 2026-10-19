// SharpSFV (C++ port) - main window: user commands.
// Ported from Form1.Actions.cs.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "MainWindow.h"
#include "SortUtil.h"

#include "ChecksumWriter.h"
#include "DataStorage.h"
#include "Dialogs.h"
#include "DriveDetector.h"
#include "FileSystem.h"
#include "PathUtil.h"
#include "Scripts.h"
#include "Text.h"

#include <windowsx.h>

#include <algorithm>
#include <ctime>
#include <set>

namespace sfv::gui {

namespace {
bool MovePath(const std::string& from, const std::string& to) {
    return MoveFileExW(fs::ToWinPath(from).c_str(), fs::ToWinPath(to).c_str(), MOVEFILE_COPY_ALLOWED) != 0;
}

std::string LastError() {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   GetLastError(), 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    std::string s = msg ? std::string(text::Trim(N(msg))) : std::string("Unknown error");
    if (msg) LocalFree(msg);
    return s;
}

std::string Timestamp() { // DateTime.Now.ToString("yyyyMMddHHmmss")
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%d%H%M%S", &tm);
    return buf;
}

bool IsHex(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}
} // namespace

std::string MainWindow::CurrentRootDirectory() const {
    if (store_.Count() == 0) return {};
    if (!store_.BaseDirectory(0).empty()) return store_.BaseDirectory(0);
    return path::GetDirectoryName(store_.GetFullPath(0)).value_or("");
}

std::vector<int> MainWindow::BadItems() const {
    std::vector<int> out;
    const int n = store_.Count();
    for (int i = 0; i < n; ++i) {
        const ItemStatus s = store_.Status(i);
        if (!store_.IsSummaryRow(i) && (s == ItemStatus::Bad || s == ItemStatus::Error)) out.push_back(i);
    }
    return out;
}

// ------------------------------------------------------------------ File

void MainWindow::ActionOpen() {
    const auto file = OpenFileDialog(hwnd_,
                                     "Hash Files (*.xxh3;*.md5;*.sfv;*.sha1;*.sha256;*.txt)|*.xxh3;*.md5;*.sfv;*.sha1;"
                                     "*.sha256;*.txt|All Files (*.*)|*.*",
                                     CurrentRootDirectory());
    if (file) HandlePaths({*file});
}

void MainWindow::ActionSave() {
    if (jobMode_) {
        MsgBox(hwnd_, "Job Mode saves checksums automatically upon completion.", "SharpSFV", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (store_.Count() == 0) {
        MsgBox(hwnd_, "No files to save.", "SharpSFV", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::string filter;
    switch (currentAlgo_) {
        case HashType::Crc32: filter = "SFV File (*.sfv)|*.sfv|Text File (*.txt)|*.txt"; break;
        case HashType::MD5: filter = "MD5 File (*.md5)|*.md5|Text File (*.txt)|*.txt"; break;
        case HashType::SHA1: filter = "SHA1 File (*.sha1)|*.sha1|Text File (*.txt)|*.txt"; break;
        case HashType::SHA256: filter = "SHA256 File (*.sha256)|*.sha256|Text File (*.txt)|*.txt"; break;
        default: filter = "xxHash3 File (*.xxh3)|*.xxh3|Text File (*.txt)|*.txt"; break;
    }
    const auto chosen = SaveFileDialog(hwnd_, filter, std::string("checksums") + ExtensionForAlgo(currentAlgo_),
                                       CurrentRootDirectory());
    if (!chosen) return;
    std::string err;
    if (!WriteSaveAs(chosen->path, store_, settings_, currentAlgo_, &err))
        MsgBox(hwnd_, "Error saving checksum file: " + err, "SharpSFV", MB_OK | MB_ICONERROR);
}

// ------------------------------------------------------------------ Edit

void MainWindow::ActionCopy() {
    const auto rows = SelectedRows();
    if (rows.empty()) return;
    std::string out;
    if (jobMode_) {
        for (int r : rows) {
            if (auto job = jobs_.Get(r))
                out += job->name + "\t" + ToString(job->status.load()) + "\t" + text::FormatF1(job->progress.load()) + "%\r\n";
        }
    } else {
        // "<HASH> *<PATH>"
        for (int r : rows) {
            if (r >= static_cast<int>(display_.size())) continue;
            const int i = display_[static_cast<std::size_t>(r)];
            std::string hash = store_.GetCalculatedHashString(i, settings_.crc32ByteOrder);
            if (hash == "Pending") hash.clear();
            const std::string p = settings_.showFullPaths ? store_.GetFullPath(i)
                                  : !store_.RelativePath(i).empty() ? store_.RelativePath(i)
                                                                     : store_.FileName(i);
            out += hash + " *" + p + "\r\n";
        }
    }
    SetClipboardText(hwnd_, out);
}

void MainWindow::ActionPaste() {
    const auto clip = GetClipboardText(hwnd_);
    if (!clip || text::IsNullOrWhiteSpace(*clip)) return;
    std::vector<std::string> paths;
    for (const auto& line : text::SplitLines(*clip)) {
        std::string p(text::Trim(line));
        while (!p.empty() && p.front() == '"') p.erase(p.begin());
        while (!p.empty() && p.back() == '"') p.pop_back();
        if (!p.empty() && (fs::FileExists(p) || fs::DirectoryExists(p))) paths.push_back(p);
    }
    if (!paths.empty() && !(processing_ && !jobMode_)) HandlePaths(std::move(paths));
}

void MainWindow::ActionCompareClipboard() {
    // Compare the selected file's hash with a hex string from the clipboard (e.g. a website).
    if (jobMode_) return;
    std::string clip(text::Trim(GetClipboardText(hwnd_).value_or("")));
    if (clip.empty()) return;
    if (!IsHex(clip)) {
        MsgBox(hwnd_, "Clipboard does not contain a valid Hex hash.", "Invalid Hash", MB_OK | MB_ICONWARNING);
        return;
    }
    int target = -1;
    const auto rows = SelectedRows();
    if (rows.size() == 1 && rows[0] < static_cast<int>(display_.size())) target = display_[static_cast<std::size_t>(rows[0])];
    else if (store_.Count() == 1) target = 0;
    if (target < 0) {
        MsgBox(hwnd_, "Please select a single file to compare, or filter the list to one item.", "Selection Required",
               MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (clip.size() % 2 != 0) clip = "0" + clip;
    std::vector<std::uint8_t> expected;
    if (!TryFromHex(clip, expected)) {
        MsgBox(hwnd_, "Could not parse clipboard text as a Hex string.", "Parse Error", MB_OK | MB_ICONERROR);
        return;
    }
    store_.SetExpected(target, expected);
    const Digest& calc = store_.Calculated(target);
    if (!calc.empty() && !processing_) {
        // CRC-32 is accepted in either byte order, as in verification.
        const DigestMatch m = CompareDigest(calc, expected, calc.size == 4);
        store_.SetStatus(target, m == DigestMatch::Mismatch ? ItemStatus::Bad : ItemStatus::OK);
        if (calc.size == 4) // the Hash column then shows the CRC in the order that matched
            store_.SetCrcMatch(target, m == DigestMatch::Exact          ? CrcMatch::Standard
                                       : m == DigestMatch::ByteReversed ? CrcMatch::Reversed
                                                                        : CrcMatch::None);
        MessageBeep(m == DigestMatch::Mismatch ? MB_ICONHAND : MB_ICONASTERISK);
        RecountStats();
    }
    InvalidateRect(list_, nullptr, FALSE);
    SyncMenu();
}

void MainWindow::ActionSelectAll() {
    if (!list_) return;
    ListView_SetItemState(list_, -1, LVIS_SELECTED, LVIS_SELECTED);
}

// ------------------------------------------------------------------ context menu

void MainWindow::ActionOpenFolder() {
    const auto rows = SelectedRows();
    if (rows.size() != 1 || rows[0] >= static_cast<int>(display_.size())) return;
    ShowInExplorer(store_.GetFullPath(display_[static_cast<std::size_t>(rows[0])]));
}

void MainWindow::ActionRename() {
    const auto rows = SelectedRows();
    if (processing_ || rows.size() != 1 || rows[0] >= static_cast<int>(display_.size())) return;
    const int i = display_[static_cast<std::size_t>(rows[0])];
    const std::string currentPath = store_.GetFullPath(i);
    const std::string currentName = store_.FileName(i);
    const auto newName = InputDialog(hwnd_, "Rename File", "Enter new filename:", currentName);
    if (!newName || text::IsNullOrWhiteSpace(*newName) || *newName == currentName) return;
    const std::string dir = path::GetDirectoryName(currentPath).value_or(store_.BaseDirectory(i));
    if (!MovePath(currentPath, path::Combine(dir, *newName))) {
        MsgBox(hwnd_, LastError(), "SharpSFV", MB_OK | MB_ICONERROR);
        return;
    }
    const std::string oldRel = store_.RelativePath(i).empty() ? currentName : store_.RelativePath(i);
    const std::string relDir = path::GetDirectoryName(oldRel).value_or("");
    store_.SetFileName(i, *newName);
    store_.SetRelativePath(i, relDir.empty() ? *newName : path::Combine(relDir, *newName));
    InvalidateRect(list_, nullptr, FALSE);
}

void MainWindow::RemoveStoreItems(std::vector<int> storeIndices) {
    StableSortIndices(storeIndices, [](int a, int b) { return a < b; });
    storeIndices.erase(std::unique(storeIndices.begin(), storeIndices.end()), storeIndices.end());
    if (storeIndices.empty()) return;
    store_.RemoveMany(storeIndices);
    RemapAfterRemoval(display_, storeIndices); // keeps the current filter and sort order
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    RefreshItemCount(true);
    RecountStats();
    SyncMenu();
}

void MainWindow::ActionDelete() {
    const auto rows = SelectedRows();
    if (processing_ || rows.empty()) return;
    if (MsgBox(hwnd_, "Delete " + std::to_string(rows.size()) + " file(s) from disk?", "Confirm Delete",
               MB_YESNO | MB_ICONWARNING) != IDYES)
        return;
    std::vector<int> removed;
    int failed = 0;
    for (int r : rows) {
        if (r >= static_cast<int>(display_.size())) continue;
        const int i = display_[static_cast<std::size_t>(r)];
        const std::string full = store_.GetFullPath(i);
        if (!fs::FileExists(full) || fs::RemoveFile(full)) removed.push_back(i);
        else ++failed;
    }
    RemoveStoreItems(std::move(removed));
    if (failed > 0) MsgBox(hwnd_, std::to_string(failed) + " file(s) could not be deleted.", "SharpSFV", MB_OK | MB_ICONWARNING);
}

void MainWindow::ActionRemoveFromList() {
    if (processing_) return;
    std::vector<int> idx;
    for (int r : SelectedRows())
        if (r < static_cast<int>(display_.size())) idx.push_back(display_[static_cast<std::size_t>(r)]);
    RemoveStoreItems(std::move(idx));
}

// ------------------------------------------------------------------ bad file / duplicate tools

void MainWindow::ActionMoveBadFiles() {
    if (processing_ || store_.Count() == 0) return;
    if (MsgBox(hwnd_,
               "This will move all files marked as BAD or ERROR to a '_BAD_FILES' subfolder in their respective "
               "locations.\n\nContinue?",
               "Move Bad Files", MB_YESNO | MB_ICONWARNING) != IDYES)
        return;
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    int moved = 0, errors = 0;
    for (int i : BadItems()) {
        const std::string full = store_.GetFullPath(i);
        if (!fs::FileExists(full)) continue;
        const auto dir = path::GetDirectoryName(full);
        if (!dir) continue;
        const std::string badDir = path::Combine(*dir, "_BAD_FILES");
        const std::string name(path::GetFileName(full));
        std::string dest = path::Combine(badDir, name);
        if (!fs::CreateDirectories(badDir)) {
            ++errors;
            continue;
        }
        if (fs::FileExists(dest)) // name collision: append a timestamp
            dest = path::Combine(badDir, std::string(path::GetFileNameWithoutExtension(name)) + "_" + Timestamp() +
                                             std::string(path::GetExtension(name)));
        if (!MovePath(full, dest)) {
            ++errors;
            continue;
        }
        const std::string relDir = path::GetDirectoryName(store_.RelativePath(i)).value_or("");
        store_.SetRelativePath(i, relDir.empty() ? path::Combine("_BAD_FILES", name)
                                                 : path::Combine(path::Combine(relDir, "_BAD_FILES"), name));
        ++moved;
    }
    SetCursor(old);
    InvalidateRect(list_, nullptr, FALSE);
    MsgBox(hwnd_, "Operation Complete.\nMoved: " + std::to_string(moved) + "\nErrors: " + std::to_string(errors),
           "Move Bad Files", MB_OK | MB_ICONINFORMATION);
}

void MainWindow::ActionRenameBadFiles() {
    if (processing_ || store_.Count() == 0) return;
    if (MsgBox(hwnd_, "This will append '.CORRUPT' to all files marked as BAD or ERROR.\n\nContinue?", "Rename Bad Files",
               MB_YESNO | MB_ICONWARNING) != IDYES)
        return;
    int renamed = 0, errors = 0;
    for (int i : BadItems()) {
        const std::string full = store_.GetFullPath(i);
        if (!fs::FileExists(full)) continue;
        if (!MovePath(full, full + ".CORRUPT")) {
            ++errors;
            continue;
        }
        store_.SetFileName(i, store_.FileName(i) + ".CORRUPT");
        store_.SetRelativePath(i, store_.RelativePath(i) + ".CORRUPT");
        ++renamed;
    }
    InvalidateRect(list_, nullptr, FALSE);
    MsgBox(hwnd_, "Operation Complete.\nRenamed: " + std::to_string(renamed) + "\nErrors: " + std::to_string(errors),
           "Rename Bad Files", MB_OK | MB_ICONINFORMATION);
}

std::optional<std::string> MainWindow::SaveScriptDialog(const char* defaultName) {
    const auto chosen = SaveFileDialog(hwnd_, "Batch Script (*.bat)|*.bat", defaultName, CurrentRootDirectory());
    if (!chosen) return std::nullopt;
    return chosen->path;
}

namespace {
void WriteScript(HWND owner, const std::string& file, const std::string& content) {
    fs::TextWriter w;
    std::string err;
    if (!w.Open(file, &err)) {
        MsgBox(owner, "Error saving script: " + err, "SharpSFV", MB_OK | MB_ICONERROR);
        return;
    }
    w.Write(content);
    if (!w.Close(&err)) MsgBox(owner, "Error saving script: " + err, "SharpSFV", MB_OK | MB_ICONERROR);
}
} // namespace

void MainWindow::ActionBadFilesScript() {
    const auto bad = BadItems();
    if (bad.empty()) return;
    const auto file = SaveScriptDialog("delete_bad_files.bat");
    if (!file) return;
    std::vector<std::string> paths;
    for (int i : bad) paths.push_back(store_.GetFullPath(i));
    WriteScript(hwnd_, *file, DeleteBadFilesScript(paths));
}

void MainWindow::ActionDuplicateScript(bool copy) {
    if (store_.Count() == 0 || Button_GetCheck(chkDups_) != BST_CHECKED) return;
    const auto file = SaveScriptDialog(copy ? "copy_duplicates.bat" : "delete_duplicates.bat");
    if (!file) return;
    std::vector<std::vector<std::string>> groups;
    for (const auto& g : DuplicateGroups(store_)) {
        std::vector<std::string> paths;
        for (int i : g) paths.push_back(store_.GetFullPath(i));
        groups.push_back(std::move(paths));
    }
    WriteScript(hwnd_, *file, copy ? CopyDuplicatesScript(groups) : DeleteDuplicatesScript(groups));
}

// ------------------------------------------------------------------ Options / Help

void MainWindow::ActionDataStorage() {
    DataStorage storage(settings_);
    if (!storage.Allowed()) {
        storage.Ask(hwnd_);
        return;
    }
    const auto records = DriveTypeStore(storage.DriveTypeFile()).Load();
    const std::string msg = "Local data storage is allowed.\n\nDrive classifications are kept in:\n" +
                            storage.DriveTypeFile() + "\n(" + std::to_string(records.size()) +
                            " drive(s) recorded)\n\nWithdraw permission? This deletes DriveType.ini; drives will then "
                            "be detected every time and per-drive settings are no longer kept.";
    if (MsgBox(hwnd_, msg, DataStorage::ConsentTitle(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    std::vector<std::string> removed;
    std::string err;
    if (!storage.SetConsent(DataConsent::Declined, &removed, &err)) {
        MsgBox(hwnd_, err, "SharpSFV", MB_OK | MB_ICONERROR);
        return;
    }
    std::string done = "Local data storage declined. Nothing will be stored.";
    for (const auto& f : removed) done += "\n\nDeleted: " + f;
    MsgBox(hwnd_, done, DataStorage::ConsentTitle(), MB_OK | MB_ICONINFORMATION);
}

void MainWindow::ActionRegisterShell(bool reg) {
    std::string err;
    if (reg) {
        if (RegisterShell(&err))
            MsgBox(hwnd_,
                   "Shell integration registered successfully!\n\nYou can now Right-Click files or folders to create "
                   "checksums.",
                   "SharpSFV", MB_OK | MB_ICONINFORMATION);
        else
            MsgBox(hwnd_, "Error registering shell extension: " + err, "Error", MB_OK | MB_ICONERROR);
    } else {
        if (UnregisterShell(&err)) MsgBox(hwnd_, "Shell integration removed.", "SharpSFV", MB_OK | MB_ICONINFORMATION);
        else MsgBox(hwnd_, "Error removing shell extension: " + err, "Error", MB_OK | MB_ICONERROR);
    }
}

void MainWindow::ActionInstall(bool install) {
    const std::string dir = InstallDirectory();
    std::string err;
    std::vector<std::string> files;
    if (install) {
        if (MsgBox(hwnd_,
                   "Install SharpSFV for the command line?\n\nThis copies SharpSFV.exe (and SharpSFV-cli.exe, if it is "
                   "next to it) and your settings to:\n" + dir +
                       "\nand adds that folder to your user PATH, so scripts can run \"SharpSFV-cli ...\" or "
                       "\"SharpSFV -headless ...\" from any folder.\n\nNothing else is changed; Uninstall reverses it.",
                   "Install for Command Line", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        SaveSettings(); // the installed copy starts with the current settings
        if (!InstallForCommandLine(&files, &err)) {
            MsgBox(hwnd_, "Install failed: " + err, "SharpSFV", MB_OK | MB_ICONERROR);
            return;
        }
        MsgBox(hwnd_, "Installed in " + dir + ".\n\nOpen a new command prompt to use it.", "SharpSFV",
               MB_OK | MB_ICONINFORMATION);
    } else {
        if (MsgBox(hwnd_, "Remove " + dir + " from your user PATH and delete the copies of SharpSFV there?",
                   "Uninstall from Command Line", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return;
        UninstallForCommandLine(&files, &err);
        std::string msg = "Removed from PATH.";
        for (const auto& f : files) msg += "\nDeleted: " + f;
        if (!err.empty()) msg += "\n\n" + err;
        MsgBox(hwnd_, msg, "SharpSFV", MB_OK | MB_ICONINFORMATION);
    }
}

void MainWindow::ActionDiskInfo() {
    const std::string p = store_.Count() > 0 ? store_.GetFullPath(0) : ModulePath();
    DataStorage storage(settings_);
    const DriveResolver drives(&storage);
    const auto st = drives.Inspect(settings_.processingMode, p);
    std::string t = "In use: " + DescribeDecision(st.inUse) + "\n";
    if (!storage.Allowed()) t += "Stored: - (local data storage not allowed)\n";
    else if (st.stored) t += std::string("Stored: ") + (st.stored->isHdd ? "HDD" : "SSD") +
                             (st.stored->userSet ? " (your setting)" : " (detected " + st.stored->recorded + ")") + "\n";
    else t += "Stored: not yet\n";
    t += "\n" + drive::GetDebugInfo(p);
    TextDialog(hwnd_, "Disk Debug Info", t);
}

void MainWindow::ActionResetDefaults() {
    if (MsgBox(hwnd_,
               "Reset all settings to their defaults and restart SharpSFV?\n\nThis deletes SharpSFV.ini (including the "
               "local data storage decision, which will be asked again). DriveType.ini is kept.",
               "Reset to Default Values", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;
    settings_.ResetToDefaults();
    skipSaveOnClose_ = true;
    RestartSelf();
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

} // namespace sfv::gui
