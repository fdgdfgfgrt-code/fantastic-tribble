// language: C++17, file: tds_plus.cpp, runtime: MinGW/MSVC, target: Windows 11
// build: g++ -O2 -std=c++17 -static -mwindows tds_plus.cpp -o tds_plus_native.exe -lpsapi -lgdi32 -lws2_32
// tds+ external — auto-ability: reads PlayerGui.ReactGameAbilities from memory,
// presses each ability's own hotkey (binding label) the moment its cooldown
// (timeLeftAbility) clears; re-resolves the GUI as it rebuilds during play.
// control surface: Win32/GDI or Qt Quick, F6 = toggle, END = exit,
// log -> tds_plus.log
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>

#include <chrono>
#include <atomic>
#include <algorithm>
#include <cstdarg>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "backend_api.hpp"
#include "chain_state.hpp"

static std::atomic<bool> g_exit_requested{false};

#include "rbx_offsets.hpp"
#include "offset_updater.hpp"
#include "offsets_fetch.hpp"

struct GroupRule {
    int duration;
};

static HANDLE g_proc = nullptr;

template <typename T>
static T read(uintptr_t addr) {
    T v{};
    SIZE_T got = 0;
    ReadProcessMemory(g_proc, (LPCVOID)addr, &v, sizeof(T), &got);
    return v;
}

static bool valid_ptr(uintptr_t p) { return p >= 0x10000 && p < 0x7FFFFFFFFFFF; }

// plain std::string member (GUI properties): len@+0x10, inline or ptr@+0x00
static std::string read_string(uintptr_t addr) {
    if (!valid_ptr(addr)) return "";
    uint32_t len = read<uint32_t>(addr + 0x10);
    if (len == 0 || len > 1024) return "";
    uintptr_t at = addr;
    if (len > 15) {
        at = read<uintptr_t>(addr);
        if (!valid_ptr(at)) return "";
    }
    std::string out(len, '\0');
    SIZE_T got = 0;
    if (!ReadProcessMemory(g_proc, (LPCVOID)at, out.data(), len, &got) || got == 0) return "";
    out.resize(got);
    auto nul = out.find('\0');
    if (nul != std::string::npos) out.resize(nul);
    return out;
}

// Instance+0x70 -> struct{ header, std::string @+0x08 }: size@+0x18, cap@+0x20
static std::string get_name(uintptr_t inst) {
    uintptr_t np = read<uintptr_t>(inst + off::INST_NAME);
    if (!valid_ptr(np)) return "";
    uint64_t len = read<uint64_t>(np + 0x18);
    if (len == 0 || len > 1024) return "";
    uintptr_t at = np + 0x08;
    if (len > 15) {
        at = read<uintptr_t>(np + 0x08);
        if (!valid_ptr(at)) return "";
    }
    std::string out((size_t)len, '\0');
    SIZE_T got = 0;
    if (!ReadProcessMemory(g_proc, (LPCVOID)at, out.data(), (SIZE_T)len, &got) || got == 0) return "";
    out.resize(got);
    auto nul = out.find('\0');
    if (nul != std::string::npos) out.resize(nul);
    return out;
}

static std::string get_class_name(uintptr_t inst) {
    uintptr_t desc = read<uintptr_t>(inst + off::INST_CLASS_DESC);
    if (!valid_ptr(desc)) return "";
    uintptr_t str = read<uintptr_t>(desc + 0x8);
    if (!valid_ptr(str)) return "";
    if (read<uintptr_t>(str + 0x18) == 0x1F) str = read<uintptr_t>(str);
    return read_string(str);
}

static std::vector<uintptr_t> get_children(uintptr_t inst) {
    std::vector<uintptr_t> out;
    uintptr_t holder = read<uintptr_t>(inst + off::INST_CHILDREN);
    if (!valid_ptr(holder)) return out;
    uintptr_t cur = read<uintptr_t>(holder);
    uintptr_t end = read<uintptr_t>(holder + 0x8);
    if (!valid_ptr(cur) || !valid_ptr(end) || end < cur) return out;
    if ((end - cur) / 0x10 > 20000) return out;
    for (; cur != end; cur += 0x10) {
        uintptr_t child = read<uintptr_t>(cur);
        if (valid_ptr(child)) out.push_back(child);
    }
    return out;
}

static uintptr_t find_child(uintptr_t inst, const std::string& name) {
    for (uintptr_t c : get_children(inst))
        if (get_name(c) == name) return c;
    return 0;
}

static uintptr_t find_child_of_class(uintptr_t inst, const std::string& cls) {
    for (uintptr_t c : get_children(inst))
        if (get_class_name(c) == cls) return c;
    return 0;
}

static uintptr_t g_player_gui = 0;
static uintptr_t g_bar_frame = 0;
static uintptr_t g_banner_label = 0;  // hidden rewards retain their last banner text

// ---- serialized state stream (TDS custom serializer, len8 format) ----
// AbilityState record: 04 04 "name" 02 <len> <chars>  03 "uid" 02 36 <uuid>
//                      0b "cooldownEnd" 0c <double>     04 10 "cooldownDuration" 0c <double>
// ServerTime record:   0a "ServerTime" 09 06 <double>
struct StreamAbility { std::string name, uid; double cd_end = 0, cd_dur = 0; };

struct StreamState {
    double server_time = 0;
    std::unordered_map<std::string, StreamAbility> by_uid;
    std::vector<std::pair<uintptr_t, size_t>> regions;
    bool discovered = false;
    size_t scan_region = 0, scan_offset = 0;
    std::unordered_map<std::string, StreamAbility> scan_abilities;
};

static const uint8_t CDEND_TAG[] = {0x0b,'c','o','o','l','d','o','w','n','E','n','d'};
static const uint8_t ST_TAG[] = {0x0a,'S','e','r','v','e','r','T','i','m','e',0x09,0x06};

static bool abl_string(const uint8_t* w, size_t n, const char* key, std::string& out) {
    size_t kl = strlen(key);
    for (size_t i = 0; i + kl + 2 < n; i++) {
        if (memcmp(w + i, key, kl)) continue;
        size_t v = i + kl;
        if (w[v] != 0x02 || v + 2 > n) continue;
        uint8_t sl = w[v + 1];
        if (!sl || v + 2 + sl > n) continue;
        out.assign((const char*)(w + v + 2), sl);
        return true;
    }
    return false;
}

static void stream_discover(StreamState& st) {
    st.regions.clear();
    uintptr_t addr = 0;
    std::vector<uint8_t> buf(1 << 20);
    while (addr < 0x7FFFFFFFFFFFULL && !g_exit_requested.load()) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(g_proc, (LPCVOID)addr, &mbi, sizeof(mbi))) break;
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
        if (mbi.State != MEM_COMMIT || mbi.Type == MEM_IMAGE) continue;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) continue;
        if (!(mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY |
                             PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) continue;
        if (mbi.RegionSize > 512 * 1024 * 1024) continue;
        bool hit = false;
        for (size_t off = 0; off < mbi.RegionSize && !hit && !g_exit_requested.load(); off += buf.size()) {
            size_t want = std::min(buf.size(), (size_t)mbi.RegionSize - off);
            SIZE_T got = 0;
            if (!ReadProcessMemory(g_proc, (LPCVOID)((uintptr_t)mbi.BaseAddress + off),
                                   buf.data(), want, &got) || !got) continue;
            for (size_t i = 0; i + sizeof(CDEND_TAG) <= got; i++)
                if (!memcmp(buf.data() + i, CDEND_TAG, sizeof(CDEND_TAG))) { hit = true; break; }
        }
        if (hit) st.regions.push_back({(uintptr_t)mbi.BaseAddress, (size_t)mbi.RegionSize});
    }
    st.discovered = true;
}

static bool stream_scan(StreamState& st) {
    if (st.regions.empty()) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
    std::vector<uint8_t> buf((1 << 20) + 256);
    do {
        const auto [base, size] = st.regions[st.scan_region];
        const size_t leading = std::min(size_t{128}, st.scan_offset);
        const size_t start = st.scan_offset - leading;
        const size_t advance = std::min(size_t{1 << 20}, size - st.scan_offset);
        const size_t want = std::min(buf.size(), size - start);
        SIZE_T got = 0;
        ReadProcessMemory(g_proc, (LPCVOID)(base + start), buf.data(), want, &got);
        for (size_t i = 0; i + sizeof(ST_TAG) + 8 <= got; i++)
            if (!memcmp(buf.data() + i, ST_TAG, sizeof(ST_TAG))) {
                double v;
                memcpy(&v, buf.data() + i + sizeof(ST_TAG), 8);
                if (v > st.server_time && v < 1e7) st.server_time = v;  // monotonic per match
            }
        for (size_t i = 0; i + sizeof(CDEND_TAG) + 9 <= got; i++) {
            if (memcmp(buf.data() + i, CDEND_TAG, sizeof(CDEND_TAG))) continue;
            size_t ws = i > 120 ? i - 120 : 0;
            size_t we = std::min((size_t)got, i + 64);
            const uint8_t* w = buf.data() + ws;
            size_t n = we - ws;
            StreamAbility a;
            abl_string(w, n, "name", a.name);
            abl_string(w, n, "uid", a.uid);
            memcpy(&a.cd_end, buf.data() + i + sizeof(CDEND_TAG) + 1, 8);
            for (size_t j = 0; j + 26 <= n; j++)
                if (!memcmp(w + j, "cooldownDuration", 16) && w[j + 16] == 0x0c) {
                    memcpy(&a.cd_dur, w + j + 17, 8);
                    break;
                }
            if (a.name.empty() || a.uid.empty()) continue;
            st.scan_abilities[a.uid] = a;
            i += sizeof(CDEND_TAG);
        }
        st.scan_offset += advance;
        if (st.scan_offset >= size) { st.scan_offset = 0; ++st.scan_region; }
        if (st.scan_region >= st.regions.size()) {
            st.scan_region = 0;
            st.by_uid.swap(st.scan_abilities);
            st.scan_abilities.clear();
            return true;
        }
    } while (std::chrono::steady_clock::now() < deadline && !g_exit_requested.load());
    return false;
}

// ready count per ability name: uids whose cooldown has expired (0 = never used)
static std::unordered_map<std::string, int> stream_ready_counts(const StreamState& st) {
    std::unordered_map<std::string, int> out;
    for (auto& [uid, a] : st.by_uid) {
        bool ready = a.cd_end == 0 || a.cd_end <= st.server_time;
        if (ready) out[a.name]++;
    }
    return out;
}

struct AbilityBtn {
    uintptr_t content;      // TextButton "content"
    uintptr_t time_left;    // TextLabel "timeLeftAbility"
    uintptr_t binding;      // TextLabel "binding"
    uintptr_t locked;       // Frame "Locked"
    uintptr_t ammo;         // TextLabel "AmmoLabel"
    uintptr_t price;        // TextLabel "priceLabel"
    uintptr_t image;        // ImageButton "imageButton"
};

static void logf(const char* fmt, ...);

// GUI_IMAGE is only inferred for newer clients (see rbx_offsets.hpp), so until it has produced a real
// asset id the neighbouring offsets are tried and the first one that works becomes the offset.
static bool g_image_offset_ok = false;

static bool looks_like_asset(const std::string& s) {
    return s.rfind("rbxassetid://", 0) == 0 || s.rfind("http", 0) == 0;
}

static std::string btn_image_id(const AbilityBtn& b) {
    if (!b.image) return "";
    std::string s = read_string(b.image + off::GUI_IMAGE);
    if (!g_image_offset_ok) {
        if (looks_like_asset(s)) {
            g_image_offset_ok = true;
        } else {
            for (const intptr_t delta : {intptr_t{0x18}, intptr_t{-0x18}}) {
                const uintptr_t candidate = off::GUI_IMAGE + delta;
                const std::string probe = read_string(b.image + candidate);
                if (!looks_like_asset(probe)) continue;
                logf("image offset 0x%llx gave no asset id, 0x%llx did: using it",
                     (unsigned long long)off::GUI_IMAGE, (unsigned long long)candidate);
                off::GUI_IMAGE = candidate;
                g_image_offset_ok = true;
                s = probe;
                break;
            }
        }
    }
    const std::string pfx = "rbxassetid://";
    if (s.rfind(pfx, 0) == 0) s = s.substr(pfx.size());
    return s;
}

// ReactGameAbilities/Frame/<col>/<slot>/content
static std::vector<AbilityBtn> collect_buttons(uintptr_t gui) {
    std::vector<AbilityBtn> out;
    uintptr_t root = find_child(gui, "ReactGameAbilities");
    if (!root) return out;
    uintptr_t frame = find_child(root, "Frame");
    if (!frame) return out;
    for (uintptr_t col : get_children(frame)) {
        if (get_class_name(col) != "Frame") continue;
        for (uintptr_t slot : get_children(col)) {
            if (get_class_name(slot) != "Frame") continue;
            uintptr_t content = find_child(slot, "content");
            if (!content || get_class_name(content) != "TextButton") continue;
            AbilityBtn b{};
            b.content    = content;
            b.time_left  = find_child(content, "timeLeftAbility");
            b.binding    = find_child(content, "binding");
            b.locked     = find_child(content, "Locked");
            b.ammo       = find_child(content, "AmmoLabel");
            b.price      = find_child(content, "priceLabel");
            b.image      = find_child(content, "imageButton");
            if (b.time_left && b.binding) out.push_back(b);
        }
    }
    return out;
}

static bool gui_visible(uintptr_t inst) {
    return inst && read<uint8_t>(inst + off::GUI_VISIBLE) != 0;
}

static bool gui_effectively_visible(uintptr_t inst, uintptr_t player_gui) {
    if (!valid_ptr(player_gui)) return false;
    bool screen_gui_seen = false;
    for (int depth = 0; valid_ptr(inst) && depth < 32; ++depth) {
        if (inst == player_gui) return screen_gui_seen;
        const auto cls = get_class_name(inst);
        if (cls.empty()) return false;
        if (cls == "ScreenGui") {
            if (read<uint8_t>(inst + off::SCREEN_GUI_ENABLED) != 1) return false;
            screen_gui_seen = true;
        } else if (cls == "Frame" || cls == "ScrollingFrame" || cls == "CanvasGroup"
                   || cls == "TextLabel" || cls == "TextButton" || cls == "TextBox"
                   || cls == "ImageLabel" || cls == "ImageButton" || cls == "ViewportFrame"
                   || cls == "VideoFrame") {
            if (read<uint8_t>(inst + off::GUI_VISIBLE) != 1) return false;
        }
        inst = read<uintptr_t>(inst + off::INST_PARENT);
    }
    return false;
}

static bool match_has_ended(uintptr_t banner_label, uintptr_t player_gui) {
    if (!gui_effectively_visible(banner_label, player_gui)) return false;
    const auto text = read_string(banner_label + off::GUI_TEXT);
    return text == "YOU LOST" || text == "YOU WON";
}

static std::optional<int> btn_ready_count(const AbilityBtn& button) {
    const auto label = find_child(button.content, "AmmoLabel");
    if (!label || get_class_name(label) != "TextLabel"
        || read<uintptr_t>(label + off::INST_PARENT) != button.content) return std::nullopt;
    return parse_ready_count(read_string(label + off::GUI_TEXT));
}

// What the engine does with a slot. A slot whose ability is unknown (no id rule, and no name from the
// id table or the stream link) is never pressed: every per-ability choice is keyed by name, so pressing
// it would ignore them. It used to fall into the spam default, which is how Bounty got pressed before
// it was linked.
enum class SlotAction { Skip, Off, Chain, Spam };
static SlotAction slot_action(bool has_name, const GroupRule* rule) {
    if (rule) return rule->duration <= 0 ? SlotAction::Off : SlotAction::Chain;
    return has_name ? SlotAction::Spam : SlotAction::Skip;
}

// why (optional) receives a short reason when the slot cannot be pressed;
// on_cooldown (optional) is set when that reason is the normal cooldown timer
static bool btn_ready(const AbilityBtn& b, char* key_out, std::string* why = nullptr,
                      bool* on_cooldown = nullptr) {
    if (on_cooldown) *on_cooldown = false;
    const auto refuse = [&](std::string reason) {
        if (why) *why = std::move(reason);
        return false;
    };
    if (gui_visible(b.locked)) return refuse("is locked");
    std::string tl = read_string(b.time_left + off::GUI_TEXT);
    if (!tl.empty()) {                                              // counting down
        if (on_cooldown) *on_cooldown = true;
        return refuse("shows a cooldown timer \"" + tl + "\"");
    }
    if (gui_visible(b.price)) {
        std::string p = read_string(b.price + off::GUI_TEXT);
        if (!p.empty()) return refuse("costs money right now (" + p + ")");
    }
    if (b.ammo) {
        const auto count = btn_ready_count(b);
        if (!count) return refuse("ready count is unreadable");
        if (*count == 0) return refuse("has no ready charges");
    }
    std::string key = read_string(b.binding + off::GUI_TEXT);
    if (key.empty()) return refuse("has no hotkey label");
    *key_out = key[0];
    return true;
}

static void debug_btn(const AbilityBtn& b) {
    std::string key = read_string(b.binding + off::GUI_TEXT);
    std::string tl = read_string(b.time_left + off::GUI_TEXT);
    std::string ammo = b.ammo ? read_string(b.ammo + off::GUI_TEXT) : "(none)";
    std::string price = b.price ? read_string(b.price + off::GUI_TEXT) : "(none)";
    printf("  slot[%s] id=%s locked_vis=%d timeLeft=\"%s\" ammo=\"%s\" price=\"%s\" price_vis=%d\n",
           key.c_str(), btn_image_id(b).c_str(), (int)gui_visible(b.locked), tl.c_str(),
           ammo.c_str(), price.c_str(), b.price ? (int)gui_visible(b.price) : -1);
}

static HWND g_game_wnd = nullptr;

static BOOL CALLBACK enum_wnd_cb(HWND hwnd, LPARAM lp) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == (DWORD)lp && IsWindowVisible(hwnd) && GetWindowTextLengthA(hwnd) > 0) {
        g_game_wnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

static bool refresh_game_window(DWORD pid) {
    g_game_wnd = nullptr;
    EnumWindows(enum_wnd_cb, (LPARAM)pid);
    return g_game_wnd != nullptr;
}

// returns false when the game window could not be brought forward — caller must not press
static bool focus_game() {
    // the window may not exist yet when we attach, or may have been re-created since
    if (!g_game_wnd || !IsWindow(g_game_wnd)) refresh_game_window(GetProcessId(g_proc));
    if (!g_game_wnd) return false;
    DWORD game_pid = 0;
    if (g_game_wnd) GetWindowThreadProcessId(g_game_wnd, &game_pid);
    DWORD fg_pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &fg_pid);
    if (fg_pid != game_pid || game_pid == 0) {
        HWND fg = GetForegroundWindow();
        DWORD fg_tid = GetWindowThreadProcessId(fg, nullptr);
        DWORD my_tid = GetCurrentThreadId();
        AttachThreadInput(my_tid, fg_tid, TRUE);
        SetForegroundWindow(g_game_wnd);
        BringWindowToTop(g_game_wnd);
        AttachThreadInput(my_tid, fg_tid, FALSE);
        Sleep(120);
        GetWindowThreadProcessId(GetForegroundWindow(), &fg_pid);
    }
    return game_pid != 0 && fg_pid == game_pid;
}

// roblox reads scancodes — vk-only synthetic keys are ignored, KEYEVENTF_SCANCODE lands.
// never VkKeyScanA here: it resolves through the thread's keyboard layout and returns
// -1 for latin letters when the layout is cyrillic — letters map to vks directly
static bool press_key(char k) {
    WORD vk = 0;
    if (k >= 'A' && k <= 'Z') vk = (WORD)k;
    else if (k >= 'a' && k <= 'z') vk = (WORD)(k - 'a' + 'A');
    else if (k >= '0' && k <= '9') vk = (WORD)k;
    else return false;
    WORD scan = (WORD)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    INPUT in[2]{};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[0].ki.wScan = scan;
    in[0].ki.dwFlags = KEYEVENTF_SCANCODE;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    const UINT down = SendInput(1, in, sizeof(INPUT));
    Sleep(30 + rand() % 25);
    const UINT up = SendInput(1, in + 1, sizeof(INPUT));
    return down == 1 && up == 1;
}

static DWORD find_pid(const char* name) {
    DWORD pids[4096], bytes = 0;
    if (!EnumProcesses(pids, sizeof(pids), &bytes)) return 0;
    for (DWORD i = 0; i < bytes / sizeof(DWORD); i++) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
        if (!h) continue;
        char exe[MAX_PATH]{};
        DWORD n = MAX_PATH;
        QueryFullProcessImageNameA(h, 0, exe, &n);
        CloseHandle(h);
        const char* base = strrchr(exe, '\\');
        if (_stricmp(base ? base + 1 : exe, name) == 0) return pids[i];
    }
    return 0;
}

struct AbilityConfig {
    std::unordered_map<std::string, GroupRule> rules;        // asset id -> rule (digits keys)
    std::unordered_map<std::string, GroupRule> name_rules;   // ability name (lowercase) -> rule
    std::unordered_map<std::string, std::string> names;      // asset id -> display name (fallback)
};

// canonical ability key: lowercase, ability part only ("Commander/Call to Arms" -> "call to arms")
static std::string canon_key(std::string s) {
    auto slash = s.find('/');
    if (slash != std::string::npos) s = s.substr(slash + 1);
    for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    // in-game names drift from wiki names; aliases keep them the same rule
    static const std::pair<const char*, const char*> ALIASES[] = {
        {"call of arms", "call to arms"},
        {"drop the beat", "drop the beat"},
    };
    for (auto& [from, to] : ALIASES)
        if (s == from) return to;
    return s;
}

static bool is_digits(const std::string& s) {
    return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos;
}

// tds_abilities.ini:  "Tower/Ability = spam|chain N|off"  or  "asset_id = <same>"
// id-table lines ("digits = free text") feed the name map; everything else is a rule
static AbilityConfig load_ability_config(const std::string& path) {
    AbilityConfig cfg;
    std::ifstream in(path);
    std::string line;
    auto trim = [](std::string& s) {
        const char* ws = " \t\r\n";
        auto b = s.find_first_not_of(ws), e = s.find_last_not_of(ws);
        s = b == std::string::npos ? "" : s.substr(b, e - b + 1);
    };
    auto is_digits = [](const std::string& s) {
        return s.find_first_not_of("0123456789") == std::string::npos;
    };
    std::vector<std::pair<std::string, std::string>> pending_rules;
    while (std::getline(in, line)) {
        // strip trailing comment (but keep ';' out of names)
        auto semi = line.find(';');
        if (semi != std::string::npos) line = line.substr(0, semi);
        auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        trim(key); trim(val);
        if (key.empty() || val.empty()) continue;
        const std::string pfx = "rbxassetid://";
        if (key.rfind(pfx, 0) == 0) key = key.substr(pfx.size());
        std::string val_low = val;
        for (auto& c : val_low) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        bool is_mode = val_low.rfind("spam", 0) == 0 || val_low.rfind("chain", 0) == 0 ||
                       val_low.rfind("off", 0) == 0 || is_digits(val_low);
        if (is_digits(key) && !is_mode) {
            cfg.names[key] = val;                    // id-table entry
        } else if (is_mode) {
            pending_rules.push_back({key, val_low}); // name or id + mode
        }
    }
    // name -> id resolution pass (needs the full id table first)
    std::unordered_map<std::string, std::string> name_to_id;
    for (auto& [id, nm] : cfg.names) {
        std::string low = nm;
        for (auto& c : low) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        name_to_id[low] = id;
    }
    // name rules stay keyed by name — the state stream links buttons to names at runtime
    for (auto& [key, mode] : pending_rules) {
        if (mode.rfind("spam", 0) == 0) continue;   // spam is the default — no rule needed
        int dur = 0;
        if (mode.rfind("chain", 0) == 0) {
            dur = atoi(mode.c_str() + 5);
            if (dur <= 0) dur = 10;
        }
        if (is_digits(key)) {
            cfg.rules[key] = GroupRule{dur};
        } else {
            cfg.name_rules[canon_key(key)] = GroupRule{dur};
        }
    }
    return cfg;
}

// tds_ui_rules.ini: written by the UI — one "name = spam|chain N|off" per line,
// overlaid on top of tds_abilities.ini (wins on conflict)
static void load_ui_rules(AbilityConfig& cfg, const std::string& path) {
    std::ifstream in(path);
    std::string line;
    auto trim = [](std::string& s) {
        const char* ws = " \t\r\n";
        auto b = s.find_first_not_of(ws), e = s.find_last_not_of(ws);
        s = b == std::string::npos ? "" : s.substr(b, e - b + 1);
    };
    while (std::getline(in, line)) {
        auto semi = line.find(';');
        if (semi != std::string::npos) line = line.substr(0, semi);
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        trim(key); trim(val);
        if (key.empty() || val.empty()) continue;
        std::string low = canon_key(key);
        for (auto& c : val) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (val.rfind("spam", 0) == 0) {
            cfg.name_rules.erase(low);
            cfg.rules.erase(key);
        } else if (val.rfind("off", 0) == 0) {
            if (is_digits(key)) cfg.rules[key] = GroupRule{0};
            else cfg.name_rules[low] = GroupRule{0};
        } else if (val.rfind("chain", 0) == 0) {
            int dur = atoi(val.c_str() + 5);
            if (dur <= 0) dur = 10;
            if (is_digits(key)) cfg.rules[key] = GroupRule{dur};
            else cfg.name_rules[low] = GroupRule{dur};
        }
    }
}

// Shared control state; both desktop interfaces use the same engine.
struct UiEntry {
    std::string name;   // display name (ability part)
    std::string tower;  // owner tower if known
    double cd = 0;      // wiki cooldown, seconds
    int mode = 0;       // 0 = spam, 1 = chain, 2 = off
    int seconds = 10;
    bool live = false;
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
    std::unordered_map<std::string, std::pair<int, int>> rule_overrides;
};

static UiShared g_ui;
static std::atomic<bool> g_ui_dirty{false};
static std::chrono::steady_clock::time_point g_t0;

static void logf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (size_t n = strlen(buf); n > 0 && buf[n - 1] == '\n'; --n) buf[n - 1] = '\0';
    printf("%s\n", buf);
    fflush(stdout);
    std::lock_guard<std::mutex> lock(g_ui.mtx);
    g_ui.log.push_back(buf);
    if (g_ui.log.size() > 40) g_ui.log.erase(g_ui.log.begin());
}

static bool ui_write_rules() {
    std::ofstream out("tds_ui_rules.ini.tmp", std::ios::trunc);
    if (!out) return false;
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
    out.close();
    if (!out || !MoveFileExA("tds_ui_rules.ini.tmp", "tds_ui_rules.ini",
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;
    g_ui_dirty = true;
    return true;
}

static void ui_seed_entries(const AbilityConfig& config, const std::vector<WikiAbility>& wiki) {
    std::lock_guard<std::mutex> lock(g_ui.mtx);
    g_ui.entries.clear();
    auto append = [&](const std::string& name, const std::string& tower, double cooldown) {
        const auto key = canon_key(name);
        for (auto& existing : g_ui.entries) {
            if (canon_key(existing.name) != key) continue;
            if (existing.tower.empty()) existing.tower = tower;
            return;
        }
        UiEntry entry;
        entry.name = name;
        entry.tower = tower;
        entry.cd = cooldown;
        if (auto it = config.name_rules.find(canon_key(entry.name)); it != config.name_rules.end()) {
            entry.mode = it->second.duration <= 0 ? 2 : 1;
            entry.seconds = it->second.duration <= 0 ? 10 : it->second.duration;
        }
        g_ui.entries.push_back(entry);
    };
    for (const auto& ability : wiki) append(ability.ability, ability.tower, ability.cd);
    for (const auto& [id, name] : config.names) {
        const auto slash = name.find('/');
        append(slash == std::string::npos ? name : name.substr(slash + 1),
               slash == std::string::npos || name.substr(0, slash) == "?" ? "" : name.substr(0, slash), 0);
    }
    for (const auto& [name, rule] : config.name_rules) append(name, "", 0);
    g_ui.phase = "waiting for Roblox";
}

#ifndef TDS_PLUS_QT
#include "simple_ui.inc"
#endif

TdsSnapshot tds_snapshot() {
    std::lock_guard<std::mutex> lock(g_ui.mtx);
    TdsSnapshot snapshot;
    snapshot.running = g_ui.running;
    snapshot.exit_requested = g_exit_requested.load();
    snapshot.phase = g_ui.phase;
    snapshot.slots = g_ui.slots;
    snapshot.log = g_ui.log;
    for (const auto& entry : g_ui.entries)
        snapshot.abilities.push_back({canon_key(entry.name), entry.name, entry.tower,
                                      entry.cd, entry.mode, entry.seconds, entry.live});
    return snapshot;
}

void tds_set_running(bool running) {
    std::lock_guard<std::mutex> lock(g_ui.mtx);
    g_ui.running = running;
}

bool tds_set_rule(const std::string& key, int mode, int seconds) {
    if (mode < 0 || mode > 2 || seconds < 1 || seconds > 600) return false;
    int old_mode = 0, old_seconds = 10;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(g_ui.mtx);
        for (auto& entry : g_ui.entries) {
            if (canon_key(entry.name) != key) continue;
            old_mode = entry.mode;
            old_seconds = entry.seconds;
            entry.mode = mode;
            entry.seconds = seconds;
            g_ui.rule_overrides[key] = {mode, seconds};
            found = true;
            break;
        }
    }
    if (!found) return false;
    if (ui_write_rules()) return true;
    std::lock_guard<std::mutex> lock(g_ui.mtx);
    for (auto& entry : g_ui.entries) {
        if (canon_key(entry.name) != key) continue;
        entry.mode = old_mode;
        entry.seconds = old_seconds;
    }
    g_ui.rule_overrides[key] = {old_mode, old_seconds};
    return false;
}

void tds_request_exit() { g_exit_requested.store(true); }

// legacy tds_chain.ini: one "asset_id=duration_seconds" per line
static std::unordered_map<std::string, GroupRule> load_chain_rules(const std::string& path) {
    std::unordered_map<std::string, GroupRule> rules;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == ';' || line[first] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string id = line.substr(0, eq), dur = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            const char* ws = " \t\r\n";
            auto b = s.find_first_not_of(ws), e = s.find_last_not_of(ws);
            s = b == std::string::npos ? "" : s.substr(b, e - b + 1);
        };
        trim(id); trim(dur);
        const std::string pfx = "rbxassetid://";
        if (id.rfind(pfx, 0) == 0) id = id.substr(pfx.size());
        if (id.empty() || dur.empty()) continue;
        rules[id] = GroupRule{atoi(dur.c_str())};
    }
    return rules;
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS* ep) {
    // a dying thread must not take the process down silently — log the code
    FILE* f = fopen("tds_plus_crash.log", "a");
    if (f) {
        fprintf(f, "exception 0x%lx at %p\n", ep->ExceptionRecord->ExceptionCode,
                ep->ExceptionRecord->ExceptionAddress);
        fclose(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---- Roblox offsets: learned online for a new client version (see offset_updater.hpp) ----

// the download runs on its own thread so a slow network cannot stall the engine loop (F6, END, exit)
struct NetFetch {
    std::mutex mtx;
    bool done = false;
    std::string body, error;
};
static std::shared_ptr<NetFetch> g_net_fetch;  // engine thread only; the worker holds its own reference

static std::string read_text_file(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

static bool write_text_file(const char* path, const std::string& text) {
    const std::string temp = std::string(path) + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << text;
        out.close();
        if (!out) return false;
    }
    return MoveFileExA(temp.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

static OffsetUpdater::Io make_offset_io() {
    OffsetUpdater::Io io;
    io.load_settings = [] { return read_text_file("tds_offsets.ini"); };
    io.load_cache = [] { return read_text_file("tds_offsets_cache.ini"); };
    io.save_cache = [](const std::string& text) { return write_text_file("tds_offsets_cache.ini", text); };
    io.log = [](const std::string& line) { logf("%s", line.c_str()); };
    io.start_fetch = [](const std::string& url) {
        auto fetch = std::make_shared<NetFetch>();
        g_net_fetch = fetch;
        std::thread([fetch, url] {
            std::string body, error;
            if (!tds_net::https_get(url, body, error) && error.empty()) error = "the download failed";
            std::lock_guard<std::mutex> lock(fetch->mtx);
            fetch->body = std::move(body);
            fetch->error = std::move(error);
            fetch->done = true;
        }).detach();
    };
    io.poll_fetch = [](std::string& body, std::string& error) {
        if (!g_net_fetch) return false;
        {
            std::lock_guard<std::mutex> lock(g_net_fetch->mtx);
            if (!g_net_fetch->done) return false;
            body = std::move(g_net_fetch->body);
            error = std::move(g_net_fetch->error);
        }
        g_net_fetch.reset();
        return true;
    };
    io.applied = [] { g_image_offset_ok = false; };  // the icon offset is probed again for the new set
    return io;
}

int tds_run_engine(bool offline) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetUnhandledExceptionFilter(crash_filter);
#ifndef TDS_PLUS_QT
    char executable_path[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, executable_path, MAX_PATH)) {
        if (char* separator = strrchr(executable_path, '\\')) {
            *separator = '\0';
            SetCurrentDirectoryA(executable_path);
        }
    }
#endif
    freopen("tds_plus.log", "w", stdout);
    g_t0 = std::chrono::steady_clock::now();
    srand(GetTickCount() ^ GetCurrentProcessId());
    AbilityConfig acfg = load_ability_config("tds_abilities.ini");
    if (acfg.rules.empty() && acfg.names.empty()) {
        auto legacy = load_chain_rules("tds_chain.ini");
        if (!legacy.empty()) {
            acfg.rules = std::move(legacy);
            logf("legacy tds_chain.ini loaded (%zu)", acfg.rules.size());
        }
    }
    auto& chain_rules = acfg.rules;
    load_ui_rules(acfg, "tds_ui_rules.ini");
    if (!chain_rules.empty() || !acfg.name_rules.empty())
        logf("ability rules: %zu by id, %zu by name", chain_rules.size(), acfg.name_rules.size());

    auto wiki_db = load_wiki_db("tds_wiki_db.ini");
    if (!wiki_db.empty()) logf("wiki db: %zu abilities", wiki_db.size());
    ui_seed_entries(acfg, wiki_db);

#ifndef TDS_PLUS_QT
    HANDLE ui_handle = CreateThread(nullptr, 0, ui_thread, nullptr, 0, nullptr);
    if (!ui_handle) { logf("CreateThread failed: %lu", GetLastError()); return 1; }
    CloseHandle(ui_handle);
#endif

    if (offline) {
        { std::lock_guard<std::mutex> lock(g_ui.mtx); g_ui.phase = "offline preview"; }
        while (!g_exit_requested.load()) Sleep(60);
        return 0;
    }

    while (!g_exit_requested.load()) {

        DWORD pid = 0;
        uintptr_t base = 0;
        while (!g_exit_requested.load()) {
            if (GetAsyncKeyState(VK_END) & 0x8000) { g_exit_requested.store(true); break; }
            pid = find_pid("RobloxPlayerBeta.exe");
            if (pid) {
                g_proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, pid);
                if (g_proc) {
                    HMODULE mods[8]{};
                    DWORD bytes = 0;
                    if (EnumProcessModules(g_proc, mods, sizeof(mods), &bytes) && bytes >= sizeof(HMODULE)) {
                        base = reinterpret_cast<uintptr_t>(mods[0]);
                        break;
                    }
                    CloseHandle(g_proc);
                    g_proc = nullptr;
                }
                std::lock_guard<std::mutex> lock(g_ui.mtx);
                g_ui.phase = "access denied";
            } else {
                std::lock_guard<std::mutex> lock(g_ui.mtx);
                g_ui.phase = "waiting for Roblox";
            }
            Sleep(500);
        }
        if (!g_proc || g_exit_requested.load()) {
            if (g_proc) { CloseHandle(g_proc); g_proc = nullptr; }
            return 0;
        }
        acfg = load_ability_config("tds_abilities.ini");
        load_ui_rules(acfg, "tds_ui_rules.ini");
        refresh_game_window(pid);
        logf("tds+ attached: pid=%lu base=0x%llx window=0x%llx", pid,
             (unsigned long long)base, (unsigned long long)(uintptr_t)g_game_wnd);
        // Offsets differ between Roblox versions. The updater picks the set of the running client
        // (built in, or learned earlier) and, for a version it does not know, looks the offsets up online.
        char image_path[MAX_PATH]{};
        DWORD image_len = MAX_PATH;
        const std::string client_version = QueryFullProcessImageNameA(g_proc, 0, image_path, &image_len)
            ? off::roblox_version_from_path(image_path) : std::string();
        OffsetUpdater offset_updater(make_offset_io());
        offset_updater.attach(client_version, std::chrono::steady_clock::now());

        StreamState stream;
        logf("scanning state stream regions...\n");
        stream_discover(stream);
        size_t stream_mb = 0;
        for (auto& [b, s] : stream.regions) stream_mb += s;
        logf("%zu stream regions (%.1f MB)\n", stream.regions.size(), stream_mb / 1048576.0);

        bool running = false, t_held = false, d_held = false;
        std::vector<AbilityBtn> buttons;
        bool needs_link = false;
        std::unordered_map<std::string, std::string> link;   // button asset id -> ability name (from stream)
        std::unordered_set<std::string> unidentified_logged;  // slot ids already reported as unidentified
        std::unordered_map<std::string, ChainState> chain_states;
        std::string last_phase;
        struct PressBlock { std::string key; std::chrono::steady_clock::time_point since; bool logged = false; };
        PressBlock press_block;
        auto last_buttons = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        auto last_resolve = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        logf("auto-ability starts OFF: press Start or F6 (the log shows [on]) | F7 = debug slots | END = exit\n");

        while (!g_exit_requested.load()) {
            if (WaitForSingleObject(g_proc, 0) == WAIT_OBJECT_0) {
                logf("Roblox closed");
                break;
            }
            // UI and F6 share the same flag — UI wins when it changes
            bool flipped = false;
            {
                std::lock_guard<std::mutex> lock(g_ui.mtx);
                if (g_ui.running != running) { running = g_ui.running; flipped = true; }
            }
            if (flipped) logf("[%s]", running ? "on" : "off");   // never logf inside the lock
            if (g_ui_dirty.exchange(false)) {
                acfg = load_ability_config("tds_abilities.ini");
                load_ui_rules(acfg, "tds_ui_rules.ini");
                chain_rules = acfg.rules;
                logf("config reloaded: %zu by id, %zu by name\n", chain_rules.size(), acfg.name_rules.size());
            }
            bool t = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
            if (t && !t_held) {
                running = !running;
                { std::lock_guard<std::mutex> lock(g_ui.mtx); g_ui.running = running; }
                logf("[%s]\n", running ? "on" : "off");
            }
            t_held = t;
            bool d = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
            if (d && !d_held) {
                logf("slot debug (%zu):\n", buttons.size());
                for (const auto& b : buttons) debug_btn(b);
            }
            d_held = d;
            if (GetAsyncKeyState(VK_END) & 0x8000) { g_exit_requested.store(true); break; }

            auto now = std::chrono::steady_clock::now();
            if (g_player_gui && now - last_buttons >= std::chrono::milliseconds(200)) {
                last_buttons = now;
                buttons = collect_buttons(g_player_gui);
                const auto ability_gui = find_child(g_player_gui, "ReactGameAbilities");
                g_bar_frame = ability_gui ? find_child(ability_gui, "Frame") : 0;
            }
            if (now - last_resolve > std::chrono::seconds(2)) {   // GUI rebuilds as towers change
                last_resolve = now;
                uintptr_t ve = read<uintptr_t>(base + off::VE_POINTER);
                uintptr_t fdm = valid_ptr(ve) ? read<uintptr_t>(ve + off::VE_FAKE_DM) : 0;
                uintptr_t dm = valid_ptr(fdm) ? read<uintptr_t>(fdm + off::FAKE_REAL_DM) : 0;
                uintptr_t players = valid_ptr(dm) ? find_child_of_class(dm, "Players") : 0;
                offset_updater.tick(now, players != 0);
                uintptr_t lp = players ? read<uintptr_t>(players + off::PLAYERS_LOCAL) : 0;
                uintptr_t gui = valid_ptr(lp) ? find_child(lp, "PlayerGui") : 0;
                if (gui && g_player_gui && g_player_gui != gui) chain_states.clear();
                g_player_gui = gui;
                buttons = gui ? collect_buttons(gui) : std::vector<AbilityBtn>{};
                uintptr_t rga = gui ? find_child(gui, "ReactGameAbilities") : 0;
                g_bar_frame = rga ? find_child(rga, "Frame") : 0;
                uintptr_t rnr = gui ? find_child(gui, "ReactGameNewRewards") : 0;
                uintptr_t rnr_frame = rnr ? find_child(rnr, "Frame") : 0;
                uintptr_t gameover = rnr_frame ? find_child(rnr_frame, "gameOver") : 0;
                uintptr_t rs = gameover ? find_child(gameover, "RewardsScreen") : 0;
                uintptr_t rb = rs ? find_child(rs, "RewardBanner") : 0;
                g_banner_label = rb ? find_child(rb, "textLabel") : 0;

                // state stream: fresh ability cooldowns -> link buttons to ability names
                needs_link = false;
                for (const auto& button : buttons) {
                    const auto id = btn_image_id(button);
                    if (!id.empty() && !link.count(id) && !acfg.names.count(id)) needs_link = true;
                }

                static size_t last_n = SIZE_MAX;
                if (buttons.size() != last_n) {
                    std::string banner_now = g_banner_label ? read_string(g_banner_label + off::GUI_TEXT) : "";
                    logf("ability slots: %zu | end-screen: %s \"%s\"\n", buttons.size(),
                         gui_effectively_visible(g_banner_label, g_player_gui) ? "visible" : "hidden",
                         banner_now.c_str());
                    for (const auto& b : buttons) {
                        std::string id = btn_image_id(b);
                        std::string key = read_string(b.binding + off::GUI_TEXT);
                        std::string nm;
                        if (link.count(id)) nm = link[id];
                        else if (acfg.names.count(id)) nm = acfg.names[id];
                        std::string disp = nm.empty() ? "" : " \"" + nm + "\"";
                        const GroupRule* rule = nullptr;
                        if (auto it = chain_rules.find(id); it != chain_rules.end()) rule = &it->second;
                        else if (!nm.empty()) {
                            if (auto name_rule = acfg.name_rules.find(canon_key(nm)); name_rule != acfg.name_rules.end()) rule = &name_rule->second;
                        }
                        const char* mode = !rule ? (nm.empty() ? "(not identified, not pressed)" : "(spam)")
                                                 : rule->duration <= 0 ? "(off)" : "(chain)";
                        logf("  slot[%s] id=%s%s %s\n", key.c_str(), id.c_str(), disp.c_str(), mode);
                    }
                    last_n = buttons.size();
                }

                // sync the UI entry list with what's live + what's configured + the full wiki list
                {
                    std::lock_guard<std::mutex> lock(g_ui.mtx);
                    g_ui.entries.clear();
                    auto push_entry = [&](const std::string& name, const std::string& tower = "",
                                          double cd = 0, bool live = false) {
                        std::string ck = canon_key(name);
                        for (auto& e : g_ui.entries) {
                            if (canon_key(e.name) != ck) continue;
                            if (!tower.empty()) { e.tower = tower; e.cd = cd; }   // enrich existing
                            e.live = e.live || live;
                            return;
                        }
                        UiEntry e;
                        e.name = name;
                        e.tower = tower;
                        e.cd = cd;
                        e.live = live;
                        if (auto it = acfg.name_rules.find(ck); it != acfg.name_rules.end()) {
                            e.mode = it->second.duration <= 0 ? 2 : 1;
                            e.seconds = it->second.duration <= 0 ? 10 : it->second.duration;
                        }
                        if (auto it = g_ui.rule_overrides.find(ck); it != g_ui.rule_overrides.end()) {
                            e.mode = it->second.first;
                            e.seconds = it->second.second;
                        }
                        g_ui.entries.push_back(e);
                    };
                    for (const auto& w : wiki_db)
                        push_entry(w.ability, w.tower, w.cd);   // everything available, wiki-first
                    for (const auto& b : buttons) {
                        std::string id = btn_image_id(b);
                        std::string nm;
                        if (link.count(id)) nm = link[id];
                        else if (acfg.names.count(id)) nm = acfg.names[id];
                        if (!nm.empty()) {
                            auto slash = nm.find('/');
                            push_entry(slash != std::string::npos ? nm.substr(slash + 1) : nm,
                                       "", 0, true);
                        }
                    }
                    for (auto& [low, r] : acfg.name_rules)
                        push_entry(low);   // configured but not currently slotted
                    for (auto& [id, nm] : acfg.names) {
                        auto slash = nm.find('/');
                        push_entry(slash != std::string::npos ? nm.substr(slash + 1) : nm);
                    }
                }
            }

            if (needs_link && stream.discovered && stream_scan(stream)) {
                const auto ready = stream_ready_counts(stream);
                std::unordered_map<std::string, std::vector<std::string>> candidates;
                std::unordered_map<std::string, bool> known_names;
                for (const auto& button : buttons) {
                    const auto id = btn_image_id(button);
                    if (auto found = acfg.names.find(id); found != acfg.names.end()) known_names[canon_key(found->second)] = true;
                    if (auto found = link.find(id); found != link.end()) known_names[canon_key(found->second)] = true;
                    if (id.empty() || link.count(id) || acfg.names.count(id)) continue;
                    const auto count = button.ammo ? parse_ready_count(read_string(button.ammo + off::GUI_TEXT)) : std::nullopt;
                    if (count) candidates[std::to_string(*count)].push_back(id);
                }
                std::unordered_map<std::string, std::vector<std::string>> names_by_count;
                for (const auto& [name, count] : ready)
                    if (count > 0 && !known_names.count(canon_key(name))) names_by_count[std::to_string(count)].push_back(name);
                for (const auto& [count, names] : names_by_count) {
                    const auto candidate = candidates.find(count);
                    if (names.size() != 1 || candidate == candidates.end() || candidate->second.size() != 1) continue;
                    const auto& id = candidate->second.front();
                    if (link.count(id)) continue;
                    link[id] = names.front();
                    logf("linked: id=%s -> \"%s\"", id.c_str(), names.front().c_str());
                }
            }

            const bool menu_open = !gui_effectively_visible(g_bar_frame, g_player_gui);
            const bool game_over = match_has_ended(g_banner_label, g_player_gui);
            const std::string phase = game_over ? "match over" : !buttons.empty() ? "in match"
                : (!offset_updater.known() && !g_player_gui) ? "unknown roblox version" : "lobby / intermission";
            if (phase != last_phase) {
                logf("phase: %s", phase.c_str());
                last_phase = phase;
            }

            // page status: phase + live slot count
            {
                std::lock_guard<std::mutex> lock(g_ui.mtx);
                g_ui.slots = (int)buttons.size();
                g_ui.phase = phase;
            }

            const bool can_send = running && !menu_open && !game_over;
            // first reason this pass could not press anything for an enabled ability; reported only when
            // it lasts longer than its threshold, so normal cooldown waits stay out of the log
            std::string block_key, block_detail;
            int block_after_s = 0;
            const auto block = [&](const char* key, int after_s, std::string detail = "") {
                if (!block_key.empty()) return;
                block_key = key;
                block_after_s = after_s;
                block_detail = std::move(detail);
            };
            if (!buttons.empty() && !game_over) {
                bool game_focused = false;
                std::unordered_map<std::string, bool> group_fired;  // one press per group per pass
                std::unordered_map<std::string, bool> group_observed;
                for (const auto& b : buttons) {
                    std::string id = btn_image_id(b);
#ifdef TDS_TEST_ALLOWED_ABILITY_ID
                    if (id != TDS_TEST_ALLOWED_ABILITY_ID) continue;
#endif
                    if (!gui_effectively_visible(b.content, g_player_gui)) continue;
                    // rule lookup: by asset id first, then by ability name (stream link or id-table)
                    GroupRule* rule = nullptr;
                    std::string name;
                    if (link.count(id)) name = link[id];
                    else if (acfg.names.count(id)) name = acfg.names[id];
                    std::string rname = name.empty() ? id : name;
                    const auto group_key = name.empty() ? id : canon_key(name);
                    if (auto it = chain_rules.find(id); it != chain_rules.end()) {
                        rule = &it->second;
                    } else if (!name.empty()) {
                        if (auto name_rule = acfg.name_rules.find(group_key); name_rule != acfg.name_rules.end())
                            rule = &name_rule->second;
                    }
                    if (slot_action(!name.empty(), rule) == SlotAction::Skip) {
                        const std::string hotkey = read_string(b.binding + off::GUI_TEXT);
                        // slots without an icon share the empty id, so tell them apart by hotkey
                        if (unidentified_logged.insert(id.empty() ? "no-icon:" + hotkey : id).second) {
                            if (id.empty())
                                logf("slot [%s] has no icon id, so its ability cannot be identified; not pressing it",
                                     hotkey.c_str());
                            else
                                logf("slot [%s] id=%s is not identified yet, not pressing it "
                                     "(to enable it add \"%s = Tower/Ability\" to the id table in tds_abilities.ini)",
                                     hotkey.c_str(), id.c_str(), id.c_str());
                        }
                        continue;
                    }
                    char key = 0;
                    ChainState* chain = nullptr;
                    std::optional<int> ammo_n;
                    if (rule) {
                        auto& r = *rule;
                        if (r.duration <= 0) continue;                 // off -> never auto-press
                        ammo_n = b.ammo ? btn_ready_count(b) : std::nullopt;
                        chain = &chain_states[group_key];
                        const auto observed_at = std::chrono::steady_clock::now();
#ifdef TDS_TEST_ALLOWED_ABILITY_ID
                        if (ammo_n && chain->candidate_count != *ammo_n)
                            logf("chain sample count=%d | t=%.3fs", *ammo_n,
                                 std::chrono::duration<double>(observed_at - g_t0).count());
#endif
                        const auto event = group_observed[group_key] ? ChainEvent::None
                            : chain->observe(observed_at, ammo_n, std::chrono::seconds(r.duration));
                        group_observed[group_key] = true;
                        if (event == ChainEvent::Confirmed)
                            logf("chain confirmed %s, buff until +%llds", rname.c_str(), (long long)chain->active_duration.count());
                        else if (event == ChainEvent::Manual)
                            logf("manual fire adopted %s, buff until +%ds", rname.c_str(), r.duration);
                        else if (event == ChainEvent::Unconfirmed)
                            logf("chain unconfirmed %s, holding buff window", rname.c_str());
                        if (!can_send) { block(running ? "menu" : "paused", 5); continue; }
                        if (!chain->can_fire(observed_at)) continue;   // buff window / waiting for the game's ack
                        if (b.ammo && !chain->count_is_stable(ammo_n)) {
                            block("count", 20, ammo_n ? "ready count keeps changing" : "ready count is unreadable");
                            continue;
                        }
                        std::string why;
                        bool cooling = false;
                        // a timer is the normal wait: report it only past the longest wiki cooldown (120 s)
                        if (!btn_ready(b, &key, &why, &cooling)) { block("slot", cooling ? 150 : 45, why); continue; }
                        if (group_fired[group_key]) continue;
                    } else {
                        if (!can_send) { block(running ? "menu" : "paused", 5); continue; }
                        if (!btn_ready(b, &key)) continue;
                    }
                    if (!game_focused) {
                        if (!focus_game()) {
                            block("focus", 3, g_game_wnd ? "the Roblox window could not be brought to the front"
                                                         : "no Roblox window was found");
                            continue;
                        }
                        game_focused = true;
                    }
                    if (chain) {
                        const auto current_count = b.ammo ? btn_ready_count(b) : std::nullopt;
                        if (b.ammo && !chain->count_is_stable(current_count)) continue;
                        chain->begin_press(std::chrono::steady_clock::now(), current_count, std::chrono::seconds(rule->duration));
                        group_fired[group_key] = true;
                    }
                    const bool sent = press_key(key);
                    const double fired_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_t0).count();
                    if (sent)
                        logf("fired ability [%c] %s | t=%.3fs", key, rname.empty() ? id.c_str() : rname.c_str(), fired_s);
                    else
                        logf("key [%c] for %s was rejected by Windows (error %lu)", key,
                             rname.empty() ? id.c_str() : rname.c_str(), GetLastError());
                    {
                        std::lock_guard<std::mutex> lock(g_ui.mtx);
                        double now_s = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - g_t0).count();
                        g_ui.fired_at[canon_key(rname.empty() ? id : rname)] = now_s;
                    }
                    Sleep(150 + rand() % 100);
                }
            }
            {
                const auto block_now = std::chrono::steady_clock::now();
                if (block_key != press_block.key) { press_block = {block_key, block_now, false}; }
                if (!block_key.empty() && !press_block.logged &&
                    block_now - press_block.since >= std::chrono::seconds(block_after_s)) {
                    press_block.logged = true;
                    if (block_key == "paused")
                        logf("not pressing: auto-ability is OFF, press Start or F6 (the log shows [on] when it is enabled)");
                    else if (block_key == "menu")
                        logf("not pressing: the upgrade menu is open or the ability bar is hidden");
                    else if (block_key == "focus")
                        logf("not pressing: %s", block_detail.c_str());
                    else if (block_key == "count")
                        logf("not pressing: chain: %s", block_detail.c_str());
                    else
                        logf("not pressing: the slot %s", block_detail.c_str());
                }
            }
            Sleep(60);
        }
        CloseHandle(g_proc);
        g_proc = nullptr;
        g_player_gui = g_bar_frame = g_banner_label = 0;
        {
            std::lock_guard<std::mutex> lock(g_ui.mtx);
            g_ui.running = false;
            g_ui.slots = 0;
            g_ui.phase = "waiting for Roblox";
            for (auto& entry : g_ui.entries) entry.live = false;
        }
        if (GetAsyncKeyState(VK_END) & 0x8000) g_exit_requested.store(true);
    }
    logf("tds+ stopped\n");
    return 0;
}

#ifndef TDS_PLUS_QT
int main() { return tds_run_engine(); }
#endif
