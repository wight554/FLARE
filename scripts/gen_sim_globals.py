#!/usr/bin/env python3
"""Generate tests/host/<build>/sim_globals.c from firmware/src/main.c.

The sync/motion/toolchange control law references ~140 `g_` tunables and
state globals defined at file scope in main.c, initialized from CONF_*/
FLARE_INT_* macros. main.c itself cannot be linked into the host simulation
(it owns main(), GPIO/PIO init, and 11 pico/hardware includes), so this
script extracts just the top-level `g_` definitions and emits them into a
standalone translation unit, initializers copied verbatim.

Never hand-edit the output — it is regenerated on every build. See
openspec/changes/host-sync-sim/design.md "Globals: generated, not
hand-written" and scripts/test_settings_parity.py for the same brace-aware
technique applied to a different extraction problem.
"""
import os
import re
import sys

DEF_START = re.compile(
    r"^(?:volatile\s+)?[A-Za-z_][A-Za-z0-9_]*\s*\*?\s+g_[A-Za-z0-9_]*\s*(?:\[[^\]]*\])?\s*[=;]"
)

HEADER = """\
/* GENERATED FILE — do not edit by hand.
 * Produced by scripts/gen_sim_globals.py from firmware/src/main.c.
 * Regenerate: python3 scripts/gen_sim_globals.py firmware/src/main.c <out> */

#include "config.h"
#include "controller_shared.h"

"""


def extract_definitions(text):
    """Return the list of top-level `g_` definition statements, verbatim."""
    defs = []
    lines = text.splitlines(keepends=True)
    i = 0
    while i < len(lines):
        line = lines[i]
        if DEF_START.match(line):
            # Statement may span multiple lines (e.g. an array initializer);
            # scan forward tracking [] / {} depth for the terminating ';'.
            buf = []
            depth = 0
            j = i
            done = False
            while j < len(lines) and not done:
                seg = lines[j]
                k = 0
                while k < len(seg):
                    c = seg[k]
                    if c in "[{":
                        depth += 1
                    elif c in "]}":
                        depth -= 1
                    elif c == ";" and depth == 0:
                        done = True
                        k += 1
                        break
                    k += 1
                # Drop anything after the terminating ';' on its line — may be
                # a trailing comment whose close is on a following line we
                # don't otherwise capture.
                buf.append(seg[:k])
                j += 1
            defs.append("".join(buf).rstrip("\n"))
            i = j
        else:
            i += 1
    return defs


def generate(main_c_path, out_path):
    with open(main_c_path, encoding="utf-8") as f:
        text = f.read()

    defs = extract_definitions(text)
    if not defs:
        raise SystemExit(f"gen_sim_globals: found zero g_ definitions in {main_c_path}")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(HEADER)
        for d in defs:
            f.write(d + "\n")


def main(argv):
    if len(argv) != 3:
        print(f"usage: {argv[0]} <firmware/src/main.c> <output sim_globals.c>", file=sys.stderr)
        return 2
    generate(argv[1], argv[2])
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
