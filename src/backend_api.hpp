// language: C++17, file: backend_api.hpp, runtime: Windows 11, target: tds+ UI bridge
#pragma once

#include <string>
#include <vector>

struct TdsAbilityState {
    std::string key;
    std::string name;
    std::string tower;
    double cooldown = 0;
    int mode = 0;
    int seconds = 10;
    bool live = false;
};

struct TdsSnapshot {
    bool running = false;
    bool exit_requested = false;
    std::string phase;
    int slots = 0;
    std::vector<TdsAbilityState> abilities;
    std::vector<std::string> log;
};

int tds_run_engine(bool offline = false);
TdsSnapshot tds_snapshot();
void tds_set_running(bool running);
bool tds_set_rule(const std::string& key, int mode, int seconds);
void tds_request_exit();
