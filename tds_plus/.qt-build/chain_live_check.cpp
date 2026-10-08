// language: C++17, file: chain_live_check.cpp, runtime: MSVC, target: Windows 11
#define TDS_PLUS_QT
#define NOMINMAX
#define TDS_TEST_ALLOWED_ABILITY_ID "4594880289"
#include "../tds_plus.cpp"
#include <thread>
#include <set>

int main(int argc, char** argv) {
    using namespace std::chrono_literals;
    if (argc != 2 || !SetCurrentDirectoryA(argv[1])) return 2;
    std::ofstream report("chain_live_report.txt");
    std::thread engine([] { tds_run_engine(); });
    bool attached = false;
    for (int attempt = 0; attempt < 200; ++attempt) {
        const auto state = tds_snapshot();
        if (state.phase == "in match") {
            for (const auto& ability : state.abilities)
                if (ability.key == "call to arms" && ability.live) attached = true;
        }
        if (attached) break;
        std::this_thread::sleep_for(100ms);
    }
    report << (attached ? "PASS" : "FAIL") << " sandbox Commander detected" << std::endl;
    if (!attached) { tds_request_exit(); engine.join(); return 3; }
    report << "Waiting for any existing buff to finish, engine paused" << std::endl;
    std::this_thread::sleep_for(11s);
    tds_set_running(true);
    report << "Running exactly three Commander chain presses" << std::endl;
    std::set<double> fired_times;
    const auto deadline = std::chrono::steady_clock::now() + 45s;
    while (std::chrono::steady_clock::now() < deadline && fired_times.size() < 3) {
        const auto state = tds_snapshot();
        for (const auto& line : state.log) {
            if (line.rfind("fired ability [F] Commander/Call to Arms", 0) != 0) continue;
            const auto marker = line.find("| t=");
            if (marker == std::string::npos) continue;
            const double time = atof(line.c_str() + marker + 4);
            if (fired_times.insert(time).second) report << "CAST " << time << std::endl;
        }
        std::this_thread::sleep_for(25ms);
    }
    tds_set_running(false);
    report << "Paused; waiting for the last buff to finish" << std::endl;
    std::this_thread::sleep_for(11s);
    tds_request_exit();
    engine.join();
    bool intervals_ok = fired_times.size() == 3;
    double previous = -1;
    for (const auto time : fired_times) {
        if (previous >= 0) {
            report << "INTERVAL " << time - previous << std::endl;
            intervals_ok = intervals_ok && time - previous >= 10;
        }
        previous = time;
    }
    std::ifstream log("tds_plus.log");
    int confirmed = 0;
    bool retried = false;
    for (std::string line; std::getline(log, line);) {
        if (line.rfind("chain confirmed Commander/Call to Arms", 0) == 0) ++confirmed;
        if (line.find("retrying") != std::string::npos) retried = true;
    }
    report << (intervals_ok ? "PASS" : "FAIL") << " three casts, each after a complete 10-second buff" << std::endl;
    report << (confirmed == 3 ? "PASS" : "FAIL") << " three casts acknowledged by game, confirmed=" << confirmed << std::endl;
    report << (!retried ? "PASS" : "FAIL") << " no immediate retries" << std::endl;
    const auto exit_code = intervals_ok && confirmed == 3 && !retried ? 0 : 1;
    report << "RESULT " << exit_code << std::endl;
    return exit_code;
}
