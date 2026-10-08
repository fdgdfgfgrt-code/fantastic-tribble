// language: C++17, file: rbx_dump.cpp, target: Windows 11, MSVC
// build: cl /EHsc /std:c++17 /O2 rbx_dump.cpp /link user32.lib psapi.lib
// external memory dumper: walks committed readable regions of a target process,
// writes region binaries + map, module list, optional string table and pattern scan
// usage:
//   rbx_dump <pid|exe-name> <outdir> [--strings] [--max-region-mb N] [--include-mapped]
//   rbx_dump <pid|exe-name> <outdir> --scan <"text"|DE AD ?? EF>
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

struct Args {
    std::string target;
    std::string outdir;
    bool strings = false;
    bool include_mapped = false;
    size_t max_region_mb = 256;
    std::optional<std::string> scan_text;
    std::optional<std::string> scan_hex;
};

static bool readable(DWORD protect) {
    if (protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                      PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
}

static const char* protect_str(DWORD p) {
    if (p & PAGE_EXECUTE_READWRITE) return "ERW";
    if (p & PAGE_EXECUTE_READ) return "ER-";
    if (p & PAGE_READWRITE) return "-RW";
    if (p & PAGE_READONLY) return "-R-";
    if (p & PAGE_WRITECOPY) return "-WC";
    if (p & PAGE_EXECUTE_WRITECOPY) return "EWC";
    return "???";
}

static const char* type_str(DWORD t) {
    return t == MEM_IMAGE ? "IMAGE" : t == MEM_MAPPED ? "MAPPED" : "PRIVATE";
}

static DWORD find_pid(const std::string& name) {
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
        if (_stricmp(base ? base + 1 : exe, name.c_str()) == 0) return pids[i];
    }
    return 0;
}

static void dump_modules(HANDLE proc, const std::string& outdir) {
    HMODULE mods[1024];
    DWORD bytes = 0;
    std::ofstream out(outdir + "\\modules.txt");
    if (!EnumProcessModulesEx(proc, mods, sizeof(mods), &bytes, LIST_MODULES_ALL)) return;
    for (DWORD i = 0; i < bytes / sizeof(HMODULE); i++) {
        char name[MAX_PATH]{};
        MODULEINFO mi{};
        GetModuleFileNameExA(proc, mods[i], name, MAX_PATH);
        GetModuleInformation(proc, mods[i], &mi, sizeof(mi));
        out << std::hex << (uintptr_t)mi.lpBaseOfDll << " " << mi.SizeOfImage << " " << name << "\n";
    }
}

static bool is_string_char(unsigned char c) {
    return c >= 0x20 && c < 0x7F;
}

// single pass over a region buffer: dump to disk, collect strings, scan patterns
static void process_region(HANDLE proc, const MEMORY_BASIC_INFORMATION& mbi,
                           const Args& args, std::ofstream& map, std::ofstream& strings,
                           std::ofstream& hits, const std::vector<std::optional<uint8_t>>& pat) {
    const size_t chunk = 1 << 20;
    std::vector<uint8_t> buf(chunk);
    std::string path;
    std::ofstream bin;
    bool scan_mode = args.scan_text || args.scan_hex;
    if (!scan_mode) {
        char name[64];
        snprintf(name, sizeof(name), "region_%016llX.bin", (unsigned long long)mbi.BaseAddress);
        path = args.outdir + "\\regions\\" + name;
        bin.open(path, std::ios::binary);
        if (!bin) return;
    }

    std::string cur_str;
    uintptr_t cur_str_addr = 0;
    size_t total_read = 0;
    bool region_hit = false;

    for (size_t off = 0; off < mbi.RegionSize; off += chunk) {
        size_t want = std::min(chunk, mbi.RegionSize - off);
        SIZE_T got = 0;
        uintptr_t addr = (uintptr_t)mbi.BaseAddress + off;
        if (!ReadProcessMemory(proc, (LPCVOID)addr, buf.data(), want, &got) || got == 0) {
            if (bin) { std::vector<char> zeros(want, 0); bin.write(zeros.data(), want); }  // hole = zeros, offsets stay aligned
            if (args.strings && cur_str.size() >= 6) strings << std::hex << cur_str_addr << " " << cur_str << "\n";
            cur_str.clear();
            continue;
        }
        total_read += got;
        if (bin) bin.write((const char*)buf.data(), got);

        for (size_t i = 0; i < got; i++) {
            if (args.strings) {
                if (is_string_char(buf[i])) {
                    if (cur_str.empty()) cur_str_addr = addr + i;
                    cur_str += (char)buf[i];
                } else {
                    if (cur_str.size() >= 6) strings << std::hex << cur_str_addr << " " << cur_str << "\n";
                    cur_str.clear();
                }
            }
        }
        if (!pat.empty()) {
            for (size_t i = 0; i + pat.size() <= got; i++) {
                bool m = true;
                for (size_t j = 0; j < pat.size(); j++)
                    if (pat[j] && buf[i + j] != *pat[j]) { m = false; break; }
                if (m) { hits << std::hex << (addr + i) << "\n"; region_hit = true; }
            }
        }
    }
    // scan mode: keep the region's bytes when it produced hits, so the context is analyzable offline
    if (scan_mode && region_hit) {
        char name[64];
        snprintf(name, sizeof(name), "hitregion_%016llX.bin", (unsigned long long)mbi.BaseAddress);
        std::ofstream out(args.outdir + "\\" + name, std::ios::binary);
        std::vector<char> rbuf(chunk);
        for (size_t off = 0; off < mbi.RegionSize; off += chunk) {
            size_t want = std::min(chunk, mbi.RegionSize - off);
            SIZE_T got = 0;
            if (ReadProcessMemory(proc, (LPCVOID)((uintptr_t)mbi.BaseAddress + off), rbuf.data(), want, &got) && got)
                out.write(rbuf.data(), got);
            else { std::vector<char> zeros(want, 0); out.write(zeros.data(), want); }
        }
    }
    map << std::hex << (uintptr_t)mbi.BaseAddress << " " << mbi.RegionSize << " "
        << type_str(mbi.Type) << " " << protect_str(mbi.Protect) << " " << total_read << "\n";
}

static std::vector<std::optional<uint8_t>> build_pattern(const Args& a) {
    std::vector<std::optional<uint8_t>> pat;
    if (a.scan_text) for (char c : *a.scan_text) pat.push_back((uint8_t)c);
    if (a.scan_hex) {
        std::string h = *a.scan_hex;
        for (size_t i = 0; i + 1 < h.size();) {
            if (h[i] == ' ') { i++; continue; }
            if (h[i] == '?') { pat.push_back(std::nullopt); i += 2; }
            else { pat.push_back((uint8_t)strtoul(h.substr(i, 2).c_str(), nullptr, 16)); i += 2; }
        }
    }
    return pat;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: rbx_dump <pid|exe-name> <outdir> [--strings] [--include-mapped] "
               "[--max-region-mb N] [--scan <\"text\"|hex with ?? wildcards>]\n");
        return 1;
    }
    Args a;
    a.target = argv[1];
    a.outdir = argv[2];
    for (int i = 3; i < argc; i++) {
        std::string f = argv[i];
        if (f == "--strings") a.strings = true;
        else if (f == "--include-mapped") a.include_mapped = true;
        else if (f == "--max-region-mb" && i + 1 < argc) a.max_region_mb = strtoull(argv[++i], nullptr, 10);
        else if (f == "--scan" && i + 1 < argc) {
            std::string p = argv[++i];
            if (p.size() >= 2 && p[0] == '"' && p.back() == '"')
                a.scan_text = p.substr(1, p.size() - 2);
            else if (p.find_first_not_of("0123456789abcdefABCDEF ?") == std::string::npos)
                a.scan_hex = p;  // pure hex/space/?? input — quote with "..." to force text
            else
                a.scan_text = p;
        }
    }

    DWORD pid = strtoul(a.target.c_str(), nullptr, 10);
    if (pid == 0) pid = find_pid(a.target);
    if (pid == 0) { printf("process not found: %s\n", a.target.c_str()); return 1; }

    HANDLE proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!proc) {
        printf("OpenProcess failed: %lu (run as same user; admin if target is elevated)\n", GetLastError());
        return 1;
    }
    printf("attached to pid %lu\n", pid);

    std::string cmd = "mkdir \"" + a.outdir + "\" 2>nul & mkdir \"" + a.outdir + "\\regions\" 2>nul";
    system(cmd.c_str());

    dump_modules(proc, a.outdir);

    std::ofstream map(a.outdir + "\\map.txt");
    std::ofstream strings, hits;
    if (a.strings) strings.open(a.outdir + "\\strings.txt");
    auto pat = build_pattern(a);
    if (!pat.empty()) hits.open(a.outdir + "\\hits.txt");

    map << "# base size type prot bytes_read\n";
    uintptr_t addr = 0;
    size_t regions = 0, dumped = 0;
    while (addr < 0x7FFFFFFF0000ULL) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(proc, (LPCVOID)addr, &mbi, sizeof(mbi))) break;
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
        if (mbi.State != MEM_COMMIT || !readable(mbi.Protect)) continue;
        if (mbi.Type == MEM_MAPPED && !a.include_mapped) continue;
        regions++;
        if (mbi.RegionSize > a.max_region_mb * 1024 * 1024) {
            map << std::hex << (uintptr_t)mbi.BaseAddress << " " << mbi.RegionSize << " "
                << type_str(mbi.Type) << " " << protect_str(mbi.Protect) << " SKIPPED(too big)\n";
            continue;
        }
        process_region(proc, mbi, a, map, strings, hits, pat);
        dumped++;
        if (dumped % 50 == 0) printf("\r%zu regions...", dumped);
    }
    printf("\rdone: %zu readable region(s), %zu processed -> %s\n", regions, dumped, a.outdir.c_str());
    CloseHandle(proc);
    return 0;
}
