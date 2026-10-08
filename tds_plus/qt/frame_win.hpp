// language: C++17, file: frame_win.hpp, runtime: Windows 10/11, target: tds+ frameless Qt window
// Win32 side of the custom title bar. The Qt window is created with Qt::FramelessWindowHint, this adds
// back the styles Windows needs for sizing, snapping and the shadow, and makes the client area cover the
// whole window (no Windows caption, no Windows buttons). The title bar itself is drawn in QML.
//
//   install(hwnd)   once, after the window exists
//   handle(...)     from a native event filter: WM_NCCALCSIZE and WM_NCHITTEST
//
// Needs Qt >= 6.7: older Qt mis-sizes such windows (QWindowKit works around it with
// the private "_q_windowsCustomMargins" property).
#pragma once

#include <windows.h>
#include <shellapi.h>

#include "frame_zone.hpp"

namespace tds_frame {

// Title bar geometry in device-independent pixels, read from QML.
struct Bar {
    int height_dp = 36;
    int buttons_dp = 138;
};

namespace detail {

constexpr int kGripDp = 6;
constexpr int kAutoHideTaskbarPx = 2;
constexpr DWORD kDwmaBorderColor = 34;
constexpr DWORD kDwmaColorNone = 0xFFFFFFFEu;

// Same layout as MARGINS from dwmapi.h, declared here so the header needs no extra include.
struct Margins {
    int left, right, top, bottom;
};

template <class Fn>
inline Fn proc_as(FARPROC p) {
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(p));
}

inline UINT dpi_for(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static const Fn fn = proc_as<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn) {
        if (const UINT dpi = fn(hwnd)) return dpi;
    }
    return 96;
}

inline int metric(int index, UINT dpi) {
    using Fn = int(WINAPI*)(int, UINT);
    static const Fn fn = proc_as<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"));
    return fn ? fn(index, dpi) : GetSystemMetrics(index);
}

inline int scaled(int dp, UINT dpi) { return MulDiv(dp, static_cast<int>(dpi), 96); }

// A maximised window overhangs its monitor by the resize frame; pull the client area back onto the
// screen, and leave a 2px edge where an auto-hide taskbar lives so it can still be revealed.
inline void fit_maximized(HWND hwnd, UINT dpi, RECT& r) {
    const int fx = metric(SM_CXSIZEFRAME, dpi) + metric(SM_CXPADDEDBORDER, dpi);
    const int fy = metric(SM_CYSIZEFRAME, dpi) + metric(SM_CXPADDEDBORDER, dpi);
    r.left += fx;
    r.right -= fx;
    r.top += fy;
    r.bottom -= fy;

    APPBARDATA state{};
    state.cbSize = sizeof state;
    if (!(SHAppBarMessage(ABM_GETSTATE, &state) & ABS_AUTOHIDE)) return;
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor || !GetMonitorInfoW(monitor, &mi)) return;
    const auto has_taskbar = [&](UINT edge) {
        APPBARDATA bar{};
        bar.cbSize = sizeof bar;
        bar.uEdge = edge;
        bar.rc = mi.rcMonitor;
        return SHAppBarMessage(0x0000000b /* ABM_GETAUTOHIDEBAREX */, &bar) != 0;
    };
    if (has_taskbar(ABE_TOP)) r.top += kAutoHideTaskbarPx;
    if (has_taskbar(ABE_BOTTOM)) r.bottom -= kAutoHideTaskbarPx;
    if (has_taskbar(ABE_LEFT)) r.left += kAutoHideTaskbarPx;
    if (has_taskbar(ABE_RIGHT)) r.right -= kAutoHideTaskbarPx;
}

inline LRESULT hit_code(Zone zone) {
    switch (zone) {
    case Zone::Caption: return HTCAPTION;
    case Zone::Top: return HTTOP;
    case Zone::Bottom: return HTBOTTOM;
    case Zone::Left: return HTLEFT;
    case Zone::Right: return HTRIGHT;
    case Zone::TopLeft: return HTTOPLEFT;
    case Zone::TopRight: return HTTOPRIGHT;
    case Zone::BottomLeft: return HTBOTTOMLEFT;
    case Zone::BottomRight: return HTBOTTOMRIGHT;
    case Zone::Client: break;
    }
    return HTCLIENT;
}

}  // namespace detail

inline void install(HWND hwnd) {
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style |= WS_THICKFRAME | WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);

    // dwmapi is optional: it gives the window its shadow and switches the system border off,
    // the QML side draws a 1px border of its own.
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        using Extend = HRESULT(WINAPI*)(HWND, const detail::Margins*);
        using SetAttr = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        const auto extend = detail::proc_as<Extend>(GetProcAddress(dwm, "DwmExtendFrameIntoClientArea"));
        const auto set_attr = detail::proc_as<SetAttr>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
        const detail::Margins margins{1, 1, 1, 1};
        if (extend) extend(hwnd, &margins);
        const DWORD none = detail::kDwmaColorNone;
        if (set_attr) set_attr(hwnd, detail::kDwmaBorderColor, &none, sizeof none);  // Windows 11 only
        FreeLibrary(dwm);
    }
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

// Returns true when the message is fully handled and *out is the window procedure result.
inline bool handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* out, const Bar& bar) {
    switch (msg) {
    case WM_NCCALCSIZE: {
        // client area == window, apart from the maximised overhang
        if (wp && IsZoomed(hwnd))
            detail::fit_maximized(hwnd, detail::dpi_for(hwnd), reinterpret_cast<NCCALCSIZE_PARAMS*>(lp)->rgrc[0]);
        *out = 0;
        return true;
    }
    case WM_NCHITTEST: {
        POINT pt{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
        ScreenToClient(hwnd, &pt);
        RECT client;
        GetClientRect(hwnd, &client);
        const UINT dpi = detail::dpi_for(hwnd);
        Layout layout;
        layout.width = client.right;
        layout.height = client.bottom;
        layout.border = detail::scaled(detail::kGripDp, dpi);
        layout.bar_height = detail::scaled(bar.height_dp, dpi);
        layout.buttons_width = detail::scaled(bar.buttons_dp, dpi);
        layout.maximized = IsZoomed(hwnd) != 0;
        *out = detail::hit_code(classify(layout, pt.x, pt.y));
        return true;
    }
    default:
        return false;
    }
}

}  // namespace tds_frame
