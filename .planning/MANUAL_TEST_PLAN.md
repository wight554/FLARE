# FLARE — Open Hardware Validation Plan

Every `HW:` item still unchecked across the roadmap, in one place, grouped by
what the rig has to be doing. Work a group top to bottom; each group's setup
is shared by its items. Pairs with `scripts/verify_hw_open_items.py` (fires
triggers, classifies events from the log — no eyeballing a UI for pass/fail).

**Convention:** every item heading carries a box. `[ ]` open, `[x]` done —
tick it here *and* in the owning plan/sheet (last section) when it passes,
with the evidence timestamp. Nothing is ticked without real rig results.

**Rig on hand is type-P.** Items that only exist on type-D are parked at the
end, not deleted.

Last consolidated: 2026-09-14. Source phases: 1, 11, 12, 13, 14, 15, 16.
Last rig session: 2026-09-14 (see the session log below the closed list).

---

## Already closed — do not re-run

- [x] **#1** `CAL` during cut → `ER:PERSIST_BUSY` — 2026-09-12, `fire 1-cal-busy`, PASS
- [x] **#2** `CP` during cut → `ER:BUSY:CUTTER` — 2026-09-12, `fire 2-cp-busy`, PASS
- [x] **#3** `TC:` during toolchange → `ER:BUSY:TC` — 2026-09-12, `fire 3-tc-busy`, PASS
- [x] **#4** `EV:BL:TIMEOUT` fires once, no storm — 2026-09-12, `watch`, two clean singles, PASS
- [x] **#8** Toolchange (default `TC_TS_PARK_MM`) → no `FAULT:MOVE_COMPRESSION`, `SYNC:AUTO_START` after `TC:DONE` — 2026-09-12, `watch`, two organic toolchanges, `12-SPEC.md`
- [x] **#9** Cutter abort mid-stroke → `EV:CUT:ERROR:ABORTED`, blade back at block — 2026-09-12, `fire 9-cutter-abort`, `12-SPEC.md`
- [x] **#13** Phase 13 snap-to-max baseline capture — 2026-09-12 2 h print, `13-…/baseline-capture.md`
- [x] **#14** FlowGuard `level` trend + UI meter — 2026-09-12, 975 pushes, `14-VERIFICATION.md` #1
- [x] **#15 (7/9)** No-op command stubs register — 2026-09-12, `fire 15-command-stubs`, 7/7 PASS (the other 2 are **A4**)
- [x] Type-P BL rail-relative break (`51bdca8`) — 2026-09-12, 4/4 swaps clean
- [x] **A4** `MMU_PRINT_START`/`MMU_PRINT_END` — 2026-09-14, two print starts + one print end ran the real macros organically, no gcode-queue hang (closes old #15 at 9/9)
- [x] **C2b** `RL:` no-op with toolhead filament confirmed — 2026-09-14, `OK` + single `EV:RELOAD:LOADED:1`, no lane task, buffer unchanged, no `FOLLOW_JAM`
- [x] **E5 (counting half)** `swaps`/`cutter_cuts` increment per swap/cut and survive daemon restart — 2026-09-14 after `41481c4`: 10→20 swaps, 4→14 cuts across a print (LIMIT/PAUSE half still open, see E5)
- [x] Purge-start false `FAULT_HOLD` (found this session, not on the list) — `4fa569a` + `a5c4fc9`, 18/18 purges clean on the rig (8 bench, 10 in-print) after 2/2 failed on the previous firmware
- [x] `cutter_cuts` never counted / `MMU_STATS` wrong port (found this session) — `41481c4`, counting verified on the rig; `MMU_STATS COUNTER=… LIMIT=` round-trip not yet re-tried

## Session log — 2026-09-14 (extruder hot, 6 bench swaps, 1 cancelled print, 1 full print)

Firmware flashed twice during the session; the rig ended on **`a5c4fc9`**,
daemon + `mmu.py` on **`41481c4`**. Event log and a `BP`/`SM` position CSV
captured from the dev machine for the whole session (not committed).

**Found and fixed (three commits, all CI green):**
- `4fa569a` — extreme relaxation re-seeded the tension extreme to a
  compression-side reading after a purge-start relief overshoot →
  `RELIEF_ON` at +0.44 on the compression side.
- `a5c4fc9` — `BUF_TENSION` means "below goal", not "starving"; the 13-03
  probe and the 13-02 mm trip were gated on it alone, so the probe ran on
  every regulated buffer and read a *falling* buffer (purge from idle,
  estimator-bounded relief lagging the extruder) as `NO_CONSUMER` →
  `FAULT_HOLD` 3 s into every purge. Now: probe opens only ≥ 0.15 below the
  tension-zone edge, distance from window open, verdict = movement in either
  direction ≥ 0.05; trip counts feed only while pinned or falling. Sim
  scenarios `sem_psf_relief_purge_overshoot`, `sem_psf_probe_purge_from_idle`.
- `41481c4` — daemon counter hooks compared against `EV:CUT:DONE` but the
  parser strips `EV:`; `mmu.py` posted `MMU_STATS` to port 4111 (daemon is
  8088); the unit test fed the impossible string and masked both.

**Informal print data (Box_klein_PYD_PETG_1h7m — same model as the 09-12
baseline, different slice, so NOT the D-30 A/B; 74 min, 10 swaps):**
`verify_phase13_hw_log.py` on the print window: item 1 relief-pause rate
**0.80/60 s PASS** (baseline: one every 2.4–3.4 s); item 2
`TENSION_RISK_HIGH` **21 vs <13 FAIL** (warn-only, no action taken on any);
item 3 false stops **0/0/0/0 PASS**; item 4 rail hits 2107 **FAIL** — 997/1005 are the +1.0 compression rail (low-flow limit cycle), only 8 tension; tension side essentially never hit. Both fails = the open compression-side hunting question, not the starvation domain today's fixes target.
`RELIEF_ON` 509 (~9.5/min of sync) and 76 `AUTO_START`/59 `RELIEF_PAUSE`
cycles say the hunting question (`typep-feed-hunting`) is still open.

**Observed, not acted on (belong to Group F / tuning):**
- Rail guard (`CONF_PSF_WALL_SAT_MS`, absolute rail) fault-held 3× on fast
  manual extrudes from a full buffer before the print (~40 mm/s from idle
  outruns the estimator-bounded relief). Probe said `CONSUMER` correctly
  each time; this is the next net down.
- Compression-rail limit cycle at low flow: `AUTO_START → 1.5 s →
  RELIEF_PAUSE` every ~3.3 s (45× in the cancelled print's first 2.5 min).
- `BL:FOLLOW_GATED` on 6/10 in-print tip-forms and 2/6 bench ones with a
  normal purge after every one — **no correlation with tip quality** (the
  one bad tip, bench swap 4, was a `FOLLOW_GATED` swap, but so were seven
  good ones). Near-slip note stays a tuning item only.
- Cancel mid-print misbehaves in the retract helper — out of scope, user
  call.

---

## Common pre-flight (once per session)

1. **Build the dev-tuning superset and flash.** Tier-3 `SET:`/`GET:` handlers,
   including every knob below, exist only under `-DFLARE_DEV_TUNING=ON`.
   ```bash
   python3 scripts/validate_regression.py     # green = same superset CI builds
   python3 scripts/flash_flare.py             # BUILD_FLASH.md "Easy Path"
   ```
2. **Power-cycle the board** — a real reboot, not a reconnect. This is what
   catches a runtime `SET:` that never persisted (the `rail-break 3000`
   lesson: the rig ran a value for a day that a reboot silently dropped).
3. **Read back every knob you rely on with `GET:` *after* the reboot.** If a
   value differs from what was `SET:`, stop — re-flash, re-check, then
   continue. Never measure a configuration the board isn't actually running.
4. **Start the passive monitor in a second terminal and leave it running for
   the whole session.** It classifies A3, C1 and every `TMC:*`/`SYNC:*` event (Group D)
   passively as they happen, and logs everything.
   ```bash
   python3 scripts/verify_hw_open_items.py watch --log hw_$(date +%Y%m%d_%H%M).jsonl
   ```
5. **Precondition for most bench items:** filament *loaded* in both lanes
   (`lane_in_present` true on the active lane, `lane_out_present` clear on
   the other). Idle, not printing.

**Reading results.** `Ctrl+C` on the watch terminal prints the report, or
re-render with `python3 scripts/verify_hw_open_items.py report <log>.jsonl`.
`PASS` → tick the box in the owning plan/sheet (listed per item below),
citing the JSONL timestamp. `FAIL` → don't tick; the report names the
offending event — open a debug session on that one. `PENDING` → the
condition never occurred; re-run the trigger.

---

## Group A — Bench, no motion, no Klipper needed

Board powered, daemon up. Nothing moves.

### [ ] A1 · Flash wear counter persists across reboots (old #5)
Owner: Phase 1 §3 · `01-01-PLAN.md` "Perform repeated settings saves…"

1. `python3 scripts/flare_cmd.py "GET:FLASH_ERASE_COUNT"` → note `N`.
2. Do 3 settings saves: `SET:SYNC_MIN_RATE:100` / `:101` / `:100` (any
   persisted tunable — each `SET:` of a persisted value is one sector write).
3. `GET:FLASH_ERASE_COUNT` → expect `N+3`.
4. **Power-cycle.** `GET:FLASH_ERASE_COUNT` → expect `N+3` still.
5. Pass: count increments per save and survives the reboot. No
   `EV:FLASH:WEAR_WARNING` (threshold is 80 000 — if you see it, the counter
   is wrong, not the flash).

### [ ] A2 · Watchdog reset leaves a readable crash log (old #7)
Owner: Phase 11 · `11-01-PLAN.md` "HW: confirm `EV:CRASH:DETECTED:WATCHDOG`…"

There is no bench command that forces a stall on purpose. Options, pick one:
- hold the board in a debugger / SWD halt long enough for the hardware
  watchdog to fire, or
- temporarily add a `while(1);` behind a dev-only command, flash, trigger,
  revert (do **not** commit it).

1. Note the current loop stats: `python3 scripts/flare_cmd.py --loop-stats`.
2. Trigger the stall. Board must reset itself within the watchdog period.
3. After it comes back (~2 s): expect `EV:SYSTEM:WATCHDOG_RESET` then
   `EV:CRASH:DETECTED:WATCHDOG` in the watch log.
4. `python3 scripts/verify_hw_open_items.py fire watchdog-check` — it reads
   `--crashlog` and `--loop-stats` back for you.
5. Pass: `OK:CRASH:HDR:reason=…` with entries whose `t=` timestamps run up to
   the stall, then `OK:CRASH:END`. `CAL:CRASHLOG_CLEAR` and confirm
   `OK:NO_CRASH` afterwards.

### [ ] A3 · TMC UART loss → exactly one fault, restored on replug (old #11)
Owner: Phase 12 · `12-01-PLAN.md` HW bullet "TMC unplug → single `TMC:FAULT`"

1. Printer fully idle. Watch monitor running.
2. Unplug lane-2 TMC UART (or its 5 V) for ~1 s. Replug.
3. Pass (monitor classifies passively): exactly one
   `EV:TMC:FAULT:2:COMM_FAIL`, no repeat storm while unplugged, then one
   `EV:TMC:RESTORED:2` after replug. Health flag back to `1` in status.

### [x] A4 · `MMU_PRINT_START` / `MMU_PRINT_END` register (remainder of old #15) — closed 2026-09-14, see top
Owner: Phase 14 · `14-VERIFICATION.md` "Human Verification Required" #2 (7/9 done)

These two are **not** no-ops — they run `_FLARE_SYNC_TOOLHEAD` (a synchronous
`BS`) and will block Klipper's gcode queue for minutes if the buffer isn't
settled. That was the "Moonraker hanging / `Request 'gcode/script' pending`"
symptom during earlier bench work.

1. Klipper connected, printer idle. In the watch terminal confirm sync is
   idle and the last `BUF_STAB:DONE` has already landed.
2. `python3 scripts/verify_hw_open_items.py fire 15-command-stubs --include-print-sync`
3. Pass: both dispatch without "Unknown command" and Klipper's queue returns
   within a few seconds. If it hangs: `FIRMWARE_RESTART` clears the queue —
   that is a *finding* (the macro ran with an unsettled buffer), record it.

---

## Group B — Bench, firmware self-triggers the motion

Filament loaded both lanes. The script fires the firmware command itself
immediately before the check, so there's nothing to time by hand. Only one
item left here — #9 (cutter abort) closed on 2026-09-12.

### [ ] B2 · Bare `BL:T` is passive; argumented `BL:T` breaks and follows (old #10, re-run)
Owner: Phase 12 · `12-SPEC.md` bullet marked **NOT YET CONFIRMED** — the
2026-09-12 attempt hit a script timing bug, fixed in `b28b40f`; this is the
re-run.

```bash
python3 scripts/verify_hw_open_items.py fire 10-bl-bare-vs-args --moonraker-url http://192.168.8.144:7125
```
What it does: bare `BL:T` → expect **no** `BREAK`/`FOLLOW`. Then nudges the
buffer toward tension with a relative extrude (`buf_max_travel × 1.5`) so the
PRIME phase has less ground to cover, arms `BL:T:20:300`, and fires a real
20 mm relative retract via Moonraker.
Pass: `BREAK` → `FOLLOW` → `FOLLOW_DONE`. A `PRIME_BOUND` short of engaging
means the buffer was resting at compression when armed — the nudge is
supposed to prevent that; if it still happens, record the `BP` at arm time.

---

## Group C — Bench, Klipper connected, one manual gcode line

Filament loaded. No queued print job — just the Klipper console.

### [ ] C1 · Genuine runout escalates to RELOAD without a race stall (old #6)
Owner: Phase 1 §3 · `01-01-PLAN.md` "Verify genuine complete runout…"

1. Load a short remnant (~30 cm) on the active lane; the other lane full.
2. Console: `G1 E300 F300` (repeat shorter extrudes if you prefer) until the
   remnant runs past the sensor.
3. Pass (monitor classifies): clean `EV:RUNOUT` → `RELOAD:SWITCHING` →
   `EV:RELOAD:LOADED`, no stall between them, no `FOLLOW_JAM`.

### [ ] C2 · Type-P RELOAD set — four cases (Phase 1 §2, H4 fix `audit-reliability-fixes`)
Owner: Phase 1 §2 · `01-01-PLAN.md` all four unchecked boxes. Background:
`.planning/backlog/audit-reliability-fixes/` H4 — the fix is consumer-aware
staged-compression completion + `RL:` state-aware resume; build-green,
HW-pending.

All four use the same setup as C1 (remnant on active lane, full standby).
Watch `BP`, `TC`, and `RELOAD:*` events; the ambiguity these tests resolve is
"did it complete on compression contact vs. wait for a tension grab".

| | # | Case | Steps | Pass |
|---|---|---|---|---|
| [ ] | C2a | Runout → auto RELOAD with **consumer active** | As C1, but keep extruding (`G1 E200 F300`) through the reload | Contact seen on compression side, then success on the extruder's grab; `RELOAD:LOADED`, no `FOLLOW_JAM` |
| [x] | C2b | No false `RELOAD:LOADED` without motion — **PASS 2026-09-14** | Fresh state, both lanes loaded, extruder **idle**. `python3 scripts/flare_cmd.py RL:` | `RL:` is a no-op that re-emits `RELOAD:LOADED` with **no motor motion** (toolhead already confirms filament). Any motion or `RELOAD:JOINING` = FAIL |
| [ ] | C2c | Paused / no-consumer `RL:` completes on staged compression | After a real runout (C1), do **not** extrude; `RL:` with extruder idle | Follow completes with filament parked at the extruder mouth (staged compression), `RELOAD:LOADED`, **no `FOLLOW_JAM`**, no wait for a tension grab that can't come |
| [ ] | C2d | `RL:` re-issued right after a completed reload, consumer active | Complete C2a, keep extruding, immediately `RL:` again | `RELOAD:LOADED` re-emitted, zero motion, no `FOLLOW_JAM` |

---

## Group D — Phase 16 TMC tension boost (bench functional, then thermal)

Owner: Phase 16 · `16-VALIDATION.md` "Manual-Only Verifications" + ROADMAP
Phase 16 SC3 "`HW:` thermal check on rig before default-on". Knobs
(MANUAL.md): `SYNC_TENSION_BOOST_IRUN` (mA, 0 = off, ≤ 1200),
`SYNC_TENSION_BOOST_ON` (default −0.50), `SYNC_TENSION_BOOST_OFF` (default
−0.30). Status token `TB:` 0/1. Events `TMC:BOOST:<lane>` /
`TMC:NORMAL:<lane>`.

**Two things to know before you start.**
- Boost only engages if `IRUN` is **strictly greater** than the lane's base
  run current (`sync.c` `sync_check_tension_boost`: `boost_ma <= run_current
  → no-op`). With `run_current: 0.8` that means `IRUN` must be > 800.
- The ON/OFF thresholds are **absolute** `BP` values. This project already
  learned (ROADMAP Phase 13 carried caveat, `51bdca8`) that this rig's
  tension rail reads shallow: `−0.36` cold, `−0.6…−0.7` mid-print, `−1.00`
  only with a hot extruder pulling. `−0.50` is inside the mid-print range,
  so it *should* engage — **D1 is there to prove it does on this rig**, and
  if it never trips, that's the finding to bring back to planning, not a
  reason to lower the threshold on the spot.

Setup:
```bash
python3 scripts/flare_cmd.py "SET:SYNC_TENSION_BOOST_IRUN:1000"
# power-cycle, then confirm all three persisted:
python3 scripts/flare_cmd.py "GET:SYNC_TENSION_BOOST_IRUN"   # 1000
python3 scripts/flare_cmd.py "GET:SYNC_TENSION_BOOST_ON"     # -0.50
python3 scripts/flare_cmd.py "GET:SYNC_TENSION_BOOST_OFF"    # -0.30
```

### [ ] D1 · Boost engages at the ON threshold, releases at OFF (R1, R2)
1. Klipper connected, sync active (a slow continuous `G1 E…` at a rate the
   MMU can't quite keep up with pulls the buffer toward tension; or use the
   Phase 13 print in Group F and watch there).
2. Watch `BP` and `TB:` in the daemon poll.
3. Pass: `EV:TMC:BOOST:1` the first time `BP ≤ −0.50`, `TB:1`; **no repeat
   `BOOST` events** while it stays engaged (edge-triggered); `EV:TMC:NORMAL:1`
   and `TB:0` only once `BP ≥ −0.30` (hysteresis — a bounce between −0.50 and
   −0.30 must not toggle it).

### [ ] D2 · Boost is unconditionally released on every exit path (R3)
For each exit, get boost engaged (D1) then trigger the exit; expect
`TMC:NORMAL` + `TB:0` immediately:
- sync ends normally (stop extruding, wait for `SYNC:AUTO_STOP`)
- `STOP`
- a sync fault (`SYNC:FAULT_HOLD` — e.g. let tension dwell exceed
  `SYNC_TENSION_STOP_MS`)
- `TC:` toolchange away from the boosted lane
Pass: 4/4 release. A `TB:1` surviving any of these is a FAIL.

### [ ] D3 · Heartbeat re-apply restores the *current* value, not a stale one (R6)
With boost engaged (`TB:1`), do the A3 unplug/replug on the **active** lane's
TMC. On `EV:TMC:RESTORED:1`, the re-applied IRUN must be the boosted 1000,
not the base 800 (readback goes through `g_shadow_ihold_irun[]`). Then
release boost and repeat: re-apply must now give 800.
Pass: readback matches the shadow state both times.

### [ ] D4 · Thermal margin under sustained boost (R1, R4) — **gates default-on**
1. Boost engaged and *held* for **10 min** (a print stretch that keeps the
   buffer in tension, or the D1 bench pull).
2. Probe the gear-motor case temperature at the end (IR thermometer or
   contact probe on the can).
3. Pass: **< 65 °C**. Record the actual number in `16-VALIDATION.md`. This
   is the one measurement that decides whether `SYNC_TENSION_BOOST_IRUN`
   can ship with a non-zero default.

Afterwards, unless you're keeping it: `SET:SYNC_TENSION_BOOST_IRUN:0`,
power-cycle, `GET:` to confirm 0.

---

## Group E — Phase 15 daemon deploy on the Pi (Moonraker / OrcaSlicer / WebUI)

Owner: Phase 15 · `15-SPEC.md` acceptance lines. Nothing here touches
filament; it's the first run of 15-01/15-02 on the real Pi + Moonraker.
As of 2026-09-13 the Pi's daemon is **pre-Phase-15** (Moonraker returned
`404 Namespace lane_data not found`). Includes the `97901cf` re-entrancy fix.

Deploy:
```bash
# on the Pi (pi@192.168.8.144), in the FLARE checkout
git pull
sudo python3 scripts/install_daemon.py   # copies klipper/mmu.py + mmu_sensors.py, restarts flare_daemon.service
sudo systemctl restart klipper           # picks up the new mmu.py
journalctl -u flare_daemon.service -f    # leave open; any lane_data sync error prints here
```

### [ ] E1 · `lane_data` namespace is populated (REQ-moonraker-lane-data-push)
```bash
curl -s "http://192.168.8.144:7125/server/database/item?namespace=lane_data" | python3 -m json.tool
```
Pass: `lane0`, `lane1` present with `vendor_name, name, color, material,
bed_temp, nozzle_temp, scan_time, td, lane, spool_id, filament_id`. Gates
with a Spoolman spool show vendor/temps; gates without fall back to the gate
map (`vendor_name: ""`, temps 0).

### [ ] E2 · Sync is event-driven and non-blocking (REQ-…-sync-lifecycle)
1. In the WebUI change gate 0's color (or `POST /gatemap`).
2. Re-run the E1 curl within ~1 s.
Pass: `lane0.color` updated; daemon `/status` kept answering during the
write (no multi-second stall). In the journal, **exactly one** sync per
edit — a second sync ~immediately after is the re-entrancy bug returning.

### [ ] E3 · Orphan cleanup (REQ-…-cleanup)
Manually plant a stale key, then trigger any sync:
```bash
curl -s -X POST http://192.168.8.144:7125/server/database/item \
  -H 'Content-Type: application/json' \
  -d '{"namespace":"lane_data","key":"lane7","value":{"name":"stale"}}'
```
Pass: `lane7` gone from the namespace after the next sync.

### [ ] E4 · OrcaSlicer discovers the lanes (the point of E1)
In OrcaSlicer → device → sync filaments from printer. Pass: both lanes
appear with the names/colors/temps from E1. This is the only step that
needs the slicer; do it last.

### [ ] E5 · Maintenance counters, thresholds, PAUSE (REQ-maintenance-*)
Steps 1–2 **closed 2026-09-14** (after `41481c4`; before it `cutter_cuts`
stayed at 0 through six real cuts). Steps 3–4 still open — the first attempt
(`MMU_STATS COUNTER=swaps LIMIT=7 PAUSE=1`) never reached the daemon because
of the port typo `41481c4` fixed; not re-tried since.
1. ~~`GET /maintenance` → counters present~~ ✓
2. ~~one `CU` + one `TC:` → each +1, survives daemon restart~~ ✓
3. Klipper console: `MMU_STATS COUNTER=cutter_cuts LIMIT=<current+1> PAUSE=1`
   then one more `CU`.
   Pass: warning emitted **and** Klipper enters `PAUSE`. `RESUME`, then
   `MMU_STATS COUNTER=cutter_cuts RESET=1` → count 0, `last_reset` set.
4. Bare `MMU_STATS` / `SHOWCOUNTS=1` lists the counters.

### [ ] E6 · WebUI Maintenance card
Open the WebUI. Pass: card shows each counter, count-vs-limit bar, a warning
indicator once over limit (use the E5 state), and Reset per counter works.

---

## Group F — Real print (Phase 13 A/B, with Group D riding along)

### [ ] F1 · Phase 13 A/B print passes the four-item bar (D-31)
*2026-09-14, informal run of a DIFFERENT slice (`Box_klein_PYD_PETG_1h7m`),
so NOT the D-30 A/B and NOT a Phase 13 verdict. `verify_phase13_hw_log.py`
on the print window: item 1 relief-pause 0.80/60 s **PASS**; item 2
`TENSION_RISK_HIGH` 21 vs <13 **FAIL**; item 3 false stops 0/0/0/0 **PASS**;
item 4 rail hits 2107 across 33 433 sync-active samples **FAIL**. Both fails
are the SAME compression-side low-flow limit cycle: of 1005 strict (±0.999)
hits, 997 are the +1.0 compression rail (buffer full → RELIEF_PAUSE → drain,
128 episodes ~0.8 s each), only 8 touch the −1.0 tension rail (0.02%). The
buffer sits at goal (+0.50) 76% of sync time. The tension/starvation domain
today's purge fixes target is essentially never hit; the residual is the
open `typep-feed-hunting` compression-side question. The formal A/B still
needs the 09-12 job itself (`Box_klein_PYD_PETG_1h44m`).*
Owner: Phase 13 · `13-HW-VALIDATION.md` (the full sheet — pre-flight, capture,
pass bar, results table all live there; **this section only tells you where
it fits in the session**). Also closes the `typep-feed-hunting-open` decision.

**Why this can't be a bench item:** the four pass-bar items are trends under
slicer-generated motion (speed/direction changes, retract timing, ooze
micro-adjustments). Manual `G1 E…` lines give a flat demand signal and a
misleadingly clean result.

1. Complete Group D setup first if you want D1/D4 from the same print
   (recommended — it's the only realistic sustained-tension source).
2. Follow `13-HW-VALIDATION.md` **Pre-flight** (GET `SYNC_PSF_RELIEF_MULT`
   1.33, `SYNC_TENSION_STOP_MM` 32.0, `SYNC_TENSION_STOP_MS` 6000 after a
   reboot).
3. Two captures, both for the whole print:
   ```bash
   python3 scripts/verify_hw_open_items.py watch --log phase13_hw_watch.jsonl --interval 1.0
   python3 scripts/flare_sync_check.py --daemon --csv phase13_hw_positions.csv --duration 7500
   ```
4. **Same job as the 2026-09-12 baseline**: ~2 h, ~10 swaps, same filament,
   same slicer settings. A changed print is not a comparison.
5. Derive the verdict with the committed parser — never by hand:
   ```bash
   python3 scripts/verify_phase13_hw_log.py --events phase13_hw_watch.jsonl --csv phase13_hw_positions.csv
   ```
   Paste its output verbatim into the sheet's Results table.

Pass bar (D-31, all four): relief-pause ≤ 1/60 s · fewer than 13
`TENSION_RISK_HIGH` · zero false stops (`FAULT_HOLD`/`STOP:MM`/`STOP:MS`/
`PROBE:NO_CONSUMER`) · no rail hits during print sync, judged relative to
this capture's own observed extremes.

**Also observe during this print (no pass bar, just record):**
- `TB:` engaging/releasing per D1 — count of `TMC:BOOST` events and the
  `BP` at each.
- The "near-slip sound" on the ~43 mm gear retract during tip-form, noted
  during the 2026-09-12 `51bdca8` validation and left open as a tuning
  item: does it recur, and does the follow gate or a second `BREAK`
  accompany it?
- `flowguard.level` swing (optional re-confirm of old #14 from a
  Moonraker-sourced sample; the 2026-09-12 data already closed it).

Neither capture file is committed — only the parser output.

---

## Parked — need a type-D rig, or a decision rather than a test

| Old # | Item | Why parked |
|---|---|---|
| #8 (`>0` variant) | Toolchange with `TC_TS_PARK_MM > 0` → no `FAULT:MOVE_COMPRESSION` | The default-config case is **closed** (see top). The firmware-side park itself is **type-D only** (MANUAL.md: "skipped on type-P"), so the `>0` path can't run on this rig. Owner `12-01-PLAN.md`. |
| — | Bare `BL:T` on type-D **during retract guard** stays passive | `12-SPEC.md` calls this out as type-D-specific; B2 covers the generic case only. |
| #12 | Buffer-state-lock D2 question | A design decision, not a pass/fail check. |
| #16 | TMC thermal margin (generic) | Superseded by **D4**, which is the concrete version. |

---

## Where each result gets recorded

| Group | Tick the box in |
|---|---|
| A1, C1 | `.planning/phases/01-hardware-validation-and-audit-closeout/01-01-PLAN.md` §3 |
| C2a–d | same file, §2 |
| A2 | `.planning/phases/11-firmware-forensics-jitter/11-01-PLAN.md` HW line |
| A3, B2 | `.planning/phases/12-post-phase-2-10-regression-fixes/12-SPEC.md` HW bullets (the spec of record — `12-01-PLAN.md`'s summary bullet mirrors it); B2 clears the **NOT YET CONFIRMED** line |
| A4 | `.planning/phases/14-klipper-mmu-status-parity/14-VERIFICATION.md` human item #2 |
| D1–D4 | `.planning/phases/16-tmc-tension-current-boost/16-VALIDATION.md`; ROADMAP Phase 16 SC3 `HW:` |
| E1–E6 | `.planning/phases/15-…/15-SPEC.md` acceptance lines; ROADMAP Phase 15 row |
| F | `.planning/phases/13-type-p-sync-relief-fault-trip/13-HW-VALIDATION.md` Results + `HW:` acceptance |

Then update the ROADMAP progress rows for Phases 1, 12, 13, 15, 16 from
"(HW pending)" and `STATE.md`'s current focus. AGENTS.md rule 12: no `HW:`
box is ever ticked without real rig results and the developer's explicit
confirmation.
