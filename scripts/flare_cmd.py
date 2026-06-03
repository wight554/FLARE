#!/usr/bin/env python3
"""
flare_cmd.py — send FLARE commands and/or dump the live device configuration.

SEND MODE (default):
    python3 scripts/flare_cmd.py [--port PORT] [--timeout S] CMD:PAYLOAD [CMD2 ...]

    Sends one or more commands in sequence.  Each command waits for OK:/ER:.
    Long-running commands (FL:, UL:, UM:, RL:, CU:, CX:) wait for the
    corresponding completion event. TC: and MV: return after OK so Klipper can
    run coordinated work while firmware motion continues.

DUMP MODE:
    python3 scripts/flare_cmd.py [--port PORT] --dump [--raw]

    Reads all GET-able parameters from the device and prints them in
    config.ini format (copy-paste ready).  Use --raw for a terse list.

POLL MODE:
    python3 scripts/flare_cmd.py [--port PORT] --poll MS

    Repeatedly sends the status command (?:) at the specified interval.
    Useful for live debugging of the control loop.

Exit codes: 0 = success, 1 = error or timeout.
"""
import argparse
import glob
import sys
import time
import urllib.request
import urllib.error
import json
import threading
import queue

try:
    import serial
except ImportError:
    print("flare_cmd: 'pyserial' not installed. Run: pip install pyserial", file=sys.stderr)
    sys.exit(1)

# ---------------------------------------------------------------------------
# Commands that must wait for a completion event rather than just OK:
# ---------------------------------------------------------------------------
COMPLETION_EVENTS = {
    'RL': (['EV:RELOAD:LOADED'], ['EV:TC:ERROR']),
    'CU': (['EV:CUT:DONE'], ['EV:CUT:ERROR']),
    'CX': (['EV:CUT:DONE'], ['EV:CUT:ERROR']),
}

# ---------------------------------------------------------------------------
# Parameters to dump, grouped by config snapshot section.
# Each entry: (serial_cmd, output_key, lane_aware)
# lane_aware=True  → fetched as CMD_L1 and CMD_L2, emitted as "key: L1, L2"
# lane_aware=False → fetched once, emitted as "key: VALUE"
# ---------------------------------------------------------------------------
DUMP_PARAMS = [
    # (serial_cmd,        config.ini key,           lane_aware)
    # --- Motor / TMC ---
    ("RUN_CURRENT_MA",    "run_current",             True),
    ("HOLD_CURRENT_MA",   "hold_current",            True),
    ("MICROSTEPS",        "microsteps",              True),
    ("ROTATION_DIST",     "rotation_distance",       True),
    ("FULL_STEPS",        "full_steps_per_rotation", True),
    ("GEAR_RATIO",        "gear_ratio",              True),
    ("INTERPOLATE",       "interpolate",             True),
    ("STEALTHCHOP",       "stealthchop_threshold",   True),
    # --- Chopper ---
    ("DRIVER_TBL",        "driver_tbl",              True),
    ("DRIVER_TOFF",       "driver_toff",             True),
    ("DRIVER_HSTRT",      "driver_hstrt",            True),
    ("DRIVER_HEND",       "driver_hend",             True),
    # --- Speeds ---
    ("FEED_RATE",         "feed_rate",               False),
    ("REV_RATE",          "rev_rate",                False),
    ("AUTO_RATE",         "auto_rate",               False),
    ("BUF_STAB_RATE",     "buf_stab_rate",           False),
    ("GLOBAL_MAX_RATE",   "global_max_rate",         False),
    ("SYNC_MAX_RATE",     "sync_max_rate",           False),
    ("SYNC_MIN_RATE",     "sync_min_rate",           False),
    ("JOIN_RATE",         "join_rate",               False),
    ("PRESS_RATE",        "press_rate",              False),
    ("COMPRESSION_RATE",     "compression_rate",           False),
    # --- Motion / Ramp ---
    ("STARTUP_MS",        "motion_startup_ms",       False),
    ("GLOBAL_MAX_ACCEL",  "global_max_accel",        False),
    ("RAMP_TICK_MS",      "ramp_tick_ms",            False),
    ("PRE_RAMP_RATE",     "pre_ramp_rate",           False),
    # --- Buffer sync ---
    ("BUF_SWITCH_SPAN",    "buf_switch_span_mm",       False),
    ("BUF_HYST",          "buf_hyst_ms",             False),
    ("SYNC_RAMP_ACCEL",   "sync_ramp_accel",         False),
    ("SYNC_RAMP_DECEL",   "sync_ramp_decel",         False),
    ("SYNC_TICK_MS",      "sync_tick_ms",            False),
    ("BASELINE_RATE",     "baseline_rate",           False),
    ("BASELINE_ALPHA",    "baseline_alpha",          False),
    ("BUF_PREDICT_THR_MS", "buf_predict_thr_ms",     False),
    ("SYNC_KP_RATE",      "sync_kp_rate",            False),
    ("SYNC_OVERSHOOT_PCT", "sync_overshoot_pct",     False),
    ("SYNC_RESERVE_PCT",  "sync_reserve_pct",        False),
    ("COMPRESSION_BIAS_FRAC",   "sync_compression_bias_frac", False),
    ("NEUTRAL_CREEP_TIMEOUT_MS", "neutral_creep_timeout_ms", False),
    ("NEUTRAL_CREEP_RATE",    "neutral_creep_rate_sps_per_s", False),
    ("NEUTRAL_CREEP_CAP",     "neutral_creep_cap_frac",      False),
    ("VAR_BLEND_FRAC",    "buf_variance_blend_frac", False),
    ("VAR_BLEND_REF_MM",  "buf_variance_blend_ref_mm", False),
    ("SYNC_AUTO_STOP",    "sync_auto_stop_ms",       False),
    ("POST_PRINT_STAB_MS", "post_print_stab_delay_ms", False),
    # --- Tension Hardening ---
    ("SYNC_TENSION_STOP_MS",  "sync_tension_dwell_stop_ms", False),
    ("SYNC_TENSION_RAMP_MS",  "sync_tension_ramp_delay_ms", False),
    ("SYNC_OVERSHOOT_NEUTRAL_EXT", "sync_overshoot_neutral_extend", False),
    # --- Integral Centering & Confidence ---
    ("SYNC_INT_GAIN",     "sync_reserve_integral_gain", False),
    ("SYNC_INT_CLAMP",    "sync_reserve_integral_clamp_mm", False),
    ("SYNC_INT_DECAY_MS", "sync_reserve_integral_decay_ms", False),
    ("EST_SIGMA_CAP",     "est_sigma_hard_cap_mm",     False),
    # est_low_cf_warn_threshold / est_fallback_cf_threshold: Tier-3 internal
    # (tune_internal.h); not dumped — release GET: is dev-build-only.
    ("RELAY_CATCHUP_FRAC", "relay_catchup_frac",       False),
    ("RELAY_NEUTRAL_FRAC", "relay_neutral_frac",       False),
    ("SYNC_RELAY_TRIM_STEP_SPS", "sync_relay_trim_step_sps", False),
    ("SYNC_RELAY_TRIM_CLAMP_SPS", "sync_relay_trim_clamp_sps", False),
    ("SYNC_COMPRESSION_DRAIN_FRAC", "sync_compression_drain_frac", False),
    ("SYNC_COMPRESSION_DRAIN_BUDGET_MM", "sync_compression_drain_budget_mm", False),
    ("SYNC_EST_ATTACK_ALPHA", "sync_est_attack_alpha", False),
    ("SYNC_TENSION_FAST_MM_S", "sync_tension_fast_mm_s", False),
    ("SYNC_TENSION_BURST_MS", "sync_tension_burst_ms", False),
    ("SYNC_TENSION_ESC_STEP_SPS", "sync_tension_esc_step_sps", False),
    ("SYNC_TENSION_ESC_RATIO", "sync_tension_esc_ratio", False),
    ("RELAY_MIN_FLIP_MM", "relay_min_flip_mm",         False),
    ("RELAY_COLLAPSE_DELAY_MS", "relay_collapse_delay_ms", False),
    ("RELAY_COLLAPSE_RAMP_MULT", "relay_collapse_ramp_mult", False),
    ("RELAY_COLLAPSE_CAP_MS", "relay_collapse_cap_ms",  False),
    # --- Smarter Sync ---
    ("EST_ALPHA_MIN",     "est_alpha_min",           False),
    ("EST_ALPHA_MAX",     "est_alpha_max",           False),
    ("ZONE_BIAS_BASE",    "zone_bias_base_rate",     False),
    ("ZONE_BIAS_RAMP",    "zone_bias_ramp_rate",     False),
    ("ZONE_BIAS_MAX",     "zone_bias_max_rate",      False),
    ("RELOAD_LEAN",       "reload_lean_factor",      False),
    # --- Physical Model ---
    ("DIST_IN_OUT",       "dist_in_out",             False),
    ("DIST_OUT_Y",        "dist_out_y",              False),
    ("DIST_Y_BUF",        "dist_y_buf",              False),
    ("BUF_BODY_LEN",      "buf_body_len",            False),
    ("BUF_MAX_TRAVEL",    "buf_max_travel_mm",       False),
    # --- Sync-Feedback Sensor ---
    ("BUF_SENSOR",        "buf_sensor_type",         False),
    ("BUF_HOME_STATE",    "buf_home_state",          False),
    ("BUF_PSF_MAX_COMP",  "buf_psf_max_comp",       False),
    ("BUF_PSF_MAX_TENS",  "buf_psf_max_tens",       False),
    ("BUF_PSF_NEUTRAL",   "buf_psf_neutral",        False),
    ("BUF_GOAL",          "buf_psf_goal",           False),
    ("BUF_ALPHA",         "buf_analog_alpha",       False),
    ("TS_BUF_MS",         "ts_buf_fallback_ms",     False),
    # --- Flow / Reload ---
    ("AUTO_MODE",         "auto_mode",               False),
    ("RELOAD_MODE",       "reload_mode",             False),
    ("RELOAD_Y_MS",       "reload_y_timeout_ms",     False),
    ("RELOAD_JOIN_MS",    "reload_join_delay_ms",    False),
    ("AUTO_PRELOAD",      "auto_preload",            False),
    # --- Safety ---
    ("AUTOLOAD_MAX",      "autoload_max_mm",         False),
    ("LOAD_MAX",          "load_max_mm",             False),
    ("UNLOAD_MAX",        "unload_max_mm",           False),
    ("RETRACT_MM",        "autoload_retract_mm",     False),
    ("RUNOUT_COOLDOWN_MS", "runout_cooldown_ms",     False),
    ("FOLLOW_MS",         "follow_timeout_ms",       True),
    # --- Timeouts ---
    ("TC_CUT_MS",         "tc_timeout_cut_ms",       False),
    ("TC_TH_MS",          "tc_timeout_th_ms",        False),
    ("TC_Y_MS",           "tc_timeout_y_ms",         False),
    # --- Cutter ---
    ("CUTTER",            "enable_cutter",           False),
    ("UNLOAD_CUT",        "unload_cut",              False),
    ("SERVO_OPEN",        "servo_open_us",           False),
    ("SERVO_CLOSE",       "servo_close_us",          False),
    ("SERVO_BLOCK",       "servo_block_us",          False),
    ("SERVO_SETTLE",      "servo_settle_ms",         False),
    ("CUT_FEED_MS",       "cut_feed_timeout_ms",     False),
    ("CUT_SETTLE_MS",     "cut_settle_timeout_ms",   False),
    ("CUT_FEED",          "cut_feed_mm",             False),
    ("CUT_LEN",           "cut_length_mm",           False),
    ("CUT_AMT",           "cut_amount",              False),
]

# Section boundaries for pretty-printing (config_key → section header)
SECTION_BREAKS = {
    "run_current":         "# ─── Motor / TMC (per-lane) ────────────────────────────────────────────────",
    "driver_tbl":          "# ─── TMC Chopper (per-lane) ────────────────────────────────────────────────",
    "feed_rate":           "# ─── Speeds (mm/min) ───────────────────────────────────────────────────────",
    "motion_startup_ms":   "# ─── Motion / Ramp ─────────────────────────────────────────────────────────",
    "buf_switch_span_mm":   "# ─── Buffer Sync ───────────────────────────────────────────────────────────",
    "sync_tension_dwell_stop_ms": "# ─── Tension Hardening ───────────────────────────────────────────",
    "sync_reserve_integral_gain": "# ─── Integral Centering & Confidence ───────────────────────────",
    "est_alpha_min":       "# ─── Smarter Sync (Estimator) ─────────────────────────────────────────────",
    "dist_in_out":         "# ─── Physical Model (mm) ─────────────────────────────────────────────────",
    "buf_sensor_type":     "# ─── Sync-Feedback Sensor ─────────────────────────────────────────────────",
    "auto_mode":           "# ─── Flow / Reload ────────────────────────────────────────────────────────",
    "autoload_max_mm":     "# ─── Safety ────────────────────────────────────────────────────────────────",
    "tc_timeout_cut_ms":   "# ─── Toolchange Timeouts ───────────────────────────────────────────────────",
    "enable_cutter":       "# ─── Cutter / Servo ────────────────────────────────────────────────────────",
}

BOOL_KEYS = {
    "interpolate",
    "auto_mode",
    "auto_preload",
    "enable_cutter",
    "unload_cut",
    "sync_overshoot_neutral_extend",
}


def format_dump_value(key, value):
    if value == "?":
        return value

    if key in {"run_current", "hold_current"}:
        try:
            return f"{int(value) / 1000.0:.3f}"
        except ValueError:
            return value

    if key in BOOL_KEYS:
        return "True" if value not in ("0", "False", "false") else "False"

    return value


# ---------------------------------------------------------------------------
# Daemon Proxy Integration
# ---------------------------------------------------------------------------
DAEMON_URL = "http://127.0.0.1:8088"

def get_daemon_status():
    try:
        req = urllib.request.Request(f"{DAEMON_URL}/status")
        with urllib.request.urlopen(req, timeout=0.1) as response:
            if response.status == 200:
                return json.loads(response.read().decode('utf-8'))
    except Exception:
        pass
    return None

def send_daemon_cmd(cmd_str, timeout=10.0):
    try:
        data = json.dumps({"cmd": cmd_str}).encode('utf-8')
        req = urllib.request.Request(
            f"{DAEMON_URL}/cmd",
            data=data,
            headers={"Content-Type": "application/json"},
            method="POST"
        )
        with urllib.request.urlopen(req, timeout=timeout + 2.0) as response:
            if response.status == 200:
                res = json.loads(response.read().decode('utf-8'))
                return res.get("response")
    except Exception:
        pass
    return None

def sse_listener_thread(event_q, stop_event):
    try:
        req = urllib.request.Request(f"{DAEMON_URL}/telemetry")
        with urllib.request.urlopen(req, timeout=5.0) as response:
            while not stop_event.is_set():
                line = response.readline()
                if not line:
                    break
                line_str = line.decode('utf-8', errors='ignore').strip()
                if line_str.startswith("data:"):
                    data_json = line_str[5:].strip()
                    try:
                        data = json.loads(data_json)
                        event_q.put(data)
                    except Exception:
                        pass
    except Exception:
        pass

def run_send_daemon(args):
    has_complex = False
    for raw_cmd in args.cmd:
        verb = raw_cmd.split(':', 1)[0].upper()
        if verb in COMPLETION_EVENTS:
            has_complex = True
            break

    event_q = queue.Queue()
    stop_event = threading.Event()
    listener_t = None

    if has_complex:
        listener_t = threading.Thread(target=sse_listener_thread, args=(event_q, stop_event), daemon=True)
        listener_t.start()
        time.sleep(0.1)

    try:
        for raw_cmd in args.cmd:
            verb = raw_cmd.split(':', 1)[0].upper()
            events = COMPLETION_EVENTS.get(verb)

            while not event_q.empty():
                try:
                    event_q.get_nowait()
                except queue.Empty:
                    break

            resp = send_daemon_cmd(raw_cmd, timeout=args.timeout)
            if resp is None:
                print("flare_cmd: daemon command timeout or error", file=sys.stderr)
                sys.exit(1)

            print(resp, flush=True)
            if resp.startswith('ER:'):
                sys.exit(1)

            if events:
                ok_evs, err_evs = events
                deadline = time.time() + args.timeout
                got_completion = False

                while time.time() < deadline:
                    try:
                        evt = event_q.get(timeout=0.1)
                        if "event_type" in evt:
                            evt_type = evt["event_type"]
                            evt_data = evt.get("event_data", "")
                            evt_line = f"EV:{evt_type}"
                            if evt_data:
                                evt_line += f",{evt_data}"
                            print(evt_line, flush=True)

                            if any(evt_line.startswith(ev) for ev in err_evs):
                                sys.exit(1)
                            if any(evt_line.startswith(ev) for ev in ok_evs):
                                got_completion = True
                                break
                    except queue.Empty:
                        continue

                if not got_completion:
                    print("flare_cmd: completion timeout", file=sys.stderr)
                    sys.exit(1)
    finally:
        stop_event.set()
        if listener_t:
            listener_t.join(timeout=0.5)

def run_dump_daemon(args):
    results = {}
    errors = []

    for (cmd, key, lane_aware) in DUMP_PARAMS:
        if lane_aware:
            vals = []
            for suffix, label in [("_L1", "L1"), ("_L2", "L2")]:
                resp = send_daemon_cmd(f"GET:{cmd}{suffix}")
                if resp and resp.startswith("OK:"):
                    parts = resp.split(":", 2)
                    raw_val = parts[2] if len(parts) >= 3 else "?"
                    vals.append(format_dump_value(key, raw_val))
                else:
                    vals.append("?")
                    errors.append(f"GET:{cmd}{suffix} → {resp}")
            if len(set(vals)) == 1:
                results[key] = vals[0]
            else:
                results[key] = ", ".join(vals)
        else:
            resp = send_daemon_cmd(f"GET:{cmd}")
            if resp and resp.startswith("OK:"):
                parts = resp.split(":", 2)
                raw_val = parts[2] if len(parts) >= 3 else "?"
                results[key] = format_dump_value(key, raw_val)
            elif resp and "UNKNOWN_PARAM" in resp:
                continue  # demoted Tier-3 / dev-gated in this build — omit from dump
            else:
                results[key] = "?"
                errors.append(f"GET:{cmd} → {resp}")

    if args.raw:
        for (_, key, _) in DUMP_PARAMS:
            if key in results:
                print(f"{key}: {results[key]}")
    else:
        print("# FLARE live config dump (via daemon)")
        print(f"# Generated by flare_cmd.py --dump")
        print()
        for (_, key, _) in DUMP_PARAMS:
            if key not in results:
                continue
            if key in SECTION_BREAKS:
                print()
                print(SECTION_BREAKS[key])
            print(f"{key}: {results[key]}")

    if errors:
        print("\n# Warnings — some parameters could not be read:", file=sys.stderr)
        for e in errors:
            print(f"#   {e}", file=sys.stderr)

def run_poll_daemon(args):
    interval = args.poll / 1000.0
    print(f"# Polling status every {args.poll} ms (via daemon). Press Ctrl+C to stop.", flush=True)
    try:
        while True:
            start_time = time.time()
            status = get_daemon_status()
            if status:
                line_parts = []
                line_parts.append(f"LN:{status.get('active_lane', 0)}")
                line_parts.append(f"TC:{status.get('tc_state', 'UNKNOWN')}")
                line_parts.append(f"BP:{status.get('g_buf_pos', 0.0):.3f}")
                line_parts.append(f"BUF:{status.get('buf_state', 'NEUTRAL')}")
                line_parts.append(f"SM:{status.get('sync_enabled', 0)}")
                line_parts.append(f"ST:{status.get('sync_state', '?')}")
                line_parts.append(f"BL:{status.get('bl_arm', '?')}")
                line_parts.append(f"MM:{status.get('sps', 0.0):.3f}")
                line_parts.append(f"BF:{status.get('baseline_sps', 0.0):.3f}")
                line_parts.append(f"EST:{status.get('extruder_est_sps', 0.0):.3f}")
                line_parts.append(f"RE:{status.get('reserve_error_mm', 0.0):.3f}")
                line_parts.append(f"I1:{status.get('in1', 0)}")
                line_parts.append(f"O1:{status.get('out1', 0)}")
                line_parts.append(f"I2:{status.get('in2', 0)}")
                line_parts.append(f"O2:{status.get('out2', 0)}")
                line_parts.append(f"TH:{status.get('toolhead', 0)}")
                line_parts.append(f"YS:{status.get('y_split', 0)}")
                line_parts.append(f"RELOAD:{status.get('reload_mode', 0)}")
                print(f"OK:{','.join(line_parts)}", flush=True)
            else:
                print("ER:DAEMON_OFFLINE", flush=True)

            elapsed = time.time() - start_time
            sleep_time = max(0, interval - elapsed)
            time.sleep(sleep_time)
    except KeyboardInterrupt:
        print("\n# Stopped by user.")


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def find_serial_port():
    ports = glob.glob('/dev/tty.usbmodem*') + glob.glob('/dev/ttyACM*')
    if not ports:
        print("flare_cmd: no serial port found", file=sys.stderr)
        sys.exit(1)
    return ports[0]


def open_port(port):
    try:
        return serial.Serial(port, 115200, timeout=0.5)
    except Exception as e:
        print(f"flare_cmd: {e}", file=sys.stderr)
        sys.exit(1)


def send_recv(ser, cmd, timeout=3.0):
    """Send a command, return the first OK:/ER: response line."""
    ser.reset_input_buffer()
    ser.write(f"{cmd}\n".encode())
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line.startswith('OK:') or line.startswith('ER:') or line == 'OK':
            return line
    return None


# ---------------------------------------------------------------------------
# Send mode
# ---------------------------------------------------------------------------
def run_send(args):
    port = args.port or find_serial_port()
    ser  = open_port(port)

    for raw_cmd in args.cmd:
        verb = raw_cmd.split(':', 1)[0].upper()
        events = COMPLETION_EVENTS.get(verb)

        ser.write(f"{raw_cmd}\n".encode())
        deadline = time.time() + args.timeout
        got_ok   = False

        while time.time() < deadline:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if not line:
                continue
            print(line, flush=True)

            if line.startswith('ER:'):
                ser.close()
                sys.exit(1)

            if not got_ok and (line == 'OK' or line.startswith('OK:')):
                got_ok = True
                if events is None:
                    break          # simple command done
                continue

            if events and got_ok:
                ok_evs, err_evs = events
                if any(line.startswith(ev) for ev in err_evs):
                    ser.close()
                    sys.exit(1)
                if any(line.startswith(ev) for ev in ok_evs):
                    break
        else:
            print("flare_cmd: timeout", file=sys.stderr)
            ser.close()
            sys.exit(1)

    ser.close()


# ---------------------------------------------------------------------------
# Dump mode
# ---------------------------------------------------------------------------
def run_dump(args):
    port = args.port or find_serial_port()
    ser  = open_port(port)
    time.sleep(0.3)           # let the device settle after open

    results = {}              # config_key → value string (or "L1, L2")
    errors  = []

    for (cmd, key, lane_aware) in DUMP_PARAMS:
        if lane_aware:
            vals = []
            for suffix, label in [("_L1", "L1"), ("_L2", "L2")]:
                resp = send_recv(ser, f"GET:{cmd}{suffix}")
                if resp and resp.startswith("OK:"):
                    # Response format: OK:CMD:VALUE
                    parts = resp.split(":", 2)
                    raw_val = parts[2] if len(parts) >= 3 else "?"
                    vals.append(format_dump_value(key, raw_val))
                else:
                    vals.append("?")
                    errors.append(f"GET:{cmd}{suffix} → {resp}")
            # Collapse identical values: "5, 5" → "5"
            if len(set(vals)) == 1:
                results[key] = vals[0]
            else:
                results[key] = ", ".join(vals)
        else:
            resp = send_recv(ser, f"GET:{cmd}")
            if resp and resp.startswith("OK:"):
                parts = resp.split(":", 2)
                raw_val = parts[2] if len(parts) >= 3 else "?"
                results[key] = format_dump_value(key, raw_val)
            elif resp and "UNKNOWN_PARAM" in resp:
                continue  # demoted Tier-3 / dev-gated in this build — omit from dump
            else:
                results[key] = "?"
                errors.append(f"GET:{cmd} → {resp}")

    ser.close()

    # ---- Print ----
    if args.raw:
        for (_, key, _) in DUMP_PARAMS:
            if key in results:
                print(f"{key}: {results[key]}")
    else:
        print("# FLARE live config dump")
        print(f"# Generated by flare_cmd.py --dump  (port: {port})")
        print()
        prev_section = None
        for (_, key, _) in DUMP_PARAMS:
            if key not in results:
                continue
            if key in SECTION_BREAKS:
                print()
                print(SECTION_BREAKS[key])
            print(f"{key}: {results[key]}")

    if errors:
        print("\n# Warnings — some parameters could not be read:", file=sys.stderr)
        for e in errors:
            print(f"#   {e}", file=sys.stderr)


# ---------------------------------------------------------------------------
# Poll mode
# ---------------------------------------------------------------------------
def run_poll(args):
    port = args.port or find_serial_port()
    ser  = open_port(port)
    interval = args.poll / 1000.0

    print(f"# Polling status every {args.poll} ms on {port}. Press Ctrl+C to stop.", flush=True)
    try:
        while True:
            start_time = time.time()
            ser.write(b"?:\n")

            # Read until OK: response or ER:
            while True:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if not line:
                    break
                if line:
                    print(line, flush=True)
                if line.startswith('OK:') or line == 'OK' or line.startswith('ER:'):
                    break

            elapsed = time.time() - start_time
            sleep_time = max(0, interval - elapsed)
            time.sleep(sleep_time)
    except KeyboardInterrupt:
        print("\n# Stopped by user.")
    finally:
        ser.close()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description='Send FLARE commands or dump live device config.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument('--port',    help='Serial port (auto-detected if omitted)')
    parser.add_argument('--timeout', type=float, default=300.0,
                        help='Timeout for long-running commands (default: 300 s)')
    parser.add_argument('--dump',    action='store_true',
                        help='Read all parameters from device and print as config.ini')
    parser.add_argument('--raw',     action='store_true',
                        help='With --dump: print terse key: value lines without comments')
    parser.add_argument('--poll',    type=int, metavar='MS',
                        help='Repeatedly poll status (?:) at specified interval in ms')
    parser.add_argument('--api-port', type=int, default=8088,
                        help='Daemon HTTP API port (default: 8088)')
    parser.add_argument('cmd', nargs='*', help='FLARE command(s) to send')
    args = parser.parse_args()

    global DAEMON_URL
    DAEMON_URL = f"http://127.0.0.1:{args.api_port}"

    daemon_online = (args.port is None) and (get_daemon_status() is not None)

    if args.dump:
        if daemon_online:
            run_dump_daemon(args)
        else:
            run_dump(args)
    elif args.poll:
        if daemon_online:
            run_poll_daemon(args)
        else:
            run_poll(args)
    elif args.cmd:
        if daemon_online:
            run_send_daemon(args)
        else:
            run_send(args)
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == '__main__':
    main()
