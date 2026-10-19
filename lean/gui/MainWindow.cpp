// SharpSFV (lean C++ rewrite) - main window: lifecycle, layout, fonts, settings.
// Port of src/gui/MainWindow.cpp (Form1.cs, Form1.UI.Layout.cs, Form1.ActiveJobs.cs).
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
#include "../base/System.h"
#include "Panel.h"
#include "resource.h"

#include <shellapi.h>
#include <windowsx.h>

namespace sfv::gui {

namespace {

constexpr wchar_t kClassName[] = L"SharpSFV.Main";

// Colors from Form1.cs / Form1.UI.Layout.cs.
constexpr COLORREF kDarkGreen = RGB(0, 100, 0);
constexpr COLORREF kRed = RGB(255, 0, 0);
constexpr COLORREF kDarkGoldenrod = RGB(184, 134, 11);
constexpr COLORREF kDarkOrange = RGB(255, 140, 0);
constexpr COLORREF kBlue = RGB(0, 0, 255);
constexpr COLORREF kAdvancedBack = RGB(240, 240, 245);
constexpr COLORREF kActiveBack = RGB(245, 245, 255);

// Layout metrics at 96 DPI (the original's pixel values).
constexpr int kStatsHeight = 50, kFilterHeight = 35, kAdvancedHeight = 35, kCommentsHeight = 60;
constexpr int kProgressHeight = 15, kSplitterWidth = 4, kButtonW = 75, kButtonH = 26;
constexpr int kMinWidth = 600, kMinHeight = 400; // minimum window size at 96 DPI

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void SetFont(HWND h, HFONT f) {
    if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
}

int TextWidthOf(HWND label) {
    const text::WStr t = W(GetWindowTextUtf8(label));
    return MeasureTextWidth(label, reinterpret_cast<HFONT>(SendMessageW(label, WM_GETFONT, 0, 0)), t);
}

} // namespace

MainWindow::MainWindow(const Vec<Str>& args, bool createMode)
    : startupArgs_(args), settings_(fs::ExecutableDirectory()), createMode_(createMode), engine_(store_, throttle_, this),
      runSettings_("") {
    settings_.Load();
    currentAlgo_ = settings_.defaultAlgo;
    store_.SetDisplayCrcOrder(settings_.crc32ByteOrder);
    // Throttled progress: keep the latest snapshot, tell the UI thread to look.
    throttle_.SetPresenter(&Present, this);
}

MainWindow::~MainWindow() {
    if (worker_.Running()) {
        engine_.Cancel();
        worker_.Join();
    }
    HFONT fonts[] = {font_, fontBold_, fontStrike_, fontMono_};
    for (HFONT f : fonts)
        if (f) DeleteObject(f);
    for (HFONT f : retiredFonts_) DeleteObject(f);
    if (accelFull_) DestroyAcceleratorTable(accelFull_);
    if (accelEdit_) DestroyAcceleratorTable(accelEdit_);
}

bool MainWindow::Create(int showCommand) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    wc.hIcon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                             GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    RegisterClassExW(&wc);

    const DWORD style = createMode_ ? (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN)
                                    : (WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN);
    hwnd_ = CreateWindowExW(0, kClassName, L"SharpSFV", style, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr,
                            inst, this);
    if (!hwnd_) return false;
    notifyTarget_ = hwnd_;

    if (createMode_) {
        // Explorer "Create Checksum": stay hidden while paths arrive from other instances.
        for (const Str& a : startupArgs_)
            if (!a.Empty() && a[0] != '-') collected_.Push(a);
        pipeServer_.Start(hwnd_, WM_APP_PIPE_PATHS);
        // The C# version only started this timer when a second instance sent a path, so a
        // single selected item never got past this point.
        SetTimer(hwnd_, TIMER_CREATE, 200, nullptr);
        return true;
    }

    ApplySettings();
    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    PostMessageW(hwnd_, WM_APP_STARTUP, 0, 0);
    return true;
}

int MainWindow::RunMessageLoop() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (hwnd_ && IsWindow(hwnd_)) {
            // IsDialogMessage walks the visible, enabled controls looking for the focused one;
            // if focus was left on a control inside a panel that has since been hidden or
            // disabled, that walk never terminates (seen under Wine). Move focus first.
            if (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) EnsureFocusReachable();
            // In a text box, Ctrl+C/V/A belong to the text box; only Ctrl+O/S stay global.
            wchar_t cls[32] = {};
            GetClassNameW(GetFocus(), cls, 32);
            const bool inEdit = EqualsIgnoreCaseW(cls, L"Edit");
            if (TranslateAcceleratorW(hwnd_, inEdit ? accelEdit_ : accelFull_, &msg)) continue;
            if (IsDialogMessageW(hwnd_, &msg)) continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void MainWindow::EnsureFocusReachable() {
    HWND f = GetFocus();
    if (!f || f == hwnd_ || !IsChild(hwnd_, f)) return;
    for (HWND w = f; w && w != hwnd_; w = GetParent(w)) {
        if (!IsWindowVisible(w) || !IsWindowEnabled(w)) {
            HWND target = (list_ && IsWindowVisible(list_)) ? list_ : hwnd_;
            SetFocus(target);
            return;
        }
    }
}

LRESULT CALLBACK MainWindow::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT MainWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == TaskbarProgress::ButtonCreatedMessage() && msg != 0) {
        taskbar_.Attach(hwnd_);
        return 0;
    }
    switch (msg) {
        case WM_CREATE:
            dpi_ = DpiOf(hwnd_);
            CreateFonts();
            if (createMode_) CreateMiniUi();
            else CreateStandardUi();
            DragAcceptFiles(hwnd_, createMode_ ? FALSE : TRUE);
            // Allow drops and taskbar progress when running elevated (UIPI).
            ChangeWindowMessageFilterEx(hwnd_, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
            ChangeWindowMessageFilterEx(hwnd_, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
            ChangeWindowMessageFilterEx(hwnd_, 0x0049 /*WM_COPYGLOBALDATA*/, MSGFLT_ALLOW, nullptr);
            ChangeWindowMessageFilterEx(hwnd_, TaskbarProgress::ButtonCreatedMessage(), MSGFLT_ALLOW, nullptr);
            return 0;
        case WM_SIZE:
            if (createMode_) LayoutMini();
            else Layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            if (!createMode_) {
                mmi->ptMinTrackSize.x = S(kMinWidth);
                mmi->ptMinTrackSize.y = S(kMinHeight);
            }
            return 0;
        }
        case WM_DPICHANGED:
            OnDpiChanged(HIWORD(wp), reinterpret_cast<const RECT*>(lp));
            return 0;
        case WM_COMMAND:
            OnCommand(LOWORD(wp), HIWORD(wp), reinterpret_cast<HWND>(lp));
            return 0;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (list_ && nm->hwndFrom == list_) {
                switch (nm->code) {
                    case LVN_GETDISPINFOW: OnGetDispInfo(reinterpret_cast<NMLVDISPINFOW*>(lp)); return 0;
                    case NM_CUSTOMDRAW: return OnCustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(lp));
                    case LVN_COLUMNCLICK: OnColumnClick(reinterpret_cast<NMLISTVIEW*>(lp)->iSubItem); return 0;
                    case LVN_ODFINDITEMW: return -1;
                    default: break;
                }
            }
            if (list_ && nm->hwndFrom == ListView_GetHeader(list_) &&
                (nm->code == HDN_ITEMCHANGINGW || nm->code == HDN_ITEMCHANGINGA))
                return OnHeaderItemChanging(reinterpret_cast<NMHEADERW*>(lp)) ? TRUE : FALSE;
            // The Active Jobs columns are fixed (LvActiveJobs_ColumnWidthChanging).
            if (active_ && nm->hwndFrom == ListView_GetHeader(active_) &&
                (nm->code == HDN_BEGINTRACKW || nm->code == HDN_BEGINTRACKA || nm->code == HDN_DIVIDERDBLCLICKW ||
                 nm->code == HDN_DIVIDERDBLCLICKA))
                return TRUE;
            break;
        }
        case WM_CONTEXTMENU:
            if (reinterpret_cast<HWND>(wp) == list_) {
                ShowContextMenu({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
                return 0;
            }
            break;
        case WM_DROPFILES: {
            auto drop = reinterpret_cast<HDROP>(wp);
            Vec<Str> paths;
            const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; ++i) {
                const UINT len = DragQueryFileW(drop, i, nullptr, 0);
                text::WStr p;
                p.Resize(len);
                DragQueryFileW(drop, i, p.Data(), len + 1);
                paths.Push(text::Narrow(p.C(), len));
            }
            DragFinish(drop);
            // Do not accept new files while hashing, except into the job queue.
            if (!(processing_ && !jobMode_)) HandlePaths(Move(paths));
            return 0;
        }
        case WM_TIMER:
            if (wp == TIMER_FILTER) {
                KillTimer(hwnd_, TIMER_FILTER);
                ApplyFilter();
            } else if (wp == TIMER_CREATE) {
                KillTimer(hwnd_, TIMER_CREATE);
                LaunchCreateMode();
            }
            return 0;
        case WM_SETCURSOR:
            if (activeVisible_ && LOWORD(lp) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd_, &pt);
                const RECT r = SplitterRect();
                if (PtInRect(&r, pt)) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                    return TRUE;
                }
            }
            break;
        case WM_LBUTTONDOWN:
            if (activeVisible_) {
                const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                const RECT r = SplitterRect();
                if (PtInRect(&r, pt)) {
                    draggingSplitter_ = true;
                    SetCapture(hwnd_);
                }
            }
            return 0;
        case WM_MOUSEMOVE:
            if (draggingSplitter_) {
                RECT top, bottom;
                GetWindowRect(active_, &top);
                GetWindowRect(list_, &bottom);
                MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&top), 2);
                MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&bottom), 2);
                const int areaTop = top.top, areaHeight = bottom.bottom - top.top;
                splitterPos_ = Clamp(GET_Y_LPARAM(lp) - areaTop, S(30), Max(S(30), areaHeight - S(60)));
                Layout();
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            if (draggingSplitter_) {
                draggingSplitter_ = false;
                if (msg == WM_LBUTTONUP) ReleaseCapture();
            }
            return 0;
        case WM_APP_STARTUP: {
            // First run: ask before anything is stored in AppData (not in mini/headless modes).
            if (settings_.dataStorageConsent == DataConsent::NotAsked) {
                DataStorage storage(settings_);
                storage.AskOwned(hwnd_);
                SyncMenu();
            }
            // Form1_Shown: process file paths passed on the command line.
            Vec<Str> paths;
            for (const Str& a : startupArgs_)
                if (!a.Empty() && a[0] != '-') paths.Push(a);
            if (!paths.Empty()) HandlePaths(Move(paths));
            return 0;
        }
        case WM_APP_PROGRESS: OnProgressMessage(); return 0;
        case WM_APP_DISCOVERED: OnDiscovered(); return 0;
        case WM_APP_LARGEFILES: OnLargeFiles(); return 0;
        case WM_APP_VERIFY_LOADED: OnVerifyLoaded(); return 0;
        case WM_APP_DRIVE_DECIDED: OnDriveDecided(); return 0;
        case WM_APP_RUN_DONE: OnRunDone(); return 0;
        case WM_APP_PIPE_PATHS: OnPipePaths(); return 0;
        case WM_APP_JOBS_CHANGED:
            __atomic_store_n(&jobsPosted_, false, __ATOMIC_RELEASE);
            if (jobMode_) {
                RefreshItemCount(false);
                InvalidateRect(list_, nullptr, FALSE);
                UpdateJobStats(throttle_.SpeedString());
            }
            return 0;
        case WM_CLOSE:
            OnClose();
            return 0;
        case WM_DESTROY:
            pipeServer_.Stop();
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ------------------------------------------------------------------ creation

void MainWindow::CreateFonts() {
    // The previous fonts stay alive until the controls have been given the new ones (see
    // OnDpiChanged): a control repainting with a deleted HFONT is undefined on Windows.
    HFONT* fonts[] = {&font_, &fontBold_, &fontStrike_, &fontMono_};
    for (HFONT* f : fonts) {
        if (*f) retiredFonts_.Push(*f);
        *f = nullptr;
    }
    font_ = CreateUiFont(dpi_);
    fontBold_ = CreateUiFont(dpi_, true);
    fontStrike_ = CreateUiFont(dpi_, false, true);
    fontMono_ = CreateNamedFont(dpi_, L"Consolas", 9);
}

HWND MainWindow::MakeLabel(HWND parent, const wchar_t* textValue, HFONT font) {
    HWND h = CreateWindowExW(0, L"STATIC", textValue, WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_NOPREFIX | SS_ENDELLIPSIS,
                             0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetFont(h, font);
    panelLabels_.Push(h);
    return h;
}

HWND MainWindow::MakeEdit(HWND parent, UINT id, StrView value) {
    HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", W(value).C(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0,
                             0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), GetModuleHandleW(nullptr),
                             nullptr);
    SetFont(h, font_);
    return h;
}

HWND MainWindow::MakeCheck(HWND parent, UINT id, const wchar_t* textValue, bool checked) {
    HWND h = CreateWindowExW(0, L"BUTTON", textValue, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0,
                             parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SetFont(h, font_);
    Button_SetCheck(h, checked ? BST_CHECKED : BST_UNCHECKED);
    return h;
}

void MainWindow::AddTooltip(HWND control, const wchar_t* tip) {
    TOOLINFOW ti{};
    ti.cbSize = sizeof ti;
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = GetParent(control);
    ti.uId = reinterpret_cast<UINT_PTR>(control);
    ti.lpszText = const_cast<wchar_t*>(tip);
    SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

void MainWindow::CreateStandardUi() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    auto button = [&](HWND parent, UINT id, const wchar_t* t) {
        HWND b = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | WS_DISABLED, 0, 0, 0,
                                 0, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), inst, nullptr);
        SetFont(b, font_);
        return b;
    };

    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0,
                               hwnd_, nullptr, inst, nullptr);
    SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, S(400));
    SendMessageW(tooltip_, TTM_SETDELAYTIME, TTDT_AUTOPOP, 15000);

    // --- Stats panel (SetupStatsPanel)
    statsPanel_ = CreatePanel(hwnd_, GetSysColor(COLOR_3DLIGHT));
    lblProgress_ = MakeLabel(statsPanel_, L"Ready", fontBold_);
    lblTime_ = MakeLabel(statsPanel_, L"", fontBold_);
    lblWarning_ = MakeLabel(statsPanel_, L"", fontBold_);
    lblOk_ = MakeLabel(statsPanel_, L"OK: 0", fontBold_);
    lblBad_ = MakeLabel(statsPanel_, L"BAD: 0", fontBold_);
    lblMissing_ = MakeLabel(statsPanel_, L"MISSING: 0", fontBold_);
    SetChildTextColor(statsPanel_, lblTime_, kBlue);
    SetChildTextColor(statsPanel_, lblWarning_, kDarkOrange);
    SetChildTextColor(statsPanel_, lblOk_, kDarkGreen);
    SetChildTextColor(statsPanel_, lblBad_, kRed);
    SetChildTextColor(statsPanel_, lblMissing_, kDarkGoldenrod);
    pause_ = button(statsPanel_, IDC_PAUSE, L"Pause");
    stop_ = button(statsPanel_, IDC_STOP, L"Stop");
    ShowWindow(statsPanel_, SW_SHOW);

    // --- Comments panel (SetupCommentsPanel): "//" lines of the verified checksum file
    commentsPanel_ = CreatePanel(hwnd_, GetSysColor(COLOR_INFOBK));
    txtComments_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL, 0, 0, 0,
                                   0, commentsPanel_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_COMMENTS)), inst,
                                   nullptr);
    SetFont(txtComments_, fontMono_);
    SetChildTextColor(commentsPanel_, txtComments_, GetSysColor(COLOR_INFOTEXT));

    // --- Advanced options bar (SetupAdvancedPanel)
    advPanel_ = CreatePanel(hwnd_, kAdvancedBack);
    lblPrefix_ = MakeLabel(advPanel_, L"Path Prefix:", font_);
    txtPrefix_ = MakeEdit(advPanel_, IDC_ADV_PREFIX, settings_.pathPrefix);
    lblInclude_ = MakeLabel(advPanel_, L"Include:", font_);
    txtInclude_ = MakeEdit(advPanel_, IDC_ADV_INCLUDE, settings_.includePattern);
    lblExclude_ = MakeLabel(advPanel_, L"Exclude:", font_);
    txtExclude_ = MakeEdit(advPanel_, IDC_ADV_EXCLUDE, settings_.excludePattern);
    chkRecursive_ = MakeCheck(advPanel_, IDC_ADV_RECURSIVE, L"Scan Subfolders", settings_.scanRecursive);
    chkComments_ = MakeCheck(advPanel_, IDC_ADV_COMMENTS, L"Save Comments", settings_.enableChecksumComments);
    AddTooltip(txtPrefix_, L"Virtual folder structure to prepend to saved paths.\nExample: incoming\\iso\\");
    AddTooltip(txtInclude_, L"Only process files matching these patterns.\nSeparate multiple patterns with a semicolon (;).\n"
                            L"Example: *.iso;*.mkv;*.rar");
    AddTooltip(txtExclude_, L"Skip files matching these patterns.\nSeparate multiple patterns with a semicolon (;).\n"
                            L"Example: *.txt;*.nfo;*.sfv");
    AddTooltip(chkRecursive_, L"If checked, all subdirectories will be scanned recursively.");
    AddTooltip(chkComments_, L"If checked, header comments (Generated By / Algorithm) will be written to the output file.");

    // --- Search / filter bar (SetupFilterPanel)
    filterPanel_ = CreatePanel(hwnd_, GetSysColor(COLOR_BTNFACE));
    lblSearch_ = MakeLabel(filterPanel_, L"Search:", font_);
    txtFilter_ = MakeEdit(filterPanel_, IDC_FILTER_TEXT, "");
    lblStatus_ = MakeLabel(filterPanel_, L"Status:", font_);
    cmbStatus_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0,
                                 0, 0, 0, filterPanel_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FILTER_STATUS)),
                                 inst, nullptr);
    SetFont(cmbStatus_, font_);
    static const wchar_t* const kStatuses[] = {L"All", L"Pending", L"OK", L"BAD", L"MISSING", L"Waiting..."};
    for (const wchar_t* s : kStatuses) SendMessageW(cmbStatus_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(cmbStatus_, CB_SETCURSEL, 0, 0);
    chkDups_ = MakeCheck(filterPanel_, IDC_FILTER_DUPS, L"Show Duplicates", false);

    // --- Active jobs list (top of the splitter, hidden until a file > 50 MB is hashed)
    active_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | LVS_REPORT | LVS_NOSORTHEADER, 0, 0, 0, 0,
                              hwnd_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_ACTIVE)), inst, nullptr);
    ListView_SetExtendedListViewStyle(active_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(active_, kActiveBack);
    ListView_SetTextBkColor(active_, kActiveBack);
    SetFont(active_, font_);
    static const struct {
        const wchar_t* title;
        int width;
    } activeCols[] = {{L"Name", 300}, {L"Progress", 220}};
    for (int c = 0; c < 2; ++c) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<wchar_t*>(activeCols[c].title);
        col.cx = S(activeCols[c].width);
        ListView_InsertColumn(active_, c, &col);
    }

    // --- Main list (virtual)
    list_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS, 0, 0, 0, 0,
                            hwnd_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_LIST)), inst, nullptr);
    SetFont(list_, font_);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER |
                                                 (settings_.lockColumns ? 0 : LVS_EX_HEADERDRAGDROP));

    // --- Progress bar (bottom)
    progress_ = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 0, 0, 0, 0, hwnd_,
                                reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_PROGRESS)), inst, nullptr);

    BuildMenu();
    SetMenu(hwnd_, menu_);
    CreateAccelerators();
    SetupStandardColumns(false);
}

void MainWindow::CreateMiniUi() {
    // SetupMiniLayout: two progress bars (total / current file), Pause and Stop.
    HINSTANCE inst = GetModuleHandleW(nullptr);
    progress_ = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 0, 0, 0, 0, hwnd_,
                                reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_PROGRESS)), inst, nullptr);
    progressFile_ = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 0, 0, 0, 0, hwnd_,
                                    reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_PROGRESS_FILE)), inst, nullptr);
    SendMessageW(progressFile_, PBM_SETRANGE32, 0, 100);
    pause_ = CreateWindowExW(0, L"BUTTON", L"Pause", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd_,
                             reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_PAUSE)), inst, nullptr);
    stop_ = CreateWindowExW(0, L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd_,
                            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_STOP)), inst, nullptr);
    SetFont(pause_, font_);
    SetFont(stop_, font_);
    // Fixed client size 450 x (26 + 2*25) at 96 DPI.
    RECT rc{0, 0, S(450), S(kButtonH + 50)};
    AdjustWindowRectExForDpi(&rc, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)), FALSE, 0, dpi_);
    SetWindowPos(hwnd_, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);
}

void MainWindow::ApplyFonts() {
    // Re-apply DPI-specific fonts to every control (after WM_DPICHANGED).
    for (HWND h : panelLabels_) {
        const bool bold =
            h == lblProgress_ || h == lblTime_ || h == lblWarning_ || h == lblOk_ || h == lblBad_ || h == lblMissing_;
        SetFont(h, bold ? fontBold_ : font_);
    }
    HWND controls[] = {txtPrefix_, txtInclude_, txtExclude_, txtFilter_, cmbStatus_, chkRecursive_,
                       chkComments_, chkDups_, stop_, pause_, list_, active_};
    for (HWND h : controls) SetFont(h, font_);
    SetFont(txtComments_, fontMono_);
}

// ------------------------------------------------------------------ layout

RECT MainWindow::SplitterRect() const {
    RECT r{};
    if (!activeVisible_ || !active_) return r;
    RECT a;
    GetWindowRect(active_, &a);
    MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&a), 2);
    RECT c;
    GetClientRect(hwnd_, &c);
    return RECT{0, a.bottom, c.right, a.bottom + S(kSplitterWidth)};
}

namespace {
// The window's own WS_VISIBLE flag. IsWindowVisible() also requires every parent to be visible,
// so it reports a panel as hidden while the main window itself is not shown yet.
bool ShownFlag(HWND h) { return h && (GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE) != 0; }

// Moves windows in one DeferWindowPos batch per parent; falls back to SetWindowPos for a parent
// whose batch Windows refuses.
class WindowBatch {
public:
    void Place(HWND h, int x, int y, int w, int hgt) {
        if (h) items_.Push(Item{GetParent(h), h, x, y, w, hgt});
    }
    void Commit() {
        constexpr UINT kFlags = SWP_NOZORDER | SWP_NOACTIVATE;
        Vec<bool> done;
        done.Resize(items_.Size());
        for (usize i = 0; i < items_.Size(); ++i) {
            if (done[i]) continue;
            const HWND parent = items_[i].parent;
            Vec<usize> group;
            for (usize j = i; j < items_.Size(); ++j)
                if (!done[j] && items_[j].parent == parent) {
                    group.Push(j);
                    done[j] = true;
                }
            HDWP dwp = BeginDeferWindowPos(static_cast<int>(group.Size()));
            for (const usize j : group) {
                if (!dwp) break;
                const Item& it = items_[j];
                dwp = DeferWindowPos(dwp, it.hwnd, nullptr, it.x, it.y, it.w, it.h, kFlags);
            }
            if (!dwp || !EndDeferWindowPos(dwp)) {
                for (const usize j : group) {
                    const Item& it = items_[j];
                    SetWindowPos(it.hwnd, nullptr, it.x, it.y, it.w, it.h, kFlags);
                }
            }
        }
        items_.Clear();
    }

private:
    struct Item {
        HWND parent, hwnd;
        int x, y, w, h;
    };
    Vec<Item> items_;
};
} // namespace

void MainWindow::Layout() {
    if (!list_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int w = rc.right;
    int y = 0;
    // Windows requires every window in one DeferWindowPos batch to have the same parent; a
    // control inside a panel in the main window's batch makes Windows discard the whole batch
    // (nothing gets sized: a blank window). Wine accepts it, so batch per parent.
    WindowBatch batch;
    auto place = [&](HWND h, int x, int yy, int cw, int ch) { batch.Place(h, x, yy, Max(0, cw), Max(0, ch)); };
    auto fit = [&](HWND label, int x, int yy) { place(label, S(x), S(yy), TextWidthOf(label) + S(2), S(17)); };

    // Docked panels, top to bottom: stats, comments, advanced, filter (Form1's dock order).
    place(statsPanel_, 0, y, w, S(kStatsHeight));
    const int xStop = w - S(kButtonW) - S(10);
    place(stop_, xStop, S(10), S(kButtonW), S(kButtonH));
    place(pause_, xStop - S(kButtonW) - S(10), S(10), S(kButtonW), S(kButtonH));
    y += S(kStatsHeight);

    if (ShownFlag(commentsPanel_)) {
        place(commentsPanel_, 0, y, w, S(kCommentsHeight));
        place(txtComments_, S(5), S(5), w - S(10), S(kCommentsHeight - 10));
        y += S(kCommentsHeight);
    }
    if (ShownFlag(advPanel_)) {
        place(advPanel_, 0, y, w, S(kAdvancedHeight));
        fit(lblPrefix_, 5, 10);
        place(txtPrefix_, S(75), S(7), S(150), S(22));
        fit(lblInclude_, 240, 10);
        place(txtInclude_, S(290), S(7), S(80), S(22));
        fit(lblExclude_, 380, 10);
        place(txtExclude_, S(430), S(7), S(80), S(22));
        place(chkRecursive_, S(530), S(9), TextWidthOf(chkRecursive_) + S(24), S(18));
        place(chkComments_, S(650), S(9), TextWidthOf(chkComments_) + S(24), S(18));
        y += S(kAdvancedHeight);
    }
    if (ShownFlag(filterPanel_)) {
        place(filterPanel_, 0, y, w, S(kFilterHeight));
        fit(lblSearch_, 10, 8);
        place(txtFilter_, S(60), S(5), S(200), S(22));
        fit(lblStatus_, 280, 8);
        place(cmbStatus_, S(330), S(5), S(100), S(200));
        place(chkDups_, S(450), S(8), TextWidthOf(chkDups_) + S(24), S(18));
        y += S(kFilterHeight);
    }

    // Bottom progress bar (15 px panel, 2 px padding).
    const int bottom = rc.bottom - S(kProgressHeight);
    place(progress_, S(2), bottom + S(2), w - S(4), S(kProgressHeight) - S(4));

    // Fill: optional Active Jobs list, splitter, main list.
    const int areaH = Max(0, bottom - y);
    if (activeVisible_) {
        const int top = Clamp(splitterPos_, S(30), Max(S(30), areaH - S(60)));
        place(active_, 0, y, w, top);
        place(list_, 0, y + top + S(kSplitterWidth), w, areaH - top - S(kSplitterWidth));
    } else {
        place(list_, 0, y, w, areaH);
    }
    batch.Commit();
    LayoutStatsLabels();
    ShowWindow(active_, activeVisible_ ? SW_SHOW : SW_HIDE);
    EnsureFocusReachable(); // a panel may just have been hidden with the focus inside it
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::LayoutMini() {
    if (!stop_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int marginX = S(12), y = S(25), spacing = S(10);
    const int stopX = rc.right - marginX - S(kButtonW);
    const int pauseX = stopX - spacing - S(kButtonW);
    const int barW = pauseX - spacing - marginX;
    MoveWindow(stop_, stopX, y, S(kButtonW), S(kButtonH), TRUE);
    MoveWindow(pause_, pauseX, y, S(kButtonW), S(kButtonH), TRUE);
    MoveWindow(progress_, marginX, y, barW, S(12), TRUE);
    MoveWindow(progressFile_, marginX, y + S(14), barW, S(12), TRUE);
}

void MainWindow::OnDpiChanged(UINT newDpi, const RECT* suggested) {
    const UINT old = dpi_;
    dpi_ = newDpi;
    CreateFonts();
    if (list_) {
        settingWidth_ = true;
        for (int c = 0; c < static_cast<int>(columns_.Size()); ++c) {
            ListView_SetColumnWidth(list_, c, MulDiv(ListView_GetColumnWidth(list_, c), static_cast<int>(newDpi), static_cast<int>(old)));
            originalWidths_[static_cast<usize>(c)] =
                MulDiv(originalWidths_[static_cast<usize>(c)], static_cast<int>(newDpi), static_cast<int>(old));
        }
        settingWidth_ = false;
        splitterPos_ = MulDiv(splitterPos_, static_cast<int>(newDpi), static_cast<int>(old));
    }
    if (active_) {
        HWND header = ListView_GetHeader(active_);
        const int n = header ? Header_GetItemCount(header) : 0;
        for (int c = 0; c < n; ++c)
            ListView_SetColumnWidth(active_, c, MulDiv(ListView_GetColumnWidth(active_, c), static_cast<int>(newDpi), static_cast<int>(old)));
    }
    ApplyFonts();
    if (createMode_) {
        SetFont(stop_, font_);
        SetFont(pause_, font_);
    }
    for (HFONT f : retiredFonts_) DeleteObject(f); // every control now uses the new fonts
    retiredFonts_.Clear();
    SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                 suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
    if (createMode_) LayoutMini();
    else Layout();
}

void MainWindow::ToggleActiveJobsPanel(bool show) {
    if (!active_ || activeVisible_ == show) return;
    activeVisible_ = show;
    if (show) {
        RECT rc;
        GetClientRect(list_, &rc);
        // Restore the saved splitter distance or use 20% of the height.
        splitterPos_ = settings_.splitterDistance > 0 ? settings_.splitterDistance : Max(S(50), static_cast<int>(rc.bottom / 5));
    }
    Layout();
}

// ------------------------------------------------------------------ settings

void MainWindow::ApplySettings() {
    {
        // Sizes in SharpSFV.ini are physical pixels (as the C# build writes them). The untouched
        // default, 800x600, is a 96-DPI size: scale it, so the window is not born smaller than its
        // own minimum on a 125-200% display. Anything else is kept, but never below the minimum.
        int w = settings_.windowWidth, h = settings_.windowHeight;
        if (w <= 100 || h <= 100 || (w == 800 && h == 600)) {
            w = S(800);
            h = S(600);
        }
        w = Max(w, S(kMinWidth));
        h = Max(h, S(kMinHeight));
        MONITORINFO mi{};
        mi.cbSize = sizeof mi;
        if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi)) { // fit the screen
            w = Min(w, static_cast<int>(mi.rcWork.right - mi.rcWork.left));
            h = Min(h, static_cast<int>(mi.rcWork.bottom - mi.rcWork.top));
        }
        SetWindowPos(hwnd_, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
    }
    if (settings_.hasCustomLocation) {
        RECT r;
        GetWindowRect(hwnd_, &r);
        const RECT target{settings_.windowX, settings_.windowY, settings_.windowX + (r.right - r.left),
                          settings_.windowY + (r.bottom - r.top)};
        if (MonitorFromRect(&target, MONITOR_DEFAULTTONULL)) // ensure the window is not off-screen
            SetWindowPos(hwnd_, nullptr, settings_.windowX, settings_.windowY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    ShowWindow(filterPanel_, settings_.showFilterPanel ? SW_SHOW : SW_HIDE);
    ShowWindow(advPanel_, settings_.showAdvancedBar ? SW_SHOW : SW_HIDE);
    ShowWindow(commentsPanel_, SW_HIDE);
    SetAlgorithm(settings_.defaultAlgo);
    SetupStandardColumns(false);
    SyncMenu();
    Layout();
}

void MainWindow::SaveSettings() {
    if (skipSaveOnClose_ || createMode_) return;
    settings_.pathPrefix = GetWindowTextUtf8(txtPrefix_);
    settings_.includePattern = GetWindowTextUtf8(txtInclude_);
    settings_.excludePattern = GetWindowTextUtf8(txtExclude_);
    settings_.scanRecursive = Button_GetCheck(chkRecursive_) == BST_CHECKED;
    settings_.enableChecksumComments = Button_GetCheck(chkComments_) == BST_CHECKED;

    // AppSettings::Save(SaveState) of the standard build: the window state goes into the
    // settings, then everything is written.
    AppSettings out = settings_;
    out.defaultAlgo = currentAlgo_;
    out.splitterDistance = activeVisible_ && splitterPos_ > 0 ? splitterPos_ : settings_.splitterDistance;
    // Restore bounds, so a minimized/maximized window does not save its minimized size.
    WINDOWPLACEMENT wp{};
    wp.length = sizeof wp;
    GetWindowPlacement(hwnd_, &wp);
    RECT r = wp.rcNormalPosition; // workspace coordinates: convert to screen
    if (HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST)) {
        MONITORINFO mi{};
        mi.cbSize = sizeof mi;
        GetMonitorInfoW(mon, &mi);
        OffsetRect(&r, mi.rcWork.left - mi.rcMonitor.left, mi.rcWork.top - mi.rcMonitor.top);
    }
    out.windowX = r.left, out.windowY = r.top;
    out.windowWidth = r.right - r.left, out.windowHeight = r.bottom - r.top;
    out.hasCustomLocation = true;
    out.columnOrder = CurrentColumnOrder();
    out.SaveCurrent();
}

void MainWindow::SetAlgorithm(HashType t) {
    currentAlgo_ = t;
    SyncMenu();
    UpdateIdleTitle();
}

void MainWindow::UpdateIdleTitle() {
    if (processing_ || !hwnd_ || createMode_) return;
    const Str algo = ToString(currentAlgo_);
    if (jobMode_) SetWindowTextUtf8(hwnd_, "SharpSFV - Job Queue [" + algo + "]");
    else if (verificationMode_) SetWindowTextUtf8(hwnd_, "SharpSFV - Verify [" + algo + "]");
    else SetWindowTextUtf8(hwnd_, "SharpSFV [" + algo + "]");
}

void MainWindow::OnClose() {
    closing_ = true;
    if (worker_.Running()) {
        engine_.Resume();
        engine_.Cancel();
        worker_.Join();
    }
    KillTimer(hwnd_, TIMER_FILTER);
    if (!createMode_) SaveSettings();
    DestroyWindow(hwnd_);
}

} // namespace sfv::gui
