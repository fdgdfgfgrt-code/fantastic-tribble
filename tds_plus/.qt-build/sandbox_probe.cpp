// language: C++17, file: sandbox_probe.cpp, runtime: MSVC, target: Windows 11
#define TDS_PLUS_QT
#define NOMINMAX
#include "../tds_plus.cpp"

static void print_ancestry(uintptr_t object, uintptr_t gui) {
    for (int depth = 0; valid_ptr(object) && depth < 20; ++depth) {
        const auto cls = get_class_name(object);
        printf("  %s/%s @0x%llx parent=0x%llx visible=%u enabled=%u\n",
               cls.c_str(), get_name(object).c_str(), (unsigned long long)object,
               (unsigned long long)read<uintptr_t>(object + 0x68),
               (unsigned)read<uint8_t>(object + off::GUI_VISIBLE),
               (unsigned)read<uint8_t>(object + 0x4b4));
        if (object == gui) break;
        object = read<uintptr_t>(object + 0x68);
    }
}

static void print_branch(uintptr_t object, const std::string& path, int depth, int& budget) {
    if (!valid_ptr(object) || depth > 6 || --budget < 0) return;
    const auto cls = get_class_name(object);
    printf("%s [%s] visible=%u text=\"%s\"\n", path.c_str(), cls.c_str(),
           (unsigned)read<uint8_t>(object + off::GUI_VISIBLE),
           (cls == "TextLabel" || cls == "TextButton") ? read_string(object + off::GUI_TEXT).c_str() : "");
    for (const auto child : get_children(object)) print_branch(child, path + "/" + get_name(child), depth + 1, budget);
}

int main() {
    const auto pid = find_pid("RobloxPlayerBeta.exe");
    g_proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!g_proc) return 1;
    HMODULE mods[8]{};
    DWORD bytes = 0;
    if (!EnumProcessModules(g_proc, mods, sizeof(mods), &bytes)) return 2;
    const auto base = reinterpret_cast<uintptr_t>(mods[0]);
    const auto ve = read<uintptr_t>(base + off::VE_POINTER);
    const auto fdm = read<uintptr_t>(ve + off::VE_FAKE_DM);
    const auto dm = read<uintptr_t>(fdm + off::FAKE_REAL_DM);
    const auto players = find_child_of_class(dm, "Players");
    const auto lp = read<uintptr_t>(players + off::PLAYERS_LOCAL);
    const auto gui = find_child(lp, "PlayerGui");
    printf("pid=%lu gui=0x%llx\n", pid, (unsigned long long)gui);
    for (const auto* name : {"ReactGameTowerTimeSpans", "ReactGameTowerBuffs", "ReactGameAbilityIndicator"}) {
        int budget = 120;
        print_branch(find_child(gui, name), name, 0, budget);
    }
    uintptr_t label = gui;
    for (const auto* name : {"ReactGameNewRewards", "Frame", "gameOver", "RewardsScreen", "RewardBanner", "textLabel"}) {
        label = label ? find_child(label, name) : 0;
        if (!label) break;
    }
    printf("rewards banner=\"%s\"\n", label ? read_string(label + off::GUI_TEXT).c_str() : "missing");
    print_ancestry(label, gui);
    const auto buttons = collect_buttons(gui);
    printf("ability groups=%zu\n", buttons.size());
    for (const auto& button : buttons) {
        char key = 0;
        debug_btn(button);
        printf("  ready=%d\n", (int)btn_ready(button, &key));
        print_ancestry(button.content, gui);
    }
    CloseHandle(g_proc);
    g_proc = nullptr;
    return gui && label && !buttons.empty() ? 0 : 3;
}
