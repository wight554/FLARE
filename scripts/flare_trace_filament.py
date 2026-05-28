#!/usr/bin/env python3
"""
flare_trace_filament.py — triangulate FLARE filament-position telemetry across
the three layers that feed the Fluidd/Mainsail MMU widget, to diagnose the
standalone-load (#1) and drawn-tip (#4) tracking issues.

WHY THREE LAYERS
    daemon  /status              -> firmware ground truth (tc_state, sensors,
                                    feed_rate_mms). What the board really did.
    moonraker HTTP query  (poll) -> klippy get_status() recomputed ON DEMAND.
                                    Each poll re-runs the synthetic
                                    filament_position math, so this is smooth by
                                    construction -- it shows what the value COULD
                                    be if sampled often.
    moonraker WEBSOCKET (push)   -> exactly what Fluidd receives via
                                    printer.objects.subscribe. This is the
                                    cadence that the widget animates from. If the
                                    gcode parser is locked in a synchronous wait
                                    loop (FLARE_WAIT_TC / FLARE_WAIT_UNLOAD), the
                                    push can be starved even though HTTP polls
                                    stay smooth.

    Comparing the HTTP-poll timeline against the WEBSOCKET timeline is the
    decisive experiment for #1:
      poll many values, ws few   -> push starvation during the gcode lock
                                    (the widget only sees the jumps).
      poll few,        ws few    -> klippy isn't producing intermediate values
                                    (phase not set / load_phase_start anchor).
      both many                  -> telemetry streams fine; look elsewhere.

    For #4 the run reports every distinct filament_pos enum value seen. If only
    {0, 4, 10} appear, the drawn tip can occupy at most three spots regardless
    of the smooth filament_position mm -- confirming a discrete binding.

USAGE
    python3 scripts/flare_trace_filament.py [options]

    Then, in another shell / the Fluidd console, drive ONE operation at a time:
        MMU_LOAD GATE=0          # standalone load   (#1)
        MMU_UNLOAD               # standalone unload (#2 regression check)
        # a cut-enabled unload / toolchange           (#3 regression check)
    Watch the live console; stop with the duration timeout or Ctrl-C. A full
    JSONL trace is written to --out and an analysis summary prints at the end.

OPTIONS
    --duration S        capture window in seconds (default 90; 0 = until Ctrl-C)
    --daemon-url URL    FLARE daemon base URL (default http://127.0.0.1:8088)
    --moonraker-host H  Moonraker host (default 127.0.0.1)
    --moonraker-port P  Moonraker port (default 7125)
    --object NAME       Moonraker object to watch (default mmu; repeatable)
    --daemon-hz N       daemon /status poll rate     (default 20)
    --poll-hz N         moonraker HTTP query rate     (default 10)
    --out PATH          JSONL output (default flare_trace_<ts>.jsonl)
    --no-daemon         skip the daemon /status layer
    --no-poll           skip the moonraker HTTP-query layer
    --no-ws             skip the moonraker websocket layer

Std-lib only (urllib, socket, threading) -- no extra install on a Pi host.
"""
import argparse
import base64
import json
import os
import socket
import struct
import sys
import threading
import time
import urllib.error
import urllib.request
from collections import OrderedDict


# --------------------------------------------------------------------------- #
# Thread-safe JSONL recorder + live console                                    #
# --------------------------------------------------------------------------- #
class Recorder:
    """Append timestamped records to a JSONL file and echo a compact live line
    whenever a watched field (tc_state / filament_pos / filament_position)
    changes, tagged by source layer."""

    def __init__(self, path, t0):
        self._path = path
        self._t0 = t0
        self._fh = open(path, "w", buffering=1)
        self._lock = threading.Lock()
        self._records = []          # kept in memory for the end-of-run analysis
        self._last_line = {}        # per-source last (tc_state, pos, fpos) for change echo

    def log(self, src, data):
        t = time.monotonic() - self._t0
        rec = OrderedDict()
        rec["t"] = round(t, 4)
        rec["wall"] = time.strftime("%H:%M:%S") + f".{int((time.time() % 1) * 1000):03d}"
        rec["src"] = src
        rec.update(data)
        with self._lock:
            self._fh.write(json.dumps(rec) + "\n")
            self._records.append(rec)
            self._echo(src, rec, t)

    def _echo(self, src, rec, t):
        key = (rec.get("tc_state"), rec.get("filament_pos"), rec.get("filament_position"))
        if self._last_line.get(src) == key:
            return
        self._last_line[src] = key
        bits = [f"[{t:7.2f}s] {src:9s}"]
        if rec.get("tc_state") is not None:
            bits.append(f"tc={rec['tc_state']}")
        if rec.get("action") is not None:
            bits.append(f"act={rec['action']}")
        if rec.get("filament_pos") is not None:
            bits.append(f"pos={rec['filament_pos']}")
        if rec.get("filament_position") is not None:
            bits.append(f"mm={rec['filament_position']}")
        if rec.get("feed_rate_mms") is not None:
            bits.append(f"feed={rec['feed_rate_mms']}")
        if rec.get("toolhead") is not None:
            bits.append(f"th={rec['toolhead']}")
        print("  ".join(bits))

    def close(self):
        with self._lock:
            self._fh.close()

    def records(self):
        with self._lock:
            return list(self._records)


# --------------------------------------------------------------------------- #
# Layer 1: FLARE daemon /status poller (firmware ground truth)                 #
# --------------------------------------------------------------------------- #
def daemon_poller(url, hz, rec, stop):
    endpoint = url.rstrip("/") + "/status"
    period = 1.0 / max(1.0, hz)
    keys = ("tc_state", "action", "feed_rate_mms", "rev_rate_mms", "sps",
            "active_lane", "lane1_task", "lane2_task", "toolhead",
            "in1", "out1", "in2", "out2", "y_split", "board_online")
    miss = 0
    while not stop.is_set():
        try:
            with urllib.request.urlopen(endpoint, timeout=0.5) as resp:
                state = json.loads(resp.read().decode("utf-8"))
            data = {k: state.get(k) for k in keys if k in state}
            # Keep every key we did not explicitly enumerate, too (forward-proof).
            for k, v in state.items():
                if k not in data and not isinstance(v, (dict, list)):
                    data[k] = v
            rec.log("daemon", data)
            miss = 0
        except (urllib.error.URLError, OSError, ValueError):
            miss += 1
            if miss == 1:
                print("  (daemon /status unreachable — is flare_daemon.py running?)",
                      file=sys.stderr)
        stop.wait(period)


# --------------------------------------------------------------------------- #
# Layer 2: Moonraker HTTP object query (masquerade, recomputed on demand)      #
# --------------------------------------------------------------------------- #
def moonraker_poller(base, objects, hz, rec, stop):
    q = "&".join(f"{o}" for o in objects)
    endpoint = f"{base}/printer/objects/query?{q}"
    period = 1.0 / max(1.0, hz)
    miss = 0
    while not stop.is_set():
        try:
            with urllib.request.urlopen(endpoint, timeout=0.5) as resp:
                body = json.loads(resp.read().decode("utf-8"))
            status = body.get("result", {}).get("status", {})
            mmu = status.get("mmu", {})
            rec.log("mr_poll", _mmu_fields(mmu))
            miss = 0
        except (urllib.error.URLError, OSError, ValueError):
            miss += 1
            if miss == 1:
                print(f"  (moonraker HTTP query failed at {base} — running standalone?)",
                      file=sys.stderr)
        stop.wait(period)


def _mmu_fields(mmu):
    """Pull the fields the widget actually renders from an mmu status object."""
    out = {}
    for k in ("filament", "filament_pos", "filament_position", "action", "gate",
              "tool", "active_gate"):
        if k in mmu:
            out[k] = mmu[k]
    sensors = mmu.get("sensors")
    if isinstance(sensors, dict):
        # toolhead is the binary the fill animation keys off (design.md §14)
        for sk in ("toolhead", "filament_compression", "filament_tension"):
            if sk in sensors:
                out["sensor_" + sk] = sensors[sk]
    return out


# --------------------------------------------------------------------------- #
# Layer 3: minimal RFC6455 websocket client + Moonraker subscription (push)    #
# --------------------------------------------------------------------------- #
class WSConn:
    """Tiny client-side websocket: text send (masked) + frame recv, with a
    small read buffer to absorb bytes that trail the HTTP upgrade response."""

    def __init__(self, sock, initial=b""):
        self.sock = sock
        self.buf = initial

    def _read(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("websocket closed")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def send_text(self, text):
        payload = text.encode("utf-8")
        hdr = bytearray([0x81])           # FIN + text opcode
        n = len(payload)
        if n < 126:
            hdr.append(0x80 | n)
        elif n < 65536:
            hdr.append(0x80 | 126)
            hdr += struct.pack(">H", n)
        else:
            hdr.append(0x80 | 127)
            hdr += struct.pack(">Q", n)
        mask = os.urandom(4)
        hdr += mask
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(bytes(hdr) + masked)

    def recv_message(self):
        """Return (opcode, bytes) of one full (possibly fragmented) message."""
        frames = b""
        first_op = None
        while True:
            b0, b1 = self._read(2)
            fin = b0 & 0x80
            op = b0 & 0x0F
            length = b1 & 0x7F
            if length == 126:
                length = struct.unpack(">H", self._read(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", self._read(8))[0]
            mask = self._read(4) if (b1 & 0x80) else None
            payload = self._read(length) if length else b""
            if mask:
                payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
            if op != 0x0:
                first_op = op
            frames += payload
            if fin:
                return first_op, frames

    def send_pong(self, payload=b""):
        hdr = bytearray([0x8A])           # FIN + pong
        n = len(payload)
        hdr.append(0x80 | n)              # pong payload is always short
        mask = os.urandom(4)
        hdr += mask
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(bytes(hdr) + masked)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def ws_connect(host, port, path="/websocket", timeout=5.0):
    sock = socket.create_connection((host, port), timeout=timeout)
    key = base64.b64encode(os.urandom(16)).decode()
    req = (f"GET {path} HTTP/1.1\r\n"
           f"Host: {host}:{port}\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           f"Sec-WebSocket-Key: {key}\r\n"
           "Sec-WebSocket-Version: 13\r\n\r\n")
    sock.sendall(req.encode())
    resp = b""
    while b"\r\n\r\n" not in resp:
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("no websocket handshake response")
        resp += chunk
    head, _, tail = resp.partition(b"\r\n\r\n")
    if b" 101 " not in head.split(b"\r\n", 1)[0]:
        raise ConnectionError("upgrade rejected: " + head.split(b"\r\n", 1)[0].decode("latin1"))
    sock.settimeout(None)
    return WSConn(sock, initial=tail)


def moonraker_ws_subscriber(host, port, objects, rec, stop, holder):
    try:
        ws = ws_connect(host, port)
    except (OSError, ConnectionError) as e:
        print(f"  (moonraker websocket unavailable at {host}:{port}: {e})", file=sys.stderr)
        return
    holder["ws"] = ws
    sub = {
        "jsonrpc": "2.0",
        "method": "printer.objects.subscribe",
        "params": {"objects": {o: None for o in objects}},
        "id": 9001,
    }
    try:
        ws.send_text(json.dumps(sub))
        while not stop.is_set():
            op, payload = ws.recv_message()
            if op == 0x8:                          # close
                break
            if op == 0x9:                          # ping -> pong
                ws.send_pong(payload[:125])
                continue
            if op not in (0x1, 0x2):
                continue
            try:
                msg = json.loads(payload.decode("utf-8"))
            except ValueError:
                continue
            mmu = None
            if msg.get("method") == "notify_status_update":
                params = msg.get("params") or []
                if params and isinstance(params[0], dict):
                    mmu = params[0].get("mmu")
            elif "result" in msg and isinstance(msg["result"], dict):
                mmu = msg["result"].get("status", {}).get("mmu")
            if isinstance(mmu, dict):
                rec.log("mr_ws", _mmu_fields(mmu))
    except (OSError, ConnectionError):
        if not stop.is_set():
            print("  (moonraker websocket dropped)", file=sys.stderr)
    finally:
        ws.close()


# --------------------------------------------------------------------------- #
# Analysis                                                                     #
# --------------------------------------------------------------------------- #
def _distinct(records, src, field):
    vals = [r[field] for r in records if r["src"] == src and field in r and r[field] is not None]
    seen = []
    for v in vals:
        if v not in seen:
            seen.append(v)
    return vals, seen


def analyze(records, duration):
    print("\n" + "=" * 70)
    print("ANALYSIS")
    print("=" * 70)
    srcs = []
    for r in records:
        if r["src"] not in srcs:
            srcs.append(r["src"])

    pos_distinct = {}
    for src in srcs:
        n = sum(1 for r in records if r["src"] == src)
        print(f"\n[{src}]  {n} samples over {duration:.0f}s")
        for field in ("tc_state", "action", "filament_pos", "filament_position"):
            vals, seen = _distinct(records, src, field)
            if not vals:
                continue
            if field == "filament_position":
                pos_distinct[src] = len(seen)
                lo, hi = min(seen), max(seen)
                show = seen if len(seen) <= 12 else f"{len(seen)} values, range [{lo}, {hi}]"
                print(f"    filament_position : {len(seen):4d} distinct  {show}")
            else:
                print(f"    {field:18s}: {seen}")

    # ---- #1 verdict: HTTP poll (on-demand) vs WS push (what the widget sees) --
    print("\n" + "-" * 70)
    print("VERDICT — #1 standalone-load animation")
    poll = pos_distinct.get("mr_poll")
    ws = pos_distinct.get("mr_ws")
    if poll is None and ws is None:
        print("    No moonraker filament_position captured (standalone/no Klipper?).")
        print("    Re-run with Moonraker reachable to resolve #1.")
    elif ws is None:
        print(f"    HTTP-poll saw {poll} distinct mm values, but no websocket push")
        print("    captured. Re-run with --no-ws removed and Moonraker up to compare")
        print("    the push cadence (the cadence the widget actually animates from).")
    elif poll and poll >= 8 and ws <= 4:
        print(f"    HTTP-poll = {poll} distinct mm, WEBSOCKET push = {ws}.")
        print("    => PUSH STARVATION: klippy computes smooth values, but the")
        print("       subscription is frozen during the gcode-locked wait loop, so")
        print("       Fluidd only receives the endpoints. #1 is a SAMPLING problem,")
        print("       not an anchor problem. Fix at the push side (yield the lock /")
        print("       drive a background reactor timer), not load_phase_start.")
    elif poll and poll <= 4 and (ws or 0) <= 4:
        print(f"    HTTP-poll = {poll} distinct mm, WEBSOCKET push = {ws}.")
        print("    => klippy itself is not producing intermediate values. #1 is a")
        print("       PHASE/ANCHOR problem: current_phase not 'load' during the")
        print("       standalone load, or load_phase_start mis-anchored (37.4.1).")
    else:
        print(f"    HTTP-poll = {poll} distinct mm, WEBSOCKET push = {ws}.")
        print("    => filament_position streams with reasonable granularity on both")
        print("       layers. If the widget still looked stepped, the issue is the")
        print("       widget binding (see #4), not the mm telemetry.")

    # ---- #4 verdict: how many filament_pos enum stops were emitted ----------
    print("\nVERDICT — #4 drawn tip")
    src_for_pos = "mr_ws" if any(r["src"] == "mr_ws" for r in records) else "mr_poll"
    _, seen_pos = _distinct(records, src_for_pos, "filament_pos")
    if not seen_pos:
        print("    No filament_pos captured.")
    else:
        print(f"    filament_pos values seen ({src_for_pos}): {sorted(seen_pos)}")
        if set(seen_pos) <= {0, 4, 10}:
            print("    => only the {0,4,10} stops. If the Fluidd tip binds to")
            print("       filament_pos it can occupy at most 3 spots -> confirms the")
            print("       discrete binding. Cross-check the field the component reads")
            print("       (filament_pos vs filament_position) in the fluidd source.")
        else:
            print("    => finer filament_pos granularity present; the tip CAN move if")
            print("       the component binds to filament_pos.")
    print("=" * 70)


# --------------------------------------------------------------------------- #
# main                                                                         #
# --------------------------------------------------------------------------- #
def main():
    p = argparse.ArgumentParser(
        description="Triangulate FLARE filament-position telemetry (daemon / Moonraker HTTP / Moonraker websocket).")
    p.add_argument("--duration", type=float, default=90.0,
                   help="capture window in seconds (0 = until Ctrl-C; default 90)")
    p.add_argument("--daemon-url", default="http://127.0.0.1:8088")
    p.add_argument("--moonraker-host", default="127.0.0.1")
    p.add_argument("--moonraker-port", type=int, default=7125)
    p.add_argument("--object", action="append", dest="objects", default=None,
                   help="Moonraker object to watch (default: mmu; repeatable)")
    p.add_argument("--daemon-hz", type=float, default=20.0)
    p.add_argument("--poll-hz", type=float, default=10.0)
    p.add_argument("--out", default=None)
    p.add_argument("--no-daemon", action="store_true")
    p.add_argument("--no-poll", action="store_true")
    p.add_argument("--no-ws", action="store_true")
    args = p.parse_args()

    objects = args.objects or ["mmu"]
    out = args.out or f"flare_trace_{time.strftime('%Y%m%d_%H%M%S')}.jsonl"
    base = f"http://{args.moonraker_host}:{args.moonraker_port}"

    t0 = time.monotonic()
    rec = Recorder(out, t0)
    stop = threading.Event()
    holder = {}
    threads = []

    if not args.no_daemon:
        threads.append(threading.Thread(target=daemon_poller,
                                        args=(args.daemon_url, args.daemon_hz, rec, stop),
                                        daemon=True))
    if not args.no_poll:
        threads.append(threading.Thread(target=moonraker_poller,
                                        args=(base, objects, args.poll_hz, rec, stop),
                                        daemon=True))
    if not args.no_ws:
        threads.append(threading.Thread(target=moonraker_ws_subscriber,
                                        args=(args.moonraker_host, args.moonraker_port,
                                              objects, rec, stop, holder),
                                        daemon=True))

    print(f"flare_trace_filament: writing {out}")
    print(f"  layers: daemon={not args.no_daemon}  mr_poll={not args.no_poll}  mr_ws={not args.no_ws}")
    print(f"  watching objects: {objects}")
    print("  Now drive ONE operation (MMU_LOAD / MMU_UNLOAD / cut) and watch below.")
    print("  Live (echoes only on change):\n")

    for t in threads:
        t.start()

    try:
        if args.duration > 0:
            stop.wait(args.duration)
        else:
            while not stop.is_set():
                stop.wait(1.0)
    except KeyboardInterrupt:
        print("\n  (interrupted)")
    finally:
        stop.set()
        ws = holder.get("ws")
        if ws is not None:
            ws.close()                 # unblock the blocking recv in the ws thread
        for t in threads:
            t.join(timeout=1.5)
        rec.close()

    records = rec.records()
    elapsed = time.monotonic() - t0
    analyze(records, elapsed)
    print(f"\nFull trace: {out}  ({len(records)} records)")


if __name__ == "__main__":
    main()
