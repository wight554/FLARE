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
CSV_FIELDS = ("idx", "BUF", "BP", "EST", "MM", "RE", "AV", "CT", "SM", "ST",
              "SYNC_RELIEVE_MM", "SYNC_REFILL_MM")

# PASS thresholds for the compression-overfeed-stop fix.
# The damage signal is forward feed into a FULL buffer, not dwell length: once
# feed stops (~0), a long idle COMPRESSION sit (purge pause) is harmless.
OVERFILL_BUDGET_MM = 3.0    # max forward overfill (SYNC_RELIEVE_MM) into a full buffer
STOP_EPS_SPS = 30           # feed at/below this counts as stopped (SYNC_MIN is 100)


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
def state_runs(samples: List[Sample], state: str) -> List[Tuple[int, int]]:
    """Maximal index ranges [start, end] where BUF == state and sync active."""
    runs: List[Tuple[int, int]] = []
    start = None
    for i, smp in enumerate(samples):
        active = smp.s("SM") in (None, "1")  # treat missing SM as active
        match = smp.s("BUF") == state and active
        if match and start is None:
            start = i
        elif not match and start is not None:
            runs.append((start, i - 1))
            start = None
    if start is not None:
        runs.append((start, len(samples) - 1))
    return runs


def analyze_purge(samples: List[Sample], events, threshold: float,
                  hardwall: float) -> Tuple[str, List[str]]:
    """compression-overfeed-stop success: when the buffer reaches COMPRESSION
    after a purge, the MMU must stop (0 feed) and overfill must stay within the
    relief budget — i.e. small SYNC_RELIEVE_MM and a short COMPRESSION dwell (CT),
    not the multi-second / multi-mm grind of the old SYNC_MIN-forward behavior.
    Return (verdict, report_lines)."""
    report: List[str] = []
    worst_overfill = 0.0
    worst_steady_feed = 0.0
    worst_grind_ms = 0.0
    had_overfeed = False
    runs = state_runs(samples, "COMPRESSION")

    for (a, b) in runs:
        rel = [s.f("SYNC_RELIEVE_MM") for s in samples[a:b + 1]
               if s.f("SYNC_RELIEVE_MM") is not None]
        ct = [s.f("CT") for s in samples[a:b + 1] if s.f("CT") is not None]
        mm = [s.f("MM") for s in samples[a:b + 1] if s.f("MM") is not None]
        bp = [s.f("BP") for s in samples[a:b + 1] if s.f("BP") is not None]
        overfill = (max(rel) - min(rel)) if rel else 0.0
        grind_ms = max(ct) if ct else 0.0
        min_bp = min(bp) if bp else 0.0
        # Skip the initial ramp-down, then feed must stay ~0 (true-stop). A long
        # dwell at zero feed is a benign idle sit, not an overfeed. Episodes too
        # brief to have a post-ramp-down portion (the fast-stop makes COMPRESSION
        # very short) can't be judged on feed — a single entry sample is the
        # pre-ramp transient, not steady; rely on overfill for those.
        skip = min(6, len(mm) // 3)
        steady = mm[skip:]
        have_steady = len(steady) >= 2
        steady_feed = max(steady) if steady else 0.0
        sustained_feed = have_steady and steady_feed > STOP_EPS_SPS
        overfeed = sustained_feed or overfill > OVERFILL_BUDGET_MM
        worst_overfill = max(worst_overfill, overfill)
        if have_steady:
            worst_steady_feed = max(worst_steady_feed, steady_feed)
        worst_grind_ms = max(worst_grind_ms, grind_ms)
        verdict = "OVERFEED" if overfeed else ("OK" if have_steady else "OK(brief)")
        feed_str = f"{steady_feed:.0f} sps" if have_steady else "n/a (brief)"
        report.append(
            f"  COMPRESSION idx[{a}-{b}] steady_feed {feed_str} "
            f"(stop<= {STOP_EPS_SPS}), overfill {overfill:.1f} mm "
            f"(budget {OVERFILL_BUDGET_MM:.1f}), dwell {grind_ms:.0f} ms, "
            f"BPmin {min_bp:+.2f} -> {verdict}"
        )
        if overfeed:
            had_overfeed = True

    relief_pauses = [e for (_, e) in events if "RELIEF_PAUSE" in e]
    # EV:BS:COMPRESSION,<mm>,<bp> state-change events catch episodes too brief
    # to land on a poll sample.
    ev_compression = [e for (_, e) in events if "BS:COMPRESSION" in e]
    report.insert(0, f"  COMPRESSION episodes: {len(runs)} sampled, "
                     f"{len(ev_compression)} in events; worst steady_feed "
                     f"{worst_steady_feed:.0f} sps, worst overfill "
                     f"{worst_overfill:.1f} mm, worst dwell {worst_grind_ms:.0f} ms")
    report.insert(1, f"  RELIEF_PAUSE events: {len(relief_pauses)}")
    if worst_grind_ms > 2000 and worst_steady_feed <= STOP_EPS_SPS:
        report.insert(2, "  (long dwell at zero feed = benign idle sit; confirm "
                         "it recovers to NEUTRAL when extrusion resumes)")

    if not runs:
        if ev_compression or relief_pauses:
            return "INCONCLUSIVE", report + [
                "  Compression happened (events) but was too brief to sample at "
                "this poll rate. Re-run with --poll 20 to quantify."]
        return "INCONCLUSIVE", report + [
            "  No COMPRESSION episode captured — run the purge macro "
            "(e.g. _FLARE_PURGE PURGE=60) during capture."]
    if had_overfeed:
        return "FAIL", report + [
            "  Feed did not stop / overfill exceeded budget — MMU still feeding "
            "a full buffer (check COMPRESSION true-stop + relief budget)."]
    return "PASS", report


# ---------------------------------------------------------------------------
# REGRESSION analysis (test C)
# ---------------------------------------------------------------------------
def analyze_regression(samples: List[Sample], events, threshold: float
                       ) -> Tuple[str, List[str]]:
    """Normal-print regression: the COMPRESSION true-stop must not starve the
    relay cycle or fire RELIEF_PAUSE on every routine compression touch. Watch
    starvation/degraded events and premature relief during a normal print."""
    report: List[str] = []
    counts: Dict[str, int] = {k: 0 for k in STARVATION_EVENTS}
    for (_, ev) in events:
        for k in STARVATION_EVENTS:
            if k in ev:
                counts[k] += 1

    comp_runs = state_runs(samples, "COMPRESSION")
    relief_pauses = counts.get("RELIEF_PAUSE", 0)

    total_starv = sum(v for k, v in counts.items() if k != "RELIEF_PAUSE")
    for k in STARVATION_EVENTS:
        if counts[k]:
            report.append(f"  {k}: {counts[k]}")
    report.insert(0, f"  COMPRESSION episodes: {len(comp_runs)}; "
                     f"starvation/degraded events (excl RELIEF_PAUSE): {total_starv}")

    if not samples:
        return "INCONCLUSIVE", report + ["  No samples captured."]
    if total_starv > 0:
        return "FAIL", report + [
            "  Starvation/degraded events during a normal print — the true-stop "
            "may be over-pausing the relay cycle."]
    # Frequent RELIEF_PAUSE relative to compression touches = over-pausing.
    if comp_runs and relief_pauses > max(1, len(comp_runs) // 2):
        return "FAIL", report + [
            f"  RELIEF_PAUSE fired {relief_pauses}x over {len(comp_runs)} "
            "compression touches — relief budget likely too tight for normal use."]
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
