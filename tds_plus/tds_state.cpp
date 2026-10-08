// language: C++17, file: tds_state.cpp, target: Windows 11, MinGW
// build: g++ -O2 -std=c++17 -static tds_state.cpp -o tds_state.exe -lpsapi
// reads TDS serialized tower records (the "Sync" payloads) straight from memory:
// every tower record carries Name / OwnerId / Upgrade / Damage / Range / Cooldown /
// TotalSpent / Worth / UID — alphabetically ordered, u32len key, tag 06 = double,
// tag 02 = u32len string. discovery: one full scan for len-prefixed "OwnerId",
// then only the hit regions are re-read each refresh.
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

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
    return out;
}
static std::string get_name(uintptr_t inst) {
    uintptr_t np = read<uintptr_t>(inst + 0x70);
    if (!valid_ptr(np)) return "";
    uint64_t len = read<uint64_t>(np + 0x18);
    if (!len || len > 128) return "";
    uintptr_t at = np + 0x08;
    if (len > 15) { at = read<uintptr_t>(np + 0x08); if (!valid_ptr(at)) return ""; }
    std::string s = read_at(at, (size_t)len);
    auto nul = s.find('\0');
    if (nul != std::string::npos) s.resize(nul);
    return s;
}
static std::vector<uintptr_t> get_children(uintptr_t inst) {
    std::vector<uintptr_t> out;
    uintptr_t holder = read<uintptr_t>(inst + 0x78);
    if (!valid_ptr(holder)) return out;
    uintptr_t cur = read<uintptr_t>(holder), end = read<uintptr_t>(holder + 0x8);
    if (!valid_ptr(cur) || !valid_ptr(end) || end < cur || (end - cur) / 0x10 > 20000) return out;
    for (; cur != end; cur += 0x10) {
        uintptr_t c = read<uintptr_t>(cur);
        if (valid_ptr(c)) out.push_back(c);
    }
    return out;
}
static std::string get_class_name(uintptr_t inst) {
    uintptr_t desc = read<uintptr_t>(inst + 0x18);
    if (!valid_ptr(desc)) return "";
    uintptr_t str = read<uintptr_t>(desc + 0x8);
    if (!valid_ptr(str)) return "";
    if (read<uintptr_t>(str + 0x18) == 0x1F) str = read<uintptr_t>(str);
    uint32_t len = read<uint32_t>(str + 0x10);
    if (!len || len > 128) return "";
    uintptr_t at = str;
    if (len > 15) { at = read<uintptr_t>(str); if (!valid_ptr(at)) return ""; }
    std::string s = read_at(at, len);
    auto nul = s.find('\0');
    if (nul != std::string::npos) s.resize(nul);
    return s;
}

// ---- serialized record parsing ----
struct TowerRec {
    std::string uid, name, owner_name, targeting;
    double owner_id = 0, upgrade = 0, damage = 0, range = 0, cooldown = 0;
    double total_spent = 0, worth = 0, ammo = 0;
    uintptr_t addr = 0;
    bool operator==(const TowerRec& o) const {
        return uid == o.uid && name == o.name && owner_name == o.owner_name &&
               targeting == o.targeting && owner_id == o.owner_id && upgrade == o.upgrade &&
               damage == o.damage && range == o.range && cooldown == o.cooldown &&
               total_spent == o.total_spent && worth == o.worth && ammo == o.ammo;
    }
    bool operator!=(const TowerRec& o) const { return !(*this == o); }
};

static const uint8_t OWNER_TAG[] = {7,0,0,0,'O','w','n','e','r','I','d'};
static const uint8_t CDEND_TAG[] = {0x0b,'c','o','o','l','d','o','w','n','E','n','d'};

// parse "<u32len>key <tag><value>" within a window; returns true if found
static bool rec_string(const uint8_t* w, size_t n, const char* key, std::string& out) {
    size_t kl = strlen(key);
    uint8_t hdr[4] = {(uint8_t)kl, 0, 0, 0};
    for (size_t i = 0; i + 4 + kl + 1 < n; i++) {
        if (memcmp(w + i, hdr, 4) || memcmp(w + i + 4, key, kl)) continue;
        size_t v = i + 4 + kl;
        if (w[v] != 0x02 || v + 5 > n) return false;
        uint32_t sl;
        memcpy(&sl, w + v + 1, 4);
        if (sl > 256 || v + 5 + sl > n) return false;
        out.assign((const char*)(w + v + 5), sl);
        return true;
    }
    return false;
}

static bool rec_double(const uint8_t* w, size_t n, const char* key, double& out) {
    size_t kl = strlen(key);
    uint8_t hdr[4] = {(uint8_t)kl, 0, 0, 0};
    for (size_t i = 0; i + 4 + kl + 1 < n; i++) {
        if (memcmp(w + i, hdr, 4) || memcmp(w + i + 4, key, kl)) continue;
        size_t v = i + 4 + kl;
        if (w[v] != 0x06 || v + 9 > n) return false;
        memcpy(&out, w + v + 1, 8);
        return true;
    }
    return false;
}

static bool parse_record(const uint8_t* w, size_t n, uintptr_t addr, TowerRec& r) {
    r.addr = addr;
    if (!rec_double(w, n, "OwnerId", r.owner_id)) return false;
    rec_string(w, n, "UID", r.uid);
    rec_string(w, n, "Name", r.name);
    rec_string(w, n, "OwnerName", r.owner_name);
    rec_string(w, n, "TargetingMode", r.targeting);
    rec_double(w, n, "Upgrade", r.upgrade);
    rec_double(w, n, "Damage", r.damage);
    rec_double(w, n, "Range", r.range);
    rec_double(w, n, "Cooldown", r.cooldown);
    rec_double(w, n, "TotalSpent", r.total_spent);
    rec_double(w, n, "Worth", r.worth);
    rec_double(w, n, "Ammo", r.ammo);
    return !r.uid.empty() || !r.name.empty();
}

// ---- process / discovery ----
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

struct Region { uintptr_t base; size_t size; };

static std::vector<Region> discover() {
    std::vector<Region> out;
    uintptr_t addr = 0;
    std::vector<uint8_t> buf(1 << 20);
    while (addr < 0x7FFFFFFFFFFFULL) {
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
        for (size_t off = 0; off < mbi.RegionSize && !hit; off += buf.size()) {
            size_t want = std::min(buf.size(), (size_t)mbi.RegionSize - off);
            SIZE_T got = 0;
            if (!ReadProcessMemory(g_proc, (LPCVOID)((uintptr_t)mbi.BaseAddress + off),
                                   buf.data(), want, &got) || !got) continue;
            for (size_t i = 0; i + sizeof(OWNER_TAG) <= got; i++) {
                if (!memcmp(buf.data() + i, OWNER_TAG, sizeof(OWNER_TAG)) ||
                    (i + sizeof(CDEND_TAG) <= got && !memcmp(buf.data() + i, CDEND_TAG, sizeof(CDEND_TAG)))) {
                    hit = true;
                    break;
                }
            }
        }
        if (hit) out.push_back({(uintptr_t)mbi.BaseAddress, (size_t)mbi.RegionSize});
    }
    return out;
}

// len8-format AbilityState record:  04 04 "name" 02 <len> <chars>  03 "uid" 02 36 <uuid>
//                                   0b "cooldownEnd" 0c <double>    04 10 "cooldownDuration" 0c <double>
struct AbilityRec { std::string name, uid; double cd_end = 0, cd_dur = 0; };

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

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int interval = argc > 1 ? atoi(argv[1]) : 2000;
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

    // my user id via LocalPlayer
    uintptr_t ve = read<uintptr_t>(base + 0x858d208);
    uintptr_t fdm = valid_ptr(ve) ? read<uintptr_t>(ve + 0xaf0) : 0;
    uintptr_t dm = valid_ptr(fdm) ? read<uintptr_t>(fdm + 0x1f8) : 0;
    uintptr_t players = 0;
    for (uintptr_t c : get_children(dm))
        if (get_class_name(c) == "Players") { players = c; break; }
    uintptr_t lp = players ? read<uintptr_t>(players + 0x120) : 0;
    double my_id = (double)read<int64_t>(lp + 0xc0);
    printf("me: %s (%.0f)\n", get_name(lp).c_str(), my_id);

    printf("discovering sync regions...\n");
    std::vector<Region> regions = discover();
    size_t total = 0;
    for (auto& r : regions) total += r.size;
    printf("%zu regions (%.1f MB)\n", regions.size(), total / 1048576.0);

    // GUI text member (inline std::string)
    auto gui_text = [](uintptr_t inst) -> std::string {
        if (!inst) return "";
        uint32_t len = read<uint32_t>(inst + 0xdf0 + 0x10);
        if (!len || len > 256) return "";
        uintptr_t at = inst + 0xdf0;
        if (len > 15) { at = read<uintptr_t>(inst + 0xdf0); if (!valid_ptr(at)) return ""; }
        std::string s = read_at(at, len);
        auto nul = s.find('\0');
        if (nul != std::string::npos) s.resize(nul);
        return s;
    };
    auto find_child = [](uintptr_t inst, const std::string& name) -> uintptr_t {
        for (uintptr_t c : get_children(inst)) if (get_name(c) == name) return c;
        return 0;
    };

    std::map<std::string, TowerRec> roster;   // accumulates over the whole match
    std::map<std::string, AbilityRec> abilities;
    double server_time = 0;
    std::string last_hover;
    std::string last_hud;
    uintptr_t hover_title = 0, hover_level = 0, hover_owner = 0;
    uintptr_t hud_wave = 0, hud_hp = 0, hud_time = 0;
    std::vector<uint8_t> buf;
    bool dirty = false;
    while (!(GetAsyncKeyState(VK_END) & 0x8000)) {
        // hover panel: PlayerGui/GameGui/HoverGui/Towers — live name+level of the hovered tower
        if (!hover_title) {
            uintptr_t gui = valid_ptr(lp) ? find_child(lp, "PlayerGui") : 0;
            uintptr_t gg = gui ? find_child(gui, "GameGui") : 0;
            uintptr_t hg = gg ? find_child(gg, "HoverGui") : 0;
            uintptr_t tw = hg ? find_child(hg, "Towers") : 0;
            if (tw) {
                hover_title = find_child(tw, "Title");
                hover_level = find_child(tw, "Level");
                uintptr_t ow = find_child(tw, "Owner");
                hover_owner = ow ? find_child(ow, "TextLabel") : 0;
            }
        }
        if (hover_title) {
            std::string t = gui_text(hover_title);
            std::string l = gui_text(hover_level);
            std::string o = gui_text(hover_owner);
            if (!t.empty()) {
                std::string cur = t + " | " + l + " | " + o;
                if (cur != last_hover) {
                    last_hover = cur;
                    printf("hover: %s %s (owner %s)\n", t.c_str(), l.c_str(), o.c_str());
                }
            }
        }

        // match HUD: PlayerGui/ReactGameTopGameDisplay/Frame
        if (!hud_wave) {
            uintptr_t gui = valid_ptr(lp) ? find_child(lp, "PlayerGui") : 0;
            uintptr_t top = gui ? find_child(gui, "ReactGameTopGameDisplay") : 0;
            uintptr_t fr = top ? find_child(top, "Frame") : 0;
            if (fr) {
                uintptr_t w = find_child(fr, "wave");
                uintptr_t wc = w ? find_child(w, "container") : 0;
                hud_wave = wc ? find_child(wc, "value") : 0;
                uintptr_t hb = find_child(fr, "healthbar");
                hud_hp = hb ? find_child(hb, "progress") : 0;
                uintptr_t wt = find_child(fr, "waveTimer");
                uintptr_t wtc = wt ? find_child(wt, "container") : 0;
                hud_time = wtc ? find_child(wtc, "value") : 0;
            }
        }
        if (hud_wave) {
            int n_players = players ? (int)get_children(players).size() : 0;
            char hud[160];
            snprintf(hud, sizeof(hud), "hud: wave %s | base %s | time %s | players %d",
                     gui_text(hud_wave).c_str(), gui_text(hud_hp).c_str(),
                     gui_text(hud_time).c_str(), n_players);
            if (last_hud != hud) {
                last_hud = hud;
                printf("%s\n", hud);
            }
        }

        for (auto& rg : regions) {
            buf.resize(rg.size);
            SIZE_T got = 0;
            if (!ReadProcessMemory(g_proc, (LPCVOID)rg.base, buf.data(), rg.size, &got) || !got) continue;
            // ServerTime:  0a "ServerTime" 09 06 <double>   (len8 format)
            static const uint8_t ST_TAG[] = {0x0a,'S','e','r','v','e','r','T','i','m','e',0x09,0x06};
            for (size_t i = 0; i + sizeof(ST_TAG) + 8 <= got; i++)
                if (!memcmp(buf.data() + i, ST_TAG, sizeof(ST_TAG))) {
                    double v;
                    memcpy(&v, buf.data() + i + sizeof(ST_TAG), 8);
                    if (v > server_time && v < 1e7) server_time = v;  // monotonic within a match
                }
            for (size_t i = 0; i + sizeof(OWNER_TAG) + 9 <= got; i++) {
                if (!memcmp(buf.data() + i, OWNER_TAG, sizeof(OWNER_TAG))) {
                    size_t ws = i > 600 ? i - 600 : 0;
                    size_t we = std::min((size_t)got, i + 900);
                    TowerRec r;
                    if (!parse_record(buf.data() + ws, we - ws, rg.base + i, r)) { i += 8; continue; }
                    std::string key = r.uid.empty() ? std::to_string(r.addr) : r.uid;
                    auto& slot = roster[key];
                    if (slot != r) { slot = r; dirty = true; }
                    i += 8;
                    continue;
                }
                if (i + sizeof(CDEND_TAG) + 9 <= got &&
                    !memcmp(buf.data() + i, CDEND_TAG, sizeof(CDEND_TAG))) {
                    size_t ws = i > 120 ? i - 120 : 0;
                    size_t we = std::min((size_t)got, i + 64);
                    const uint8_t* w = buf.data() + ws;
                    size_t n = we - ws;
                    AbilityRec a;
                    abl_string(w, n, "name", a.name);
                    abl_string(w, n, "uid", a.uid);
                    memcpy(&a.cd_end, buf.data() + i + sizeof(CDEND_TAG) + 1, 8);
                    // cooldownDuration sits before name in the same record
                    for (size_t j = 0; j + 18 + 9 <= n; j++)
                        if (!memcmp(w + j, "cooldownDuration", 16) && w[j + 16] == 0x0c) {
                            memcpy(&a.cd_dur, w + j + 17, 8);
                            break;
                        }
                    if (a.name.empty() || a.uid.empty()) continue;
                    auto& slot = abilities[a.uid];
                    if (slot.name != a.name || slot.cd_end != a.cd_end) { slot = a; dirty = true; }
                    i += sizeof(CDEND_TAG);
                }
            }
        }
        if (dirty) {
            dirty = false;
            // render once, print only on real change — buffers flap as the game reuses memory
            std::string out;
            char line[256];
            int n = snprintf(line, sizeof(line), "\n--- roster (%zu towers seen) ---\n", roster.size());
            out.append(line, n);
            for (auto& [k, r] : roster) {
                bool mine = r.owner_id == my_id;
                double dps = r.cooldown > 0 ? r.damage / r.cooldown : 0;
                n = snprintf(line, sizeof(line),
                       "%s %-18s lvl=%.0f dmg=%.1f rng=%.1f cd=%.2f dps=%.1f spent=%.0f worth=%.0f %s@%s\n",
                       mine ? "*" : " ", r.name.c_str(), r.upgrade, r.damage, r.range,
                       r.cooldown, dps, r.total_spent, r.worth,
                       r.owner_name.c_str(), r.uid.substr(0, 8).c_str());
                out.append(line, n);
            }
            out.append("(* = mine)\n");
            n = snprintf(line, sizeof(line), "--- abilities (server time %.1f) ---\n", server_time);
            out.append(line, n);
            for (auto& [k, a] : abilities) {
                // drop stale snapshots: a live record is either ready (0) or within its cooldown window
                double left = a.cd_end > 0 ? a.cd_end - server_time : 0;
                if (a.cd_end > 0 && (left < -5 || (a.cd_dur > 0 && left > a.cd_dur + 15))) continue;
                n = snprintf(line, sizeof(line), "%-22s uid=%s cd=%.1fs left=%.1fs\n",
                       a.name.c_str(), a.uid.substr(0, 8).c_str(), a.cd_dur, left);
                out.append(line, n);
            }
            static std::string last_print;
            if (out != last_print) {
                fputs(out.c_str(), stdout);
                last_print = std::move(out);
            }
        }
        Sleep(interval);
    }
    printf("stopped\n");
    CloseHandle(g_proc);
    return 0;
}
