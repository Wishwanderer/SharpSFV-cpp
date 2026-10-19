// SharpSFV (C++ port) - colored container window.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Panel.h"

#include <unordered_map>

namespace sfv::gui {

namespace {

constexpr wchar_t kClass[] = L"SharpSFV.Panel";

struct PanelState {
    COLORREF bg = 0;
    HBRUSH brush = nullptr;
    std::unordered_map<HWND, COLORREF> textColors;
};

PanelState* State(HWND h) { return reinterpret_cast<PanelState*>(GetWindowLongPtrW(h, GWLP_USERDATA)); }

LRESULT CALLBACK PanelProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    PanelState* st = State(h);
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* s = new PanelState;
            s->bg = static_cast<COLORREF>(reinterpret_cast<UINT_PTR>(cs->lpCreateParams));
            s->brush = CreateSolidBrush(s->bg);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
            break;
        }
        case WM_NCDESTROY:
            if (st) {
                DeleteObject(st->brush);
                delete st;
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            }
            break;
        case WM_ERASEBKGND:
            if (st) {
                RECT rc;
                GetClientRect(h, &rc);
                FillRect(reinterpret_cast<HDC>(wp), &rc, st->brush);
                return 1;
            }
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            if (st) {
                HDC dc = reinterpret_cast<HDC>(wp);
                auto it = st->textColors.find(reinterpret_cast<HWND>(lp));
                SetTextColor(dc, it != st->textColors.end() ? it->second : GetSysColor(COLOR_WINDOWTEXT));
                SetBkColor(dc, st->bg);
                SetBkMode(dc, OPAQUE); // opaque, so text that changes length leaves no remnants
                return reinterpret_cast<LRESULT>(st->brush);
            }
            break;
        case WM_COMMAND:
        case WM_NOTIFY:
        case WM_CONTEXTMENU:
            return SendMessageW(GetParent(h), msg, wp, lp);
        // IsDialogMessage (used by the main window for Tab navigation) puts edit controls in
        // dialog mode: a multi-line edit then sends Esc as WM_CLOSE and Tab as WM_NEXTDLGCTL to
        // its parent, i.e. this panel. DefWindowProc would destroy the panel on WM_CLOSE.
        case WM_CLOSE:
            return 0;
        case WM_NEXTDLGCTL: {
            HWND root = GetAncestor(h, GA_ROOT);
            HWND next = LOWORD(lp) ? reinterpret_cast<HWND>(wp) : GetNextDlgTabItem(root, GetFocus(), wp != 0);
            if (next) SetFocus(next);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void Register() {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    done = true;
}

} // namespace

HWND CreatePanel(HWND parent, COLORREF background) {
    Register();
    return CreateWindowExW(WS_EX_CONTROLPARENT, kClass, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                           parent, nullptr, GetModuleHandleW(nullptr),
                           reinterpret_cast<void*>(static_cast<UINT_PTR>(background)));
}

void SetPanelBackground(HWND panel, COLORREF background) {
    if (PanelState* st = State(panel)) {
        DeleteObject(st->brush);
        st->bg = background;
        st->brush = CreateSolidBrush(background);
        InvalidateRect(panel, nullptr, TRUE);
    }
}

void SetChildTextColor(HWND panel, HWND child, COLORREF color) {
    if (PanelState* st = State(panel)) {
        st->textColors[child] = color;
        InvalidateRect(child, nullptr, TRUE);
    }
}

} // namespace sfv::gui
