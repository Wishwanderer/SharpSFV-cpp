// SharpSFV (C++ port) - modal dialogs.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Dialogs.h"

#include "DataStorage.h"
#include "DriveClassifier.h"
#include "Text.h"
#include "resource.h"

#include <commctrl.h>

#include <vector>

namespace sfv::gui {

namespace {

template <typename T>
T* DlgState(HWND h) { return reinterpret_cast<T*>(GetWindowLongPtrW(h, DWLP_USER)); }

// ------------------------------------------------------------------ input

struct InputState {
    std::string caption, prompt, value;
    std::optional<std::string> result;
};

INT_PTR CALLBACK InputProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            auto* s = reinterpret_cast<InputState*>(lp);
            SetWindowLongPtrW(h, DWLP_USER, lp);
            SetWindowTextUtf8(h, s->caption);
            SetWindowTextUtf8(GetDlgItem(h, IDC_INPUT_LABEL), s->prompt);
            HWND edit = GetDlgItem(h, IDC_INPUT_EDIT);
            SetWindowTextUtf8(edit, s->value);
            SetFocus(edit);
            SendMessageW(edit, EM_SETSEL, 0, -1);
            return FALSE; // focus set
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                DlgState<InputState>(h)->result = GetWindowTextUtf8(GetDlgItem(h, IDC_INPUT_EDIT));
                EndDialog(h, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(h, IDCANCEL);
                return TRUE;
            }
            break;
        default:
            break;
    }
    return FALSE;
}

// ------------------------------------------------------------------ text

struct TextState {
    std::string caption, text;
    HFONT font = nullptr;
};

INT_PTR CALLBACK TextProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            auto* s = reinterpret_cast<TextState*>(lp);
            SetWindowLongPtrW(h, DWLP_USER, lp);
            SetWindowTextUtf8(h, s->caption);
            HWND edit = GetDlgItem(h, IDC_TEXT_EDIT);
            s->font = CreateNamedFont(DpiOf(h), L"Consolas", 9);
            SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(s->font), TRUE);
            std::string t;
            for (const auto& l : text::SplitLines(s->text)) t += l + "\r\n";
            SetWindowTextUtf8(edit, t);
            SetFocus(GetDlgItem(h, IDOK));
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_TEXT_COPY) {
                SetClipboardText(h, DlgState<TextState>(h)->text);
                MsgBox(h, "Copied!", "SharpSFV", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
                EndDialog(h, IDOK);
                return TRUE;
            }
            break;
        case WM_DESTROY:
            if (auto* s = DlgState<TextState>(h); s && s->font) DeleteObject(s->font);
            break;
        default:
            break;
    }
    return FALSE;
}

// ------------------------------------------------------------------ about

INT_PTR CALLBACK AboutProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            SetWindowTextUtf8(GetDlgItem(h, IDC_ABOUT_TEXT),
                              "SharpSFV v2.7 (C++ port)\nInspired by QuickSFV\n\nCreated by L33T.\n\n"
                              "This program comes with ABSOLUTELY NO WARRANTY. It is free software, licensed "
                              "under the GNU General Public License version 3; you may redistribute it under "
                              "its terms.");
            return TRUE;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm->idFrom == IDC_ABOUT_LINK && (nm->code == NM_CLICK || nm->code == NM_RETURN)) {
                OpenUrl(N(reinterpret_cast<NMLINK*>(lp)->item.szUrl));
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
                EndDialog(h, IDOK);
                return TRUE;
            }
            break;
        default:
            break;
    }
    return FALSE;
}

// ------------------------------------------------------------------ drives

struct DrivesState {
    AppSettings* settings = nullptr;
    std::vector<DriveRecord> records;
};

void FillDrives(HWND h, DrivesState* s) {
    DataStorage storage(*s->settings);
    HWND list = GetDlgItem(h, IDC_DRIVES_LIST);
    const bool allowed = storage.Allowed();
    s->records = allowed ? DriveTypeStore(storage.DriveTypeFile()).Load() : std::vector<DriveRecord>{};

    SetWindowTextUtf8(GetDlgItem(h, IDC_DRIVES_INFO),
                      allowed ? "Stored in " + storage.DriveTypeFile() +
                                    ". Your own setting for a drive is always used; automatic results are "
                                    "detected once per drive."
                              : std::string("Local data storage has not been allowed, so per-drive settings "
                                            "cannot be kept. Processing Mode (Auto / HDD / SSD) still applies "
                                            "to all drives."));
    ShowWindow(GetDlgItem(h, IDC_DRIVES_CONSENT), allowed ? SW_HIDE : SW_SHOW);
    EnableWindow(GetDlgItem(h, IDC_DRIVES_ADD), allowed);

    ListView_DeleteAllItems(list);
    int row = 0;
    for (const DriveRecord& r : s->records) {
        const std::string drive = r.mountPoint.empty() ? r.section : r.mountPoint;
        const std::string cols[] = {
            drive,
            r.label,
            r.isHdd ? "HDD" : "SSD",
            r.userSet ? "Your setting" : "Automatic",
            std::string(r.detectedIsHdd ? "HDD" : "SSD") + (r.detectedBy.empty() ? "" : " - " + r.detectedBy),
            r.guid,
            r.serial,
        };
        const std::wstring first = W(cols[0]);
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = row;
        it.pszText = const_cast<wchar_t*>(first.c_str());
        it.lParam = row;
        ListView_InsertItem(list, &it);
        for (int c = 1; c < 7; ++c) {
            const std::wstring v = W(cols[c]);
            ListView_SetItemText(list, row, c, const_cast<wchar_t*>(v.c_str()));
        }
        ++row;
    }
    const bool any = !s->records.empty();
    for (int id : {IDC_DRIVES_SSD, IDC_DRIVES_HDD, IDC_DRIVES_FORGET}) EnableWindow(GetDlgItem(h, id), any && allowed);
}

int SelectedRecord(HWND h) {
    return ListView_GetNextItem(GetDlgItem(h, IDC_DRIVES_LIST), -1, LVNI_SELECTED);
}

INT_PTR CALLBACK DrivesProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            auto* s = reinterpret_cast<DrivesState*>(lp);
            SetWindowLongPtrW(h, DWLP_USER, lp);
            HWND list = GetDlgItem(h, IDC_DRIVES_LIST);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            const UINT dpi = DpiOf(h);
            const struct { const wchar_t* name; int width; } cols[] = {
                {L"Drive", 70}, {L"Label", 80}, {L"Type", 45}, {L"Source", 85},
                {L"Automatic detection", 230}, {L"Volume GUID", 250}, {L"Serial", 85}};
            for (int c = 0; c < 7; ++c) {
                LVCOLUMNW col{};
                col.mask = LVCF_TEXT | LVCF_WIDTH;
                col.pszText = const_cast<wchar_t*>(cols[c].name);
                col.cx = Scale(cols[c].width, dpi);
                ListView_InsertColumn(list, c, &col);
            }
            FillDrives(h, s);
            return TRUE;
        }
        case WM_COMMAND: {
            auto* s = DlgState<DrivesState>(h);
            const int id = LOWORD(wp);
            if (id == IDOK || id == IDCANCEL) {
                EndDialog(h, IDOK);
                return TRUE;
            }
            DataStorage storage(*s->settings);
            const DriveTypeStore store(storage.DriveTypeFile());
            std::string err;
            const int sel = SelectedRecord(h);
            const bool haveSel = sel >= 0 && sel < static_cast<int>(s->records.size());
            if ((id == IDC_DRIVES_SSD || id == IDC_DRIVES_HDD || id == IDC_DRIVES_FORGET) && !haveSel) {
                MsgBox(h, "Select a drive first.", "Drive Classification", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            if (id == IDC_DRIVES_SSD || id == IDC_DRIVES_HDD) {
                if (!store.SetType(s->records[static_cast<std::size_t>(sel)].section, id == IDC_DRIVES_HDD, &err))
                    MsgBox(h, err, "Drive Classification", MB_OK | MB_ICONERROR);
            } else if (id == IDC_DRIVES_FORGET) {
                store.Remove(s->records[static_cast<std::size_t>(sel)].section, &err);
            } else if (id == IDC_DRIVES_ADD) {
                if (auto folder = PickFolder(h, "Choose a folder on the drive to add")) {
                    const DriveDecision d = DriveResolver(&storage).Resolve(ProcessingMode::Auto, *folder);
                    if (d.source == DriveDecision::Source::DetectedNotSaved)
                        MsgBox(h, "The drive could not be added: " + d.note, "Drive Classification", MB_OK | MB_ICONWARNING);
                }
            } else if (id == IDC_DRIVES_CONSENT) {
                storage.Ask(h);
            } else {
                break;
            }
            FillDrives(h, s);
            if (haveSel && id != IDC_DRIVES_FORGET) {
                HWND list = GetDlgItem(h, IDC_DRIVES_LIST);
                ListView_SetItemState(list, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            return TRUE;
        }
        default:
            break;
    }
    return FALSE;
}

} // namespace

std::optional<std::string> InputDialog(HWND owner, const std::string& caption, const std::string& prompt,
                                       const std::string& value) {
    InputState s{caption, prompt, value, std::nullopt};
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_INPUT), owner, InputProc,
                    reinterpret_cast<LPARAM>(&s));
    return s.result;
}

void TextDialog(HWND owner, const std::string& caption, const std::string& textContent) {
    TextState s{caption, textContent, nullptr};
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_TEXT), owner, TextProc, reinterpret_cast<LPARAM>(&s));
}

void AboutDialog(HWND owner) {
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ABOUT), owner, AboutProc, 0);
}

void DrivesDialog(HWND owner, AppSettings& settings) {
    DrivesState s;
    s.settings = &settings;
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_DRIVES), owner, DrivesProc,
                    reinterpret_cast<LPARAM>(&s));
}

} // namespace sfv::gui
