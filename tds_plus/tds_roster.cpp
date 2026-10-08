// language: C++17, file: tds_roster.cpp, target: Windows 11, MinGW
// build: g++ -O2 -std=c++17 -static tds_roster.cpp -o tds_roster.exe -lpsapi
// tower roster: every placed tower with owner, level, stats and a structure
// fingerprint (skin-independent type id is NOT in the instance tree — the
// fingerprint is stable per skin; label it once in tds_towers.ini)
#include <windows.h>
#include <psapi.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace off {
    constexpr uintptr_t VE_POINTER     = 0x858d208;
    constexpr uintptr_t VE_FAKE_DM     = 0xaf0;
    constexpr uintptr_t FAKE_REAL_DM   = 0x1f8;
    constexpr uintptr_t DM_WORKSPACE   = 0x150;
    constexpr uintptr_t INST_NAME      = 0x70;
    constexpr uintptr_t INST_CHILDREN  = 0x78;
    constexpr uintptr_t INST_CLASS_DESC= 0x18;
    constexpr uintptr_t PLAYERS_LOCAL  = 0x120;
    constexpr uintptr_t PLAYER_USERID  = 0xc0;    // Player::UserId (int64)
    constexpr uintptr_t VALUE          = 0xa8;    // ValueBase::Value
}

static HANDLE g_proc = nullptr;

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
static std::string get_name(uintptr_t inst) {
    uintptr_t np = read<uintptr_t>(inst + off::INST_NAME);
    if (!valid_ptr(np)) return "";
    uint64_t len = read<uint64_t>(np + 0x18);
    if (!len || len > 128) return "";
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
    // class-name struct is a plain std::string here (len@+0x10)
    uint32_t len = read<uint32_t>(str + 0x10);
    if (!len || len > 128) return "";
    uintptr_t at = str;
    if (len > 15) { at = read<uintptr_t>(str); if (!valid_ptr(at)) return ""; }
    return read_at(at, len);
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

// FNV-1a over the descendant "Class:Name" list — stable per skin/tower combo.
// "Weapon" and "Upgrades" folders are excluded: their contents swap on upgrade,
// which would change the fingerprint every level
static uint64_t fingerprint(uintptr_t inst) {
    uint64_t h = 1469598103934665603ULL;
    std::vector<std::pair<uintptr_t, int>> stack{{inst, 0}};
    while (!stack.empty()) {
        auto [cur, d] = stack.back(); stack.pop_back();
        if (d > 5) continue;
        for (uintptr_t c : get_children(cur)) {
            std::string name = get_name(c);
            if (name == "Weapon" || name == "Upgrades") continue;
            std::string key = get_class_name(c) + ":" + name + ";";
            for (char ch : key) { h ^= (uint8_t)ch; h *= 1099511628211ULL; }
            stack.push_back({c, d + 1});
        }
    }
    return h;
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

static std::map<std::string, std::string> load_labels(const char* path) {
    std::map<std::string, std::string> m;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            const char* ws = " \t\r\n";
            auto b = s.find_first_not_of(ws), e = s.find_last_not_of(ws);
            s = b == std::string::npos ? "" : s.substr(b, e - b + 1);
        };
        trim(k); trim(v);
        if (!k.empty() && k[0] != ';' && k[0] != '#') m[k] = v;
    }
    return m;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
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

    uintptr_t ve = read<uintptr_t>(base + off::VE_POINTER);
    uintptr_t fdm = valid_ptr(ve) ? read<uintptr_t>(ve + off::VE_FAKE_DM) : 0;
    uintptr_t dm = valid_ptr(fdm) ? read<uintptr_t>(fdm + off::FAKE_REAL_DM) : 0;
    if (!valid_ptr(dm)) { printf("no datamodel — in a game?\n"); return 1; }

    uintptr_t players = 0;
    for (uintptr_t c : get_children(dm))
        if (get_class_name(c) == "Players") { players = c; break; }
    uintptr_t lp = players ? read<uintptr_t>(players + off::PLAYERS_LOCAL) : 0;
    int64_t my_id = valid_ptr(lp) ? read<int64_t>(lp + off::PLAYER_USERID) : 0;
    printf("me: %s (%lld)\n\n", get_name(lp).c_str(), (long long)my_id);

    uintptr_t ws = read<uintptr_t>(dm + off::DM_WORKSPACE);
    uintptr_t towers = valid_ptr(ws) ? find_child(ws, "Towers") : 0;
    if (!towers) { printf("no Towers folder — not in a match\n"); return 1; }

    auto labels = load_labels("tds_towers.ini");
    int mine = 0, theirs = 0;
    for (uintptr_t tw : get_children(towers)) {
        std::string skin = get_name(tw);
        int64_t owner = 0;
        if (uintptr_t o = find_child(tw, "Owner")) owner = (int64_t)read<double>(o + off::VALUE);
        bool is_mine = owner == my_id && my_id != 0;
        is_mine ? mine++ : theirs++;
        if (!is_mine) continue;

        char fp[24];
        snprintf(fp, sizeof(fp), "%016llx", (unsigned long long)fingerprint(tw));
        auto lbl = labels.find(fp);
        std::string rigs;   // model children other than the swappable weapon — the labeling hint
        for (uintptr_t c : get_children(tw)) {
            std::string n = get_name(c);
            if (get_class_name(c) == "Model" && n != "Weapon" && n != "Upgrades") rigs += n + " ";
        }
        // Display/ folder values are a stale billboard cache, so level/stats are not read
        printf("skin=%-10s type=%-22s fp=%s  rigs: %s\n",
               skin.c_str(), lbl != labels.end() ? lbl->second.c_str() : "?", fp, rigs.c_str());
    }
    printf("\nmy towers: %d | other players' towers: %d\n", mine, theirs);
    printf("(unknown fp -> add to tds_towers.ini as  fp = TowerName)\n");
    CloseHandle(g_proc);
    return 0;
}
