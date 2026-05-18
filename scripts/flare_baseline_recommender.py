#!/usr/bin/env python3
"""flare_baseline_recommender.py - observe-only baseline recommender for FLARE.

Reads TTY status lines or replays from a file, tracks live-tuner drift signals,
and suggests a persistent baseline_sps at end-of-print. Pure stdlib only.
"""

import argparse
import os
import re
import sys
import time

from path_utils import resolve_input, PathError

try:
    import serial
except ImportError:
    serial = None

STATUS_FIELD_RE = re.compile(r"(?P<key>[A-Z0-9]+):(?P<val>-?\d+(?:\.\d+)?|[A-Z_]+|[^,]*)")
BIAS_SAFE_MIN = 0.05
BIAS_SAFE_MAX = 0.65

def parse_status(line):
    if line.startswith("OK:"):
        line = line[3:]
    return dict(STATUS_FIELD_RE.findall(line))

class Recommender:
    def __init__(self):
        self.reset()

    def reset(self):
        self.total_est_n = 0
        self.total_est_sum = 0.0
        self.total_bias_n = 0
        self.total_bias_sum = 0.0
        self.start_ts = time.time()

    def process_line(self, line):
        if "NT:START" in line or "FLARE_TUNE:START" in line:
            if self.total_est_n > 0:
                self.report()
            self.reset()
            return

        status = parse_status(line)
        if "EST" in status and "BUF" in status and status["BUF"] == "MID":
            try:
                est = float(status["EST"])
                if est <= 0: return
                self.total_est_sum += est
                self.total_est_n += 1
                
                if "BP" in status and "RT" in status:
                    bp = float(status["BP"])
                    rt = float(status["RT"])
                    self.total_bias_sum += (0.4 + (bp - rt) / 7.8)
                    self.total_bias_n += 1
            except ValueError:
                pass

    def report(self):
        if self.total_est_n > 0:
            baseline = self.total_est_sum / self.total_est_n
            bias = self.total_bias_sum / self.total_bias_n if self.total_bias_n > 0 else 0.4
            bias = max(BIAS_SAFE_MIN, min(BIAS_SAFE_MAX, bias))
            
            print("\n--- Recommendation ---")
            print(f"Suggested baseline_sps: {int(round(baseline))}")
            print(f"Suggested sync_trailing_bias_frac: {bias:.3f}")
            print(f"Samples collected: {self.total_est_n}")
            print(f"Duration: {time.time() - self.start_ts:.1f}s")
            print("----------------------\n")
        else:
            print("\nNo MID-zone data collected in this print segment.")

def main():
    ap = argparse.ArgumentParser(description="FLARE Baseline Recommender")
    ap.add_argument("--port", help="Serial port (e.g. /dev/ttyACM0)")
    ap.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    ap.add_argument("--file", help="Replay from a recorded stream file")
    args = ap.parse_args()

    try:
        args.file = resolve_input(args.file)
    except PathError as exc:
        print(f"Error: {exc.path}: {exc.reason}", file=sys.stderr)
        sys.exit(2)

    rec = Recommender()
    
    if args.file:
        try:
            with open(args.file, "r", errors="ignore") as fh:
                for line in fh:
                    rec.process_line(line.strip())
            rec.report()
        except KeyboardInterrupt:
            rec.report()
            sys.exit(0)
    elif args.port:
        if not serial:
            print("Error: pyserial not installed. Run 'pip install pyserial'.", file=sys.stderr)
            sys.exit(1)
        try:
            ser = serial.Serial(args.port, args.baud, timeout=1.0)
            print(f"[*] Listening on {args.port}...")
            while True:
                line = ser.readline()
                if not line: continue
                try:
                    line_str = line.decode("utf-8", errors="ignore").strip()
                    if line_str:
                        rec.process_line(line_str)
                except Exception:
                    pass
        except serial.SerialException as e:
            print(f"Error: {e}", file=sys.stderr)
            sys.exit(1)
        except KeyboardInterrupt:
            rec.report()
            sys.exit(0)
    else:
        ap.print_help()

if __name__ == "__main__":
    main()
