// language: C++17, file: offset_updater.hpp, runtime: any (no platform headers), target: tds+ engine
// Decides which Roblox offsets the engine uses and learns them for client versions it has no row for.
//
//   attach()  once per connection to Roblox: picks the offsets of the running client version
//               built in (rbx_offsets.hpp) -> learned earlier (tds_offsets_cache.ini) -> unknown
//   tick()    regularly: polls a running download, confirms the offsets, starts a download when needed
//
// A download is started
//   - at once, for a version that is neither built in nor learned (the guess is probably wrong);
//   - after kConfirmWait without the game data being found, for a known version (its row may be wrong);
// at most once per kRetryAfter, only with auto_update on (tds_offsets.ini), and only for a version
// that could be read from the process path. The downloaded table must be for exactly that version
// and every offset in it must be plausible (rbx_offsets.hpp). The offsets are only written to the
// cache after the engine found the game data with them: offsets that do not work are never kept.
//
// All side effects go through Io, so the policy is testable without Windows or a network.
#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "rbx_offsets.hpp"

class OffsetUpdater {
public:
    using Clock = std::chrono::steady_clock;

    struct Io {
        std::function<std::string()> load_settings;               // tds_offsets.ini, "" when missing
        std::function<std::string()> load_cache;                  // tds_offsets_cache.ini, "" when missing
        std::function<bool(const std::string&)> save_cache;       // replaces tds_offsets_cache.ini
        std::function<void(const std::string&)> log;
        std::function<void(const std::string&)> start_fetch;      // begin an HTTPS GET of the url
        std::function<bool(std::string&, std::string&)> poll_fetch;  // finished? body, error ("" = success)
        std::function<void()> applied;                            // the offsets in use changed
    };

    static constexpr std::chrono::seconds kConfirmWait{60};
    static constexpr std::chrono::minutes kRetryAfter{10};

    explicit OffsetUpdater(Io io) : io_(std::move(io)) {}

    void attach(const std::string& version, Clock::time_point now) {
        version_ = version;
        attached_ = now;
        next_try_ = now;
        confirmed_ = false;
        fetching_ = false;
        settled_ = false;
        unsaved_.reset();
        settings_ = off::parse_settings(io_.load_settings ? io_.load_settings() : std::string());
        learned_ = off::parse_cache(io_.load_cache ? io_.load_cache() : std::string());

        const std::string who = "Roblox client " + (version.empty() ? std::string("(version unknown)") : version);
        if (const off::Set* built_in = off::known_set(version)) {
            use(*built_in);
            known_ = true;
            source_ = "built in";
            say(who + ": offsets built in");
        } else if (const auto saved = learned_.find(version); saved != learned_.end()) {
            use(saved->second);
            known_ = true;
            source_ = "saved";
            say(who + ": offsets learned earlier (tds_offsets_cache.ini)");
        } else {
            use(off::kVersions[0].set);
            known_ = false;
            source_ = "guess";
            if (version.empty())
                say(who + ": the version could not be read from the process path; using the newest offsets, "
                          "which may be wrong");
            else if (settings_.auto_update)
                say(who + " is new to this build: looking its offsets up online (" + settings_.source +
                    "). auto_update = off in tds_offsets.ini turns this off");
            else
                say(who + " is new to this build and auto_update is off in tds_offsets.ini: the offsets "
                          "are probably wrong");
        }
    }

    // data_found: the engine found the game data (Players) with the offsets in use
    void tick(Clock::time_point now, bool data_found) {
        if (fetching_) {
            std::string body, error;
            if (io_.poll_fetch && io_.poll_fetch(body, error)) {
                fetching_ = false;
                next_try_ = now + kRetryAfter;
                finish(body, error);
            }
        }
        if (data_found && !confirmed_) {
            confirmed_ = true;
            say("offsets confirmed (" + source_ + "): the game data was found");
            if (unsaved_) {
                save(*unsaved_);
                unsaved_.reset();
            }
        }
        if (confirmed_ || fetching_ || settled_ || !settings_.auto_update || version_.empty()) return;
        if (known_ && now - attached_ < kConfirmWait) return;
        if (now < next_try_) return;
        fetching_ = true;
        say("looking up the offsets for " + version_ + " online");
        if (io_.start_fetch) io_.start_fetch(settings_.source);
    }

    bool known() const { return known_; }          // the offsets were chosen for this version, not guessed
    bool confirmed() const { return confirmed_; }  // the game data was found with them
    const std::string& source() const { return source_; }

private:
    void say(const std::string& message) const {
        if (io_.log) io_.log(message);
    }

    void use(const off::Set& set) {
        off::apply(set);
        if (io_.applied) io_.applied();
    }

    void finish(const std::string& body, const std::string& error) {
        const std::string retry = "; trying again in 10 minutes";
        if (!error.empty()) {
            say("could not download the offsets table: " + error + retry);
            return;
        }
        off::Set candidate{};
        std::string why;
        if (!off::table_for_version(body, version_, off::kVersions[0].set, candidate, why)) {
            say("the downloaded offsets table cannot be used: " + why + retry);
            return;
        }
        if (known_ && off::same_ignoring_image(candidate, off::current)) {
            say("the downloaded offsets are the ones already in use; nothing to change");
            settled_ = true;
            return;
        }
        const off::Set before = off::current;
        use(candidate);
        known_ = true;
        source_ = "downloaded";
        unsaved_ = candidate;
        say("offsets for " + version_ + " taken from the public table" + describe(before, candidate));
    }

    static std::string describe(const off::Set& before, const off::Set& after) {
        std::string changed;
        for (const off::Field& f : off::kFields) {
            if (before.*(f.member) == after.*(f.member)) continue;
            changed += (changed.empty() ? ": " : ", ") + std::string(f.name) + " " +
                       off::detail::hex(before.*(f.member)) + " -> " + off::detail::hex(after.*(f.member)) +
                       (f.dump_name ? "" : " (inferred)");
        }
        return changed.empty() ? ": the same values as the guess" : changed;
    }

    void save(const off::Set& set) {
        const std::string text = off::cache_with(io_.load_cache ? io_.load_cache() : std::string(), version_, set);
        if (io_.save_cache && io_.save_cache(text))
            say("offsets for " + version_ + " saved to tds_offsets_cache.ini");
        else
            say("the offsets for " + version_ + " work, but tds_offsets_cache.ini could not be written");
    }

    Io io_;
    off::Settings settings_;
    std::map<std::string, off::Set> learned_;
    std::string version_;
    std::string source_ = "guess";
    Clock::time_point attached_{};
    Clock::time_point next_try_{};
    bool known_ = false;
    bool confirmed_ = false;
    bool fetching_ = false;
    bool settled_ = false;  // the table said nothing new for a known version: stop asking this attach
    std::optional<off::Set> unsaved_;
};
