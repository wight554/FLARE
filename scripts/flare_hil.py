#!/usr/bin/env python3
"""FLARE operator-assisted HIL test runner for the buffer flows.

Drives a real board through ``flare_daemon`` (see ``flare_hil_harness``) and
asserts the emitted ``EV:`` events for each buffer flow:

    sync     PD control reactions + auto-sync toggle (D18)
    stab     idle BUF_STAB normalize-to-goal (type-P D23 Gate A) / to-neutral (D)
    buflock  BL: prime/lock/follow (D19/D20)
    load     FL flow-load completion + guards (buffer-relevant load only)
    unload   UL over-tension guard (type-P D22) + type-D parity

Each case prompts the operator to move the buffer / stage filament, sends the
driving command, and asserts (or refutes) the resulting events. Type-P is the
focus; a few type-D parity cases are included and only run with ``--type d`` on
type-D hardware.

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

# A test case. run(board) raises AssertionError/HilError on failure.
#   buf_types: sensor types the case applies to, e.g. ("p",) or ("p", "d").
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
# UNLOAD  (D22 — type-P unload over-tension guard)
# ---------------------------------------------------------------------------

@case("unload", "ul_tug_of_war_p", "P: pinned at tension during unload -> UNLOAD_BLOCKED")
def _ul_tug(board):
    board.prompt("Load filament past the OUT sensor. During unload, HOLD the buffer "
                 "arm pinned at the TENSION/home rail (+1.0) the whole time.")
    resp = board.send("UL")
    assert resp and "ER" not in resp, f"UL rejected: {resp!r} (is OUT sensor active?)"
    board.expect("UNLOAD_BLOCKED", timeout=8.0)   # > UNLOAD_TENSION_BLOCK_MS (5 s)


@case("unload", "ul_no_false_block_p", "P: healthy unload (off-rail) -> no UNLOAD_BLOCKED")
def _ul_healthy(board):
    board.prompt("Load filament past OUT. During unload let the MMU retract freely and "
                 "keep the buffer OFF the tension rail (deflected / relaxed below +0.9).")
    resp = board.send("UL")
    assert resp and "ER" not in resp, f"UL rejected: {resp!r}"
    board.refute("UNLOAD_BLOCKED", window=6.0)     # healthy must not block


@case("unload", "ul_blocked_d", "D: buffer TENSION sustained during unload -> UNLOAD_BLOCKED",
      buf_types=("d",))
def _ul_blocked_d(board):
    board.prompt("Load filament past OUT. Hold the buffer at the TENSION switch during unload.")
    resp = board.send("UL")
    assert resp and "ER" not in resp, f"UL rejected: {resp!r}"
    board.expect("UNLOAD_BLOCKED", timeout=8.0)


# ---------------------------------------------------------------------------
# SYNC  (control reactions + auto-toggle D18)
# ---------------------------------------------------------------------------

@case("sync", "sync_relief_pause_p", "P: compression pin during sync -> SYNC:RELIEF_PAUSE")
def _sync_relief(board):
    board.send("SM:1")
    board.prompt("Push and HOLD the buffer to full COMPRESSION (-1.0).")
    board.expect("SYNC:RELIEF_PAUSE", timeout=6.0)


@case("sync", "sync_fault_hold_tension_p", "P: tension pin during sync -> SYNC:FAULT_HOLD")
def _sync_fault(board):
    board.send("SM:1")
    board.prompt("Push and HOLD the buffer to full TENSION/home (+1.0).")
    board.expect("SYNC:FAULT_HOLD", timeout=6.0)


@case("sync", "sync_auto_start_p", "P: AUTO_MODE + tension transition -> SYNC:AUTO_START")
def _sync_auto_start(board):
    board.send("SET:AUTO_MODE:1")
    board.prompt("With filament loaded, set the buffer below +0.6, then push it UP "
                 "across +0.6 toward TENSION (a real transition, not resting at home).")
    board.expect("SYNC:AUTO_START", timeout=6.0)


@case("sync", "sync_auto_start_gated_p", "P: AUTO_MODE + resting at home -> no spurious AUTO_START (D18)")
def _sync_auto_gate(board):
    board.send("SET:AUTO_MODE:1")
    board.prompt("Leave the buffer RESTING at the TENSION/home rail (+1.0) without any "
                 "fresh transition (simulate boot/home).")
    board.refute("SYNC:AUTO_START", window=4.0)


# ---------------------------------------------------------------------------
# STAB  (BUF_STAB — D23 Gate A for type-P)
# ---------------------------------------------------------------------------

@case("stab", "stab_loaded_to_goal_p", "P: loaded + off-goal -> BUF_STAB normalizes to goal")
def _stab_loaded(board):
    board.prompt("Ensure filament is present (OUT/IN active). Set the buffer OFF goal "
                 "(toward COMPRESSION).")
    resp = board.send("BS")
    assert resp and "ER" not in resp, f"BS rejected: {resp!r}"
    board.expect("BUF_STAB:START", timeout=2.0)
    board.expect("BUF_STAB:DONE", timeout=12.0)


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
    board.expect("BUF_STAB:DONE", timeout=12.0)


# ---------------------------------------------------------------------------
# BUF LOCK  (BL: — D19 prime/lock/break, D20 follow)
# ---------------------------------------------------------------------------

@case("buflock", "bl_prime_lock_p", "P: BL:T primes to tension extreme -> BL:LOCKED (D19)")
def _bl_lock(board):
    resp = board.send("BL:T")
    assert resp and "ER" not in resp, f"BL rejected: {resp!r}"
    board.expect("BL:PRIME", timeout=3.0)
    board.prompt("Let the buffer arm reach the TENSION extreme (>=+0.9).")
    board.expect("BL:LOCKED", timeout=8.0)


@case("buflock", "bl_break_follow_p", "P: locked, arm pulled off extreme -> BL:FOLLOW -> FOLLOW_DONE (D20)")
def _bl_follow(board):
    resp = board.send("BL:T:20:300")               # arm tension + follow 20mm @ 300 mm/min
    assert resp and "ER" not in resp, f"BL rejected: {resp!r}"
    board.prompt("Let the arm reach the TENSION extreme and lock.")
    board.expect("BL:LOCKED", timeout=8.0)
    board.prompt("Now pull the arm OFF the tension extreme (<+0.9) to simulate the extruder filling.")
    board.expect("BL:FOLLOW", timeout=6.0)
    board.expect("BL:FOLLOW_DONE", timeout=10.0)


@case("buflock", "bl_timeout_p", "P: BL with no arm motion -> BL:TIMEOUT")
def _bl_timeout(board):
    resp = board.send("BL:C")
    assert resp and "ER" not in resp, f"BL rejected: {resp!r}"
    board.prompt("Do NOT move the buffer; wait for the lock-acquire timeout.")
    board.expect("BL:TIMEOUT", timeout=15.0)


# ---------------------------------------------------------------------------
# LOAD  (FL flow-load — buffer-relevant; preload/autoload excluded)
# ---------------------------------------------------------------------------

@case("load", "load_fl_p", "P: FL with filament at gate -> LOADED")
def _load_fl(board):
    board.prompt("Stage filament at the lane IN sensor (gate) with the path clear to load.")
    resp = board.send("FL")
    assert resp and "ER" not in resp, f"FL rejected: {resp!r} (is IN sensor active?)"
    board.expect("LOADED", timeout=40.0)            # bowden-length dependent


@case("load", "load_fl_no_filament", "FL with no filament at gate -> ER:NO_FILAMENT (guard)",
      buf_types=("p", "d"))
def _load_fl_guard(board):
    board.prompt("Ensure NO filament at the lane IN sensor.")
    resp = board.send("FL")
    assert resp and "NO_FILAMENT" in resp, f"expected ER:NO_FILAMENT, got {resp!r}"


# ---------------------------------------------------------------------------
# runner
# ---------------------------------------------------------------------------

def select(flow, buf_type):
    for c in _REGISTRY:
        if flow != "all" and c.flow != flow:
            continue
        if buf_type not in c.buf_types:
            continue
        yield c


def run(board, cases):
    results = []
    for c in cases:
        print(f"\n=== [{c.flow}] {c.id} ===\n    {c.title}")
        try:
            _reset(board)
            c.run(board)
            print(f"    PASS")
            results.append((c, True, ""))
        except (AssertionError, HilError) as e:
            print(f"    FAIL: {e}")
            results.append((c, False, str(e)))
        except KeyboardInterrupt:
            print("    SKIP (interrupted)")
            board.stop_motion()
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

    cases = list(select(args.flow, args.type))
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

    passed = sum(1 for _, ok, _ in results if ok)
    failed = [(c, why) for c, ok, why in results if not ok]
    print(f"\n==== {passed}/{len(results)} passed ====")
    for c, why in failed:
        print(f"  FAIL [{c.flow}] {c.id}: {why}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
