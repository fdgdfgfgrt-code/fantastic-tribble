// language: C++17, file: rbx_offsets_test.cpp, runtime: any, target: tds+ client versions, offsets table, settings
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "../rbx_offsets.hpp"

static int failures = 0;
#define CHECK(expr, name)                                                                   \
    do {                                                                                    \
        const bool ok_ = static_cast<bool>(expr);                                           \
        std::printf("%s %s\n", ok_ ? "PASS" : "FAIL", name);                                \
        failures += !ok_;                                                                   \
    } while (0)

// a table in the format of the public one, built from a set
static std::string make_table(const std::string& version, const off::Set& set, const std::string& eol = "\n") {
    std::string text = "#pragma once" + eol + "/* header comment with a } and a namespace x { inside */" + eol +
                       "namespace Offsets {" + eol;
    if (!version.empty()) text += "    inline std::string ClientVersion = \"" + version + "\";" + eol;
    std::string open;
    for (const off::Field& f : off::kFields) {
        if (!f.dump_name) continue;
        const std::string full = f.dump_name;
        const size_t sep = full.find("::");
        const std::string ns = full.substr(0, sep), name = full.substr(sep + 2);
        text += "    namespace " + ns + " {" + eol + "         inline constexpr uintptr_t " + name + " = " +
                off::detail::hex(set.*(f.member)) + ";" + eol + "    }" + eol;
    }
    return text + "}" + eol;
}

int main() {
    using off::roblox_version_from_path;
    const off::Set& newest = off::kVersions[0].set;
    const off::Set& previous = off::kVersions[1].set;

    // ---- version from the process image path ----
    CHECK(roblox_version_from_path("C:\\Program Files\\Roblox\\Versions\\version-02c37bc51a384b8f\\RobloxPlayerBeta.exe")
              == "version-02c37bc51a384b8f", "program files install");
    CHECK(roblox_version_from_path("C:\\Users\\me\\AppData\\Local\\Roblox\\Versions\\version-cec3ad5889b447cf\\RobloxPlayerBeta.exe")
              == "version-cec3ad5889b447cf", "per-user install");
    CHECK(roblox_version_from_path("C:/Roblox/Versions/version-cec3ad5889b447cf/RobloxPlayerBeta.exe")
              == "version-cec3ad5889b447cf", "forward slashes");
    CHECK(roblox_version_from_path("C:\\ROBLOX\\VERSIONS\\VERSION-CEC3AD5889B447CF\\RobloxPlayerBeta.exe")
              == "version-cec3ad5889b447cf", "upper case folder is normalised");
    CHECK(!roblox_version_from_path("C:\\Roblox\\version-cec3ad5889b447cf").empty(), "version folder at the end of the path");
    CHECK(roblox_version_from_path("C:\\Games\\RobloxPlayerBeta.exe").empty(), "no version folder");
    CHECK(roblox_version_from_path("").empty(), "empty path");
    CHECK(roblox_version_from_path("C:\\Roblox\\version-cec3ad5889b447c\\x.exe").empty(), "15 digits is not a version");
    CHECK(roblox_version_from_path("C:\\Roblox\\version-cec3ad5889b447cfa\\x.exe").empty(), "17 digits is not a version");
    CHECK(roblox_version_from_path("C:\\Roblox\\version-zec3ad5889b447cf\\x.exe").empty(), "non-hex digits are rejected");
    CHECK(roblox_version_from_path("C:\\version-short\\Roblox\\Versions\\version-02c37bc51a384b8f\\x.exe")
              == "version-02c37bc51a384b8f", "an earlier version-looking folder does not hide the real one");
    CHECK(off::is_version("version-02c37bc51a384b8f") && !off::is_version("version-02c37bc51a384b8") && !off::is_version(""),
          "is_version accepts exactly one complete version string");

    // ---- the built-in rows ----
    CHECK(std::string(off::kVersions[0].version) == "version-cec3ad5889b447cf" && newest.ve_pointer == 0x8656e40
              && newest.gui_text == 0xe08,
          "newest row matches the public dump for version-cec3ad5889b447cf");
    CHECK(std::string(off::kVersions[1].version) == "version-02c37bc51a384b8f" && previous.ve_pointer == 0x858d208
              && previous.gui_text == 0xdf0 && previous.gui_image == 0xc10,
          "previous row keeps the live-verified values");
    CHECK(off::known_set("version-02c37bc51a384b8f") == &previous && off::known_set("version-cec3ad5889b447cf") == &newest
              && off::known_set("version-0123456789abcdef") == nullptr && off::known_set("") == nullptr,
          "known_set finds exactly the built-in versions");
    off::apply(previous);
    CHECK(off::VE_POINTER == 0x858d208 && off::GUI_TEXT == 0xdf0 && off::GUI_IMAGE == 0xc10 && off::INST_NAME == 0x70,
          "apply() changes every alias the engine reads");
    off::GUI_IMAGE = 0xc28;  // the run-time probe writes through the alias
    CHECK(off::current.gui_image == 0xc28, "an alias is the field of the set in use");
    off::apply(newest);
    CHECK(off::VE_POINTER == 0x8656e40 && off::GUI_TEXT == 0xe08 && off::GUI_IMAGE == 0xc28, "apply() switches back");
    CHECK(off::same_ignoring_image(newest, newest) && !off::same_ignoring_image(newest, previous), "same_ignoring_image");
    off::Set other_image = newest;
    other_image.gui_image += 8;
    CHECK(off::same_ignoring_image(newest, other_image), "the inferred image offset is ignored when comparing");

    // ---- real data: the table the project was built on (offsets/External_Offsets.hpp) ----
#ifdef TDS_REPO_DIR
    {
        std::ifstream in(TDS_REPO_DIR "/offsets/External_Offsets.hpp", std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        const std::string text = buffer.str();
        CHECK(!text.empty(), "the repository's offsets table is readable");
        const off::Table table = off::parse_table(text);
        CHECK(table.version == "version-02c37bc51a384b8f", "version read from the real table");
        CHECK(table.values.size() > 300, "hundreds of entries parsed from the real table");
        CHECK(table.values.count("VisualEngine::Pointer") && table.values.at("VisualEngine::Pointer") == 0x858d208,
              "VisualEngine::Pointer from the real table");
        off::Set learned{};
        std::string why;
        CHECK(off::set_from_table(table, newest, learned, why), "a set is built from the real table");
        CHECK(learned.ve_pointer == previous.ve_pointer && learned.gui_text == previous.gui_text
                  && off::same_ignoring_image(learned, previous),
              "the real table gives exactly the live-verified offsets of the previous row");
        CHECK(learned.gui_image == previous.gui_image,
              "shifting the newest image offset like GuiObject::Text reproduces the live-verified 0xc10");
        off::Set out{};
        CHECK(off::table_for_version(text, "version-02c37bc51a384b8f", newest, out, why) && out.gui_image == 0xc10,
              "table_for_version accepts the real table for its own version");
        CHECK(!off::table_for_version(text, "version-cec3ad5889b447cf", newest, out, why)
                  && why.find("version-02c37bc51a384b8f") != std::string::npos,
              "table_for_version rejects the real table for another version and says which one it is for");
    }
#endif

    // ---- synthetic tables ----
    {
        off::Set s = newest;
        s.ve_pointer = 0x9000000;
        s.gui_text = 0xe20;  // +0x18 against the newest row
        const std::string v = "version-aaaaaaaaaaaaaaaa";
        off::Set out{};
        std::string why;
        CHECK(off::table_for_version(make_table(v, s), v, newest, out, why) && out.ve_pointer == 0x9000000
                  && out.gui_text == 0xe20 && out.gui_image == newest.gui_image + 0x18,
              "a valid table for the running version gives its offsets and a shifted image offset");
        CHECK(off::table_for_version(make_table(v, s, "\r\n"), v, newest, out, why), "CRLF line endings are accepted");
        CHECK(!off::table_for_version(make_table(v, s), "version-bbbbbbbbbbbbbbbb", newest, out, why)
                  && why.find("version-aaaaaaaaaaaaaaaa") != std::string::npos,
              "a table for another version is rejected");
        CHECK(!off::table_for_version(make_table("", s), v, newest, out, why) && why.find("does not say") != std::string::npos,
              "a table without a version is rejected");
        std::string broken = make_table(v, s);
        broken.erase(broken.find("namespace VisualEngine"), broken.find("namespace FakeDataModel") - broken.find("namespace VisualEngine"));
        CHECK(!off::table_for_version(broken, v, newest, out, why) && why.find("VisualEngine::Pointer") != std::string::npos,
              "a table missing an offset the engine needs is rejected, naming it");
        off::Set zero = s;
        zero.inst_parent = 0;
        CHECK(!off::table_for_version(make_table(v, zero), v, newest, out, why) && why.find("Instance::Parent") != std::string::npos,
              "a zero offset (the dump's 'not found') is rejected");
        off::Set huge = s;
        huge.ve_pointer = 0x7fffffffffff;
        CHECK(!off::table_for_version(make_table(v, huge), v, newest, out, why), "an implausibly large RVA is rejected");
        off::Set big_field = s;
        big_field.inst_name = 0x20000;
        CHECK(!off::table_for_version(make_table(v, big_field), v, newest, out, why), "an implausibly large field offset is rejected");
        off::Set low_text = s;
        low_text.gui_text = 0x10;  // would put the inferred image offset below zero
        CHECK(!off::table_for_version(make_table(v, low_text), v, newest, out, why), "an impossible inferred image offset is rejected");
        CHECK(!off::table_for_version("", v, newest, out, why) && !off::table_for_version("garbage {{ ; }", v, newest, out, why),
              "empty and garbage input are rejected");
        CHECK(off::parse_table(std::string(5u << 20, 'x')).values.empty(), "an oversized input is not parsed");

        const off::Table dup = off::parse_table("namespace A {\n inline constexpr uintptr_t X = 0x10;\n inline constexpr uintptr_t X = 0x20;\n}\n");
        CHECK(dup.values.at("A::X") == 0x10, "the first occurrence of a name wins");
        const off::Table nested = off::parse_table("namespace Offsets {\n namespace A {\n inline constexpr uintptr_t X = 0x10;\n }\n inline constexpr uintptr_t Y = 0x20;\n}\n");
        CHECK(nested.values.at("A::X") == 0x10 && nested.values.at("Offsets::Y") == 0x20, "names are scoped by their innermost namespace");
        const off::Table junk = off::parse_table("namespace A {\n inline constexpr uintptr_t X = 0x;\n inline constexpr uintptr_t Z = 12;\n inline constexpr uintptr_t W = 0x10\n inline constexpr uintptr_t V = 0x1g;\n}\n");
        CHECK(junk.values.empty(), "malformed values are skipped, not guessed");
    }

    // ---- tds_offsets.ini ----
    {
        off::Settings defaults = off::parse_settings("");
        CHECK(defaults.auto_update && defaults.source == off::kDefaultSource, "settings default to auto_update on and the built-in source");
        CHECK(!off::parse_settings("auto_update = off\n").auto_update && !off::parse_settings("AUTO_UPDATE=No ; why\n").auto_update
                  && !off::parse_settings("auto_update = 0").auto_update && !off::parse_settings("auto_update = false").auto_update,
              "auto_update accepts off / no / 0 / false in any case, with a comment");
        CHECK(off::parse_settings("auto_update = off\nauto_update = on\n").auto_update, "the last auto_update line wins");
        CHECK(off::parse_settings("auto_update = maybe\n").auto_update, "an unknown auto_update value keeps the default");
        CHECK(off::parse_settings("source = https://example.org/o.hpp\n").source == "https://example.org/o.hpp", "a custom https source is used");
        CHECK(off::parse_settings("source = http://example.org/o.hpp\n").source == off::kDefaultSource, "a plain http source is ignored");
        CHECK(off::parse_settings("; source = https://example.org/o.hpp\n").source == off::kDefaultSource, "a commented-out source is ignored");
    }

    // ---- tds_offsets_cache.ini ----
    {
        const std::string v1 = "version-1111111111111111", v2 = "version-2222222222222222";
        off::Set a = newest, b = previous;
        a.ve_pointer = 0x1234567;
        const std::string line = off::format_cache_line(v1, a);
        const auto parsed = off::parse_cache(line + "\n");
        CHECK(parsed.size() == 1 && parsed.count(v1) && parsed.at(v1).ve_pointer == 0x1234567
                  && off::same_ignoring_image(parsed.at(v1), a) && parsed.at(v1).gui_image == a.gui_image,
              "a cache line round-trips, including the image offset");
        std::string text = off::cache_with("", v1, a);
        CHECK(text.rfind(";", 0) == 0 && off::parse_cache(text).count(v1), "a new cache file starts with a comment and holds the line");
        text = off::cache_with(text, v2, b);
        CHECK(off::parse_cache(text).size() == 2, "a second version is added next to the first");
        a.ve_pointer = 0x7654321;
        text = off::cache_with(text, v1, a);
        const auto again = off::parse_cache(text);
        CHECK(again.size() == 2 && again.at(v1).ve_pointer == 0x7654321 && again.at(v2).ve_pointer == previous.ve_pointer,
              "saving a known version replaces its line and keeps the others");
        size_t lines = 0;
        for (char c : text) lines += c == '\n';
        CHECK(lines == 3, "no duplicate lines are left behind (comment + two versions)");
        CHECK(off::parse_cache(line.substr(0, line.size() - 12) + "\n").empty(), "a truncated line is ignored as a whole");
        std::string bad = line;
        bad.replace(bad.find("VE_POINTER=0x1234567"), 20, "VE_POINTER=0x0");
        CHECK(off::parse_cache(bad).empty(), "a line with a zero offset is ignored");
        std::string unknown_key = line + " EXTRA=0x1";
        CHECK(off::parse_cache(unknown_key).size() == 1, "an unknown key on a line is tolerated");
        CHECK(off::parse_cache("version-short = VE_POINTER=0x1\n; comment = x\n= nothing\nnot a line\n").empty(),
              "junk lines are ignored");
        std::string upper = line;
        upper.replace(0, v1.size(), "VERSION-1111111111111111");
        CHECK(off::parse_cache(upper).count(v1) == 1, "the version key is case-insensitive");
    }

    std::printf("RESULT %d failures\n", failures);
    return failures ? 1 : 0;
}
