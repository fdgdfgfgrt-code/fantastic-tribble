# tds+

External tooling for **Tower Defense Simulator** (Roblox). Reads game state straight
from process memory (no injection) and presses ability hotkeys via OS-level input.

## Tools (all in `src/`)

| tool | what it does |
|---|---|
| `tds_plus.cpp` | the main tool: auto-ability (spam / commander chain), engine core (`tds_run_engine`) |
| `simple_ui.inc` | the native dark UI window (pure Win32/GDI) — included by `tds_plus.cpp` |
| `backend_api.hpp` | engine <-> UI bridge: `TdsSnapshot`, `tds_set_running`, `tds_set_rule`, `tds_request_exit` |
| `chain_state.hpp` | commander chain state machine (confirm votes, manual-fire adoption) |
| `tds_state.cpp` | live watcher: HUD (wave / base HP / timer / players), hover panel, serialized tower cards with DPS, ability cooldowns |
| `tds_roster.cpp` | tower roster from the instance tree (owner, level-insensitive fingerprint) |
| `tds_tree.cpp` | DataModel instance-tree dumper |
| `tds_watch.exe`→`tds_watch.cpp` | GUI/state diff-watcher for reversing |
| `rbx_dump.cpp` | full-process memory dumper + pattern scanner |

## Build (MinGW-w64 or MSVC)

```sh
g++ -O2 -std=c++17 -mwindows -static src/tds_plus.cpp -o tds_plus.exe -lpsapi -lgdi32
g++ -O2 -std=c++17 -static src/tds_state.cpp -o tds_state.exe -lpsapi
g++ -O2 -std=c++17 -static src/tds_roster.cpp -o tds_roster.exe -lpsapi
g++ -O2 -std=c++17 -static src/tds_tree.cpp  -o tds_tree.exe  -lpsapi
g++ -O2 -std=c++17 -static src/tds_watch.cpp -o tds_watch.exe -lpsapi
g++ -O2 -std=c++17 -static src/rbx_dump.cpp  -o rbx_dump.exe  -lpsapi
```

## Config

- `tds_abilities.ini` — ability rules by name: `Commander/Call to Arms = chain 10`,
  `DJ Booth/Drop the Beat = spam`, `Medic/Ubercharge = off`; id table + full wiki
  reference inside.
- `tds_wiki_db.ini` — all available abilities from tds.fandom with cooldowns.
- `tds_towers.ini` — tower structure fingerprints -> names.
- `tds_ui_rules.ini` — written by the UI at runtime (not committed).

## How it reads the game

- instance tree: `VisualEngine -> FakeDataModel -> RealDataModel -> children walk`
  (offsets in `offsets/`, live-verified for `version-02c37bc51a384b8f`);
- GUI truth: `ReactGameAbilities` hotbar (hotkey, cooldown text, ready-tower count),
  `ReactGameTopGameDisplay` HUD, hover panel;
- TDS custom serializer streams: tower cards (`Name/OwnerId/Upgrade/Damage/Range/
  Cooldown/UID`) and ability records (`name/uid/cooldownEnd`) + `ServerTime`.

Input is sent with `SendInput` + real scancodes (Roblox ignores vk-only synthetic keys).
Offsets change on every Roblox version bump — re-dump with `rbx_dump.exe` when they do. The Qt app / `tds_plus.cpp`
picks its offsets by the version of the running client (`tds_plus/rbx_offsets.hpp`: one row per known
version) and, for a version it does not know, downloads the public offsets table once over HTTPS, checks
that it is for exactly that version, and keeps the offsets in `tds_offsets_cache.ini` once they have worked
(`auto_update = off` in `tds_plus/tds_offsets.ini` turns the download off; see `tds_plus/QT_UI.md`).
