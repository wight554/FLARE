# FLARE – USB Serial Command Reference

All communication is over USB CDC serial at 115200 baud (line-buffered, `\n` terminated).

```
Request:   CMD:PAYLOAD\n   (payload may be empty: CMD:\n or just CMD\n)
Response:  OK:DATA\n       (data absent if not applicable: OK\n)
           ER:REASON\n
Events:    EV:TYPE:DATA\n  (unsolicited, emitted any time)
```

Unsolicited `EV:` traffic is best-effort. Firmware drops events when USB CDC is not connected and rate-limits event emission to protect the control loop from serial backpressure.

---

## Firmware Identity

FLARE means **Filament Lane Automation and Reload Engine**. The name is scoped
to firmware and host tooling: it describes the controller logic that automates
filament lanes, coordinates cutting/unloading/loading, manages buffer sync, and
handles RELOAD failover. It is not a claim that the repo defines the complete
mechanical module.

Because the firmware is still in active development, command-line tools,
telemetry marker names, sidecar filenames, state directories, and Klipper
examples use the FLARE namespace directly. No compatibility aliases are
guaranteed until a stable release is declared.

---

## Operating Modes

FLARE behavior is controlled by two independent flags: **`AUTO_MODE`** (Flow Control) and **`RELOAD_MODE`** (Redundancy Control).

### 1. Flow Control (`AUTO_MODE`)
Controls whether the MMU handles internal breakpoints automatically or waits for the host.

- **Automated Flow (`AUTO_MODE:1`)** [Default]:
    - **Auto-Preload**: Inserting filament triggers a load to the OUT sensor (if `AUTO_PRELOAD` is 1).
    - **Auto-Sync**: Pulling the buffer arm (`BUF_TENSION`) automatically enables sync mode.
    - **Post-Load Sync**: Completing a `FL:` or `TC:` load automatically enables sync.
    - **Auto-Load**: If the MMU is empty, inserting filament triggers a full load to the toolhead.
- **Host-Controlled Flow (`AUTO_MODE:0`)**:
    - **Wait for Commands**: No unsolicited motion. FLARE only moves when it receives a serial command (`LO:`, `FL:`, `UL:`, `SM:1`, etc.).
    - **Status Only**: Emits runtime events (`EV:RUNOUT`, `EV:ACTIVE`, `EV:SYNC:...`, etc.) and waits for host instructions.

### 2. Redundancy Control (`RELOAD_MODE`)
Controls whether the MMU automatically swaps lanes on filament runout.

- **RELOAD Enabled (`RELOAD_MODE:1`)**:
    - **Auto-Swap**: If the active lane runs out, the controller automatically triggers a toolchange to the standby lane.
    - **Standalone Redundancy**: Designed to keep a print running without requiring host-side macros for runout recovery.
- **RELOAD Disabled (`RELOAD_MODE:0`)**:
    - **Standard MMU**: Runout events are reported to the host, but no autonomous swapping occurs.

> [!TIP]
> These flags can be combined. For example, `AUTO_MODE:0` + `RELOAD_MODE:1` allows a host to control all loading logic while still letting the MMU handle a runout swap autonomously if needed.

---

## Command Reference

### Motion Control
| Command | Mode | Description |
|---------|------|-------------|
| `T:n` | Both | **Select Active Lane** — set active lane to `1` or `2` without moving filament. |
| `LO:` | Manual| **Preload** — runs forward until OUT sensor triggers. Limit: `AUTOLOAD_MAX`. |
| `FL:` | Manual| **Full Load** — runs forward until `TS:1`, `TS_BUF_MS`, or sane buffer geometry reports loaded. Limit: `LOAD_MAX`. |
| `RL:` | Manual| **Reload Load** — manually triggers RELOAD sync. Pushes active lane to approach and follow a disconnected tail. |
| `UL:` | Both  | **Unload (Extruder)** — reverse until OUT sensor clears; when `UNLOAD_CUT=1` and the cutter is enabled, clears OUT, cuts, then clears OUT again. Limit: `UNLOAD_MAX`. |
| `UM:` | Both  | **Unload (MMU)** — reverse until IN sensor clears. If OUT is occupied at entry, first runs the `UL:` clear/cut/clear cycle; if only Y is occupied, it skips the cut. Limit: `UNLOAD_MAX`. |
| `TC:n` | Manual| **Toolchange** — Unload active lane and load lane `n`. |
| `MV:mm:F[:D]`| Both | **Exact Move** — move `abs(mm)` at `F` mm/min. Direction from sign of `mm` or optional `D` (`F`/`R`/`B`, `+`/`-`). Disables sync. |
| `FD:` | Both  | **Continuous Feed** — runs forward until `ST:`. |
| `BS:` | Both  | **Buffer Stabilize** — if the controller is idle, run the buffer neutralization move immediately to bring a dual-endstop buffer back toward `NEUTRAL`. |
| `ST:` | Both  | **Stop** — aborts all motion and resets toolchange state. |
| `CU:` | Both  | **Cut** — performs the full cutter sequence (Open -> Feed -> Close -> Open -> Repeat -> Block) on the active lane. |
| `CX:` | Both  | **Bare Cut** — performs the cutter sequence without filament movement (Open -> Close -> Open -> Repeat -> Block). |
| `CP:us` | Both  | **Cutter Position** — moves the cutter servo to the specified pulse width (400-2700 us) and stays there. Useful for mechanical tuning. |

### Status & Configuration
| Command | Response | Description |
|---------|----------|-------------|
| `?:` | Status | **Full Status** — returns all sensors, tasks, and rates. |
| `VR:` | Version| **Version** — returns firmware version. |
| `TS:<0\|1>`| OK | **Toolhead Sensor** — report toolhead filament status (sent by host). |
| `HD:<0\|1>`| OK | **Sync Hold** — `HD:1` suppresses sync and negative-sync following during tip-forming wiggles; `HD:0` releases it. `TS:1`, `TC:`, and `UL:` auto-clear hold. |
| `SM:<0\|1>`| OK | **Sync Mode** — manually toggle buffer sync. |
| `BI:<0\|1>`| OK | **Buffer Invert** — invert buffer endstop logic. |
| `MARK:<tag>` | `OK:MARK` | **Telemetry Marker** — stores a short host marker in firmware. Subsequent status replies expose it as `MK:<seq>:<tag>`. |
| `SV:` | OK | **Save Settings** — persist current runtime parameters to flash. Rejected with `ER:PERSIST_BUSY` while motion, toolchange, cutter activity, or buffer stabilization is active. |
| `LD:` | OK | **Load Settings** — reload persisted settings from flash. Rejected with `ER:PERSIST_BUSY` while motion, toolchange, cutter activity, or buffer stabilization is active. |
| `RS:` | OK | **Reset Settings** — restore defaults and save them to flash. Rejected with `ER:PERSIST_BUSY` while motion, toolchange, cutter activity, or buffer stabilization is active. |
| `CA:lane:ma` | OK | **Set Run Current** — immediately program the lane TMC run current in mA. |
| `BOOT:` | OK | **Reboot To BOOTSEL** — reboot into RP2040 USB boot mode for flashing. |

### Driver Access
| Command | Response | Description |
|---------|----------|-------------|
| `CW:lane:reg:val` | OK | **TMC Write** — write raw TMC register value. Bring-up / diagnostics only. |
| `TR:lane:reg` | `OK:lane:reg:0x...` | **TMC Read** — read raw TMC register value. |
| `RR:lane` | Probe dump | **UART Probe** — try TMC addresses `0..3` and return the raw reply frames for bring-up/debug. |

These commands are intended for low-level diagnostics and board bring-up. Prefer normal `SET:` / `GET:` parameters for supported runtime configuration.

---

## Parameters (`SET:` / `GET:`)

### Physical Model (Hardware Dimensions)
| Parameter | `config.ini` Key | Description | Default |
|-----------|------------------|-------------|---------|
| `DIST_IN_OUT` | `dist_in_out` | Distance between IN and OUT sensors | 150 |
| `DIST_OUT_Y` | `dist_out_y` | Distance between OUT sensor and Y-splitter | 100 |
| `DIST_Y_BUF` | `dist_y_buf` | Distance between Y-splitter and buffer entry | 300 |
| `BUF_BODY_LEN`| `buf_body_len`| Physical length of the buffer body/tube | 200 |
| `BUF_SWITCH_SPAN` | `buf_switch_span_mm` | Full switch-to-switch sensing span for the type-D buffer sensor | 10 |
| `BUF_MAX_TRAVEL` | `buf_max_travel_mm` | Full mechanical buffer travel | 25 |

### Speeds & Rates (mm/min)
| Parameter | `config.ini` Key | Description | Default |
|-----------|------------------|-------------|---------|
| `FEED_RATE` | `feed_rate` | Standard feeding speed | 3000 |
| `REV_RATE` | `rev_rate` | Standard retract speed | 3000 |
| `AUTO_RATE` | `auto_rate` | Preload speed (`LO:`) | 3000 |
| `BUF_STAB_RATE` | `buf_stab_rate` | Buffer stabilization speed for boot neutralization and UL tension-recovery move | 600 |
| `JOIN_RATE` | `join_rate` | RELOAD: Fast approach speed | 1600 |
| `PRESS_RATE` | `press_rate` | RELOAD: Slow follow-sync speed | 1200 |
| `GLOBAL_MAX_RATE` | `global_max_rate` | Absolute ceiling applied to every commanded motor rate; `SYNC_MAX_RATE` remains the sync-only soft cap under it | 4000 |
| `SYNC_MAX_RATE` | `sync_max_rate` | Max speed allowed during sync | 2200 |
| `BASELINE_RATE` | `baseline_rate` | Sync bootstrap target and scalar fallback baseline when no flow schedule is configured | 1600 |

### Smarter Sync (Estimator)
| Parameter | `config.ini` Key | Description | Default |
|-----------|------------------|-------------|---------|
| `SYNC_TICK_MS` | `sync_tick_ms` | Period between sync-controller updates | 20 |
| `SYNC_UP_RATE` | `sync_ramp_up_rate` | Max sync-speed increase applied each control tick | 40 |
| `SYNC_DN_RATE` | `sync_ramp_dn_rate` | Max sync-speed decrease applied each control tick | 80 |
| `BASELINE_ALPHA` | `baseline_alpha` | Settled-NEUTRAL baseline adaptation factor | 0.02 |
| `BUF_PREDICT_THR_MS` | `buf_predict_thr_ms` | NEUTRAL-dwell threshold used by tension prediction | 250 |
| `SYNC_KP_RATE` | `sync_kp_rate` | Proportional reserve-correction window around the virtual buffer target | 900 |
| `EST_ALPHA_MIN`| `est_alpha_min` | Estimator responsiveness for slow drifts | 0.12 |
| `EST_ALPHA_MAX`| `est_alpha_max` | Estimator responsiveness for sharp jumps | 0.65 |
| `SYNC_RESERVE_PCT` | `sync_reserve_pct` | Normal-sync reserve target as % of half `BUF_SWITCH_SPAN` toward compression | 35 |
| `COMPRESSION_BIAS_FRAC` | `sync_compression_bias_frac` | Scalar fallback compression-side setpoint shift when no flow schedule is configured (0.0 to 0.7) | 0.0 |
| `NEUTRAL_CREEP_TIMEOUT_MS` | `neutral_creep_timeout_ms` | Neutral-dwell wait before creep activates | 0 |
| `NEUTRAL_CREEP_RATE` | `neutral_creep_rate_sps_per_s` | Creep ramp slope (SPS/s) | 0 |
| `NEUTRAL_CREEP_CAP` | `neutral_creep_cap_frac` | Hard cap on creep as % of extruder_est_sps | 10 |
| `RELAY_CATCHUP_FRAC` | `relay_catchup_frac` | Type-D relay TENSION refill multiplier | 1.30 |
| `RELAY_NEUTRAL_FRAC` | `relay_neutral_frac` | Type-D relay NEUTRAL fallback/lean multiplier | 1.25 |
| `RELAY_MIN_FLIP_MM` | `relay_min_flip_mm` | Distance hysteresis for type-D flips; 0.0 keeps time-only `BUF_HYST` behavior | 0.5 |
| `VAR_BLEND_FRAC` | `buf_variance_blend_frac` | Max variance-aware blend fraction (0.0=OFF) | 0.0 |
| `VAR_BLEND_REF_MM` | `buf_variance_blend_ref_mm` | Sigma value at which blend distrust saturates | 1.0 |
| `ZONE_BIAS_BASE`| `zone_bias_base_rate`| Base reserve-recovery correction around the virtual buffer target (mm/min) | 90 |
| `ZONE_BIAS_RAMP`| `zone_bias_ramp_rate`| Extra reserve-recovery ramp while buffer stays away from target (mm/min per second) | 30 |
| `ZONE_BIAS_MAX` | `zone_bias_max_rate` | Max reserve-recovery correction (mm/min) | 600 |
| `RELOAD_LEAN`  | `reload_lean_factor` | RELOAD follow over-feed factor (0.0 to 5.0) | 1.15 |
| `LIVE_TUNE_LOCK` | _(runtime only)_ | Debug-only host live-write guard. The default observe-only tuner does not use it. `SET:LIVE_TUNE_LOCK:1` blocks live writes to `BASELINE_RATE`/`BASELINE_SPS`, `COMPRESSION_BIAS_FRAC`, `NEUTRAL_CREEP_*`, and `VAR_BLEND_*`/`BUF_VARIANCE_*`; `GET:LIVE_TUNE_LOCK` returns `0` or `1`. Not persisted; resets to `0` on boot. | 0 |

### Safety & Timeouts
| Parameter | `config.ini` Key | Description | Default |
|-----------|------------------|-------------|---------|
| `RAMP_TICK_MS` | `ramp_tick_ms` | Period between lane acceleration ramp steps | 5 |
| `LOAD_MAX` | `load_max_mm` | Max distance for `FL:` or **Auto-Load** | 3000 |
| `UNLOAD_MAX` | `unload_max_mm` | Max distance for `UL:`, `UM:` | 3000 |
| `RETRACT_MM` | `autoload_retract_mm` | Distance to retract after `LO:` triggers OUT, and after `UL:` clears OUT. 0 = stop immediately. | 5 |
| `UNLOAD_TENSION_BLOCK_MS` | `unload_tension_block_ms` | Stop `UL:` if buffer stays in `TENSION` for this long (printer blocking retraction). 0 = disabled. | 5000 |
| `AUTO_MODE` | `auto_mode` | Enable autonomous Flow (Auto-Sync, Toolhead load) | 1 |
| `AUTO_PRELOAD`| `auto_preload` | Enable parking preload on insertion | 1 |
| `RELOAD_MODE`| `reload_mode` | Enable autonomous RELOAD behavior (Auto-Swap) | 1 |
| `RUNOUT_COOLDOWN_MS` | `runout_cooldown_ms` | Cooldown before another runout can be reported on the same lane | 12000 |
| `SYNC_OVERSHOOT_PCT` | `sync_overshoot_pct` | Extra compression-side trim as percent of sync correction after reserve overshoots full (0..200) | 150 |
| `SYNC_OVERSHOOT_NEUTRAL_EXT` | `sync_overshoot_neutral_extend` | Extend compression overshoot trim into `BUF_NEUTRAL` when virtual position is below the deadband. | 1 |
| `SYNC_AUTO_STOP` | `sync_auto_stop_ms` | Auto-mode only: tail-assist stop after sustained `COMPRESSION`; in normal print sync, stops if continuous `COMPRESSION` dwell exceeds the timeout and recovery speed has collapsed to the minimum sync floor. | 5000 |
| `SYNC_TENSION_STOP_MS` | `sync_tension_dwell_stop_ms` | Hard stop if continuously pinned at tension endstop for this many ms. 0 = disable. | 6000 |
| `SYNC_TENSION_RAMP_MS` | `sync_tension_ramp_delay_ms` | Grace window before refill-assist overrides target to `SYNC_MAX_RATE`, bypassing the estimator ceiling. 0 = disable. | 0 |
| `SYNC_INT_GAIN` | `sync_reserve_integral_gain` | Integral reserve-centering gain (mm of target bias per mm·s of reserve error). **0.0 = disabled** by default. Enable with a small value (e.g. 0.005) after reviewing long-run soak logs. | 0.0 |
| `SYNC_INT_CLAMP` | `sync_reserve_integral_clamp_mm` | Maximum integral correction magnitude in mm. The integral cannot shift the effective reserve target by more than this amount. | 0.6 |
| `SYNC_INT_DECAY_MS` | `sync_reserve_integral_decay_ms` | Reserved for future integral decay rate. 0 = hold integral value when frozen. | 0 |
| `EST_SIGMA_CAP` | `est_sigma_hard_cap_mm` | Estimator sigma hard cap in mm. Confidence (`EC`) drops to 0 when the physics-based position uncertainty reaches this level. | 1.5 |
| `EST_LOW_CF_THR` | `est_low_cf_warn_threshold` | `EV:BUF,EST_LOW_CF` fires when estimator confidence falls below this threshold (runtime-only, not persisted). | 0.5 |
| `EST_FALLBACK_THR` | `est_fallback_cf_threshold` | Integral centering freezes when confidence falls below this threshold. Also the floor below which `EV:BUF,EST_FALLBACK` is eligible (runtime-only). | 0.2 |
| `BUF_DRIFT_TAU_MS` | `buf_drift_ewma_tau_ms` | EWMA time constant for per-transition residual drift estimate (ms). Longer = more stable; shorter = adapts faster. | 60000 |
| `BUF_DRIFT_MIN_SMP` | `buf_drift_min_samples` | Transition samples required for full-strength drift correction. When correction is explicitly enabled, it ramps in from the first sample to this count. | 3 |
| `BUF_DRIFT_THR_MM` | `buf_drift_apply_thr_mm` | Minimum `|BPD|` required to apply correction (mm). **0.0 = disabled**. Provisional print default applies correction only after meaningful observed drift. | 2.0 |
| `BUF_DRIFT_CLAMP` | `buf_drift_clamp_mm` | Hard clamp on applied drift correction magnitude in mm. Runtime range: 0.0–8.0. | 3.0 |
| `BUF_DRIFT_MIN_CF` | `buf_drift_apply_min_cf` | Minimum estimator confidence (`EC`/100) required to apply drift correction. Correction freezes (but EWMA continues accumulating) when below this. | 0.5 |
| `TENSION_RISK_WINDOW` | `tension_risk_window_ms` | Rolling window for `TPX` tension-pin density (ms). Runtime-only, not persisted. | 60000 |
| `TENSION_RISK_THR` | `tension_risk_threshold` | `EV:SYNC,TENSION_RISK_HIGH` fires when `TPX >= this`. 0 = disable. Runtime-only, not persisted. | 4 |
| `POST_PRINT_STAB_MS` | `post_print_stab_delay_ms` | Delay before idle+`COMPRESSION` recovery starts; once triggered, the low-speed post-print stabilization move settles the buffer back to `NEUTRAL` and only falls back to the tension-side handoff if it overshoots center. `0` starts immediately | 0 |
| `RELOAD_Y_MS` | `reload_y_timeout_ms` | Max time for tail to clear Y during RELOAD | 10000 |
| `RELOAD_JOIN_MS` | `reload_join_delay_ms` | Extra RELOAD-only settling delay after tail and Y clear before `RELOAD:JOINING` starts | 10000 |
| `STEALTHCHOP` | `stealthchop_threshold` | Velocity threshold (mm/min) for StealthChop. 0 = always SpreadCycle. | 500 |

`BASELINE_RATE` and `COMPRESSION_BIAS_FRAC` remain persistent scalar controls. If
`config.ini` has no `[flow_schedule.v1]` table, `scripts/gen_config.py`
synthesizes an exact one-point schedule from those scalar values. If a schedule
table is present, sync evaluates it at the live `extruder_est_sps` to obtain the
active baseline and compression bias; `SET:BASELINE_*` or `SET:COMPRESSION_BIAS_FRAC`
returns runtime behavior to the scalar one-point schedule until settings are
reloaded or firmware is reflashed.

Config-only schedule keys:

| `config.ini` Key | Description | Default |
|------------------|-------------|---------|
| `flow_schedule_cap` | Maximum generated schedule breakpoints, 1..16 | 8 |
| `[flow_schedule.v1] pointN` | Optional schedule rows: `flow_sps, baseline_sps, compression_bias_frac` (or bias milli 0..700). Breakpoints are sorted by flow and interpolated without extrapolation. | absent |

`BASELINE_RATE` remains a persistent bootstrap target. AUTO sync no longer rewrites it during startup.

Runtime status `BL` is the active control baseline after flow-schedule lookup
and any ephemeral live segment ratchet. `GET:` / `SET:` / `SV:` / `LD:` for
`BASELINE_RATE` operate on the configured bootstrap target.

Pre-rename half-travel and size serial tokens are removed; use full-range
`BUF_SWITCH_SPAN` and `BUF_MAX_TRAVEL`.

### Cutter / Servo
| Parameter | `config.ini` Key | Description | Default |
|-----------|------------------|-------------|---------|
| `UNLOAD_CUT` | `unload_cut` | Cut during unload sequences when the cutter is enabled | 0 |
| `SERVO_BLOCK` | `servo_block_us` | Servo block position used between cutter phases | 950 |
| `CUT_FEED_RATE` | `cut_feed_rate` | Motor speed (mm/min) during cutter feed; ramped from zero — lower if motor stalls | 1500 |
| `CUT_FEED_MS` | `cut_feed_timeout_ms` | Safety timeout for the cutter motor feed phase. Runtime range: 1000-120000 ms. Raise when long `CUT_FEED` distances would exceed the default. | 30000 |
| `CUT_SETTLE_MS` | `cut_settle_timeout_ms` | Safety timeout for cutter servo settle phases. Runtime range: 500-10000 ms. Must exceed `SERVO_SETTLE` for normal cutter phases to complete. | 3000 |

### Runtime-only Controls
| Parameter | Description |
|-----------|-------------|
| `GET:HOLD` | Returns `HOLD:1` while `HD:1` sync hold is active, else `HOLD:0`. |

### Diagnostic Status Fields

These fields are included in the `?:` response. Most diagnostics are appended
after `SS:`; `HD` appears with the core sync fields near `SM`.

| Field | Unit | Description |
|-------|------|-------------|
| `RT` | mm (signed) | Reserve target position. Negative = compression side. Set by `SYNC_RESERVE_PCT`, active flow-schedule bias (or scalar `COMPRESSION_BIAS_FRAC` fallback), and half of `BUF_SWITCH_SPAN`. |
| `HD` | bool | Sync HOLD state from `HD:1` / `HD:0`. |
| `CB` | % (int) | Active compression bias fraction × 100 after flow-schedule lookup. |
| `NC` | SPS | Neutral-zone creep component added to target rate |
| `VB` | % (int) | Variance blend distrust percentage |
| `BPV`| mm × 100 | Post-blend effective position used by control loops |
| `MK` | seq:tag | Telemetry marker tag and sequence number set by the most recent `MARK:` command. |
| `RD` | mm | Reserve deadband width around the target. |
| `TT` | ms | Time the buffer arm has been continuously pinned at the tension-side switch. Zero when not in `BUF_TENSION`. |
| `CT` | ms | Time the buffer arm has been continuously pinned at the compression-side switch. Zero when not in `BUF_COMPRESSION`. |
| `TW` | ms | Estimated time to compression wall (remaining physical margin ÷ current net push velocity). Capped at 99999 when not applicable or well out of range. |
| `EA` | ms | Age of the extruder velocity estimate — time since the estimator was last updated by a zone transition or bleed. |
| `SK` | enum | Active buffer sensor kind: `0` = virtual endstop, `1` = analog. |
| `CF` | 0.0–1.0 | Signal confidence from the active source. Below ~0.5 indicates saturation or stale data; the control loop treats values below 0.4 as unreliable. |
| `RI` | mm (signed) | Reserve integral term — slow centering correction added to the reserve target. |
| `RC` | 0–100 | Effective integral gain scalar (0 = frozen/disabled, 100 = active). |
| `ES` | mm | Estimator sigma — physics-based position uncertainty in mm. |
| `EC` | 0–100 | Estimator confidence based on sigma (independent of source `CF`). |
| `BPR` | mm (signed) | Last per-transition residual: `g_buf_pos − switch_pos_mm` measured just before the virtual position snaps to the switch threshold. Non-zero values indicate virtual/physical mismatch at that crossing. |
| `BPD` | mm (signed) | Drift EWMA — exponentially weighted average of `BPR` samples (time constant `BUF_DRIFT_TAU_MS`). A stable non-zero value indicates systematic virtual-position bias. |
| `BPN` | int | Number of zone transitions sampled into `BPD`. Drift correction ramps in until `BPN >= BUF_DRIFT_MIN_SMP`, then can apply at full configured strength away from the opposite wall. |
| `TPX` | int | Count of `BUF_TENSION` pin entries within the last `TENSION_RISK_WINDOW` ms. `EV:SYNC,TENSION_RISK_HIGH` fires when this reaches `TENSION_RISK_THR`. |
| `RDC` | 0–100 | Drift-correction activity scalar after confidence gating, sample ramp, clamp, and opposite-wall taper. `100` means correction is applying at the configured clamp; values can drop near a physical endstop so correction cannot hide the wall. |

---

## Events (`EV:`)

| Event | Data | Description |
|-------|------|-------------|
| `AUTO_LOAD` | `lane` | Automatic full-load was started because the controller was empty when filament was inserted. |
| `PRELOAD` | `lane` | Automatic preload-to-OUT was started on filament insertion. |
| `RUNOUT` | `lane` | Filament runout detected on specified lane. |
| `LOADED` | `lane` | Filament successfully reached the toolhead/gears. |
| `UNLOADED`| `lane` | Filament successfully retracted past the OUT or IN sensor. |
| `LOAD_TIMEOUT` | `lane` | A load task hit its configured distance limit before completion. |
| `UNLOAD_TIMEOUT` | `lane` | An unload task hit its configured distance limit before completion. |
| `MOVE_DONE` | `lane` | Exact move completed. |
| `ACTIVE` | `lane\|NONE`| Reported when the active lane changes. |
| `FAULT:DRY_SPIN`| `lane` | Motor spinning > 8s without filament (`IN` clear). |
| `SYNC` | `AUTO_START\|AUTO_STOP\|FAULT_HOLD\|FAULT_HOLD_RECOVERY\|TENSION_DWELL_WARN\|TENSION_RISK_HIGH` | Automatic sync state transitions. `FAULT_HOLD` fires on tension-dwell timeout or hard-wall critical; recovers automatically after `CONF_SYNC_FAULT_HOLD_RECOVERY_MS`. `TENSION_DWELL_WARN` fires when centering drift reaches a significant threshold. `TENSION_RISK_HIGH` fires when tension-pin density in the rolling window reaches `TENSION_RISK_THR`. |
| `BUF` | `DRIFT_RESET` | Drift EWMA was reset. Fires when sync stops, `EST_FALLBACK` occurs, or sensor is hot-swapped. Subsequent `BPN` will restart from 0. |
| `BUF` | `EST_LOW_CF\|EST_FALLBACK` | Buffer estimator events. `EST_LOW_CF` fires when confidence drops; `EST_FALLBACK` fires when sigma exceeds the hard cap. |
| `BUF_STAB` | `START\|DONE\|TIMEOUT` | Buffer neutralization started, reached `NEUTRAL`, or hit its safety timeout. |
| `BS` | Mode-specific snapshot | Periodic buffer/sync status event used during sync and RELOAD follow. |
| `TC:*` | Phase-specific | Toolchange progress events such as `TC:UNLOADING`, `TC:SWAPPING`, `TC:LOADING`, `TC:DONE`, `TC:ERROR`. |
| `RELOAD:*` | Phase-specific | RELOAD progress and fault events such as `RELOAD:SWITCHING`, `RELOAD:JOINING`, `RELOAD:LOADED`, `RELOAD:FAULT`. |
| `CUT:FEEDING` | — | Cutter feed phase started. |

### Fault Recovery
Most faults (`TIMEOUT`, sensor-related faults) are transient and reset on the next command.
**`FAULT:DRY_SPIN`** is sticky: it blocks automatic background tasks (Sync, RELOAD follow) to prevent motor wear. It clears automatically when a new spool is inserted (`IN` sensor triggers) or when a manual load command (`LO:`, `FL:`, etc.) is issued.

### Tuning Guide

For first-time tuning, two-profile capture, flow-schedule analysis, and
verification steps, use [TUNING.md](./TUNING.md). This manual remains the
command and field reference.

---

## Tools

### Calibration Workflow
Tuning is a calibration-time workflow, not a normal-print service.
The goal is to run several sidecar-tracked calibration prints, analyze the
telemetry, review a patch, bake accepted values into `config.ini`, flash
firmware, and then disconnect the host so FLARE runs standalone.

Before running the first 2.9.9 build, please back up your state file:
```bash
cp ~/flare-state/buckets-<id>.json ~/flare-state/buckets-<id>.json.schema2.bak
```

1. Find the Klipper API socket path on the Pi:
   ```bash
   ps -ef | grep '[k]lippy.py'
   ```
   Use the `-a` path. Common modern installs use
   `/home/pi/printer_data/comms/klippy.sock`.
2. Postprocess calibration G-code with a sidecar:
   ```bash
   python3 scripts/gcode_marker.py input.gcode --output input.flare.gcode \
       --emit sidecar
   ```
   By default, layer changes are recognized (both `;LAYER:<n>` and OrcaSlicer
   `;LAYER_CHANGE` comments). Use `--no-layer-markers` to disable.
3. Capture data using the observe-only tuner in daemon mode, emitting CSV:
   ```bash
   python3 scripts/flare_live_tuner.py --port /dev/ttyACM0 \
       --machine-id myprinter \
       --klipper-uds /home/pi/printer_data/comms/klippy.sock \
       --sidecar /home/pi/printer_data/gcodes/input.flare.json \
       --csv-out ~/flare-runs/run1.csv \
       --observe-daemon &
   ```
   `--klipper-mode auto` is the default. It tries UDS first and fails if the
   socket is unavailable. Use `--klipper-mode on` to require UDS.
4. After at least three calibration runs, analyze the CSV corpus and tuner
   state:
   ```bash
   python3 scripts/flare_analyze.py \
       --in ~/flare-runs/run1.csv ~/flare-runs/run2.csv ~/flare-runs/run3.csv \
       --state ~/flare-state/buckets-myprinter.json \
       --out config.patch.ini \
       --acceptance-gate
   ```
   `--mode safe` refuses to emit learned values when the state file has zero
   `LOCKED` buckets. `--mode aggressive` writes a loud pre-lock warning and
   low-confidence estimates. Use `--force` only for explicit bootstrap/debug
   work where you accept pre-lock estimates.
5. Review `config.patch.ini`. It is commented review text only; copy chosen
   values into `config.ini` by hand.
   For a deterministic flow-keyed schedule from two bracket profiles, run:
   ```bash
   python3 scripts/flare_analyze.py \
       --profile-fast ~/flare-runs/fast.csv \
       --profile-slow ~/flare-runs/slow.csv \
       --emit-flow-schedule \
       --out flow-schedule.ini
   ```
   Copy the reviewed `flow_schedule_cap` and `[flow_schedule.v1]` block into
   `config.ini`. Sparse inputs emit a one-point schedule equivalent to the
   scalar `baseline_rate` / `sync_compression_bias_frac` fallback.
6. Regenerate and flash:
   ```bash
   python3 scripts/gen_config.py
   ninja -C build_local
   bash scripts/flash_flare.sh
   ```
7. Update the watermark in your state file so drift tracking works:
   ```bash
   python3 scripts/flare_analyze.py --commit-watermark --state ~/flare-state/buckets-myprinter.json
   ```

The acceptance gate differentiates between hardware/math failures (**FAIL**) and
stale-configuration warnings (**WARN**). It compares the state-aware 
recommendation path once per "comparable" run. A run is comparable only if it 
contains at least 50 NEUTRAL rows for at least three contributing buckets. 

- **FAIL (Recommendation Unreliable)**: Triggered by high scatter 
  (sigma_p95 >= 5.0 mm), inconsistent recommendations between runs, 
  very low contributor mass (< 40% after ignoring sparse buckets), or having
  fewer than 2 comparable runs.
- **WARN (Config Stale / Immature)**: Triggered by actual scatter exceeding 
  the current config reference, contributor mass below 65%, low run counts
  (< 3), short print durations (< 30 min total or any run < 10 min), or
  having fewer than 3 LOCKED buckets.

Raw NEUTRAL-row coverage is reported as a diagnostic warning only. On FAIL, the 
analyzer still writes a patch with `Acceptance gate: FAIL` and prints explicit 
reasons to stderr.

### Live Tuner Modes
`scripts/flare_live_tuner.py` now defaults to observe-only. It reads status and
marker tags from the Klipper API sidecar path or shell-marker fallback, updates
bucket Kalman state, persists JSON, and emits a review patch at
`--commit-on-idle` or `--commit-on-finish`. It does not send `SET:`,
`SET:LIVE_TUNE_LOCK`, or `SV:` in default mode. Use `--observe-daemon` to run
continuously across prints.

Mode flags:

- default observe mode: no firmware writes, no save.
- `--allow-bias-writes`: debug-only live `SET:COMPRESSION_BIAS_FRAC` writes.
- `--allow-baseline-writes`: debug-only live `SET:BASELINE_SPS` writes.
- `--klipper-uds PATH`: Klipper API Unix socket path.
- `--klipper-mode auto|on`: auto-fallback or require UDS.
- `--sidecar PATH`: sidecar JSON generated by `gcode_marker.py`.
- `--recommend-recheck`: compares current buckets against watermark drift flags.
- `--prune-stale`: removes buckets not seen in >60 days.

Inspect state with:

```bash
python3 scripts/flare_live_tuner.py --machine-id myprinter --state-info --include-stale
python3 scripts/flare_live_tuner.py --machine-id myprinter --state-info --csv
python3 scripts/flare_live_tuner.py --machine-id myprinter --state-info --verbose
```

Bucket lock is cumulative across calibration runs: samples, run count, layer
count, and time spent in `NEUTRAL` must all pass before a bucket becomes `LOCKED`.
Very low `EST` samples below 100 steps/s and rail-clamped bias buckets are
tracked as diagnostics but excluded from lock/write eligibility.

FLARE uses residual-aware lock hysteresis. A locked bucket is no longer
unlocked by one moderate sample; the tuner waits for catastrophic mismatch,
sustained outlier streak, or sustained mean drift. Buckets with high residual
scatter remain `STABLE` with `wait=noise sigma/x=...` instead of locking and
chattering. Freshly locked buckets may briefly show `wait=dwell N/20` while the
new unlock detector gathers post-lock evidence.

`--state-info --verbose` appends residual diagnostics to the table:
`sigma2` is the residual-variance EWMA, `streak` is the current moderate
outlier streak, `dwell` is the number of samples observed since the last lock,
and `last_unlock` records `catastrophic`, `streak`, or `drift` when an unlock
actually occurs. With `--csv --verbose`, the same data is appended as
`resid_var_ewma,outlier_streak,locked_sample_count,last_unlock_reason`.

If a serial write fails in an explicit live-write mode, the tuner waits 1 s and
attempts to reopen the same port up to five times. If reconnect fails, it exits
non-zero and leaves the state file unchanged.
