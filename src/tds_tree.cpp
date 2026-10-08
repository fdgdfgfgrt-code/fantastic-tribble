// language: C++17, file: tds_tree.cpp, target: Windows 11, MinGW/MSVC
// build: g++ -O2 -std=c++17 -static tds_tree.cpp -o tds_tree.exe -lpsapi
//        cl /EHsc /std:c++17 /O2 tds_tree.cpp /link psapi.lib
// live instance-tree reader: attaches to a running Roblox client and prints the
// DataModel tree (Workspace.Towers, Players.LocalPlayer.PlayerGui) with per-node
// class, name, and GuiObject Visible/Text — the map for the tds+ memory reader
// usage: tds_tree.exe <pid|RobloxPlayerBeta.exe> [outfile] [maxdepth]
#include <windows.h>
#include <psapi.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace off {
    constexpr uintptr_t VE_POINTER        = 0x858d208;  // VisualEngine::Pointer (RVA from module base)
    constexpr uintptr_t VE_FAKE_DM        = 0xaf0;      // VisualEngine::FakeDataModel
    constexpr uintptr_t FAKE_REAL_DM      = 0x1f8;      // FakeDataModel::RealDataModel
    constexpr uintptr_t DM_WORKSPACE      = 0x150;      // DataModel::Workspace
    constexpr uintptr_t DM_GAME_LOADED    = 0x5d0;      // DataModel::GameLoaded
    constexpr uintptr_t DM_PLACE_ID       = 0x188;      // DataModel::PlaceId
    constexpr uintptr_t INST_PARENT       = 0x68;
    constexpr uintptr_t INST_NAME         = 0x70;       // NameContainer: pointer to string struct
    constexpr uintptr_t INST_CHILDREN     = 0x78;       // ChildrenStart: pointer to {begin,end}
    constexpr uintptr_t INST_CLASS_DESC   = 0x18;       // ClassDescriptor
    constexpr uintptr_t PLAYERS_LOCAL     = 0x120;      // Players::LocalPlayer
    constexpr uintptr_t GUI_VISIBLE       = 0x59d;      // GuiObject::Visible (byte)
    constexpr uintptr_t GUI_TEXT          = 0xdf0;      // GuiObject::Text (inline string struct)
}

static HANDLE g_proc = nullptr;

template <typename T>
static T read(uintptr_t addr) {
    T v{};
    SIZE_T got = 0;
    ReadProcessMemory(g_proc, (LPCVOID)addr, &v, sizeof(T), &got);
    return v;
}

static bool valid_ptr(uintptr_t p) {
    return p >= 0x10000 && p < 0x7FFFFFFFFFFF;
}

static std::string read_string(uintptr_t addr) {
    if (!valid_ptr(addr)) return "";
    uint32_t len = read<uint32_t>(addr + 0x10);
    if (len == 0) return "";
    if (len > 4096) len = 4096;
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

// Instance+0x70 -> struct { +0x00 header, +0x08 std::string (MSVC layout):
// inline chars/heap ptr at +0x08, size at +0x18, cap at +0x20 }
static std::string get_name(uintptr_t inst) {
    uintptr_t nameptr = read<uintptr_t>(inst + off::INST_NAME);
    if (!valid_ptr(nameptr)) return "";
    uint64_t len = read<uint64_t>(nameptr + 0x18);
    if (len == 0 || len > 4096) return "";
    uintptr_t at = nameptr + 0x08;
    if (len > 15) {
        at = read<uintptr_t>(nameptr + 0x08);
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
    if (read<uintptr_t>(str + 0x18) == 0x1F)  // heap-string marker in the class-name struct
        str = read<uintptr_t>(str);
    return read_string(str);
}

static std::vector<uintptr_t> get_children(uintptr_t inst) {
    std::vector<uintptr_t> out;
    uintptr_t holder = read<uintptr_t>(inst + off::INST_CHILDREN);
    if (!valid_ptr(holder)) return out;
    uintptr_t cur = read<uintptr_t>(holder);
    uintptr_t end = read<uintptr_t>(holder + 0x8);
    if (!valid_ptr(cur) || !valid_ptr(end) || end < cur) return out;
    size_t count = (end - cur) / 0x10;  // shared_ptr entries, 16 bytes each
    if (count > 20000) return out;
    out.reserve(count);
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

static uintptr_t module_base(HANDLE proc) {
    HMODULE mods[64];
    DWORD bytes = 0;
    if (!EnumProcessModules(proc, mods, sizeof(mods), &bytes)) return 0;
    return (uintptr_t)mods[0];  // first module = the exe itself
}

struct Printer {
    std::ofstream& out;
    size_t& count;
    size_t cap;
    void tree(uintptr_t inst, int depth, int maxdepth) {
        if (!inst || count >= cap) return;
        std::string name = get_name(inst);
        std::string cls = get_class_name(inst);
        std::string extra;
        if (cls == "TextLabel" || cls == "TextButton" || cls == "ImageButton" ||
            cls == "ImageLabel" || cls == "TextBox" || cls == "GuiButton") {
            int vis = read<uint8_t>(inst + off::GUI_VISIBLE);
            std::string text = read_string(inst + off::GUI_TEXT);
            char buf[600];
            snprintf(buf, sizeof(buf), "  visible=%d text=\"%.200s\"", vis, text.c_str());
            extra = buf;
        }
        out << std::string(depth * 2, ' ') << "[" << cls << "] " << name << extra
            << "  @0x" << std::hex << inst << std::dec << "\n";
        count++;
        if (depth >= maxdepth) return;
        for (uintptr_t c : get_children(inst)) tree(c, depth + 1, maxdepth);
    }
};

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: tds_tree.exe <pid|RobloxPlayerBeta.exe> [outfile=tree.txt] [maxdepth=8]\n");
        return 1;
    }
    const char* outfile = argc > 2 ? argv[2] : "tree.txt";
    int maxdepth = argc > 3 ? atoi(argv[3]) : 8;

    DWORD pid = strtoul(argv[1], nullptr, 10);
    if (pid == 0) pid = find_pid(argv[1]);
    if (!pid) { printf("process not found: %s\n", argv[1]); return 1; }

    g_proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!g_proc) { printf("OpenProcess failed: %lu (admin if target elevated)\n", GetLastError()); return 1; }

    uintptr_t base = module_base(g_proc);
    printf("pid=%lu base=0x%llx\n", pid, (unsigned long long)base);

    uintptr_t ve = read<uintptr_t>(base + off::VE_POINTER);
    uintptr_t fake_dm = valid_ptr(ve) ? read<uintptr_t>(ve + off::VE_FAKE_DM) : 0;
    uintptr_t dm = valid_ptr(fake_dm) ? read<uintptr_t>(fake_dm + off::FAKE_REAL_DM) : 0;
    printf("visualengine=0x%llx fakedm=0x%llx datamodel=0x%llx\n",
           (unsigned long long)ve, (unsigned long long)fake_dm, (unsigned long long)dm);
    if (!valid_ptr(dm)) { printf("datamodel chain broken — offsets stale or not in a game\n"); return 1; }

    printf("place_id=%llu game_loaded=%d\n",
           (unsigned long long)read<uint64_t>(dm + off::DM_PLACE_ID),
           (int)read<uint8_t>(dm + off::DM_GAME_LOADED));

    std::ofstream out(outfile);
    if (!out) { printf("cannot open %s\n", outfile); return 1; }

    size_t count = 0, cap = 60000;
    Printer p{out, count, cap};

    uintptr_t ws = read<uintptr_t>(dm + off::DM_WORKSPACE);
    printf("workspace=0x%llx\n", (unsigned long long)ws);
    if (valid_ptr(ws)) {
        uintptr_t towers = find_child(ws, "Towers");
        out << "== Workspace\\Towers ==\n";
        if (towers) p.tree(towers, 0, maxdepth);
        else out << "(no Towers folder — not in a match?)\n";
    }

    uintptr_t players = find_child_of_class(dm, "Players");
    printf("players=0x%llx\n", (unsigned long long)players);
    if (players) {
        uintptr_t lp = read<uintptr_t>(players + off::PLAYERS_LOCAL);
        printf("localplayer=0x%llx name=%s\n", (unsigned long long)lp, get_name(lp).c_str());
        if (valid_ptr(lp)) {
            uintptr_t gui = find_child(lp, "PlayerGui");
            out << "\n== PlayerGui ==\n";
            if (gui) p.tree(gui, 0, maxdepth);
            else out << "(no PlayerGui)\n";
        }
    }

    printf("wrote %s (%zu nodes)\n", outfile, count);
    CloseHandle(g_proc);
    return 0;
}
