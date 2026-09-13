#!/usr/bin/env python3
"""Machine-proves the `ST:` status line always fits `STATUS_LINE_MAX`.

Phase 13 Plan 02 Task 2 (13-RESEARCH.md Pitfall 3): the extended-tail
`snprintf` in `protocol_status.c` silently truncates on overflow — no
compile-time or runtime error otherwise. Rather than trust that by
inspection, this test regex-reads the two format-string literals straight
out of `cmd_handle_status_dump()`, computes a documented WORST-CASE rendered
width for every conversion specifier, and asserts the sum stays strictly
below `STATUS_LINE_MAX`. A future field that pushes the line over budget
fails this test, not a silent truncation on a real rig.

Same regex-the-C-source idiom `scripts/test_settings_parity.py` uses for
settings_store.h/.c. Note (project memory): `unittest discover` silently
skips a module with no `unittest.TestCase` subclass -- this file defines
one.
"""
import os
import re
import unittest

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
PROTOCOL_H = os.path.join(REPO_ROOT, "firmware", "include", "protocol.h")
PROTOCOL_STATUS_C = os.path.join(REPO_ROOT, "firmware", "src", "protocol_status.c")

# Worst-case rendered width for a signed/unsigned 32-bit %d/%u conversion:
# "-2147483648" is 11 characters, the widest either can render as.
INT_WIDTH = 11
# Worst-case rendered width for a %.1f/%.2f conversion of a rate or position
# in this file (all bounded by six integer digits in practice): sign + 6
# digits + '.' + up to 2 decimals = 10; rounded up to 12 for headroom so a
# field whose bound assumption drifts slightly doesn't require touching
# this constant.
FLOAT_WIDTH = 12

# Per-field %s worst-case widths, matched against the literal label text
# immediately preceding each %s in the format string (not by position, so
# reordering fields can't silently mis-attribute a width). Every %s in
# cmd_handle_status_dump()'s two format strings MUST have an entry here --
# an unrecognized %s fails the test loudly rather than being silently
# under-budgeted.
STRING_FIELD_WIDTHS = {
    # tc_state_name() (toolchange.c): longest literal is "LOAD_RETRY_RETRACT"
    # (19 chars); +1 margin.
    "TC:%s": 20,
    # task_name() (toolchange.c): longest literal is "LOAD_FULL" (9 chars);
    # +1 margin.
    "L1T:%s": 10,
    "L2T:%s": 10,
    # buf_state_name() via buf_status_label() (protocol_status.c /
    # sync_buf.c): longest literal is "COMPRESSION" (11 chars); +1 margin.
    "BUF:%s": 12,
    # sync_buffer_lock_arm_str() (sync.c): always exactly "0", "T", or "C"
    # (1 char); +1 margin.
    "BL:%s": 2,
    # g_marker_tag (controller_shared.h): char[MARKER_TAG_LEN=32], worst
    # case fills every byte but the NUL terminator.
    "MK:%u:%s": 31,
}

# Regex matching one printf conversion specifier (excludes a literal %%).
SPEC_RE = re.compile(r"%(?!%)[-+ 0#]*\d*(?:\.\d+)?[a-zA-Z]")


def _read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def _cmd_line_max():
    text = _read(PROTOCOL_H)
    m = re.search(r"#define\s+CMD_LINE_MAX\s+(\d+)\b", text)
    if not m:
        raise AssertionError(f"CMD_LINE_MAX not found in {PROTOCOL_H}")
    return int(m.group(1))


def _status_line_max(cmd_line_max):
    text = _read(PROTOCOL_STATUS_C)
    m = re.search(r"STATUS_LINE_MAX\s*=\s*CMD_LINE_MAX\s*-\s*(\d+)\b", text)
    if not m:
        raise AssertionError(f"STATUS_LINE_MAX arithmetic not found in {PROTOCOL_STATUS_C}")
    return cmd_line_max - int(m.group(1))


def _func_body(text, name):
    m = re.search(r"\b" + re.escape(name) + r"\s*\([^)]*\)\s*\{", text)
    if not m:
        raise AssertionError(f"cannot locate {name}() in {PROTOCOL_STATUS_C}")
    i, depth = m.end(), 1
    while depth and i < len(text):
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    return text[m.end():i - 1]


def _extract_format_literals(body):
    """Every snprintf(...) call in `body`, as one concatenated literal per
    call (C adjacent string-literal concatenation: multiple "..." "..."
    segments on separate lines form a single literal with no separator)."""
    literals = []
    for call_start in [m.start() for m in re.finditer(r"\bsnprintf\s*\(", body)]:
        # Slice from the call to its matching close paren so the literal
        # scan can't run into a LATER snprintf's own string arguments.
        i, depth = call_start, 0
        j = body.index("(", call_start)
        i, depth = j, 1
        while depth and i + 1 < len(body):
            i += 1
            depth += {"(": 1, ")": -1}.get(body[i], 0)
        call_text = body[call_start:i + 1]
        segments = re.findall(r'"((?:[^"\\]|\\.)*)"', call_text)
        if not segments:
            continue
        literals.append("".join(segments))
    if len(literals) != 2:
        raise AssertionError(
            f"expected exactly 2 snprintf format literals in cmd_handle_status_dump(), "
            f"found {len(literals)} -- update this test's assumptions if the function "
            f"structure changed")
    return literals


def _worst_case_length(fmt):
    """Sum of literal text plus each conversion specifier's documented
    worst-case rendered width. Raises on any %s this test doesn't have a
    labeled width for -- silently guessing a width defeats the point."""
    total = len(fmt)
    remaining = fmt
    for label, width in STRING_FIELD_WIDTHS.items():
        count = remaining.count(label)
        if count:
            total += count * (width - 2)  # "%s" itself is 2 source chars
            remaining = remaining.replace(label, "")
    if "%s" in remaining:
        raise AssertionError(
            f"unrecognized %s in status format string (no STRING_FIELD_WIDTHS entry): "
            f"{remaining!r}")
    for m in SPEC_RE.finditer(remaining):
        spec = m.group(0)
        kind = spec[-1]
        if kind in ("d", "u"):
            total += INT_WIDTH - len(spec)
        elif kind == "f":
            total += FLOAT_WIDTH - len(spec)
        elif kind == "c":
            # A %c conversion renders exactly one character, always --
            # structurally provable, not just an observed bound (e.g. ARM:
            # in protocol_status.c, rendered from a '0'/'1' literal ternary).
            total += 1 - len(spec)
        else:
            raise AssertionError(f"unrecognized conversion specifier {spec!r} in {fmt!r}")
    return total


class TestStatusLineBudget(unittest.TestCase):
    def test_status_line_fits_budget_with_headroom(self):
        cmd_line_max = _cmd_line_max()
        status_line_max = _status_line_max(cmd_line_max)
        text = _read(PROTOCOL_STATUS_C)
        body = _func_body(text, "cmd_handle_status_dump")
        literals = _extract_format_literals(body)
        worst_case_total = sum(_worst_case_length(fmt) for fmt in literals)
        headroom = status_line_max - worst_case_total
        self.assertLess(
            worst_case_total, status_line_max,
            f"worst-case ST: line ({worst_case_total} chars) does not fit "
            f"STATUS_LINE_MAX ({status_line_max} chars, CMD_LINE_MAX={cmd_line_max}) -- "
            f"headroom is {headroom} chars; shorten or drop a field, don't relax this "
            f"assertion")
        # Not a hard requirement, just visibility for whoever adds the next
        # field: report the remaining budget so they know how much room is
        # actually left before this test starts failing.
        print(f"\nST: line worst-case {worst_case_total}/{status_line_max} chars "
              f"({headroom} chars headroom)")


if __name__ == "__main__":
    unittest.main()
