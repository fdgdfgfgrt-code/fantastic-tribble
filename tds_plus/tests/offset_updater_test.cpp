// language: C++17, file: offset_updater_test.cpp, runtime: any, target: tds+ offset update policy
#include <cstdio>
#include <string>
#include <vector>

#include "../offset_updater.hpp"

static int failures = 0;
#define CHECK(expr, name)                                                                   \
    do {                                                                                    \
        const bool ok_ = static_cast<bool>(expr);                                           \
        std::printf("%s %s\n", ok_ ? "PASS" : "FAIL", name);                                \
        failures += !ok_;                                                                   \
    } while (0)

using Clock = OffsetUpdater::Clock;
using namespace std::chrono_literals;

static const std::string kNewVersion = "version-aaaaaaaaaaaaaaaa";
static const std::string kOtherVersion = "version-bbbbbbbbbbbbbbbb";

static std::string make_table(const std::string& version, const off::Set& set) {
    std::string text = "namespace Offsets {\n    inline std::string ClientVersion = \"" + version + "\";\n";
    for (const off::Field& f : off::kFields) {
        if (!f.dump_name) continue;
        const std::string full = f.dump_name;
        const size_t sep = full.find("::");
        text += "    namespace " + full.substr(0, sep) + " {\n         inline constexpr uintptr_t " + full.substr(sep + 2) +
                " = " + off::detail::hex(set.*(f.member)) + ";\n    }\n";
    }
    return text + "}\n";
}

// everything the updater can touch, with the network under the test's control
struct World {
    std::string settings, cache;
    std::vector<std::string> log;
    std::vector<std::string> fetched_urls;
    bool fetch_in_flight = false;
    bool fetch_ready = false;
    std::string response_body, response_error;
    int applied_count = 0;
    bool cache_writable = true;
    int cache_writes = 0;

    OffsetUpdater::Io io() {
        OffsetUpdater::Io io;
        io.load_settings = [this] { return settings; };
        io.load_cache = [this] { return cache; };
        io.save_cache = [this](const std::string& text) {
            ++cache_writes;
            if (!cache_writable) return false;
            cache = text;
            return true;
        };
        io.log = [this](const std::string& line) { log.push_back(line); };
        io.start_fetch = [this](const std::string& url) {
            fetched_urls.push_back(url);
            fetch_in_flight = true;
            fetch_ready = false;
        };
        io.poll_fetch = [this](std::string& body, std::string& error) {
            if (!fetch_in_flight || !fetch_ready) return false;
            fetch_in_flight = false;
            body = response_body;
            error = response_error;
            return true;
        };
        io.applied = [this] { ++applied_count; };
        return io;
    }
    void respond(const std::string& body, const std::string& error = "") {
        response_body = body;
        response_error = error;
        fetch_ready = true;
    }
    bool logged(const std::string& part) const {
        for (const auto& line : log)
            if (line.find(part) != std::string::npos) return true;
        return false;
    }
};

static void reset_offsets() { off::apply(off::kVersions[0].set); }

int main() {
    const Clock::time_point t0 = Clock::now();
    off::Set fresh = off::kVersions[0].set;
    fresh.ve_pointer = 0x9000000;
    fresh.gui_text = 0xe20;
    fresh.inst_children = 0x80;

    // 1. a built-in version: offsets built in, no download while the game data shows up
    {
        reset_offsets();
        World w;
        OffsetUpdater up(w.io());
        up.attach("version-02c37bc51a384b8f", t0);
        CHECK(up.known() && up.source() == "built in" && off::VE_POINTER == 0x858d208 && off::GUI_TEXT == 0xdf0,
              "a built-in version selects its own offsets");
        CHECK(w.applied_count == 1, "the engine is told once that the offsets changed");
        up.tick(t0 + 5s, true);
        CHECK(up.confirmed() && w.logged("confirmed (built in)"), "the game data being found confirms the offsets");
        up.tick(t0 + 10min, false);
        CHECK(w.fetched_urls.empty(), "a confirmed built-in version never goes online");
    }

    // 2. a built-in version whose game data never shows up (e.g. the player sits on the home screen)
    {
        reset_offsets();
        World w;
        OffsetUpdater up(w.io());
        up.attach("version-02c37bc51a384b8f", t0);
        up.tick(t0 + 30s, false);
        CHECK(w.fetched_urls.empty(), "a known version is given a minute to show the game before anything is downloaded");
        up.tick(t0 + 61s, false);
        CHECK(w.fetched_urls.size() == 1 && w.fetched_urls[0] == off::kDefaultSource, "after a minute without the game data the table is requested");
        w.respond(make_table("version-02c37bc51a384b8f", off::kVersions[1].set));
        up.tick(t0 + 63s, false);
        CHECK(w.logged("nothing to change") && off::VE_POINTER == 0x858d208, "the same offsets in the table change nothing");
        up.tick(t0 + 20min, false);
        CHECK(w.fetched_urls.size() == 1, "and the table is not asked again during this connection");
    }

    // 3. a built-in version whose row turns out to be wrong: the table corrects it
    {
        reset_offsets();
        World w;
        OffsetUpdater up(w.io());
        up.attach("version-02c37bc51a384b8f", t0);
        up.tick(t0 + 61s, false);
        w.respond(make_table("version-02c37bc51a384b8f", fresh));
        up.tick(t0 + 62s, false);
        CHECK(off::VE_POINTER == 0x9000000 && off::INST_CHILDREN == 0x80 && up.source() == "downloaded",
              "a different table for a known version replaces its offsets");
        CHECK(w.logged("VE_POINTER 0x858d208 -> 0x9000000") && w.logged("GUI_IMAGE") && w.logged("(inferred)"),
              "the log names what changed, and marks the inferred offset");
        CHECK(w.cache_writes == 0, "nothing is saved before the offsets are confirmed");
        up.tick(t0 + 64s, true);
        CHECK(w.cache_writes == 1 && off::parse_cache(w.cache).count("version-02c37bc51a384b8f"), "confirmed downloaded offsets are saved");
    }

    // 4. an unknown version: the table is requested at once and applied
    {
        reset_offsets();
        World w;
        OffsetUpdater up(w.io());
        up.attach(kNewVersion, t0);
        CHECK(!up.known() && up.source() == "guess" && off::VE_POINTER == off::kVersions[0].set.ve_pointer,
              "an unknown version starts from the newest built-in offsets");
        CHECK(w.logged("new to this build") && w.logged(off::kDefaultSource), "the log says what is about to happen and where it asks");
        up.tick(t0 + 2s, false);
        CHECK(w.fetched_urls.size() == 1, "the table is requested on the first tick");
        up.tick(t0 + 4s, false);
        CHECK(w.fetched_urls.size() == 1, "and not requested again while the download is running");
        w.respond(make_table(kNewVersion, fresh));
        up.tick(t0 + 6s, false);
        CHECK(up.known() && up.source() == "downloaded" && off::VE_POINTER == 0x9000000 && off::GUI_TEXT == 0xe20
                  && off::GUI_IMAGE == off::kVersions[0].set.gui_image + 0x18,
              "the table for the running version is applied, with a shifted image offset");
        CHECK(w.cache.empty(), "still nothing saved");
        up.tick(t0 + 8s, true);
        CHECK(up.confirmed() && w.logged("saved to tds_offsets_cache.ini"), "found game data confirms and saves");
        const auto saved = off::parse_cache(w.cache);
        CHECK(saved.count(kNewVersion) && saved.at(kNewVersion).ve_pointer == 0x9000000, "the cache file holds the downloaded offsets");
        up.tick(t0 + 30min, true);
        CHECK(w.fetched_urls.size() == 1 && w.cache_writes == 1, "no further downloads or writes once confirmed");
    }

    // 5. the next start: the saved offsets are used without going online
    {
        reset_offsets();
        World w;
        w.cache = off::cache_with("", kNewVersion, fresh);
        OffsetUpdater up(w.io());
        up.attach(kNewVersion, t0);
        CHECK(up.known() && up.source() == "saved" && off::VE_POINTER == 0x9000000 && off::GUI_IMAGE == fresh.gui_image,
              "learned offsets are applied at once");
        up.tick(t0 + 2s, true);
        CHECK(up.confirmed() && w.fetched_urls.empty() && w.cache_writes == 0, "no download and no rewrite of the cache");
    }

    // 6. a saved version that stops working falls back to the table after the grace period
    {
        reset_offsets();
        World w;
        w.cache = off::cache_with("", kNewVersion, fresh);
        OffsetUpdater up(w.io());
        up.attach(kNewVersion, t0);
        up.tick(t0 + 30s, false);
        CHECK(w.fetched_urls.empty(), "saved offsets get the grace period too");
        up.tick(t0 + 61s, false);
        CHECK(w.fetched_urls.size() == 1, "and then the table is asked");
    }

    // 7. the table lags behind Roblox: wrong version, retry later, then it appears
    {
        reset_offsets();
        World w;
        OffsetUpdater up(w.io());
        up.attach(kNewVersion, t0);
        up.tick(t0 + 2s, false);
        w.respond(make_table(kOtherVersion, fresh));
        up.tick(t0 + 4s, false);
        CHECK(!up.known() && w.logged("table is for " + kOtherVersion) && w.logged("10 minutes"),
              "a table for another version is refused, with the reason");
        CHECK(off::VE_POINTER == off::kVersions[0].set.ve_pointer, "the guess stays in place");
        up.tick(t0 + 9min, false);
        CHECK(w.fetched_urls.size() == 1, "no retry before ten minutes have passed");
        up.tick(t0 + 15min, false);
        CHECK(w.fetched_urls.size() == 2, "a retry after ten minutes");
        w.respond(make_table(kNewVersion, fresh));
        up.tick(t0 + 15min + 2s, false);
        CHECK(up.known() && off::VE_POINTER == 0x9000000, "the table that has caught up is applied");
    }

    // 8. a network error
    {
        reset_offsets();
        World w;
        OffsetUpdater up(w.io());
        up.attach(kNewVersion, t0);
        up.tick(t0 + 2s, false);
        w.respond("", "could not connect");
        up.tick(t0 + 4s, false);
        CHECK(w.logged("could not download") && w.logged("could not connect") && !up.known(), "a failed download is reported and nothing changes");
        up.tick(t0 + 11min, false);
        CHECK(w.fetched_urls.size() == 2, "the download is retried");
    }

    // 9. an unusable table
    {
        reset_offsets();
        World w;
        OffsetUpdater up(w.io());
        up.attach(kNewVersion, t0);
        up.tick(t0 + 2s, false);
        w.respond("<html>503 Service Unavailable</html>");
        up.tick(t0 + 4s, false);
        CHECK(w.logged("cannot be used") && !up.known() && off::VE_POINTER == off::kVersions[0].set.ve_pointer,
              "a page that is not a table is refused and the guess stays");
    }

    // 10. auto_update = off
    {
        reset_offsets();
        World w;
        w.settings = "; settings\nauto_update = off\n";
        OffsetUpdater up(w.io());
        up.attach(kNewVersion, t0);
        up.tick(t0 + 5s, false);
        up.tick(t0 + 30min, false);
        CHECK(w.fetched_urls.empty() && w.logged("auto_update is off"), "nothing is downloaded with auto_update = off, and the log says why");
        reset_offsets();
        World w2;
        w2.settings = "auto_update = off\n";
        OffsetUpdater up2(w2.io());
        up2.attach("version-02c37bc51a384b8f", t0);
        up2.tick(t0 + 5min, false);
        CHECK(w2.fetched_urls.empty(), "also for a known version that never shows the game");
    }

    // 11. a custom source
    {
        reset_offsets();
        World w;
        w.settings = "source = https://example.org/offsets.hpp\n";
        OffsetUpdater up(w.io());
        up.attach(kNewVersion, t0);
        up.tick(t0 + 2s, false);
        CHECK(w.fetched_urls.size() == 1 && w.fetched_urls[0] == "https://example.org/offsets.hpp", "the configured https source is requested");
    }

    // 12. the version could not be read from the process path
    {
        reset_offsets();
        World w;
        OffsetUpdater up(w.io());
        up.attach("", t0);
        up.tick(t0 + 5s, false);
        up.tick(t0 + 5min, false);
        CHECK(!up.known() && w.fetched_urls.empty() && w.logged("could not be read"),
              "without a version nothing can be matched, so nothing is downloaded");
    }

    // 13. the cache cannot be written
    {
        reset_offsets();
        World w;
        w.cache_writable = false;
        OffsetUpdater up(w.io());
        up.attach(kNewVersion, t0);
        up.tick(t0 + 2s, false);
        w.respond(make_table(kNewVersion, fresh));
        up.tick(t0 + 4s, false);
        up.tick(t0 + 6s, true);
        CHECK(up.confirmed() && w.logged("could not be written") && off::VE_POINTER == 0x9000000,
              "a cache that cannot be written is reported and the offsets stay in use");
    }

    // 14. another attach (Roblox restarted) starts clean
    {
        reset_offsets();
        World w;
        OffsetUpdater up(w.io());
        up.attach(kNewVersion, t0);
        up.tick(t0 + 2s, false);
        w.respond(make_table(kNewVersion, fresh));
        up.tick(t0 + 4s, false);
        up.tick(t0 + 6s, true);
        up.attach(kNewVersion, t0 + 1h);
        CHECK(up.known() && up.source() == "saved" && !up.confirmed(), "a reconnect uses the saved offsets and needs a new confirmation");
        up.attach("version-02c37bc51a384b8f", t0 + 2h);
        CHECK(up.source() == "built in" && off::VE_POINTER == 0x858d208, "a reconnect to another version re-selects its offsets");
    }

    // 15. an unknown version whose table equals the guess is still accepted and remembered
    {
        reset_offsets();
        World w;
        OffsetUpdater up(w.io());
        up.attach(kNewVersion, t0);
        up.tick(t0 + 2s, false);
        w.respond(make_table(kNewVersion, off::kVersions[0].set));
        up.tick(t0 + 4s, false);
        CHECK(up.known() && w.logged("the same values as the guess"), "a table that confirms the guess makes the version known");
        up.tick(t0 + 6s, true);
        CHECK(off::parse_cache(w.cache).count(kNewVersion) == 1, "and it is saved after confirmation");
    }

    reset_offsets();
    std::printf("RESULT %d failures\n", failures);
    return failures ? 1 : 0;
}
