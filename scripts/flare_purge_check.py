#!/usr/bin/env python3
"""
flare_purge_check.py — objective pass/fail for the neutral-demand-collapse-brake
change. Parses FLARE `?:` poll telemetry (live or from a captured log) and
evaluates two things:

  PURGE (tests A/B): after a fast purge the buffer must NOT slam compression.
    The estimator (EST) must DECAY during the post-surge NEUTRAL glide instead
    of freezing at the high value it learned during the tension catch-up.

  REGRESSION (test C): during normal extrusion the demand-collapse corrector
    must NOT false-fire and must not introduce starvation. Counts starvation /
    low-confidence events and flags estimator decays that happen inside the
    recent-tension holdoff (where the corrector is supposed to be inhibited).

USAGE
  Offline (analyze a captured log):
    python3 scripts/flare_purge_check.py --log purge.txt
    python3 scripts/flare_purge_check.py --log print.txt --mode regression

  Live (capture while you run the macro / print, Ctrl+C to stop and analyze):
    python3 scripts/flare_purge_check.py --live --poll 100 --csv run.csv

Geometry (defaults match config.ini buf_switch_span_mm=10, buf_max_travel_mm=25):
  --switch-span-mm 10   -> compression/tension switch at +/-5 mm
  --max-travel-mm 25    -> physical hard wall at +/-12.5 mm

Exit code: 0 = PASS, 1 = FAIL, 2 = INCONCLUSIVE (no relevant episode captured).
"""
from __future__ import annotations

import argparse
import sys
import time
from typing import Dict, List, Optional, Tuple

# Events that signal starvation / degraded estimator (regression watch).
STARVATION_EVENTS = (
    "cannot_refill",
    "cannot_relieve",
    "TENSION_DWELL_WARN",
    "EST_LOW_CF",
    "EST_FALLBACK",
    "TENSION_RISK_HIGH",
    "RELIEF_PAUSE",
)

# Fields kept in the CSV / used by the analysis.
CSV_FIELDS = ("idx", "BUF", "BP", "EST", "MM", "RE", "AV", "CT", "SM", "ST")


class Sample:
    __slots__ = ("idx", "fields")

    def __init__(self, idx: int, fields: Dict[str, str]):
        self.idx = idx
        self.fields = fields

    def f(self, key: str) -> Optional[float]:
        v = self.fields.get(key)
        if v is None:
            return None
        try:
            return float(v)
        except ValueError:
            return None

    def s(self, key: str) -> Optional[str]:
        return self.fields.get(key)


def parse_status_line(line: str) -> Optional[Dict[str, str]]:
    """Parse an `OK:KEY:VAL,KEY:VAL,...` status line into a dict."""
    if not line.startswith("OK:"):
        return None
    body = line[3:]
    if ":" not in body:
        return None
    fields: Dict[str, str] = {}
    for token in body.split(","):
        if ":" not in token:
            continue
        key, val = token.split(":", 1)
        fields[key.strip()] = val.strip()
    return fields if "BUF" in fields else None


def parse_stream(lines: List[str]) -> Tuple[List[Sample], List[Tuple[int, str]]]:
    """Return (samples, events). events are (sample_idx_at_time, raw_ev_string)."""
    samples: List[Sample] = []
    events: List[Tuple[int, str]] = []
    for raw in lines:
        line = raw.strip()
        if not line:
            continue
        if line.startswith("EV:"):
            events.append((len(samples), line))
            continue
        fields = parse_status_line(line)
        if fields is not None:
            samples.append(Sample(len(samples), fields))
    return samples, events


# ---------------------------------------------------------------------------
# PURGE analysis (tests A/B)
# ---------------------------------------------------------------------------
def neutral_runs(samples: List[Sample]) -> List[Tuple[int, int]]:
    """Maximal index ranges [start, end] where BUF == NEUTRAL and sync active."""
    runs: List[Tuple[int, int]] = []
    start = None
    for i, smp in enumerate(samples):
        active = smp.s("SM") in (None, "1")  # treat missing SM as active
        is_neutral = smp.s("BUF") == "NEUTRAL" and active
        if is_neutral and start is None:
            start = i
        elif not is_neutral and start is not None:
            runs.append((start, i - 1))
            start = None
    if start is not None:
        runs.append((start, len(samples) - 1))
    return runs


def analyze_purge(samples: List[Sample], events, threshold: float,
                  hardwall: float) -> Tuple[str, List[str]]:
    """Return (verdict, report_lines). verdict in PASS/FAIL/INCONCLUSIVE."""
    report: List[str] = []
    drop_thresh = 0.4 * threshold          # meaningful slide toward compression
    surge_floor = 200.0                    # EST high enough to have learned a surge
    collapse_glides = 0
    frozen_glides = 0

    for (a, b) in neutral_runs(samples):
        if b - a < 3:
            continue
        bp0 = samples[a].f("BP")
        bp1 = samples[b].f("BP")
        est0 = samples[a].f("EST")
        est1 = samples[b].f("EST")
        mm_vals = [s.f("MM") for s in samples[a:b + 1] if s.f("MM") is not None]
        if None in (bp0, bp1, est0, est1) or not mm_vals:
            continue
        bp_drop = bp0 - bp1                 # positive => moved toward compression
        mmu_mean = sum(mm_vals) / len(mm_vals)
        # Demand-collapse glide: buffer slid toward compression while feeding,
        # following a surge that pushed EST high.
        if bp_drop < drop_thresh or mmu_mean < 50.0 or est0 < surge_floor:
            continue
        collapse_glides += 1
        est_decay = est0 - est1            # positive => estimator backed off
        required = max(0.10 * est0, 40.0)
        status = "OK (EST decayed)"
        if est_decay < required:
            frozen_glides += 1
            status = "FROZEN (EST did not decay)"
        report.append(
            f"  glide idx[{a}-{b}] BP {bp0:+.2f}->{bp1:+.2f} (drop {bp_drop:.2f}) "
            f"EST {est0:.0f}->{est1:.0f} (decay {est_decay:.0f}, need {required:.0f}) "
            f"MMU~{mmu_mean:.0f} -> {status}"
        )

    # Compression depth past the switch (corroborating slam signal).
    min_bp = min((s.f("BP") for s in samples if s.f("BP") is not None), default=0.0)
    depth_past_switch = max(0.0, -min_bp - threshold)
    relief_pauses = [e for (_, e) in events if "RELIEF_PAUSE" in e]

    report.insert(0, f"  demand-collapse glides: {collapse_glides}, frozen: {frozen_glides}")
    report.insert(1, f"  deepest BP: {min_bp:+.2f} mm "
                     f"(switch +/-{threshold:.1f}, wall +/-{hardwall:.1f}, "
                     f"{depth_past_switch:.2f} mm past switch)")
    if relief_pauses:
        report.insert(2, f"  RELIEF_PAUSE events: {len(relief_pauses)} (compression auto-stop fired)")

    if collapse_glides == 0:
        return "INCONCLUSIVE", report + [
            "  No post-surge demand-collapse glide captured — run the purge "
            "macro (e.g. _FLARE_PURGE PURGE=60) during capture."]
    if frozen_glides > 0:
        return "FAIL", report
    # Frozen estimator fixed; flag residual slam as a secondary concern only.
    if depth_past_switch > 0.5 * (hardwall - threshold):
        return "FAIL", report + [
            "  EST decayed but buffer still drove deep past the switch — check "
            "fast-brake hot-gate / ramp."]
    return "PASS", report


# ---------------------------------------------------------------------------
# REGRESSION analysis (test C)
# ---------------------------------------------------------------------------
def analyze_regression(samples: List[Sample], events, threshold: float
                       ) -> Tuple[str, List[str]]:
    report: List[str] = []
    counts: Dict[str, int] = {k: 0 for k in STARVATION_EVENTS}
    for (_, ev) in events:
        for k in STARVATION_EVENTS:
            if k in ev:
                counts[k] += 1

    # Spurious-fire heuristic: EST drops sharply within the recent-tension
    # holdoff window after leaving TENSION, while BP is NOT sliding to
    # compression. The corrector is supposed to be inhibited there.
    spurious = 0
    holdoff = 8  # samples (~800ms at 100ms poll), heuristic
    for i in range(1, len(samples)):
        prev, cur = samples[i - 1], samples[i]
        if prev.s("BUF") == "TENSION" and cur.s("BUF") == "NEUTRAL":
            est_at_exit = cur.f("EST")
            if est_at_exit is None or est_at_exit < 200.0:
                continue
            j_end = min(i + holdoff, len(samples) - 1)
            est_end = samples[j_end].f("EST")
            bp_now = cur.f("BP")
            bp_end = samples[j_end].f("BP")
            if None in (est_end, bp_now, bp_end):
                continue
            est_drop = est_at_exit - est_end
            bp_drop = bp_now - bp_end
            if est_drop > 0.20 * est_at_exit and bp_drop < 0.4 * threshold:
                spurious += 1

    total_starv = sum(counts.values())
    for k in STARVATION_EVENTS:
        if counts[k]:
            report.append(f"  {k}: {counts[k]}")
    report.insert(0, f"  starvation/degraded events: {total_starv}")
    report.append(f"  suspected spurious corrector fires (decay inside "
                  f"recent-tension holdoff, no compression slide): {spurious}")

    if total_starv > 0 or spurious > 0:
        return "FAIL", report
    if not samples:
        return "INCONCLUSIVE", report + ["  No samples captured."]
    return "PASS", report


# ---------------------------------------------------------------------------
# IO
# ---------------------------------------------------------------------------
def capture_live(port: Optional[str], poll_ms: int, duration: Optional[float]
                 ) -> List[str]:
    try:
        import serial  # lazy
    except ImportError:
        print("flare_purge_check: pyserial not installed. pip install pyserial",
              file=sys.stderr)
        sys.exit(1)
    sys.path.insert(0, __import__("os").path.dirname(__file__))
    from serial_utils import find_port  # type: ignore

    dev = find_port(port)
    if not dev:
        print("flare_purge_check: no serial port found", file=sys.stderr)
        sys.exit(1)
    ser = serial.Serial(dev, 115200, timeout=0.5)
    interval = poll_ms / 1000.0
    lines: List[str] = []
    deadline = time.time() + duration if duration else None
    print(f"# Capturing on {dev} every {poll_ms} ms. Run the macro/print now. "
          f"Ctrl+C to stop and analyze.", flush=True)
    try:
        while True:
            t0 = time.time()
            ser.write(b"?:\n")
            while True:
                line = ser.readline().decode("utf-8", errors="ignore").strip()
                if not line:
                    break
                lines.append(line)
                if line.startswith(("OK:", "ER:")) or line == "OK":
                    break
            if deadline and time.time() >= deadline:
                break
            time.sleep(max(0.0, interval - (time.time() - t0)))
    except KeyboardInterrupt:
        print("\n# Capture stopped.", flush=True)
    finally:
        ser.close()
    return lines


def write_csv(path: str, samples: List[Sample]) -> None:
    import csv
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(CSV_FIELDS)
        for s in samples:
            w.writerow([s.idx] + [s.fields.get(k, "") for k in CSV_FIELDS[1:]])


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--log", help="Analyze a captured telemetry log file")
    src.add_argument("--live", action="store_true",
                     help="Capture live from the device, then analyze")
    ap.add_argument("--port", help="Serial port (auto-detect if omitted)")
    ap.add_argument("--poll", type=int, default=100, help="Live poll interval ms")
    ap.add_argument("--duration", type=float,
                    help="Live capture seconds (default: until Ctrl+C)")
    ap.add_argument("--csv", help="Write parsed samples to this CSV path")
    ap.add_argument("--mode", choices=("purge", "regression", "both"),
                    default="both", help="Which check(s) to run")
    ap.add_argument("--switch-span-mm", type=float, default=10.0)
    ap.add_argument("--max-travel-mm", type=float, default=25.0)
    args = ap.parse_args()

    threshold = args.switch_span_mm / 2.0
    hardwall = args.max_travel_mm / 2.0

    if args.live:
        lines = capture_live(args.port, args.poll, args.duration)
    else:
        with open(args.log, "r", errors="ignore") as fh:
            lines = fh.readlines()

    samples, events = parse_stream(lines)
    print(f"# parsed {len(samples)} samples, {len(events)} events")
    if args.csv:
        write_csv(args.csv, samples)
        print(f"# wrote {args.csv}")

    verdicts: List[str] = []
    if args.mode in ("purge", "both"):
        v, rep = analyze_purge(samples, events, threshold, hardwall)
        print(f"\nPURGE (A/B): {v}")
        for line in rep:
            print(line)
        verdicts.append(v)
    if args.mode in ("regression", "both"):
        v, rep = analyze_regression(samples, events, threshold)
        print(f"\nREGRESSION (C): {v}")
        for line in rep:
            print(line)
        verdicts.append(v)

    if "FAIL" in verdicts:
        return 1
    if all(v == "INCONCLUSIVE" for v in verdicts):
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
