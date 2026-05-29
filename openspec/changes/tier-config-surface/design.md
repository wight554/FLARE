# Design — tier-config-surface

## The tier model

```
        │ config.ini │ persist │ SET/GET │ lives in        │ who owns it
────────┼────────────┼─────────┼─────────┼─────────────────┼──────────────────
T0 board│     ✗      │    ✗    │    ✗    │ config.h        │ firmware author
T1 hw   │     ✓      │    ✓    │    ✓    │ config.ini→tune │ commission once
T2 durbl│     ✓      │    ✓    │ ✓(+tuner)│ config.ini→tune │ operator / print
T3 const│     ✗      │    ✗    │ dev-only│ owning .c/.h    │ firmware author
```

T0 already exists (`config.h`: pins, `CONF_RSENSE_OHM`, `NUM_LANES`,
`EN_ACTIVE_LOW`) and is the precedent — `project-architecture` already says
*"Board pin assumptions shall live in config headers."* T3 extends the same
principle from board pins to internal control-loop constants.

## Decision rubric

Ask, in order:

1. **Is it a board constant** (pin, sense resistor, lane count)? → **T0**,
   `config.h`.
2. **Is it a physical fact of the motor/driver/wiring** such that a wrong value
   means the mechanism is mis-driven, not merely sub-optimal? → **T1**.
   (`full_steps`, `gear_ratio`, `rotation_distance`, `microsteps`, `run_/hold_current`,
   `driver_tbl/toff/hstrt/hend`, `dir_invert`, `stealthchop_threshold`.)
3. **Does the operator legitimately set it per-machine or per-print**, and is
   there a documented procedure (TUNING.md / live tuner / analyzer) for doing
   so? → **T2**. Two sub-kinds, same plumbing:
   - *geometry*: `dist_in_out`, `dist_out_y`, `dist_y_buf`, `buf_body_len`,
     `buf_switch_span_mm`, `buf_max_travel_mm`, `buf_sensor_type`,
     `enable_cutter`, `unload_cut`, `servo_*_us`, all Klipper `_FLARE_VARS`.
   - *subjective*: `baseline_rate`, `sync_compression_bias_frac`, flow schedule,
     `relay_catchup_frac`, `relay_neutral_frac`, feed/rev/auto/load/unload
     speeds, `runout_cooldown_ms`, `auto_mode`, `auto_preload`, `reload_mode`.
4. **Otherwise** it is an internal constant tuned once against the algorithm:
   → **T3**, demote to code.

## T3 inventory (~40 — demote to owning module)

```
EWMA / filter τ, α   baseline_alpha · baseline_variance_reject_frac ·
                     baseline_settle_count · baseline_cooldown_ms · baseline_cooldown_mm ·
                     buf_analog_alpha · buf_drift_ewma_tau_ms ·
                     est_alpha_min · est_alpha_max · psf_vel_alpha
estimator confidence est_sigma_hard_cap_mm · est_low_cf_warn_threshold ·
                     est_fallback_cf_threshold
variance blend       buf_variance_blend_frac · buf_variance_blend_ref_mm
drift observer       buf_drift_min_samples · buf_drift_apply_thr_mm ·
                     buf_drift_clamp_mm · buf_drift_apply_min_cf
relay collapse law   relay_collapse_delay_ms · relay_collapse_ramp_mult · relay_collapse_cap_ms
neutral creep        neutral_creep_timeout_ms · neutral_creep_rate_sps_per_s · neutral_creep_cap_frac
zone bias            zone_bias_base_rate · zone_bias_ramp_rate · zone_bias_max_rate
sync internals       sync_overshoot_neutral_extend · sync_reserve_integral_gain ·
                     sync_reserve_integral_clamp_mm · sync_reserve_integral_decay_ms ·
                     sync_tension_dwell_stop_ms · sync_tension_ramp_delay_ms ·
                     buf_predict_thr_ms · buf_hyst_ms
PSF internals        psf_ctrl_deadband · kd_psf · psf_soft_wall_start ·
                     psf_jump_norm_per_s · psf_stop_confirm_ms · psf_wall_sat_ms
telemetry            tension_risk_window_ms · tension_risk_threshold
dead / blocked       relay_min_flip_mm  (must stay 0.0 — see relay-min-flip deadlock)
```

## Frozen classification (task 1 — demotion scope)

Rule applied: a param stays **T2** (full path) only if TUNING.md / live tuner /
analyzer documents how and why an operator changes it, **or** it is a per-build
safety/workflow timeout an operator may legitimately extend. Otherwise **T3**.
Ties break to T2 (keep control). This list is the frozen scope; code touches
exactly these.

**T1 — hardware (keep):** `microsteps` · `rotation_distance` · `run_current` ·
`hold_current` · `full_steps_per_rotation` · `gear_ratio` · `dir_invert` ·
`interpolate` · `stealthchop_threshold` · `driver_tbl` · `driver_toff` ·
`driver_hstrt` · `driver_hend`

**T2 — geometry (keep):** `dist_in_out` · `dist_out_y` · `dist_y_buf` ·
`buf_body_len` · `buf_switch_span_mm` · `buf_max_travel_mm` · `buf_sensor_type` ·
`enable_cutter` · `unload_cut` · `servo_open_us` · `servo_close_us` ·
`servo_block_us` · `servo_settle_ms` · `buf_psf_max_comp` · `buf_psf_max_tens` ·
`buf_psf_neutral` · `buf_psf_goal`

**T2 — subjective / operator (keep):** `baseline_rate` ·
`sync_compression_bias_frac` · `flow_schedule_cap` (+`[flow_schedule.v1]`) ·
`relay_catchup_frac` · `relay_neutral_frac` · `feed_rate` · `rev_rate` ·
`auto_rate` · `sync_max_rate` · `sync_min_rate` · `global_max_rate` ·
`sync_kp_rate` · `sync_reserve_pct` · `load_max_mm` · `unload_max_mm` ·
`autoload_max_mm` · `autoload_retract_mm` · `unload_tension_block_ms` ·
`runout_cooldown_ms` · `auto_mode` · `auto_preload` · `reload_mode` ·
`follow_timeout_ms` · `reload_y_timeout_ms` · `reload_join_delay_ms` ·
`join_rate` · `press_rate` · `compression_rate` · `cut_feed_rate` ·
`cut_feed_mm` · `cut_length_mm` · `cut_amount` · `cut_feed_timeout_ms` ·
`cut_settle_timeout_ms` · `tc_timeout_cut_ms` · `tc_timeout_th_ms` ·
`tc_timeout_y_ms`

**T3 — DEMOTE to code (~45):**

| Group | Params |
|-------|--------|
| EWMA / α / τ | `baseline_alpha` · `baseline_variance_reject_frac` · `baseline_settle_count` · `baseline_cooldown_ms` · `baseline_cooldown_mm` · `buf_analog_alpha` · `psf_vel_alpha` · `buf_drift_ewma_tau_ms` · `est_alpha_min` · `est_alpha_max` |
| estimator confidence | `est_sigma_hard_cap_mm` · `est_low_cf_warn_threshold` · `est_fallback_cf_threshold` |
| variance blend | `buf_variance_blend_frac` · `buf_variance_blend_ref_mm` |
| drift observer | `buf_drift_min_samples` · `buf_drift_apply_thr_mm` · `buf_drift_clamp_mm` · `buf_drift_apply_min_cf` |
| relay collapse | `relay_collapse_delay_ms` · `relay_collapse_ramp_mult` · `relay_collapse_cap_ms` · `relay_min_flip_mm` (frozen 0.0) |
| neutral creep | `neutral_creep_timeout_ms` · `neutral_creep_rate_sps_per_s` · `neutral_creep_cap_frac` |
| zone bias | `zone_bias_base_rate` · `zone_bias_ramp_rate` · `zone_bias_max_rate` |
| sync integral / overshoot | `sync_overshoot_pct` · `sync_overshoot_neutral_extend` · `sync_reserve_integral_gain` · `sync_reserve_integral_clamp_mm` · `sync_reserve_integral_decay_ms` |
| sync tension guards | `sync_tension_dwell_stop_ms` · `sync_tension_ramp_delay_ms` |
| loop quant / shaping | `buf_predict_thr_ms` · `buf_hyst_ms` · `sync_tick_ms` · `ramp_tick_ms` · `sync_ramp_accel` · `sync_ramp_decel` · `buf_stab_rate` · `pre_ramp_rate` · `motion_startup_ms` · `global_max_accel` |
| PSF internals | `psf_ctrl_deadband` · `kd_psf` · `psf_soft_wall_start` · `psf_jump_norm_per_s` · `psf_stop_confirm_ms` · `psf_wall_sat_ms` |
| telemetry | `tension_risk_window_ms` · `tension_risk_threshold` |
| fallback / RELOAD internals | `ts_buf_fallback_ms` · `post_print_stab_delay_ms` · `reload_touch_settle_ms` · `reload_touch_boost_ms` · `reload_touch_floor_pct` · `reload_lean_factor` |
| already compile-only (drop config key) | `sync_cannot_refill_mm` · `sync_cannot_relieve_mm` · `sync_fault_hold_recovery_ms` |

**Explicit borderline rulings (recorded):**
- `sync_reserve_pct` → **T2** (reserve depth = operator feel). `sync_overshoot_pct`
  → **T3** (trailing-trim, algorithm-internal).
- `sync_kp_rate` → **T2** (documented analog type-P knob).
  `sync_ramp_accel/decel`, `sync_tick_ms`, `ramp_tick_ms` → **T3**
  (loop bandwidth/quantization, rig-validated).
- All cut/tc/reload *timeouts* → **T2** (per-build safety an operator may extend).
  `post_print_stab_delay_ms`, `reload_touch_*`, `reload_lean_factor`,
  `ts_buf_fallback_ms` → **T3** (internal algorithm timing, no operator procedure).
- `buf_stab_rate`, `pre_ramp_rate`, `motion_startup_ms`, `global_max_accel` →
  **T3** (internal motion shaping).

## Demotion mechanism (revised — two homes, one set of removals)

Every T3 param, regardless of home, loses the same plumbing: it is **removed
from `config.ini.example`, `settings_t`, `settings_save`/`load`/`defaults`, and
the release `SET:`/`GET:` surface**, and its dev `SET:` is wrapped in
`#ifdef FLARE_DEV_TUNING`. The T3 global keeps its name and is read unchanged
(e.g. `EST_ALPHA_MIN`), so no reader edits. One `SETTINGS_VERSION` bump covers
the `settings_t` shrink.

What differs is **where the value lives**, and this is forced by units:

- **Unit-independent T3 (literals: fracs, αs, `_ms`, `_mm`, counts, pct, ~37)** —
  value moves to a new `firmware/include/tune_internal.h` as a `#define`
  (this is the "move to header" goal). The key is **removed from
  `gen_config.py` `DEFAULTS`** and added to `DEPRECATED_KEYS`, so a stale
  `config.ini` warns-and-ignores. `main.c` initializes the global from the
  `tune_internal.h` `#define`.

- **Motor-dependent rate/accel T3 (~8: `sync_ramp_accel`, `sync_ramp_decel`,
  `zone_bias_base/ramp/max_rate`, `buf_stab_rate`, `pre_ramp_rate`,
  `global_max_accel`)** — these are converted to **SPS via the motor's
  `mm_per_step`** at config-gen time (`mm_min_to_sps`, `accel_to_step_sps`).
  A frozen literal would hard-code one motor's SPS and break any other motor, so
  the value **stays in `gen_config.py` `DEFAULTS` + `tune.h` `CONF_*`** (the
  motor-aware conversion is the whole point). The global stays
  `= CONF_*`-initialized. They are still removed from `config.ini.example` +
  persistence + release `SET:`/`GET:`; they remain valid-but-undocumented
  compile keys (NOT in `DEPRECATED_KEYS`, since `gen_config` still emits them).

So `tune.h` keeps two kinds of `CONF_*`: T1/T2 (also in config.ini + persist +
SET) and motor-dependent T3 (compile-only). `tune_internal.h` holds the
unit-independent T3 literals. The testable contract (spec) is unchanged: no T3
in `config.ini.example`, `settings_t`, or release `SET:`/`GET:`.

## Dev escape hatch — why `#ifdef`, not keep-SET

`flare_live_tuner.py` writes only `COMPRESSION_BIAS_FRAC` and `BASELINE_SPS`
(both T2). `flare_analyze.py` / `flare_baseline_recommender.py` emit
config.ini / flow-schedule text — also T2. **Nothing automated touches a T3
knob.** The sole consumer of T3 `SET:` is a human typing `flare_cmd.py SET:...`
at a bench. So:

```c
#ifdef FLARE_DEV_TUNING
    else if (!strcmp(base_param, "EST_ALPHA_MIN")) EST_ALPHA_MIN = clamp_f(fv, ...);
    ...   // ephemeral: no settings_t field, no SV:, reverts on reboot
#endif
```

Release builds drop these branches (smaller binary, smaller `SET:UNKNOWN`
surface). Dev builds get full live experimentation with **no persistence** — a
T3 value is the code's, not flash's, so a reboot always returns to the audited
constant. This is the honest version of the accidental state `EST_LOW_CF_*` /
`TENSION_RISK_*` are in today.

## Migration

- **`settings_t` shrink → one `SETTINGS_VERSION` bump** (e.g. 57 → 58). Paid
  once. Removing ~30 fields changes the layout, so existing flash is wiped to
  defaults on first boot after flashing — acceptable for a deliberate
  one-time surface change, and the demoted values are now compile-time
  constants anyway (the operator was never meant to own them).
- **Demoted-key manifest** — one list (e.g. `gen_config.py: DEPRECATED_KEYS`)
  consumed by both `gen_config.validate_known_keys` (warn + ignore, not
  `sys.exit(1)`) and `flare_cmd.py --config` (skip on dump). Prevents the
  failure mode where a pre-migration `config.ini` or a dumped config refuses to
  build.
- **Docs**: move the T3 entries out of `config.ini.example` and `TUNING.md`
  "Open Questions" into a short `## Internal constants (not user-tunable)` note
  pointing at the owning source files.

## The durable win

Today: tweaking `est_alpha_min` during tuning iteration = edit config.ini,
regen, and (because it's persisted) a `SETTINGS_VERSION` bump that wipes every
operator's calibration on update. After T3 demotion: edit one `static const` in
`sync.c`, recompile. Zero version bumps for the entire class of changes that
firmware authors actually make most often.

## Risks

- **Mis-tiering a genuinely operator-facing knob into T3** removes their control.
  Mitigated by the "documented procedure ⇒ T2" rule and the borderline table —
  no silent auto-classification.
- **A user relying on a persisted T3 value** loses it at the version bump. But a
  T3 value reverting to the audited default is the *intended* end state; if a
  machine truly needed a different value, that is a retune (separate change) or
  evidence the knob was mis-tiered.
