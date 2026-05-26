#!/usr/bin/env python3
"""
flare_sync_check.py — objective pass/fail for FLARE type-D sync buffer behavior.
Parses FLARE `?:` poll telemetry (live or from a captured log) and evaluates the
buffer-recovery contracts. Replaces the older purge-only checker; the purge and
regression checks are unchanged, plus two analyzers for the
sync-relief-rearm-hardening fixes (D1 re-arm, D2 estimator).

  PURGE (tests A/B): after a fast purge the buffer must NOT slam compression.
    The estimator (EST) must DECAY during the post-surge NEUTRAL glide instead
    of freezing at the high value it learned during the tension catch-up.

  REGRESSION (test C): during normal extrusion the demand-collapse corrector
    must NOT false-fire and must not introduce starvation. Counts starvation /
    low-confidence events and flags over-pausing (RELIEF_PAUSE per compression
    touch) — this also catches a D6 fast-brake limit cycle (maneuver M2).

  REARM (D1, maneuvers M1/M3): a RELIEF_PAUSE must re-arm (SYNC AUTO_START) when
    the buffer recovers — and on an idle capture it must NOT re-arm at all.
    --idle flips the expectation: any RELIEF_PAUSE -> AUTO_START is a FAIL
    (spurious end-of-print re-arm). Without --idle a RELIEF_PAUSE that never
    re-arms, or any cannot_refill, is a FAIL. Reports the BUF state at each
    re-arm so you can confirm NEUTRAL re-arm (the D1 improvement). M3 macros
    that intentionally end after a successful resume can use
    --allow-terminal-idle-relief to ignore one final idle RELIEF_PAUSE.

  ESTIMATOR (D2, maneuver M4): on a TENSION->COMPRESSION span the estimator must
    not spike. The span may pass through NEUTRAL, because real buffer motion and
    100 ms polling often miss a direct adjacent edge. FAIL if EST jumps by more
    than --est-spike-factor x its pre-transition value (with an absolute floor)
    — the modeled-overwrite spike the blend is meant to prevent.

USAGE
  Offline (analyze a captured log):
    python3 scripts/flare_sync_check.py --log purge.txt
    python3 scripts/flare_sync_check.py --log print.txt --mode regression
    python3 scripts/flare_sync_check.py --log idle.txt   --mode rearm --idle
    python3 scripts/flare_sync_check.py --log resume.txt  --mode rearm
    python3 scripts/flare_sync_check.py --log resume.txt  --mode rearm --allow-terminal-idle-relief
    python3 scripts/flare_sync_check.py --log spike.txt   --mode estimator
    python3 scripts/flare_sync_check.py --log run.txt     --mode all

  Live (capture while you run the macro / print, Ctrl+C to stop and analyze):
    python3 scripts/flare_sync_check.py --live --poll 100 --csv run.csv
    python3 scripts/flare_sync_check.py --daemon --poll 100 --csv run.csv
    python3 scripts/flare_sync_check.py --daemon --capture-log run.txt --csv run.csv

Geometry (defaults match config.ini buf_switch_span_mm=10, buf_max_travel_mm=25):
  --switch-span-mm 10   -> compression/tension switch at +/-5 mm
  --max-travel-mm 25    -> physical hard wall at +/-12.5 mm

Exit code: 0 = PASS, 1 = FAIL, 2 = INCONCLUSIVE (no relevant episode captured).
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
from typing import Dict, List, Optional, Tuple
import urllib.error
import urllib.request

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


def daemon_status_to_line(status: Dict[str, object]) -> Optional[str]:
    """Convert daemon /status JSON to the normal serial OK status shape."""
    raw = status.get("raw_status")
    if isinstance(raw, dict) and "BUF" in raw:
        return "OK:" + ",".join(f"{k}:{v}" for k, v in raw.items())

    if "buf_state" not in status:
        return None

    parts = [
        ("LN", status.get("active_lane", 0)),
        ("TC", status.get("tc_state", "UNKNOWN")),
        ("BP", f"{float(status.get('g_buf_pos', 0.0)):.3f}"),
        ("BUF", status.get("buf_state", "NEUTRAL")),
        ("SM", status.get("sync_enabled", 0)),
        ("MM", f"{float(status.get('sps', 0.0)):.3f}"),
        ("BF", f"{float(status.get('baseline_sps', 0.0)):.3f}"),
        ("EST", f"{float(status.get('extruder_est_sps', 0.0)):.3f}"),
        ("RE", f"{float(status.get('reserve_error_mm', 0.0)):.3f}"),
        ("AV", "0.000"),
        ("CT", "0"),
        ("ST", status.get("sync_enabled", 0)),
        ("I1", status.get("in1", 0)),
        ("O1", status.get("out1", 0)),
        ("I2", status.get("in2", 0)),
        ("O2", status.get("out2", 0)),
        ("TH", status.get("toolhead", 0)),
        ("YS", status.get("y_split", 0)),
        ("RELOAD", status.get("reload_mode", 0)),
        ("SYNC_RELIEVE_MM", "0"),
        ("SYNC_REFILL_MM", "0"),
    ]
    return "OK:" + ",".join(f"{k}:{v}" for k, v in parts)


def daemon_event_to_line(event: Dict[str, object]) -> Optional[str]:
    evt_type = str(event.get("type", "")).strip()
    evt_data = str(event.get("data", "")).strip()
    if not evt_type:
        return None
    if evt_data:
        return f"EV:{evt_type},{evt_data}"
    return f"EV:{evt_type}"


def daemon_event_key(event: Dict[str, object]) -> Tuple[object, object, object]:
    return (event.get("time"), event.get("type"), event.get("data"))


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
    relief budget while the lane is still feeding, not the multi-second /
    multi-mm grind of the old SYNC_MIN-forward behavior. A long dwell with
    steady feed at zero is idle relief/settling and is reported separately.
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
        moving_overfill = overfill > OVERFILL_BUDGET_MM and not (
            have_steady and steady_feed <= STOP_EPS_SPS)
        hit_hardwall = abs(min_bp) >= hardwall
        overfeed = sustained_feed or moving_overfill or hit_hardwall
        worst_overfill = max(worst_overfill, overfill)
        if have_steady:
            worst_steady_feed = max(worst_steady_feed, steady_feed)
        worst_grind_ms = max(worst_grind_ms, grind_ms)
        verdict = "OVERFEED" if overfeed else ("OK" if have_steady else "OK(brief)")
        if not overfeed and overfill > OVERFILL_BUDGET_MM:
            verdict = "OK(idle-relief)"
        feed_str = f"{steady_feed:.0f} sps" if have_steady else "n/a (brief)"
        report.append(
            f"  COMPRESSION idx[{a}-{b}] steady_feed {feed_str} "
            f"(stop<= {STOP_EPS_SPS}), overfill {overfill:.1f} mm "
            f"(budget {OVERFILL_BUDGET_MM:.1f}), dwell {grind_ms:.0f} ms, "
            f"BPmin {min_bp:+.2f} (hardwall {hardwall:.1f}) -> {verdict}"
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
            "  Feed did not stop, moving overfill exceeded budget, or hardwall "
            "was reached (check COMPRESSION true-stop + relief budget)."]
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
# REARM analysis (D1 — maneuvers M1 idle / M3 resume)
# ---------------------------------------------------------------------------
def _buf_at(samples: List[Sample], event_idx: int) -> Optional[str]:
    """BUF state of the sample nearest an event. events store the sample count at
    emit time, i.e. the index of the next sample."""
    if not samples:
        return None
    j = event_idx if event_idx < len(samples) else len(samples) - 1
    return samples[j].s("BUF")


def _terminal_idle_after(samples: List[Sample], event_idx: int) -> bool:
    if not samples:
        return False
    start = event_idx if event_idx < len(samples) else len(samples) - 1
    tail = samples[start:]
    if not tail:
        return False
    last = samples[-1]
    last_buf = last.s("BUF")
    if last.f("SM") != 0 or last.f("MM") > STOP_EPS_SPS:
        return False
    if last_buf not in ("NEUTRAL", "COMPRESSION"):
        return False
    if any(s.s("BUF") == "TENSION" or s.f("MM") > STOP_EPS_SPS for s in tail):
        return False
    idle_tail = samples[-min(3, len(samples)):]
    return all(s.f("SM") == 0 and s.f("MM") <= STOP_EPS_SPS for s in idle_tail)


def analyze_rearm(samples: List[Sample], events, idle: bool,
                  allow_terminal_idle_relief: bool = False
                  ) -> Tuple[str, List[str]]:
    """D1: a RELIEF_PAUSE must re-arm (SYNC AUTO_START) when the buffer recovers.
    On an --idle capture it must NOT re-arm (spurious end-of-print re-arm). Each
    re-arm records the BUF state so a NEUTRAL re-arm (the D1 improvement) is
    visible."""
    report: List[str] = []
    n_relief = 0
    rearm_states: List[str] = []
    pending_idx: Optional[int] = None
    for (si, ev) in events:
        if "RELIEF_PAUSE" in ev:
            n_relief += 1
            pending_idx = si
        elif "AUTO_START" in ev and pending_idx is not None:
            rearm_states.append(_buf_at(samples, si) or "?")
            pending_idx = None
    n_rearm = len(rearm_states)
    cannot = sum(1 for (_, e) in events if "cannot_refill" in e)
    ignored_terminal_idle = 0
    if (not idle and allow_terminal_idle_relief and pending_idx is not None and
            n_rearm > 0 and _terminal_idle_after(samples, pending_idx)):
        ignored_terminal_idle = 1

    report.append(f"  RELIEF_PAUSE: {n_relief}, re-arms (AUTO_START after pause): "
                  f"{n_rearm}")
    if rearm_states:
        report.append(f"  re-arm BUF states: {', '.join(rearm_states)}")
    if cannot:
        report.append(f"  cannot_refill: {cannot}")
    if ignored_terminal_idle:
        report.append("  ignored terminal idle RELIEF_PAUSE: 1")

    if n_relief == 0:
        return "INCONCLUSIVE", report + [
            "  No RELIEF_PAUSE captured — run M1 (idle) or M3 (pause/resume)."]
    if idle:
        if n_rearm == 0:
            return "PASS", report + ["  idle: no spurious re-arm (correct)."]
        return "FAIL", report + [
            "  idle capture re-armed — spurious end-of-print AUTO_START "
            "(D1 over-fires; check the !g_boot_stabilizing guard)."]
    if cannot:
        return "FAIL", report + [
            "  cannot_refill during resume — starved before/at re-arm."]
    effective_relief = n_relief - ignored_terminal_idle
    if n_rearm < effective_relief:
        return "FAIL", report + [
            f"  {effective_relief - n_rearm} RELIEF_PAUSE(s) never re-armed — "
            "stuck paused."]
    extra = []
    if any(s == "NEUTRAL" for s in rearm_states):
        extra.append("  re-armed from NEUTRAL (D1 path exercised).")
    return "PASS", report + extra


# ---------------------------------------------------------------------------
# BUFFER-LOCK analysis (task 7.1)
# ---------------------------------------------------------------------------
def analyze_buffer_lock(samples: List[Sample], events) -> Tuple[str, List[str]]:
    """BL lifecycle check (task 7.1).

    Asserts:
      1. BL:PRIME event seen (arm accepted).
      2. BL:LOCKED event seen (prime completed to target state or bound).
      3. BL:BREAK event seen (lock-break triggered the catch).
      4. Either BL:CATCH_SETTLE or EV:BL:TIMEOUT seen (lifecycle completed).
      5. No FAULT:MOVE_TENSION or FAULT:MOVE_COMPRESSION events between
         BL:BREAK and CATCH_SETTLE/TIMEOUT (catch does not trigger MV guards).

    Inconclusive when no BL events found — run with BL:T active and
    a printer-side retract to exercise the full lifecycle.
    """
    report: List[str] = []

    n_prime   = sum(1 for (_, e) in events if "BL" in e and "PRIME" in e and "BOUND" not in e)
    n_locked  = sum(1 for (_, e) in events if "BL" in e and "LOCKED" in e)
    n_break   = sum(1 for (_, e) in events if "BL" in e and "BREAK" in e)
    n_settle  = sum(1 for (_, e) in events if "BL" in e and ("CATCH_SETTLE" in e or "TIMEOUT" in e))
    n_timeout = sum(1 for (_, e) in events if "EV:BL" in e and "TIMEOUT" in e)
    n_bound   = sum(1 for (_, e) in events if "EV:BL" in e and "PRIME_BOUND" in e)
    n_mv_fault = sum(1 for (_, e) in events
                     if "FAULT:MOVE_TENSION" in e or "FAULT:MOVE_COMPRESSION" in e)

    report.append(f"  BL PRIME: {n_prime}  LOCKED: {n_locked}  BREAK: {n_break}"
                  f"  SETTLE/TIMEOUT: {n_settle}  PRIME_BOUND: {n_bound}")
    if n_bound:
        report.append(f"  WARNING: prime hit half-travel cap {n_bound}x "
                      "(buffer may not have reached target extreme).")
    if n_timeout:
        report.append(f"  NOTE: watchdog timeout fired {n_timeout}x.")

    if n_prime == 0 and n_locked == 0 and n_break == 0:
        return "INCONCLUSIVE", report + [
            "  No BL lifecycle events found. Run: BL:T, wait ~1s, "
            "perform a printer-side retract, then BS."]

    failed = False
    if n_prime == 0:
        report.append("  FAIL: no BL:PRIME event — arm never started.")
        failed = True
    if n_locked == 0:
        report.append("  FAIL: no BL:LOCKED event — prime never completed.")
        failed = True
    if n_break == 0:
        report.append("  FAIL: no BL:BREAK event — lock-break never triggered "
                      "(was a printer retract executed after BL:T?).")
        failed = True
    if n_settle == 0 and n_break > 0:
        report.append("  FAIL: lock-break fired but no CATCH_SETTLE or TIMEOUT "
                      "— catch never released (was BS sent after M400?).")
        failed = True

    # Check for MV faults during catch window
    # (approximate: any FAULT:MOVE_* after the first BL:BREAK is suspicious)
    break_idx = next((si for (si, e) in events
                      if "BL" in e and "BREAK" in e), None)
    settle_idx = next((si for (si, e) in events
                       if "BL" in e and ("CATCH_SETTLE" in e or "TIMEOUT" in e)), None)
    mv_during_catch = []
    for si, e in events:
        if "FAULT:MOVE_TENSION" in e or "FAULT:MOVE_COMPRESSION" in e:
            if break_idx is not None and si > break_idx:
                if settle_idx is None or si < settle_idx:
                    mv_during_catch.append(e.strip())

    if mv_during_catch:
        report.append(f"  FAIL: {len(mv_during_catch)} MV fault(s) during catch "
                      f"window: {mv_during_catch[:3]}")
        failed = True
    else:
        report.append("  no FAULT:MOVE_* during catch window (correct).")

    if failed:
        return "FAIL", report
    return "PASS", report + ["  full prime→locked→catch→settle lifecycle verified."]


def analyze_stabilize(samples: List[Sample], events) -> Tuple[str, List[str]]:
    """D1 (M1 Recipe A): a boot/BS stabilize must drive the buffer to NEUTRAL
    (BUF_STAB DONE) without a spurious SYNC AUTO_START. Sync stays off during
    the stabilize path, so any AUTO_START means the !g_boot_stabilizing guard
    let it re-arm. Scope the capture to the maneuver (MV->COMPRESSION then BS)."""
    report: List[str] = []
    n_start = sum(1 for (_, e) in events if "BUF_STAB" in e and "START" in e)
    n_done = sum(1 for (_, e) in events if "BUF_STAB" in e and "DONE" in e)
    n_timeout = sum(1 for (_, e) in events if "BUF_STAB" in e and "TIMEOUT" in e)
    n_auto = sum(1 for (_, e) in events if "AUTO_START" in e)
    done_idx = next((si for (si, e) in events
                     if "BUF_STAB" in e and "DONE" in e), None)

    report.append(f"  BUF_STAB START: {n_start}, DONE: {n_done}, "
                  f"TIMEOUT: {n_timeout}; AUTO_START: {n_auto}")
    if done_idx is not None:
        report.append(f"  BUF at DONE: {_buf_at(samples, done_idx) or '?'}")

    if n_start == 0 and n_done == 0 and n_timeout == 0:
        return "INCONCLUSIVE", report + [
            "  No BUF_STAB captured — run Recipe A (MV:25:600 -> BS)."]
    if n_auto > 0:
        return "FAIL", report + [
            "  spurious SYNC AUTO_START during stabilize "
            "(check the !g_boot_stabilizing guard)."]
    if n_timeout > 0:
        return "FAIL", report + [
            "  BUF_STAB TIMEOUT — stabilize did not reach NEUTRAL."]
    if n_done > 0:
        return "PASS", report + [
            "  stabilize reached DONE with no spurious re-arm."]
    return "INCONCLUSIVE", report


# ---------------------------------------------------------------------------
# STABILITY analysis (buffer-state-lock task 9.10 — sync loop ringing)
# ---------------------------------------------------------------------------
def analyze_stability(samples: List[Sample], events,
                      poll_ms: int = 100,
                      cycle_threshold_hz: float = 1.0,
                      window_sec: int = 3) -> Tuple[str, List[str]]:
    """Detect closed-loop sync ringing. Sustained BUF state oscillation greater
    than 1 cycle/sec (2 transitions/sec) over any window_sec window indicates
    loop instability — typically sync_kp_rate too high for the active
    sync_ramp_accel. Tune via sync_kp_rate <- sync_kp_rate / k, where
    k = sync_ramp_accel / 33.

    Run during a typical print soak with mixed flow; capture at the default
    100ms poll for at least 30 seconds. PASS = peak window rate within
    threshold; FAIL = peak exceeds threshold; INCONCLUSIVE = capture too short.
    """
    report: List[str] = []
    if len(samples) < 30:
        return "INCONCLUSIVE", report + [
            "  too few samples; capture at least ~30s of print activity."]

    samples_per_sec = max(1, int(round(1000.0 / max(1, poll_ms))))
    window_n = samples_per_sec * window_sec
    # 2 transitions per cycle; allow the threshold rate over the full window.
    transitions_threshold = int(round(cycle_threshold_hz * 2 * window_sec))

    transitions: List[int] = []
    prev = None
    for i, smp in enumerate(samples):
        cur = smp.s("BUF")
        if cur is None:
            continue
        if prev is not None and cur != prev:
            transitions.append(i)
        prev = cur

    total_transitions = len(transitions)
    duration_s = len(samples) * poll_ms / 1000.0
    avg_hz = (total_transitions / duration_s) if duration_s > 0 else 0.0

    peak_count = 0
    peak_at = -1
    for start_idx in range(0, max(1, len(samples) - window_n + 1)):
        end_idx = start_idx + window_n
        count = sum(1 for t in transitions if start_idx <= t < end_idx)
        if count > peak_count:
            peak_count = count
            peak_at = start_idx
    peak_hz = peak_count / window_sec if window_sec else 0.0
    peak_cycles_hz = peak_hz / 2.0

    state_counts: Dict[str, int] = {}
    for smp in samples:
        cur = smp.s("BUF")
        if cur:
            state_counts[cur] = state_counts.get(cur, 0) + 1
    total_known = sum(state_counts.values())
    state_pct = {k: (100.0 * v / total_known if total_known else 0.0)
                 for k, v in state_counts.items()}

    report.append(f"  capture: {len(samples)} samples ({duration_s:.1f}s "
                  f"@ {poll_ms}ms poll = {samples_per_sec}/s)")
    report.append(f"  BUF transitions: {total_transitions} total "
                  f"({avg_hz:.2f} transitions/s avg)")
    report.append(f"  peak in {window_sec}s window: {peak_count} transitions "
                  f"({peak_hz:.2f}/s = {peak_cycles_hz:.2f} cycles/s) "
                  f"starting at sample {peak_at}")
    report.append("  BUF time-in-state: " +
                  ", ".join(f"{k} {v:.0f}%" for k, v in
                            sorted(state_pct.items(), key=lambda x: -x[1])))

    if peak_count > transitions_threshold:
        return "FAIL", report + [
            f"  ringing: peak {peak_cycles_hz:.2f} cycles/s > "
            f"{cycle_threshold_hz:.2f} threshold over {window_sec}s window.",
            "  mitigation: SET:SYNC_KP_RATE:<current/k> where "
            "k = sync_ramp_accel / 33. See design.md §D6a."]

    return "PASS", report + [
        f"  no sustained ringing: peak {peak_cycles_hz:.2f} cycles/s under "
        f"{cycle_threshold_hz:.2f} threshold."]


# ---------------------------------------------------------------------------
# DRIFT analysis (companion to STABILITY for autotune saddle search)
# ---------------------------------------------------------------------------
def analyze_drift(samples: List[Sample],
                  endstop_threshold_pct: float = 30.0
                  ) -> Tuple[str, List[str]]:
    """Time-in-endstop check: if the buffer spends more than threshold% at
    TENSION or COMPRESSION combined, sync is lagging the extruder demand —
    typically because sync_kp_rate is too low. Companion to analyze_stability
    for autotune: ringing means kp too high, drift means kp too low; healthy
    print has buffer mostly at NEUTRAL with brief endstop excursions."""
    report: List[str] = []
    counts = {"TENSION": 0, "COMPRESSION": 0, "NEUTRAL": 0}
    total = 0
    for s in samples:
        cur = s.s("BUF")
        if cur in counts:
            counts[cur] += 1
            total += 1
    if total < 10:
        return "INCONCLUSIVE", report + [
            "  too few samples for drift analysis"]

    pct = {k: 100.0 * v / total for k, v in counts.items()}
    endstop_pct = pct["TENSION"] + pct["COMPRESSION"]
    report.append(f"  BUF time: NEUTRAL {pct['NEUTRAL']:.0f}% "
                  f"TENSION {pct['TENSION']:.0f}% "
                  f"COMPRESSION {pct['COMPRESSION']:.0f}%")
    report.append(f"  endstop total: {endstop_pct:.0f}% "
                  f"(threshold {endstop_threshold_pct:.0f}%)")
    if endstop_pct > endstop_threshold_pct:
        return "FAIL", report + [
            f"  drift: sync lagging extruder demand. "
            f"Raise SYNC_KP_RATE."]
    return "PASS", report


# ---------------------------------------------------------------------------
# ESTIMATOR analysis (D2 — maneuver M4)
# ---------------------------------------------------------------------------
def analyze_estimator(samples: List[Sample], events, factor: float,
                      window: int, est_floor: float,
                      transition_gap: int = 50) -> Tuple[str, List[str]]:
    """D2: on a TENSION->COMPRESSION span the estimator must not spike.
    The span may pass through NEUTRAL. FAIL if EST jumps by more than `factor`
    x its pre-transition value (with an absolute floor to ignore noise on tiny
    values)."""
    report: List[str] = []
    transitions = 0
    worst_ratio = 0.0
    worst: Optional[Tuple[int, float, float]] = None
    failed = False
    pending_idx: Optional[int] = None
    pending_est: Optional[float] = None
    for i, smp in enumerate(samples):
        buf = smp.s("BUF")
        if buf == "TENSION":
            pending_idx = i
            pending_est = smp.f("EST")
            continue
        if pending_idx is not None and i - pending_idx > transition_gap:
            pending_idx = None
            pending_est = None
        if buf == "COMPRESSION" and pending_idx is not None:
            transitions += 1
            pre = pending_est
            post_vals = [samples[k].f("EST")
                         for k in range(i, min(i + window, len(samples)))
                         if samples[k].f("EST") is not None]
            post = max(post_vals) if post_vals else None
            if pre is not None and post is not None:
                ratio = post / max(pre, est_floor)
                if ratio > worst_ratio:
                    worst_ratio = ratio
                    worst = (i, pre, post)
                if ratio > factor and post > est_floor:
                    failed = True
            pending_idx = None
            pending_est = None

    report.append(f"  TENSION->COMPRESSION spans: {transitions}; "
                  f"worst EST ratio {worst_ratio:.2f}x (cap {factor:.2f})")
    if worst:
        report.append(f"  worst at idx {worst[0]}: EST {worst[1]:.0f} -> "
                      f"{worst[2]:.0f} mm/min")
    if transitions == 0:
        return "INCONCLUSIVE", report + [
            "  No TENSION->COMPRESSION span — run M4 (fast disturbance)."]
    if failed:
        return "FAIL", report + [
            "  estimator spiked on a modeled transition — D2 blend not limiting it."]
    return "PASS", report


# ---------------------------------------------------------------------------
# IO
# ---------------------------------------------------------------------------
def capture_live(port: Optional[str], poll_ms: int, duration: Optional[float]
                 ) -> List[str]:
    try:
        import serial  # lazy
    except ImportError:
        print("flare_sync_check: pyserial not installed. pip install pyserial",
              file=sys.stderr)
        sys.exit(1)
    sys.path.insert(0, __import__("os").path.dirname(__file__))
    from serial_utils import find_port  # type: ignore

    dev = find_port(port)
    if not dev:
        print("flare_sync_check: no serial port found", file=sys.stderr)
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
                print("# Capture duration reached.", flush=True)
                break
            time.sleep(max(0.0, interval - (time.time() - t0)))
    except KeyboardInterrupt:
        print("\n# Capture stopped.", flush=True)
    finally:
        ser.close()
    return lines


def daemon_send_cmd(url: str, cmd_str: str,
                    timeout: float = 5.0) -> Optional[str]:
    """POST a single FLARE command (e.g. 'SET:SYNC_KP_RATE:200') to the
    daemon /cmd endpoint and return the device response string, or None on
    failure."""
    try:
        data = json.dumps({"cmd": cmd_str}).encode("utf-8")
        req = urllib.request.Request(
            f"{url}/cmd", data=data,
            headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=timeout + 1.0) as response:
            if response.status == 200:
                res = json.loads(response.read().decode("utf-8"))
                return res.get("response")
    except Exception:
        return None
    return None


def daemon_get_float(url: str, name: str) -> Optional[float]:
    """GET:<name> via daemon, parse trailing float from OK:NAME:value."""
    resp = daemon_send_cmd(url, f"GET:{name}")
    if not resp:
        return None
    parts = resp.strip().split(":")
    try:
        return float(parts[-1])
    except (ValueError, IndexError):
        return None


def capture_daemon(url: str, poll_ms: int, duration: Optional[float]) -> List[str]:
    interval = poll_ms / 1000.0
    endpoint = url.rstrip("/") + "/status"
    lines: List[str] = []
    seen_events = set()
    seeded_events = False
    deadline = time.time() + duration if duration else None
    print(f"# Capturing via daemon {endpoint} every {poll_ms} ms. "
          "Run the macro/print now. Ctrl+C to stop and analyze.", flush=True)
    try:
        while True:
            t0 = time.time()
            try:
                with urllib.request.urlopen(endpoint, timeout=2.0) as resp:
                    status = json.loads(resp.read().decode("utf-8"))
            except (OSError, urllib.error.URLError, json.JSONDecodeError) as e:
                print(f"# daemon status read failed: {e}", file=sys.stderr,
                      flush=True)
                status = {}

            events = status.get("events", [])
            if isinstance(events, list):
                for event in events:
                    if not isinstance(event, dict):
                        continue
                    key = daemon_event_key(event)
                    if key in seen_events:
                        continue
                    seen_events.add(key)
                    if not seeded_events:
                        continue
                    line = daemon_event_to_line(event)
                    if line:
                        lines.append(line)
                seeded_events = True

            line = daemon_status_to_line(status)
            if line:
                lines.append(line)

            if deadline and time.time() >= deadline:
                print("# Capture duration reached.", flush=True)
                break
            time.sleep(max(0.0, interval - (time.time() - t0)))
    except KeyboardInterrupt:
        print("\n# Capture stopped.", flush=True)
    return lines


def run_tune(args) -> int:
    """Autotune SYNC_KP_RATE during a live print. Saddle search:
    ringing -> kp too high -> divide by step; drift -> kp too low ->
    multiply by step. Converges when both stability and drift PASS.

    Requires --daemon (the device is shared; live serial would conflict).
    The operator must be actively printing so the loop has disturbances to
    measure. Bails on min/max kp or unstable-at-this-accel signature.
    """
    if not args.daemon:
        print("ERROR: --mode tune requires --daemon (shared device access).")
        return 1
    url = args.daemon_url

    kp_initial = daemon_get_float(url, "SYNC_KP_RATE")
    accel_now = daemon_get_float(url, "SYNC_RAMP_ACCEL")
    if kp_initial is None or accel_now is None:
        print("ERROR: failed to read SYNC_KP_RATE / SYNC_RAMP_ACCEL via daemon.")
        return 1

    if args.tune_set_accel is not None:
        resp = daemon_send_cmd(url, f"SET:SYNC_RAMP_ACCEL:{args.tune_set_accel}")
        if resp is None:
            print("ERROR: failed to SET SYNC_RAMP_ACCEL.")
            return 1
        accel_now = float(args.tune_set_accel)
        print(f"# set SYNC_RAMP_ACCEL={accel_now:.0f} mm/s²")

    kp_min = args.tune_kp_min
    kp_max = args.tune_kp_max if args.tune_kp_max > 0 else kp_initial * 4.0
    kp_cur = kp_initial
    history: List[Tuple[float, str, str]] = []

    print(f"# autotune start: kp={kp_cur:.0f}, sync_ramp_accel={accel_now:.0f}")
    print(f"# bounds: kp ∈ [{kp_min:.0f}, {kp_max:.0f}], "
          f"step ×/{args.tune_kp_step}, max {args.tune_iter_max} iterations")
    print(f"# window {args.tune_window_sec}s, poll {args.poll}ms")
    print(f"# NOTE: print must be active throughout. Ctrl+C aborts cleanly.")

    try:
        for i in range(args.tune_iter_max):
            print(f"\n=== iter {i+1}/{args.tune_iter_max}: "
                  f"kp={kp_cur:.0f}, accel={accel_now:.0f} ===")
            lines = capture_daemon(url, args.poll,
                                   float(args.tune_window_sec))
            samples, events = parse_stream(lines)
            print(f"# parsed {len(samples)} samples")

            s_v, s_rep = analyze_stability(
                samples, events, args.poll,
                args.stability_cycle_hz, args.stability_window_sec)
            d_v, d_rep = analyze_drift(samples, args.tune_drift_pct)

            print(f"STABILITY: {s_v}")
            for line in s_rep:
                print(line)
            print(f"DRIFT: {d_v}")
            for line in d_rep:
                print(line)
            history.append((kp_cur, s_v, d_v))

            if s_v == "PASS" and d_v == "PASS":
                print(f"\nCONVERGED: SYNC_KP_RATE={kp_cur:.0f} mm/min "
                      f"@ SYNC_RAMP_ACCEL={accel_now:.0f} mm/s².")
                print(f"  apply permanently with: "
                      f"SET:SYNC_KP_RATE:{int(round(kp_cur))}")
                return 0

            if s_v == "FAIL" and d_v == "FAIL":
                print(f"\nUNSTABLE: ringing AND drift at kp={kp_cur:.0f}. "
                      f"System cannot follow at SYNC_RAMP_ACCEL={accel_now:.0f}. "
                      f"Try lowering accel and re-run.")
                return 1
            if s_v == "INCONCLUSIVE" and d_v == "INCONCLUSIVE":
                print(f"\nINCONCLUSIVE: capture too quiet — extruder idle? "
                      f"Print harder or raise --tune-window-sec.")
                return 2

            if s_v == "FAIL":
                kp_cur = kp_cur / args.tune_kp_step
                why = "ringing -> lower kp"
            elif d_v == "FAIL":
                kp_cur = kp_cur * args.tune_kp_step
                why = "drift -> raise kp"
            else:
                kp_cur = kp_cur / args.tune_kp_step
                why = "partial PASS -> nudge kp down"

            if kp_cur < kp_min:
                print(f"\nFLOOR: kp would go below {kp_min:.0f}; bail.")
                return 1
            if kp_cur > kp_max:
                print(f"\nCEILING: kp would exceed {kp_max:.0f}; bail.")
                return 1

            kp_int = int(round(kp_cur))
            resp = daemon_send_cmd(url, f"SET:SYNC_KP_RATE:{kp_int}")
            if resp is None:
                print(f"\nERROR: failed to SET:SYNC_KP_RATE:{kp_int}")
                return 1
            print(f"# {why}: SET SYNC_KP_RATE={kp_int} (was {history[-1][0]:.0f})")

        print(f"\nMAX_ITER: did not converge in {args.tune_iter_max} steps.")
        print(f"  history: {history}")
        return 1
    except KeyboardInterrupt:
        print(f"\n# autotune aborted. restoring SYNC_KP_RATE={int(kp_initial)}.")
        daemon_send_cmd(url, f"SET:SYNC_KP_RATE:{int(round(kp_initial))}")
        return 130


def write_csv(path: str, samples: List[Sample]) -> None:
    import csv
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(CSV_FIELDS)
        for s in samples:
            w.writerow([s.idx] + [s.fields.get(k, "") for k in CSV_FIELDS[1:]])


def write_capture_log(path: str, lines: List[str]) -> None:
    with open(path, "w") as fh:
        for line in lines:
            fh.write(line)
            fh.write("\n")


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--log", help="Analyze a captured telemetry log file")
    src.add_argument("--live", action="store_true",
                     help="Capture live from the device, then analyze")
    src.add_argument("--daemon", action="store_true",
                     help="Capture live through flare_daemon /status")
    ap.add_argument("--port", help="Serial port (auto-detect if omitted)")
    ap.add_argument("--daemon-url", default="http://127.0.0.1:8088",
                    help="flare_daemon base URL for --daemon")
    ap.add_argument("--poll", type=int, default=100, help="Live poll interval ms")
    ap.add_argument("--duration", type=float,
                    help="Live capture seconds (default: until Ctrl+C)")
    ap.add_argument("--capture-log",
                    help="Write captured OK:/EV: stream to this file")
    ap.add_argument("--csv", help="Write parsed samples to this CSV path")
    ap.add_argument("--mode",
                    choices=("purge", "regression", "rearm", "estimator",
                             "stabilize", "stability", "buffer-lock",
                             "tune", "both", "all"),
                    default="both",
                    help="Which check(s) to run ('both'=purge+regression, "
                         "'all'=every analyzer). 'stabilize'=M1 Recipe A "
                         "(BUF_STAB -> NEUTRAL, no spurious AUTO_START). "
                         "'stability'=sync-loop ringing during print soak "
                         "(task 9.10). 'buffer-lock'=BL lifecycle "
                         "(prime/locked/catch/settle and no MV faults). "
                         "'tune'=autotune SYNC_KP_RATE during a live print "
                         "(requires --daemon).")
    ap.add_argument("--stability-cycle-hz", type=float, default=1.0,
                    help="stability mode: max allowed BUF cycles/sec over the "
                         "sliding window before FAIL (default 1.0).")
    ap.add_argument("--stability-window-sec", type=int, default=3,
                    help="stability mode: sliding window length in seconds "
                         "(default 3).")
    ap.add_argument("--tune-set-accel", type=float, default=None,
                    help="tune mode: SET SYNC_RAMP_ACCEL to this (mm/s²) "
                         "before starting; default leaves it as-is.")
    ap.add_argument("--tune-iter-max", type=int, default=6,
                    help="tune mode: max search iterations (default 6).")
    ap.add_argument("--tune-window-sec", type=int, default=30,
                    help="tune mode: capture seconds per iteration (default 30).")
    ap.add_argument("--tune-kp-step", type=float, default=1.5,
                    help="tune mode: kp multiplier/divisor per step (default 1.5).")
    ap.add_argument("--tune-kp-min", type=float, default=100.0,
                    help="tune mode: bail if kp would go below this (mm/min).")
    ap.add_argument("--tune-kp-max", type=float, default=0.0,
                    help="tune mode: bail if kp would exceed this (mm/min); "
                         "0 = 4× the starting kp.")
    ap.add_argument("--tune-drift-pct", type=float, default=30.0,
                    help="tune mode: max combined TENSION+COMPRESSION time %% "
                         "before drift FAIL (default 30).")
    ap.add_argument("--idle", action="store_true",
                    help="rearm mode: this capture is idle — any re-arm is a FAIL")
    ap.add_argument("--allow-terminal-idle-relief", action="store_true",
                    help="rearm mode: ignore one final idle RELIEF_PAUSE after "
                         "a successful resume re-arm")
    ap.add_argument("--est-spike-factor", type=float, default=2.0,
                    help="estimator mode: max allowed EST jump ratio on a "
                         "TENSION->COMPRESSION transition")
    ap.add_argument("--est-window", type=int, default=5,
                    help="estimator mode: samples after a transition to scan for "
                         "the EST peak")
    ap.add_argument("--est-floor", type=float, default=100.0,
                    help="estimator mode: EST floor (mm/min) below which jumps "
                         "are treated as noise")
    ap.add_argument("--est-transition-gap", type=int, default=50,
                    help="estimator mode: max samples allowed between TENSION "
                         "and later COMPRESSION")
    ap.add_argument("--switch-span-mm", type=float, default=10.0)
    ap.add_argument("--max-travel-mm", type=float, default=25.0)
    args = ap.parse_args()

    threshold = args.switch_span_mm / 2.0
    hardwall = args.max_travel_mm / 2.0

    if args.mode == "tune":
        return run_tune(args)

    if args.live:
        lines = capture_live(args.port, args.poll, args.duration)
    elif args.daemon:
        lines = capture_daemon(args.daemon_url, args.poll, args.duration)
    else:
        with open(args.log, "r", errors="ignore") as fh:
            lines = fh.readlines()

    samples, events = parse_stream(lines)
    print(f"# parsed {len(samples)} samples, {len(events)} events")
    if args.capture_log:
        write_capture_log(args.capture_log, lines)
        print(f"# wrote {args.capture_log}")
    if args.csv:
        write_csv(args.csv, samples)
        print(f"# wrote {args.csv}")

    verdicts: List[str] = []
    if args.mode in ("purge", "both", "all"):
        v, rep = analyze_purge(samples, events, threshold, hardwall)
        print(f"\nPURGE (A/B): {v}")
        for line in rep:
            print(line)
        verdicts.append(v)
    if args.mode in ("regression", "both", "all"):
        v, rep = analyze_regression(samples, events, threshold)
        print(f"\nREGRESSION (C): {v}")
        for line in rep:
            print(line)
        verdicts.append(v)
    if args.mode in ("rearm", "all"):
        v, rep = analyze_rearm(samples, events, args.idle,
                               args.allow_terminal_idle_relief)
        print(f"\nREARM (D1{', idle' if args.idle else ''}): {v}")
        for line in rep:
            print(line)
        verdicts.append(v)
    if args.mode in ("estimator", "all"):
        v, rep = analyze_estimator(samples, events, args.est_spike_factor,
                                   args.est_window, args.est_floor,
                                   args.est_transition_gap)
        print(f"\nESTIMATOR (D2): {v}")
        for line in rep:
            print(line)
        verdicts.append(v)
    if args.mode in ("stabilize", "all"):
        v, rep = analyze_stabilize(samples, events)
        print(f"\nSTABILIZE (D1, M1-A): {v}")
        for line in rep:
            print(line)
        verdicts.append(v)
    if args.mode in ("stability", "all"):
        v, rep = analyze_stability(samples, events, args.poll,
                                   args.stability_cycle_hz,
                                   args.stability_window_sec)
        print(f"\nSTABILITY (task 9.10): {v}")
        for line in rep:
            print(line)
        verdicts.append(v)
    if args.mode in ("buffer-lock", "all"):
        v, rep = analyze_buffer_lock(samples, events)
        print(f"\nBUFFER-LOCK: {v}")
        for line in rep:
            print(line)
        verdicts.append(v)

    if "FAIL" in verdicts:
        return 1
    if all(v == "INCONCLUSIVE" for v in verdicts):
        return 2
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        try:
            devnull = os.open(os.devnull, os.O_WRONLY)
            os.dup2(devnull, sys.stdout.fileno())
        except OSError:
            pass
        sys.exit(1)
