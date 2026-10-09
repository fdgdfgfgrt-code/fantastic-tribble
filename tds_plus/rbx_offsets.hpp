// language: C++17, file: rbx_offsets.hpp, runtime: any (no platform headers), target: tds+ engine
// Roblox client offsets used by the engine, chosen by the version of the running client.
//
// version-02c37bc51a384b8f: values live-verified in this project.
// version-cec3ad5889b447cf: values from the public dump (offsets.imtheo.lol, RbxDumperV2 2.2.4,
//   dumped 06/10/2026). Comparing that dump with the table this project was built on, 25 of 393
//   values differ between the two versions and only two of them are read by the engine:
//   VisualEngine::Pointer and GuiObject::Text. The others the engine uses are identical.
//   GUI_IMAGE (Image on an ImageButton, not a named entry of the dump) is not covered by the dump:
//   GuiObject::Image, ::Text, ::RichText and ::TextColor3 all moved by +0x18 in that dump, so
//   the value below is the old one plus 0x18. It is an inference; btn_image_id() probes the
//   neighbouring offsets and corrects it at run time if it is wrong.
//
// To support a new client: re-dump with rbx_dump.exe (or take the public table), add a row to
// kVersions at the top. Rows are matched by the folder name of the running RobloxPlayerBeta.exe.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace off {

struct VersionOffsets {
    const char* version;
    uintptr_t ve_pointer;  // VisualEngine::Pointer, RVA from the module base
    uintptr_t gui_text;    // GuiObject::Text, inline std::string: len@+0x10, cap@+0x18
    uintptr_t gui_image;   // Image on an ImageButton
};

inline constexpr VersionOffsets kVersions[] = {
    {"version-cec3ad5889b447cf", 0x8656e40, 0xe08, 0xc28},  // newest first; the default
    {"version-02c37bc51a384b8f", 0x858d208, 0xdf0, 0xc10},
};

// the same in every known version
constexpr uintptr_t VE_FAKE_DM         = 0xaf0;
constexpr uintptr_t FAKE_REAL_DM       = 0x1f8;
constexpr uintptr_t DM_PLACE_ID        = 0x188;
constexpr uintptr_t DM_WORKSPACE       = 0x150;
constexpr uintptr_t INST_NAME          = 0x70;
constexpr uintptr_t INST_CHILDREN      = 0x78;
constexpr uintptr_t INST_CLASS_DESC    = 0x18;
constexpr uintptr_t INST_PARENT        = 0x68;
constexpr uintptr_t PLAYERS_LOCAL      = 0x120;
constexpr uintptr_t SCREEN_GUI_ENABLED = 0x4b4;
constexpr uintptr_t GUI_VISIBLE        = 0x59d;

// differ between versions; written once per attach by use_version(), on the engine thread
inline uintptr_t VE_POINTER = kVersions[0].ve_pointer;
inline uintptr_t GUI_TEXT   = kVersions[0].gui_text;
inline uintptr_t GUI_IMAGE  = kVersions[0].gui_image;

// Selects the row for `version` and returns true. An unknown or empty version selects the newest
// row and returns false: its values may be wrong, the caller should say so.
inline bool use_version(const std::string& version) {
    for (const auto& row : kVersions) {
        if (version != row.version) continue;
        VE_POINTER = row.ve_pointer;
        GUI_TEXT = row.gui_text;
        GUI_IMAGE = row.gui_image;
        return true;
    }
    VE_POINTER = kVersions[0].ve_pointer;
    GUI_TEXT = kVersions[0].gui_text;
    GUI_IMAGE = kVersions[0].gui_image;
    return false;
}

// "C:\...\Roblox\Versions\version-02c37bc51a384b8f\RobloxPlayerBeta.exe" -> "version-02c37bc51a384b8f";
// empty when the path has no complete "version-<16 hex digits>" folder.
inline std::string roblox_version_from_path(const std::string& path) {
    std::string low = path;
    for (char& c : low)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
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

}  // namespace off
