// language: C++17, file: chain_state.hpp, runtime: STL/MSVC, target: Windows 11
#pragma once
#include <algorithm>
#include <charconv>
#include <chrono>
#include <optional>
#include <string_view>

inline std::optional<int> parse_ready_count(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return std::nullopt;
    text.remove_prefix(first);
    text = text.substr(0, text.find_last_not_of(" \t\r\n") + 1);
    int count = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), count);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || count < 0)
        return std::nullopt;
    return count;
}

enum class ChainEvent { None, Confirmed, Manual, Unconfirmed };

struct ChainState {
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    TimePoint window_end{}, confirm_until{};
    std::chrono::seconds active_duration{0};
    bool pending = false;
    int pre_press_count = -1, stable_count = -1, candidate_count = -1, candidate_votes = 0;

    ChainEvent observe(TimePoint now, std::optional<int> count, std::chrono::seconds duration) {
        ChainEvent event = ChainEvent::None;
        if (count) {
            if (candidate_count == *count) candidate_votes = std::min(candidate_votes + 1, 2);
            else { candidate_count = *count; candidate_votes = 1; }
            if (candidate_votes >= 2) {
                const int previous = stable_count;
                stable_count = *count;
                if (pending && pre_press_count >= 0 && stable_count < pre_press_count) {
                    pending = false;
                    window_end = std::max(window_end, now + active_duration);
                    event = ChainEvent::Confirmed;
                } else if (!pending && previous >= 0 && stable_count < previous) {
                    window_end = std::max(window_end, now + duration);
                    event = ChainEvent::Manual;
                }
            }
        } else {
            candidate_votes = 0;
        }
        if (pending && now >= confirm_until) {
            pending = false;
            // A delayed acknowledgement cannot prove the press failed. Keep a full buff window.
            window_end = std::max(window_end, now + active_duration);
            event = ChainEvent::Unconfirmed;
        }
        return event;
    }

    bool count_is_stable(std::optional<int> count) const {
        return count && candidate_votes >= 2 && candidate_count == *count && stable_count == *count;
    }
    bool can_fire(TimePoint now) const { return !pending && now >= window_end; }
    void begin_press(TimePoint now, std::optional<int> count, std::chrono::seconds duration) {
        active_duration = duration;
        window_end = std::max(window_end, now + duration);
        confirm_until = now + std::chrono::seconds(3);
        pending = true;
        pre_press_count = count.value_or(-1);
        candidate_votes = 0;
    }
};
