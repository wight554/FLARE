#!/usr/bin/env python3
"""
Generate firmware/include/tune.h from config.ini.
Includes all user-tunable firmware parameters.
Speeds are specified in mm/min.
"""

import configparser
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)

DEFAULT_CONFIG = os.path.join(REPO_ROOT, "config.ini")
DEFAULT_OUTPUT = os.path.join(REPO_ROOT, "firmware", "include", "tune.h")

MANDATORY = ("microsteps", "rotation_distance", "run_current")

# Tier-3 internal constants demoted out of config.ini (see tier-config-surface).
# A stale config.ini key listed here warns and is ignored rather than aborting
# the build, so an old config or a pre-demotion device dump still generates.
DEPRECATED_KEYS = {
    "est_low_cf_warn_threshold",
    "est_fallback_cf_threshold",
    "tension_risk_window_ms",
    "tension_risk_threshold",
    "buf_drift_ewma_tau_ms",
    "buf_drift_min_samples",
    "buf_drift_apply_thr_mm",
    "buf_drift_clamp_mm",
    "buf_drift_apply_min_cf",
    "sync_tension_dwell_stop_ms",
    "sync_tension_ramp_delay_ms",
    "sync_overshoot_neutral_extend",
    "sync_reserve_integral_gain",
    "sync_reserve_integral_clamp_mm",
    "sync_reserve_integral_decay_ms",
    "est_sigma_hard_cap_mm",
    "relay_min_flip_mm",
    "relay_collapse_delay_ms",
    "relay_collapse_ramp_mult",
    "relay_collapse_cap_ms",
    "neutral_creep_timeout_ms",
    "neutral_creep_rate_sps_per_s",
    "neutral_creep_cap_frac",
    "buf_variance_blend_frac",
    "buf_variance_blend_ref_mm",
    "kd_psf",
    "motion_startup_ms",
    "buf_hyst_ms",
    "buf_predict_thr_ms",
    "baseline_alpha",
    "sync_overshoot_pct",
    "post_print_stab_delay_ms",
    "est_alpha_min",
    "est_alpha_max",
    "reload_lean_factor",
    "buf_analog_alpha",
    "ts_buf_fallback_ms",
    "cut_feed_timeout_ms",
    "cut_settle_timeout_ms",
    "tc_timeout_cut_ms",
    "tc_timeout_th_ms",
    "tc_timeout_y_ms",
    "reload_y_timeout_ms",
    "sync_tension_burst_ms",
    "sync_tension_esc_step_sps",
    "sync_tension_esc_ratio",
}

# --- Defaults (merged from config.h and Klipper standards) ---
DEFAULTS = {
    # Motor / TMC
    "full_steps_per_rotation": "200",
    "gear_ratio": "50:17",
    "hold_current": "0.980",
    "interpolate": "False",
    "driver_tbl": "1",
    "driver_toff": "4",
    "driver_hstrt": "5",
    "driver_hend": "3",
    "stealthchop_threshold": "500",
    "dir_invert": "0",

    # Speeds (mm/min)
    "feed_rate": "3000",
    "rev_rate": "3000",
    "auto_rate": "3000",
    "buf_stab_rate": "600",
    "sync_max_rate": "2200",
    "global_max_rate": "5000",
    "sync_min_rate": "100",       # mm/min — quiet default. A HIGH floor (≈fast-segment rate) eliminates
                                  # the slow→fast TENSION skip but overfeeds slow sections into constant
                                  # COMPRESSION clicks (too loud for real prints). Slow-drift protection
                                  # moves to the type-d-dynamic-flow soft-wall lean; raise this only for the
                                  # optional loud zero-fast-step-skip mode. Floors only the type-D relay path.
    "pre_ramp_rate": "90",

    # Motion / Ramp
    "global_max_accel": "3500",   # mm/s² — raw lane accel for MV/AUTOLOAD/FEED/UNLOAD/BL.
    "ramp_tick_ms": "5",

    # Buffer Sync
    "buf_switch_span_mm": "10",
    "buf_psf_max_comp": "1.0",
    "buf_psf_max_tens": "0.0",
    "buf_psf_neutral": "0.5",
    "buf_psf_goal": "0.7",
    "sync_ramp_accel": "700",     # mm/s² — sync loop UP slew (type-D HW 2026-06-03; catch step-ups).
    "sync_ramp_decel": "700",     # mm/s² — sync loop DN slew (type-D HW: fast decel cuts COMPRESSION
                                  # overfeed noise on down-steps; was 300/150). Shared — review for type-P.
    "sync_tick_ms": "20",
    "sync_psf_slew_per_mm": "1500",   # type-P feed slew: max sps change per mm filament moved (lower = gentler)
    "sync_psf_filter_mm": "25.0",     # type-P feed target EMA length in mm (bigger = smoother)
    "baseline_rate": "1600",
    "baseline_settle_count": "3",
    "baseline_variance_reject_frac": "0.15",
    "baseline_cooldown_ms": "2000",
    "baseline_cooldown_mm": "5.0",
    "flow_schedule_cap": "8",
    "sync_fault_hold_recovery_ms": "5000",
    "sync_cannot_refill_mm": "50.0",
    "sync_cannot_relieve_mm": "50.0",
    "sync_kp_rate": "900",
    "sync_reserve_pct": "65",     # type-D HW 2026-06-03: compression-side step headroom (cliffs ~70). Shared.
    "sync_auto_stop_ms": "5000",
    # Type-D Relay Fallback Law
    "relay_catchup_frac": "1.30",
    # NEUTRAL feed = extruder_est_sps * this. 1.00 = demand match; switches act
    # as guardrails. Earlier 1.10/1.25 overfed NEUTRAL and produced
    # NEUTRAL->COMPRESSION->stop chatter once the type-D ramp could overshoot.
    # Raise slightly only if hardware soak shows steady TENSION drift.
    "relay_neutral_frac": "1.00",
    # Type-D crossing trim: TENSION touches add this many raw SPS; COMPRESSION
    # touches do not subtract. Smaller step = slower anti-starvation correction.
    "sync_relay_trim_step_sps": "300",
    "sync_relay_trim_clamp_sps": "2000",
    # Type-D COMPRESSION partial-drain guard. 0.0 = legacy hard-stop; >0 feeds
    # this demand fraction while TASK_FEED is active, clamped strictly below demand.
    "sync_compression_drain_frac": "0.40",
    # Stop partial drain after this much COMPRESSION relieve effort. 0.0 = hard-stop.
    "sync_compression_drain_budget_mm": "3.0",
    # Type-D rising-demand estimator attack. Bypasses EST_ALPHA_MAX only on up-steps.
    "sync_est_attack_alpha": "0.8",
    # Type-D fast TENSION recovery. Fast crossings snap EST toward measured demand;
    # each TENSION touch snaps a held feed floor up to measured demand; a
    # per-tick probe ramps it up while starved and backs it off in COMPRESSION
    # (symmetric AIMD hunt, no clock — COMPRESSION is the recovery-done signal).
    "sync_tension_fast_mm_s": "2.0",
    "sync_tension_probe_max": "3000",   # mm/min, latch ceiling
    "sync_tension_probe_up": "3000",    # mm/min per s, probe up while TENSION
    "sync_tension_probe_down": "1200",  # mm/min per s, ease off while COMPRESSION
    "sync_tension_probe_neutral": "150", # mm/min per s, uncertainty creep toward COMPRESSION while NEUTRAL
    # Drift Observer

    # Adaptive Sync
    "sync_compression_bias_frac": "0.45",
    "zone_bias_base_rate": "120",
    "zone_bias_ramp_rate": "45",
    "zone_bias_max_rate": "600",

    # Cutter / Servo
    "enable_cutter": "False",
    "unload_cut": "False",
    "servo_open_us": "500",
    "servo_close_us": "1400",
    "servo_block_us": "950",
    "servo_settle_ms": "500",
    "cut_feed_rate": "1500",
    "cut_feed_mm": "150",
    "cut_length_mm": "10",
    "cut_amount": "1",

    # Toolchange / Safety

    # Safety / Swap
    "runout_cooldown_ms": "12000",
    "load_max_mm": "3000",
    "unload_max_mm": "3000",
    "unload_tension_block_ms": "5000",
    "autoload_max_mm": "600",
    "autoload_retract_mm": "3",
    "auto_mode": "1",
    "auto_preload": "True",

    # Sync-Feedback Sensor (buf_psf_* live under "Buffer Sync" above)
    "buf_sensor_type": "0",
    "buf_home_state": "0",
    "psf_ctrl_deadband": "0.1",
    "psf_vel_alpha": "0.3",
    "psf_soft_wall_start": "0.8",
    "psf_jump_norm_per_s": "5.0",
    "psf_stop_confirm_ms": "200",
    "psf_wall_sat_ms": "1000",

    # Reload Mode
    "reload_mode": "1",
    "reload_join_delay_ms": "10000",
    "compression_rate": "90",
    "join_rate": "1600",
    "press_rate": "1200",
    "reload_touch_settle_ms": "120",
    "reload_touch_boost_ms": "900",
    "reload_touch_floor_pct": "90",
    "follow_timeout_ms": "10000",
    "dist_in_out": "150",
    "dist_out_y": "100",
    "dist_y_buf": "300",
    "buf_body_len": "200",
    "buf_max_travel_mm": "25",
}


def read_flat_ini(path):
    with open(path, "r") as f:
        content = f.read()
    first_content = ""
    for line in content.splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith(("#", ";")):
            first_content = stripped
            break
    if first_content and not first_content.startswith("["):
        content = "[DEFAULT]\n" + content
    cfg = configparser.ConfigParser(strict=False, inline_comment_prefixes=('#', ';'))
    cfg.read_string(content)
    params = dict(cfg.defaults())
    for section in cfg.sections():
        for key, val in cfg.items(section):
            params[key.lower()] = val
    return params, cfg


def valid_config_key(key):
    if key in DEFAULTS or key in MANDATORY:
        return True
    for suffix in ("_l1", "_l2"):
        base_key = key[:-len(suffix)]
        if key.endswith(suffix) and (base_key in DEFAULTS or base_key in MANDATORY):
            return True
    return False


def validate_known_keys(cfg, config_path):
    unknown = []
    deprecated = []

    def classify(key, label):
        if key in DEPRECATED_KEYS:
            deprecated.append(label)
        elif not valid_config_key(key):
            unknown.append(label)

    for key in cfg.defaults():
        classify(key, key)

    for section in cfg.sections():
        raw_section = getattr(cfg, "_sections", {}).get(section, {})
        for key in raw_section:
            if key == "__name__":
                continue
            if section == "flow_schedule.v1" and key.startswith("point"):
                continue
            classify(key, f"{section}.{key}")

    if deprecated:
        print(f"Warning: ignoring deprecated/demoted config key(s) in "
              f"{config_path}: {', '.join(deprecated)}", file=sys.stderr)

    if unknown:
        print(f"Error: unknown config key(s) in {config_path}: {', '.join(unknown)}")
        sys.exit(1)


def parse_gear_ratio(s):
    ratio = 1.0
    for part in s.split(","):
        nums = part.strip().split(":")
        if len(nums) == 2:
            ratio *= float(nums[0]) / float(nums[1])
    return ratio


def main():
    config_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_CONFIG
    output_path = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUTPUT

    if not os.path.exists(config_path):
        print(f"Error: {config_path} not found.")
        print(f"  Copy config.ini.example to config.ini and fill in your values.")
        sys.exit(1)

    raw, cfg = read_flat_ini(config_path)
    validate_known_keys(cfg, config_path)
    params = {**DEFAULTS, **raw}

    def get(key):
        return str(params.get(key, "")).strip()

    def get_bool(key):
        return get(key).lower() in ("true", "1", "yes", "on")

    def get_float(key):
        v = get(key)
        try:
            return float(v)
        except ValueError:
            return 0.0

    missing = [k for k in MANDATORY if not get(k)]
    if missing:
        print(f"Error: mandatory fields not set in {config_path}: {', '.join(missing)}")
        sys.exit(1)

    def get_list(key, default_val=""):
        val = get(key) or default_val
        return [p.strip() for p in val.split(",")]

    def get_motor_params(lane_idx):
        def gm(key, default=None):
            # 1. Check for suffixed override (e.g. run_current_l1)
            suffix = f"_l{lane_idx+1}"
            v = get(f"{key}{suffix}")
            if v: return v

            # 2. Check for global comma-separated list (e.g. run_current: 0.8, 0.9)
            #    Resolution order:
            #      a) parts[lane_idx]          — exact lane entry
            #      b) parts[0]                 — short list: first value covers all remaining lanes
            #      c) g_val (single value)     — no comma: value applies to every lane
            #      d) default
            g_val = get(key)
            if "," in g_val:
                parts = [p.strip() for p in g_val.split(",")]
                if lane_idx < len(parts):
                    return parts[lane_idx]
                # Fewer list entries than lanes → reuse first value for any extra lane
                return parts[0]
            # 3. Single value — apply to all lanes
            return g_val or default

        microsteps = int(gm("microsteps", "16"))
        rotation_distance = float(gm("rotation_distance", "0"))
        run_current = float(gm("run_current", "0.8"))
        full_steps = int(gm("full_steps_per_rotation", "200"))
        gear_ratio = parse_gear_ratio(gm("gear_ratio", "1:1"))
        hold_str = gm("hold_current")
        hold_current = float(hold_str) if hold_str else run_current / 2.0
        interpolate = (gm("interpolate", "True").lower() in ("true", "1", "yes", "on"))
        toff = int(gm("driver_toff", "3"))
        tbl = int(gm("driver_tbl", "2"))
        hstrt = int(gm("driver_hstrt", "5"))
        hend = int(gm("driver_hend", "0"))
        mm_per_step = rotation_distance / (full_steps * microsteps * gear_ratio) if rotation_distance > 0 else 0.0125
        stealthchop_threshold_mm_min = float(gm("stealthchop_threshold", "0"))
        stealthchop_sps = int(round(stealthchop_threshold_mm_min / 60.0 / mm_per_step)) if stealthchop_threshold_mm_min > 0 else 0
        
        # Direction
        dir_invert = int(gm("dir_invert", "0"))
        follow_timeout_ms = int(gm("follow_timeout_ms", "10000"))

        run_ma = int(round(run_current * 1000))
        hold_ma = int(round(hold_current * 1000))

        return {
            "microsteps": microsteps,
            "rotation_distance": rotation_distance,
            "full_steps": full_steps,
            "gear_ratio": gear_ratio,
            "run_ma": run_ma,
            "hold_ma": hold_ma,
            "interpolate": interpolate,
            "toff": toff,
            "tbl": tbl,
            "hstrt": hstrt,
            "hend": hend,
            "mm_per_step": mm_per_step,
            "stealthchop_sps": stealthchop_sps,
            "dir_invert": dir_invert,
            "follow_timeout_ms": follow_timeout_ms
        }

    # Generate for 2 lanes
    lanes = [get_motor_params(i) for i in range(2)]
    l1, l2 = lanes[0], lanes[1]

    def mm_min_to_sps(mm_min_str, m_params):
        mm_min = float(mm_min_str)
        if mm_min <= 0: return 0
        return int(round(mm_min / 60.0 / m_params["mm_per_step"]))

    def accel_to_step_sps(accel_mm_s2_str, tick_ms_str, m_params):
        accel = float(accel_mm_s2_str)
        tick_s = float(tick_ms_str) / 1000.0
        if accel <= 0 or tick_s <= 0: return 0
        return int(round(accel * tick_s / m_params["mm_per_step"]))

    def parse_flow_schedule():
        cap = int(get("flow_schedule_cap") or "8")
        if cap < 1:
            print("Error: flow_schedule_cap must be >= 1")
            sys.exit(1)
        if cap > 16:
            print("Error: flow_schedule_cap must be <= 16")
            sys.exit(1)

        points = []
        section = "flow_schedule.v1"
        if cfg.has_section(section):
            raw_section = getattr(cfg, "_sections", {}).get(section, {})
            for key in sorted(raw_section):
                if key == "__name__" or not key.startswith("point"):
                    continue
                parts = [p.strip() for p in raw_section[key].split(",")]
                if len(parts) != 3:
                    print(f"Error: {section}.{key} must be flow_sps, baseline_sps, bias_frac")
                    sys.exit(1)
                try:
                    flow_sps = int(round(float(parts[0])))
                    baseline_sps = int(round(float(parts[1])))
                    bias_raw = float(parts[2])
                except ValueError:
                    print(f"Error: {section}.{key} contains a non-numeric value")
                    sys.exit(1)
                bias_milli = int(round(bias_raw if abs(bias_raw) > 1.0 else bias_raw * 1000.0))
                points.append((flow_sps, baseline_sps, bias_milli))

        if not points:
            baseline_sps = mm_min_to_sps(get("baseline_rate"), l1)
            bias_milli = int(round(get_float("sync_compression_bias_frac") * 1000.0))
            points = [(baseline_sps, baseline_sps, bias_milli)]

        points.sort(key=lambda p: p[0])
        if len(points) > cap:
            print(f"Error: flow schedule has {len(points)} points but flow_schedule_cap is {cap}")
            sys.exit(1)

        prev_flow = None
        for flow_sps, baseline_sps, bias_milli in points:
            if flow_sps < 0 or baseline_sps < 0:
                print("Error: flow schedule flow_sps and baseline_sps must be >= 0")
                sys.exit(1)
            if prev_flow is not None and flow_sps <= prev_flow:
                print("Error: flow schedule flow_sps values must be strictly increasing")
                sys.exit(1)
            if bias_milli < 0 or bias_milli > 700:
                print("Error: flow schedule bias must be in 0.0..0.7 or 0..700 milli")
                sys.exit(1)
            prev_flow = flow_sps

        return cap, points

    flow_sched_cap, flow_sched = parse_flow_schedule()
    flow_sched_entries = ", ".join(
        f"{{{flow_sps}, {baseline_sps}, {bias_milli}}}"
        for flow_sps, baseline_sps, bias_milli in flow_sched
    )

    rel_config = os.path.relpath(config_path, REPO_ROOT)
    lines = [
        "#pragma once",
        "// AUTO-GENERATED — do not edit. Re-run: python3 scripts/gen_config.py",
        f"// Source: {rel_config}",
        "",
        "// --- Lane 1 parameters ---",
        f"#define CONF_L1_RUN_CURRENT_MA     {l1['run_ma']}",
        f"#define CONF_L1_HOLD_CURRENT_MA    {l1['hold_ma']}",
        f"#define CONF_L1_MICROSTEPS         {l1['microsteps']}",
        f"#define CONF_L1_ROTATION_DISTANCE  {l1['rotation_distance']:.7f}f",
        f"#define CONF_L1_GEAR_RATIO         {l1['gear_ratio']:.7f}f",
        f"#define CONF_L1_FULL_STEPS         {l1['full_steps']}",
        f"#define CONF_L1_MM_PER_STEP        {l1['mm_per_step']:.7f}f",
        f"#define CONF_L1_TOFF               {l1['toff']}",
        f"#define CONF_L1_TBL                {l1['tbl']}",
        f"#define CONF_L1_HSTRT              {l1['hstrt']}",
        f"#define CONF_L1_HEND               {l1['hend']}",
        f"#define CONF_L1_INTPOL             {'true' if l1['interpolate'] else 'false'}",
        f"#define CONF_L1_STEALTHCHOP_THRESHOLD {l1['stealthchop_sps']}",
        f"#define CONF_L1_FOLLOW_TIMEOUT_MS  {l1['follow_timeout_ms']}",
        "",
        "// --- Lane 2 parameters ---",
        f"#define CONF_L2_RUN_CURRENT_MA     {l2['run_ma']}",
        f"#define CONF_L2_HOLD_CURRENT_MA    {l2['hold_ma']}",
        f"#define CONF_L2_MICROSTEPS         {l2['microsteps']}",
        f"#define CONF_L2_ROTATION_DISTANCE  {l2['rotation_distance']:.7f}f",
        f"#define CONF_L2_GEAR_RATIO         {l2['gear_ratio']:.7f}f",
        f"#define CONF_L2_FULL_STEPS         {l2['full_steps']}",
        f"#define CONF_L2_MM_PER_STEP        {l2['mm_per_step']:.7f}f",
        f"#define CONF_L2_TOFF               {l2['toff']}",
        f"#define CONF_L2_TBL                {l2['tbl']}",
        f"#define CONF_L2_HSTRT              {l2['hstrt']}",
        f"#define CONF_L2_HEND               {l2['hend']}",
        f"#define CONF_L2_INTPOL             {'true' if l2['interpolate'] else 'false'}",
        f"#define CONF_L2_STEALTHCHOP_THRESHOLD {l2['stealthchop_sps']}",
        f"#define CONF_L2_FOLLOW_TIMEOUT_MS  {l2['follow_timeout_ms']}",
        "",
        "// --- Direction Inverts ---",
        f"#define CONF_L1_DIR_INVERT      {l1['dir_invert']}",
        f"#define CONF_L2_DIR_INVERT      {l2['dir_invert']}",
        "",
        "// --- Speeds (converted to SPS using Lane 1 baseline) ---",
        f"#define CONF_FEED_SPS           {mm_min_to_sps(get('feed_rate'), l1)}",
        f"#define CONF_REV_SPS            {mm_min_to_sps(get('rev_rate'), l1)}",
        f"#define CONF_AUTO_SPS           {mm_min_to_sps(get('auto_rate'), l1)}",
        f"#define CONF_BUF_STAB_SPS       {mm_min_to_sps(get('buf_stab_rate'), l1)}",
        f"#define CONF_SYNC_MAX_SPS       {mm_min_to_sps(get('sync_max_rate'), l1)}",
        f"#define CONF_SYNC_MIN_SPS       {mm_min_to_sps(get('sync_min_rate'), l1)}",
        f"#define CONF_PRE_RAMP_SPS       {mm_min_to_sps(get('pre_ramp_rate'), l1)}",
        "",
        "// --- Motion / Ramp ---",
        f"#define CONF_RAMP_STEP_SPS      {accel_to_step_sps(get('global_max_accel'), get('ramp_tick_ms'), l1)}",
        f"#define CONF_RAMP_TICK_MS       {get('ramp_tick_ms')}",
        "",
        "// --- Buffer Sync ---",
        f"#define CONF_BUF_SWITCH_SPAN_MM {get_float('buf_switch_span_mm')}f",
        f"#define CONF_SYNC_RAMP_UP_SPS   {accel_to_step_sps(get('sync_ramp_accel'), get('sync_tick_ms'), l1)}",
        f"#define CONF_SYNC_RAMP_DN_SPS   {accel_to_step_sps(get('sync_ramp_decel'), get('sync_tick_ms'), l1)}",
        f"#define CONF_SYNC_TICK_MS       {get('sync_tick_ms')}",
        f"#define CONF_SYNC_PSF_SLEW_PER_MM  {get_float('sync_psf_slew_per_mm')}f",
        f"#define CONF_SYNC_PSF_FILTER_MM {get_float('sync_psf_filter_mm')}f",
        f"#define CONF_BASELINE_SPS       {mm_min_to_sps(get('baseline_rate'), l1)}",
        f"#define CONF_BASELINE_SETTLE_COUNT {get('baseline_settle_count')}",
        f"#define CONF_BASELINE_VARIANCE_REJECT_FRAC {get_float('baseline_variance_reject_frac')}f",
        f"#define CONF_BASELINE_COOLDOWN_MS {get('baseline_cooldown_ms')}",
        f"#define CONF_BASELINE_COOLDOWN_MM {get_float('baseline_cooldown_mm')}f",
        f"#define CONF_SYNC_FAULT_HOLD_RECOVERY_MS {get('sync_fault_hold_recovery_ms')}",
        f"#define CONF_SYNC_CANNOT_REFILL_MM {get_float('sync_cannot_refill_mm')}f",
        f"#define CONF_SYNC_CANNOT_RELIEVE_MM {get_float('sync_cannot_relieve_mm')}f",
        f"#define CONF_FLOW_SCHED_CAP     {flow_sched_cap}",
        "typedef struct { int flow_sps; int baseline_sps; int bias_milli; } flow_schedule_point_t;",
        f"#define CONF_FLOW_SCHED_LEN     {len(flow_sched)}",
        f"#define CONF_FLOW_SCHED         {{{flow_sched_entries}}}",
        f"#define CONF_GLOBAL_MAX_SPS      {mm_min_to_sps(get('global_max_rate'), l1)}",
        f"#define CONF_SYNC_KP_SPS        {mm_min_to_sps(get('sync_kp_rate'), l1)}",
        f"#define CONF_SYNC_RESERVE_PCT   {get('sync_reserve_pct')}",
        f"#define CONF_SYNC_AUTO_STOP_MS {get('sync_auto_stop_ms')}",
        f"#define CONF_SYNC_COMPRESSION_BIAS_FRAC {get_float('sync_compression_bias_frac')}f",
        f"#define CONF_RELAY_CATCHUP_FRAC {get_float('relay_catchup_frac')}f",
        f"#define CONF_RELAY_NEUTRAL_FRAC {get_float('relay_neutral_frac')}f",
        f"#define CONF_SYNC_RELAY_TRIM_STEP_SPS {get('sync_relay_trim_step_sps')}",
        f"#define CONF_SYNC_RELAY_TRIM_CLAMP_SPS {get('sync_relay_trim_clamp_sps')}",
        f"#define CONF_SYNC_COMPRESSION_DRAIN_FRAC {get_float('sync_compression_drain_frac')}f",
        f"#define CONF_SYNC_COMPRESSION_DRAIN_BUDGET_MM {get_float('sync_compression_drain_budget_mm')}f",
        f"#define CONF_SYNC_EST_ATTACK_ALPHA {get_float('sync_est_attack_alpha')}f",
        f"#define CONF_SYNC_TENSION_FAST_MM_S {get_float('sync_tension_fast_mm_s')}f",
        f"#define CONF_SYNC_TENSION_PROBE_MAX_SPS {mm_min_to_sps(get('sync_tension_probe_max'), l1)}",
        f"#define CONF_SYNC_TENSION_PROBE_UP_SPS_PER_S {mm_min_to_sps(get('sync_tension_probe_up'), l1)}",
        f"#define CONF_SYNC_TENSION_PROBE_DOWN_SPS_PER_S {mm_min_to_sps(get('sync_tension_probe_down'), l1)}",
        f"#define CONF_SYNC_TENSION_PROBE_NEUTRAL_SPS_PER_S {mm_min_to_sps(get('sync_tension_probe_neutral'), l1)}",
        f"#define CONF_ZONE_BIAS_BASE_SPS   {mm_min_to_sps(get('zone_bias_base_rate'), l1)}",
        f"#define CONF_ZONE_BIAS_RAMP_SPS_S {mm_min_to_sps(get('zone_bias_ramp_rate'), l1)}",
        f"#define CONF_ZONE_BIAS_MAX_SPS    {mm_min_to_sps(get('zone_bias_max_rate'), l1)}",
        "",
        "// --- Cutter / Servo ---",
        f"#define CONF_ENABLE_CUTTER      {1 if get_bool('enable_cutter') else 0}",
        f"#define CONF_UNLOAD_CUT        {1 if get_bool('unload_cut') else 0}",
        f"#define CONF_SERVO_OPEN_US      {get('servo_open_us')}",
        f"#define CONF_SERVO_CLOSE_US     {get('servo_close_us')}",
        f"#define CONF_SERVO_BLOCK_US     {get('servo_block_us')}",
        f"#define CONF_SERVO_SETTLE_MS    {get('servo_settle_ms')}",
        f"#define CONF_CUT_FEED_SPS       {mm_min_to_sps(get('cut_feed_rate'), l1)}",
        f"#define CONF_CUT_FEED_MM        {get('cut_feed_mm')}",
        f"#define CONF_CUT_LENGTH_MM      {get('cut_length_mm')}",
        f"#define CONF_CUT_AMOUNT         {get('cut_amount')}",
        "",
        "// --- Toolchange Timeouts ---",
        f"#define CONF_LOAD_MAX_MM            {get('load_max_mm')}",
        f"#define CONF_UNLOAD_MAX_MM          {get('unload_max_mm')}",
        f"#define CONF_UNLOAD_TENSION_BLOCK_MS    {get('unload_tension_block_ms')}",
        f"#define CONF_AUTOLOAD_MAX_MM        {get('autoload_max_mm')}",
        f"#define CONF_AUTOLOAD_RETRACT_MM    {get('autoload_retract_mm')}",
        f"#define CONF_RELOAD_JOIN_DELAY_MS  {get('reload_join_delay_ms')}",
        f"#define CONF_AUTO_MODE              {1 if get_bool('auto_mode') else 0}",
        f"#define CONF_AUTO_PRELOAD           {1 if get_bool('auto_preload') else 0}",
        "",
        "// --- Safety / Swap ---",
        f"#define CONF_RUNOUT_COOLDOWN_MS     {get('runout_cooldown_ms')}",
        "",
        "// --- Sync-Feedback Sensor ---",
        f"#define CONF_BUF_SENSOR_TYPE    {get('buf_sensor_type')}",
        f"#define CONF_BUF_HOME_STATE     {get('buf_home_state')}",
        f"#define CONF_BUF_PSF_MAX_COMP   {get_float('buf_psf_max_comp'):.3f}f",
        f"#define CONF_BUF_PSF_MAX_TENS   {get_float('buf_psf_max_tens'):.3f}f",
        f"#define CONF_BUF_PSF_NEUTRAL    {get_float('buf_psf_neutral'):.3f}f",
        f"#define CONF_BUF_GOAL           {get_float('buf_psf_goal'):.3f}f",
        f"#define CONF_PSF_CTRL_DEADBAND  {get_float('psf_ctrl_deadband'):.3f}f",
        f"#define CONF_PSF_VEL_ALPHA      {get_float('psf_vel_alpha'):.3f}f",
        f"#define CONF_PSF_SOFT_WALL_START {get_float('psf_soft_wall_start'):.3f}f",
        f"#define CONF_PSF_JUMP_NORM_PER_S {get_float('psf_jump_norm_per_s'):.3f}f",
        f"#define CONF_PSF_STOP_CONFIRM_MS {get('psf_stop_confirm_ms')}",
        f"#define CONF_PSF_WALL_SAT_MS     {get('psf_wall_sat_ms')}",
        "",
        "// --- TS Fallback ---",
        "",
        "// --- Reload Mode ---",
        f"#define CONF_RELOAD_MODE           {get('reload_mode')}",
        f"#define CONF_JOIN_SPS           {mm_min_to_sps(get('join_rate'), l1)}",
        f"#define CONF_PRESS_SPS          {mm_min_to_sps(get('press_rate'), l1)}",
        f"#define CONF_COMPRESSION_SPS       {mm_min_to_sps(get('compression_rate'), l1)}",
        f"#define CONF_RELOAD_TOUCH_SETTLE_MS {get('reload_touch_settle_ms')}",
        f"#define CONF_RELOAD_TOUCH_BOOST_MS  {get('reload_touch_boost_ms')}",
        f"#define CONF_RELOAD_TOUCH_FLOOR_PCT {get('reload_touch_floor_pct')}",
        f"#define CONF_FOLLOW_TIMEOUT_MS  {get('follow_timeout_ms')}",
        "",
        "// --- Physical Model ---",
        f"#define CONF_DIST_IN_OUT            {get('dist_in_out')}",
        f"#define CONF_DIST_OUT_Y             {get('dist_out_y')}",
        f"#define CONF_DIST_Y_BUF             {get('dist_y_buf')}",
        f"#define CONF_BUF_BODY_LEN           {get('buf_body_len')}",
        f"#define CONF_BUF_MAX_TRAVEL_MM      {get('buf_max_travel_mm')}",
        "",
    ]

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w") as f:
        f.write("\n".join(lines))

    print(f"Generated {os.path.relpath(output_path, REPO_ROOT)}")


if __name__ == "__main__":
    main()
