// ui_titlebar.hpp — dark, custom-drawn title bar for the Win32/GDI window.
// Replaces the stock Windows caption (title + minimise / maximise / close) but keeps what the OS
// already does well: drag, edge resize, snap, double-click maximise, system menu, Win11 snap layouts.
//
//   1. after CreateWindowEx:    titlebar::attach(hwnd);          // title = window text
//                               titlebar::attach(hwnd, theme, {false, false});  // fixed-size window:
//                               // min + close only, sizing blocked (WS_THICKFRAME is kept on purpose so
//                               // the DWM frame, shadow and corners look the same as for normal windows)
//                               titlebar::fit_client(hwnd, w, h);  // optional: exact client size
//   2. first thing in WndProc:  LRESULT r; if (titlebar::handle(hwnd, m, w, l, &r)) return r;
//   3. end of WM_PAINT:         titlebar::paint(hwnd, hdc);      // draws rows 0 .. height()-1
//   4. lay content out from y = titlebar::height(hwnd) (client coordinates)
//
// Colours live in titlebar::Theme. Links against user32/gdi32/shell32 only (dwmapi is loaded at
// runtime), so the existing `-lgdi32` build line keeps working.
#pragma once

#include <windows.h>
#include <shellapi.h>

#include <cmath>
#include <initializer_list>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#endif

#ifndef WM_NCMOUSELEAVE
#define WM_NCMOUSELEAVE 0x02A2
#endif
#ifndef TME_NONCLIENT
#define TME_NONCLIENT 0x00000010
#endif
#ifndef ABM_GETAUTOHIDEBAREX
#define ABM_GETAUTOHIDEBAREX 0x0000000b
#endif

namespace titlebar {

struct Theme {
    COLORREF bg            = RGB(22, 23, 27);     // match the window background for a seamless bar
    COLORREF fg            = RGB(232, 234, 240);
    COLORREF fg_dim        = RGB(127, 132, 147);  // title / glyphs while the window is inactive
    COLORREF hover         = RGB(44, 46, 54);
    COLORREF pressed       = RGB(35, 37, 44);
    COLORREF close_hover   = RGB(196, 43, 28);
    COLORREF close_pressed = RGB(160, 36, 24);
    COLORREF border        = RGB(46, 48, 56);     // top edge, separator, Win11 window border
};

namespace detail {

enum Btn { kNone, kMin, kMax, kClose };

struct State {
    Theme theme;
    HFONT font = nullptr;
    UINT font_dpi = 0;
    Btn hot = kNone;
    Btn pressed = kNone;
    bool active = true;
    bool tracking = false;
    bool can_max = true;
    bool can_resize = true;
};

constexpr wchar_t kProp[] = L"tds.titlebar";

inline State* state(HWND hwnd) { return static_cast<State*>(GetPropW(hwnd, kProp)); }

template <class Fn>
inline Fn proc_as(FARPROC p) {
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(p));
}

inline FARPROC user32_proc(const char* name) {
    return GetProcAddress(GetModuleHandleW(L"user32.dll"), name);
}

inline UINT dpi_for(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = proc_as<Fn>(user32_proc("GetDpiForWindow"));
    if (fn) {
        if (UINT d = fn(hwnd)) return d;
    }
    HDC dc = GetDC(nullptr);
    const int d = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return d > 0 ? static_cast<UINT>(d) : 96u;
}

inline int metric(int index, UINT dpi) {
    using Fn = int(WINAPI*)(int, UINT);
    static Fn fn = proc_as<Fn>(user32_proc("GetSystemMetricsForDpi"));
    return fn ? fn(index, dpi) : GetSystemMetrics(index);
}

inline int scale(int v, UINT dpi) { return MulDiv(v, static_cast<int>(dpi), 96); }
inline int bar_height(UINT dpi) { return scale(32, dpi); }
inline int button_width(UINT dpi) { return scale(46, dpi); }
inline int resize_handle(UINT dpi) { return metric(SM_CXPADDEDBORDER, dpi) + metric(SM_CYSIZEFRAME, dpi); }

// Buttons sit right to left: close, [maximise], minimise.
inline int slot_of(Btn b, bool can_max) { return b == kClose ? 1 : b == kMax ? 2 : can_max ? 3 : 2; }

inline Btn button_at(int x, int y, int client_w, UINT dpi, bool can_max) {
    if (x < 0 || x >= client_w || y < 0 || y >= bar_height(dpi)) return kNone;
    const int slot = (client_w - 1 - x) / button_width(dpi);
    if (slot == 0) return kClose;
    if (can_max) return slot == 1 ? kMax : slot == 2 ? kMin : kNone;
    return slot == 1 ? kMin : kNone;
}

inline Btn button_from_ht(WPARAM ht) {
    return ht == HTCLOSE ? kClose : ht == HTMAXBUTTON ? kMax : ht == HTMINBUTTON ? kMin : kNone;
}

inline RECT button_rect(Btn b, int client_w, UINT dpi, bool can_max) {
    const int bw = button_width(dpi);
    const int slot = slot_of(b, can_max);
    return RECT{client_w - slot * bw, 0, client_w - (slot - 1) * bw, bar_height(dpi)};
}

inline void invalidate_bar(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    rc.bottom = bar_height(dpi_for(hwnd));
    InvalidateRect(hwnd, &rc, FALSE);
}

inline void set_visual(HWND hwnd, State& s, Btn hot, Btn pressed) {
    if (s.hot == hot && s.pressed == pressed) return;
    s.hot = hot;
    s.pressed = pressed;
    invalidate_bar(hwnd);
}

inline void perform(HWND hwnd, Btn b) {
    const WPARAM cmd = b == kMin ? SC_MINIMIZE : b == kMax ? (IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE) : SC_CLOSE;
    PostMessageW(hwnd, WM_SYSCOMMAND, cmd, 0);
}

// Dark system menu / Win11 border colour. dwmapi is optional, failures are ignored.
inline void apply_dwm(HWND hwnd, const Theme& t) {
    using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static Fn set_attr = [] {
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        return dwm ? proc_as<Fn>(GetProcAddress(dwm, "DwmSetWindowAttribute")) : nullptr;
    }();
    if (!set_attr) return;
    const BOOL dark = TRUE;
    if (FAILED(set_attr(hwnd, 20, &dark, sizeof dark))) set_attr(hwnd, 19, &dark, sizeof dark);
    const COLORREF border = t.border;
    set_attr(hwnd, 34, &border, sizeof border);
}

// A maximised window overhangs the monitor by its resize frame; with an auto-hide taskbar leave a
// 2px edge so the taskbar can still be revealed.
inline void maximized_client_rect(HWND hwnd, UINT dpi, RECT& r) {
    r.top += resize_handle(dpi);
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    APPBARDATA st{};
    st.cbSize = sizeof st;
    if (!mon || !GetMonitorInfoW(mon, &mi) || !(SHAppBarMessage(ABM_GETSTATE, &st) & ABS_AUTOHIDE)) return;
    auto has_bar = [&](UINT edge) {
        APPBARDATA d{};
        d.cbSize = sizeof d;
        d.uEdge = edge;
        d.rc = mi.rcMonitor;
        return SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &d) != 0;
    };
    const int k = 2;
    if (has_bar(ABE_TOP)) r.top += k;
    if (has_bar(ABE_BOTTOM)) r.bottom -= k;
    if (has_bar(ABE_LEFT)) r.left += k;
    if (has_bar(ABE_RIGHT)) r.right -= k;
}

// ---- drawing ----

inline void fill(HDC dc, int l, int t, int r, int b, COLORREF c) {
    const RECT rc{l, t, r, b};
    SetBkColor(dc, c);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
}

inline COLORREF mix(COLORREF a, COLORREF b, double t) {
    auto ch = [t](int x, int y) { return static_cast<int>(x + (y - x) * t + 0.5); };
    return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
}

// Anti-aliased segment with round caps, blended over whatever is already in the DC.
inline void aa_line(HDC dc, double x1, double y1, double x2, double y2, double width, COLORREF fg) {
    const double dx = x2 - x1, dy = y2 - y1, len2 = dx * dx + dy * dy;
    const int pad = static_cast<int>(std::ceil(width)) + 1;
    const int x_lo = static_cast<int>(std::floor(x1 < x2 ? x1 : x2)) - pad;
    const int x_hi = static_cast<int>(std::ceil(x1 < x2 ? x2 : x1)) + pad;
    const int y_lo = static_cast<int>(std::floor(y1 < y2 ? y1 : y2)) - pad;
    const int y_hi = static_cast<int>(std::ceil(y1 < y2 ? y2 : y1)) + pad;
    for (int y = y_lo; y <= y_hi; ++y) {
        for (int x = x_lo; x <= x_hi; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            double t = len2 > 0 ? ((px - x1) * dx + (py - y1) * dy) / len2 : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            const double ex = x1 + t * dx - px, ey = y1 + t * dy - py;
            double a = width / 2 + 0.5 - std::sqrt(ex * ex + ey * ey);
            if (a <= 0) continue;
            if (a > 1) a = 1;
            SetPixelV(dc, x, y, mix(GetPixel(dc, x, y), fg, a));
        }
    }
}

inline void outline(HDC dc, int l, int t, int r, int b, int w, COLORREF c) {
    fill(dc, l, t, r, t + w, c);
    fill(dc, l, b - w, r, b, c);
    fill(dc, l, t, l + w, b, c);
    fill(dc, r - w, t, r, b, c);
}

inline void draw_glyph(HDC dc, Btn b, const RECT& r, COLORREF fg, COLORREF under, bool zoomed, UINT dpi) {
    const int g = scale(10, dpi);
    const int w = scale(1, dpi) < 1 ? 1 : scale(1, dpi);
    const int x0 = (r.left + r.right) / 2 - g / 2;
    const int y0 = (r.top + r.bottom) / 2 - g / 2;
    if (b == kMin) {
        fill(dc, x0, y0 + g / 2, x0 + g, y0 + g / 2 + w, fg);
    } else if (b == kMax && !zoomed) {
        outline(dc, x0, y0, x0 + g, y0 + g, w, fg);
    } else if (b == kMax) {
        const int o = scale(2, dpi), q = g - o;
        outline(dc, x0 + o, y0, x0 + g, y0 + q, w, fg);
        fill(dc, x0, y0 + o, x0 + q, y0 + g, under);
        outline(dc, x0, y0 + o, x0 + q, y0 + g, w, fg);
    } else {
        const double s = dpi / 96.0, lw = 1.25 * s;
        aa_line(dc, x0 + 0.5, y0 + 0.5, x0 + g - 0.5, y0 + g - 0.5, lw, fg);
        aa_line(dc, x0 + g - 0.5, y0 + 0.5, x0 + 0.5, y0 + g - 0.5, lw, fg);
    }
}

inline HFONT ensure_font(State& s, UINT dpi) {
    if (s.font && s.font_dpi == dpi) return s.font;
    if (s.font) DeleteObject(s.font);
    s.font = CreateFontW(-scale(12, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    s.font_dpi = dpi;
    return s.font;
}

inline void draw_bar(HDC dc, HWND hwnd, State& s, int w, int h, UINT dpi) {
    const Theme& t = s.theme;
    const bool zoomed = IsZoomed(hwnd) != 0;
    const int edge = zoomed ? 0 : 1;  // the stock top border disappears with the caption

    fill(dc, 0, 0, w, h, t.bg);
    if (edge) fill(dc, 0, 0, w, edge, t.border);
    fill(dc, 0, h - 1, w, h, t.border);

    for (Btn b : {kMin, kMax, kClose}) {
        if (b == kMax && !s.can_max) continue;
        const RECT r = button_rect(b, w, dpi, s.can_max);
        const bool hot = s.hot == b, down = hot && s.pressed == b;
        const COLORREF under = b == kClose ? (down ? t.close_pressed : hot ? t.close_hover : t.bg)
                                           : (down ? t.pressed : hot ? t.hover : t.bg);
        if (under != t.bg) fill(dc, r.left, edge, r.right, h - 1, under);
        const COLORREF fg = (b == kClose && hot) ? RGB(255, 255, 255) : (s.active || hot) ? t.fg : t.fg_dim;
        draw_glyph(dc, b, r, fg, under, zoomed, dpi);
    }

    wchar_t title[256] = L"";
    GetWindowTextW(hwnd, title, 256);
    RECT tr{scale(12, dpi), edge, w - (s.can_max ? 3 : 2) * button_width(dpi) - scale(8, dpi), h - 1};
    HGDIOBJ old_font = SelectObject(dc, ensure_font(s, dpi));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, s.active ? t.fg : t.fg_dim);
    DrawTextW(dc, title, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, old_font);
}

}  // namespace detail

// Height of the bar in pixels for the window's current DPI.
inline int height(HWND hwnd) { return detail::bar_height(detail::dpi_for(hwnd)); }

struct Options {
    bool maximizable = true;  // maximise / restore button, double-click and snap maximise
    bool resizable = true;    // edge resize; false also disables maximise
};

// Call once after the window is created.
inline void attach(HWND hwnd, const Theme& theme = Theme(), const Options& options = Options()) {
    if (!hwnd || detail::state(hwnd)) return;
    auto* s = new detail::State;
    s->theme = theme;
    s->can_resize = options.resizable;
    s->can_max = options.maximizable && options.resizable;
    SetPropW(hwnd, detail::kProp, s);
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style |= WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX;
    if (s->can_max) style |= WS_MAXIMIZEBOX;
    else style &= ~static_cast<LONG_PTR>(WS_MAXIMIZEBOX);
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    detail::apply_dwm(hwnd, theme);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

// Resizes the window (top-left stays) so the client area is exactly cw x ch. Call after attach().
inline void fit_client(HWND hwnd, int cw, int ch) {
    RECT wr, cr;
    if (!GetWindowRect(hwnd, &wr) || !GetClientRect(hwnd, &cr)) return;
    SetWindowPos(hwnd, nullptr, 0, 0, (wr.right - wr.left) - cr.right + cw, (wr.bottom - wr.top) - cr.bottom + ch,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Draws the bar into the top rows of the client area; call at the end of WM_PAINT with its HDC.
inline void paint(HWND hwnd, HDC hdc) {
    detail::State* s = detail::state(hwnd);
    if (!s) return;
    RECT cr;
    GetClientRect(hwnd, &cr);
    const UINT dpi = detail::dpi_for(hwnd);
    const int w = cr.right, h = detail::bar_height(dpi);
    if (w <= 0) return;
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem, bmp);
    detail::draw_bar(mem, hwnd, *s, w, h, dpi);
    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

// Returns true when the message was fully handled and *out is the WndProc result.
inline bool handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* out) {
    using namespace detail;
    State* s = state(hwnd);
    if (!s) return false;

    switch (msg) {
    case WM_NCCALCSIZE: {
        if (!wp) return false;
        auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
        const LONG top = p->rgrc[0].top;
        const LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);  // keeps the native left/right/bottom frame
        if (r != 0) {
            *out = r;
            return true;
        }
        p->rgrc[0].top = top;  // drop the caption
        if (IsZoomed(hwnd)) maximized_client_rect(hwnd, dpi_for(hwnd), p->rgrc[0]);
        *out = 0;
        return true;
    }
    case WM_NCHITTEST: {
        const LRESULT def = DefWindowProcW(hwnd, msg, wp, lp);
        if (def != HTCLIENT) {
            *out = (!s->can_resize && def >= HTLEFT && def <= HTBOTTOMRIGHT) ? static_cast<LRESULT>(HTBORDER) : def;
            return true;
        }
        POINT pt{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
        ScreenToClient(hwnd, &pt);
        RECT cr;
        GetClientRect(hwnd, &cr);
        const UINT dpi = dpi_for(hwnd);
        if (pt.y < 0 || pt.y >= bar_height(dpi)) return false;
        const Btn b = button_at(pt.x, pt.y, cr.right, dpi, s->can_max);
        if (b != kNone) *out = b == kClose ? HTCLOSE : b == kMax ? HTMAXBUTTON : HTMINBUTTON;
        else if (s->can_resize && !IsZoomed(hwnd) && pt.y < resize_handle(dpi)) *out = HTTOP;
        else *out = HTCAPTION;
        return true;
    }
    case WM_NCMOUSEMOVE:
        if (!s->tracking) {
            TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE | TME_NONCLIENT, hwnd, 0};
            s->tracking = TrackMouseEvent(&tme) != FALSE;
        }
        set_visual(hwnd, *s, button_from_ht(wp), s->pressed);
        return false;
    case WM_NCMOUSELEAVE:
        s->tracking = false;
        set_visual(hwnd, *s, kNone, kNone);
        return false;
    case WM_MOUSEMOVE:
    case WM_LBUTTONUP:
        set_visual(hwnd, *s, kNone, kNone);
        return false;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK: {
        const Btn b = button_from_ht(wp);
        if (b == kNone) return false;
        set_visual(hwnd, *s, b, b);
        *out = 0;
        return true;
    }
    case WM_NCLBUTTONUP: {
        const Btn b = button_from_ht(wp);
        if (b == kNone) return false;
        const Btn was = s->pressed;
        set_visual(hwnd, *s, b, kNone);
        if (b == was) perform(hwnd, b);
        *out = 0;
        return true;
    }
    case WM_SYSCOMMAND:
        if (!s->can_resize && (wp & 0xFFF0) == SC_SIZE) {  // keyboard resize via the system menu
            *out = 0;
            return true;
        }
        return false;
    case WM_NCACTIVATE:
        s->active = wp != 0;
        invalidate_bar(hwnd);
        return false;
    case WM_SIZE:
    case WM_SETTEXT:
        invalidate_bar(hwnd);
        return false;
    case WM_NCDESTROY:
        RemovePropW(hwnd, kProp);
        if (s->font) DeleteObject(s->font);
        delete s;
        return false;
    default:
        return false;
    }
}

}  // namespace titlebar
