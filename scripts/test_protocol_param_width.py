#!/usr/bin/env python3
"""Static guard for protocol SET/GET parameter token width."""

import os
import re
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import functest_adapter  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = ROOT / "firmware" / "src" / "protocol.c"


PUBLIC_PARAMS = [
    "SYNC_COMPRESSION_DRAIN_BUDGET_MM",
    "SYNC_COMPRESSION_DRAIN_FRAC",
    "SYNC_EST_ATTACK_ALPHA",
    "SYNC_TENSION_PROBE_MAX",
    "SYNC_TENSION_PROBE_UP",
    "SYNC_TENSION_PROBE_DOWN",
    "SYNC_TENSION_PROBE_NEUTRAL",
]


def main() -> None:
    text = PROTOCOL.read_text()
    max_match = re.search(r"#define\s+CMD_PARAM_MAX\s+(\d+)", text)
    width_match = re.search(r"#define\s+CMD_PARAM_SCAN_WIDTH\s+(\d+)", text)
    if not max_match or not width_match:
        raise SystemExit("protocol-param-width: missing CMD_PARAM_* defines")

    param_max = int(max_match.group(1))
    scan_width = int(width_match.group(1))
    longest = max(len(p) for p in PUBLIC_PARAMS)
    if longest + 1 > param_max:
        raise SystemExit(
            f"protocol-param-width: longest public param {longest} chars "
            f"does not fit CMD_PARAM_MAX={param_max}"
        )
    if longest > scan_width:
        raise SystemExit(
            f"protocol-param-width: longest public param {longest} chars "
            f"exceeds CMD_PARAM_SCAN_WIDTH={scan_width}"
        )
    # The SET scanner is assembled from CMD_PARAM_SCAN_FMT / CMD_VALUE_SCAN_FMT
    # (firmware-shared-constants-dry); check the macro carries the width and the
    # sscanf actually uses it.
    if f'#define CMD_PARAM_SCAN_FMT "%{scan_width}[^:]"' not in text:
        raise SystemExit("protocol-param-width: CMD_PARAM_SCAN_FMT width does not match CMD_PARAM_SCAN_WIDTH")
    if 'sscanf(p, CMD_PARAM_SCAN_FMT ":" CMD_VALUE_SCAN_FMT' not in text:
        raise SystemExit("protocol-param-width: SET sscanf width not wired")
    print("protocol-param-width PASS")


RunnerTests = functest_adapter.testcase_from_callable(main)  # unittest discover entry


if __name__ == "__main__":
    main()
