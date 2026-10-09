// language: C++17, file: simple_ui.inc, runtime: Windows 11, target: tds_plus.cpp Win32/GDI interface
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <windowsx.h>

namespace ui_color {
    constexpr COLORREF bg = RGB(12, 14, 16);
    constexpr COLORREF panel = RGB(21, 24, 27);
    constexpr COLORREF panel_hot = RGB(27, 31, 34);
    constexpr COLORREF border = RGB(43, 48, 52);
    constexpr COLORREF text = RGB(238, 241, 241);
    constexpr COLORREF muted = RGB(143, 151, 154);
    constexpr COLORREF green = RGB(159, 228, 183);
    constexpr COLORREF amber = RGB(229, 194, 119);
    constexpr COLORREF red = RGB(224, 143, 143);
    constexpr COLORREF input = RGB(29, 33, 36);
}

static constexpr int ui_width = 820;
static constexpr int ui_height = 690;
static constexpr int list_top = 244;
static constexpr int list_bottom = 616;
static constexpr int row_height = 62;
static constexpr int visible_rows = 6;
static HWND g_ui_hwnd = nullptr;
static HWND g_search_hwnd = nullptr;
static HFONT g_search_font = nullptr;
static HBRUSH g_search_brush = nullptr;
static std::wstring g_filter;
static int g_scroll = 0;
static int g_filtered_count = 0;
static int g_hover_row = -1;
static RECT g_toggle_rect{650, 28, 790, 72};
static std::vector<RECT> g_mode_rects;
static std::vector<std::string> g_mode_keys;

static std::wstring ui_wide(const std::string& value) {
    if (value.empty()) return {};
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int length = MultiByteToWideChar(code_page, flags, value.c_str(), -1, nullptr, 0);
    if (!length) {
        code_page = CP_ACP;
        flags = 0;
        length = MultiByteToWideChar(code_page, flags, value.c_str(), -1, nullptr, 0);
    }
    if (!length) return {};
    std::wstring result(length, L'\0');
    MultiByteToWideChar(code_page, flags, value.c_str(), -1, result.data(), length);
    result.resize(length - 1);
    return result;
}

static std::wstring ui_lower(std::wstring value) {
    for (wchar_t& character : value) character = std::towlower(character);
    return value;
}

static std::wstring ui_phase(const std::string& phase) {
    if (phase == "in match") return L"Матч идёт";
    if (phase == "lobby / intermission") return L"Лобби / перерыв";
    if (phase == "waiting for Roblox") return L"Ожидание Roblox";
    if (phase == "unknown roblox version") return L"Неизвестная версия Roblox";
    if (phase == "access denied") return L"Нет доступа к Roblox";
    if (phase == "match over") return L"Матч завершён";
    return L"Подключение";
}

static HFONT ui_font(int pixels, int weight) {
    return CreateFontW(-pixels, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

static void ui_text(HDC dc, HFONT font, COLORREF color, const std::wstring& value,
                    RECT bounds, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX) {
    HGDIOBJ previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), -1, &bounds, flags);
    SelectObject(dc, previous);
}

static void ui_panel(HDC dc, RECT bounds, int radius, COLORREF fill, COLORREF stroke) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, stroke);
    HGDIOBJ previous_brush = SelectObject(dc, brush);
    HGDIOBJ previous_pen = SelectObject(dc, pen);
    RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, radius, radius);
    SelectObject(dc, previous_pen);
    SelectObject(dc, previous_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

static bool ui_contains(RECT bounds, int x, int y) {
    return x >= bounds.left && x < bounds.right && y >= bounds.top && y < bounds.bottom;
}

static void ui_paint(HWND window) {
    PAINTSTRUCT paint{};
    HDC target = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    HDC canvas = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ previous_bitmap = SelectObject(canvas, bitmap);
    HBRUSH background = CreateSolidBrush(ui_color::bg);
    FillRect(canvas, &client, background);
    DeleteObject(background);

    HFONT logo_font = ui_font(27, FW_BOLD);
    HFONT title_font = ui_font(20, FW_SEMIBOLD);
    HFONT name_font = ui_font(16, FW_SEMIBOLD);
    HFONT body_font = ui_font(13, FW_NORMAL);
    HFONT label_font = ui_font(11, FW_SEMIBOLD);
    HFONT button_font = ui_font(13, FW_SEMIBOLD);

    std::vector<UiEntry> entries;
    std::string phase;
    int slots = 0;
    bool running = false;
    {
        std::lock_guard<std::mutex> lock(g_ui.mtx);
        entries = g_ui.entries;
        phase = g_ui.phase;
        slots = g_ui.slots;
        running = g_ui.running;
    }

    ui_text(canvas, logo_font, ui_color::text, L"tds", {28, 16, 81, 57});
    ui_text(canvas, logo_font, ui_color::green, L"+", {76, 16, 105, 57});
    ui_text(canvas, body_font, ui_color::muted, L"Автопрожим способностей", {29, 61, 430, 86});
    ui_panel(canvas, g_toggle_rect, 14,
             running ? ui_color::green : ui_color::panel,
             running ? ui_color::green : ui_color::border);
    ui_text(canvas, button_font, running ? ui_color::bg : ui_color::text,
            running ? L"Остановить" : L"Запустить", g_toggle_rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    ui_panel(canvas, {28, 100, 792, 159}, 16, ui_color::panel, ui_color::border);
    ui_text(canvas, label_font, ui_color::muted, L"СОСТОЯНИЕ", {45, 103, 200, 126});
    const std::wstring status = ui_phase(phase) + L"  ·  " + std::to_wstring(slots) + L" слотов в игре";
    ui_text(canvas, title_font, ui_color::text, status, {44, 125, 610, 155},
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    ui_text(canvas, body_font, ui_color::muted, L"F6 — вкл/выкл", {620, 125, 774, 155},
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    ui_panel(canvas, {28, 170, 792, 212}, 12, ui_color::input, ui_color::border);
    ui_text(canvas, label_font, ui_color::muted, L"ПОИСК", {44, 175, 121, 207});
    ui_text(canvas, label_font, ui_color::muted, L"СПОСОБНОСТИ", {29, 216, 230, 240});

    struct DisplayEntry { UiEntry entry; std::string key; };
    std::vector<DisplayEntry> filtered;
    filtered.reserve(entries.size());
    for (const UiEntry& entry : entries) {
        std::wstring haystack = ui_lower(ui_wide(entry.tower + " " + entry.name));
        if (!g_filter.empty() && haystack.find(g_filter) == std::wstring::npos) continue;
        filtered.push_back({entry, canon_key(entry.name)});
    }
    std::stable_sort(filtered.begin(), filtered.end(), [](const DisplayEntry& a, const DisplayEntry& b) {
        return a.entry.live && !b.entry.live;
    });
    g_filtered_count = static_cast<int>(filtered.size());
    g_scroll = std::clamp(g_scroll, 0, std::max(0, g_filtered_count - visible_rows));
    ui_text(canvas, label_font, ui_color::muted,
            std::to_wstring(g_filtered_count) + L" НАЙДЕНО", {610, 216, 789, 240},
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    g_mode_rects.clear();
    g_mode_keys.clear();
    for (int shown = 0; shown < visible_rows && g_scroll + shown < g_filtered_count; ++shown) {
        const DisplayEntry& display = filtered[g_scroll + shown];
        const UiEntry& entry = display.entry;
        const int y = list_top + shown * row_height;
        const RECT row = {28, y, 792, y + 54};
        ui_panel(canvas, row, 13, shown == g_hover_row ? ui_color::panel_hot : ui_color::panel,
                 entry.live ? RGB(62, 99, 74) : ui_color::border);
        if (entry.live) {
            HBRUSH live_brush = CreateSolidBrush(ui_color::green);
            RECT dot = {42, y + 16, 49, y + 23};
            FillRect(canvas, &dot, live_brush);
            DeleteObject(live_brush);
        }
        const int text_x = entry.live ? 60 : 44;
        ui_text(canvas, name_font, ui_color::text, ui_wide(entry.name),
                {text_x, y + 7, 610, y + 31}, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        std::wstring meta = entry.tower.empty() ? L"Доступно" : ui_wide(entry.tower);
        if (entry.cd > 0) meta += L"  ·  " + std::to_wstring(static_cast<int>(entry.cd)) + L" с откат";
        ui_text(canvas, body_font, ui_color::muted, meta,
                {text_x, y + 30, 610, y + 49}, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        const RECT mode = {639, y + 12, 775, y + 42};
        const COLORREF mode_color = entry.mode == 0 ? ui_color::green
                                    : entry.mode == 1 ? ui_color::amber : ui_color::red;
        ui_panel(canvas, mode, 12, mode_color, mode_color);
        const std::wstring mode_text = entry.mode == 0 ? L"Авто"
                                       : entry.mode == 2 ? L"Выкл"
                                       : L"Цепь " + std::to_wstring(entry.seconds) + L" с";
        ui_text(canvas, button_font, ui_color::bg, mode_text, mode,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        g_mode_rects.push_back(mode);
        g_mode_keys.push_back(display.key);
    }

    if (filtered.empty()) {
        ui_text(canvas, body_font, ui_color::muted,
                entries.empty() ? L"Ожидание списка способностей" : L"Ничего не найдено",
                {30, list_top + 90, 790, list_top + 145},
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (g_filtered_count > visible_rows) {
        const int track_top = list_top + 2;
        const int track_height = list_bottom - list_top - 12;
        const int thumb_height = std::max(42, track_height * visible_rows / g_filtered_count);
        const int thumb_y = track_top + (track_height - thumb_height) * g_scroll /
                            (g_filtered_count - visible_rows);
        ui_panel(canvas, {798, track_top, 802, track_top + track_height}, 4,
                 ui_color::panel, ui_color::panel);
        ui_panel(canvas, {798, thumb_y, 802, thumb_y + thumb_height}, 4,
                 ui_color::muted, ui_color::muted);
    }

    HPEN rule = CreatePen(PS_SOLID, 1, ui_color::border);
    HGDIOBJ old_pen = SelectObject(canvas, rule);
    MoveToEx(canvas, 28, 624, nullptr);
    LineTo(canvas, 792, 624);
    SelectObject(canvas, old_pen);
    DeleteObject(rule);
    ui_text(canvas, body_font, ui_color::muted, L"ЛКМ: режим  ·  ПКМ: время цепочки",
            {28, 636, 480, 667});
    ui_text(canvas, body_font, ui_color::muted, L"F6: вкл/выкл  ·  End: выход",
            {480, 636, 792, 667}, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    BitBlt(target, 0, 0, client.right, client.bottom, canvas, 0, 0, SRCCOPY);
    DeleteObject(logo_font);
    DeleteObject(title_font);
    DeleteObject(name_font);
    DeleteObject(body_font);
    DeleteObject(label_font);
    DeleteObject(button_font);
    SelectObject(canvas, previous_bitmap);
    DeleteObject(bitmap);
    DeleteDC(canvas);
    EndPaint(window, &paint);
}

static void ui_cycle_mode(int visible_index, bool duration_only) {
    if (visible_index < 0 || visible_index >= static_cast<int>(g_mode_keys.size())) return;
    const std::string key = g_mode_keys[visible_index];
    int mode = 0, seconds = 10;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(g_ui.mtx);
        for (const UiEntry& entry : g_ui.entries) {
            if (canon_key(entry.name) != key) continue;
            mode = entry.mode;
            seconds = entry.seconds;
            found = true;
            break;
        }
    }
    if (!found) return;
    if (duration_only) {
        if (mode != 1) return;
        static constexpr int durations[] = {5, 10, 15, 20, 30, 45, 60};
        int position = 0;
        while (position < 7 && durations[position] != seconds) ++position;
        seconds = durations[(position + 1) % 7];
    } else {
        mode = (mode + 1) % 3;
    }
    if (tds_set_rule(key, mode, seconds)) InvalidateRect(g_ui_hwnd, nullptr, FALSE);
}

static LRESULT CALLBACK ui_wndproc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_CREATE:
        g_search_font = ui_font(15, FW_NORMAL);
        g_search_brush = CreateSolidBrush(ui_color::input);
        g_search_hwnd = CreateWindowExW(0, L"EDIT", L"",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                      125, 178, 653, 26, window,
                                      reinterpret_cast<HMENU>(1001), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g_search_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_search_font), TRUE);
        SendMessageW(g_search_hwnd, 0x1501, FALSE,
                     reinterpret_cast<LPARAM>(L"Поиск по башне или способности"));
        SetTimer(window, 1, 180, nullptr);
        return 0;
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(w_param), ui_color::text);
        SetBkColor(reinterpret_cast<HDC>(w_param), ui_color::input);
        return reinterpret_cast<LRESULT>(g_search_brush);
    case WM_COMMAND:
        if (LOWORD(w_param) == 1001 && HIWORD(w_param) == EN_CHANGE) {
            wchar_t buffer[256]{};
            GetWindowTextW(g_search_hwnd, buffer, 256);
            g_filter = ui_lower(buffer);
            g_scroll = 0;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_TIMER:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        ui_paint(window);
        return 0;
    case WM_MOUSEWHEEL: {
        static int wheel_carry = 0;  // 3 rows per notch, fractions of a notch are carried over
        wheel_carry += GET_WHEEL_DELTA_WPARAM(w_param) * 3;
        const int rows = wheel_carry / WHEEL_DELTA;
        wheel_carry -= rows * WHEEL_DELTA;
        if (rows == 0) return 0;
        g_scroll = std::clamp(g_scroll - rows, 0,
                              std::max(0, g_filtered_count - visible_rows));
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(l_param);
        const int y = GET_Y_LPARAM(l_param);
        const int row = x >= 28 && x < 792 && y >= list_top && y < list_bottom
                        ? (y - list_top) / row_height : -1;
        if (row != g_hover_row) {
            g_hover_row = row;
            InvalidateRect(window, nullptr, FALSE);
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_hover_row = -1;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        const int x = GET_X_LPARAM(l_param);
        const int y = GET_Y_LPARAM(l_param);
        if (ui_contains(g_toggle_rect, x, y)) {
            std::lock_guard<std::mutex> lock(g_ui.mtx);
            g_ui.running = !g_ui.running;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        for (int i = 0; i < static_cast<int>(g_mode_rects.size()); ++i)
            if (ui_contains(g_mode_rects[i], x, y)) { ui_cycle_mode(i, false); break; }
        return 0;
    }
    case WM_RBUTTONUP: {
        const int x = GET_X_LPARAM(l_param);
        const int y = GET_Y_LPARAM(l_param);
        for (int i = 0; i < static_cast<int>(g_mode_rects.size()); ++i)
            if (ui_contains(g_mode_rects[i], x, y)) { ui_cycle_mode(i, true); break; }
        return 0;
    }
    case WM_CLOSE:
        g_exit_requested.store(true);
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        DeleteObject(g_search_font);
        DeleteObject(g_search_brush);
        g_ui_hwnd = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

static DWORD WINAPI ui_thread(LPVOID) {
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = ui_wndproc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"tds_plus_simple_ui";
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassW(&window_class);

    RECT bounds{0, 0, ui_width, ui_height};
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&bounds, style, FALSE);
    const int outer_width = bounds.right - bounds.left;
    const int outer_height = bounds.bottom - bounds.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - outer_width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - outer_height) / 2;
    g_ui_hwnd = CreateWindowExW(0, window_class.lpszClassName, L"tds+",
                                style | WS_CLIPCHILDREN, x, y, outer_width, outer_height,
                                nullptr, nullptr, window_class.hInstance, nullptr);
    if (!g_ui_hwnd) { g_exit_requested.store(true); return 1; }
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm) {
        using DarkTitleBar = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto set_attribute = reinterpret_cast<DarkTitleBar>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
        if (set_attribute) {
            BOOL enabled = TRUE;
            set_attribute(g_ui_hwnd, 20, &enabled, sizeof(enabled));
        }
        FreeLibrary(dwm);
    }
    ShowWindow(g_ui_hwnd, SW_SHOW);
    UpdateWindow(g_ui_hwnd);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
