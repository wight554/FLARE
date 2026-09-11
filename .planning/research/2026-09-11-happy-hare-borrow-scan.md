# Happy-Hare borrow scan — 2026-09-11

**Source:** https://github.com/moggieuk/Happy-Hare, `main`, commit `ef8431c420231ca52e0ee3c9d8dfafaa7af544ea`
(2026-09-08 17:13 +0700, "Merge pull request #1196 from burkfers/fan_emu_default"). HH version string `4.0.0`
(`extras/mmu/mmu_constants.py:20`). Cloned depth 200 into the session scratchpad; all `file:line` cites below are
against that SHA. UI-side claims were checked against Fluidd `develop` (`src/components/widgets/mmu/*.vue`,
`src/mixins/mmu.ts`) and Mainsail `develop` (`src/components/panels/Mmu/*.vue`, `src/components/mixins/mmu.ts`)
raw files fetched 2026-09-11.

**Scope:** what FLARE (RP2040 firmware + `scripts/flare_daemon.py` + `klipper/mmu.py` mock) can borrow NOW.
Layout note: HH v4 moved the sync controller to `extras/mmu/unit/mmu_sync_controller.py` (pure-Python, no Klipper
imports) with a Klipper glue class in `extras/mmu/unit/mmu_sync_feedback.py`; sensors in `extras/mmu/unit/mmu_buffer.py`
+ `extras/mmu/mmu_sensor_utils.py` + `extras/mmu/mmu_sensor_manager.py`; commands are one file each under
`extras/mmu/commands/`.

## Already borrowed (skip)

Sync-Feedback vocabulary + D/P/CO/TO codes; tension/compression naming; buffer range vocab; `rd_filter_len_mm` /
`rd_rate_per_mm` distance-based smoothing (`_smooth_rd_by_distance`, `mmu_sync_controller.py:1527`); `_map_reading`
asymmetric normalization (`mmu_buffer.py:269`); raw ADC [0,1] space; polarity note; `get_status` mock (filament_pos
ladder, sensors dict, gate maps, ttg_map, bypass = -2, Spoolman active spool); Fluidd/Mainsail command stubs;
EndlessSpool concept (FLARE RELOAD is the firmware analogue). HH's in-controller EKF / rotation-distance
autotune rejected for type-P (2026-06-04 psf-analog-rig: direct measurement makes it moot). Note FLARE *does*
have host-side autotune — `scripts/flare_sync_check.py tune` (SYNC_KP_RATE saddle search during a live print,
23cce2d) and the Phase 5 calibration wizard / live tuner (cc30e39) — so "autotune" below always means HH's
controller-side RD estimator, never host tuning. Nothing below re-proposes these.

## Top recommendations (ranked)

| # | Finding | HH cite | FLARE gap | Verdict | Effort | Tier |
|---|---------|---------|-----------|---------|--------|------|
| 1 | `flowguard` status dict (`enabled/active/trigger/level/max_clog/max_tangle`) — both Fluidd and Mainsail render a FlowGuard meter whenever a tension/compression/proportional sensor is present | `mmu_sync_feedback.py:370-420`; Fluidd `MmuFlowguardMeter.vue:193-229`; Mainsail `MmuFlowguardMeter.vue` | `klipper/mmu.py:1359-1497` has no `flowguard` key → meter renders dead | BORROW NOW | S | daemon + klipper mock |
| 2 | Bounded relief snap at extremes: pegged → RD target = `c·rd_ref/(1−relief_frac)`, `relief_frac` clamped 0.05–0.60 (default 0.25), plus `rate_extreme_multiplier=2.0` on the slew cap, never "snap to max" | `mmu_sync_controller.py:1170-1182`, `:1527-1555`, cfg `:169-173` | `sync.c:1988-2003` urgent refill snaps `g_sync_current_sps = target_sps` (soft-wall-blended toward max) → known ±0.8 hunting (memory: typep-feed-hunting-open) | BORROW NOW | S | firmware |
| 3 | Distance-based fault trip: while pegged, accumulate *relief motion* (mm) not time; trip when `≥ flowguard_relief_mm` (default `max(0.3·range, maxrange)`, user knob `flowguard_max_relief` = 8 mm); arm only after first observed state change / near-neutral | `mmu_sync_controller.py:807-905`, `:926-940`, `__post_init__ :228-233` | `sync.c:1617` tension dwell is ms-based (`SYNC_TENSION_STOP_MS` 6000), `sync.c:1374` `CONF_PSF_WALL_SAT_MS` | BORROW NOW (add mm-trip alongside ms) | S | firmware |
| 4 | Missing `printer.mmu` keys that current Fluidd/Mainsail read: `endless_spool_groups`, `endless_spool_enabled`, `sync_feedback_flow_rate`, `sync_drive`, `reason_for_pause`, `operation`, `last_tool`, `next_tool`, `last_toolchange`, `is_paused`, `has_bypass`, `unit`, `filament_direction`, `toolchange_purge_volume`, `gate_temperature`, `slicer_tool_map`, `espooler`/`espooler_active`, `drying_state`, `encoder: None`, `grip`, `servo`, `clog_detection_enabled`, `extruder_filament_remaining` | `mmu_controller.py:631-720`; Fluidd `mixins/mmu.ts` + widgets (key list §6); Mainsail `mixins/mmu.ts` | `klipper/mmu.py:1449-1497` lacks all of them | BORROW NOW | S | klipper mock (+daemon for the live ones) |
| 5 | Tangle prevention: boost gear IRUN to 100 % when type-P tension ≥ 0.3, release at ≤ 0.2 (hysteresis), armed only while sync/monitoring active, coalesced + re-validated before apply, always restored on unsync/gate change | `mmu_sync_feedback.py:216-317`, params `mmu_unit_parameters.py:398-400`; test `test/test_mmu_tangle_prevention.py` | FLARE has one static IRUN per lane (`motion.c:280`, `settings_store.c:512`); no current profile per phase | BORROW NOW | M | firmware |
| 6 | Proportional-sensor active probes resolve "deep tension = free in bowden vs starved": gear-only centre → still ≤ −0.9 ⇒ not gripped; extruder-only retract → shift >+0.1 toward compression ⇒ gripped | `mmu_filament_movement.py:3655-3700`, `:1922-1985` (post-load grip check: synced feed 0.5·maxrange, then ≤4 extruder-only steps until compression releases) | Memory: type-P +1.0 tension ambiguity resolved only by `(mode × filament_present)` heuristic | BORROW NOW (firmware `PROBE:` cmd + toolchange post-load use) | M | firmware |
| 7 | Moonraker `lane_data` namespace push for OrcaSlicer (per-lane vendor/name/color/material/temps/spool_id) + cleanup on gate-count change | `components/mmu_server.py:1746-1846` | Daemon already has Moonraker + Spoolman clients (`flare_daemon.py:305-380`) but pushes no `lane_data` | BORROW NOW | S | daemon |
| 8 | Maintenance counters with `LIMIT`/`WARNING`/`PAUSE` (`MMU_STATS COUNTER=blade INCR=1`), persisted; cutter macro fires `_MMU_EVENT EVENT=filament_cut` | `commands/mmu_stats.py:34-47, 61-105`; `config/macros/mmu_servo_cutter.cfg` (`filament_cut` event) | Daemon stats only swaps/loads/unloads (`flare_daemon.py:181-231`); firmware emits `EV:CUT:DONE` (MANUAL.md:376) but nothing counts it | BORROW NOW | S | daemon + klipper mock |
| 9 | Action strings beyond Loading/Unloading: `Cutting Filament`, `Forming Tip`, `Heating`, `Preload`, `Checking` | `mmu_constants.py:124-137`, `_get_action_string` | `flare_daemon.py:1342-1357` `_derive_action` returns only Loading/Unloading/Idle; `EV:CUT:*`, `TC:CUTTING`, AUTOPRELOAD task available | BORROW NOW | S | daemon |
| 10 | Plant "chaos" stick-slip measurement + switch hysteresis in the sync simulator | `utils/sync_feedback_sim.py:159-383` | `tests/host/sim_plant.c` integrates slack with slew/lag "stress" only; no stick-slip measurement noise, no switch hysteresis | LATER | S | tests/host |

## 1. Sync controller (`extras/mmu/unit/mmu_sync_controller.py`, glue `unit/mmu_sync_feedback.py`)

Architecture (unchanged since the rd_filter port, but now split into `_AutotuneEngine`, `_FlowguardEngine`,
`SyncController`). Update is motion-triggered: Klipper's extruder monitor calls `update(eventtime, extruder_delta_mm,
sensor_reading)` every `sync_feedback_extrude_threshold` mm (default 5 mm, `mmu_unit_parameters.py:393`) and also on
any discrete sensor edge (`_handle_sync_feedback`, `mmu_sync_feedback.py:597`). Output is a rotation-distance ratio,
so "extruder stopped" is a non-issue for HH (no rate to decay) — FLARE's `SYNC_PSF_DECAY_SPS_PER_S` remains
necessary; nothing to borrow there.

### 1.1 Extreme handling — bounded relief snap (rec #2)
```python
# mmu_sync_controller.py:1170-1182
if cfg.snap_at_extremes and d_ext != 0.0 and (comp_ext or tens_ext):
    zsign = 1 if comp_ext else -1
    relief_frac = max(0.05, min(0.60, float(cfg.extreme_relief_frac)))   # default 0.25
    denom = max(0.05, 1.0 - (sgn * zsign) * relief_frac)
    rd_target = (c_hat * rd_ref) / denom
```
i.e. when pegged in tension the gear is commanded to at most `1/(1−0.25) = 1.33×` the extruder-equivalent rate,
never the envelope max; `_smooth_rd_by_distance` still applies with `rate_extreme_multiplier=2.0` (`:1545-1553`) and
`readiness_extreme_floor=0.7`. The absolute envelope is `rd_min_max_speed_multiplier=0.25` (±25 % speed,
`:1279-1287`).
- FLARE today: `sync.c:1988-2003` — type-P below `−CONF_PSF_SOFT_WALL_START` sets `g_sync_current_sps = target_sps`
  where `psf_control_law` (`sync_analog.c:48-64`) has already blended target toward `max_sps`. Memory
  `typep-feed-hunting-open` already proposes "moderate snap to est×1.3"; HH's shipped default is exactly 1.33×.
- Borrow: `g_sync_current_sps = min(target_sps, est_sps * (1/(1−PSF_RELIEF_FRAC)))` with
  `PSF_RELIEF_FRAC=0.25` in `config.ini`, and double `SYNC_PSF_SLEW_PER_MM` while pegged instead of bypassing
  smoothing entirely. Verdict BORROW NOW, S, firmware.

### 1.2 FlowGuard — distance-based clog/tangle (rec #3)
`_FlowguardEngine.update_flowguard` (`:807-905`): per tick compute signed relief effort
`effort = d_ext·(rd_ref/rd_cur − 1)` (`:926-940`); while compression-pegged accumulate tension-side effort into
`_relief_comp_mm`, trip `"clog"` when `≥ flowguard_relief_mm`; while tension-pegged accumulate compression-side
effort → `"tangle"`. Leaving the extreme resets both sides. `level` = signed fraction of relief budget used
(`+` clog side, `−` tangle side); `max_clog`/`max_tangle` are high-water marks — these are what the UI meters draw.
Arming (`:829-841`): disarmed until motion *and* (a coarse state change or |z| < 0.12 for P) — prevents boot-time
false trips. FlowGuard trip → `runout_helper.note_clog_tangle()` → same `_runout()` path as a real runout
(`mmu_controller.py:3844`), which then calls `check_filament_runout()` to discriminate clog vs runout by sensors.
- FLARE today: `sync_check_tension_dwell_and_ramp` (`sync.c:1617`) trips `FAULT_HOLD` after
  `SYNC_TENSION_STOP_MS` (6000 ms) of continuous tension; compression uses `CONF_PSF_WALL_SAT_MS`. Time-based
  thresholds change meaning with print speed. FLARE has no extruder view, but it *does* know its own feed travel:
  "fed N mm while pinned at tension with buffer not moving" is the firmware-local analogue of `_relief_tens_mm`.
- Borrow: add `SYNC_TENSION_STOP_MM` (lane travel while `BUF_TENSION`, reset on leaving tension) as a second trip
  condition; keep the ms timer as fallback. Also adopt the arming rule (first trip only after an observed state
  transition since sync start) — stronger than the current "restart timers on activation" fix
  (memory typep-stale-fault-timers). Verdict BORROW NOW, S, firmware. Export `level/max_clog/max_tangle` (rec #1).

### 1.3 Two-level (type D / CO / TO) branch
`_twolevel_rd_target` (`:1472-1513`): pure relay — compression extreme → `rd_high`, tension extreme → `rd_low`,
neutral holds last level; `os_min_flip_mm` anti-chatter (default 0.0 — same conclusion as FLARE memory
`relay-min-flip-compression-deadlock`). `rd_twolevel_speed_multiplier` ±5 % plus a ±5 % `boost` until the first
autotune candidate (`:1290-1303`). Type-D mid-band: HH has **no** control-side estimator either. `twolevel_phase()`
(`:500-525`) estimates progress within the current segment from a 6-sample FIFO of past segment lengths and is used
**only** for the UI (`_expected_sensor_reading`, `:1581-1635`, triangle wave between ±0.9). Duty estimator
`_recommend_rd_from_twolevel` (`:626-694`) pairs low/high segments into cycles, computes duty-weighted *speed*
(`v = (1−f)/rd_low + f/rd_high`), and rejects updates whose z-score vs cycle-to-cycle variance is < 1.0.
- FLARE: `sync_relay.c` relay + neutral-feed sampling; memory `typed-buffer-no-midband-groundtruth` says the mid-band
  estimator FAILED. HH corroborates: SKIP for control. LATER (S, daemon): reuse the segment-phase triangle for
  `sync_feedback_bias_modelled` so the UI bar moves between clicks instead of sitting at ±1.
- z-score gating on the duty estimate is worth noting for `scripts/flare_analyze.py` (offline) — LATER, S, docs/scripts.

### 1.4 Type-P specifics
- Virtual endstops from the analog value with 4 % hysteresis and direct tension↔compression jumps
  (`mmu_buffer.py:205, 295-333`); `analog_sensor_threshold` 0.9 (min 0.5). FLARE already has
  `BUF_SRC_VIRTUAL_ENDSTOP`. SKIP.
- `_rd_deadband_frac = 0.003` (`mmu_sync_feedback.py:65`): sub-0.3 % RD changes not pushed to the stepper. FLARE
  sets a software step rate; no cost. SKIP.
- `buffer_spring_state: none|tension|neutral|compression` (`mmu_buffer.py:47-58`) and `supports_validation()`
  (`:117-128`) — HH makes the spring rest side an explicit config and gates active probes on it. FLARE hard-codes
  home = −1.0 (tension). LATER, S, firmware/docs: expose as a documented invariant or knob.
- Readiness gating `sensor_lag_mm`/`info_delta_a` (`:1557-1579`): default 0 (disabled). SKIP.
- Per-gate learned RD is persisted via calibrator (`_handle_mmu_unsynced` → `note_rd_telemetry`,
  `mmu_sync_feedback.py:475-513`). FLARE rejected controller-side RD autotune (host `sync_check tune` covers kp). SKIP.

### 1.5 Tangle prevention — gear current boost (rec #5)
`_check_tangle_prevention` (`mmu_sync_feedback.py:291-317`): `tension_level = −sensor`; `≥ threshold (0.3)` →
schedule boost to 100 % IRUN; `≤ release (0.2)` → restore `sync_gear_current` (default 50 %,
`mmu_unit_parameters.py:352`). Applied via coalesced deferred callback that re-checks `enabled && active &&
armed` and verifies the driver actually took the value (`:264-289`); armed only while filament monitoring is on
(`mmu_controller.py:2493-2520`); unconditionally restored on unsync / gate change (`:475-527`, PR #1020 fix).
- FLARE: one `g_tmc_run_current_ma[lane]` for everything (`settings_store.c:512`, `motion.c:280`). HH also runs a
  reduced `sync_gear_current` during printing (thermal) and `extruder_collision_homing_current` 50 % when homing.
- Borrow: a small per-lane current profile in firmware — `SYNC_CURRENT_PCT` (default 100 to preserve behaviour),
  `TANGLE_BOOST_THR/REL` (type-P only, 0.3/0.2, tension side), boost writes `IHOLD_IRUN` over UART only on hysteresis
  edges (TMC UART cost is per edge, not per tick). Restore on sync stop, fault hold, lane switch. Verdict BORROW
  NOW, M, firmware. Test it in `tests/host` (HH has `test/test_mmu_tangle_prevention.py`).

### 1.6 Post-load tension relax
`adjust_filament_tension` (`mmu_sync_feedback.py:319-339`): switch path homes *off* the triggered switch then centres
by `buffer_range/2` (`:839-918`); proportional path does one proportional move `−state·(maxrange/2)` then nudges of
`0.1·maxrange/2` with 0.1 s settles, 10 s timeout, total-travel cap (`:920-1046`). Called after every load
(`toolhead_post_load_tension_adjust`, `mmu_filament_movement.py:2000-2043`).
- FLARE: `BS:` buffer stabilize exists; type-P `BUF_GOAL` seeding. Roughly equivalent. SKIP.

## 2. Runout / EndlessSpool / gate-switch flow

- Runout dispatch (`commands/mmu_sensor_runout.py`): every runout carries `EVENTTIME`; it is ignored if
  `eventtime < runout_last_handled_time` (duplicate from a second sensor) or if it falls inside the last
  monitoring-disabled window `[runout_last_disable_time, runout_last_enable_time)` (`mmu_controller.py:2493-2520`).
  The handler re-reads the sensor (`check_event_sensor`) and refuses if it now shows filament ("suspects sensor
  malfunction"). Only `mmu_entry` (pre-gate) on the selected gate, `mmu_exit`/`mmu_shared_exit`, trigger a runout;
  `extruder` entry sensor runout ⇒ "manual intervention" (filament already past the MMU — nothing to reload).
  FLARE firmware is synchronous and masks sensors during TC (BEHAVIOR.md:174); the window logic is implicit. SKIP.
  The "re-read before acting" and "runout at extruder ⇒ not reloadable" distinctions are already how FLARE's
  IN/OUT-anchored RELOAD behaves. SKIP.
- `_runout()` (`mmu_controller.py:3844-3905`): park → if event type unknown, `check_filament_runout()`
  (`mmu_filament_movement.py:3612`) decides clog vs runout from sensors before the gate (encoder buzz fallback) →
  mark gate EMPTY → `get_next_endless_spool_gate` (round-robin within the ES group, skipping EMPTY,
  `mmu_gate_maps.py:212-225`) → optional `endless_spool_eject_gate` (waste gate) → unload with standalone tip form →
  eject from gate → load next → `_continue_after("endless_spool")`.
- `endless_spool_on_load` (`mmu_controller.py:3046-3067`): on `T<n>`/load, if the target gate is EMPTY and ES is
  on, remap the tool to the next non-empty gate in its group *before* loading. FLARE: `TC:n` to an empty lane fails
  (`TC:ERROR:RUNOUT`/`NO_FILAMENT`); with two same-material lanes the daemon/mock could remap `T0`→lane 2
  automatically when lane 1 `IN=0`. Verdict LATER, S, klipper mock (`MMU_ENDLESS_SPOOL` is a NOOP at
  `klipper/mmu.py:170`; groups also needed for rec #4).
- `validate_gate_status` (`mmu_gate_maps.py:230-246`): sensor truth overrides persisted gate status (exit sensor
  ⇒ AVAILABLE; pre-gate false ⇒ EMPTY; pre-gate true but EMPTY ⇒ UNKNOWN). FLARE daemon already derives
  `gate_status` from IN/OUT each poll. SKIP.
- `MMU_TEST_RUNOUT [TYPE=clog]` (`commands/mmu_test_runout.py`) injects a synthetic runout into the real handler.
  FLARE has no injection path except the host sim. LATER, S, firmware (dev-tuning build only): `TEST:RUNOUT` that
  forces the active lane's IN-clear path so RELOAD can be regression-tested on the rig without pulling filament.

## 3. Toolhead / tip forming / cutter

- HH `_MMU_CUT_TIP` is a toolhead-side Filametrix macro (`config/macros/mmu_cut_tip.cfg`, 307 lines: current
  reduction around the cut, in-bounds move, gantry servo); MMU-side cut is `SERVO_CUTTER_ACTION`
  (`config/macros/mmu_servo_cutter.cfg`): open → feed `feed_length+cut_length` → N× close/open → retract 1 mm →
  close → `_MMU_EVENT EVENT=filament_cut` → re-park at gate. FLARE's `cutter.c` sequencing already covers this
  (clear→cut→clear, feed timeout). Only the **event/counter** is missing (rec #8). SKIP the macro itself.
- Unload verification: `_unload_extruder(validate=True)` uses `toolhead_residual_filament` +
  `toolhead_ooze_reduction` subtracted from unload distance (`mmu_filament_movement.py:2109-2117`) and a
  `toolhead_unload_safety_margin` (10 mm). FLARE unload is sensor-anchored (OUT clears), not distance-anchored.
  SKIP.
- Extruder-entry homing via compression switch (`extruder_homing_endstop: filament_compression`,
  `mmu_parameters.cfg:278-280`) and `toolhead_entry_tension_test` (`mmu_filament_movement.py:1835-1862`): after
  bowden load, drive **extruder only** up to `2·buffer_maxrange` and require the compression switch to release —
  proves the extruder gripped. FLARE's RELOAD FOLLOW already uses the buffer as the join detector
  (BEHAVIOR.md:601-631). For MMU-mode `FL:`/`TC:` loads without a toolhead sensor FLARE has no grip proof; the
  firmware cannot move the extruder, but a Klipper macro can: `_FLARE_LOAD_HOTEND` could do `G1 E<maxrange>`
  with sync off and read `ST:` buffer position (compression must release). LATER, S, klipper macros/docs.

## 4. Sensor handling

- `MmuRunoutHelper` (`mmu_sensor_utils.py:98-306`): one class per switch; insert vs remove vs runout decided by
  `print_stats.state == "printing"` (`:202-232`); `min_event_systime = NEVER` while a handler runs (re-entrancy
  gate); `suspend_events()` (`:250-278`) suppresses gcode events without disabling the sensor so `check_sensor()`
  still reads it — used when an operation deliberately drives filament across a sensor. FLARE firmware masks
  sensors in the same way. SKIP.
- Sensor ordering ladder for `get_sensors_before/after(pos)` (`mmu_sensor_manager.py:688-747`):
  entry → exit → shared_exit(HOMED_GATE) → extruder(HOMED_ENTRY) → toolhead(HOMED_TS). `check_for_runout()`
  (`:604-611`) = any sensor before LOADED reads False. FLARE's mock cascade (`klipper/mmu.py:1361-1378`) is the
  same idea. SKIP.
- Proportional-sensor ambiguity (rec #6): HH does **not** resolve "deep tension" statically — it probes.
  `_check_filament_in_extruder_proportional` (`mmu_filament_movement.py:3655-3700`): (1) gear-only centring via
  `adjust_filament_tension()`; if it fails and value still ≤ −0.9 ⇒ "filament free in bowden" (no consumer);
  (2) extruder-only retract of `buffer_maxrange`, shift > +0.1 toward compression ⇒ gripped. For FLARE the
  firmware-local half is (1): feed the lane a bounded `BUF_MAX_TRAVEL_MM` at `JOIN_RATE`; if `g_buf_pos` stays at
  the tension rail the strand has no consumer (runout past the buffer or not gripped) — distinguishes the
  `home vs starvation` case that memory `type-p-tension-klipper-agnostic` leaves to `(mode × filament_present)`.
  Verdict BORROW NOW, M, firmware: a `PROBE:` command + use inside RELOAD FOLLOW_JAM / MMU-mode post-load.
- Analog sample chain: 5 ms sample × 5 count, 100 ms report (`mmu_buffer.py:202-204`). FLARE samples faster in
  firmware. SKIP.

## 5. Statistics / telemetry / diagnostics

- Swap timing buckets `pre_unload, form_tip, unload, post_unload, pre_load, load, purge, post_load, total`
  via `_wrap_track_time` on `toolhead.get_last_move_time()` (`mmu_controller.py:1406-1440`), lifetime + job +
  last; `swaps_since_pause` / `_record`; per-gate `EMPTY_GATE_STATS_ENTRY` = `pauses, loads, load_distance,
  load_delta, unloads, unload_distance, unload_delta, load_failures, unload_failures, quality`
  (`mmu_constants.py:343`); `quality = |1 − delta/dist|` needs an encoder (`mmu_filament_movement.py:3406`).
  Persisted in `mmu_vars.cfg` under `mmu_statistics_swaps` / `mmu_statistics_gate_<n>` / `mmu_statistics_counters`
  (`mmu_constants.py:267-275`).
  - FLARE daemon counts swaps/loads/unloads from `TC:DONE`/`LOADED`/`UNLOADED` (`flare_daemon.py:200-231`).
    Per-phase durations are derivable from the existing `TC:UNLOADING/CUTTING/SWAPPING/LOADING/DONE` events
    (MANUAL.md:374) with daemon timestamps. LATER, S, daemon: `phase_ms` histogram in SQLite + `MMU_STATS`
    text; per-lane `load_failures`/`unload_failures` from `TC:ERROR:<reason>`.
- Maintenance counters (rec #8): `MMU_STATS COUNTER=<name> [INCR=n] [LIMIT=n WARNING=".." PAUSE=1] [RESET=1]
  [DELETE=1]` (`commands/mmu_stats.py:34-47, 61-105`); over-limit prints the warning each time and optionally
  pauses. FLARE has a cutter with `EV:CUT:DONE`: daemon should count `cutter_cuts` and expose a limit/warning
  (blade life). BORROW NOW, S, daemon + mock (`MMU_STATS COUNTER=...` passthrough).
- Encoder clog detection has no buffer-only analogue beyond FlowGuard (§1.2) — FlowGuard *is* HH's buffer-only
  clog/tangle detector and is what FLARE's dwell timers already approximate.
- Sync telemetry: per-gate `sync_<gate>.jsonl` written by the controller (`mmu_sync_controller.py:1637-1700`),
  plotted by `utils/plot_sync_feedback.sh` using the **same** controller module (`utils/README.md`). FLARE has
  `flare_analyze.py` on `ST:`/`EV:` streams. SKIP (equivalent).
- `flow_rate` for type-P = `min(1, rd_tuned/rd_current)·100` (`mmu_sync_feedback.py:687-689`) — surfaced as
  `sync_feedback_flow_rate` and drawn by both UIs (`MmuFlowguardMeter.vue:205-209`). FLARE analogue:
  `100·min(1, baseline_sps/current_sps)` (or `est_sps/current_sps`). BORROW NOW as part of rec #1/#4.

## 6. Host / Klipper integration surface

`printer.mmu` keys HH emits (`mmu_controller.py:631-720` + sub-status from `sync_feedback.get_status`
`:370-420`, `gate_maps.get_status`, `extruder_wrapper.get_status`, `selector.get_status`, `sensor_manager`,
`encoder`, `espooler`, `drying_state`, `nfc`). Recent commits that matter for compatibility:
- `60ed80f` 2026-08-17 "Keep MMU status fields stable during startup": `encoder`, `sync_feedback_*`, `flowguard`,
  `tangle_prevention` are always present (None before init) so Moonraker subscriptions are stable.
- `4d343c9` 2026-09-06 "Publish complete MMU status schema before ready".
- `07cadc4` 2026-08-17 "Expose selector status on printer.mmu" (`status['selector']`).
- `ee6bb5c` 2026-08-14 `printer.mmu_machine.happy_hare_version` added "for easy UI behavior switching"
  (`extras/mmu_machine.py:105`). FLARE's `mmu_machine` mock (`klipper/mmu.py:8-30`) lacks it — set it to a v3
  string FLARE actually emulates, or UIs may take v4 code paths.
- Keys Fluidd `develop` reads from `mmuState` (grep of `mixins/mmu.ts` + `widgets/mmu/*.vue`): `action,
  bowden_progress, clog_detection_enabled, drying_state, enabled, endless_spool_enabled, endless_spool_groups,
  espooler, espooler_active, extruder_filament_remaining, filament, filament_direction, filament_pos,
  filament_position, flowguard, gate, gate_color, gate_filament_name, gate_material, gate_speed_override,
  gate_spool_id, gate_status, gate_temperature, grip, has_bypass, is_homed, is_paused, last_tool,
  last_toolchange, next_tool, num_gates, num_toolchanges, operation, print_state, reason_for_pause, sensors,
  servo, slicer_tool_map, spoolman_support, sync_drive, sync_feedback_bias_modelled, sync_feedback_enabled,
  sync_feedback_flow_rate, sync_feedback_state, tool, toolchange_purge_volume, ttg_map, unit, encoder`.
  Mainsail `mixins/mmu.ts` additionally keys on `has_bypass, servo, grip, sync_drive, unit, espooler`.
  Missing in `klipper/mmu.py` today: see rec #4 (all except the gate/tool/sensor ones already present).
- Commands the UIs send that FLARE's mock does not register: `MMU_TEST_CONFIG QUIET=1 ...` (Fluidd maintenance
  dialog `MmuMaintenanceDialog.vue:539`), `MMU_LED QUIET=1` (`:524`), `MMU_GRIP`, `MMU_RELEASE`, `MMU_SERVO`,
  `MMU_LED_VARS`, `MMU_SOFTWARE_VARS`. Register no-op/ack stubs (S, klipper mock) so the dialogs don't error.
  `MMU_PRINT_START` / `MMU_PRINT_END` are also absent; users porting HH-style `PRINT_START` macros will call them
  — stub to `_FLARE_SYNC_TOOLHEAD` (S).
- Moonraker component (`components/mmu_server.py`): Spoolman "extra" fields `printer_name`, `mmu_gate_map`,
  `rfid` (multi-UID, `381822e`); `moonraker_push_lane_data` → Moonraker DB namespace `lane_data`,
  keys `lane<N>` with `{vendor_name,name,color,material,bed_temp,nozzle_temp,scan_time,td,lane,spool_id,
  filament_id}` (`:1746-1820`), consumed by OrcaSlicer's Klipper/AMS-style lane sync — rec #7, daemon can POST
  `/server/database/item?namespace=lane_data`. `cleanup_lane_data(num_gates)` (`:1822-1846`).
  Gcode preprocessor (placeholders for referenced tools/colors/temps/purge volumes, `MMU_SLICER_TOOL_MAP`
  injection) — FLARE has `scripts/gcode_marker.py`; the full preprocessor is a Klipper/Moonraker-plugin-shaped
  dependency. SKIP.
- Print state machine (`extras/mmu/mmu_print_state_machine.py`) states: `initialized, ready, started, printing,
  complete, cancelled, error, pause_locked, paused, standby`; FLARE daemon emits only `printing|ready`
  (`flare_daemon.py:1493-1499`). LATER, S, daemon: map Moonraker `print_stats.state` → `paused/complete/cancelled/
  error` for UI parity.

## 7. Config / UX patterns

- `mmu_parameters.cfg` is Jinja-templated by section with `[[PARAM_*]]` placeholders and `[% if
  MMU_HAS_SYNC_FEEDBACK_BUFFER %]` gating (`config/base/mmu_parameters.cfg:376-451`); every runtime tunable is a
  `ParamSpec(name, kind, default, section, limits, guard, validator, on_change)` (`mmu_base_parameters.py:43-68`)
  — the single table drives config parsing, `MMU_TEST_CONFIG` get/set/list, limits and live `on_change` hooks
  (e.g. `flowguard_max_relief` → `apply_flowguard_tuning`, `mmu_sync_feedback.py:751-758`). FLARE's
  `cmd_handle_set` (`protocol.c:1268`) is hand-written per key, with parity enforced by tests
  (`scripts/test_settings_parity.py`, `test_protocol_param_width.py`). A table-driven registry would remove the
  "SET without GET/dump/docs" class of bug (AGENTS.md rule 8). LATER, L, firmware — refactor, not a feature.
- `MMU_TEST_CONFIG QUIET=1` semantics and diff-vs-default listing: FLARE `flare_cmd.py --dump` covers it. SKIP.
- Calibration commands (`MMU_CALIBRATE_PSENSOR`, `_GEAR`, `_BOWDEN`, `_TOOLHEAD`) — FLARE has `CAL:PSF_*`.
  SKIP.
- LEDs (`extras/mmu/mmu_led_manager.py`): segments exit/entry/status/logo, functional effects `gate_status`,
  `filament_color`, `slicer_color`, timed effects with per-unit return-to-default timers, transient flashes
  (`:1-120`); action→effect table (`:112-238`), print-state→effect (`:239-320`). FLARE `main.c:472-510` has six
  fixed colours. LATER, S, firmware+daemon: `SET:LED:<lane>:<r,g,b>` so the daemon can paint the loaded spool's
  `gate_color_rgb` (already fetched from Spoolman) on the lane LED when idle; keep firmware colours for
  loading/TC/error/cutting.

## 8. Testing

- `test/` (47 files, 1339 tests, `make test`): a fake Klipper reactor/toolhead/MCU + fake Moonraker that runs the
  **real** HH modules; covers bootup, toolchange, endless spool, sensor runout, stepper current nesting, tangle
  prevention (`test/README.md`). FLARE's `tests/host/flare_sim` already follows the same principle (real
  `sync*.c`/`toolchange.c` against fakes). The HH pieces worth copying:
  - Status-schema test: `test_status_fields_exist_before_ready` (`60ed80f`, `test/test_mmu_bootup.py`) asserts
    every UI-facing key exists from the first query. FLARE: add an equivalent to
    `scripts/test_flare_mmu_status.py` over the rec #4 key list. BORROW NOW, S.
  - Plant: `SimplePrinterModel.measure()` stick-slip "chaos" (measured position moves toward true by a random
    jerk ≤ `chaos·maxrange` per tick) and switch hysteresis `thr_on/thr_off` (`utils/sync_feedback_sim.py:316-383`);
    `--sample-error` jitters update stride 100–125 %. FLARE `sim_plant.c` has slew/lag "stress" only
    (`sim_plant.h:10-20`). LATER, S, tests/host (rec #10) — directly exercises the type-P hunting scenario.
  - Interactive CLI sim (`tick`, `clog`, `tangle` commands, `_forced_extreme_test`, `:1022`) — FLARE's scenario
    files + `FORCE_STUCK` (`sim_scenario.h:49-60`) are equivalent. SKIP.

## Explicitly not worth it

- EKF / PD-on-x / autotune of rotation distance (§1): already rejected; HH itself only uses EKF for type-P and
  its autotune is gated behind cooldowns + certainty scoring (`:696-770`) — a lot of machinery for a firmware
  that measures position directly.
- Type-D mid-band estimator for control: HH has none (§1.3) — confirms FLARE's reverted attempt.
- `sensor_lag_mm` readiness gating: disabled by default in HH.
- Extruder-stopped decay: N/A to a ratio controller; FLARE's `SYNC_PSF_DECAY_SPS_PER_S` stays.
- Toolhead cutter macro (`_MMU_CUT_TIP`), tip-forming macro, Blobifier, purge volume calc, gcode preprocessor:
  Klipper/toolhead-side and slicer-side; FLARE's cutter is MMU-side and already sequenced in `cutter.c`.
- Runout timestamp windows / duplicate suppression: FLARE firmware is synchronous and masks sensors already.
- eSpooler, NFC/RFID arbiter, environment/heater/fan managers, multi-unit aggregation, selector types: hardware
  FLARE does not have (>2 lanes / selectors out of scope).
- Persisted `mmu_vars.cfg` variables: FLARE persists in flash (firmware) and SQLite (daemon); no gain.
- `_rd_deadband_frac`, `analog_report_time` sampling chain, `MMU_TEST_CONFIG` listing: covered or moot.

## Sources

- HH `ef8431c4` (2026-09-08): `extras/mmu/unit/mmu_sync_controller.py`, `extras/mmu/unit/mmu_sync_feedback.py`,
  `extras/mmu/unit/mmu_buffer.py`, `extras/mmu/mmu_sensor_utils.py`, `extras/mmu/mmu_sensor_manager.py`,
  `extras/mmu/mmu_controller.py`, `extras/mmu/mmu_filament_movement.py`, `extras/mmu/mmu_gate_maps.py`,
  `extras/mmu/mmu_constants.py`, `extras/mmu/mmu_led_manager.py`, `extras/mmu/mmu_print_state_machine.py`,
  `extras/mmu/unit/mmu_unit_parameters.py`, `extras/mmu/mmu_base_parameters.py`, `extras/mmu_machine.py`,
  `extras/mmu/commands/{mmu_sensor_runout,mmu_stats,mmu_test_config,mmu_test_runout,mmu_test_grip}.py`,
  `components/mmu_server.py`, `config/base/mmu_parameters.cfg`, `config/macros/{mmu_servo_cutter,mmu_cut_tip,
  mmu_sequence}.cfg`, `config/mmu_vars.cfg`, `test/README.md`, `utils/README.md`, `utils/sync_feedback_sim.py`;
  `git log` 2025-09 → 2026-09 (commits `60ed80f`, `4d343c9`, `07cadc4`, `ee6bb5c`, `2d95871`/`e55333c`,
  `99ca6c0`, `b656e94`, `69cbe00`, `c4cae8c`).
- Fluidd `develop` (fetched 2026-09-11): `src/mixins/mmu.ts`, `src/components/widgets/mmu/{MmuCard,
  MmuFlowguardMeter, MmuClogMeter, MmuMaintenanceDialog, MmuUnit, MmuUnitFooter, MmuFilamentStatus,
  MmuEditGateMapDialog, MmuSettings, MmuControls, MmuSpool, MmuGateStatus, MmuMachine}.vue`.
- Mainsail `develop` (fetched 2026-09-11): `src/components/mixins/mmu.ts`,
  `src/components/panels/Mmu/{MmuFlowguardMeter, MmuFilamentStatusSyncFeedback}.vue`.
- FLARE (working tree at `f404bba`): `firmware/src/{sync.c,sync_analog.c,sync_relay.c,toolchange.c,motion.c,
  settings_store.c,protocol.c,main.c}`, `klipper/mmu.py`, `scripts/flare_daemon.py`, `tests/host/{sim_plant.h,
  sim_scenario.h}`, `MANUAL.md`, `BEHAVIOR.md`, memory notes listed in the task brief.
