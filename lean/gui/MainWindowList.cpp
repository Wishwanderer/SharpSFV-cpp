// SharpSFV (lean C++ rewrite) - main window: the virtual file/job list, filters, stats labels.
// Port of src/gui/MainWindowList.cpp (Form1.UI.VirtualMode.cs, SetupUIForMode / UpdateStats /
// UpdateJobStats / RecalculateColumnWidths in Form1.UI.Layout.cs, SetupContextMenu).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "MainWindow.h"

#include "../base/FileSystem.h"
#include "../core/Hash.h"
#include "Panel.h"

#include <windowsx.h>

namespace sfv::gui {

namespace {
// Row colors (Form1.cs).
constexpr COLORREF kGreenBack = RGB(220, 255, 220), kGreenText = RGB(0, 100, 0);
constexpr COLORREF kRedBack = RGB(255, 220, 220), kRedText = RGB(255, 0, 0);
constexpr COLORREF kYellowBack = RGB(255, 255, 224), kYellowText = RGB(184, 134, 11);
constexpr COLORREF kOrangeText = RGB(255, 140, 0), kOldLace = RGB(253, 245, 230), kAliceBlue = RGB(240, 248, 255);

void CopyOut(LVITEMW& item, StrView s) {
    if (!item.pszText || item.cchTextMax <= 0) return;
    CopyTruncated(item.pszText, static_cast<usize>(item.cchTextMax), W(s).C());
}

const char* JobStatusText(JobStatus s) {
    switch (s) {
        case JobStatus::Done: return "DONE";
        case JobStatus::Scanning: return "SCANNING";
        case JobStatus::InProgress: return "IN PROGRESS";
        case JobStatus::Paused: return "PAUSED";
        case JobStatus::Error: return "ERROR";
        default: return "QUEUED";
    }
}
} // namespace

// ------------------------------------------------------------------ columns

void MainWindow::AddColumn(const wchar_t* title, int width, ColumnKind kind) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = const_cast<wchar_t*>(title);
    col.cx = S(width);
    col.iSubItem = static_cast<int>(columns_.Size());
    settingWidth_ = true;
    ListView_InsertColumn(list_, static_cast<int>(columns_.Size()), &col);
    settingWidth_ = false;
    columns_.Push(kind);
    originalWidths_.Push(S(width));
}

void MainWindow::SetupStandardColumns(bool verification) {
    if (jobMode_ || !list_) return;
    settingWidth_ = true;
    while (ListView_DeleteColumn(list_, 0)) {
    }
    settingWidth_ = false;
    columns_.Clear();
    originalWidths_.Clear();
    sortColumn_ = -1;
    sortOrder_ = SortOrder::None;
    verificationMode_ = verification;
    ListView_SetExtendedListViewStyleEx(list_, LVS_EX_HEADERDRAGDROP, settings_.lockColumns ? 0 : LVS_EX_HEADERDRAGDROP);

    AddColumn(L"File Name", 300, ColumnKind::Name);
    if (settings_.showHashCol) AddColumn(L"Hash", 220, ColumnKind::Hash);
    AddColumn(L"Status", 100, ColumnKind::Status);
    if (verificationMode_ && settings_.showExpectedHashCol) AddColumn(L"Expected Hash", 220, ColumnKind::Expected);
    if (settings_.showTimeTab) AddColumn(L"Time", 80, ColumnKind::Time);

    ShowWindow(lblTime_, settings_.showTimeTab ? SW_SHOW : SW_HIDE);
    RestoreColumnOrder();
    ApplyNameColumnWidth();
    UpdateSortArrows();
    UpdateIdleTitle();
}

void MainWindow::SetupJobColumns() {
    settingWidth_ = true;
    while (ListView_DeleteColumn(list_, 0)) {
    }
    settingWidth_ = false;
    columns_.Clear();
    originalWidths_.Clear();
    sortColumn_ = -1;
    sortOrder_ = SortOrder::None;
    AddColumn(L"Job Name", 250, ColumnKind::JobName);
    AddColumn(L"Root Path", 350, ColumnKind::RootPath);
    AddColumn(L"Progress", 100, ColumnKind::Progress);
    AddColumn(L"Status", 100, ColumnKind::JobStatus);
    AddColumn(L"Time", 100, ColumnKind::JobTime);
}

void MainWindow::RestoreColumnOrder() {
    const int n = static_cast<int>(columns_.Size());
    if (settings_.columnOrder.Empty() || n == 0) return;
    Vec<int> order;
    Vec<bool> placed;
    for (int c = 0; c < n; ++c) order.Push(-1), placed.Push(false);
    for (int c = 0; c < n; ++c) {
        for (const ColumnOrderEntry& e : settings_.columnOrder) {
            const int idx = e.displayIndex;
            if (e.tag == ColumnTag(columns_[static_cast<usize>(c)]) && idx >= 0 && idx < n && order[static_cast<usize>(idx)] < 0) {
                order[static_cast<usize>(idx)] = c;
                placed[static_cast<usize>(c)] = true;
                break;
            }
        }
    }
    int next = 0;
    for (int c = 0; c < n; ++c) {
        if (placed[static_cast<usize>(c)]) continue;
        while (order[static_cast<usize>(next)] >= 0) ++next;
        order[static_cast<usize>(next)] = c;
    }
    ListView_SetColumnOrderArray(list_, n, order.Data());
}

Vec<ColumnOrderEntry> MainWindow::CurrentColumnOrder() const {
    const int n = static_cast<int>(columns_.Size());
    Vec<int> order;
    order.Resize(static_cast<usize>(n));
    if (n == 0 || !ListView_GetColumnOrderArray(list_, n, order.Data())) return {};
    Vec<ColumnOrderEntry> out;
    for (int c = 0; c < n; ++c) {
        int pos = 0;
        while (pos < n && order[static_cast<usize>(pos)] != c) ++pos;
        ColumnOrderEntry e;
        e.tag = ColumnTag(columns_[static_cast<usize>(c)]);
        e.displayIndex = pos;
        out.Push(Move(e));
    }
    return out;
}

int MainWindow::ColumnIndex(ColumnKind k) const {
    for (usize i = 0; i < columns_.Size(); ++i)
        if (columns_[i] == k) return static_cast<int>(i);
    return -1;
}

void MainWindow::SetColumnWidth(int column, int width) {
    settingWidth_ = true;
    ListView_SetColumnWidth(list_, column, width);
    settingWidth_ = false;
}

// ------------------------------------------------------------------ virtual items

void MainWindow::OnGetDispInfo(NMLVDISPINFOW* info) {
    LVITEMW& item = info->item;
    if (!(item.mask & LVIF_TEXT)) return;
    const int row = item.iItem, col = item.iSubItem;
    if (col < 0 || col >= static_cast<int>(columns_.Size())) return;
    const ColumnKind kind = columns_[static_cast<usize>(col)];

    if (jobMode_) {
        const JobRef job = jobs_.Get(row);
        if (!job) {
            CopyOut(item, col == 0 ? "Loading..." : "");
            return;
        }
        const JobStatus st = job->Status();
        switch (kind) {
            case ColumnKind::JobName: CopyOut(item, job->name); break;
            case ColumnKind::RootPath: CopyOut(item, job->rootPath); break;
            case ColumnKind::Progress: {
                const Str pct = text::FormatF1(job->Progress()) + "%";
                if (st == JobStatus::InProgress) CopyOut(item, pct);
                else if (st == JobStatus::Scanning) CopyOut(item, "Counting files...");
                else if (st == JobStatus::Paused) CopyOut(item, pct + " (Paused)");
                else if (st == JobStatus::Queued) CopyOut(item, "Pending");
                else CopyOut(item, "100%");
                break;
            }
            case ColumnKind::JobStatus: CopyOut(item, JobStatusText(st)); break;
            case ColumnKind::JobTime: CopyOut(item, job->TimeStr()); break;
            default: break;
        }
        return;
    }

    if (row < 0 || row >= static_cast<int>(display_.Size())) {
        CopyOut(item, col == 0 ? "Loading..." : "");
        return;
    }
    const int i = display_[static_cast<usize>(row)];
    switch (kind) {
        case ColumnKind::Name:
            if (settings_.showFullPaths) CopyOut(item, store_.GetFullPath(i));
            else CopyOut(item, store_.FileName(i));
            break;
        case ColumnKind::Hash: {
            // A verified CRC-32 is shown in the byte order it matched, so it lines up with
            // the Expected Hash column; otherwise in the chosen display order.
            const CrcMatch m = store_.CrcMatchOf(i);
            const Crc32ByteOrder order = m == CrcMatch::Reversed   ? Crc32ByteOrder::Reversed
                                         : m == CrcMatch::Standard ? Crc32ByteOrder::Standard
                                                                   : settings_.crc32ByteOrder;
            CopyOut(item, store_.GetCalculatedHashString(i, order));
            break;
        }
        case ColumnKind::Status: CopyOut(item, ToString(store_.Status(i))); break;
        case ColumnKind::Expected: CopyOut(item, store_.GetExpectedHashString(i)); break;
        case ColumnKind::Time: CopyOut(item, store_.TimeStr(i)); break;
        default: break;
    }
}

LRESULT MainWindow::OnCustomDraw(NMLVCUSTOMDRAW* cd) {
    switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT: {
            const int row = static_cast<int>(cd->nmcd.dwItemSpec);
            if (ListView_GetItemState(list_, row, LVIS_SELECTED)) return CDRF_DODEFAULT; // keep the highlight
            COLORREF fg = CLR_DEFAULT, bg = CLR_DEFAULT;
            HFONT font = nullptr;
            if (jobMode_) {
                const JobRef job = jobs_.Get(row);
                if (!job) return CDRF_DODEFAULT;
                switch (job->Status()) {
                    case JobStatus::Done: fg = kGreenText; bg = kGreenBack; break;
                    case JobStatus::Error: fg = kRedText; bg = kRedBack; break;
                    case JobStatus::Scanning: fg = kOrangeText; bg = kOldLace; break;
                    case JobStatus::InProgress: bg = kAliceBlue; break;
                    case JobStatus::Paused: fg = kYellowText; bg = kYellowBack; break;
                    default: break;
                }
            } else {
                if (row < 0 || row >= static_cast<int>(display_.Size())) return CDRF_DODEFAULT;
                const int i = display_[static_cast<usize>(row)];
                switch (store_.Status(i)) {
                    case ItemStatus::Error:
                    case ItemStatus::Bad: fg = kRedText; bg = kRedBack; break;
                    case ItemStatus::OK: fg = kGreenText; bg = kGreenBack; break;
                    case ItemStatus::Missing:
                        fg = kYellowText;
                        bg = kYellowBack;
                        font = fontStrike_;
                        break;
                    default: break;
                }
            }
            if (fg == CLR_DEFAULT && bg == CLR_DEFAULT && !font) return CDRF_DODEFAULT;
            if (fg != CLR_DEFAULT) cd->clrText = fg;
            if (bg != CLR_DEFAULT) cd->clrTextBk = bg;
            if (font) SelectObject(cd->nmcd.hdc, font);
            return CDRF_NEWFONT;
        }
        default: return CDRF_DODEFAULT;
    }
}

// ------------------------------------------------------------------ sorting, resizing

void MainWindow::OnColumnClick(int column) {
    // Not while hashing (results arrive by index) and not for the job queue.
    if (processing_ || jobMode_ || column < 0 || column >= static_cast<int>(columns_.Size())) return;
    if (column != sortColumn_) {
        sortColumn_ = column;
        sortOrder_ = SortOrder::Ascending;
    } else {
        sortOrder_ = sortOrder_ == SortOrder::Ascending ? SortOrder::Descending : SortOrder::Ascending;
    }
    UpdateSortArrows();
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    SortIndices(display_, store_, columns_[static_cast<usize>(column)], sortOrder_);
    SetCursor(old);
    InvalidateRect(list_, nullptr, FALSE);
}

void MainWindow::UpdateSortArrows() {
    HWND header = ListView_GetHeader(list_);
    for (int c = 0; c < static_cast<int>(columns_.Size()); ++c) {
        HDITEMW hd{};
        hd.mask = HDI_FORMAT;
        Header_GetItem(header, c, &hd);
        hd.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (c == sortColumn_) hd.fmt |= sortOrder_ == SortOrder::Descending ? HDF_SORTDOWN : HDF_SORTUP;
        Header_SetItem(header, c, &hd);
    }
}

bool MainWindow::OnHeaderItemChanging(const NMHEADERW* nm) {
    // LvFiles_ColumnWidthChanging: keep user resizes within 75%..150% of the original width
    // (the name column may grow to fit full paths). Returns true to block the change.
    if (settingWidth_ || !nm->pitem || !(nm->pitem->mask & HDI_WIDTH)) return false;
    const int c = nm->iItem;
    if (c < 0 || c >= static_cast<int>(originalWidths_.Size())) return false;
    const int orig = originalWidths_[static_cast<usize>(c)];
    const int minW = orig * 3 / 4;
    int maxW = orig * 3 / 2;
    if (columns_[static_cast<usize>(c)] == ColumnKind::Name && settings_.showFullPaths && !jobMode_)
        maxW = Max(minW, cachedFullPathWidth_);
    return nm->pitem->cxy < minW || nm->pitem->cxy > maxW;
}

void MainWindow::RecalculateColumnWidths() {
    // Estimate from the longest strings instead of measuring every row.
    const int n = jobMode_ ? 0 : store_.Count();
    Str longestPath, longestName;
    usize maxPath = 0, maxName = 0;
    for (int i = 0; i < n; ++i) {
        const usize est = store_.BaseDirectory(i).Size() + store_.RelativePath(i).Size() + 1;
        if (est > maxPath) {
            maxPath = est;
            longestPath = store_.GetFullPath(i);
        }
        if (store_.FileName(i).Size() > maxName) {
            maxName = store_.FileName(i).Size();
            longestName = store_.FileName(i);
        }
    }
    cachedFullPathWidth_ = longestPath.Empty() ? S(400) : MeasureTextWidth(list_, font_, W(longestPath)) + S(25);
    cachedNameWidth_ = longestName.Empty() ? S(300) : MeasureTextWidth(list_, font_, W(longestName)) + S(25);
}

void MainWindow::ApplyNameColumnWidth() {
    const int c = ColumnIndex(ColumnKind::Name);
    if (c < 0) return;
    if (settings_.showFullPaths) {
        SetColumnWidth(c, cachedFullPathWidth_);
    } else {
        // Wide enough for the longest name of the last run, never narrower than the default.
        const int w = Max(S(300), cachedNameWidth_);
        originalWidths_[static_cast<usize>(c)] = w;
        SetColumnWidth(c, w);
    }
}

// ------------------------------------------------------------------ filtering

FilterSpec MainWindow::CurrentFilter() const {
    FilterSpec f;
    if (!txtFilter_) return f;
    f.text = Str(text::Trim(GetWindowTextUtf8(txtFilter_)));
    f.status = GetWindowTextUtf8(cmbStatus_);
    f.duplicates = Button_GetCheck(chkDups_) == BST_CHECKED;
    return f;
}

void MainWindow::ApplyFilter() {
    if (jobMode_ || !list_) return;
    const ColumnKind kind = sortColumn_ >= 0 ? columns_[static_cast<usize>(sortColumn_)] : ColumnKind::Name;
    display_ = FilterIndices(store_, CurrentFilter(), kind, sortColumn_ >= 0 ? sortOrder_ : SortOrder::None);
    RefreshItemCount(true);
    SyncMenu();
}

void MainWindow::RefreshItemCount(bool full) {
    const int count = jobMode_ ? jobs_.Count() : static_cast<int>(display_.Size());
    ListView_SetItemCountEx(list_, count, full ? 0 : (LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL));
    if (full) InvalidateRect(list_, nullptr, FALSE);
}

Vec<int> MainWindow::SelectedRows() const {
    Vec<int> rows;
    int i = -1;
    while ((i = ListView_GetNextItem(list_, i, LVNI_SELECTED)) >= 0) rows.Push(i);
    return rows;
}

void MainWindow::ShowContextMenu(POINT screen) {
    if (jobMode_) return; // no context menu for the job queue
    const Vec<int> rows = SelectedRows();
    if (rows.Empty()) return;
    const bool single = rows.Size() == 1;
    bool exists = false;
    if (single && rows[0] < static_cast<int>(display_.Size()))
        exists = fs::FileExists(store_.GetFullPath(display_[static_cast<usize>(rows[0])]));

    HMENU m = CreatePopupMenu();
    // Changing the list while hashing would detach results from their rows.
    const UINT idle = processing_ ? MF_GRAYED : MF_ENABLED;
    AppendMenuW(m, MF_STRING | (single ? MF_ENABLED : MF_GRAYED), ID_CTX_OPENFOLDER, L"Open Containing Folder");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_CTX_COPY, single ? L"Copy Entry" : L"Copy Entries");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (single && exists && !processing_ ? MF_ENABLED : MF_GRAYED), ID_CTX_RENAME, L"Rename File...");
    AppendMenuW(m, MF_STRING | idle, ID_CTX_DELETE, single ? L"Delete File" : L"Delete Files");
    AppendMenuW(m, MF_STRING | idle, ID_CTX_REMOVE, L"Remove from List");

    if (screen.x == -1 && screen.y == -1) { // keyboard: Shift+F10 / menu key
        int focused = ListView_GetNextItem(list_, -1, LVNI_FOCUSED);
        if (focused < 0) focused = rows[0];
        RECT r{};
        ListView_GetItemRect(list_, focused, &r, LVIR_LABEL);
        screen = {r.left, r.bottom};
        ClientToScreen(list_, &screen);
    }
    const UINT cmd = TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD, screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(m);
    if (cmd) OnCommand(cmd, 0, nullptr);
}

// ------------------------------------------------------------------ stats labels

void MainWindow::LayoutStatsLabels() {
    if (!statsPanel_) return;
    auto width = [](HWND l) {
        const text::WStr t = W(GetWindowTextUtf8(l));
        return MeasureTextWidth(l, reinterpret_cast<HFONT>(SendMessageW(l, WM_GETFONT, 0, 0)), t) + 2;
    };
    // Labels end before the Pause button (long text gets an ellipsis).
    RECT pr{};
    GetWindowRect(pause_, &pr);
    MapWindowPoints(nullptr, statsPanel_, reinterpret_cast<POINT*>(&pr), 2);
    const int limit = Max(S(100), static_cast<int>(pr.left) - S(10));
    bool moved = false;
    auto put = [&](HWND l, int x, int y, int w) {
        const int cw = Max(0, Min(w, limit - x));
        RECT old{};
        GetWindowRect(l, &old);
        MapWindowPoints(nullptr, statsPanel_, reinterpret_cast<POINT*>(&old), 2);
        if (old.left == x && old.top == y && old.right - old.left == cw && old.bottom - old.top == S(17)) return;
        SetWindowPos(l, nullptr, x, y, cw, S(17), SWP_NOZORDER | SWP_NOACTIVATE);
        moved = true;
    };
    const int progressW = width(lblProgress_);
    put(lblProgress_, S(10), S(8), progressW);
    // The original placed the time label at a fixed 300 px, where long throughput text ran into it.
    put(lblTime_, Max(S(300), S(10) + progressW + S(20)), S(8), width(lblTime_));
    int x = S(10);
    HWND counters[] = {lblOk_, lblBad_, lblMissing_};
    for (HWND l : counters) {
        const int lw = width(l);
        put(l, x, S(28), lw);
        x += lw + S(15);
    }
    put(lblWarning_, Max(S(300), x + S(10)), S(28), width(lblWarning_));
    if (moved) RedrawWindow(statsPanel_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void MainWindow::SetStatusLabel(HWND label, StrView value, COLORREF color) {
    SetWindowTextUtf8(label, value);
    SetChildTextColor(statsPanel_, label, color);
}

void MainWindow::UpdateStats(int current, int total, int ok, int bad, int missing, StrView speed) {
    if (jobMode_ || !lblProgress_) return;
    Str t = "Ready";
    if (!(total == 0 && current == 0)) {
        t = "Completed files: " + ToStr(current) + " / " + ToStr(total);
        if (settings_.showThroughputStats && !speed.Empty()) t += Str(" | ") + speed;
    }
    SetWindowTextUtf8(lblProgress_, t);
    SetStatusLabel(lblOk_, "OK: " + ToStr(ok), RGB(0, 100, 0));
    SetStatusLabel(lblBad_, "BAD: " + ToStr(bad), RGB(255, 0, 0));
    SetStatusLabel(lblMissing_, "MISSING: " + ToStr(missing), RGB(184, 134, 11));
    LayoutStatsLabels();
}

// Status counters recomputed from the list itself (after a mode switch or removing items).
void MainWindow::RecountStats() {
    int done = 0, ok = 0, bad = 0, missing = 0;
    const int n = store_.Count();
    for (int i = 0; i < n; ++i) {
        switch (store_.Status(i)) {
            case ItemStatus::OK: ++ok; ++done; break;
            case ItemStatus::Bad:
            case ItemStatus::Error: ++bad; ++done; break;
            case ItemStatus::Missing: ++missing; ++done; break;
            default: break;
        }
    }
    UpdateStats(done, n, ok, bad, missing);
}

void MainWindow::UpdateJobStats(StrView speed) {
    if (!jobMode_ || !lblProgress_) return;
    int done = 0, error = 0, active = 0;
    const int n = jobs_.Count();
    for (int i = 0; i < n; ++i) {
        const JobRef job = jobs_.Get(i);
        if (!job) continue;
        switch (job->Status()) {
            case JobStatus::Done: ++done; break;
            case JobStatus::Error: ++error; break;
            case JobStatus::InProgress:
            case JobStatus::Paused: ++active; break;
            default: break;
        }
    }
    Str t = "Jobs Completed: " + ToStr(done) + " / " + ToStr(n);
    if (settings_.showThroughputStats && !speed.Empty()) t += Str(" | ") + speed;
    SetWindowTextUtf8(lblProgress_, t);
    SetStatusLabel(lblOk_, "DONE: " + ToStr(done), RGB(0, 100, 0));
    SetStatusLabel(lblBad_, "ERROR: " + ToStr(error), RGB(255, 0, 0));
    SetStatusLabel(lblMissing_, "ACTIVE: " + ToStr(active), RGB(0, 0, 255));
    LayoutStatsLabels();
}

void MainWindow::SetWarnings(Vec<Str> warnings) {
    warnings_ = Move(warnings);
    Str t;
    for (const Str& w : warnings_) t += (t.Empty() ? Str() : Str("  |  ")) + w;
    SetWindowTextUtf8(lblWarning_, t);
    ShowWindow(lblWarning_, t.Empty() ? SW_HIDE : SW_SHOW);
    LayoutStatsLabels();
}

} // namespace sfv::gui
