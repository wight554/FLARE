#!/usr/bin/env python3
"""Unit tests for verify_phase13_hw_log.py.

Drives the parser as a subprocess (sys.executable) over synthetic JSONL/CSV fixtures written
to a temp directory, exactly the shape `verify_hw_open_items.py watch` and
`flare_sync_check.py --daemon --csv` produce. These fixtures are the only proof this parser
is correct: the real 2h rig capture exists once and cannot be replayed."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_PATH = Path(__file__).resolve().parent / "verify_phase13_hw_log.py"


def _event(t, evt_type, data=""):
    return {"kind": "event", "time": t, "type": evt_type, "data": data}


def _write_jsonl(path, records):
    with open(path, "w", encoding="utf-8") as f:
        for rec in records:
            f.write(json.dumps(rec) + "\n")


def _write_csv(path, rows):
    """rows: list of dicts with at least BP/SM keys; header matches CSV_FIELDS subset used."""
    header = ["idx", "BP", "SM"]
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(",".join(header) + "\n")
        for row in rows:
            f.write(",".join(str(row[k]) for k in header) + "\n")


def _run(events_path, csv_path=None):
    cmd = [sys.executable, str(SCRIPT_PATH), "--events", str(events_path)]
    if csv_path is not None:
        cmd += ["--csv", str(csv_path)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    return proc.returncode, proc.stdout


class AllFourPassTests(unittest.TestCase):
    def test_clean_two_hour_print_passes_all_four_items(self):
        with tempfile.TemporaryDirectory() as tmp:
            events_path = Path(tmp) / "events.jsonl"
            csv_path = Path(tmp) / "capture.csv"

            # 2h span, no relief pauses, no tension-risk-high, no false stops.
            events = [
                _event(1000.0, "SYNC:AUTO_START"),
                _event(1000.0 + 7200.0, "SYNC:AUTO_STOP"),
            ]
            _write_jsonl(events_path, events)

            # Deliberate BL/toolchange touches (SM=0) reach the real rail extremes;
            # ordinary print-sync samples (SM=1) stay well clear of them.
            rows = [
                {"idx": 0, "BP": -1.00, "SM": 0},   # BL:T deliberate prime to the rail
                {"idx": 1, "BP": 1.00, "SM": 0},    # BL:C deliberate prime to the rail
                {"idx": 2, "BP": -0.10, "SM": 1},
                {"idx": 3, "BP": 0.00, "SM": 1},
                {"idx": 4, "BP": 0.20, "SM": 1},
                {"idx": 5, "BP": -0.20, "SM": 1},
            ]
            _write_csv(csv_path, rows)

            returncode, stdout = _run(events_path, csv_path)

            self.assertEqual(returncode, 0, stdout)
            self.assertIn("ITEM 1:", stdout)
            self.assertIn("ITEM 2:", stdout)
            self.assertIn("ITEM 3:", stdout)
            self.assertIn("ITEM 4:", stdout)
            for line in stdout.splitlines():
                if line.startswith("ITEM"):
                    self.assertIn("verdict=PASS", line, line)
            self.assertIn("OVERALL: PASS", stdout)


class FailingCaptureTests(unittest.TestCase):
    def test_tension_risk_high_at_baseline_and_a_false_stop_both_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            events_path = Path(tmp) / "events.jsonl"

            events = [_event(0.0, "SYNC:AUTO_START")]
            # Baseline was 13 in ~2h; matching it exactly must NOT read as an improvement.
            for i in range(13):
                events.append(_event(10.0 * (i + 1), "SYNC:TENSION_RISK_HIGH"))
            # A false stop: the mm distance trip fired on a print that should complete cleanly.
            events.append(_event(500.0, "SYNC:FAULT_HOLD"))
            events.append(_event(500.0, "SYNC:TENSION_STOP", "MM"))
            events.append(_event(7200.0, "SYNC:AUTO_STOP"))
            _write_jsonl(events_path, events)

            returncode, stdout = _run(events_path)

            self.assertNotEqual(returncode, 0, stdout)
            item2_line = next(line for line in stdout.splitlines() if line.startswith("ITEM 2:"))
            item3_line = next(line for line in stdout.splitlines() if line.startswith("ITEM 3:"))
            self.assertIn("verdict=FAIL", item2_line)
            self.assertIn("observed=13", item2_line)
            self.assertIn("verdict=FAIL", item3_line)
            self.assertIn("stop_mm:1", item3_line)
            self.assertIn("fault_hold:1", item3_line)
            self.assertIn("OVERALL: FAIL", stdout)


class MissingCsvNeedsInputTests(unittest.TestCase):
    def test_item_four_reports_needs_input_never_pass_without_csv(self):
        with tempfile.TemporaryDirectory() as tmp:
            events_path = Path(tmp) / "events.jsonl"
            # Otherwise-clean log: items 1-3 would all pass on their own.
            events = [
                _event(0.0, "SYNC:AUTO_START"),
                _event(7200.0, "SYNC:AUTO_STOP"),
            ]
            _write_jsonl(events_path, events)

            returncode, stdout = _run(events_path, csv_path=None)

            self.assertNotEqual(returncode, 0, stdout)
            item4_line = next(line for line in stdout.splitlines() if line.startswith("ITEM 4:"))
            self.assertIn("NEEDS-INPUT", item4_line)
            self.assertNotIn("verdict=PASS", item4_line)
            self.assertIn("OVERALL: FAIL", stdout)

    def test_item_four_reports_needs_input_when_csv_has_no_bp_samples(self):
        with tempfile.TemporaryDirectory() as tmp:
            events_path = Path(tmp) / "events.jsonl"
            csv_path = Path(tmp) / "empty.csv"
            _write_jsonl(events_path, [_event(0.0, "SYNC:AUTO_START")])
            with open(csv_path, "w", encoding="utf-8") as f:
                f.write("idx,BP,SM\n")  # header only, no data rows

            returncode, stdout = _run(events_path, csv_path)

            self.assertNotEqual(returncode, 0, stdout)
            item4_line = next(line for line in stdout.splitlines() if line.startswith("ITEM 4:"))
            self.assertIn("NEEDS-INPUT", item4_line)


class RailHitDetectionTests(unittest.TestCase):
    def test_sync_active_sample_within_break_delta_of_extreme_fails_item_four(self):
        with tempfile.TemporaryDirectory() as tmp:
            events_path = Path(tmp) / "events.jsonl"
            csv_path = Path(tmp) / "capture.csv"
            _write_jsonl(events_path, [_event(0.0, "SYNC:AUTO_START")])

            # Observed extremes are -1.00/1.00; a print-sync (SM=1) sample at 0.80 sits
            # within BL_BREAK_DELTA_NORM (0.25) of the +1.00 extreme -- a rail hit during
            # ordinary sync, not a deliberate BL/toolchange touch.
            rows = [
                {"idx": 0, "BP": -1.00, "SM": 0},
                {"idx": 1, "BP": 1.00, "SM": 0},
                {"idx": 2, "BP": 0.80, "SM": 1},
            ]
            _write_csv(csv_path, rows)

            returncode, stdout = _run(events_path, csv_path)

            self.assertNotEqual(returncode, 0, stdout)
            item4_line = next(line for line in stdout.splitlines() if line.startswith("ITEM 4:"))
            self.assertIn("verdict=FAIL", item4_line)
            self.assertIn("hits:1", item4_line)


if __name__ == "__main__":
    unittest.main()
