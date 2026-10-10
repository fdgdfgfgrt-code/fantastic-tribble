// language: C++17, file: offsets_fetch.hpp, runtime: Windows 10/11, target: tds+ HTTPS download
// Small HTTPS GET over WinHTTP, loaded at run time so no extra library has to be linked (the build
// lines in the README stay as they are). HTTPS only; the certificate is checked by WinHTTP; a
// redirect from https to http is refused by WinHTTP's default policy; the answer is size-limited.
#pragma once

#include <windows.h>
#include <winhttp.h>

#include <string>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

namespace tds_net {

namespace detail {

template <class Fn>
inline Fn proc_as(FARPROC p) {
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(p));
}

struct Api {
    using OpenFn = HINTERNET(WINAPI*)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
    using TimeoutsFn = BOOL(WINAPI*)(HINTERNET, int, int, int, int);
    using ConnectFn = HINTERNET(WINAPI*)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
    using OpenRequestFn = HINTERNET(WINAPI*)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
    using SendFn = BOOL(WINAPI*)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
    using ReceiveFn = BOOL(WINAPI*)(HINTERNET, LPVOID);
    using QueryFn = BOOL(WINAPI*)(HINTERNET, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
    using ReadFn = BOOL(WINAPI*)(HINTERNET, LPVOID, DWORD, LPDWORD);
    using CloseFn = BOOL(WINAPI*)(HINTERNET);

    OpenFn open = nullptr;
    TimeoutsFn timeouts = nullptr;
    ConnectFn connect = nullptr;
    OpenRequestFn open_request = nullptr;
    SendFn send = nullptr;
    ReceiveFn receive = nullptr;
    QueryFn query = nullptr;
    ReadFn read = nullptr;
    CloseFn close = nullptr;

    bool load() {
        HMODULE dll = LoadLibraryW(L"winhttp.dll");
        if (!dll) return false;
        open = proc_as<OpenFn>(GetProcAddress(dll, "WinHttpOpen"));
        timeouts = proc_as<TimeoutsFn>(GetProcAddress(dll, "WinHttpSetTimeouts"));
        connect = proc_as<ConnectFn>(GetProcAddress(dll, "WinHttpConnect"));
        open_request = proc_as<OpenRequestFn>(GetProcAddress(dll, "WinHttpOpenRequest"));
        send = proc_as<SendFn>(GetProcAddress(dll, "WinHttpSendRequest"));
        receive = proc_as<ReceiveFn>(GetProcAddress(dll, "WinHttpReceiveResponse"));
        query = proc_as<QueryFn>(GetProcAddress(dll, "WinHttpQueryHeaders"));
        read = proc_as<ReadFn>(GetProcAddress(dll, "WinHttpReadData"));
        close = proc_as<CloseFn>(GetProcAddress(dll, "WinHttpCloseHandle"));
        return open && timeouts && connect && open_request && send && receive && query && read && close;
    }
};

// closes a WinHTTP handle when it goes out of scope
struct Handle {
    const Api& api;
    HINTERNET h;
    Handle(const Api& a, HINTERNET handle) : api(a), h(handle) {}
    ~Handle() {
        if (h) api.close(h);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

inline std::string error_text(const char* step, DWORD code) {
    const char* meaning = code == 12007 ? " (the host name was not found)"
                        : code == 12029 ? " (could not connect)"
                        : code == 12002 ? " (timed out)"
                        : code == 12175 ? " (the TLS certificate was not accepted)"
                        : code == 12157 ? " (TLS error)"
                        : "";
    return std::string(step) + " failed: WinHTTP error " + std::to_string(code) + meaning;
}

inline std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

}  // namespace detail

// GET `url` (must be https://host/path). true: `body` holds the answer. false: `error` says why.
inline bool https_get(const std::string& url, std::string& body, std::string& error,
                      size_t max_bytes = 1u << 20, int timeout_ms = 6000) {
    body.clear();
    const std::string scheme = "https://";
    if (url.size() <= scheme.size() || url.compare(0, scheme.size(), scheme) != 0) {
        error = "only https:// addresses are allowed";
        return false;
    }
    const size_t slash = url.find('/', scheme.size());
    const std::string host = url.substr(scheme.size(), slash == std::string::npos ? slash : slash - scheme.size());
    const std::string path = slash == std::string::npos ? "/" : url.substr(slash);
    if (host.empty() || host.find_first_of(":@ \t") != std::string::npos) {
        error = "the address has no usable host name";
        return false;
    }

    static detail::Api api;
    static const bool api_ok = api.load();
    if (!api_ok) {
        error = "winhttp.dll is not available";
        return false;
    }
    detail::Handle session(api, api.open(L"tds+ offsets", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) {
        error = detail::error_text("opening the session", GetLastError());
        return false;
    }
    api.timeouts(session.h, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    detail::Handle connection(api, api.connect(session.h, detail::widen(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.h) {
        error = detail::error_text("connecting", GetLastError());
        return false;
    }
    detail::Handle request(api, api.open_request(connection.h, L"GET", detail::widen(path).c_str(), nullptr,
                                                 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.h) {
        error = detail::error_text("creating the request", GetLastError());
        return false;
    }
    if (!api.send(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        error = detail::error_text("sending the request", GetLastError());
        return false;
    }
    if (!api.receive(request.h, nullptr)) {
        error = detail::error_text("waiting for the answer", GetLastError());
        return false;
    }
    DWORD status = 0, status_size = sizeof(status);
    if (!api.query(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                   &status, &status_size, WINHTTP_NO_HEADER_INDEX)) {
        error = detail::error_text("reading the status", GetLastError());
        return false;
    }
    if (status != 200) {
        error = "the server answered HTTP " + std::to_string(status);
        return false;
    }
    char chunk[8192];
    for (;;) {
        DWORD got = 0;
        if (!api.read(request.h, chunk, sizeof(chunk), &got)) {
            error = detail::error_text("reading the answer", GetLastError());
            return false;
        }
        if (got == 0) break;
        if (body.size() + got > max_bytes) {
            error = "the answer is larger than " + std::to_string(max_bytes) + " bytes";
            body.clear();
            return false;
        }
        body.append(chunk, got);
    }
    return true;
}

}  // namespace tds_net
