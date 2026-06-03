#!/usr/bin/env python3
"""Static guard for protocol SET/GET parameter token width."""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = ROOT / "firmware" / "src" / "protocol.c"


PUBLIC_PARAMS = [
    "SYNC_COMPRESSION_DRAIN_BUDGET_MM",
    "SYNC_COMPRESSION_DRAIN_FRAC",
    "SYNC_EST_ATTACK_ALPHA",
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
    if f"%{scan_width}[^:]:%31s" not in text:
        raise SystemExit("protocol-param-width: SET sscanf width not wired")
    print("protocol-param-width PASS")


if __name__ == "__main__":
    main()
