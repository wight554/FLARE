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
        with open(out, encoding="utf-8") as fh:
            return fh.read()


def macro_int(text, name):
    match = re.search(rf"#define {name}\s+(-?\d+)", text)
    if not match:
        raise AssertionError(f"missing macro {name}")
    return int(match.group(1))


def macro_float(text, name):
    match = re.search(rf"#define {name}\s+(-?\d+(?:\.\d+)?)f?", text)
    if not match:
        raise AssertionError(f"missing macro {name}")
    return float(match.group(1))


def test_scalar_config_emits_one_point():
    text = generate(BASE_CONFIG)
    baseline = macro_int(text, "CONF_BASELINE_SPS")

    assert macro_int(text, "CONF_FLOW_SCHED_CAP") == 8
    assert macro_int(text, "CONF_FLOW_SCHED_LEN") == 1
    assert f"{{{baseline}, {baseline}, 250}}" in text
    # Tier-3 internal constants (relay collapse, relay_min_flip, est sigma,
    # watchdog timeouts, ...) are no longer emitted as CONF_* — they live in
    # tune_internal.h.
    assert "CONF_RELAY_COLLAPSE_DELAY_MS" not in text
    assert "CONF_RELAY_MIN_FLIP_MM" not in text
    assert "CONF_CUT_FEED_MS" not in text
    assert "CONF_TC_TIMEOUT_CUT_MS" not in text


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


def test_demoted_key_is_ignored_not_emitted():
    # A demoted Tier-3 key in config.ini must warn-and-ignore (gen_config still
    # exits 0) and must NOT produce a CONF_* macro — its value lives in
    # tune_internal.h. Guards the tier-config-surface migration contract.
    text = generate(BASE_CONFIG + """
relay_collapse_delay_ms: 375
buf_variance_blend_frac: 0.3
est_sigma_hard_cap_mm: 2.0
""")

    assert "CONF_RELAY_COLLAPSE_DELAY_MS" not in text
    assert "CONF_BUF_VARIANCE_BLEND_FRAC" not in text
    assert "CONF_EST_SIGMA_HARD_CAP_MM" not in text


def test_demoted_watchdog_timeouts_not_emitted():
    # Watchdog timeouts demoted to tune_internal.h: stale config keys warn + are
    # ignored, and no CONF_* macro is emitted for them.
    text = generate(BASE_CONFIG + """
cut_feed_timeout_ms: 45000
tc_timeout_y_ms: 9000
reload_y_timeout_ms: 20000
""")

    assert "CONF_CUT_FEED_MS" not in text
    assert "CONF_TC_TIMEOUT_Y_MS" not in text
    assert "CONF_RELOAD_Y_TIMEOUT_MS" not in text


def main():
    test_scalar_config_emits_one_point()
    test_schedule_section_sorts_points()
    test_demoted_key_is_ignored_not_emitted()
    test_demoted_watchdog_timeouts_not_emitted()
    print("gen_config schedule tests PASS")


if __name__ == "__main__":
    main()
