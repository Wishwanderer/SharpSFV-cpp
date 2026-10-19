// SharpSFV (lean C++ rewrite) - main window: user commands.
// Port of src/gui/MainWindowActions.cpp (Form1.Actions.cs).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "MainWindow.h"

#include "../base/FileSystem.h"
#include "../base/Path.h"
#include "../core/ChecksumFile.h"
#include "../core/Drive.h"
#include "../core/Scripts.h"
#include "Dialogs.h"

#include <windowsx.h>

namespace sfv::gui {

namespace {
bool MovePath(StrView from, StrView to) {
    return MoveFileExW(fs::ToWinPath(from).C(), fs::ToWinPath(to).C(), MOVEFILE_COPY_ALLOWED) != 0;
}

Str LastError() {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   GetLastError(), 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    Str s = msg ? Str(text::Trim(N(msg))) : Str("Unknown error");
    if (msg) LocalFree(msg);
    return s;
}

Str Timestamp() { // DateTime.Now.ToString("yyyyMMddHHmmss")
    SYSTEMTIME t;
    GetLocalTime(&t);
    const int parts[] = {t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond};
    Str s = ToStr(t.wYear);
    for (const int p : parts) s.Append(static_cast<char>('0' + p / 10)).Append(static_cast<char>('0' + p % 10));
    return s;
}

bool IsHex(StrView s) {
    if (s.Empty()) return false;
    for (usize i = 0; i < s.Size(); ++i) {
        const char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}
} // namespace

Str MainWindow::CurrentRootDirectory() const {
    if (store_.Count() == 0) return {};
    if (!store_.BaseDirectory(0).Empty()) return store_.BaseDirectory(0);
    return path::GetDirectoryName(store_.GetFullPath(0)).ValueOr(Str());
}

Vec<int> MainWindow::BadItems() const {
    Vec<int> out;
    const int n = store_.Count();
    for (int i = 0; i < n; ++i) {
        const ItemStatus s = store_.Status(i);
        if (s == ItemStatus::Bad || s == ItemStatus::Error) out.Push(i);
    }
    return out;
}

// ------------------------------------------------------------------ File

void MainWindow::ActionOpen() {
    const Opt<Str> file = OpenFileDialog(hwnd_,
                                         "Hash Files (*.xxh3;*.md5;*.sfv;*.sha1;*.sha256;*.txt)|*.xxh3;*.md5;*.sfv;*.sha1;"
                                         "*.sha256;*.txt|All Files (*.*)|*.*",
                                         CurrentRootDirectory());
    if (file) {
        Vec<Str> paths;
        paths.Push(*file);
        HandlePaths(Move(paths));
    }
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
    const char* filter;
    switch (currentAlgo_) {
        case HashType::Crc32: filter = "SFV File (*.sfv)|*.sfv|Text File (*.txt)|*.txt"; break;
        case HashType::MD5: filter = "MD5 File (*.md5)|*.md5|Text File (*.txt)|*.txt"; break;
        case HashType::SHA1: filter = "SHA1 File (*.sha1)|*.sha1|Text File (*.txt)|*.txt"; break;
        case HashType::SHA256: filter = "SHA256 File (*.sha256)|*.sha256|Text File (*.txt)|*.txt"; break;
        default: filter = "xxHash3 File (*.xxh3)|*.xxh3|Text File (*.txt)|*.txt"; break;
    }
    const Opt<SaveResult> chosen =
        SaveFileDialog(hwnd_, filter, Str("checksums") + ExtensionForAlgo(currentAlgo_), CurrentRootDirectory());
    if (!chosen) return;
    Str err;
    if (!WriteSaveAs(chosen->path, store_, settings_, currentAlgo_, &err))
        MsgBox(hwnd_, "Error saving checksum file: " + err, "SharpSFV", MB_OK | MB_ICONERROR);
}

// ------------------------------------------------------------------ Edit

void MainWindow::ActionCopy() {
    const Vec<int> rows = SelectedRows();
    if (rows.Empty()) return;
    Str out;
    if (jobMode_) {
        for (const int r : rows) {
            if (const JobRef job = jobs_.Get(r))
                out += job->name + "\t" + ToString(job->Status()) + "\t" + text::FormatF1(job->Progress()) + "%\r\n";
        }
    } else {
        // "<HASH> *<PATH>"
        for (const int r : rows) {
            if (r >= static_cast<int>(display_.Size())) continue;
            const int i = display_[static_cast<usize>(r)];
            Str hash = store_.GetCalculatedHashString(i, settings_.crc32ByteOrder);
            if (hash == "Pending") hash.Clear();
            const Str p = settings_.showFullPaths ? store_.GetFullPath(i)
                          : !store_.RelativePath(i).Empty() ? store_.RelativePath(i)
                                                             : store_.FileName(i);
            out += hash + " *" + p + "\r\n";
        }
    }
    SetClipboardText(hwnd_, out);
}

void MainWindow::ActionPaste() {
    const Opt<Str> clip = GetClipboardText(hwnd_);
    if (!clip || text::IsNullOrWhiteSpace(*clip)) return;
    Vec<Str> paths;
    for (const Str& line : text::SplitLines(*clip)) {
        StrView p = text::Trim(line);
        while (!p.Empty() && p[0] == '"') p = p.Sub(1);
        while (!p.Empty() && p[p.Size() - 1] == '"') p = p.Sub(0, p.Size() - 1);
        if (!p.Empty() && (fs::FileExists(p) || fs::DirectoryExists(p))) paths.Push(Str(p));
    }
    if (!paths.Empty() && !(processing_ && !jobMode_)) HandlePaths(Move(paths));
}

void MainWindow::ActionCompareClipboard() {
    // Compare the selected file's hash with a hex string from the clipboard (e.g. a website).
    if (jobMode_) return;
    const Opt<Str> raw = GetClipboardText(hwnd_);
    Str clip(text::Trim(raw ? StrView(*raw) : StrView()));
    if (clip.Empty()) return;
    if (!IsHex(clip)) {
        MsgBox(hwnd_, "Clipboard does not contain a valid Hex hash.", "Invalid Hash", MB_OK | MB_ICONWARNING);
        return;
    }
    int target = -1;
    const Vec<int> rows = SelectedRows();
    if (rows.Size() == 1 && rows[0] < static_cast<int>(display_.Size())) target = display_[static_cast<usize>(rows[0])];
    else if (store_.Count() == 1) target = 0;
    if (target < 0) {
        MsgBox(hwnd_, "Please select a single file to compare, or filter the list to one item.", "Selection Required",
               MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (clip.Size() % 2 != 0) clip = "0" + clip;
    Vec<u8> expected;
    if (!TryFromHex(clip, expected)) {
        MsgBox(hwnd_, "Could not parse clipboard text as a Hex string.", "Parse Error", MB_OK | MB_ICONERROR);
        return;
    }
    store_.SetExpected(target, expected);
    const Digest& calc = store_.Calculated(target);
    if (!calc.Empty() && !processing_) {
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
    const Vec<int> rows = SelectedRows();
    if (rows.Size() != 1 || rows[0] >= static_cast<int>(display_.Size())) return;
    ShowInExplorer(store_.GetFullPath(display_[static_cast<usize>(rows[0])]));
}

void MainWindow::ActionRename() {
    const Vec<int> rows = SelectedRows();
    if (processing_ || rows.Size() != 1 || rows[0] >= static_cast<int>(display_.Size())) return;
    const int i = display_[static_cast<usize>(rows[0])];
    const Str currentPath = store_.GetFullPath(i);
    const Str currentName = store_.FileName(i);
    const Opt<Str> newName = InputDialog(hwnd_, "Rename File", "Enter new filename:", currentName);
    if (!newName || text::IsNullOrWhiteSpace(*newName) || *newName == currentName) return;
    const Str dir = path::GetDirectoryName(currentPath).ValueOr(store_.BaseDirectory(i));
    if (!MovePath(currentPath, path::Combine(dir, *newName))) {
        MsgBox(hwnd_, LastError(), "SharpSFV", MB_OK | MB_ICONERROR);
        return;
    }
    const Str oldRel = store_.RelativePath(i).Empty() ? currentName : store_.RelativePath(i);
    const Str relDir = path::GetDirectoryName(oldRel).ValueOr(Str());
    store_.SetFileName(i, *newName);
    store_.SetRelativePath(i, relDir.Empty() ? *newName : path::Combine(relDir, *newName));
    InvalidateRect(list_, nullptr, FALSE);
}

void MainWindow::RemoveStoreItems(Vec<int> storeIndices) {
    StableSortIndices(storeIndices, [](int a, int b) { return a < b; });
    Vec<int> unique;
    for (const int i : storeIndices)
        if (unique.Empty() || unique.Back() != i) unique.Push(i);
    if (unique.Empty()) return;
    store_.RemoveMany(unique);
    RemapAfterRemoval(display_, unique); // keeps the current filter and sort order
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    RefreshItemCount(true);
    RecountStats();
    SyncMenu();
}

void MainWindow::ActionDelete() {
    const Vec<int> rows = SelectedRows();
    if (processing_ || rows.Empty()) return;
    if (MsgBox(hwnd_, "Delete " + ToStr(static_cast<i64>(rows.Size())) + " file(s) from disk?", "Confirm Delete",
               MB_YESNO | MB_ICONWARNING) != IDYES)
        return;
    Vec<int> removed;
    int failed = 0;
    for (const int r : rows) {
        if (r >= static_cast<int>(display_.Size())) continue;
        const int i = display_[static_cast<usize>(r)];
        const Str full = store_.GetFullPath(i);
        if (!fs::FileExists(full) || fs::RemoveFile(full)) removed.Push(i);
        else ++failed;
    }
    RemoveStoreItems(Move(removed));
    if (failed > 0) MsgBox(hwnd_, ToStr(failed) + " file(s) could not be deleted.", "SharpSFV", MB_OK | MB_ICONWARNING);
}

void MainWindow::ActionRemoveFromList() {
    if (processing_) return;
    Vec<int> idx;
    for (const int r : SelectedRows())
        if (r < static_cast<int>(display_.Size())) idx.Push(display_[static_cast<usize>(r)]);
    RemoveStoreItems(Move(idx));
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
    for (const int i : BadItems()) {
        const Str full = store_.GetFullPath(i);
        if (!fs::FileExists(full)) continue;
        const Opt<Str> dir = path::GetDirectoryName(full);
        if (!dir) continue;
        const Str badDir = path::Combine(*dir, "_BAD_FILES");
        const Str name(path::GetFileName(full));
        Str dest = path::Combine(badDir, name);
        if (!fs::CreateDirectories(badDir)) {
            ++errors;
            continue;
        }
        if (fs::FileExists(dest)) // name collision: append a timestamp
            dest = path::Combine(badDir, Str(path::GetFileNameWithoutExtension(name)) + "_" + Timestamp() +
                                             path::GetExtension(name));
        if (!MovePath(full, dest)) {
            ++errors;
            continue;
        }
        const Str relDir = path::GetDirectoryName(store_.RelativePath(i)).ValueOr(Str());
        store_.SetRelativePath(i, relDir.Empty() ? path::Combine("_BAD_FILES", name)
                                                 : path::Combine(path::Combine(relDir, "_BAD_FILES"), name));
        ++moved;
    }
    SetCursor(old);
    InvalidateRect(list_, nullptr, FALSE);
    MsgBox(hwnd_, "Operation Complete.\nMoved: " + ToStr(moved) + "\nErrors: " + ToStr(errors), "Move Bad Files",
           MB_OK | MB_ICONINFORMATION);
}

void MainWindow::ActionRenameBadFiles() {
    if (processing_ || store_.Count() == 0) return;
    if (MsgBox(hwnd_, "This will append '.CORRUPT' to all files marked as BAD or ERROR.\n\nContinue?", "Rename Bad Files",
               MB_YESNO | MB_ICONWARNING) != IDYES)
        return;
    int renamed = 0, errors = 0;
    for (const int i : BadItems()) {
        const Str full = store_.GetFullPath(i);
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
    MsgBox(hwnd_, "Operation Complete.\nRenamed: " + ToStr(renamed) + "\nErrors: " + ToStr(errors), "Rename Bad Files",
           MB_OK | MB_ICONINFORMATION);
}

Opt<Str> MainWindow::SaveScriptDialog(const char* defaultName) {
    const Opt<SaveResult> chosen = SaveFileDialog(hwnd_, "Batch Script (*.bat)|*.bat", defaultName, CurrentRootDirectory());
    if (!chosen) return {};
    return chosen->path;
}

namespace {
void WriteScript(HWND owner, StrView file, StrView content) {
    fs::TextWriter w;
    Str err;
    if (!w.Open(file, &err)) {
        MsgBox(owner, "Error saving script: " + err, "SharpSFV", MB_OK | MB_ICONERROR);
        return;
    }
    w.Write(content);
    if (!w.Close(&err)) MsgBox(owner, "Error saving script: " + err, "SharpSFV", MB_OK | MB_ICONERROR);
}
} // namespace

void MainWindow::ActionBadFilesScript() {
    const Vec<int> bad = BadItems();
    if (bad.Empty()) return;
    const Opt<Str> file = SaveScriptDialog("delete_bad_files.bat");
    if (!file) return;
    Vec<Str> paths;
    for (const int i : bad) paths.Push(store_.GetFullPath(i));
    WriteScript(hwnd_, *file, DeleteBadFilesScript(paths));
}

void MainWindow::ActionDuplicateScript(bool copy) {
    if (store_.Count() == 0 || Button_GetCheck(chkDups_) != BST_CHECKED) return;
    const Opt<Str> file = SaveScriptDialog(copy ? "copy_duplicates.bat" : "delete_duplicates.bat");
    if (!file) return;
    Vec<Vec<Str>> groups;
    for (const Vec<int>& g : DuplicateGroups(store_)) {
        Vec<Str> paths;
        for (const int i : g) paths.Push(store_.GetFullPath(i));
        groups.Push(Move(paths));
    }
    WriteScript(hwnd_, *file, copy ? CopyDuplicatesScript(groups) : DeleteDuplicatesScript(groups));
}

// ------------------------------------------------------------------ Options / Help

void MainWindow::ActionDataStorage() {
    DataStorage storage(settings_);
    if (!storage.Allowed()) {
        storage.AskOwned(hwnd_);
        return;
    }
    const Vec<DriveRecord> records = DriveTypeStore(storage.DriveTypeFile()).Load();
    const Str msg = "Local data storage is allowed.\n\nDrive classifications are kept in:\n" + storage.DriveTypeFile() +
                    "\n(" + ToStr(static_cast<i64>(records.Size())) +
                    " drive(s) recorded)\n\nWithdraw permission? This deletes DriveType.ini; drives will then "
                    "be detected every time and per-drive settings are no longer kept.";
    if (MsgBox(hwnd_, msg, DataStorage::ConsentTitle(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    Vec<Str> removed;
    Str err;
    if (!storage.SetConsent(DataConsent::Declined, &removed, &err)) {
        MsgBox(hwnd_, err, "SharpSFV", MB_OK | MB_ICONERROR);
        return;
    }
    Str done = "Local data storage declined. Nothing will be stored.";
    for (const Str& f : removed) done += "\n\nDeleted: " + f;
    MsgBox(hwnd_, done, DataStorage::ConsentTitle(), MB_OK | MB_ICONINFORMATION);
}

void MainWindow::ActionRegisterShell(bool reg) {
    Str err;
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
    const Str dir = InstallDirectory();
    Str err;
    Vec<Str> files;
    if (install) {
        if (MsgBox(hwnd_,
                   "Install SharpSFV for the command line?\n\nThis copies SharpSFV.exe (and SharpSFV-cli.exe, if it is "
                   "next to it) and your settings to:\n" +
                       dir +
                       "\nand adds that folder to your user PATH, so scripts can run \"SharpSFV-cli ...\" or "
                       "\"SharpSFV -headless ...\" from any folder.\n\nNothing else is changed; Uninstall reverses it.",
                   "Install for Command Line", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        SaveSettings(); // the installed copy starts with the current settings
        if (!InstallForCommandLine(&files, &err)) {
            MsgBox(hwnd_, "Install failed: " + err, "SharpSFV", MB_OK | MB_ICONERROR);
            return;
        }
        MsgBox(hwnd_, "Installed in " + dir + ".\n\nOpen a new command prompt to use it.", "SharpSFV", MB_OK | MB_ICONINFORMATION);
    } else {
        if (MsgBox(hwnd_, "Remove " + dir + " from your user PATH and delete the copies of SharpSFV there?",
                   "Uninstall from Command Line", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return;
        UninstallForCommandLine(&files, &err);
        Str msg = "Removed from PATH.";
        for (const Str& f : files) msg += "\nDeleted: " + f;
        if (!err.Empty()) msg += "\n\n" + err;
        MsgBox(hwnd_, msg, "SharpSFV", MB_OK | MB_ICONINFORMATION);
    }
}

void MainWindow::ActionDiskInfo() {
    const Str p = store_.Count() > 0 ? store_.GetFullPath(0) : ModulePath();
    DataStorage storage(settings_);
    const DriveResolver drives(&storage);
    const DriveResolver::Status st = drives.Inspect(settings_.processingMode, p);
    Str t = "In use: " + DescribeDecision(st.inUse) + "\n";
    if (!storage.Allowed()) t += "Stored: - (local data storage not allowed)\n";
    else if (st.stored)
        t += Str("Stored: ") + (st.stored->isHdd ? "HDD" : "SSD") +
             (st.stored->userSet ? Str(" (your setting)") : " (detected " + st.stored->recorded + ")") + "\n";
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
    if (fs::FileExists(settings_.IniPath())) fs::RemoveFile(settings_.IniPath());
    skipSaveOnClose_ = true;
    RestartSelf();
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

} // namespace sfv::gui
