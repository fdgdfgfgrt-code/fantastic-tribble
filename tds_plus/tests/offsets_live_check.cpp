// language: C++17, file: offsets_live_check.cpp, runtime: Windows, target: tds+ offsets download (CI diagnostic)
// Runs the real downloader (WinHTTP, HTTPS) against the real default source and parses the answer with
// the engine's own parser. Needs the network and a third-party server, so it is a diagnostic step of
// the CI workflow and not a ctest test.
#include <cstdio>
#include <string>

#include "../offsets_fetch.hpp"
#include "../rbx_offsets.hpp"

int main() {
    int problems = 0;
    std::string body, error;

    // refusals that need no network
    const char* refused[] = {"http://offsets.imtheo.lol/Offsets.hpp", "ftp://example.org/x", "https://", "https://user@host/x",
                             "https://host:8443/x"};
    for (const char* url : refused) {
        const bool ok = tds_net::https_get(url, body, error);
        std::printf("%s refuse %s (%s)\n", ok ? "FAIL" : "PASS", url, error.c_str());
        problems += ok;
    }

    std::printf("source: %s\n", off::kDefaultSource);
    if (!tds_net::https_get(off::kDefaultSource, body, error)) {
        std::printf("FAIL download: %s\n", error.c_str());
        return 1;
    }
    std::printf("PASS downloaded %zu bytes\n", body.size());

    const off::Table table = off::parse_table(body);
    std::printf("table version: %s, entries: %zu\n", table.version.c_str(), table.values.size());
    off::Set set{};
    std::string why;
    const bool usable = off::table_for_version(body, table.version, off::kVersions[0].set, set, why);
    std::printf("%s the table is usable for its own version%s%s\n", usable ? "PASS" : "FAIL", usable ? "" : ": ", why.c_str());
    problems += !usable;
    if (usable) {
        for (const off::Field& f : off::kFields)
            std::printf("  %-20s %-10s built in %-10s%s\n", f.name, off::detail::hex(set.*(f.member)).c_str(),
                        off::detail::hex(off::kVersions[0].set.*(f.member)).c_str(),
                        set.*(f.member) == off::kVersions[0].set.*(f.member) ? "" : "  <- differs");
    }
    const bool other = off::table_for_version(body, "version-0000000000000000", off::kVersions[0].set, set, why);
    std::printf("%s the table is refused for another version (%s)\n", other ? "FAIL" : "PASS", why.c_str());
    problems += other;
    std::printf("%d problem(s)\n", problems);
    return problems ? 1 : 0;
}
