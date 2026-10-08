// language: C++17, file: chain_input_audit.cpp, runtime: MSVC, target: Windows 11
#define TDS_PLUS_QT
#define NOMINMAX
#include "../tds_plus.cpp"

static std::ofstream audit;
static std::chrono::steady_clock::time_point audit_start;
static DWORD target_pid;
static int injected_down = 0, physical_down = 0;

static double audit_time() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - audit_start).count();
}

static LRESULT CALLBACK key_audit(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const auto* key = reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
        DWORD foreground_pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
        if (key->vkCode == 'F' && foreground_pid == target_pid) {
            const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            const bool injected = (key->flags & LLKHF_INJECTED) != 0;
            if (down) { if (injected) ++injected_down; else ++physical_down; }
            audit << "KEY t=" << audit_time() << " " << (down ? "down" : "up")
                  << " " << (injected ? "injected" : "physical") << std::endl;
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}

int main(int argc, char** argv) {
    if (argc < 2 || !SetCurrentDirectoryA(argv[1])) return 2;
    const bool observe_only = argc > 2;
    audit.open("chain_input_audit.txt");
    target_pid = find_pid("RobloxPlayerBeta.exe");
    g_proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, target_pid);
    if (!g_proc) return 3;
    HMODULE modules[8]{};
    DWORD bytes = 0;
    if (!EnumProcessModules(g_proc, modules, sizeof(modules), &bytes)) return 4;
    const auto ve = read<uintptr_t>(reinterpret_cast<uintptr_t>(modules[0]) + off::VE_POINTER);
    const auto fdm = read<uintptr_t>(ve + off::VE_FAKE_DM);
    const auto dm = read<uintptr_t>(fdm + off::FAKE_REAL_DM);
    const auto players = find_child_of_class(dm, "Players");
    const auto local_player = read<uintptr_t>(players + off::PLAYERS_LOCAL);
    g_player_gui = find_child(local_player, "PlayerGui");
    EnumWindows(enum_wnd_cb, target_pid);
    audit_start = std::chrono::steady_clock::now();
    const auto hook = SetWindowsHookExW(WH_KEYBOARD_LL, key_audit, GetModuleHandleW(nullptr), 0);
    if (!hook) return 5;
    bool pressed = false;
    AbilityBtn commander{};
    std::string previous;
    double last_resolve = -1;
    while (audit_time() < (observe_only ? 70 : 32)) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        const auto time = audit_time();
        if (time - last_resolve > .2) {
            last_resolve = time;
            commander = {};
            for (const auto& button : collect_buttons(g_player_gui))
                if (btn_image_id(button) == "4594880289") commander = button;
        }
        if (commander.ammo) {
            const auto text = read_string(commander.ammo + off::GUI_TEXT);
            const auto left = read_string(commander.time_left + off::GUI_TEXT);
            const auto parent = read<uintptr_t>(commander.ammo + off::INST_PARENT);
            const auto current = text + " left=" + left + " ptr=" + std::to_string(commander.ammo)
                + " attached=" + std::to_string(parent == commander.content);
            if (current != previous) {
                previous = current;
                audit << "COUNT t=" << time << " " << current << std::endl;
            }
            char key = 0;
            if (!observe_only && !pressed && time >= 11 && btn_ready(commander, &key) && focus_game()) {
                audit << "ONE PRESS t=" << audit_time() << std::endl;
                press_key('F');
                pressed = true;
            }
        }
        Sleep(20);
    }
    UnhookWindowsHookEx(hook);
    CloseHandle(g_proc);
    audit << "RESULT pressed=" << pressed << " injected_down=" << injected_down
          << " physical_down=" << physical_down << std::endl;
    return observe_only || (pressed && injected_down == 1 && physical_down == 0) ? 0 : 1;
}
