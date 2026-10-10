// language: C++17, file: rbx_offsets_test.cpp, runtime: any, target: tds+ client version / offsets selection
#include <cstdio>
#include <string>

#include "../rbx_offsets.hpp"

static int failures = 0;
#define CHECK(expr, name)                                                                   \
    do {                                                                                    \
        const bool ok_ = static_cast<bool>(expr);                                           \
        std::printf("%s %s\n", ok_ ? "PASS" : "FAIL", name);                                \
        failures += !ok_;                                                                   \
    } while (0)

int main() {
    using off::roblox_version_from_path;

    // version from the process image path
    CHECK(roblox_version_from_path("C:\\Program Files\\Roblox\\Versions\\version-02c37bc51a384b8f\\RobloxPlayerBeta.exe")
              == "version-02c37bc51a384b8f", "program files install");
    CHECK(roblox_version_from_path("C:\\Users\\me\\AppData\\Local\\Roblox\\Versions\\version-cec3ad5889b447cf\\RobloxPlayerBeta.exe")
              == "version-cec3ad5889b447cf", "per-user install");
    CHECK(roblox_version_from_path("C:/Roblox/Versions/version-cec3ad5889b447cf/RobloxPlayerBeta.exe")
              == "version-cec3ad5889b447cf", "forward slashes");
    CHECK(roblox_version_from_path("C:\\ROBLOX\\VERSIONS\\VERSION-CEC3AD5889B447CF\\RobloxPlayerBeta.exe")
              == "version-cec3ad5889b447cf", "upper case folder is normalised");
    CHECK(roblox_version_from_path("C:\\Roblox\\version-cec3ad5889b447cf").empty() == false, "version folder at the end of the path");
    CHECK(roblox_version_from_path("C:\\Games\\RobloxPlayerBeta.exe").empty(), "no version folder");
    CHECK(roblox_version_from_path("").empty(), "empty path");
    CHECK(roblox_version_from_path("C:\\Roblox\\version-cec3ad5889b447c\\x.exe").empty(), "15 digits is not a version");
    CHECK(roblox_version_from_path("C:\\Roblox\\version-cec3ad5889b447cfa\\x.exe").empty(), "17 digits is not a version");
    CHECK(roblox_version_from_path("C:\\Roblox\\version-zec3ad5889b447cf\\x.exe").empty(), "non-hex digits are rejected");
    CHECK(roblox_version_from_path("C:\\version-short\\Roblox\\Versions\\version-02c37bc51a384b8f\\x.exe")
              == "version-02c37bc51a384b8f", "an earlier version-looking folder does not hide the real one");

    // table rows are what was verified / dumped
    CHECK(off::kVersions[0].version == std::string("version-cec3ad5889b447cf")
              && off::kVersions[0].ve_pointer == 0x8656e40 && off::kVersions[0].gui_text == 0xe08,
          "newest row matches the public dump for version-cec3ad5889b447cf");
    CHECK(off::kVersions[1].version == std::string("version-02c37bc51a384b8f")
              && off::kVersions[1].ve_pointer == 0x858d208 && off::kVersions[1].gui_text == 0xdf0
              && off::kVersions[1].gui_image == 0xc10,
          "previous row keeps the live-verified values");
    CHECK(off::kVersions[0].gui_image == off::kVersions[1].gui_image + 0x18,
          "inferred image offset moved by the same +0x18 as the other GuiObject text fields");
    bool unique = true;
    for (size_t i = 0; i < sizeof(off::kVersions) / sizeof(off::kVersions[0]); ++i)
        for (size_t j = i + 1; j < sizeof(off::kVersions) / sizeof(off::kVersions[0]); ++j)
            unique = unique && std::string(off::kVersions[i].version) != off::kVersions[j].version;
    CHECK(unique, "no version is listed twice");

    // selection
    CHECK(off::VE_POINTER == 0x8656e40 && off::GUI_TEXT == 0xe08 && off::GUI_IMAGE == 0xc28, "defaults are the newest row");
    CHECK(off::use_version("version-02c37bc51a384b8f") && off::VE_POINTER == 0x858d208 && off::GUI_TEXT == 0xdf0
              && off::GUI_IMAGE == 0xc10,
          "the previous version selects its own offsets");
    CHECK(off::use_version("version-cec3ad5889b447cf") && off::VE_POINTER == 0x8656e40 && off::GUI_TEXT == 0xe08,
          "the newest version selects its own offsets");
    off::use_version("version-02c37bc51a384b8f");
    CHECK(!off::use_version("version-0123456789abcdef") && off::VE_POINTER == 0x8656e40 && off::GUI_TEXT == 0xe08
              && off::GUI_IMAGE == 0xc28,
          "an unknown version is reported and falls back to the newest offsets");
    off::use_version("version-02c37bc51a384b8f");
    CHECK(!off::use_version("") && off::VE_POINTER == 0x8656e40, "an empty version is unknown too");

    std::printf("RESULT %d failures\n", failures);
    return failures ? 1 : 0;
}
