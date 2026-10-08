// titlebar_demo.cpp — bare window with the custom title bar from ui_titlebar.hpp.
// Preview of the style and reference for wiring it into the real UI.
// build: g++ -O2 -std=c++17 -mwindows -static src/titlebar_demo.cpp -o titlebar_demo.exe -lgdi32
#include <windows.h>

#include "ui_titlebar.hpp"

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT r;
    if (titlebar::handle(hwnd, msg, wp, lp, &r)) return r;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH body = CreateSolidBrush(titlebar::Theme().bg);
        FillRect(dc, &rc, body);
        DeleteObject(body);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, titlebar::Theme().fg_dim);
        RECT text{16, titlebar::height(hwnd) + 16, rc.right - 16, rc.bottom - 16};
        DrawTextW(dc, L"content starts at y = titlebar::height(hwnd)", -1, &text, DT_LEFT | DT_TOP | DT_WORDBREAK);
        titlebar::paint(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"tds_titlebar_demo";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"tds+", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                640, 420, nullptr, nullptr, inst, nullptr);
    titlebar::attach(hwnd);
    ShowWindow(hwnd, show);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
