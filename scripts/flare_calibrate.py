#!/usr/bin/env python3
"""
flare_calibrate.py — deterministic sensor calibration wizard for FLARE Type-P buffers.

Measures and validates Type-P analog/Hall sensor ADC thresholds:
  1. Neutral point (buffer resting under no external load)
  2. Max compression (trolley compressed fully against spring)
  3. Max tension (trolley pulled fully toward tension stop)

Safety & Invariants:
  - Non-mutating by default (evidence collection only; satisfies REQ-calibration-workflow).
  - Explicit write flags required to mutate:
      --write-firmware: sends SET:BUF_PSF_* and SV: to device over serial/daemon.
      --write-config: updates config.ini with calibrated parameters.
  - Multi-sample averaging with standard deviation noise-rejection gate.
  - Span and monotonicity checks prevent inverted or truncated sensor configurations.

Usage:
  Interactive wizard:
    python3 scripts/flare_calibrate.py --interactive
  Automated with existing measurements:
    python3 scripts/flare_calibrate.py --neutral 0.505 --compression 0.880 --tension 0.120 --write-config
  Automated hardware sampling:
    python3 scripts/flare_calibrate.py --port /dev/ttyACM0 --write-firmware
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
import time
import urllib.error
import urllib.request
from typing import Callable, Dict, List, Optional, Tuple

try:
    import serial
except ImportError:
    serial = None

from path_utils import PathError, normalize_output
from serial_utils import find_port

DEFAULT_DAEMON_URL = "http://127.0.0.1:8088"
DEFAULT_SAMPLES = 20
DEFAULT_SAMPLE_DELAY = 0.025
DEFAULT_MAX_STDDEV = 0.025
DEFAULT_MIN_SPAN = 0.050


def calc_stats(samples: List[float]) -> Tuple[float, float]:
    """Return (mean, sample_stddev) for a list of float samples."""
    n = len(samples)
    if n == 0:
        return 0.0, 0.0
    mean = sum(samples) / n
    if n < 2:
        return mean, 0.0
    variance = sum((x - mean) ** 2 for x in samples) / (n - 1)
    return mean, math.sqrt(variance)


def validate_calibration(
    neutral: float,
    compression: float,
    tension: float,
    min_span: float = DEFAULT_MIN_SPAN,
) -> Tuple[bool, str, Dict[str, float]]:
    """
    Validate calibration thresholds for monotonicity, orientation, and minimum span.
    Returns (is_valid, message, metrics).
    """
    for name, val in [("neutral", neutral), ("compression", compression), ("tension", tension)]:
        if not (0.0 <= val <= 1.0):
            return False, f"Value out of ADC range [0.0, 1.0]: {name}={val:.4f}", {}

    reversed_orientation = compression < tension
    orientation_name = "reversed" if reversed_orientation else "normal"

    if reversed_orientation:
        # Reversed: compression < neutral < tension
        monotonic = compression < neutral < tension
    else:
        # Normal: tension < neutral < compression
        monotonic = tension < neutral < compression

    comp_span = abs(compression - neutral)
    tens_span = abs(tension - neutral)
    total_span = abs(compression - tension)

    metrics = {
        "neutral": neutral,
        "compression": compression,
        "tension": tension,
        "comp_span": comp_span,
        "tens_span": tens_span,
        "total_span": total_span,
        "reversed": 1.0 if reversed_orientation else 0.0,
    }

    if not monotonic:
        return (
            False,
            f"Monotonicity error: neutral ({neutral:.4f}) must lie strictly between "
            f"compression ({compression:.4f}) and tension ({tension:.4f}).",
            metrics,
        )

    if comp_span < min_span:
        return (
            False,
            f"Compression span ({comp_span:.4f}) is below minimum threshold ({min_span:.4f}).",
            metrics,
        )

    if tens_span < min_span:
        return (
            False,
            f"Tension span ({tens_span:.4f}) is below minimum threshold ({min_span:.4f}).",
            metrics,
        )

    msg = (
        f"Valid ({orientation_name} sensor orientation): "
        f"comp_span={comp_span:.4f}, tens_span={tens_span:.4f}, total_span={total_span:.4f}"
    )
    return True, msg, metrics


class HardwareClient:
    """Communicates with FLARE controller over local daemon HTTP API or direct serial."""

    def __init__(
        self,
        port: Optional[str] = None,
        baud: int = 115200,
        daemon_url: str = DEFAULT_DAEMON_URL,
    ) -> None:
        self.daemon_url = daemon_url.rstrip("/")
        self.port = port
        self.baud = baud
        self.ser = None
        self.use_daemon = False

    def connect(self) -> None:
        # First check if daemon is running and reachable
        try:
            req = urllib.request.Request(f"{self.daemon_url}/status")
            with urllib.request.urlopen(req, timeout=0.2) as resp:
                if resp.status == 200:
                    self.use_daemon = True
                    return
        except Exception:
            self.use_daemon = False

        # Fall back to direct serial
        if serial is None:
            raise RuntimeError("pyserial is not installed and FLARE daemon is not reachable.")

        target_port = self.port or find_port()
        if not target_port:
            raise RuntimeError("No serial port found. Specify --port or start flare_daemon.")

        self.ser = serial.Serial(target_port, self.baud, timeout=1.5)
        self.port = target_port

    def close(self) -> None:
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None

    def send_cmd(self, cmd_str: str, timeout: float = 3.0) -> Optional[str]:
        if self.use_daemon:
            try:
                data = json.dumps({"cmd": cmd_str}).encode("utf-8")
                req = urllib.request.Request(
                    f"{self.daemon_url}/cmd",
                    data=data,
                    headers={"Content-Type": "application/json"},
                    method="POST",
                )
                with urllib.request.urlopen(req, timeout=timeout + 1.0) as resp:
                    if resp.status == 200:
                        payload = json.loads(resp.read().decode("utf-8"))
                        return payload.get("response")
            except Exception:
                return None
            return None

        if self.ser is None:
            return None

        self.ser.reset_input_buffer()
        self.ser.write(f"{cmd_str}\n".encode())
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.ser.readline().decode("utf-8", errors="ignore").strip()
            if line.startswith("OK:") or line.startswith("ER:") or line == "OK":
                return line
        return None

    def read_raw_adc(self) -> Optional[float]:
        """Read live BUF_POS_RAW from controller."""
        resp = self.send_cmd("GET:BUF_POS_RAW")
        if resp and resp.startswith("OK:BUF_POS_RAW:"):
            try:
                return float(resp.split(":", 2)[2])
            except ValueError:
                pass

        # Fallback: check status line BPV
        resp = self.send_cmd("?:")
        if resp and resp.startswith("OK:"):
            tokens = resp[3:].split(",")
            for t in tokens:
                if t.startswith("BPV:"):
                    try:
                        # BPV is percent integer in status
                        return float(t.split(":", 1)[1]) / 100.0
                    except ValueError:
                        pass
        return None


def sample_position(
    reader_fn: Callable[[], Optional[float]],
    num_samples: int = DEFAULT_SAMPLES,
    delay_s: float = DEFAULT_SAMPLE_DELAY,
    max_stddev: float = DEFAULT_MAX_STDDEV,
) -> Tuple[bool, float, float, List[float]]:
    """
    Take num_samples readings using reader_fn.
    Returns (success, mean, stddev, samples).
    """
    samples: List[float] = []
    for _ in range(num_samples):
        val = reader_fn()
        if val is None:
            return False, 0.0, 0.0, []
        samples.append(val)
        if delay_s > 0:
            time.sleep(delay_s)

    mean, stddev = calc_stats(samples)
    if stddev > max_stddev:
        return False, mean, stddev, samples
    return True, mean, stddev, samples


def update_config_file(config_path: str, params: Dict[str, float]) -> None:
    """
    Update or insert buf_psf_max_comp, buf_psf_max_tens, and buf_psf_neutral in config_path.
    Preserves other settings and structure.
    """
    if not os.path.exists(config_path):
        raise PathError(config_path, "config file does not exist")

    with open(config_path, encoding="utf-8") as fh:
        lines = fh.readlines()

    keys_to_set = {
        "buf_psf_max_comp": f"{params['compression']:.3f}",
        "buf_psf_max_tens": f"{params['tension']:.3f}",
        "buf_psf_neutral": f"{params['neutral']:.3f}",
    }
    keys_found = set()
    new_lines = []

    for line in lines:
        stripped = line.strip()
        matched_key = None
        for k in keys_to_set:
            if stripped.startswith(f"{k}:") or stripped.startswith(f"# {k}:"):
                matched_key = k
                break

        if matched_key:
            keys_found.add(matched_key)
            new_lines.append(f"{matched_key}: {keys_to_set[matched_key]}\n")
        else:
            new_lines.append(line)

    # Append any keys that were not found in [sync] or at end
    missing = set(keys_to_set.keys()) - keys_found
    if missing:
        # Find [sync] section
        sync_idx = -1
        for idx, line in enumerate(new_lines):
            if line.strip() == "[sync]":
                sync_idx = idx
                break
        insert_idx = sync_idx + 1 if sync_idx >= 0 else len(new_lines)
        for k in sorted(missing):
            new_lines.insert(insert_idx, f"{k}: {keys_to_set[k]}\n")
            insert_idx += 1

    with open(config_path, "w", encoding="utf-8") as fh:
        fh.writelines(new_lines)


def run_wizard(
    client: HardwareClient,
    interactive: bool,
    num_samples: int,
    sample_delay: float,
    max_stddev: float,
    min_span: float,
) -> Optional[Dict[str, float]]:
    """Run interactive or step-based calibration wizard across 3 positions."""
    steps = [
        ("neutral", "Release buffer to resting NEUTRAL position (no filament pull/push)."),
        ("compression", "Push buffer trolley fully to maximum COMPRESSION limit and hold steady."),
        ("tension", "Pull buffer trolley fully to maximum TENSION limit and hold steady."),
    ]
    results: Dict[str, float] = {}

    print("\n=======================================================")
    print("        FLARE Type-P Sensor Calibration Wizard         ")
    print("=======================================================\n")

    for key, prompt in steps:
        if interactive:
            print(f"\n[Step: {key.upper()}]")
            print(prompt)
            try:
                input("Press Enter when positioned and ready to sample... ")
            except (KeyboardInterrupt, EOFError):
                print("\nCalibration aborted by operator.")
                return None

        print(f"Sampling {key} ({num_samples} samples)...", end="", flush=True)
        ok, mean, stddev, _ = sample_position(
            client.read_raw_adc,
            num_samples=num_samples,
            delay_s=sample_delay,
            max_stddev=max_stddev,
        )
        if not ok:
            print(f" FAILED!\nError: Reading unstable (stddev={stddev:.4f} > limit={max_stddev:.4f}).")
            print("Ensure the carriage is held completely steady without mechanical vibration.")
            return None

        print(f" DONE: mean={mean:.4f}, stddev={stddev:.4f}")
        results[key] = round(mean, 4)

    return results


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Deterministic Type-P ADC sensor calibration wizard for FLARE.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument("--port", help="Serial port (e.g. /dev/ttyACM0). Auto-detected if omitted.")
    ap.add_argument("--baud", type=int, default=115200, help="Serial baud rate (default: 115200).")
    ap.add_argument("--daemon-url", default=DEFAULT_DAEMON_URL, help="FLARE daemon HTTP URL.")
    ap.add_argument("--samples", type=int, default=DEFAULT_SAMPLES, help="Samples per position (default: 20).")
    ap.add_argument("--sample-delay", type=float, default=DEFAULT_SAMPLE_DELAY, help="Delay between samples in sec.")
    ap.add_argument("--max-stddev", type=float, default=DEFAULT_MAX_STDDEV, help="Max allowed stddev for stability.")
    ap.add_argument("--min-span", type=float, default=DEFAULT_MIN_SPAN, help="Minimum ADC span between positions.")
    ap.add_argument("--interactive", action="store_true", help="Prompt user interactively for each position.")
    ap.add_argument("--neutral", type=float, help="Explicit neutral ADC fraction (bypasses sampling).")
    ap.add_argument("--compression", type=float, help="Explicit compression ADC fraction.")
    ap.add_argument("--tension", type=float, help="Explicit tension ADC fraction.")
    ap.add_argument(
        "--write-firmware",
        action="store_true",
        help="Write calibrated parameters to firmware flash over serial/daemon.",
    )
    ap.add_argument(
        "--write-config",
        nargs="?",
        const="config.ini",
        help="Write calibrated parameters to config.ini (default: config.ini).",
    )
    ap.add_argument("--json", action="store_true", help="Output summary metrics in JSON format.")
    args = ap.parse_args()

    client: Optional[HardwareClient] = None
    cal_data: Dict[str, float] = {}

    # Check if explicit thresholds were supplied
    if args.neutral is not None and args.compression is not None and args.tension is not None:
        cal_data = {
            "neutral": args.neutral,
            "compression": args.compression,
            "tension": args.tension,
        }
    else:
        # Hardware sampling required
        client = HardwareClient(port=args.port, baud=args.baud, daemon_url=args.daemon_url)
        try:
            client.connect()
        except Exception as exc:
            print(f"Error connecting to FLARE device: {exc}", file=sys.stderr)
            return 1

        is_interactive = args.interactive or (sys.stdin.isatty() and not args.json)
        sampled = run_wizard(
            client=client,
            interactive=is_interactive,
            num_samples=args.samples,
            sample_delay=args.sample_delay,
            max_stddev=args.max_stddev,
            min_span=args.min_span,
        )
        if not sampled:
            if client:
                client.close()
            return 1
        cal_data = sampled

    # Validate results
    valid, msg, metrics = validate_calibration(
        neutral=cal_data["neutral"],
        compression=cal_data["compression"],
        tension=cal_data["tension"],
        min_span=args.min_span,
    )

    if not valid:
        print(f"\nCALIBRATION REJECTED: {msg}", file=sys.stderr)
        if client:
            client.close()
        return 1

    if args.json:
        out_payload = {
            "status": "VALID",
            "message": msg,
            "parameters": {
                "buf_psf_neutral": cal_data["neutral"],
                "buf_psf_max_comp": cal_data["compression"],
                "buf_psf_max_tens": cal_data["tension"],
            },
            "metrics": metrics,
        }
        print(json.dumps(out_payload, indent=2))
    else:
        print("\n-------------------------------------------------------")
        print(f"CALIBRATION SUCCESSFUL: {msg}")
        print("Recommended Parameters:")
        print(f"  buf_psf_neutral:  {cal_data['neutral']:.4f}")
        print(f"  buf_psf_max_comp: {cal_data['compression']:.4f}")
        print(f"  buf_psf_max_tens: {cal_data['tension']:.4f}")
        print("-------------------------------------------------------")

    # Handle write flags
    mutated = False
    if args.write_firmware:
        if client is None:
            client = HardwareClient(port=args.port, baud=args.baud, daemon_url=args.daemon_url)
            try:
                client.connect()
            except Exception as exc:
                print(f"Error connecting to apply firmware settings: {exc}", file=sys.stderr)
                return 1

        print("Applying parameters to firmware...")
        cmd_res = [
            client.send_cmd(f"SET:BUF_PSF_NEUTRAL:{cal_data['neutral']:.4f}"),
            client.send_cmd(f"SET:BUF_PSF_MAX_COMP:{cal_data['compression']:.4f}"),
            client.send_cmd(f"SET:BUF_PSF_MAX_TENS:{cal_data['tension']:.4f}"),
            client.send_cmd("SV:"),
        ]
        if any(r is None or r.startswith("ER:") for r in cmd_res):
            print(f"Error applying firmware parameters: {cmd_res}", file=sys.stderr)
            if client:
                client.close()
            return 1
        print("Parameters successfully saved to controller flash (SV: OK).")
        mutated = True

    if args.write_config:
        cfg_path = normalize_output(args.write_config)
        try:
            update_config_file(cfg_path, cal_data)
            print(f"Updated configuration written to: {cfg_path}")
            mutated = True
        except Exception as exc:
            print(f"Error updating config file {cfg_path}: {exc}", file=sys.stderr)
            if client:
                client.close()
            return 1

    if not mutated and not args.json:
        print("\n[Notice] Observe-only mode: no firmware or config changes applied.")
        print("Pass --write-firmware and/or --write-config to commit these values.")

    if client:
        client.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
