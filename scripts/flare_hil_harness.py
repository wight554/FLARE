#!/usr/bin/env python3
"""FLARE operator-assisted HIL (hardware-in-the-loop) test harness.

Talks to a *running* ``flare_daemon`` over HTTP — the daemon owns the serial
port and is the single source of telemetry, so this harness never opens the
port itself (no conflict, no second reader):

  * commands  -> ``POST /cmd``        (returns the board's OK:/ER: reply)
  * events    -> ``GET /telemetry``   (SSE stream of {event_type,event_data})
  * status    -> ``GET /status``      (snapshot incl. g_buf_pos / buf_state)

This is hardware-in-the-loop, not CI: the flow tests in ``flare_hil.py`` need a
powered board, a running daemon, and an operator. The pure event-parsing /
matching logic is factored behind ``event_from_sse`` / ``parse_event`` /
``HilBoard._ingest`` (plus the ``_in_range`` / ``_target_str`` helpers) so it is
unit-tested with no hardware in ``test_flare_hil_harness.py``.

UX:
  * ``await_buffer`` polls ``/status`` and shows the live ``g_buf_pos`` while the
    operator stages the buffer, auto-proceeding when it reaches the target
    window (ENTER forces). No more guessing where the arm is.
  * raw ``< EV`` echo is muted while a prompt / staged wait / progress countdown
    is on screen, so those lines stay clean (events are still captured).
  * ``expect(..., progress=True)`` shows a one-line countdown on long waits.

Event matching note: the firmware emits ``EV:<type>:<data>`` (colon), and the
daemon only comma-splits, so the daemon's ``event_type`` carries the whole
colon-delimited token (e.g. ``SYNC:RELIEF_PAUSE``, ``BUF_STAB:DONE``,
``BL:LOCKED``). Matching is a substring/regex test against the reconstructed
payload, robust for every buffer event.
"""

import collections
import json
import re
import select
import sys
import threading
import time
import urllib.request

DEFAULT_DAEMON_URL = "http://127.0.0.1:8088"

# payload = matchable event string; data = raw event_data; t = monotonic ts
Event = collections.namedtuple("Event", "payload data t")


class HilError(RuntimeError):
    pass


def parse_event(line):
    """Payload (text after ``EV:``) for a raw firmware event line, else None.

    >>> parse_event("EV:UNLOAD_BLOCKED")
    'UNLOAD_BLOCKED'
    >>> parse_event("EV:SYNC:RELIEF_PAUSE")
    'SYNC:RELIEF_PAUSE'
    >>> parse_event("OK:LN:2") is None
    True
    """
    line = (line or "").strip()
    if not line.startswith("EV:"):
        return None
    return line[len("EV:"):]


def event_from_sse(data):
    """Reconstruct the matchable payload from a daemon SSE message dict.

    >>> event_from_sse({"event_type": "UNLOAD_BLOCKED", "event_data": ""})
    'UNLOAD_BLOCKED'
    >>> event_from_sse({"event_type": "SYNC", "event_data": "RELIEF_PAUSE"})
    'SYNC:RELIEF_PAUSE'
    >>> event_from_sse({"buf": "NEUTRAL", "bp": 0.3}) is None
    True
    """
    if not isinstance(data, dict):
        return None
    etype = data.get("event_type")
    if etype is None:
        return None
    edata = data.get("event_data") or ""
    return f"{etype}:{edata}" if edata else etype


class HilBoard:
    """Daemon-backed connection with async event capture + assertions."""

    def __init__(self, daemon_url=DEFAULT_DAEMON_URL, verbose=True,
                 prompt_fn=input, maxlen=4000):
        self.daemon_url = daemon_url.rstrip("/")
        self.verbose = verbose
        self._prompt_fn = prompt_fn
        self._events = collections.deque(maxlen=maxlen)
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._reader = None
        self._sse = None
        self._mute_echo = False     # silence < EV echo while a prompt/wait is on screen

    # -- lifecycle ------------------------------------------------------------

    def connect(self):
        try:
            self.status(timeout=2.0)
        except Exception as e:
            raise HilError(
                f"cannot reach flare_daemon at {self.daemon_url} ({e}); "
                "start it first (scripts/flare_daemon.py)")
        self._stop.clear()
        self._reader = threading.Thread(target=self._sse_loop, daemon=True)
        self._reader.start()
        return self

    def close(self):
        self._stop.set()
        try:
            self.stop_motion()
        except Exception:
            pass
        if self._sse is not None:
            try:
                self._sse.close()
            except Exception:
                pass
        if self._reader:
            self._reader.join(timeout=1.0)

    def __enter__(self):
        return self.connect()

    def __exit__(self, *exc):
        self.close()
        return False

    # -- SSE reader -----------------------------------------------------------

    def _sse_loop(self):  # pragma: no cover - needs a live daemon
        try:
            req = urllib.request.Request(f"{self.daemon_url}/telemetry")
            self._sse = urllib.request.urlopen(req, timeout=10.0)
            while not self._stop.is_set():
                line = self._sse.readline()
                if not line:
                    break
                text = line.decode("utf-8", errors="ignore").strip()
                if text.startswith("data:"):
                    self._ingest_raw(text[len("data:"):].strip())
        except Exception:
            pass

    def _ingest_raw(self, data_json):
        try:
            data = json.loads(data_json)
        except Exception:
            return None
        return self._ingest(data)

    def _ingest(self, data):
        """Record an event from a parsed SSE dict. Pure (no I/O); unit-tested."""
        payload = event_from_sse(data)
        if payload is None:
            return None
        if self.verbose and not self._mute_echo:
            print(f"   < EV {payload}")
        ev = Event(payload, data.get("event_data") or "", time.monotonic())
        with self._lock:
            self._events.append(ev)
        return ev

    # -- commands -------------------------------------------------------------

    def send(self, cmd, timeout=10.0):
        if self.verbose and not self._mute_echo:
            print(f"   > {cmd}")
        body = json.dumps({"cmd": cmd}).encode("utf-8")
        req = urllib.request.Request(
            f"{self.daemon_url}/cmd", data=body,
            headers={"Content-Type": "application/json"}, method="POST")
        try:
            with urllib.request.urlopen(req, timeout=timeout + 2.0) as resp:
                payload = json.loads(resp.read().decode("utf-8"))
                return payload.get("response")
        except Exception as e:
            raise HilError(f"cmd '{cmd}' failed: {e}")

    def stop_motion(self):
        try:
            self.send("ST:", timeout=2.0)
        except Exception:
            pass

    def status(self, timeout=2.0):
        req = urllib.request.Request(f"{self.daemon_url}/status")
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def buf_pos(self):
        """(g_buf_pos, buf_state) from /status, or (None, None) on error."""
        try:
            s = self.status(timeout=1.0)
            return s.get("g_buf_pos"), s.get("buf_state")
        except Exception:
            return None, None

    # -- event assertions -----------------------------------------------------

    @staticmethod
    def _matches(ev, needle, regex):
        if regex:
            return re.search(needle, ev.payload) is not None
        return needle in ev.payload

    def clear_events(self):
        with self._lock:
            self._events.clear()

    def events(self):
        with self._lock:
            return list(self._events)

    def wait_event(self, needle, timeout=5.0, regex=False, since=0.0, poll=0.02,
                   progress=False):
        """Block until an event payload matches ``needle``; return it or None.

        With ``progress`` (and verbose) a one-line countdown is shown and the
        ``< EV`` echo is muted so the line stays clean; the matched event is
        printed on success.
        """
        deadline = time.monotonic() + timeout
        show = progress and self.verbose
        prev_mute = self._mute_echo
        if show:
            self._mute_echo = True
        last = 0.0
        try:
            while True:
                with self._lock:
                    for ev in self._events:
                        if ev.t >= since and self._matches(ev, needle, regex):
                            if show:
                                self._clear_line()
                                print(f"   < EV {ev.payload}")
                            return ev
                now = time.monotonic()
                if show and now - last >= 0.5:
                    sys.stdout.write(f"\r   ... waiting '{needle}'  {max(0.0, deadline - now):4.0f}s ")
                    sys.stdout.flush()
                    last = now
                if now >= deadline:
                    if show:
                        self._clear_line()
                    return None
                time.sleep(poll)
        finally:
            if show:
                self._mute_echo = prev_mute

    def refute_event(self, needle, window=2.0, regex=False, poll=0.02):
        start = time.monotonic()
        deadline = start + window
        while time.monotonic() < deadline:
            with self._lock:
                for ev in self._events:
                    if ev.t >= start and self._matches(ev, needle, regex):
                        return ev
            time.sleep(poll)
        return None

    def expect(self, needle, timeout=5.0, regex=False, since=0.0, progress=False):
        ev = self.wait_event(needle, timeout=timeout, regex=regex, since=since,
                             progress=progress)
        if ev is None:
            raise AssertionError(f"expected event '{needle}' within {timeout}s, none seen")
        return ev

    def refute(self, needle, window=2.0, regex=False):
        ev = self.refute_event(needle, window=window, regex=regex)
        if ev is not None:
            raise AssertionError(f"unexpected event '{needle}' (payload: {ev.payload})")

    # -- operator interaction -------------------------------------------------

    @staticmethod
    def _clear_line():
        sys.stdout.write("\r" + " " * 64 + "\r")
        sys.stdout.flush()

    @staticmethod
    def _in_range(pos, lo, hi):
        """True if pos is within the (optional) [lo, hi] window.

        >>> HilBoard._in_range(0.95, 0.9, None)
        True
        >>> HilBoard._in_range(-0.95, None, -0.9)
        True
        >>> HilBoard._in_range(0.3, 0.9, None)
        False
        """
        return (lo is None or pos >= lo) and (hi is None or pos <= hi)

    @staticmethod
    def _target_str(lo, hi):
        """Human-readable target window.

        >>> HilBoard._target_str(0.9, None)
        '(want >= +0.90)'
        >>> HilBoard._target_str(None, -0.9)
        '(want <= -0.90)'
        """
        if lo is not None and hi is not None:
            return f"(want {lo:+.2f}..{hi:+.2f})"
        if lo is not None:
            return f"(want >= {lo:+.2f})"
        if hi is not None:
            return f"(want <= {hi:+.2f})"
        return ""

    def _enter_pressed(self):
        try:
            if sys.stdin is None or not sys.stdin.isatty():
                return False
            r, _, _ = select.select([sys.stdin], [], [], 0)
            if r:
                sys.stdin.readline()
                return True
        except Exception:
            pass
        return False

    def prompt(self, msg):
        """Operator acknowledgement (no buffer target). Mutes echo while open."""
        self._mute_echo = True
        try:
            self._prompt_fn(f"\n   [OPERATOR] {msg}\n   ...press ENTER when ready ")
        finally:
            self._mute_echo = False

    def ask(self, msg):
        """Prompt for a short string answer (echo muted)."""
        self._mute_echo = True
        try:
            return (self._prompt_fn(msg) or "").strip().lower()
        finally:
            self._mute_echo = False

    def await_buffer(self, msg, lo=None, hi=None, timeout=120.0, poll=0.25):
        """Prompt, then poll /status with a live ``g_buf_pos`` readout, returning
        when the buffer enters the [lo, hi] window (auto-detect), the operator
        presses ENTER (force), or ``timeout`` elapses. Echo muted while waiting.
        """
        target = self._target_str(lo, hi)
        print(f"\n   [OPERATOR] {msg}")
        print(f"   staging buffer {target}  —  live position below ([ENTER] to force):")
        self._mute_echo = True
        deadline = time.monotonic() + timeout
        try:
            while True:
                pos, zone = self.buf_pos()
                shown = f"{pos:+.2f}" if isinstance(pos, (int, float)) else "  ?  "
                sys.stdout.write(f"\r   buffer {shown}  {str(zone or '').ljust(11)} {target}  ")
                sys.stdout.flush()
                if isinstance(pos, (int, float)) and self._in_range(pos, lo, hi):
                    self._clear_line()
                    print(f"   buffer {pos:+.2f} {zone}  -> staged")
                    return True
                if self._enter_pressed():
                    self._clear_line()
                    print("   (forced by operator)")
                    return False
                if time.monotonic() >= deadline:
                    self._clear_line()
                    print("   (await timeout; proceeding)")
                    return False
                time.sleep(poll)
        finally:
            self._mute_echo = False

    # -- setup helpers --------------------------------------------------------

    def set_sensor_type(self, buf_type):
        """'p' -> analog (BUF_SENSOR:1); 'd' -> digital (BUF_SENSOR:0)."""
        self.send("SET:BUF_SENSOR:1" if buf_type == "p" else "SET:BUF_SENSOR:0")

    def safe_speeds(self):
        """Reduced first-motion speeds (see TEST_CASES.md)."""
        for c in ("SET:FEED_RATE:600", "SET:REV_RATE:600", "SET:AUTO_RATE:400",
                  "SET:JOIN_RATE:400", "SET:PRESS_RATE:300"):
            self.send(c)
