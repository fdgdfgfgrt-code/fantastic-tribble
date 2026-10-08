// language: C++17, file: tds_watch.cpp, target: Windows 11, MinGW
// build: g++ -O2 -std=c++17 -static tds_watch.cpp -o tds_watch.exe -lpsapi
// state diff-watcher: polls workspace.Towers + ReactGameAbilities, prints every
// change with a timestamp — run it, fire abilities in game, capture the structure
// usage: tds_watch.exe [interval_ms]   (END to stop, output: console + watch.log)
#include <windows.h>
#include <psapi.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "rbx_offsets.hpp"

static HANDLE g_proc;
static std::ofstream g_log;
static std::chrono::steady_clock::time_point g_t0;

template <typename T>
static T read(uintptr_t addr) {
    T v{};
    SIZE_T got = 0;
    ReadProcessMemory(g_proc, (LPCVOID)addr, &v, sizeof(T), &got);
    return v;
}
static bool valid_ptr(uintptr_t p) { return p >= 0x10000 && p < 0x7FFFFFFFFFFF; }

static std::string read_at(uintptr_t at, size_t len) {
    std::string out(len, '\0');
    SIZE_T got = 0;
    if (!ReadProcessMemory(g_proc, (LPCVOID)at, out.data(), len, &got) || !got) return "";
    out.resize(got);
    auto nul = out.find('\0');
    if (nul != std::string::npos) out.resize(nul);
    return out;
}

static std::string read_string(uintptr_t addr) {          // inline std::string member
    if (!valid_ptr(addr)) return "";
    uint32_t len = read<uint32_t>(addr + 0x10);
    if (!len || len > 256) return "";
    uintptr_t at = addr;
    if (len > 15) { at = read<uintptr_t>(addr); if (!valid_ptr(at)) return ""; }
    return read_at(at, len);
}

static std::string get_name(uintptr_t inst) {             // Instance name struct
    uintptr_t np = read<uintptr_t>(inst + off::INST_NAME);
    if (!valid_ptr(np)) return "";
    uint64_t len = read<uint64_t>(np + 0x18);
    if (!len || len > 256) return "";
    uintptr_t at = np + 0x08;
    if (len > 15) { at = read<uintptr_t>(np + 0x08); if (!valid_ptr(at)) return ""; }
    return read_at(at, (size_t)len);
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
    uintptr_t cur = read<uintptr_t>(holder), end = read<uintptr_t>(holder + 0x8);
    if (!valid_ptr(cur) || !valid_ptr(end) || end < cur || (end - cur) / 0x10 > 20000) return out;
    for (; cur != end; cur += 0x10) {
        uintptr_t c = read<uintptr_t>(cur);
        if (valid_ptr(c)) out.push_back(c);
    }
    return out;
}

static uintptr_t find_child(uintptr_t inst, const std::string& name) {
    for (uintptr_t c : get_children(inst)) if (get_name(c) == name) return c;
    return 0;
}

static void emit(const std::string& msg) {
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_t0).count();
    char line[1024];
    snprintf(line, sizeof(line), "[%7.2f] %s", t, msg.c_str());
    printf("%s\n", line);
    g_log << line << "\n" << std::flush;
}

// snapshot of one instance subtree, depth-limited, "path(class)=value" entries
static void snap(uintptr_t inst, const std::string& path, int depth, std::map<std::string, std::string>& out) {
    if (depth > 6) return;
    for (uintptr_t c : get_children(inst)) {
        std::string name = get_name(c), cls = get_class_name(c);
        std::string p = path + "/" + name;
        std::string val;
        if (cls == "NumberValue") {
            val = std::to_string(read<double>(c + off::VALUE));
        } else if (cls == "IntValue" || cls == "BoolValue") {
            val = std::to_string(read<int32_t>(c + off::VALUE));
        } else if (cls == "StringValue") {
            val = read_string(c + off::VALUE);
        } else if (cls == "TextLabel" || cls == "TextButton") {
            val = "\"" + read_string(c + off::GUI_TEXT) + "\" vis=" +
                  std::to_string(read<uint8_t>(c + off::GUI_VISIBLE));
        } else if (cls == "Frame" || cls == "ImageLabel" || cls == "ImageButton") {
            val = "vis=" + std::to_string(read<uint8_t>(c + off::GUI_VISIBLE));
            if (cls != "Frame") {
                std::string img = read_string(c + off::GUI_IMAGE);
                if (!img.empty()) val += " img=" + img;
            }
        } else if (cls == "ScreenGui") {
            val = "enabled=" + std::to_string(read<uint8_t>(c + off::SCREEN_GUI_ENABLED));
        }
        out[p + "(" + cls + ")"] = val;
        snap(c, p, depth + 1, out);
    }
}

static void diff(const std::map<std::string, std::string>& prev,
                 const std::map<std::string, std::string>& cur,
                 const std::string& tag) {
    for (const auto& [k, v] : cur) {
        auto it = prev.find(k);
        if (it == prev.end()) emit("  + " + tag + " " + k + " = " + v);
        else if (it->second != v) emit("  ~ " + tag + " " + k + ": " + it->second + " -> " + v);
    }
    for (const auto& [k, v] : prev)
        if (!cur.count(k)) emit("  - " + tag + " " + k + " (was " + v + ")");
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
        const char* b = strrchr(exe, '\\');
        if (_stricmp(b ? b + 1 : exe, name) == 0) return pids[i];
    }
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int interval = argc > 1 ? atoi(argv[1]) : 200;
    DWORD pid = find_pid("RobloxPlayerBeta.exe");
    if (!pid) { printf("RobloxPlayerBeta.exe not running\n"); return 1; }
    g_proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!g_proc) { printf("OpenProcess failed: %lu\n", GetLastError()); return 1; }
    HMODULE mods[8]{};
    DWORD bytes = 0;
    if (!EnumProcessModules(g_proc, mods, sizeof(mods), &bytes) || bytes < sizeof(HMODULE)) {
        printf("EnumProcessModules failed: %lu\n", GetLastError());
        CloseHandle(g_proc);
        return 1;
    }
    uintptr_t base = (uintptr_t)mods[0];
    g_log.open("watch.log");
    g_t0 = std::chrono::steady_clock::now();
    printf("watching pid=%lu — fire abilities in game, END to stop\n", pid);

    std::map<std::string, std::string> prev_towers, prev_bar, prev_ui;
    bool first = true;
    while (!(GetAsyncKeyState(VK_END) & 0x8000)) {
        uintptr_t ve = read<uintptr_t>(base + off::VE_POINTER);
        uintptr_t fdm = valid_ptr(ve) ? read<uintptr_t>(ve + off::VE_FAKE_DM) : 0;
        uintptr_t dm = valid_ptr(fdm) ? read<uintptr_t>(fdm + off::FAKE_REAL_DM) : 0;
        if (!valid_ptr(dm)) { Sleep(interval); continue; }

        std::map<std::string, std::string> cur_towers, cur_bar, cur_ui;
        uintptr_t ws = 0, players = 0;
        for (uintptr_t c : get_children(dm)) {
            std::string n = get_name(c);
            if (n == "Workspace") ws = c;
            else if (n == "Players") players = c;
        }
        if (ws) {
            uintptr_t towers = find_child(ws, "Towers");
            if (towers) snap(towers, "Towers", 0, cur_towers);
        }
        if (players) {
            uintptr_t lp = read<uintptr_t>(players + off::PLAYERS_LOCAL);
            uintptr_t gui = valid_ptr(lp) ? find_child(lp, "PlayerGui") : 0;
            uintptr_t rga = gui ? find_child(gui, "ReactGameAbilities") : 0;
            if (rga) snap(rga, "Bar", 0, cur_bar);
            uintptr_t ui = gui ? find_child(gui, "Interface") : 0;
            if (ui) snap(ui, "UI", 0, cur_ui);
        }

        if (!first) {
            diff(prev_towers, cur_towers, "towers");
            diff(prev_bar, cur_bar, "bar");
            diff(prev_ui, cur_ui, "ui");
        } else {
            emit("baseline: " + std::to_string(cur_towers.size()) + " tower nodes, " +
                 std::to_string(cur_bar.size()) + " bar nodes, " +
                 std::to_string(cur_ui.size()) + " ui nodes");
            first = false;
        }
        prev_towers = std::move(cur_towers);
        prev_bar = std::move(cur_bar);
        prev_ui = std::move(cur_ui);
        Sleep(interval);
    }
    printf("stopped\n");
    return 0;
}
