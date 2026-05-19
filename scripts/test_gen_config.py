#!/usr/bin/env python3
"""Regression checks for config generation."""

import os
import re
import subprocess
import sys
import tempfile


REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(REPO, "scripts", "gen_config.py")


BASE_CONFIG = """\
microsteps: 16
rotation_distance: 22.679
run_current: 0.980
gear_ratio: 50:17
baseline_rate: 1200
sync_compression_bias_frac: 0.25
sync_cannot_refill_mm: 50.0
sync_cannot_relieve_mm: 50.0
"""


def generate(config_text):
    with tempfile.TemporaryDirectory() as tmp:
        cfg = os.path.join(tmp, "config.ini")
        out = os.path.join(tmp, "tune.h")
        with open(cfg, "w", encoding="utf-8") as fh:
            fh.write(config_text)
        subprocess.run([sys.executable, GEN, cfg, out], check=True, cwd=REPO)
        with open(out, "r", encoding="utf-8") as fh:
            return fh.read()


def macro_int(text, name):
    match = re.search(rf"#define {name}\s+(-?\d+)", text)
    if not match:
        raise AssertionError(f"missing macro {name}")
    return int(match.group(1))


def test_scalar_config_emits_one_point():
    text = generate(BASE_CONFIG)
    baseline = macro_int(text, "CONF_BASELINE_SPS")

    assert macro_int(text, "CONF_FLOW_SCHED_CAP") == 8
    assert macro_int(text, "CONF_FLOW_SCHED_LEN") == 1
    assert f"{{{baseline}, {baseline}, 250}}" in text
    assert macro_int(text, "CONF_RELAY_COLLAPSE_DELAY_MS") == 250
    assert macro_int(text, "CONF_RELAY_COLLAPSE_RAMP_MULT") == 3
    assert macro_int(text, "CONF_RELAY_COLLAPSE_CAP_MS") == 600


def test_schedule_section_sorts_points():
    text = generate(BASE_CONFIG + """
flow_schedule_cap: 4

[flow_schedule.v1]
point1: 12000, 13000, 0.40
point0: 6000, 7000, 0.30
""")

    assert macro_int(text, "CONF_FLOW_SCHED_CAP") == 4
    assert macro_int(text, "CONF_FLOW_SCHED_LEN") == 2
    assert "{{6000, 7000, 300}, {12000, 13000, 400}}" in text


def test_relay_collapse_config_overrides_defaults():
    text = generate(BASE_CONFIG + """
relay_collapse_delay_ms: 375
relay_collapse_ramp_mult: 2
relay_collapse_cap_ms: 800
""")

    assert macro_int(text, "CONF_RELAY_COLLAPSE_DELAY_MS") == 375
    assert macro_int(text, "CONF_RELAY_COLLAPSE_RAMP_MULT") == 2
    assert macro_int(text, "CONF_RELAY_COLLAPSE_CAP_MS") == 800


def main():
    test_scalar_config_emits_one_point()
    test_schedule_section_sorts_points()
    test_relay_collapse_config_overrides_defaults()
    print("gen_config schedule tests PASS")


if __name__ == "__main__":
    main()
