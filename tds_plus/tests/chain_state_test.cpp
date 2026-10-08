// language: C++17, file: chain_state_test.cpp, runtime: STL/MSVC, target: Windows 11
#include "../chain_state.hpp"
#include <cstdio>

static ChainState::TimePoint at(int milliseconds) {
    return ChainState::TimePoint{} + std::chrono::milliseconds(milliseconds);
}

int main() {
    using namespace std::chrono_literals;
    int failures = 0;
    const auto check = [&](bool condition, const char* name) {
        printf("%s %s\n", condition ? "PASS" : "FAIL", name);
        failures += !condition;
    };
    check(parse_ready_count("3") == 3 && parse_ready_count(" 2 ") == 2, "real ready counts parse");
    check(!parse_ready_count("") && !parse_ready_count("abc") && !parse_ready_count("3x")
          && !parse_ready_count("-1") && !parse_ready_count("99999999999999"), "failed reads never become zero charges");
    ChainState chain;
    chain.observe(at(0), 3, 10s);
    check(!chain.count_is_stable(3), "first sample cannot trigger a chain press");
    chain.observe(at(60), 3, 10s);
    check(chain.count_is_stable(3), "two matching samples establish ready charges");
    chain.begin_press(at(100), 3, 10s);
    check(!chain.can_fire(at(101)) && !chain.can_fire(at(900)), "press immediately blocks the whole shared group");
    check(chain.observe(at(950), 2, 10s) == ChainEvent::None && !chain.can_fire(at(950)),
          "acknowledgement later than 800 ms does not retry");
    check(chain.observe(at(1020), 2, 10s) == ChainEvent::Confirmed, "delayed charge decrease confirms one cast");
    check(!chain.can_fire(at(11019)) && chain.can_fire(at(11020)), "next cast waits a full buff after confirmation");
    chain.begin_press(at(11020), 2, 10s);
    chain.observe(at(11200), 1, 10s);
    chain.observe(at(11260), 1, 10s);
    check(!chain.can_fire(at(21259)) && chain.can_fire(at(21260)), "second Commander also holds its complete buff");
    chain.begin_press(at(21260), 1, 10s);
    chain.observe(at(21400), 0, 10s);
    check(chain.observe(at(21460), 0, 10s) == ChainEvent::Confirmed, "third Commander can confirm the last ready charge");

    ChainState unconfirmed;
    unconfirmed.observe(at(0), 3, 10s);
    unconfirmed.observe(at(60), 3, 10s);
    unconfirmed.begin_press(at(100), 3, 10s);
    unconfirmed.observe(at(900), 3, 10s);
    check(!unconfirmed.can_fire(at(1000)), "unchanged counter cannot cause an immediate second press");
    check(unconfirmed.observe(at(3100), 3, 10s) == ChainEvent::Unconfirmed,
          "missing acknowledgement enters a conservative hold");
    check(!unconfirmed.can_fire(at(13099)) && unconfirmed.can_fire(at(13100)), "missing acknowledgement still protects a full buff");
    unconfirmed.observe(at(4000), 2, 10s);
    check(unconfirmed.observe(at(4060), 2, 10s) == ChainEvent::Manual && !unconfirmed.can_fire(at(14059)),
          "very late acknowledgement extends rather than cancels the hold");

    ChainState paused;
    paused.observe(at(0), 3, 10s);
    paused.observe(at(60), 3, 10s);
    paused.observe(at(1000), 2, 10s);
    check(!paused.count_is_stable(2), "first manual decrement cannot race an automatic cast");
    check(paused.observe(at(1060), 2, 10s) == ChainEvent::Manual,
          "manual cast is adopted while automation is paused");
    check(!paused.can_fire(at(11059)) && paused.can_fire(at(11060)), "resuming does not cut a manual buff short");

    ChainState reconfigured;
    reconfigured.observe(at(0), 3, 10s);
    reconfigured.observe(at(60), 3, 10s);
    reconfigured.begin_press(at(100), 3, 10s);
    reconfigured.observe(at(200), 2, 1s);
    reconfigured.observe(at(260), 2, 1s);
    check(!reconfigured.can_fire(at(10259)) && reconfigured.can_fire(at(10260)),
          "changing settings cannot shorten the already active buff");

    ChainState invalid;
    invalid.observe(at(0), 3, 10s);
    invalid.observe(at(60), 3, 10s);
    invalid.begin_press(at(100), 3, 10s);
    invalid.observe(at(200), std::nullopt, 10s);
    check(invalid.observe(at(260), std::nullopt, 10s) == ChainEvent::None && invalid.pending,
          "empty reads do not confirm a phantom cast");
    invalid.observe(at(300), 0, 10s);
    invalid.observe(at(360), 3, 10s);
    check(invalid.observe(at(420), 3, 10s) == ChainEvent::None && invalid.pending,
          "one transient zero is not a confirmed cast");
    check(!invalid.count_is_stable(std::nullopt), "unknown charges cannot be ready");

    ChainState delayed_loop;
    delayed_loop.begin_press(at(5000), 3, 10s);
    check(!delayed_loop.can_fire(at(15000)), "pending input remains blocked after a delayed engine loop");
    delayed_loop.observe(at(18000), 3, 10s);
    check(!delayed_loop.can_fire(at(27999)), "a long memory read cannot immediately retry a cast");

    printf("RESULT %d failures\n", failures);
    return failures ? 1 : 0;
}
