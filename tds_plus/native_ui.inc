// ---- native dark UI: pure Win32/GDI, animated, no console, no browser ----
struct UiEntry {
    std::string name;   // display name (ability part)
    std::string tower;  // owner tower if known
    double cd = 0;      // wiki cooldown, seconds
    int mode = 0;       // 0 = spam, 1 = chain, 2 = off
    int seconds = 10;
};

// tds_wiki_db.ini: "Tower/Ability = cooldown_seconds" — the full available-ability list
struct WikiAbility { std::string tower, ability; double cd; };
static std::vector<WikiAbility> load_wiki_db(const std::string& path) {
    std::vector<WikiAbility> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == ';' || line[first] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        auto slash = key.find('/');
        if (slash == std::string::npos) continue;
        WikiAbility w;
        w.tower = key.substr(0, slash);
        w.ability = key.substr(slash + 1);
        auto trim = [](std::string& s) {
            const char* ws = " \t\r\n";
            auto b = s.find_first_not_of(ws), e = s.find_last_not_of(ws);
            s = b == std::string::npos ? "" : s.substr(b, e - b + 1);
        };
        trim(w.tower); trim(w.ability); trim(val);
        w.cd = atof(val.c_str());
        if (!w.tower.empty() && !w.ability.empty()) out.push_back(w);
    }
    return out;
}

struct UiShared {
    std::mutex mtx;
    bool running = false;
    std::vector<UiEntry> entries;
    std::unordered_map<std::string, double> fired_at;  // canon name -> seconds marker
    std::string phase = "boot";
    int slots = 0;
    std::vector<std::string> log;
};

static UiShared g_ui;
static volatile bool g_ui_dirty = false;
static std::chrono::steady_clock::time_point g_t0;

static void logf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%s\n", buf);
    fflush(stdout);
    std::lock_guard<std::mutex> lock(g_ui.mtx);
    g_ui.log.push_back(buf);
    if (g_ui.log.size() > 40) g_ui.log.erase(g_ui.log.begin());
}

static void ui_write_rules() {
    std::ofstream out("tds_ui_rules.ini", std::ios::trunc);
    out << "; written by tds+ ui — overlaid on tds_abilities.ini\n";
    std::lock_guard<std::mutex> lock(g_ui.mtx);
    std::unordered_map<std::string, bool> seen;
    for (auto& e : g_ui.entries) {
        std::string ck = canon_key(e.name);
        if (seen[ck]) continue;
        seen[ck] = true;
        const char* mode = e.mode == 0 ? "spam" : e.mode == 1 ? "chain" : "off";
        out << e.name << " = " << mode;
        if (e.mode == 1) out << " " << e.seconds;
        out << "\n";
    }
    g_ui_dirty = true;
}

// palette (componentry-inspired dark)
namespace clr {
    const COLORREF bg       = RGB(14, 14, 17);
    const COLORREF card     = RGB(23, 23, 28);
    const COLORREF card_hot = RGB(29, 29, 36);
    const COLORREF border   = RGB(40, 40, 48);
    const COLORREF text     = RGB(242, 242, 245);
    const COLORREF dim      = RGB(144, 144, 153);
    const COLORREF accent   = RGB(139, 157, 255);
    const COLORREF spam     = RGB(94, 224, 160);
    const COLORREF chain    = RGB(230, 194, 106);
    const COLORREF off      = RGB(224, 112, 112);
    const COLORREF pillbg   = RGB(38, 38, 46);
}

struct UiAnim { float appear = 0, hover = 0, glow = 0; };

static HWND g_ui_hwnd = nullptr;
static float g_knob = 0;                  // 0..1 toggle position, lerped
static int g_hover_row = -1;
static std::vector<UiAnim> g_anims;
static std::vector<RECT> g_row_rects, g_pill_rects;
static RECT g_toggle_rect{};

static const int UI_W = 500;
static const int UI_HEAD = 150;
static const int UI_ROW_H = 62;

static HFONT mkfont(int h, int weight) {
    return CreateFontA(-h, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, "Segoe UI");
}

static void rr(HDC dc, RECT r, int rad, COLORREF fill, COLORREF stroke) {
    HBRUSH b = CreateSolidBrush(fill);
    HGDIOBJ ob = SelectObject(dc, b);
    HPEN p = CreatePen(PS_SOLID, 1, stroke);
    HGDIOBJ op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

static void ui_paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT crc;
    GetClientRect(hwnd, &crc);
    HDC m = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, crc.right, crc.bottom);
    HGDIOBJ ob = SelectObject(m, bmp);
    HBRUSH bg = CreateSolidBrush(clr::bg);
    FillRect(m, &crc, bg);
    DeleteObject(bg);
    SetBkMode(m, TRANSPARENT);

    HFONT f_title = mkfont(30, 700);
    HFONT f_name = mkfont(16, 600);
    HFONT f_meta = mkfont(13, 400);
    HFONT f_pill = mkfont(13, 600);
    HFONT f_label = mkfont(11, 600);

    std::vector<UiEntry> entries;
    bool running;
    std::string phase;
    int slots;
    {
        std::lock_guard<std::mutex> lock(g_ui.mtx);
        entries = g_ui.entries;
        running = g_ui.running;
        phase = g_ui.phase;
        slots = g_ui.slots;
    }

    // header
    SelectObject(m, f_title);
    SetTextColor(m, clr::text);
    TextOutA(m, 28, 26, "tds", 3);
    SIZE sz{};
    GetTextExtentPoint32A(m, "tds", 3, &sz);
    SetTextColor(m, clr::accent);
    TextOutA(m, 28 + sz.cx, 26, "+", 1);
    SelectObject(m, f_meta);
    SetTextColor(m, clr::dim);
    std::string ph = phase + (slots ? " · " + std::to_string(slots) + " slots live" : "");
    TextOutA(m, 30, 62, ph.c_str(), (int)ph.size());

    // toggle
    RECT tr{28, 92, 28 + 64, 92 + 32};
    g_toggle_rect = tr;
    rr(m, tr, 32, running ? clr::spam : clr::pillbg, clr::border);
    int knob_x = tr.left + 4 + (int)((tr.right - tr.left - 28) * g_knob);
    HBRUSH kb = CreateSolidBrush(RGB(255, 255, 255));
    HGDIOBJ ok2 = SelectObject(m, kb);
    HGDIOBJ op2 = SelectObject(m, GetStockObject(NULL_PEN));
    Ellipse(m, knob_x, tr.top + 4, knob_x + 24, tr.top + 28);
    SelectObject(m, ok2);
    SelectObject(m, op2);
    DeleteObject(kb);

    SelectObject(m, f_label);
    SetTextColor(m, clr::dim);
    TextOutA(m, 28, 134, "A B I L I T I E S", 14);

    // rows
    g_row_rects.assign(entries.size(), RECT{});
    g_pill_rects.assign(entries.size(), RECT{});
    int y = UI_HEAD;
    for (size_t i = 0; i < entries.size(); i++) {
        auto& e = entries[i];
        auto& a = g_anims[i];
        int slide = (int)((1.0f - a.appear) * 10);
        RECT row{24, y + slide, UI_W - 24, y + slide + UI_ROW_H - 8};
        g_row_rects[i] = row;
        COLORREF fill = (int)i == g_hover_row ? clr::card_hot : clr::card;
        COLORREF edge = a.glow > 0.02f ? clr::spam : clr::border;
        rr(m, row, 16, fill, edge);
        if (a.glow > 0.02f) {   // soft outer pulse on recent fires
            RECT g2{row.left - 1, row.top - 1, row.right + 1, row.bottom + 1};
            HBRUSH nb = CreateSolidBrush(clr::bg);
            HGDIOBJ onb = SelectObject(m, nb);
            HPEN gp = CreatePen(PS_SOLID, 2, clr::spam);
            HGDIOBJ ogp = SelectObject(m, gp);
            RoundRect(m, g2.left, g2.top, g2.right, g2.bottom, 18, 18);
            SelectObject(m, onb);
            SelectObject(m, ogp);
            DeleteObject(nb);
            DeleteObject(gp);
        }

        SelectObject(m, f_name);
        SetTextColor(m, clr::text);
        TextOutA(m, row.left + 16, row.top + 10, e.name.c_str(), (int)e.name.size());
        SelectObject(m, f_meta);
        SetTextColor(m, clr::dim);
        std::string meta;
        if (!e.tower.empty()) meta = e.tower + (e.cd ? " · cd " + std::to_string((int)e.cd) + "s" : "");
        else meta = "live";
        auto fit = g_ui.fired_at.find(canon_key(e.name));
        // meta second part: last fire age (painted dim, static snapshot is fine)
        TextOutA(m, row.left + 16, row.top + 30, meta.c_str(), (int)meta.size());

        RECT pill{row.right - 118, row.top + (UI_ROW_H - 8 - 30) / 2, row.right - 14,
                  row.top + (UI_ROW_H - 8 - 30) / 2 + 30};
        g_pill_rects[i] = pill;
        COLORREF mc = e.mode == 0 ? clr::spam : e.mode == 1 ? clr::chain : clr::off;
        rr(m, pill, 28, mc, mc);
        SelectObject(m, f_pill);
        SetTextColor(m, RGB(16, 16, 20));
        char buf[32];
        const char* lbl = e.mode == 0 ? "spam" : e.mode == 2 ? "off"
                          : (sprintf(buf, "chain %ds", e.seconds), buf);
        DrawTextA(m, lbl, -1, &pill, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        y += UI_ROW_H;
    }

    if (entries.empty()) {
        SelectObject(m, f_meta);
        SetTextColor(m, clr::dim);
        const char* t = "watching — abilities appear as the game syncs them";
        RECT er{24, UI_HEAD + 30, UI_W - 24, UI_HEAD + 70};
        DrawTextA(m, t, -1, &er, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    DeleteObject(f_title);
    DeleteObject(f_name);
    DeleteObject(f_meta);
    DeleteObject(f_pill);
    DeleteObject(f_label);
    BitBlt(dc, 0, 0, crc.right, crc.bottom, m, 0, 0, SRCCOPY);
    SelectObject(m, ob);
    DeleteObject(bmp);
    DeleteDC(m);
    EndPaint(hwnd, &ps);
}

static void ui_resize(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    int h = UI_HEAD + (int)g_anims.size() * UI_ROW_H + 24;
    if (rc.bottom != h) {
        RECT wr;
        GetWindowRect(hwnd, &wr);
        SetWindowPos(hwnd, nullptr, 0, 0, wr.right - wr.left,
                     h + (wr.bottom - wr.top) - rc.bottom, SWP_NOMOVE | SWP_NOZORDER);
    }
}

static LRESULT CALLBACK ui_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        ui_paint(hwnd);
        return 0;
    case WM_TIMER: {
        bool running;
        {
            std::lock_guard<std::mutex> lock(g_ui.mtx);
            running = g_ui.running;
            size_t n = g_ui.entries.size();
            if (g_anims.size() != n) g_anims.resize(n);
        }
        float target = running ? 1.0f : 0.0f;
        g_knob += (target - g_knob) * 0.25f;
        double now_s = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - g_t0).count();
        for (size_t i = 0; i < g_anims.size(); i++) {
            auto& a = g_anims[i];
            a.appear = a.appear + (1.0f - a.appear) * 0.2f;
            float hv = (int)i == g_hover_row ? 1.0f : 0.0f;
            a.hover += (hv - a.hover) * 0.3f;
            std::string nm;
            {
                std::lock_guard<std::mutex> lock(g_ui.mtx);
                if (i < g_ui.entries.size()) {
                    auto it = g_ui.fired_at.find(canon_key(g_ui.entries[i].name));
                    if (it != g_ui.fired_at.end()) {
                        double age = now_s - it->second;
                        a.glow = age < 2.0 ? (float)(2.0 - age) / 2.0f : 0.0f;
                    }
                }
            }
        }
        ui_resize(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int y = (short)HIWORD(lp);
        int row = -1;
        for (size_t i = 0; i < g_row_rects.size(); i++)
            if (y >= g_row_rects[i].top && y < g_row_rects[i].bottom) { row = (int)i; break; }
        if (row != g_hover_row) {
            g_hover_row = row;
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_hover_row = -1;
        return 0;
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        if (x >= g_toggle_rect.left && x <= g_toggle_rect.right &&
            y >= g_toggle_rect.top && y <= g_toggle_rect.bottom) {
            std::lock_guard<std::mutex> lock(g_ui.mtx);
            g_ui.running = !g_ui.running;
            return 0;
        }
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(g_ui.mtx);
            for (size_t i = 0; i < g_pill_rects.size(); i++) {
                auto& r = g_pill_rects[i];
                if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
                    auto& e = g_ui.entries[i];
                    e.mode = (e.mode + 1) % 3;
                    changed = true;
                    break;
                }
            }
        }
        if (changed) ui_write_rules();
        return 0;
    }
    case WM_RBUTTONDOWN: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(g_ui.mtx);
            for (size_t i = 0; i < g_pill_rects.size(); i++) {
                auto& r = g_pill_rects[i];
                if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom &&
                    g_ui.entries[i].mode == 1) {
                    static const int opts[] = {5, 10, 15, 20, 30, 45, 60};
                    int k = 0;
                    while (k < 7 && opts[k] != g_ui.entries[i].seconds) k++;
                    g_ui.entries[i].seconds = opts[(k + 1) % 7];
                    changed = true;
                    break;
                }
            }
        }
        if (changed) ui_write_rules();
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static DWORD WINAPI ui_thread(LPVOID) {
    WNDCLASSA wc{};
    wc.lpfnWndProc = ui_wndproc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "tds_plus_ui";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(clr::bg);
    RegisterClassA(&wc);
    g_ui_hwnd = CreateWindowExA(0, "tds_plus_ui", "tds+",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, UI_W, 620,
                                nullptr, nullptr, wc.hInstance, nullptr);
    SetTimer(g_ui_hwnd, 1, 16, nullptr);   // ~60fps animation loop
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
