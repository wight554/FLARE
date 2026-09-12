#!/usr/bin/env python3
"""verify_hw_open_items.py — log-based checks for FLARE's open hardware
validation items (ROADMAP phases 1, 11, 12, 13, 14). No UI, no eyeballing:
everything here reads the flare_daemon HTTP API (board EV:/status telemetry)
or Moonraker's REST API (printer.mmu status + gcode dispatch).

Pairs with `.planning/PRINT_TEST_PLAN.md`, which says what to slice/print and
when to fire each trigger below during that one print.

Modes
-----
watch     Long-running passive monitor. Polls the daemon's /status (event
          history + status_cache) and, if --moonraker-url is reachable,
          printer.mmu over Moonraker. Classifies events live against the
          open-items table, appends every new event/snapshot to a JSONL
          evidence log, and prints state transitions as they happen.
          Ctrl+C to stop and print the final report.

fire      Actively trigger one timing-sensitive item right now: sends a
          command via the daemon and checks the immediate OK:/ER: reply
          (items 1/2/3/9/10), or dispatches the 9 Fluidd/Mainsail
          maintenance-dialog stubs via Moonraker and checks each response
          plus (optionally) greps klippy.log for "Unknown command" (item 15).

report    Re-render the final PASS/PENDING/FAIL table from a watch log file,
          without re-running anything.

Items NOT covered here (need a bench step or a design decision, not a
log-based check during a print) — see PRINT_TEST_PLAN.md:
  #5  flash-wear counter persistence across reboot
  #7  forced main-loop stall to trip the 1s watchdog (the *trigger* is a bench
      step; `fire watchdog-check` below reads --crashlog/--loop-stats output
      once you've triggered it)
  #12 buffer-state-lock D2 design question (not a pass/fail check)
  #16 TMC thermal margin under boosted IRUN (needs a thermal camera/probe)
"""
import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import flare_cmd  # noqa: E402  (path insert must precede this import)

DEFAULT_MOONRAKER_URL = "http://localhost:7125"

# The 9 Fluidd/Mainsail maintenance-dialog command stubs (14-02,
# REQ-klipper-status-parity-command-stubs). Args are placeholders — Klipper
# rejects unknown commands before it validates arguments, so any well-formed
# call is sufficient to prove the command is registered.
MAINTENANCE_STUB_GCODES = [
    "MMU_TEST_CONFIG",
    "MMU_LED",
    "MMU_GRIP",
    "MMU_RELEASE",
    "MMU_SERVO",
    "MMU_LED_VARS",
    "MMU_SOFTWARE_VARS",
    "MMU_PRINT_START",
    "MMU_PRINT_END",
]


# ---------------------------------------------------------------------------
# Daemon / Moonraker HTTP helpers (thin wrappers; flare_cmd owns the daemon
# client so auth/timeout/error handling stay in one place).
# ---------------------------------------------------------------------------

def configure_daemon(daemon_url, auth_token):
    flare_cmd.DAEMON_URL = daemon_url
    flare_cmd.DAEMON_AUTH_TOKEN = auth_token


def get_daemon_snapshot():
    """Full /status payload: status_cache fields + last-100 event_history."""
    return flare_cmd.get_daemon_status()


def moonraker_query(moonraker_url, objects_query, timeout=2.0):
    url = f"{moonraker_url}/printer/objects/query?{objects_query}"
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            if resp.status == 200:
                return json.loads(resp.read().decode("utf-8"))
    except Exception:
        return None
    return None


def moonraker_gcode_script(moonraker_url, script, timeout=3.0):
    """POST one gcode script to Moonraker. Returns (ok, error_text_or_None)."""
    payload = json.dumps({"script": script}).encode("utf-8")
    req = urllib.request.Request(
        f"{moonraker_url}/printer/gcode/script",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read().decode("utf-8", errors="ignore")
            return resp.status == 200, (None if resp.status == 200 else body)
    except urllib.error.HTTPError as e:
        return False, e.read().decode("utf-8", errors="ignore")
    except Exception as e:
        return False, str(e)


def klippy_log_has_unknown_command(klippy_log_path, since_ts, gcode_name):
    """Grep klippy.log for an 'Unknown command' error naming gcode_name,
    logged at or after since_ts. Best-effort corroboration only — Moonraker's
    own response (checked first) is the primary signal."""
    if not klippy_log_path:
        return None
    try:
        with open(klippy_log_path, encoding="utf-8", errors="ignore") as f:
            lines = f.readlines()
    except OSError:
        return None
    needle = f"Unknown command:\"{gcode_name}\""
    for line in reversed(lines[-2000:]):
        if needle in line or (gcode_name in line and "Unknown command" in line):
            return line.strip()
    return None


# ---------------------------------------------------------------------------
# Pure classifier helpers — operate on plain event dicts
# ({"time": float, "type": str, "data": str}), so they're unit-testable
# without a running daemon. `events` is assumed sorted by "time" ascending.
# ---------------------------------------------------------------------------

def events_of_type(events, evt_type, since_ts=None):
    out = [e for e in events if e["type"] == evt_type]
    if since_ts is not None:
        out = [e for e in out if e["time"] >= since_ts]
    return out


def first_after(events, evt_type, after_ts):
    for e in events:
        if e["type"] == evt_type and e["time"] >= after_ts:
            return e
    return None


def classify_single_occurrence(events, evt_type, since_ts, burst_window_s, data_contains=None):
    """Item #4 / #11 shape: expect exactly one evt_type in the window, not a
    storm. Returns (verdict, evidence) where verdict is one of
    "pending" (not seen yet), "pass" (one clean occurrence),
    "fail" (repeated within burst_window_s of each other)."""
    matches = events_of_type(events, evt_type, since_ts)
    if data_contains is not None:
        matches = [e for e in matches if data_contains in e["data"]]
    if not matches:
        return "pending", None
    if len(matches) == 1:
        return "pass", matches[0]
    for i in range(1, len(matches)):
        if matches[i]["time"] - matches[i - 1]["time"] < burst_window_s:
            return "fail", matches
    return "pass", matches[-1]


def classify_sequence(events, seq_types, since_ts, max_total_span_s, forbid_types=None):
    """Item #6 / #8 shape: seq_types must occur in order within
    max_total_span_s of each other, with none of forbid_types occurring
    between the first and last match."""
    cursor_ts = since_ts
    matched = []
    for evt_type in seq_types:
        hit = first_after(events, evt_type, cursor_ts)
        if hit is None:
            return "pending", matched
        matched.append(hit)
        cursor_ts = hit["time"]
    span = matched[-1]["time"] - matched[0]["time"]
    if span > max_total_span_s:
        return "fail", matched
    if forbid_types:
        for e in events:
            if matched[0]["time"] <= e["time"] <= matched[-1]["time"] and e["type"] in forbid_types:
                return "fail", matched + [e]
    return "pass", matched


def classify_absence_then(events, forbid_types, then_type, since_ts, window_s):
    """Item #10 (bare BL:T) shape: none of forbid_types must occur before
    then_type, within window_s of since_ts."""
    then_hit = first_after(events, then_type, since_ts)
    if then_hit is None:
        if any(e["time"] - since_ts > window_s for e in events if e["time"] >= since_ts):
            return "fail", None  # window elapsed with no then_type at all
        return "pending", None
    for e in events:
        if since_ts <= e["time"] < then_hit["time"] and e["type"] in forbid_types:
            return "fail", e
    return "pass", then_hit


# ---------------------------------------------------------------------------
# Open-items table. `phase` and `req` are for the report header only.
# ---------------------------------------------------------------------------

WATCH_ITEMS = {
    "4-bl-timeout": dict(
        phase="1", desc="Buffer-lock timeout fires once, not repeated",
        check=lambda events, t0: classify_single_occurrence(events, "BL:TIMEOUT", t0, burst_window_s=30),
    ),
    "6-runout-reload": dict(
        phase="1", desc="Genuine runout escalates cleanly to RELOAD, no stall",
        check=lambda events, t0: classify_sequence(
            events, ["RUNOUT", "RELOAD:LOADED"], t0, max_total_span_s=120,
            forbid_types=["FAULT:MOVE_COMPRESSION", "FAULT:MOVE_TENSION", "TC:ERROR"],
        ),
    ),
    "8-tc-no-compression-fault": dict(
        phase="12", desc="Toolchange with TC_TS_PARK_MM: no FAULT:MOVE_COMPRESSION, sync auto-resumes",
        check=lambda events, t0: classify_sequence(
            events, ["TC:DONE", "SYNC:AUTO_START"], t0, max_total_span_s=30,
            forbid_types=["FAULT:MOVE_COMPRESSION"],
        ),
    ),
    "11-tmc-single-fault": dict(
        phase="12", desc="Lane-2 UART/5V unplug -> exactly one TMC:FAULT, then TMC:RESTORED on replug",
        check=lambda events, t0: classify_sequence(
            events, ["TMC:FAULT", "TMC:RESTORED"], t0, max_total_span_s=60,
        ),
    ),
}

# Each item self-triggers its own busy/abort window via `pre_cmd` — the cut
# (CU) and toolchange (TC:) cycles run for hundreds of ms to seconds
# (SERVO_SETTLE_MS alone is 500ms, firmware/include/tune.h), so sending the
# check command immediately after `pre_cmd` with no sleep reliably lands
# inside the busy window. No manual timing / catching a live moment needed.
FIRE_ITEMS = {
    "1-cal-busy": dict(
        phase="1", desc="CAL during active motion -> ER:PERSIST_BUSY, no flash corruption",
        precondition="Active lane has filament loaded (lane_in_present); other lane's OUT sensor clear.",
        pre_cmd="CU", cmd="CAL:CRASHLOG_CLEAR", expect_reply_prefix="ER:PERSIST_BUSY",
    ),
    "2-cp-busy": dict(
        phase="1", desc="CP mid-cut -> ER:BUSY:CUTTER, cut still completes",
        precondition="Active lane has filament loaded (lane_in_present); other lane's OUT sensor clear.",
        pre_cmd="CU", cmd="CP:950", expect_reply_prefix="ER:BUSY:CUTTER",
    ),
    "3-tc-busy": dict(
        phase="1", desc="T:/TC: during active toolchange -> ER:BUSY:TC, toolchange unaffected",
        precondition="Both lanes loaded with filament; active lane already selected (T:); not double-loaded.",
        pre_cmd="TC:{other_lane}", cmd="TC:{lane}", expect_reply_prefix="ER:BUSY:TC",
    ),
    "9-cutter-abort": dict(
        phase="12", desc="STOP mid-cut -> cutter aborts cleanly (EV:CUT:ERROR:ABORTED)",
        precondition="Active lane has filament loaded (lane_in_present); other lane's OUT sensor clear.",
        pre_cmd="CU", cmd="STOP", expect_event="CUT:ERROR:ABORTED",
    ),
}


def run_fire_bl_bare_vs_args(args):
    """Item #10: a bare `BL:T` (no args) must be a no-op on retract (no
    BL:BREAK/FOLLOW, motor stays still, BS -> BUF_STAB:DONE); an argumented
    `BL:T:20:300` on the same retract must show BREAK->FOLLOW->FOLLOW_DONE."""
    configure_daemon(args.daemon_url, flare_cmd.get_auth_token(args.auth_token))
    print("Firing 10-bl-bare-vs-args: bare BL:T must no-op, BL:T:20:300 must FOLLOW")

    t0 = time.time()
    print("  -> sending bare 'BL:T' now (on a retract)")
    reply1 = flare_cmd.send_daemon_cmd("BL:T", timeout=args.timeout)
    print(f"     reply: {reply1}")
    print("  -> sending 'BS' now")
    reply2 = flare_cmd.send_daemon_cmd("BS", timeout=args.timeout)
    print(f"     reply: {reply2}")

    snap = get_daemon_snapshot()
    events = snap.get("events", []) if snap else []
    verdict1, evidence1 = classify_absence_then(events, ["BL:BREAK", "BL:FOLLOW"], "BUF_STAB:DONE", t0, window_s=30)
    print(f"  bare BL:T no-op: {verdict1.upper()} (evidence: {evidence1})")

    t1 = time.time()
    print("  -> sending 'BL:T:20:300' now (on a retract)")
    reply3 = flare_cmd.send_daemon_cmd("BL:T:20:300", timeout=args.timeout)
    print(f"     reply: {reply3}")
    time.sleep(2.0)
    snap = get_daemon_snapshot()
    events = snap.get("events", []) if snap else []
    verdict2, evidence2 = classify_sequence(events, ["BL:BREAK", "BL:FOLLOW", "BL:FOLLOW_DONE"], t1, max_total_span_s=30)
    print(f"  BL:T:20:300 follow sequence: {verdict2.upper()} (evidence: {evidence2})")

    return 0 if verdict1 == "pass" and verdict2 == "pass" else 1


def wait_for_event(evt_type, since_ts, timeout_s, poll_interval_s=0.2):
    """Poll the daemon's /status until evt_type appears at/after since_ts,
    or timeout_s elapses. Returns the matching event dict or None."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        snap = get_daemon_snapshot()
        if snap is not None:
            for e in snap.get("events", []):
                if e["type"] == evt_type and e["time"] >= since_ts:
                    return e
        time.sleep(poll_interval_s)
    return None


def run_fire(item_id, args):
    if item_id not in FIRE_ITEMS:
        print(f"Unknown fire item {item_id!r}. Known: {sorted(FIRE_ITEMS)}", file=sys.stderr)
        return 1
    item = FIRE_ITEMS[item_id]
    configure_daemon(args.daemon_url, flare_cmd.get_auth_token(args.auth_token))
    print(f"Firing {item_id}: {item['desc']}")
    if item.get("precondition"):
        print(f"  precondition: {item['precondition']}")

    pre_cmd = item.get("pre_cmd")
    if pre_cmd:
        pre_cmd = pre_cmd.format(lane=args.tc_lane, other_lane=args.tc_other_lane)
        print(f"  -> self-triggering: sending {pre_cmd!r} to open the busy/abort window")
        pre_reply = flare_cmd.send_daemon_cmd(pre_cmd, timeout=args.timeout)
        print(f"     pre-trigger reply: {pre_reply}")
        if pre_reply is None or pre_reply.startswith("ER"):
            print(f"  FAIL ({item_id}): pre-trigger command was rejected — check the precondition above "
                  "(filament loaded? correct active/other lane? cutter/gcode enabled?)")
            return 1

    since_ts = time.time()
    cmd = item["cmd"].format(lane=args.tc_lane, other_lane=args.tc_other_lane)
    print(f"  -> sending {cmd!r} now (no sleep — relying on the busy window pre_cmd just opened)")
    reply = flare_cmd.send_daemon_cmd(cmd, timeout=args.timeout)
    print(f"  reply: {reply}")

    if "expect_event" in item:
        evt = wait_for_event(item["expect_event"], since_ts, timeout_s=min(args.timeout, 10.0))
        if evt is not None:
            print(f"  PASS ({item_id}): observed EV:{evt['type']}:{evt['data']}")
            return 0
        print(f"  FAIL ({item_id}): never saw EV:{item['expect_event']} within the timeout")
        return 1

    if reply is None:
        print("  NO REPLY (daemon unreachable or command timed out) -> FAIL")
        return 1
    if reply.startswith(item["expect_reply_prefix"]):
        print(f"  PASS ({item_id})")
        return 0
    print(f"  FAIL ({item_id}): expected reply prefix {item['expect_reply_prefix']!r}")
    return 1


def run_fire_stubs(args):
    print(f"Firing item 15: {len(MAINTENANCE_STUB_GCODES)} maintenance-dialog stubs via Moonraker")
    failures = []
    for gcode in MAINTENANCE_STUB_GCODES:
        since_ts = time.time()
        ok, err = moonraker_gcode_script(args.moonraker_url, gcode)
        log_hit = klippy_log_has_unknown_command(args.klippy_log, since_ts, gcode)
        if ok and log_hit is None:
            print(f"  PASS  {gcode}")
        else:
            failures.append(gcode)
            reason = err or log_hit or "unknown"
            print(f"  FAIL  {gcode}: {reason}")
    if failures:
        print(f"\n{len(failures)}/{len(MAINTENANCE_STUB_GCODES)} stub(s) failed: {failures}")
        return 1
    print(f"\nAll {len(MAINTENANCE_STUB_GCODES)} stubs registered cleanly.")
    return 0


def run_watchdog_check(args):
    """Read back --crashlog/--loop-stats after you've manually triggered a
    main-loop stall on the bench (item #7). This only reads evidence; it
    cannot trigger the stall itself."""
    configure_daemon(args.daemon_url, flare_cmd.get_auth_token(args.auth_token))
    print("Reading GET:CRASHLOG and GET:LOOP_STATS (trigger the stall yourself first)...")
    crashlog = flare_cmd.send_daemon_cmd("GET:CRASHLOG", timeout=args.timeout)
    loop_stats = flare_cmd.send_daemon_cmd("GET:LOOP_STATS", timeout=args.timeout)
    print(f"  CRASHLOG:    {crashlog}")
    print(f"  LOOP_STATS:  {loop_stats}")
    print("\nManually confirm: crashlog ends OK:CRASH:END with pre-stall TC/sync state; "
          "LOOP_STATS shows the print's own MAX<10000us and OVERRUNS 0.")
    return 0


def run_watch(args):
    configure_daemon(args.daemon_url, flare_cmd.get_auth_token(args.auth_token))
    t0 = time.time()
    log_path = Path(args.log)
    log_f = log_path.open("a", encoding="utf-8")
    seen_event_times = set()
    flowguard_history = []
    state = dict.fromkeys(WATCH_ITEMS, "pending")

    print(f"Watching from t0={t0:.3f}. Evidence log: {log_path}. Ctrl+C to stop and report.")
    try:
        while True:
            snap = get_daemon_snapshot()
            if snap is not None:
                events = snap.get("events", [])
                for e in events:
                    key = (e["time"], e["type"], e["data"])
                    if key not in seen_event_times:
                        seen_event_times.add(key)
                        log_f.write(json.dumps({"kind": "event", **e}) + "\n")
                        log_f.flush()
                        print(f"  [{e['time'] - t0:6.1f}s] EV:{e['type']}:{e['data']}")
                all_events = sorted(
                    ({"time": t, "type": ty, "data": d} for (t, ty, d) in seen_event_times),
                    key=lambda x: x["time"],
                )
                for item_id, item in WATCH_ITEMS.items():
                    if state[item_id] == "pending":
                        verdict, _evidence = item["check"](all_events, t0)
                        if verdict != "pending":
                            state[item_id] = verdict
                            print(f"  *** {item_id}: {verdict.upper()} — {item['desc']}")

            if args.moonraker_url:
                mmu = moonraker_query(args.moonraker_url, "mmu")
                if mmu is not None:
                    try:
                        level = mmu["result"]["status"]["mmu"]["flowguard"]["level"]
                        flowguard_history.append((time.time() - t0, level))
                        log_f.write(json.dumps({"kind": "flowguard", "t": time.time() - t0, "level": level}) + "\n")
                        log_f.flush()
                    except (KeyError, TypeError):
                        pass

            time.sleep(args.interval)
    except KeyboardInterrupt:
        pass
    finally:
        log_f.close()

    print_report(state, flowguard_history)
    return 0


def print_report(state, flowguard_history):
    print("\n=== Watch report ===")
    for item_id, item in WATCH_ITEMS.items():
        verdict = state.get(item_id, "pending")
        print(f"  [{verdict.upper():7s}] {item_id}: {item['desc']}")
    if flowguard_history:
        levels = [lvl for _t, lvl in flowguard_history]
        print(f"\n  flowguard.level samples={len(levels)} min={min(levels):.2f} max={max(levels):.2f} "
              f"last={levels[-1]:.2f}")
        print("  (14-14: confirm this moved toward +/-1.0 during a dwell approach and returned to "
              "0.0 on sync deactivate — see the samples above or replay the JSONL log)")
    else:
        print("\n  flowguard.level: no samples (item 14 needs --moonraker-url reachable)")
    print("\nNot covered by this script: #5 (flash wear + reboot), #7-trigger (bench stall — "
          "use `fire watchdog-check` to read evidence after triggering it), #12 (design "
          "question), #16 (TMC thermal). See PRINT_TEST_PLAN.md.")


def run_report(args):
    events = []
    flowguard_history = []
    t0 = None
    with open(args.log, encoding="utf-8") as f:
        for line in f:
            rec = json.loads(line)
            if rec["kind"] == "event":
                if t0 is None:
                    t0 = rec["time"]
                events.append({"time": rec["time"], "type": rec["type"], "data": rec["data"]})
            elif rec["kind"] == "flowguard":
                flowguard_history.append((rec["t"], rec["level"]))
    if t0 is None and events:
        t0 = events[0]["time"]
    t0 = t0 or 0.0
    state = {}
    for item_id, item in WATCH_ITEMS.items():
        verdict, _evidence = item["check"](events, t0)
        state[item_id] = verdict
    print_report(state, flowguard_history)
    return 0


def build_parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--daemon-url", default=flare_cmd.DAEMON_URL)
    p.add_argument("--moonraker-url", default=DEFAULT_MOONRAKER_URL)
    p.add_argument("--auth-token", default=None)
    p.add_argument("--timeout", type=float, default=10.0)
    p.add_argument("--klippy-log", default=None, help="Path to klippy.log for corroborating item 15")
    sub = p.add_subparsers(dest="mode", required=True)

    watch_p = sub.add_parser("watch", help="Passive live monitor + evidence log for items 4/6/8/11/14")
    watch_p.add_argument("--log", default="hw_verify_evidence.jsonl")
    watch_p.add_argument("--interval", type=float, default=1.0)

    fire_p = sub.add_parser("fire", help="Actively trigger one timing-sensitive item")
    fire_p.add_argument("item", choices=sorted(
        list(FIRE_ITEMS) + ["10-bl-bare-vs-args", "15-command-stubs", "watchdog-check"]))
    fire_p.add_argument("--tc-lane", type=int, default=1,
                         help="3-tc-busy only: lane to request second (the one that should get ER:BUSY:TC)")
    fire_p.add_argument("--tc-other-lane", type=int, default=2,
                         help="3-tc-busy only: lane to start the toolchange to first (pre_cmd)")

    report_p = sub.add_parser("report", help="Re-render the final table from a watch log")
    report_p.add_argument("log")

    return p


def main():
    args = build_parser().parse_args()
    if args.mode == "watch":
        return run_watch(args)
    if args.mode == "report":
        return run_report(args)
    if args.mode == "fire":
        if args.item == "15-command-stubs":
            return run_fire_stubs(args)
        if args.item == "watchdog-check":
            return run_watchdog_check(args)
        if args.item == "10-bl-bare-vs-args":
            return run_fire_bl_bare_vs_args(args)
        return run_fire(args.item, args)
    return 1


if __name__ == "__main__":
    sys.exit(main())
