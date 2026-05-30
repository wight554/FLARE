#!/usr/bin/env python3
"""FLARE operator-assisted HIL test runner for the buffer flows.

Drives a real board through ``flare_daemon`` (see ``flare_hil_harness``) and
asserts the emitted ``EV:`` events for each buffer flow:

    sync     PD control reactions + auto-sync toggle (D18)
    stab     idle BUF_STAB normalize-to-goal (type-P D23 Gate A) / to-neutral (D)
    buflock  BL: prime/lock/follow (D19/D20)
    load     FL flow-load completion + guards (buffer-relevant load only)
    unload   UL over-tension guard (type-P D22) + type-D parity

Cases that stage a *buffer position* use ``await_buffer`` (live g_buf_pos
readout, auto-proceeds at the target, ENTER to force). Cases that stage a
*sensor/filament* condition use ``prompt`` (operator ack). Type-P is the focus;
type-D parity cases are tagged and only run with ``--type d``.

Prereqs: a powered board, a running ``flare_daemon`` (owns the serial port), and
an operator at the bench. NOT a CI suite — the harness parsing logic is
unit-tested separately in ``test_flare_hil_harness.py``.

Usage:
    python3 scripts/flare_hil.py --list
    python3 scripts/flare_hil.py --flow unload            # type-P (default)
    python3 scripts/flare_hil.py --flow all
    python3 scripts/flare_hil.py --flow stab --type d     # type-D parity
"""

import argparse
import sys
import time
from collections import namedtuple

from flare_hil_harness import HilBoard, HilError

# run(board) raises AssertionError/HilError on failure.
Case = namedtuple("Case", "id flow title buf_types run")

FLOWS = ("sync", "stab", "buflock", "load", "unload")

_REGISTRY = []


def case(flow, cid, title, buf_types=("p",)):
    def deco(fn):
        _REGISTRY.append(Case(cid, flow, title, buf_types, fn))
        return fn
    return deco


def _reset(board):
    """Quiesce between cases: stop motion, sync off, drop stale events."""
    board.send("ST:")
    board.send("SM:0")
    time.sleep(0.2)
    board.clear_events()


# ---------------------------------------------------------------------------
# UNLOAD  (type-P: no position-based guard; type-D: UNLOAD_TENSION_BLOCK)
# ---------------------------------------------------------------------------

@case("unload", "ul_healthy_completes_p", "P: UL retracts to completion, no spurious block")
def _ul_healthy(board):
    # Type-P has no position-based unload over-tension guard: the buffer rests at
    # the tension rail through any normal retract, indistinguishable from a jam by
    # position. A normal UL must just retract until OUT clears -> UNLOADED, with no
    # UNLOAD_BLOCKED. (A genuine stuck unload falls through to UNLOAD_TIMEOUT at the
    # UNLOAD_MAX limit — not exercised here, it grinds to the limit.)
    board.prompt("Load filament past the OUT sensor (mid-tube is fine).")
    resp = board.send("UL")
    assert resp and "ER" not in resp, f"UL rejected: {resp!r} (is OUT sensor active?)"
    board.refute("UNLOAD_BLOCKED", window=4.0)              # type-P never position-blocks
    board.expect("UNLOADED", timeout=40.0, progress=True)


@case("unload", "ul_blocked_d", "D: buffer TENSION sustained during unload -> UNLOAD_BLOCKED",
      buf_types=("d",))
def _ul_blocked_d(board):
    board.prompt("Load filament past OUT. Hold the buffer at the TENSION switch during unload.")
    resp = board.send("UL")
    assert resp and "ER" not in resp, f"UL rejected: {resp!r}"
    board.expect("UNLOAD_BLOCKED", timeout=8.0, progress=True)


# ---------------------------------------------------------------------------
# SYNC  (control reactions + auto-toggle D18)
# ---------------------------------------------------------------------------

@case("sync", "sync_relief_pause_p", "P: compression pin during sync -> SYNC:RELIEF_PAUSE")
def _sync_relief(board):
    board.send("SM:1")
    board.await_buffer("With filament LOADED, push and HOLD the buffer to full COMPRESSION.", hi=-0.9)
    board.expect("SYNC:RELIEF_PAUSE", timeout=6.0, progress=True)


@case("sync", "sync_fault_hold_tension_p", "P: tension pin during sync -> SYNC:FAULT_HOLD")
def _sync_fault(board):
    board.send("SM:1")
    board.await_buffer("With filament LOADED, pull and HOLD the buffer to full TENSION "
                       "(starve it — pull the filament taut). Unloaded resting at home does "
                       "not exercise this.", lo=0.9)
    board.expect("SYNC:FAULT_HOLD", timeout=6.0, progress=True)


@case("sync", "sync_auto_start_p", "P: AUTO_MODE + tension transition -> SYNC:AUTO_START")
def _sync_auto_start(board):
    board.send("SET:AUTO_MODE:1")
    # D18 needs a real transition INTO tension, and type-P rests at the tension
    # rail, so stage it in two steps (load off the rail toward compression, then
    # cross up to tension). A single lo=0.6 target is trivially true at home and
    # provides no transition -> the gate would suppress AUTO_START.
    board.await_buffer("With filament LOADED, set the buffer to COMPRESSION (off the home rail).",
                       hi=-0.5)
    board.await_buffer("Now push the buffer UP to TENSION (a real transition into tension).", lo=0.7)
    board.expect("SYNC:AUTO_START", timeout=6.0, progress=True)


@case("sync", "sync_auto_start_gated_p", "P: AUTO_MODE + resting at home -> no spurious AUTO_START (D18)")
def _sync_auto_gate(board):
    board.send("SET:AUTO_MODE:1")
    board.prompt("Leave the buffer RESTING at the TENSION/home rail (+1.0) without any "
                 "fresh transition (simulate boot/home).")
    board.refute("SYNC:AUTO_START", window=4.0)


@case("sync", "sync_auto_stop_p", "P: tail-assist auto-sync idle at compression -> SYNC:AUTO_STOP")
def _sync_auto_stop(board):
    # AUTO_STOP only fires for an auto-started, tail-assist sync (filament past
    # OUT but absent at IN) once the buffer holds COMPRESSION for
    # SYNC_AUTO_STOP_MS (5 s) with no further demand.
    board.send("SET:AUTO_MODE:1")
    board.await_buffer("Tail-assist setup: filament PRESENT at OUT but NOT at the IN/gate "
                       "sensor. Set the buffer to COMPRESSION (off the home rail).", hi=-0.5)
    board.await_buffer("Now push the buffer UP to TENSION to auto-start (real transition).", lo=0.7)
    board.expect("SYNC:AUTO_START", timeout=6.0, progress=True)
    board.await_buffer("Now push and HOLD the buffer at COMPRESSION (tail consumed).", hi=-0.9)
    board.expect("SYNC:AUTO_STOP", timeout=8.0, progress=True)   # > SYNC_AUTO_STOP_MS (5 s)


# ---------------------------------------------------------------------------
# STAB  (BUF_STAB — D23 Gate A for type-P)
# ---------------------------------------------------------------------------

@case("stab", "stab_loaded_to_goal_p", "P: loaded + off-goal -> BUF_STAB normalizes to goal")
def _stab_loaded(board):
    board.await_buffer("Ensure filament is present (OUT/IN active), then set the buffer OFF "
                       "goal toward COMPRESSION.", hi=-0.55)
    resp = board.send("BS")
    assert resp and "ER" not in resp, f"BS rejected: {resp!r}"
    board.expect("BUF_STAB:START", timeout=2.0)
    board.expect("BUF_STAB:DONE", timeout=12.0, progress=True)


@case("stab", "stab_unloaded_noop_p", "P: unloaded at home -> BUF_STAB no-op (no START) (D23)")
def _stab_unloaded(board):
    board.prompt("REMOVE filament from all sensors (no IN/OUT). Buffer rests at TENSION/home.")
    board.send("BS")
    board.refute("BUF_STAB:START", window=3.0)     # presence gate -> no dry-spin


@case("stab", "stab_to_neutral_d", "D: pinned buffer -> BUF_STAB drives to NEUTRAL -> DONE",
      buf_types=("d",))
def _stab_d(board):
    board.prompt("Push the buffer to a switch extreme (COMPRESSION or TENSION).")
    resp = board.send("BS")
    assert resp and "ER" not in resp, f"BS rejected: {resp!r}"
    board.expect("BUF_STAB:START", timeout=2.0)
    board.expect("BUF_STAB:DONE", timeout=12.0, progress=True)


# ---------------------------------------------------------------------------
# BUF LOCK  (BL: — D19 prime/lock/break, D20 follow)
# ---------------------------------------------------------------------------

@case("buflock", "bl_prime_lock_p", "P: BL:T primes to tension extreme -> BL:LOCKED (D19)")
def _bl_lock(board):
    resp = board.send("BL:T")
    assert resp and "ER" not in resp, f"BL rejected: {resp!r}"
    board.expect("BL:PRIME", timeout=3.0)
    board.await_buffer("Let the buffer arm reach the TENSION extreme.", lo=0.9)
    board.expect("BL:LOCKED", timeout=8.0, progress=True)


@case("buflock", "bl_break_follow_p", "P: locked, arm pulled off extreme -> BL:FOLLOW -> FOLLOW_DONE (D20)")
def _bl_follow(board):
    resp = board.send("BL:T:20:300")               # arm tension + follow 20mm @ 300 mm/min
    assert resp and "ER" not in resp, f"BL rejected: {resp!r}"
    board.expect("BL:PRIME", timeout=3.0)
    board.await_buffer("Let the arm reach the TENSION extreme and lock.", lo=0.9)
    board.expect("BL:LOCKED", timeout=8.0, progress=True)
    board.await_buffer("With filament LOADED, pull the arm OFF the tension extreme (<0.90 breaks "
                       "the lock) to simulate the extruder filling the buffer.", hi=0.85)
    board.expect("BL:FOLLOW", timeout=6.0, progress=True)
    board.expect("BL:FOLLOW_DONE", timeout=10.0, progress=True)


@case("buflock", "bl_timeout_p", "P: BL with no arm motion -> BL:TIMEOUT")
def _bl_timeout(board):
    resp = board.send("BL:C")
    assert resp and "ER" not in resp, f"BL rejected: {resp!r}"
    board.expect("BL:PRIME", timeout=3.0)
    board.prompt("Do NOT move the buffer; wait for the lock-acquire timeout.")
    board.expect("BL:TIMEOUT", timeout=15.0, progress=True)


# ---------------------------------------------------------------------------
# LOAD  (FL flow-load — buffer-relevant; preload/autoload excluded)
# ---------------------------------------------------------------------------

@case("load", "load_fl_p", "P: FL with filament at gate -> LOADED")
def _load_fl(board):
    board.prompt("Stage filament at the lane IN sensor (gate) with the path clear to load.")
    resp = board.send("FL")
    assert resp and "ER" not in resp, f"FL rejected: {resp!r} (is IN sensor active?)"
    board.expect("LOADED", timeout=40.0, progress=True)   # bowden-length dependent


@case("load", "load_fl_no_filament", "FL with no filament at gate -> ER:NO_FILAMENT (guard)",
      buf_types=("p", "d"))
def _load_fl_guard(board):
    board.prompt("Ensure NO filament at the lane IN sensor.")
    resp = board.send("FL")
    assert resp and "NO_FILAMENT" in resp, f"expected ER:NO_FILAMENT, got {resp!r}"


# ---------------------------------------------------------------------------
# runner
# ---------------------------------------------------------------------------

def select_cases(flow, buf_type):
    for c in _REGISTRY:
        if flow != "all" and c.flow != flow:
            continue
        if buf_type not in c.buf_types:
            continue
        yield c


def run_one(board, c):
    """Run a case with operator retry/skip on failure. Returns (status, detail)."""
    while True:
        print(f"\n=== [{c.flow}] {c.id} ===\n    {c.title}")
        try:
            _reset(board)
            c.run(board)
            print("    PASS")
            return "pass", ""
        except KeyboardInterrupt:
            board.stop_motion()
            print("    ABORT (interrupted)")
            return "abort", "interrupted"
        except (AssertionError, HilError) as e:
            print(f"    FAIL: {e}")
            board.stop_motion()
            ans = board.ask("    [r]etry / [s]kip / ENTER=record fail: ")
            if ans == "r":
                continue
            if ans == "s":
                return "skip", str(e)
            return "fail", str(e)


def run(board, cases):
    results = []
    for c in cases:
        status, detail = run_one(board, c)
        results.append((c, status, detail))
        if status == "abort":
            break
    return results


def main(argv=None):
    ap = argparse.ArgumentParser(description="FLARE operator-assisted buffer-flow HIL tests")
    ap.add_argument("--flow", choices=("all",) + FLOWS, default="all")
    ap.add_argument("--type", choices=("p", "d"), default="p",
                    help="buffer sensor type to exercise (default: p)")
    ap.add_argument("--daemon-url", default="http://127.0.0.1:8088")
    ap.add_argument("--list", action="store_true", help="list matching cases and exit")
    ap.add_argument("--quiet", action="store_true", help="suppress raw event echo")
    args = ap.parse_args(argv)

    cases = list(select_cases(args.flow, args.type))
    if not cases:
        print(f"no cases for flow={args.flow} type={args.type}")
        return 1
    if args.list:
        for c in cases:
            print(f"  [{c.flow:7}] {c.id:26} {c.title}")
        return 0

    board = HilBoard(daemon_url=args.daemon_url, verbose=not args.quiet)
    try:
        board.connect()
    except HilError as e:
        print(f"connect failed: {e}")
        return 2

    print(f"Buffer-flow HIL: flow={args.flow} type={args.type} "
          f"({len(cases)} cases). Setting sensor type + safe speeds.")
    board.set_sensor_type(args.type)
    board.safe_speeds()

    try:
        results = run(board, cases)
    finally:
        board.send("SM:0")
        board.close()

    passed = sum(1 for _, s, _ in results if s == "pass")
    skipped = [(c, why) for c, s, why in results if s == "skip"]
    failed = [(c, why) for c, s, why in results if s == "fail"]
    print(f"\n==== {passed}/{len(results)} passed"
          + (f", {len(skipped)} skipped" if skipped else "")
          + (f", {len(failed)} failed" if failed else "") + " ====")
    for c, why in skipped:
        print(f"  SKIP [{c.flow}] {c.id}")
    for c, why in failed:
        print(f"  FAIL [{c.flow}] {c.id}: {why}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
