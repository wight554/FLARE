# FLARE – USB Serial Command Reference

All communication is over USB CDC serial at 115200 baud (line-buffered, `\n` terminated).

```
Request:   CMD:PAYLOAD\n   (payload may be empty: CMD:\n or just CMD\n)
Response:  OK:DATA\n       (data absent if not applicable: OK\n)
           ER:REASON\n
Events:    EV:TYPE:DATA\n  (unsolicited, emitted any time)
```

Unsolicited `EV:` traffic is best-effort. Firmware drops events when USB CDC is not connected and rate-limits event emission to protect the control loop from serial backpressure (critical fault-class events are exempt from this budget limit).

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
| `T:n` | Both | **Select Active Lane** — set active lane to `1` or `2` without moving filament. Returns `ER:BUSY:LANE` if toolchange/motion is active. |
| `LO:` | Manual| **Preload** — runs forward until OUT sensor triggers. Limit: `AUTOLOAD_MAX`. Returns `ER:BUSY:LANE` if toolchange/motion is active. |
| `FL:` | Manual| **Full Load** — runs forward until `TS:1`, `TS_BUF_MS`, or sane buffer geometry reports loaded. Limit: `LOAD_MAX`. Returns `ER:BUSY:LANE` if toolchange/motion is active. |
| `RL:` | Manual| **Reload Load** — retriggers the automatic RELOAD sequence from the current physical state (use when the auto trigger missed or aborted). If the active lane has run out and the other lane is loaded, swaps to it (`RELOAD:SWITCHING`); otherwise resumes approach/follow on the active lane (`RELOAD:JOINING`) — unless the toolhead already confirms filament (a prior reload already completed, or the lane never ran out), in which case `RL:` is a no-op that just re-emits `RELOAD:LOADED` with no motion. With the extruder idle (paused/bench) the follow completes on staged compression — filament parked at the extruder mouth — instead of waiting for a tension grab, and does not raise `FOLLOW_JAM`. Returns `ER:NO_FILAMENT` if neither lane holds filament, `ER:BUSY:LANE` if toolchange/motion is active. |
| `UL:` | Both  | **Unload (Extruder)** — reverse until OUT clears. If `CUTTER=1` and `UNLOAD_CUT=1`, runs clear → cut → clear. Limit: `UNLOAD_MAX`. Returns `ER:BUSY:UNLOAD` if toolchange/motion is active. |
| `UM[:lane]` | Both  | **Unload (MMU)** — reverse until IN clears. `UM` / `UM:` unload the active lane; if fully loaded and `UNLOAD_CUT=1`, they run the full `UL:` cut sequence first, then continue to IN clear. `UM:1` / `UM:2` target a specific lane; inactive targets must be idle and preloaded (`IN=1`, `OUT=0`) and never run the cutter. Limit: `UNLOAD_MAX`. Returns `ER:BUSY:UNLOAD` if toolchange/motion is active. |
| `TC:n` | Manual| **Toolchange** — If `TH:1` is latched, wait for `TS:0`/`TC_TH_MS`, then unload active lane, cut if enabled, and load lane `n`. Returns `ER:BUSY:TC` if toolchange/motion is active. |
| `MV:mm:F[:D][:I]`| Both | **Exact Move** — move `abs(mm)` at `F` mm/min. Direction from sign of `mm` or optional `D` (`F`/`R`/`B`, `+`/`-`). Optional `I` ignores buffer compression/tension guards for this finite move. Disables sync. |
| `FD:` | Both  | **Continuous Feed** — runs forward until `ST:`. Returns `ER:BUSY:LANE` if toolchange/motion is active. |
| `BS:` | Both  | **Buffer Stabilize** — cancels compatible buffer service/sync/simple lane motion, then runs buffer neutralization to bring a dual-endstop buffer back toward `NEUTRAL`. Hard activities (`TC`, cutter, manual unload) still return `ER:BUSY:BL`. |
| `ST:` | Both  | **Stop** — aborts all motion and resets toolchange state. |
| `CU:` | Both  | **Cut** — performs the full cutter sequence (Open -> Feed -> Close -> Open -> Repeat -> Block) on the active lane. Requires both lanes idle and preloaded (`IN=1`, `OUT=0`); otherwise returns `ER:NOT_PRELOADED`. |
| `CX:` | Both  | **Bare Cut** — performs the cutter sequence without filament movement (Open -> Close -> Open -> Repeat -> Block). |
| `CP:us` | Both  | **Cutter Position** — moves the cutter servo to the specified pulse width (400-2700 us) and stays there. Useful for mechanical tuning. Returns `ER:BUSY:CUTTER` if not idle or in boot park. |

### Status & Configuration
| Command | Response | Description |
|---------|----------|-------------|
| `?:` | Status | **Full Status** — returns all sensors, tasks, and rates. |
| `VR:` | Version| **Version** — returns firmware version. |
| `TS:<0\|1>`| OK | **Toolhead Sensor** — report toolhead filament status (sent by host). |
| `BL:<T\|C>` | OK | **Buffer Lock** — arm the active lane to drive the buffer to the requested extreme and hold there. `BL:T` (tension, default) or `BL:C` (compression). The prime move is capped at `BUF_MAX_TRAVEL_MM / 2`; once at the extreme the lane holds with motor energized. On any external force (printer retract) the lock breaks automatically and the catch drive engages. Cancels active buffer stabilize or sync before arming; returns `ER:BUSY:BL` for hard activities or unrelated lane tasks. Use `BS` to release manually. |
| `SM:<0\|1>`| OK | **Sync Mode** — manually toggle buffer sync. |
| `CAL:PSF_COMP` | OK | **PSF Calibration** — sample current ADC fraction and store it as `BUF_PSF_MAX_COMP`. Rejected with `ER:PERSIST_BUSY` if controller activity (including buffer-lock motor motion) is in progress. |
| `CAL:PSF_TENS` | OK | **PSF Calibration** — sample current ADC fraction and store it as `BUF_PSF_MAX_TENS`. Rejected with `ER:PERSIST_BUSY` if controller activity (including buffer-lock motor motion) is in progress. |
| `CAL:PSF_NEUT` | OK | **PSF Calibration** — sample current ADC fraction and store it as `BUF_PSF_NEUTRAL`. Rejected with `ER:PERSIST_BUSY` if controller activity (including buffer-lock motor motion) is in progress. |
| `MARK:<tag>` | `OK:MARK` | **Telemetry Marker** — stores a short host marker in firmware. Subsequent status replies expose it as `MK:<seq>:<tag>`. |
| `SV:` | OK | **Save Settings** — persist current runtime parameters to flash. Rejected with `ER:PERSIST_BUSY` while motion (including buffer-lock motor motion), toolchange, cutter activity, or buffer stabilization is active. |
| `LD:` | OK | **Load Settings** — reload persisted settings from flash. Rejected with `ER:PERSIST_BUSY` while motion (including buffer-lock motor motion), toolchange, cutter activity, or buffer stabilization is active. |
| `RS:` | OK | **Reset Settings** — restore defaults and save them to flash. Rejected with `ER:PERSIST_BUSY` while motion (including buffer-lock motor motion), toolchange, cutter activity, or buffer stabilization is active. |
| `CA:lane:ma` | OK | **Set Run Current** — immediately program the lane TMC run current in mA. |
| `BOOT:` | OK | **Reboot To BOOTSEL** — reboot into RP2040 USB boot mode for flashing. |

### Driver Access
| Command | Response | Description |
|---------|----------|-------------|
| `TW:lane:reg:val` | OK | **TMC Write** — write raw TMC register value. Bring-up / diagnostics only. |
| `TR:lane:reg` | `OK:lane:reg:0x...` | **TMC Read** — read raw TMC register value. |
| `RR:lane` | Probe dump | **UART Probe** — try TMC addresses `0..3` and return the raw reply frames for bring-up/debug. |

These commands are intended for low-level diagnostics and board bring-up. Prefer normal `SET:` / `GET:` parameters for supported runtime configuration.

---

## Parameters (`SET:` / `GET:`)

> **Internal control-loop constants are dev-build-only.** EWMA alphas, estimator
> confidence/sigma, drift observer, relay-collapse law, neutral-creep, variance
> blend, est/zone-bias, sync integral/overshoot/tension guards, PSF
> derivative/filter terms, debounce/predict/tick windows, and the open-loop ramp
> rates are now compiled constants (`firmware/include/tune_internal.h`, or
> motor-aware `tune.h` `CONF_*`). A normal **release build does not expose them**
> via `SET:`/`GET:` — it replies `ER:SET:UNKNOWN_PARAM`, and `flare_cmd --config`
> omits them. Build with `-DFLARE_DEV_TUNING=ON` to re-expose them as **ephemeral**
> overrides (runtime-only, not persisted; revert on reboot). Affected keys:
> `SYNC_TICK_MS`, `RAMP_TICK_MS`, `BUF_HYST`, `BUF_PREDICT_THR_MS`,
> `BASELINE_ALPHA`, `BUF_ALPHA`, `BUF_STAB_RATE`, `PRE_RAMP_RATE`,
> `GLOBAL_MAX_ACCEL`, `SYNC_RAMP_ACCEL`/`SYNC_RAMP_DECEL`, `SYNC_OVERSHOOT_PCT`,
> `SYNC_OVERSHOOT_NEUTRAL_EXT`, `SYNC_INT_GAIN`/`SYNC_INT_CLAMP`/`SYNC_INT_DECAY_MS`,
> `SYNC_TENSION_STOP_MS`/`SYNC_TENSION_RAMP_MS`, `EST_SIGMA_CAP`, `EST_LOW_CF_THR`,
> `EST_FALLBACK_THR`, `EST_ALPHA_MIN`/`EST_ALPHA_MAX`,
> `ZONE_BIAS_BASE`/`ZONE_BIAS_RAMP`/`ZONE_BIAS_MAX`,
> `NEUTRAL_CREEP_TIMEOUT_MS`/`NEUTRAL_CREEP_RATE`/`NEUTRAL_CREEP_CAP`,
> `VAR_BLEND_FRAC`/`VAR_BLEND_REF_MM`, `RELAY_MIN_FLIP_MM`,
> `RELAY_COLLAPSE_DELAY_MS`/`RELAY_COLLAPSE_RAMP_MULT`/`RELAY_COLLAPSE_CAP_MS`,
> `POST_PRINT_STAB_MS`, `TS_BUF_MS`, `STARTUP_MS`, `RELOAD_LEAN`,
> `BUF_DRIFT_TAU_MS`/`_MIN_SMP`/`_THR_MM`/`_CLAMP`/`_MIN_CF`,
> `TENSION_RISK_WINDOW`/`TENSION_RISK_THR`.

### Physical Model (Hardware Dimensions)
| Parameter | `config.ini` Key | Description | Default |
|-----------|------------------|-------------|---------|
| `DIST_IN_OUT` | `dist_in_out` | Distance between IN and OUT sensors | 150 |
| `DIST_OUT_Y` | `dist_out_y` | Distance between OUT sensor and Y-splitter | 100 |
| `DIST_Y_BUF` | `dist_y_buf` | Distance between Y-splitter and buffer entry | 300 |
| `BUF_BODY_LEN`| `buf_body_len`| Physical length of the buffer body/tube | 200 |
| `BUF_SWITCH_SPAN` | `buf_switch_span_mm` | Full switch-to-switch sensing span for the buffer sensor | 10 |
| `BUF_MAX_TRAVEL` | `buf_max_travel_mm` | Full mechanical buffer travel | 25 |

### Sync-Feedback Sensor (PSF Analog)
| Parameter | `config.ini` Key | Description | Default |
|-----------|------------------|-------------|---------|
| `BUF_SENSOR` | `buf_sensor_type` | Sync-feedback sensor type (`0` = dual-switch, `1` = PSF analog) | 0 |
| `BUF_HOME_STATE` | `buf_home_state` | Buffer resting state when unloaded/homed (`0`=NEUTRAL, `1`=TENSION, `2`=COMPRESSION) | 0 |
| `BUF_PSF_MAX_COMP` | `buf_psf_max_comp` | Raw ADC fraction at compression extreme | 0.0 |
| `BUF_PSF_MAX_TENS` | `buf_psf_max_tens` | Raw ADC fraction at tension extreme | 1.0 |
| `BUF_PSF_NEUTRAL` | `buf_psf_neutral` | Raw ADC fraction at neutral calibration point | 0.5 |
| `BUF_GOAL` | `buf_psf_goal` | Raw ADC goal bias used by type-P zone control | 0.3 |
| `KD_PSF` | _(runtime only)_ | Type-P derivative gain: velocity damping applied to sync output (units: sps per normalised vel). Not persisted; resets to `0.0` on boot. | 0.0 |
| `SYNC_PSF_SLEW_PER_MM` | _(runtime only)_ | Type-P feed slew limit: max sps change per mm of filament moved. Lower = gentler feed accel. Not persisted. | 1500 |
| `SYNC_PSF_FILTER_MM` | _(runtime only)_ | Type-P feed target EMA length in mm (distance-based smoothing). Bigger = smoother. Not persisted. | 25.0 |
| `PSF_STAB_STAGNANT_MS` | `psf_stab_stagnant_ms` | Type-P buffer-stabilize dry-spin window after leaving a saturated rail. Live-tunable; not persisted. | 600 |
| `PSF_STAB_STAGNANT_NORM` | `psf_stab_stagnant_norm` | Minimum normalized buffer motion inside the stagnant window before `BUF_STAB:STAGNANT_TIMEOUT`. Live-tunable; not persisted. | 0.03 |
| `PSF_STAB_RAIL_BREAK_MS` | `psf_stab_rail_break_ms` | Max time type-P buffer-stabilize may drive while the analog signal remains saturated at a rail. Live-tunable; not persisted. | 3000 |

### Speeds & Rates (mm/min)
| Parameter | `config.ini` Key | Description | Default |
|-----------|------------------|-------------|---------|
| `FEED_RATE` | `feed_rate` | Standard feeding speed | 3000 |
| `REV_RATE` | `rev_rate` | Standard retract speed | 3000 |
| `AUTO_RATE` | `auto_rate` | Preload speed (`LO:`) | 3000 |
| `JOIN_RATE` | `join_rate` | RELOAD: Fast approach speed | 1600 |
| `PRESS_RATE` | `press_rate` | RELOAD: Slow follow-sync speed | 1200 |
| `GLOBAL_MAX_RATE` | `global_max_rate` | Absolute ceiling applied to every commanded motor rate; `SYNC_MAX_RATE` remains the sync-only soft cap under it | 4000 |
| `SYNC_MAX_RATE` | `sync_max_rate` | Max speed allowed during sync | 2200 |
| `BASELINE_RATE` | `baseline_rate` | Sync bootstrap target and scalar fallback baseline when no flow schedule is configured | 1600 |

### Smarter Sync (Estimator)
Operator-facing sync knobs only. Estimator/relay internals (`EST_ALPHA_*`,
`ZONE_BIAS_*`, `NEUTRAL_CREEP_*`, `VAR_BLEND_*`, `RELAY_MIN_FLIP_MM`,
`SYNC_TICK_MS`, `BASELINE_ALPHA`, `BUF_PREDICT_THR_MS`, `RELOAD_LEAN`, …) are
dev-build-only — see the note at the top of this section.
| Parameter | `config.ini` Key | Description | Default |
|-----------|------------------|-------------|---------|
| `SYNC_KP_RATE` | `sync_kp_rate` | Proportional reserve-correction window around the virtual buffer target | 900 |
| `SYNC_RESERVE_PCT` | `sync_reserve_pct` | Normal-sync reserve target as % of half `BUF_SWITCH_SPAN` toward compression | 65 |
| `COMPRESSION_BIAS_FRAC` | `sync_compression_bias_frac` | Scalar fallback compression-side setpoint shift when no flow schedule is configured (0.0 to 0.7) | 0.45 |
| `RELAY_CATCHUP_FRAC` | `relay_catchup_frac` | Type-D relay TENSION refill multiplier | 1.30 |
| `RELAY_NEUTRAL_FRAC` | `relay_neutral_frac` | Type-D relay NEUTRAL fallback multiplier (demand match) | 1.00 |
| `SYNC_RELAY_TRIM_STEP_SPS` | `sync_relay_trim_step_sps` | Runtime-only type-D anti-starvation trim step in raw SPS. TENSION touches add it; COMPRESSION touches do not subtract. | 300 |
| `SYNC_RELAY_TRIM_CLAMP_SPS` | `sync_relay_trim_clamp_sps` | Runtime-only type-D residual trim anti-windup clamp in raw SPS. | 2000 |
| `SYNC_COMPRESSION_DRAIN_FRAC` | `sync_compression_drain_frac` | Runtime-only type-D COMPRESSION active-draw drain fraction. `0.0` disables and restores legacy hard-stop; `>0` feeds this fraction of demand while `TASK_FEED` is active, clamped below demand. | 0.40 |
| `SYNC_COMPRESSION_DRAIN_BUDGET_MM` | `sync_compression_drain_budget_mm` | Runtime-only type-D COMPRESSION partial-drain distance budget. Once COMPRESSION relieve effort reaches this many mm, partial drain true-stops at `0`; `0.0` means immediate hard-stop. | 3.0 |
| `SYNC_EST_ATTACK_ALPHA` | `sync_est_attack_alpha` | Runtime-only type-D rising-demand EST attack alpha. Applies only when a type-D switch-crossing sample is above current `EST`; falling/equal samples keep the slower dwell EMA. Clamped 0.65 to 1.0. | 0.8 |
| `SYNC_TENSION_FAST_MM_S` | `sync_tension_fast_mm_s` | Runtime-only type-D TENSION crossing velocity (mm/s) that maps to full EST snap toward measured demand. Slow crossings keep the gentle fallback. | 2.0 |
| `SYNC_TENSION_PROBE_MAX` | `sync_tension_probe_max` | Runtime-only type-D recovery feed-floor latch ceiling in mm/min. The held floor snaps to demand on a TENSION touch and is capped here. Clamped 0 to 6000. | 3000 |
| `SYNC_TENSION_PROBE_UP` | `sync_tension_probe_up` | Runtime-only type-D recovery floor probe-up rate in mm/min per s, applied each tick while `BUF_TENSION` (still starved). Clamped 0 to 12000. | 3000 |
| `SYNC_TENSION_PROBE_DOWN` | `sync_tension_probe_down` | Runtime-only type-D recovery floor ease-off rate in mm/min per s, applied each tick while `BUF_COMPRESSION` (overfed). COMPRESSION, not a timeout, ends recovery. Clamped 0 to 12000. | 1200 |
| `SYNC_TENSION_PROBE_NEUTRAL` | `sync_tension_probe_neutral` | Runtime-only type-D recovery floor uncertainty-creep rate in mm/min per s, applied each tick while `BUF_NEUTRAL`. Creeps the floor up toward the safe COMPRESSION rail so metastable `feed == demand` resolves into a compression click instead of drifting into TENSION. `0` = hold (no creep). Clamped 0 to 12000. | 150 |
| `LIVE_TUNE_LOCK` | _(runtime only)_ | Debug-only host live-write guard. The default observe-only tuner does not use it. `SET:LIVE_TUNE_LOCK:1` blocks live writes to `BASELINE_RATE`/`BASELINE_SPS`, `COMPRESSION_BIAS_FRAC`, `SYNC_COMPRESSION_DRAIN_FRAC`, `SYNC_COMPRESSION_DRAIN_BUDGET_MM`, `SYNC_EST_ATTACK_ALPHA`, `SYNC_TENSION_*`, `NEUTRAL_CREEP_*`, and `VAR_BLEND_*`/`BUF_VARIANCE_*`; `GET:LIVE_TUNE_LOCK` returns `0` or `1`. Not persisted; resets to `0` on boot. | 0 |

For type-D branch A/B tests, capture with:

```bash
python3 scripts/flare_sync_check.py --live --poll 100 --mode asymmetric --branch-label partial-drain
```

The asymmetric analyzer is read-only and uses existing `OK:` fields `BP`, `BUF`,
`MM`, and `EST`. Verdict is PASS iff TENSION touches are zero.

### Safety & Timeouts
| Parameter | `config.ini` Key | Description | Default |
|-----------|------------------|-------------|---------|
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
| `EST_LOW_CF_THR` | `est_low_cf_warn_threshold` | `EV:BUF:EST_LOW_CF` fires when estimator confidence falls below this threshold (runtime-only, not persisted). | 0.5 |
| `EST_FALLBACK_THR` | `est_fallback_cf_threshold` | Integral centering freezes when confidence falls below this threshold. Also the floor below which `EV:BUF:EST_FALLBACK` is eligible (runtime-only). | 0.2 |
| `BUF_DRIFT_TAU_MS` | `buf_drift_ewma_tau_ms` | EWMA time constant for per-transition residual drift estimate (ms). Longer = more stable; shorter = adapts faster. | 60000 |
| `BUF_DRIFT_MIN_SMP` | `buf_drift_min_samples` | Transition samples required for full-strength drift correction. When correction is explicitly enabled, it ramps in from the first sample to this count. | 3 |
| `BUF_DRIFT_THR_MM` | `buf_drift_apply_thr_mm` | Minimum `|BPD|` required to apply correction (mm). **0.0 = disabled**. Provisional print default applies correction only after meaningful observed drift. | 2.0 |
| `BUF_DRIFT_CLAMP` | `buf_drift_clamp_mm` | Hard clamp on applied drift correction magnitude in mm. Runtime range: 0.0–8.0. | 3.0 |
| `BUF_DRIFT_MIN_CF` | `buf_drift_apply_min_cf` | Minimum estimator confidence (`EC`/100) required to apply drift correction. Correction freezes (but EWMA continues accumulating) when below this. | 0.5 |
| `TENSION_RISK_WINDOW` | `tension_risk_window_ms` | Rolling window for `TPX` tension-pin density (ms). Runtime-only, not persisted. | 60000 |
| `TENSION_RISK_THR` | `tension_risk_threshold` | `EV:SYNC:TENSION_RISK_HIGH` fires when `TPX >= this`. 0 = disable. Runtime-only, not persisted. | 4 |
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

Runtime status `BF` is the active control baseline after flow-schedule lookup
and any ephemeral live segment ratchet. `GET:` / `SET:` / `SV:` / `LD:` for
`BASELINE_RATE` operate on the configured bootstrap target.

Pre-rename half-travel and size serial tokens are removed; use full-range
`BUF_SWITCH_SPAN` and `BUF_MAX_TRAVEL`.

### Cutter / Servo
| Parameter | `config.ini` Key | Description | Default |
|-----------|------------------|-------------|---------|
| `UNLOAD_CUT` | `unload_cut` | Cut during automated toolchange and active-lane `UL:` / `UM:` unload sequences when the cutter is enabled. Inactive explicit `UM:n` standby eject never auto-cuts. | 0 |
| `SERVO_BLOCK` | `servo_block_us` | Servo block position used between cutter phases | 950 |
| `CUT_FEED_RATE` | `cut_feed_rate` | Motor speed (mm/min) during cutter feed; ramped from zero — lower if motor stalls | 1500 |
| `CUT_FEED_MS` | `cut_feed_timeout_ms` | Safety timeout for the cutter motor feed phase. Runtime range: 1000-120000 ms. Raise when long `CUT_FEED` distances would exceed the default. | 30000 |
| `CUT_SETTLE_MS` | `cut_settle_timeout_ms` | Safety timeout for cutter servo settle phases. Runtime range: 500-10000 ms. Must exceed `SERVO_SETTLE` for normal cutter phases to complete. | 3000 |
| `TC_CUT_MS` | `tc_timeout_cut_ms` | Outer toolchange cut watchdog. Firmware treats this as a minimum and extends it to fit configured cutter feed/settle duration. | 5000 |

### Runtime-only Controls
| Parameter | Description |
|-----------|-------------|
| `GET:BL` | Returns `BL:T`, `BL:C`, or `BL:0` for the current buffer-lock arm state. |

### Core Status Fields

These core fields are returned in the first part of the `?:` status response:

| Field | Unit | Description |
|-------|------|-------------|
| `LN`  | int  | Active lane (`1` or `2`). |
| `TC`  | string | Toolchange state name (e.g. `IDLE`, `UNLOADING`, `LOADING`, `ERROR`). |
| `L1T` | string | Lane 1 active task name. |
| `L2T` | string | Lane 2 active task name. |
| `I1`  | 0/1  | Lane 1 input filament sensor state (`0` = empty, `1` = present). |
| `O1`  | 0/1  | Lane 1 output filament sensor state (`0` = empty, `1` = present). |
| `I2`  | 0/1  | Lane 2 input filament sensor state (`0` = empty, `1` = present). |
| `O2`  | 0/1  | Lane 2 output filament sensor state (`0` = empty, `1` = present). |
| `TH`  | 0/1  | Toolhead filament sensor state (`0` = empty, `1` = present). |
| `YS`  | 0/1  | Y-splitter sensor state (`0` = empty, `1` = present). |
| `BUF` | string | Buffer physical zone status (`NEUTRAL`, `COMPRESSION`, `TENSION`, `UNKNOWN`). |
| `MM`  | mm/min | MMU feed motor rate. |
| `BF`  | mm/min | Active control baseline rate. |
| `BP`  | mm   | Buffer virtual position. |
| `SM`  | 0/1  | Sync mode enabled (`0` = disabled, `1` = enabled). |
| `BL`  | string | Buffer lock arm state (`T` = tension-armed, `C` = compression-armed, `0` = disarmed). |
| `ST`  | int  | Sync controller state. |
| `TPR` | 0/1  | Auto preload state (`0` = disabled, `1` = enabled). |
| `CU`  | 0/1  | Cutter enabled (`0` = disabled, `1` = enabled). |
| `RELOAD` | 0/1 | Reload mode enabled (`0` = disabled, `1` = enabled). |
| `UC`  | 0/1  | Unload cut state (`0` = disabled, `1` = enabled). |
| `BST` | 0/1  | Buffer sensor type (`0` = switch/type-D, `1` = analog/type-P). |
| `EST` | mm/min | Extruder estimated rate. |
| `RE`  | mm   | Reserve error. |
| `AV`  | mm/s | Buffer arm velocity. |
| `SC`  | mm/min | Stealthchop threshold velocity. |

### Diagnostic Status Fields

These extra diagnostic fields are returned in the second part of the `?:` status response:

| Field | Unit | Description |
|-------|------|-------------|
| `RT`  | mm (signed) | Reserve target position. Negative = compression side. |
| `TT`  | ms   | Time the buffer arm has been continuously pinned at the tension-side switch. |
| `CT`  | ms   | Time the buffer arm has been continuously pinned at the compression-side switch. |
| `SK`  | 0/1  | Active buffer sensor kind (`0` = virtual endstop, `1` = analog). |
| `CF`  | 0.0–1.0 | Signal confidence from the active source. |
| `ES`  | mm   | Estimator sigma — physics-based position uncertainty in mm. |
| `TPX` | int  | Count of `BUF_TENSION` pin entries within the last `TENSION_RISK_WINDOW` ms. `EV:SYNC:TENSION_RISK_HIGH` fires when this reaches `TENSION_RISK_THR`. |
| `CB`  | % (int) | Active compression bias fraction × 100 after flow-schedule lookup. |
| `BPV` | mm × 100 | Post-blend effective position used by control loops. |
| `MK`  | seq:tag | Telemetry marker tag and sequence number set by the most recent `MARK:` command. |
| `SYNC_REFILL_MM` | mm | Sync refill effort. |
| `SYNC_RELIEVE_MM` | mm | Sync relieve effort. |
| `TF`  | mm   | Total filament moved. |
| `FL_RATE` | mm/min | Feed rate limit. |
| `UL_RATE` | mm/min | Unload rate limit. |

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
| `UNLOAD:FAULT` | `CUT_FAILED\|OUT_BLOCKED` | Manual-unload terminal fault event. |
| `MOVE_DONE` | `lane` | Exact move completed. |
| `ACTIVE` | `lane\|NONE`| Reported when the active lane changes. |
| `FAULT:DRY_SPIN`| `lane` | Motor spinning > 8s without filament (`IN` clear). |
| `SYNC` | `AUTO_START\|AUTO_STOP\|FAULT_HOLD\|FAULT_HOLD_RECOVERY\|TENSION_DWELL_WARN\|TENSION_RISK_HIGH\|RELIEF_PAUSE\|NEUTRAL_CREEP_CAP\|cannot_refill\|cannot_relieve` | Automatic sync state transitions and warnings. `FAULT_HOLD` fires on tension-dwell timeout; recovers automatically. `TENSION_DWELL_WARN` fires when centering drift reaches a significant threshold. `TENSION_RISK_HIGH` fires when tension-pin density reaches `TENSION_RISK_THR`. `RELIEF_PAUSE` indicates a temporary relief pause; `NEUTRAL_CREEP_CAP` fires when creep velocity hits its cap; `cannot_refill` and `cannot_relieve` fire when sync limits are exceeded. |
| `BUF` | `DRIFT_RESET` | Drift EWMA was reset. Fires when sync stops, `EST_FALLBACK` occurs, or sensor is hot-swapped. Subsequent `BPN` will restart from 0. |
| `BUF` | `EST_LOW_CF\|EST_FALLBACK` | Buffer estimator events. `EST_LOW_CF` fires when confidence drops; `EST_FALLBACK` fires when sigma exceeds the hard cap. |
| `BUF_STAB` | `START\|DONE\|TIMEOUT\|STAGNANT_TIMEOUT\|REVERSE` | Buffer neutralization started, reached `NEUTRAL`, hit its safety timeout, stopped because the buffer did not track the stabilize move, or reversed direction to clear static friction. |
| `BL` | `PRIME\|LOCKED\|FOLLOW\|FOLLOW_DONE\|FOLLOW_GATED\|PRIME_BOUND\|TIMEOUT` | Buffer-lock sequence events. `PRIME` on prime start; `LOCKED` on successful lock; `FOLLOW` on following extruder; `FOLLOW_DONE` on follow finish; `FOLLOW_GATED` on early position gate; `PRIME_BOUND` on travel boundary; `TIMEOUT` on watchdog timeout. |
| `BS` | Mode-specific snapshot | Periodic buffer/sync status event used during sync and RELOAD follow. |
| `TC:*` | Phase-specific | Toolchange progress events such as `TC:UNLOADING`, `TC:SWAPPING`, `TC:LOADING`, `TC:DONE`, `TC:ERROR`. |
| `RELOAD:*` | Phase-specific | RELOAD progress and fault events such as `RELOAD:SWITCHING`, `RELOAD:JOINING`, `RELOAD:LOADED`, `RELOAD:FAULT`. |
| `CUT` | `FEEDING\|DONE\|ERROR` | Cutter execution events. `FEEDING` on feed start; `DONE` on successful cut; `ERROR` on cutter failure. |

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
3. Capture data using the tuner (observe-only by default; guarded SET writes via --allow-* flags) in daemon mode, emitting CSV:
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
   python3 scripts/flash_flare.py
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
