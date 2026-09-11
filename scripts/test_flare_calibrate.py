#!/usr/bin/env python3
"""Unit tests for scripts/flare_calibrate.py."""

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(__file__))

import flare_calibrate as calib


class TestStatsAndValidation(unittest.TestCase):
    def test_calc_stats(self):
        self.assertEqual(calib.calc_stats([]), (0.0, 0.0))
        self.assertEqual(calib.calc_stats([0.5]), (0.5, 0.0))

        samples = [0.49, 0.50, 0.51]
        mean, stddev = calib.calc_stats(samples)
        self.assertAlmostEqual(mean, 0.50, places=4)
        self.assertAlmostEqual(stddev, 0.01, places=4)

    def test_validate_normal_orientation(self):
        # Normal: compression > neutral > tension
        ok, msg, metrics = calib.validate_calibration(
            neutral=0.50,
            compression=0.85,
            tension=0.15,
            min_span=0.05,
        )
        self.assertTrue(ok)
        self.assertEqual(metrics["reversed"], 0.0)
        self.assertAlmostEqual(metrics["comp_span"], 0.35, places=4)
        self.assertAlmostEqual(metrics["tens_span"], 0.35, places=4)

    def test_validate_reversed_orientation(self):
        # Reversed: compression < neutral < tension
        ok, msg, metrics = calib.validate_calibration(
            neutral=0.45,
            compression=0.10,
            tension=0.90,
            min_span=0.05,
        )
        self.assertTrue(ok)
        self.assertEqual(metrics["reversed"], 1.0)
        self.assertAlmostEqual(metrics["comp_span"], 0.35, places=4)
        self.assertAlmostEqual(metrics["tens_span"], 0.45, places=4)

    def test_validate_monotonicity_violation(self):
        # Neutral not between compression and tension
        ok, msg, _ = calib.validate_calibration(
            neutral=0.95,
            compression=0.85,
            tension=0.15,
        )
        self.assertFalse(ok)
        self.assertIn("Monotonicity error", msg)

    def test_validate_span_too_small(self):
        # Compression span 0.02 < min_span 0.05
        ok, msg, _ = calib.validate_calibration(
            neutral=0.50,
            compression=0.52,
            tension=0.20,
            min_span=0.05,
        )
        self.assertFalse(ok)
        self.assertIn("Compression span", msg)

        # Tension span 0.03 < min_span 0.05
        ok, msg, _ = calib.validate_calibration(
            neutral=0.50,
            compression=0.80,
            tension=0.47,
            min_span=0.05,
        )
        self.assertFalse(ok)
        self.assertIn("Tension span", msg)

    def test_validate_out_of_bounds(self):
        ok, msg, _ = calib.validate_calibration(neutral=-0.1, compression=0.8, tension=0.1)
        self.assertFalse(ok)
        self.assertIn("out of ADC range", msg)

        ok, msg, _ = calib.validate_calibration(neutral=0.5, compression=1.2, tension=0.1)
        self.assertFalse(ok)
        self.assertIn("out of ADC range", msg)


class TestSampling(unittest.TestCase):
    def test_sample_position_stable(self):
        readings = [0.501, 0.499, 0.500, 0.502, 0.498]
        idx = 0

        def mock_reader():
            nonlocal idx
            val = readings[idx % len(readings)]
            idx += 1
            return val

        ok, mean, stddev, samples = calib.sample_position(
            reader_fn=mock_reader,
            num_samples=5,
            delay_s=0.0,
            max_stddev=0.01,
        )
        self.assertTrue(ok)
        self.assertEqual(len(samples), 5)
        self.assertAlmostEqual(mean, 0.500, places=3)
        self.assertLess(stddev, 0.01)

    def test_sample_position_unstable(self):
        readings = [0.40, 0.60, 0.45, 0.65]
        idx = 0

        def mock_reader():
            nonlocal idx
            val = readings[idx % len(readings)]
            idx += 1
            return val

        ok, mean, stddev, samples = calib.sample_position(
            reader_fn=mock_reader,
            num_samples=4,
            delay_s=0.0,
            max_stddev=0.02,
        )
        self.assertFalse(ok)
        self.assertGreater(stddev, 0.02)

    def test_sample_position_read_error(self):
        def failing_reader():
            return None

        ok, _, _, _ = calib.sample_position(
            reader_fn=failing_reader,
            num_samples=3,
            delay_s=0.0,
        )
        self.assertFalse(ok)


class TestConfigFileUpdate(unittest.TestCase):
    def test_update_existing_config(self):
        content = (
            "[flare]\n"
            "lane_count: 2\n\n"
            "[sync]\n"
            "buf_sensor_type: 1\n"
            "# buf_psf_max_comp: 1.0\n"
            "# buf_psf_max_tens: 0.0\n"
            "buf_psf_neutral: 0.500\n"
        )
        with tempfile.NamedTemporaryFile("w", delete=False) as tf:
            tf.write(content)
            tf_path = tf.name

        try:
            params = {
                "neutral": 0.485,
                "compression": 0.892,
                "tension": 0.114,
            }
            calib.update_config_file(tf_path, params)

            with open(tf_path, encoding="utf-8") as fh:
                updated = fh.read()

            self.assertIn("buf_psf_max_comp: 0.892\n", updated)
            self.assertIn("buf_psf_max_tens: 0.114\n", updated)
            self.assertIn("buf_psf_neutral: 0.485\n", updated)
            self.assertIn("lane_count: 2", updated)
        finally:
            if os.path.exists(tf_path):
                os.remove(tf_path)


class TestCliIntegration(unittest.TestCase):
    def test_cli_explicit_json(self):
        import io
        from contextlib import redirect_stdout

        old_argv = sys.argv
        sys.argv = [
            "flare_calibrate.py",
            "--neutral",
            "0.510",
            "--compression",
            "0.875",
            "--tension",
            "0.145",
            "--json",
        ]
        buf = io.StringIO()
        try:
            with redirect_stdout(buf):
                rc = calib.main()
            self.assertEqual(rc, 0)
            data = json.loads(buf.getvalue())
            self.assertEqual(data["status"], "VALID")
            self.assertAlmostEqual(data["parameters"]["buf_psf_neutral"], 0.510)
        finally:
            sys.argv = old_argv

    def test_cli_invalid_explicit(self):
        old_argv = sys.argv
        sys.argv = [
            "flare_calibrate.py",
            "--neutral",
            "0.99",
            "--compression",
            "0.80",
            "--tension",
            "0.20",
        ]
        try:
            rc = calib.main()
            self.assertEqual(rc, 1)
        finally:
            sys.argv = old_argv


if __name__ == "__main__":
    unittest.main()
