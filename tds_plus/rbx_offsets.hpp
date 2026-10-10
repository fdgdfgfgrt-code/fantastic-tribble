// language: C++17, file: rbx_offsets.hpp, runtime: any (no platform headers), target: tds+ engine
// Roblox client offsets used by the engine, as one named set per client version, plus the parsing
// of the public offsets table that lets the engine learn a version it has no row for.
//
// version-02c37bc51a384b8f: live-verified in this project.
// version-cec3ad5889b447cf: from the public dump (offsets.imtheo.lol, RbxDumperV2 2.2.4, 06/10/2026).
//   Of the 14 offsets the engine uses, 13 are named entries of that dump; for these two versions 11
//   are identical and VisualEngine::Pointer and GuiObject::Text differ. The 14th, GUI_IMAGE (the
//   Image of an ImageButton), is not an entry of the dump: it is inferred as the offset of the
//   newest known row shifted by the same amount as GuiObject::Text (applied to the old row this
//   gives exactly the live-verified 0xc10), and btn_image_id() corrects it at run time if needed.
//
// A version this table does not know is looked up online by OffsetUpdater (offset_updater.hpp).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace off {

struct Set {
    uintptr_t ve_pointer;          // VisualEngine::Pointer, RVA from the module base
    uintptr_t ve_fake_dm;          // VisualEngine::FakeDataModel
    uintptr_t fake_real_dm;        // FakeDataModel::RealDataModel
    uintptr_t dm_place_id;         // DataModel::PlaceId
    uintptr_t dm_workspace;        // DataModel::Workspace
    uintptr_t inst_name;           // Instance::NameContainer
    uintptr_t inst_children;       // Instance::ChildrenStart
    uintptr_t inst_class_desc;     // Instance::ClassDescriptor
    uintptr_t inst_parent;         // Instance::Parent
    uintptr_t players_local;       // Player::LocalPlayer
    uintptr_t screen_gui_enabled;  // GuiObject::ScreenGui_Enabled
    uintptr_t gui_visible;         // GuiObject::Visible
    uintptr_t gui_text;            // GuiObject::Text, inline std::string: len@+0x10, cap@+0x18
    uintptr_t gui_image;           // Image on an ImageButton (not an entry of the public table)
};

inline constexpr uintptr_t kMaxRva = 0x40000000;  // sanity bounds for values taken from outside
inline constexpr uintptr_t kMaxField = 0x10000;

struct Field {
    const char* name;       // engine name; also the key in tds_offsets_cache.ini
    uintptr_t Set::*member;
    const char* dump_name;  // "Namespace::Name" in the public table; nullptr when it has no entry
    uintptr_t max;
};

inline constexpr Field kFields[] = {
    {"VE_POINTER", &Set::ve_pointer, "VisualEngine::Pointer", kMaxRva},
    {"VE_FAKE_DM", &Set::ve_fake_dm, "VisualEngine::FakeDataModel", kMaxField},
    {"FAKE_REAL_DM", &Set::fake_real_dm, "FakeDataModel::RealDataModel", kMaxField},
    {"DM_PLACE_ID", &Set::dm_place_id, "DataModel::PlaceId", kMaxField},
    {"DM_WORKSPACE", &Set::dm_workspace, "DataModel::Workspace", kMaxField},
    {"INST_NAME", &Set::inst_name, "Instance::NameContainer", kMaxField},
    {"INST_CHILDREN", &Set::inst_children, "Instance::ChildrenStart", kMaxField},
    {"INST_CLASS_DESC", &Set::inst_class_desc, "Instance::ClassDescriptor", kMaxField},
    {"INST_PARENT", &Set::inst_parent, "Instance::Parent", kMaxField},
    {"PLAYERS_LOCAL", &Set::players_local, "Player::LocalPlayer", kMaxField},
    {"SCREEN_GUI_ENABLED", &Set::screen_gui_enabled, "GuiObject::ScreenGui_Enabled", kMaxField},
    {"GUI_VISIBLE", &Set::gui_visible, "GuiObject::Visible", kMaxField},
    {"GUI_TEXT", &Set::gui_text, "GuiObject::Text", kMaxField},
    {"GUI_IMAGE", &Set::gui_image, nullptr, kMaxField},
};
inline constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

struct Row {
    const char* version;
    Set set;
};

// newest first; the first row is also the guess for a version nobody knows
inline constexpr Row kVersions[] = {
    {"version-cec3ad5889b447cf", {0x8656e40, 0xaf0, 0x1f8, 0x188, 0x150, 0x70, 0x78, 0x18, 0x68, 0x120, 0x4b4, 0x59d, 0xe08, 0xc28}},
    {"version-02c37bc51a384b8f", {0x858d208, 0xaf0, 0x1f8, 0x188, 0x150, 0x70, 0x78, 0x18, 0x68, 0x120, 0x4b4, 0x59d, 0xdf0, 0xc10}},
};

// the set in use; written by apply() on the engine thread only
inline Set current = kVersions[0].set;
inline uintptr_t& VE_POINTER = current.ve_pointer;
inline uintptr_t& VE_FAKE_DM = current.ve_fake_dm;
inline uintptr_t& FAKE_REAL_DM = current.fake_real_dm;
inline uintptr_t& DM_PLACE_ID = current.dm_place_id;
inline uintptr_t& DM_WORKSPACE = current.dm_workspace;
inline uintptr_t& INST_NAME = current.inst_name;
inline uintptr_t& INST_CHILDREN = current.inst_children;
inline uintptr_t& INST_CLASS_DESC = current.inst_class_desc;
inline uintptr_t& INST_PARENT = current.inst_parent;
inline uintptr_t& PLAYERS_LOCAL = current.players_local;
inline uintptr_t& SCREEN_GUI_ENABLED = current.screen_gui_enabled;
inline uintptr_t& GUI_VISIBLE = current.gui_visible;
inline uintptr_t& GUI_TEXT = current.gui_text;
inline uintptr_t& GUI_IMAGE = current.gui_image;

inline void apply(const Set& set) { current = set; }

inline const Set* known_set(const std::string& version) {
    for (const Row& row : kVersions)
        if (version == row.version) return &row.set;
    return nullptr;
}

// equal in every field except GUI_IMAGE, which is inferred and corrected at run time
inline bool same_ignoring_image(const Set& a, const Set& b) {
    for (const Field& f : kFields)
        if (f.dump_name && a.*(f.member) != b.*(f.member)) return false;
    return true;
}

namespace detail {

inline std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const size_t first = s.find_first_not_of(ws);
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(ws) - first + 1);
}

inline bool starts_with(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

inline std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// "0x1f8" at s[i...] -> value, i moved past the digits; false without 1..16 hex digits
inline bool parse_hex(const std::string& s, size_t& i, uintptr_t& out) {
    if (i + 2 > s.size() || s[i] != '0' || (s[i + 1] != 'x' && s[i + 1] != 'X')) return false;
    size_t j = i + 2;
    unsigned long long value = 0;
    size_t digits = 0;
    for (; j < s.size(); ++j, ++digits) {
        const char c = s[j];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if (digits >= 16) return false;
        value = value * 16 + static_cast<unsigned>(d);
    }
    if (digits == 0) return false;
    out = static_cast<uintptr_t>(value);
    i = j;
    return true;
}

inline std::string hex(uintptr_t v) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    do {
        out.insert(out.begin(), digits[v & 0xF]);
        v >>= 4;
    } while (v);
    return "0x" + out;
}

}  // namespace detail

// "C:\...\Roblox\Versions\version-02c37bc51a384b8f\RobloxPlayerBeta.exe" -> "version-02c37bc51a384b8f";
// empty when the path has no complete "version-<16 hex digits>" folder.
inline std::string roblox_version_from_path(const std::string& path) {
    const std::string low = detail::lower(path);
    const std::string prefix = "version-";
    constexpr size_t kDigits = 16;
    for (size_t at = low.find(prefix); at != std::string::npos; at = low.find(prefix, at + 1)) {
        const size_t first = at + prefix.size(), end = first + kDigits;
        if (end > low.size()) return {};
        bool hex = true;
        for (size_t i = first; i < end; ++i)
            hex = hex && ((low[i] >= '0' && low[i] <= '9') || (low[i] >= 'a' && low[i] <= 'f'));
        if (hex && (end == low.size() || low[end] == '\\' || low[end] == '/')) return low.substr(at, end - at);
    }
    return {};
}

inline bool is_version(const std::string& s) {
    return !s.empty() && roblox_version_from_path(s) == s;
}

// ---- the public offsets table (C++ header: "namespace X { inline constexpr uintptr_t Y = 0x..; }") ----

struct Table {
    std::string version;                       // inline std::string ClientVersion = "version-...";
    std::map<std::string, uintptr_t> values;   // "Namespace::Name" -> value (first occurrence wins)
};

inline Table parse_table(const std::string& text) {
    Table table;
    if (text.size() > (4u << 20)) return table;
    const std::string kConstexpr = "inline constexpr uintptr_t ";
    std::vector<std::string> scopes;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string line = detail::trim(text.substr(pos, eol - pos));
        pos = eol + 1;
        if (detail::starts_with(line, "namespace ")) {
            const size_t brace = line.find('{');
            if (brace != std::string::npos) scopes.push_back(detail::trim(line.substr(10, brace - 10)));
        } else if (!line.empty() && line[0] == '}') {
            if (!scopes.empty()) scopes.pop_back();
        } else if (detail::starts_with(line, "inline std::string ClientVersion")) {
            const size_t a = line.find('"');
            const size_t b = a == std::string::npos ? a : line.find('"', a + 1);
            if (b != std::string::npos) table.version = line.substr(a + 1, b - a - 1);
        } else if (detail::starts_with(line, kConstexpr.c_str()) && !scopes.empty()) {
            const size_t eq = line.find('=', kConstexpr.size());
            if (eq == std::string::npos) continue;
            const std::string name = detail::trim(line.substr(kConstexpr.size(), eq - kConstexpr.size()));
            size_t i = line.find_first_not_of(" \t", eq + 1);
            uintptr_t value = 0;
            if (name.empty() || i == std::string::npos || !detail::parse_hex(line, i, value)) continue;
            const size_t semi = line.find_first_not_of(" \t", i);
            if (semi == std::string::npos || line[semi] != ';') continue;
            table.values.emplace(scopes.back() + "::" + name, value);
        }
    }
    return table;
}

// Builds the set for `table`. Every offset that has a table entry must be present and plausible;
// GUI_IMAGE is inferred from `reference` shifted by the same amount as GuiObject::Text.
inline bool set_from_table(const Table& table, const Set& reference, Set& out, std::string& why) {
    Set set = reference;
    for (const Field& f : kFields) {
        if (!f.dump_name) continue;
        const auto it = table.values.find(f.dump_name);
        if (it == table.values.end()) {
            why = std::string("the table has no ") + f.dump_name;
            return false;
        }
        if (it->second == 0 || it->second > f.max) {
            why = std::string(f.dump_name) + " = " + detail::hex(it->second) + " is not plausible";
            return false;
        }
        set.*(f.member) = it->second;
    }
    const intptr_t image = static_cast<intptr_t>(reference.gui_image) +
                           (static_cast<intptr_t>(set.gui_text) - static_cast<intptr_t>(reference.gui_text));
    if (image <= 0 || static_cast<uintptr_t>(image) > kMaxField) {
        why = "the inferred image offset is not plausible";
        return false;
    }
    set.gui_image = static_cast<uintptr_t>(image);
    out = set;
    return true;
}

// The table must belong to the running client: a table for another version would be wrong in
// exactly the offsets that matter.
inline bool table_for_version(const std::string& text, const std::string& running, const Set& reference,
                              Set& out, std::string& why) {
    const Table table = parse_table(text);
    if (table.version.empty()) {
        why = "the table does not say which Roblox version it is for";
        return false;
    }
    if (table.version != running) {
        why = "the table is for " + table.version + ", the running client is " + running;
        return false;
    }
    return set_from_table(table, reference, out, why);
}

// ---- tds_offsets.ini (settings, edited by the user) and tds_offsets_cache.ini (written by the app) ----

inline constexpr const char* kDefaultSource = "https://offsets.imtheo.lol/Offsets.hpp";

struct Settings {
    bool auto_update = true;
    std::string source = kDefaultSource;
};

inline Settings parse_settings(const std::string& text) {
    Settings settings;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        const size_t comment = line.find(';');
        if (comment != std::string::npos) line.resize(comment);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = detail::lower(detail::trim(line.substr(0, eq)));
        const std::string value = detail::trim(line.substr(eq + 1));
        if (key == "auto_update") {
            const std::string v = detail::lower(value);
            if (v == "off" || v == "false" || v == "no" || v == "0") settings.auto_update = false;
            else if (v == "on" || v == "true" || v == "yes" || v == "1") settings.auto_update = true;
        } else if (key == "source" && detail::starts_with(detail::lower(value), "https://")) {
            settings.source = value;
        }
    }
    return settings;
}

inline std::string format_cache_line(const std::string& version, const Set& set) {
    std::string line = version + " =";
    for (const Field& f : kFields) line += std::string(" ") + f.name + "=" + detail::hex(set.*(f.member));
    return line;
}

// "version-xxxxxxxxxxxxxxxx = NAME=0x.. NAME=0x.. ..." lines; a line with a missing or implausible
// value is ignored as a whole
inline std::map<std::string, Set> parse_cache(const std::string& text) {
    std::map<std::string, Set> learned;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string line = detail::trim(text.substr(pos, eol - pos));
        pos = eol + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == ';') continue;
        const std::string version = detail::lower(detail::trim(line.substr(0, eq)));
        if (!is_version(version)) continue;
        Set set{};
        size_t found = 0;
        size_t at = eq + 1;
        bool ok = true;
        while (ok) {
            at = line.find_first_not_of(" \t", at);
            if (at == std::string::npos) break;
            const size_t end = line.find_first_of(" \t", at);
            const std::string token = line.substr(at, end == std::string::npos ? end : end - at);
            at = end == std::string::npos ? line.size() : end;
            const size_t tok_eq = token.find('=');
            ok = tok_eq != std::string::npos;
            if (!ok) break;
            const std::string name = token.substr(0, tok_eq);
            size_t i = tok_eq + 1;
            uintptr_t value = 0;
            ok = detail::parse_hex(token, i, value) && i == token.size();
            if (!ok) break;
            for (const Field& f : kFields) {
                if (name != f.name) continue;
                ok = value != 0 && value <= f.max;
                set.*(f.member) = value;
                ++found;
            }
        }
        if (ok && found == kFieldCount) learned[version] = set;
    }
    return learned;
}

// `existing` with the line of `version` replaced (or added); every other line is kept
inline std::string cache_with(const std::string& existing, const std::string& version, const Set& set) {
    std::string out;
    size_t pos = 0;
    while (pos < existing.size()) {
        size_t eol = existing.find('\n', pos);
        const bool last = eol == std::string::npos;
        if (last) eol = existing.size();
        const std::string line = existing.substr(pos, eol - pos);
        pos = eol + 1;
        const size_t eq = line.find('=');
        const bool same = eq != std::string::npos && detail::lower(detail::trim(line.substr(0, eq))) == version;
        if (!same) out += line + "\n";
    }
    if (out.empty()) out = "; tds+ offsets learned at run time, written by the app; safe to delete\n";
    return out + format_cache_line(version, set) + "\n";
}

}  // namespace off
