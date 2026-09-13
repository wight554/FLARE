#!/usr/bin/env python3
"""verify_phase13_hw_log.py — Phase 13 HW acceptance-sheet pass-bar evaluator.

Consumes the captures named in
`.planning/phases/13-type-p-sync-relief-fault-trip/13-HW-VALIDATION.md` and prints one
PASS/FAIL/NEEDS-INPUT verdict line per D-31 pass-bar item, plus an overall verdict line.
Exits 0 iff all four items PASS; exits non-zero if any item is FAIL or NEEDS-INPUT
(REVIEW-09 — a metric this script could not compute must never read as a pass).

Inputs
------
--events <jsonl>  Required. Output of `scripts/verify_hw_open_items.py watch --log <path>`.
                  Record shapes (verify_hw_open_items.py:589 and :609 — the entire input
                  contract for this script):
                      {"kind": "event", "time": <epoch float>, "type": <str>, "data": <str>}
                      {"kind": "flowguard", "t": <relative s>, "level": <float>}
                  Only "event" records are consumed here (flowguard is a type-D/Klipper
                  concern, not part of the Phase 13 pass bar). The log is append-mode, so a
                  resumed capture can contain more than one run; relative time is keyed off
                  the earliest "event" record this script sees, never assumed to start at 0.

--csv <path>      Optional. Output of `scripts/flare_sync_check.py --daemon --csv <path>`.
                  CSV_FIELDS (flare_sync_check.py:78) includes "BP" (buffer position,
                  normalized) and "SM" (sync_enabled, "1" while ordinary print sync is
                  active, "0" during a deliberate BL:/toolchange rail operation). Required
                  for pass-bar item 4 — a buffer-POSITION question the event log cannot
                  answer at all. Without --csv, item 4 always reports NEEDS-INPUT, never PASS.

Rail-relative discipline (51bdca8 root cause — keep this docstring note alive across any
future edit to this file). Item 4 NEVER compares BP against a literal, hardcoded position
constant such as -0.75 or -0.99. It compares against the min/max BP actually observed in
*this* capture's sync-active samples — exactly the rail-relative-tracking discipline the
firmware itself uses for `g_bl_lock_extreme`/`g_sync_tension_extreme`. A fixed absolute
threshold was proven wrong on a real rig once already: this rig's tension hard end read
shallower (-0.68) than an assumed constant (-0.75), so the buffer-lock engage gate never
fired. Do not reintroduce a hardcoded position constant into this script.
"""
import argparse
import csv
import json
import sys

# Baseline comparators, transcribed from
# .planning/phases/13-type-p-sync-relief-fault-trip/baseline-capture.md
# (2026-09-12 real-rig capture, pre-Phase-13 firmware, unmodified snap-to-max behavior).
BASELINE_TENSION_RISK_HIGH_COUNT = 13  # observed occurrences across the ~2h baseline print
BASELINE_RELIEF_PAUSE_NOTE = "dozens, roughly every 2.4-3.4s (baseline-capture.md)"

# BL_BREAK_DELTA_NORM (firmware/include/sync_internal.h:35) =
# 1.0 - PSF_BREAK_THRESHOLD_NORM (firmware/include/controller_shared.h:15) = 1.0 - 0.75 = 0.25.
# Kept here as a plain literal because host scripts do not parse firmware headers; if
# PSF_BREAK_THRESHOLD_NORM ever changes, update this constant and its comment together.
BL_BREAK_DELTA_NORM = 0.25

PASS = "PASS"
FAIL = "FAIL"
NEEDS_INPUT = "NEEDS-INPUT"


def load_events(events_path):
    """Read the JSONL watch log, return ("event"-kind records only, sorted by time, min_time).

    Ignores "flowguard" records. Relative time is keyed off the earliest "event" record this
    parser sees -- never assumed to start at t=0 -- because the log is append-mode and a
    resumed capture can span more than one run."""
    events = []
    min_time = None
    with open(events_path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            if rec.get("kind") != "event":
                continue
            time_val = rec["time"]
            events.append({"time": time_val, "type": rec["type"], "data": rec.get("data", "")})
            if min_time is None or time_val < min_time:
                min_time = time_val
    events.sort(key=lambda e: e["time"])
    return events, (min_time if min_time is not None else 0.0)


def _matching(events, evt_type, data_equals=None):
    matches = [e for e in events if e["type"] == evt_type]
    if data_equals is not None:
        matches = [e for e in matches if e["data"] == data_equals]
    return matches


def item1_relief_pause_rate(events, span_s):
    """Pass-bar item 1: relief-pause rate, per 60s of the span the log covers, at most 1."""
    count = len(_matching(events, "SYNC:RELIEF_PAUSE"))
    if span_s <= 0:
        # No usable span (e.g. a single-instant or empty capture) -- a rate cannot be derived.
        return NEEDS_INPUT, count, None
    rate_per_60s = count / (span_s / 60.0)
    verdict = PASS if rate_per_60s <= 1.0 else FAIL
    return verdict, count, rate_per_60s


def item2_tension_risk_high(events):
    """Pass-bar item 2: fewer SYNC:TENSION_RISK_HIGH events than the baseline's 13 in 2h."""
    count = len(_matching(events, "SYNC:TENSION_RISK_HIGH"))
    verdict = PASS if count < BASELINE_TENSION_RISK_HIGH_COUNT else FAIL
    return verdict, count


def item3_false_stops(events):
    """Pass-bar item 3: zero false stops on a print that completes normally -- no fault-hold,
    no distance-trip event (mm or ms), no no-consumer probe verdict."""
    fault_hold = len(_matching(events, "SYNC:FAULT_HOLD"))
    stop_mm = len(_matching(events, "SYNC:TENSION_STOP", data_equals="MM"))
    stop_ms = len(_matching(events, "SYNC:TENSION_STOP", data_equals="MS"))
    no_consumer = len(_matching(events, "SYNC:PROBE", data_equals="NO_CONSUMER"))
    total = fault_hold + stop_mm + stop_ms + no_consumer
    verdict = PASS if total == 0 else FAIL
    counts = {
        "fault_hold": fault_hold,
        "stop_mm": stop_mm,
        "stop_ms": stop_ms,
        "no_consumer": no_consumer,
        "total": total,
    }
    return verdict, counts


def item4_rail_hits(csv_path):
    """Pass-bar item 4: no rail hits during print sync -- BP never comes within
    BL_BREAK_DELTA_NORM of either observed extreme, outside deliberate BL:/toolchange
    operations (SM == "0" during those, excluded from the hit count by construction)."""
    if csv_path is None:
        return NEEDS_INPUT, None

    bp_all = []
    bp_sync_active = []
    with open(csv_path, encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            bp_raw = row.get("BP")
            if bp_raw in (None, ""):
                continue
            try:
                bp = float(bp_raw)
            except ValueError:
                continue
            bp_all.append(bp)
            if row.get("SM") == "1":
                bp_sync_active.append(bp)

    if not bp_all:
        return NEEDS_INPUT, None

    observed_min = min(bp_all)
    observed_max = max(bp_all)
    hits = [
        bp
        for bp in bp_sync_active
        if bp >= observed_max - BL_BREAK_DELTA_NORM or bp <= observed_min + BL_BREAK_DELTA_NORM
    ]
    verdict = PASS if len(hits) == 0 else FAIL
    detail = {
        "observed_min": observed_min,
        "observed_max": observed_max,
        "hit_count": len(hits),
        "sync_active_samples": len(bp_sync_active),
    }
    return verdict, detail


def build_parser():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "--events", required=True,
        help="JSONL log from `verify_hw_open_items.py watch --log <path>`",
    )
    ap.add_argument(
        "--csv", default=None,
        help="CSV capture from `flare_sync_check.py --daemon --csv <path>` (item 4 only)",
    )
    return ap


def main(argv=None):
    args = build_parser().parse_args(argv)

    events, t0 = load_events(args.events)
    span_s = (max(e["time"] for e in events) - t0) if events else 0.0

    lines = []
    overall_pass = True

    v1, c1, rate1 = item1_relief_pause_rate(events, span_s)
    overall_pass = overall_pass and v1 == PASS
    rate_str = f"{rate1:.2f}/60s" if rate1 is not None else "n/a"
    lines.append(
        f"ITEM 1: observed={c1} events ({rate_str}) "
        f"baseline=<=1/60s (was {BASELINE_RELIEF_PAUSE_NOTE}) verdict={v1}"
    )

    v2, c2 = item2_tension_risk_high(events)
    overall_pass = overall_pass and v2 == PASS
    lines.append(
        f"ITEM 2: observed={c2} baseline=<{BASELINE_TENSION_RISK_HIGH_COUNT} verdict={v2}"
    )

    v3, d3 = item3_false_stops(events)
    overall_pass = overall_pass and v3 == PASS
    lines.append(
        f"ITEM 3: observed=fault_hold:{d3['fault_hold']},stop_mm:{d3['stop_mm']},"
        f"stop_ms:{d3['stop_ms']},no_consumer:{d3['no_consumer']} "
        f"baseline=0,0,0,0 verdict={v3}"
    )

    v4, d4 = item4_rail_hits(args.csv)
    overall_pass = overall_pass and v4 == PASS
    if d4 is None:
        lines.append(
            f"ITEM 4: observed=n/a (no --csv supplied, or --csv had no BP samples) "
            f"baseline=0 hits verdict={v4}"
        )
    else:
        lines.append(
            f"ITEM 4: observed=hits:{d4['hit_count']} "
            f"(min={d4['observed_min']:.3f}, max={d4['observed_max']:.3f}, "
            f"sync-active samples={d4['sync_active_samples']}) "
            f"baseline=0 hits verdict={v4}"
        )

    for line in lines:
        print(line)
    overall = PASS if overall_pass else FAIL
    print(f"OVERALL: {overall}")
    return 0 if overall_pass else 1


if __name__ == "__main__":
    sys.exit(main())
