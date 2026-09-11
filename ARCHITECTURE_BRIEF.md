# FLARE — Architecture Brief

Scope note: this repo is **embedded firmware plus local host tooling**, not a networked
service. Several sections of a standard service brief (RPS, sharding, multi-tenant auth,
blue/green deploy) do not map. Where a section has no analogue, it says so explicitly
rather than inventing one.

Assessed at commit `cc07a25` (2026-07-27), branch `main`.

---

## 1. What the system does, who uses it, scale

**What.** FLARE = "Filament Lane Automation and Reload Engine". RP2040 firmware for the
FYSETC ERB v2.0 board that drives a **two-lane** 3D-printer filament changer / runout
reloader. It owns, on-device:

- Two TMC2209 stepper drivers over UART, one per lane (`firmware/src/tmc2209.c`)
- Per-lane IN/OUT filament switches, optional Y-splitter switch, optional toolhead sensor
  (`firmware/src/motion.c`)
- A closed-loop "sync" controller that matches lane feed rate to the printer's extruder by
  reading a spring-trolley buffer sensor (`firmware/src/sync.c`, `sync_buf.c`,
  `sync_relay.c`, `sync_analog.c`)
- Filament cutter sequencing (`firmware/src/cutter.c`)
- Toolchange and autonomous RELOAD state machines (`firmware/src/toolchange.c`)

Two top-level modes (`RELOAD_MODE`): `0` = MMU (toolchange on host command), `1` = RELOAD
(auto-switch to standby spool on runout, 3-state WAIT_Y → APPROACH → FOLLOW).

Two buffer sensor types (`BUF_SENSOR_TYPE`): type-D = two switches (dual endstop), type-P =
analog/Hall proportional. These are genuinely different control problems and have separate
control laws — see §9.

**Who uses it.** Printer operators running Klipper (Mainsail/Fluidd). Host integration is
optional: firmware runs standalone over USB CDC and keeps printing if USB drops
(`README.md:16`).

**Scale.** UNKNOWN. No telemetry, no install count, no user registry exists in-tree. What
*is* knowable:

- Concurrency ceiling is hardwired: `NUM_LANES` = 2. Two motors, one board, one printer.
- Serial link: USB CDC, 115200 baud, line protocol `CMD:params\n`.
- Firmware control loop: cooperative, no RTOS; single `while (true)` in
  `firmware/src/main.c:569` ticking every module each pass, with a 1000 ms hardware
  watchdog (`main.c:568`).
- Telemetry stream: daemon SSE at 20 Hz (`scripts/flare_daemon.py:8`).
- Codebase: ~27.5k lines total; firmware C ~8.5k lines, host Python ~15k lines.
- 1481 commits, first 2026-03-05, most recent 2026-07-27. Single primary author.

---

## 2. Service topology

Three tiers, all on one physical printer. No network fan-out.

```
Klipper (mmu.py / mmu_sensors.py, in-process Klipper extras)
   │  gcode commands, HTTP to Moonraker
   ▼
flare_daemon.py  (systemd, Python, threaded HTTP + SSE + serial multiplexer)
   │  USB CDC 115200, line protocol, serialized behind one lock
   ▼
RP2040 firmware  (cooperative main loop, no RTOS, no threads)
   │  step/dir PWM, UART
   ▼
2× TMC2209 + switches + buffer sensor + cutter servo
```

**Firmware process model.** One process, one loop, zero threads. `firmware/src/main.c:569`
onward calls in fixed order each iteration: `watchdog_update`, `cmd_poll`,
`buffer_stabilize_tick`, `cutter_tick`, `tc_tick`, `autopreload_tick`, `lane_tick(L1)`,
`lane_tick(L2)`, `buf_sensor_tick`, `sync_tick`, `neopixel_tick`. Every tick is
non-blocking and takes `now_ms`; ordering is load-bearing (see §9).

**Sync vs async.** Firmware is fully synchronous/cooperative. Host commands are
request/response (`OK:` / `ER:`) plus an unsolicited best-effort `EV:` event channel
(`CONTEXT.md:179` — explicitly "best-effort and rate-limited", i.e. events can be dropped
and must not be treated as a reliable stream).

**Daemon process model.** `scripts/flare_daemon.py`, 1494 lines, one file. Threads:
serial reader loop, `ThreadingMixIn` HTTP server, per-SSE-client queues, a Moonraker poll
loop. Shared mutable module globals (`serial_port`, `status_cache`, `command_reply`,
`current_executing_command`) guarded by `serial_lock` / `status_lock`
(`flare_daemon.py:47-75`).

Daemon HTTP surface (`flare_daemon.py:729-850`):

| Route | Method | Purpose |
|---|---|---|
| `/status` | GET | Cached telemetry snapshot, JSON |
| `/telemetry` | GET | SSE, 20 Hz |
| `/cmd` | POST | Forward a raw board command, block ≤10 s, return `OK:`/`ER:` |
| `/config` | GET | Gate map / TTG map / spoolman flags |
| `/gatemap` | GET | Gate state |
| `/` , `/index.html` | GET | Embedded HTML/Canvas dashboard |

**Klipper tier.** `klipper/mmu.py` (1483 lines) is a **Happy-Hare compatibility mock**
(`class MMUMachineMock`, `class MMUMock` at `klipper/mmu.py:4,30`). It exists so
Mainsail/Fluidd's existing MMU UI panels light up; it registers ~25 `MMU_*` gcode commands
and holds gate state pushed in from the daemon via `SET_MMU`. Some registered commands are
deliberate no-ops (`cmd_MMU_NOOP`, `cmd_MMU_MOTORS_NOOP`) and `cmd_MMU_PAUSE` is a stub —
so the host has no real pause actuator. Safety therefore lives in firmware only, by design.

---

## 3. Data layer

There is no database in the conventional sense. Two persistence stores:

**A. Firmware settings — flash-backed struct.** `firmware/src/settings_store.c`.

- Layout: one `settings_t` struct, ~46 scalar fields, ping-ponged between two flash sectors:
  Sector A (`SETTINGS_FLASH_OFFSET_A = PICO_FLASH_SIZE_BYTES - (2 * FLASH_SECTOR_SIZE)`) and
  Sector B (`SETTINGS_FLASH_OFFSET_B = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE`).
- Buffer is fixed 512 bytes, enforced by `_Static_assert(sizeof(settings_t) <= 512)`
  (`settings_store.c`).
- Integrity & Ordering: `magic` + `version` + `seq` counter + trailing `crc32` over all preceding bytes.
- Write path: targets inactive sector (`1 - g_active_sector`), increments monotonic `g_seq++`,
  erases, programs, and verifies readback CRC before flipping active sector pointer.
  **Dual ping-pong A/B slot:** power loss mid-write leaves the prior valid sector intact,
  guaranteeing zero calibration loss during brownout.
- Row count analogue: 1 record active (ping-pong across 2 sectors).

**Migration approach:** `SETTINGS_VERSION` is `64` (`settings_store.h`).
Settings evolve non-destructively via a packed Tag-Length-Value (TLV) stream.
Unknown tags are skipped gracefully on load and pruned on re-save. Legacy v63 flat
sectors are lazily migrated on the first post-boot save without wiping operator calibration.
Dual ping-pong sectors ensure write-interruption safety across unexpected reboots.

**B. Daemon SQLite.** `scripts/flare_daemon.py:121-166`. File at `<data dir>/flare.db`.

- Two tables, both key/value with a JSON-encoded `value` column: `gate_config`, `stats`
  (`flare_daemon.py:132,136`).
- Access pattern: `SELECT value FROM {table} WHERE key=?` / `INSERT OR REPLACE`. Table name
  is f-string interpolated (`flare_daemon.py:149,164`) — callers are internal constants
  today, but it is not parameterized.
- Indexes: none beyond the implicit primary key. Not needed at this size.
- Largest table row count: bounded by gate count (2) and a fixed stat-key set. Order of
  tens of rows. Migrations: `CREATE TABLE IF NOT EXISTS` only — no schema versioning.

**C. Config generation (build-time, not runtime data).** `config.ini` →
`scripts/gen_config.py` → generated `firmware/include/tune.h` (gitignored). Firmware
consumes `CONF_*` macros. Adding a tunable is a documented **10-step** path
(`CONTEXT.md:103-118`) spanning config.ini, gen_config.py, module global, `settings_t`,
save/load/reset, TMC apply, `SET:`/`GET:` handlers, docs, version bump. This is the single
largest source of change friction in the repo.

---

## 4. External dependencies

| Dep | Where | Coupling |
|---|---|---|
| Moonraker | `flare_daemon.py:219` `http://localhost:7125` | POST `/printer/gcode/script` to inject gcode; `/server/spoolman/proxy` |
| Spoolman | `flare_daemon.py:220` `http://localhost:7912` | `/v1/spool/{id}`, `/v1/spool/{id}/use` for filament consumption |
| Klipper | `klipper/mmu.py`, `klipper/mmu_sensors.py` | loaded as Klipper extras; `flare_mmu.cfg` |
| pyserial | daemon, tuner, flare_cmd, flash tool | imported defensively with a stub fallback (`flare_daemon.py:42`) |
| Pico SDK | `firmware/CMakeLists.txt` | build-time; cross toolchain must be on PATH |

Both URLs are overridable via `--moonraker-url` / `--spoolman-url`
(`flare_daemon.py:1446-1448`).

**No queue. No cache tier. No object storage. No cloud services. No auth provider.** All
external calls are plain `urllib.request` against localhost.

---

## 5. Auth model

**Partially mitigated.**
- The daemon HTTP server binds **`127.0.0.1` by default** (loopback only). Operators explicitly opt into LAN access via `--host 0.0.0.0`.
- CORS headers (`Access-Control-Allow-Origin`) are strictly restricted to loopback and same-host origins by default, rejecting untrusted cross-origin requests (`403 Forbidden`). External origins can be explicitly whitelisted via `--cors-origins`.
- There is currently no token/session authentication for raw `POST /cmd` when running in `--host 0.0.0.0` mode. Motion commands and flash settings writes over LAN remain unauthenticated if the operator exposes the daemon to the network.

Serial link itself is unauthenticated by nature (physical USB), which is acceptable. The
LAN-exposed HTTP bridge on top of it is not.

---

## 6. Failure handling

**Firmware.**

- **Hardware watchdog:** 1000 ms, enabled at `main.c:568`, kicked every loop iteration
  (`main.c:571`). Reboot cause is detected at `main.c:519` and reported once as an event
  after 2 s uptime (`main.c:573`). This closes the "H3 no HW watchdog" gap noted in prior
  audits.
- **Fault taxonomy:** `FAULT_NONE / TIMEOUT / SENSOR / BUF / CUT / DRY_SPIN`
  (`controller_shared.h:55-60`). Faults are per-lane and latch until cleared.
- **Timeouts** are pervasive and individually tunable at runtime:
  `g_reload_y_timeout_ms`, `g_follow_timeout_ms[NUM_LANES]`, `g_cut_timeout_settle_ms`,
  `g_cut_timeout_feed_ms`, `g_tc_timeout_cut_ms`, `g_tc_timeout_th_ms`, `g_tc_timeout_y_ms`,
  `g_neutral_creep_timeout_ms` (`controller_shared.h:174-260`).
- **Travel limits, not time limits,** guard load/unload: `AUTOLOAD_MAX`, `LOAD_MAX`,
  `UNLOAD_MAX` are distance-based (`CONTEXT.md:134-138`). Names like `TC_LOAD_MS` are
  legacy protocol aliases and do *not* mean milliseconds — a real readability trap.
- **Command parser** is bounded: `CMD_POLL_BYTE_BUDGET` / `CMD_POLL_COMMAND_BUDGET` cap
  work per poll (`protocol.c:1890`) so a serial flood cannot starve the control loop.
  Overflowing lines reply `ER:OVERFLOW` and are discarded (`protocol.c:1900`).
- **Persistence is activity-gated:** `SV:`/`LD:`/`RS:` return `ER:PERSIST_BUSY` during
  motion, toolchange, cutter activity, or boot stabilization (`CONTEXT.md:140-142`). This
  is what prevents a flash erase during a step train.
- **Idempotency:** commands are not idempotent and carry no request ID. `LO:`/`UL:` etc.
  are edge-triggered against current state; a duplicate delivered command re-triggers.
  There is no dedupe layer. Acceptable over a point-to-point USB link, unsafe over the
  LAN-exposed `/cmd` bridge in §5.
- **No retry in firmware.** A failed operation faults and stops; recovery is operator- or
  host-initiated. Correct choice for a machine that can grind filament.

**Host.**

- Daemon serial: `timeout=1.0`, `exclusive=True`, reconnect loop with port rediscovery
  (`flare_daemon.py:608-695`); heartbeat `?:` poll at `:709`.
- Daemon → board command wait: `command_event.wait(timeout=10.0)`, returns HTTP **504** on
  expiry (`flare_daemon.py:942`, `:834`).
- Moonraker calls: 1.0–2.0 s timeouts, 5 s fixed backoff on unreachable
  (`flare_daemon.py:1395-1398`). No exponential backoff, no jitter, no circuit breaker.
- Live tuner: up to 5 reconnect attempts then exits non-zero
  (`flare_live_tuner.py:24,471`).
- Flash tool: 10 retries on port reappearance (`flash_flare.py:142`).

**When dependency X is down:**

| Down | Effect |
|---|---|
| USB / daemon | Firmware keeps running: sync, RELOAD, cutter all local. Documented design goal (`README.md:16`). Host loses telemetry and `/cmd`. |
| Moonraker | Daemon logs and backs off 5 s, keeps serving `/status` and `/cmd`. Spool consumption accumulates locally (`flare_daemon.py:436`). Gcode injection silently fails. |
| Spoolman | Consumption is accumulated locally instead of reported. Degrades cleanly. |
| Klipper | `mmu.py` unloaded → no MMU panels, no `SET_MMU` push. Firmware unaffected. |
| Board unplugged | `board_online: false` in the status cache; `/cmd` returns 504 after 10 s. |
| SQLite file unwritable | Not handled explicitly; exception path is broad `except` in the accessors. |

---

## 7. Observability

**Exists:**

- `EV:` event channel from firmware — 26 distinct event types, most frequent `SYNC` (15
  emit sites), `BUF_STAB` (8), `BL` (6), `RUNOUT` (5). Explicitly best-effort and
  rate-limited (`CONTEXT.md:179`), so **event counts are not a valid metric** — a prior
  audit note in memory records that motion, not event counts, is the correct signal.
- `ST:` status dump (`firmware/src/protocol_status.c`) — full state snapshot on demand.
- Daemon status cache exposed at `/status` and 20 Hz SSE at `/telemetry` — the closest
  thing to a metrics feed.
- Daemon `stats` table in SQLite for cumulative counters.
- 178 `printf`/debug sites across firmware `src/`, gated by build config.
- Offline analysis tooling: `scripts/flare_analyze.py` (1174 lines) for calibration
  analysis, `scripts/flare_sync_check.py` (1269 lines), `scripts/flare_live_tuner.py`
  (1605 lines) for in-print observation.
- `scripts/klipper_motion_tracker.py` correlates Klipper extruder motion with board state.

**Does not exist:**

- No structured logging. No log levels, no correlation IDs, no log file rotation from
  firmware.
- No metrics system — no Prometheus, no counters exported in a scrapable format.
- No tracing of any kind.
- **No alerting.** Nothing pages, emails, or notifies. A mid-print fault shows on the
  NeoPixel LED and in `EV:` output; if nobody is watching the terminal, nobody knows.
- No crash dump / core capture. A watchdog reboot reports one event and loses all context.
- No persistent firmware-side log ring buffer that survives reboot.

The offline analyzers are strong; the online observability is thin. Diagnosis is currently
"reproduce it while capturing serial", which is why `reload.log` (2 MB, untracked) exists in
the working tree.

---

## 8. Deploy

**Where it runs.** Firmware on the RP2040 board. Daemon on the printer host (Raspberry Pi
class), installed as a systemd unit: `scripts/flare_daemon.service`, `Restart=always`,
`RestartSec=5`, installed to `/etc/systemd/system` by `scripts/install_daemon.py` (Linux
only, gated at `:278`).

**Firmware deploy.** `python3 scripts/gen_config.py` → cmake/ninja → `flash_flare.py`
(BOOTSEL UF2 copy, 579 lines with retry logic). Manual, operator-driven.

**CI/CD: not implemented.** `.github/` contains only `copilot-instructions.md` — there is
**no workflow file, no GitHub Actions, no automated build on push**. Everything is
local-gate.

**The local gate** is real and reasonably thorough — `scripts/validate_regression.py`
(140 lines) runs 9 stages: gen_config → cmake configure with `-DFLARE_DEV_TUNING=ON` →
ninja build → build & run `flare_sim` host simulation → `py_compile` → ruff → `unittest
discover` → mock MMU status self-test → `git diff --check`. Plus a host simulation harness
(`tests/host/`, ~1500 lines) that links the *real* `sync*.c`/`motion.c`/`toolchange.c`
against fakes, with 36 scenario tests in `scripts/test_sync_sim.py`.

But it only runs if a human runs it. Nothing enforces it on push.

**Rollback story: not implemented.**

- No firmware version pinning, no A/B flash slots, no signed images.
- Rollback = re-flash an older `.uf2` by hand, if the operator kept one.
- Worse, rollback across a `SETTINGS_VERSION` boundary **wipes all tuning** (§3), and this
  version has been bumped 60 times. Downgrade is not a safe operation.
- Two build dirs (`build_local`, `build_clang`) and prebuilt binaries sit in the working
  tree; there is no artifact registry.

Firmware and the two host tiers (daemon, Klipper module) version independently with no
compatibility handshake beyond the daemon probing `GET:BUF_MAX_TRAVEL` at connect
(`flare_daemon.py:624`).

---

## 9. Three worst design decisions

**1. Unauthenticated command bridge via `POST /cmd`.**
`scripts/flare_daemon.py:1494` + `:815`. `POST /cmd` is a raw passthrough to a machine that
moves motors, actuates a blade (`cutter.c`), and writes flash. Default bind address is now
`127.0.0.1` (loopback only) and CORS headers are restricted to loopback/same-host. However, when
an operator runs `--host 0.0.0.0` for LAN access, there is no token auth, no command allowlist,
and no rate limiting on the LAN. The `ER:PERSIST_BUSY` interlock protects flash during motion but
does nothing against unauthorized remote callers on LAN.

**2. `SETTINGS_VERSION` bump = full settings wipe, with a 10-step manual ritual to add a
field.** `firmware/src/settings_store.c:24,537` + `CONTEXT.md:103-118`. A flat struct in a
single 512-byte flash sector with a whole-blob CRC and no field migration means every
schema change destroys operator tuning — and this has happened 60 times. The change cost is
also the repo's dominant friction: adding one tunable touches config.ini, config.ini.example,
gen_config.py, a module global, `controller_shared.h`, `settings_t`, save/load/reset, the
TMC apply path, `SET:`, `GET:`, `flare_cmd.py --dump`, and four docs — enforced by rules 7
and 8 in `AGENTS.md`. A tagged key/value or TLV format in flash would make adds
backward-compatible and delete most of the ritual. The parity problem is currently policed
by a test (`scripts/test_settings_parity.py`) rather than eliminated by the design.

**3. 161 mutable globals as the module interface.** `firmware/include/controller_shared.h`
declares 161 `extern` globals; `firmware/src/sync.c` is 1994 lines and `protocol.c` is 1926.
Every module reads and writes shared state, and correctness depends on the fixed tick order
in `main.c:596-608` — `buf_sensor_tick` must precede `sync_tick`, `tc_tick` must precede
`lane_tick`, and nothing in the code expresses those constraints. `sync_tick()` is silently
a no-op outside `TC_IDLE` (`CONTEXT.md:124-126`), a footgun documented only in prose. The
project's own bug history is dominated by exactly this class of defect: stale fault timers
accumulating while sync was off, a frozen distance-clock holding feed rate high, sign-flip
errors on `g_buf_pos`. The host simulation harness in `tests/host/` was built specifically
to catch these — a strong response to the symptom, but the underlying coupling remains.

Honorable mention: `klipper/mmu.py` is a 1483-line mock of a *different project's*
(Happy-Hare's) interface, kept alive so third-party UI panels work. It carries stub commands
that lie about capability — `cmd_MMU_PAUSE` does nothing, so a UI pause button is
non-functional.

---

## 10. What breaks first at 10x load

"10x load" needs translation — there is no request load. Three plausible axes, in order of
how soon they break:

**Axis A — 10x lanes (2 → 20).** Breaks almost immediately, in several places at once:

1. **Flash settings overflow.** `_Static_assert(sizeof(settings_t) <= 512)`
   (`settings_store.c:109`) fails at compile time. Per-lane arrays like
   `g_follow_timeout_ms[NUM_LANES]` scale linearly into a fixed 512-byte sector. This is the
   first hard stop, and it is a build error rather than a runtime failure — the only
   good news here.
2. **Main loop budget.** `main.c:602-603` unrolls `lane_tick` per lane by hand. Twenty lanes
   means 10x the per-iteration work under a 1000 ms watchdog with no measured headroom
   anywhere in the tree. Loop period is not instrumented, so the margin is UNKNOWN.
3. **Protocol namespace.** Commands and status encode lane identity positionally in a
   two-lane world; `ST:` formatting (`protocol_status.c`, 83 lines) has no room to grow.

**Axis B — 10x telemetry/clients.** Breaks second.

1. **Serial bandwidth.** 115200 baud ≈ 11.5 KB/s shared by commands, replies, and `EV:`
   events. `EV:` is already rate-limited and lossy at 2 lanes. At 10x event volume the
   channel saturates and events silently drop — and because events are already documented
   as unreliable, the failure is invisible rather than loud.
2. **Daemon serial lock.** Every `/cmd` serializes on one `serial_lock` behind a 10 s
   timeout (`flare_daemon.py:942`). `ThreadingMixIn` spawns a thread per request, so 10x
   concurrent callers means threads piling up on a single lock and a wave of 504s, not
   backpressure.
3. **SSE fanout.** One unbounded `queue` per client with a 5 s get timeout
   (`flare_daemon.py:768`). A slow or hung reader grows its queue without limit; at 20 Hz ×
   many clients this is a memory leak with no eviction policy.

**Axis C — 10x print duration / cumulative writes.** Slowest but nastiest.

- **Flash wear.** Every settings save is a full sector erase + program of the *same* sector
  (`settings_store.c:367`) — no wear leveling, no A/B rotation. RP2040 flash endurance is
  finite (typically ~100k erase cycles). Auto-tuning workflows that persist frequently walk
  toward a dead sector, and there is no wear counter to warn anyone.
- **SQLite `stats` growth** is bounded by a fixed key set (`INSERT OR REPLACE`), so it does
  not grow — but it also means historical stats are overwritten, not accumulated. There is
  no retention story because there is no history.

**What does *not* break:** the firmware control loop's structure. Cooperative
non-blocking ticks with a watchdog is the right shape for this problem, and the distance-based
(not time-based) safety limits stay correct regardless of speed or duration.

---

## Explicitly not implemented

Stated plainly, so nobody goes looking:

- No CI/CD pipeline (`.github/` has instructions only, no workflows)
- No authentication or authorization anywhere
- No settings migration — version bump discards stored settings
- No firmware rollback mechanism or version compatibility handshake
- No alerting, metrics export, or distributed tracing
- No structured logging
- No flash wear leveling or redundant settings slot
- No idempotency keys or request deduplication
- No circuit breaker on external HTTP calls (fixed 5 s backoff only)
- No DIAG/stall-detection handling — pins are defined in `config.h` but no IRQ is attached
  (`CONTEXT.md:150`)
- No host-side pause actuator — `cmd_MMU_PAUSE` in `klipper/mmu.py` is a stub
