// ---- web UI: local HTTP server + animated dark page, no console window ----
struct UiEntry {
    std::string name;   // display name
    int mode = 0;       // 0 = spam, 1 = chain, 2 = off
    int seconds = 10;
};

struct UiShared {
    std::mutex mtx;
    bool running = false;
    std::vector<UiEntry> entries;
    std::unordered_map<std::string, double> fired_at;  // canon name -> seconds ago marker
    std::string phase = "boot";                        // lobby | match | ended
    int slots = 0;
    std::vector<std::string> log;                      // ring buffer for the page
};

static UiShared g_ui;
static volatile bool g_ui_dirty = false;   // UI wrote tds_ui_rules.ini -> reload config
static std::chrono::steady_clock::time_point g_t0;

// all log output goes here: file + ring buffer for the web page
static void logf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%s\n", buf);   // stdout is redirected to tds_plus.log
    std::lock_guard<std::mutex> lock(g_ui.mtx);
    g_ui.log.push_back(buf);
    if (g_ui.log.size() > 40) g_ui.log.erase(g_ui.log.begin());
}

static void ui_write_rules() {
    std::ofstream out("tds_ui_rules.ini", std::ios::trunc);
    out << "; written by tds+ ui — overlaid on tds_abilities.ini\n";
    std::lock_guard<std::mutex> lock(g_ui.mtx);
    std::unordered_map<std::string, bool> seen;
    for (auto& e : g_ui.entries) {
        std::string ck = canon_key(e.name);
        if (seen[ck]) continue;
        seen[ck] = true;
        const char* mode = e.mode == 0 ? "spam" : e.mode == 1 ? "chain" : "off";
        out << e.name << " = " << mode;
        if (e.mode == 1) out << " " << e.seconds;
        out << "\n";
    }
    g_ui_dirty = true;
}

static std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if (c >= 0x20) o += c;
    }
    return o;
}

static const char INDEX_HTML[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><title>tds+</title>
<meta http-equiv="refresh" content="0; url=/">
<style>
:root { --bg:#121216; --panel:#1b1b22; --pill:#26262e; --text:#e6e6ea; --dim:#8b8b96;
        --spam:#4fbf7a; --chain:#e6b450; --off:#d05a5a; }
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--text);
       font:14px "Segoe UI",system-ui,sans-serif; display:flex; justify-content:center; }
#app { width:460px; padding:18px 0 40px; }
header { display:flex; align-items:center; justify-content:space-between; margin-bottom:16px; }
h1 { font-size:22px; margin:0; letter-spacing:.5px; }
h1 b { color:var(--spam); }
#phase { color:var(--dim); font-size:12px; margin-top:2px; }
#toggle { position:relative; width:64px; height:32px; background:var(--pill); border:none;
          border-radius:16px; cursor:pointer; transition:background .25s; }
#toggle .knob { position:absolute; top:4px; left:4px; width:24px; height:24px; border-radius:50%;
          background:#fff; transition:left .25s cubic-bezier(.6,-.2,.3,1.4); }
#toggle.on { background:var(--spam); }
#toggle.on .knob { left:36px; }
.row { display:flex; align-items:center; justify-content:space-between; background:var(--panel);
       padding:10px 14px; border-radius:10px; margin-bottom:8px;
       animation:drop .25s ease both; transition:box-shadow .2s; }
@keyframes drop { from { opacity:0; transform:translateY(-6px); } to { opacity:1; transform:none; } }
.row.fired { box-shadow:0 0 0 1px var(--spam), 0 0 18px -4px var(--spam); }
.name { flex:1; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.meta { color:var(--dim); font-size:11px; margin-top:2px; min-height:13px; }
.pill { border:none; border-radius:14px; padding:6px 14px; min-width:86px; cursor:pointer;
        font-weight:600; color:#14141a; transition:background .2s, transform .1s; }
.pill:active { transform:scale(.94); }
.pill.m0 { background:var(--spam); } .pill.m1 { background:var(--chain); } .pill.m2 { background:var(--off); }
#log { margin-top:18px; background:var(--panel); border-radius:10px; padding:10px 14px;
       color:var(--dim); font:11px/1.6 Consolas,monospace; max-height:180px; overflow-y:auto; }
#empty { color:var(--dim); text-align:center; padding:30px 0; }
</style></head><body><div id="app">
<header><div><h1>tds<b>+</b></h1><div id="phase"></div></div>
<button id="toggle"><div class="knob"></div></button></header>
<div id="rows"></div><div id="empty" hidden>no abilities — start a match</div>
<div id="log"></div>
<script>
const MODES = ["spam","chain","off"], CHAIN_SECS = [5,10,15,20,30,45,60];
let state = null;
async function api(p){ await fetch(p,{method:"POST"}); }
toggle.onclick = async () => { await api("/api/toggle"); refresh(); };
async function refresh(){
  const r = await fetch("/api/state"); state = await r.json();
  toggle.classList.toggle("on", !!state.running);
  phase.textContent = state.phase + (state.slots ? " · slots "+state.slots : "");
  rows.innerHTML = "";
  empty.hidden = state.entries.length > 0;
  for (const e of state.entries){
    const row = document.createElement("div");
    row.className = "row" + (e.fired < 3 ? " fired" : "");
    const left = document.createElement("div");
    left.innerHTML = `<div class="name"></div><div class="meta"></div>`;
    left.querySelector(".name").textContent = e.name;
    left.querySelector(".meta").textContent =
      e.fired >= 0 ? "fired " + e.fired.toFixed(0) + "s ago" : "";
    const pill = document.createElement("button");
    pill.className = "pill m" + e.mode;
    pill.textContent = e.mode === 1 ? `chain ${e.sec}s` : MODES[e.mode];
    pill.onclick = async () => { await api(`/api/mode?name=${encodeURIComponent(e.name)}`); refresh(); };
    pill.oncontextmenu = async (ev) => { ev.preventDefault();
      if (e.mode === 1) await api(`/api/sec?name=${encodeURIComponent(e.name)}`); refresh(); };
    row.append(left, pill); rows.append(row);
  }
  log.textContent = state.log.join("\n");
  log.scrollTop = log.scrollHeight;
}
refresh(); setInterval(refresh, 1000);
</script></div></body></html>)HTML";

static void http_send(SOCKET c, const char* type, const std::string& body) {
    char head[256];
    int n = snprintf(head, sizeof(head),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
        type, body.size());
    send(c, head, n, 0);
    send(c, body.data(), (int)body.size(), 0);
}

static DWORD WINAPI ui_thread(LPVOID) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET ls = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7F000001);   // 127.0.0.1 only
    addr.sin_port = htons(7321);
    if (bind(ls, (sockaddr*)&addr, sizeof(addr)) != 0) return 1;
    listen(ls, 8);
    while (true) {
        SOCKET c = accept(ls, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        char req[4096]{};
        int got = recv(c, req, sizeof(req) - 1, 0);
        if (got <= 0) { closesocket(c); continue; }
        std::string r(req, got);
        std::string target;
        size_t sp1 = r.find(' '), sp2 = r.find(' ', sp1 + 1);
        if (sp1 != std::string::npos) target = r.substr(sp1 + 1, sp2 - sp1 - 1);

        if (target == "/" || target == "/index.html") {
            http_send(c, "text/html; charset=utf-8", INDEX_HTML);
        } else if (target.rfind("/api/state", 0) == 0) {
            std::lock_guard<std::mutex> lock(g_ui.mtx);
            double now_s = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - g_t0).count();
            std::string j = "{\"running\":" + std::string(g_ui.running ? "true" : "false") +
                ",\"phase\":\"" + g_ui.phase + "\",\"slots\":" + std::to_string(g_ui.slots) +
                ",\"entries\":[";
            for (size_t i = 0; i < g_ui.entries.size(); i++) {
                auto& e = g_ui.entries[i];
                double fired = -1;
                auto it = g_ui.fired_at.find(canon_key(e.name));
                if (it != g_ui.fired_at.end()) fired = now_s - it->second;
                if (i) j += ",";
                j += "{\"name\":\"" + json_escape(e.name) + "\",\"mode\":" + std::to_string(e.mode) +
                     ",\"sec\":" + std::to_string(e.seconds) +
                     ",\"fired\":" + std::to_string(fired) + "}";
            }
            j += "],\"log\":[";
            for (size_t i = 0; i < g_ui.log.size(); i++) {
                if (i) j += ",";
                j += "\"" + json_escape(g_ui.log[i]) + "\"";
            }
            j += "]}";
            http_send(c, "application/json", j);
        } else if (target.rfind("/api/toggle", 0) == 0) {
            { std::lock_guard<std::mutex> lock(g_ui.mtx); g_ui.running = !g_ui.running; }
            http_send(c, "application/json", "{}");
        } else if (target.rfind("/api/mode?", 0) == 0 || target.rfind("/api/sec?", 0) == 0) {
            auto qm = target.find("name=");
            if (qm != std::string::npos) {
                std::string name = target.substr(qm + 5);
                auto amp = name.find('&');
                if (amp != std::string::npos) name = name.substr(0, amp);
                // url-decode (spaces etc.)
                std::string dec;
                for (size_t i = 0; i < name.size(); i++) {
                    if (name[i] == '%' && i + 2 < name.size()) {
                        dec += (char)strtoul(name.substr(i + 1, 2).c_str(), nullptr, 16);
                        i += 2;
                    } else if (name[i] == '+') dec += ' ';
                    else dec += name[i];
                }
                bool changed = false;
                {
                    std::lock_guard<std::mutex> lock(g_ui.mtx);
                    std::string ck = canon_key(dec);
                    for (auto& e : g_ui.entries) {
                        if (canon_key(e.name) != ck) continue;
                        if (target.rfind("/api/mode?", 0) == 0) {
                            e.mode = (e.mode + 1) % 3;
                        } else {
                            static const int opts[] = {5, 10, 15, 20, 30, 45, 60};
                            int k = 0;
                            while (k < 7 && opts[k] != e.seconds) k++;
                            e.seconds = opts[(k + 1) % 7];
                        }
                        changed = true;
                    }
                }
                if (changed) ui_write_rules();
            }
            http_send(c, "application/json", "{}");
        } else {
            http_send(c, "text/plain", "404");
        }
        closesocket(c);
    }
    return 0;
}
