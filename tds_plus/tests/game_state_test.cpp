// language: C++17, file: game_state_test.cpp, runtime: MSVC, target: Windows 11
#include "../tds_plus.cpp"
#include <array>

struct FakeGui {
    std::array<unsigned char, 0x1100> object{};
    std::array<unsigned char, 0x20> descriptor{};
    std::array<unsigned char, 0x30> name_header{};
    std::array<uintptr_t, 2> children_header{};
    std::vector<std::array<uintptr_t, 2>> children;
    std::string class_name;
    std::string object_name;
    std::string text;

    FakeGui(const char* cls, const FakeGui* parent = nullptr) : class_name(cls) {
        put(descriptor, 0x8, reinterpret_cast<uintptr_t>(&class_name));
        put(object, off::INST_CLASS_DESC, reinterpret_cast<uintptr_t>(descriptor.data()));
        set_parent(parent);
        set_visible(true);
        set_enabled(true);
    }
    FakeGui(const FakeGui&) = delete;
    FakeGui& operator=(const FakeGui&) = delete;

    template<size_t N, typename T>
    static void put(std::array<unsigned char, N>& bytes, size_t offset, T value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
    uintptr_t address() const { return reinterpret_cast<uintptr_t>(object.data()); }
    void set_parent(const FakeGui* parent) {
        put(object, off::INST_PARENT, parent ? parent->address() : uintptr_t{0});
    }
    void set_visible(bool visible) { object[off::GUI_VISIBLE] = visible ? 1 : 0; }
    void set_enabled(bool enabled) { object[off::SCREEN_GUI_ENABLED] = enabled ? 1 : 0; }
    void set_text(const char* value) {
        text = value;
        std::memcpy(object.data() + off::GUI_TEXT, &text, sizeof(text));
    }
    void set_name(const char* value) {
        object_name = value;
        std::memcpy(name_header.data() + 8, &object_name, sizeof(object_name));
        put(object, off::INST_NAME, reinterpret_cast<uintptr_t>(name_header.data()));
    }
    void set_children(std::initializer_list<const FakeGui*> values) {
        children.clear();
        for (const auto* child : values) children.push_back({child->address(), 0});
        children_header[0] = reinterpret_cast<uintptr_t>(children.data());
        children_header[1] = children_header[0] + children.size() * sizeof(children.front());
        put(object, off::INST_CHILDREN, reinterpret_cast<uintptr_t>(children_header.data()));
    }
};

int main() {
    g_proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());
    if (!g_proc) return 2;
    int failures = 0;
    const auto check = [&](bool passed, const char* name) {
        printf("%s %s\n", passed ? "PASS" : "FAIL", name);
        failures += !passed;
    };
    FakeGui player_gui("PlayerGui");
    FakeGui screen("ScreenGui", &player_gui);
    FakeGui root("Frame", &screen);
    FakeGui game_over("Frame", &root);
    FakeGui rewards("Frame", &game_over);
    FakeGui banner("ImageLabel", &rewards);
    FakeGui label("TextLabel", &banner);
    const auto ended = [&] { return match_has_ended(label.address(), player_gui.address()); };
    label.set_text("YOU LOST");
    game_over.set_visible(false);
    check(!ended(), "retained YOU LOST under hidden gameOver does not end sandbox");
    label.set_text("YOU WON");
    check(!ended(), "retained YOU WON under hidden gameOver does not end sandbox");
    game_over.set_visible(true);
    check(ended(), "visible victory ends match");
    label.set_text("YOU LOST");
    check(ended(), "visible defeat ends match");
    for (auto* ancestor : {&root, &game_over, &rewards, &banner, &label}) {
        ancestor->set_visible(false);
        check(!ended(), "hidden GUI ancestor hides the result banner");
        ancestor->set_visible(true);
    }
    screen.set_enabled(false);
    check(!ended(), "disabled ScreenGui hides the result banner");
    screen.set_enabled(true);
    player_gui.set_visible(false);
    check(ended(), "PlayerGui does not use GuiObject Visible");
    screen.set_visible(false);
    check(ended(), "ScreenGui uses Enabled rather than GuiObject Visible");
    screen.set_visible(true);
    label.set_text("YOU ARE READY");
    check(!ended(), "unrelated YOU text does not end match");
    label.set_text("YOU LOST");
    game_over.set_parent(nullptr);
    check(!ended(), "detached rewards tree does not end match");
    game_over.set_parent(&label);
    check(!ended(), "cyclic stale ancestry is rejected");
    game_over.set_parent(&root);
    FakeGui another_gui("PlayerGui");
    check(!match_has_ended(label.address(), another_gui.address()), "another PlayerGui cannot supply a result");
    check(!match_has_ended(0, player_gui.address()), "missing result banner does not end match");
    check(!match_has_ended(0x10000, player_gui.address()), "unreadable result banner does not end match");
    check(gui_effectively_visible(root.address(), player_gui.address()), "visible ability bar permits processing");
    root.set_visible(false);
    check(!gui_effectively_visible(root.address(), player_gui.address()), "hidden ability bar blocks processing");

    FakeGui content("TextButton");
    FakeGui cached_ammo("TextLabel", &content);
    FakeGui current_ammo("TextLabel", &content);
    cached_ammo.set_name("AmmoLabel");
    cached_ammo.set_text("3");
    current_ammo.set_name("AmmoLabel");
    current_ammo.set_text("2");
    content.set_children({&current_ammo});
    AbilityBtn button{};
    button.content = content.address();
    button.ammo = cached_ammo.address();
    check(btn_ready_count(button) == 2, "rebuilt GUI reads current charges instead of a cached label");
    cached_ammo.set_text("0");
    check(btn_ready_count(button) == 2, "stale zero cannot confirm a phantom cast");
    current_ammo.set_parent(nullptr);
    check(!btn_ready_count(button), "detached charge label is rejected");
    current_ammo.set_parent(&content);
    current_ammo.set_text("");
    check(!btn_ready_count(button), "unreadable charge text stays unknown rather than zero");
    content.set_children({});
    check(!btn_ready_count(button), "removed charge label is rejected");

    // btn_ready names the reason a slot is not pressed (it feeds the "not pressing" log line)
    FakeGui slot("TextButton");
    FakeGui timer("TextLabel", &slot);
    FakeGui binding("TextLabel", &slot);
    FakeGui lock("Frame", &slot);
    FakeGui price("TextLabel", &slot);
    FakeGui charges("TextLabel", &slot);
    charges.set_name("AmmoLabel");
    slot.set_children({&charges});
    timer.set_text("");
    binding.set_text("F");
    price.set_text("");
    charges.set_text("2");
    lock.set_visible(false);
    price.set_visible(false);
    AbilityBtn slot_button{};
    slot_button.content = slot.address();
    slot_button.time_left = timer.address();
    slot_button.binding = binding.address();
    slot_button.locked = lock.address();
    slot_button.price = price.address();
    slot_button.ammo = charges.address();
    char slot_key = 0;
    std::string why;
    const auto refused_with = [&](const char* part) {
        why.clear();
        return !btn_ready(slot_button, &slot_key, &why) && why.find(part) != std::string::npos;
    };
    check(btn_ready(slot_button, &slot_key, &why) && slot_key == 'F' && why.empty(), "ready slot reports its hotkey");
    timer.set_text("12");
    check(refused_with("cooldown timer \"12\""), "cooldown text is named in the refusal");
    bool cooling = false;
    check(!btn_ready(slot_button, &slot_key, &why, &cooling) && cooling, "a cooldown timer is flagged as the normal wait");
    timer.set_text("");
    lock.set_visible(true);
    check(refused_with("locked"), "locked slot is named in the refusal");
    check(!btn_ready(slot_button, &slot_key, &why, &cooling) && !cooling, "a locked slot is not flagged as a normal wait");
    lock.set_visible(false);
    price.set_visible(true);
    price.set_text("$500");
    check(refused_with("costs money"), "price is named in the refusal");
    price.set_visible(false);
    charges.set_text("0");
    check(refused_with("no ready charges"), "zero charges are named in the refusal");
    charges.set_text("");
    check(refused_with("unreadable"), "unreadable charges are named in the refusal");
    charges.set_text("2");
    binding.set_text("");
    check(refused_with("hotkey"), "missing hotkey label is named in the refusal");
    binding.set_text("F");
    check(btn_ready(slot_button, &slot_key, &why), "slot is ready again once every blocker is gone");
    timer.set_text("1");
    check(!btn_ready(slot_button, &slot_key), "refusal works without a reason out-parameter");

    // slot_action: a slot of unknown ability is never pressed; id rules and names keep working
    const GroupRule chain_rule{10}, off_rule{0};
    check(slot_action(false, nullptr) == SlotAction::Skip, "a slot with no id rule and no name is not pressed");
    check(slot_action(true, nullptr) == SlotAction::Spam, "an identified slot without a rule keeps the spam default");
    check(slot_action(true, &off_rule) == SlotAction::Off && slot_action(false, &off_rule) == SlotAction::Off,
          "off rules stop a slot, by name or by id");
    check(slot_action(true, &chain_rule) == SlotAction::Chain && slot_action(false, &chain_rule) == SlotAction::Chain,
          "chain rules apply by name or by id");
    {
        const char* ini_path = "game_state_test_abilities.ini";
        FILE* ini = fopen(ini_path, "w");
        if (ini) {
            fputs("Bounty = off\nDJ Booth/Drop the Beat = spam\n138164251626688 = Kingpin/Bounty\n", ini);
            fclose(ini);
        }
        const AbilityConfig cfg = load_ability_config(ini_path);
        remove(ini_path);
        const auto named = cfg.names.find("138164251626688");
        check(named != cfg.names.end() && named->second == "Kingpin/Bounty", "the id table names the Bounty slot");
        const auto rule = named == cfg.names.end() ? cfg.name_rules.end() : cfg.name_rules.find(canon_key(named->second));
        check(rule != cfg.name_rules.end() && slot_action(true, &rule->second) == SlotAction::Off,
              "the name rule 'Bounty = off' applies once the slot is identified");
    }
#ifdef TDS_SOURCE_DIR
    {
        // the config that ships next to the exe: Bounty is named by its icon id AND switched off, so it is
        // never pressed on a folder that has no tds_ui_rules.ini yet
        const AbilityConfig shipped = load_ability_config(TDS_SOURCE_DIR "/tds_abilities.ini");
        const auto bounty_name = shipped.names.find("138164251626688");
        check(bounty_name != shipped.names.end() && canon_key(bounty_name->second) == "bounty",
              "shipped id table names the Bounty slot");
        const auto bounty_rule = shipped.name_rules.find("bounty");
        check(bounty_rule != shipped.name_rules.end() && slot_action(true, &bounty_rule->second) == SlotAction::Off,
              "shipped rules switch Bounty off");
        const auto dj_rule = shipped.name_rules.find(canon_key("DJ Booth/Drop the Beat"));
        check(dj_rule == shipped.name_rules.end(), "DJ Booth keeps the plain spam default (no rule)");
    }
#endif
    CloseHandle(g_proc);
    g_proc = nullptr;
    printf("RESULT %d failures\n", failures);
    return failures ? 1 : 0;
}
