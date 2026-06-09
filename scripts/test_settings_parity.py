#!/usr/bin/env python3
"""Persistence round-trip parity guard for firmware/src/settings_store.c.

Enforces the persistence-contract invariant: a settings_t field written by
settings_save() must be read back by settings_load(), and every global loaded
from flash must also be seeded by settings_defaults(). Catches "write-only"
fields (saved, never loaded — e.g. the BUF_VARIANCE_BLEND_* bug) and
"loaded-never-defaulted" globals, without needing a field->global name map:
it compares the assignment LHS globals of load() against defaults() directly.
"""
import os
import re
import unittest

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "firmware", "src", "settings_store.c")

SKIP_FIELDS = {"magic", "version", "crc32"}

# Assignment LHS: bare identifier (optional [..] subscript) followed by a single
# '=' (not '==' / '!=' / '<=' / '>='). Rejects if/for/while and comparisons.
ASSIGN = re.compile(r"\s*([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*=(?!=)")


def func_body(text, name):
    m = re.search(r"\bvoid\s+" + name + r"\s*\([^)]*\)\s*\{", text)
    if not m:
        raise ValueError(f"parity: cannot locate {name}()")
    i, depth = m.end(), 1
    while depth and i < len(text):
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    return text[m.end():i - 1]


def struct_fields(text):
    m = re.search(r"typedef struct \{(.*?)\}\s*settings_t;", text, re.S)
    if not m:
        raise ValueError("parity: cannot locate settings_t")
    fields = []
    for line in m.group(1).splitlines():
        line = line.split("//")[0].strip()
        if not line.endswith(";"):
            continue
        parts = line[:-1].split(None, 1)        # [type, "a, b[N], *c"]
        if len(parts) < 2:
            continue
        for nm in parts[1].split(","):
            nm = nm.strip().lstrip("*").split("[")[0].strip()
            if re.fullmatch(r"[a-z_]\w*", nm) and nm not in SKIP_FIELDS:
                fields.append(nm)
    return set(fields)


def assigned_globals(body, require_sref=False):
    out = set()
    for line in body.splitlines():
        code = line.split("//")[0]
        if require_sref and "s->" not in code:
            continue
        m = ASSIGN.match(code)
        if m:
            out.add(m.group(1))
    return out


def strip_comments(text):
    # Remove single-line comments
    text = re.sub(r"//.*", "", text)
    # Remove multi-line comments
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return text


class TestSettingsParity(unittest.TestCase):
    def test_settings_parity(self):
        with open(SRC, encoding="utf-8") as f:
            text = f.read()
        fields = struct_fields(text)
        save_b = func_body(text, "settings_save")

        load_funcs = re.findall(r"\b(settings_load\w*)\s*\([^)]*\)\s*\{", text)
        self.assertTrue(load_funcs, "Could not find any settings_load functions")
        load_b = "\n".join(func_body(text, name) for name in sorted(set(load_funcs)))

        defs_funcs = re.findall(r"\b(settings_defaults\w*)\s*\([^)]*\)\s*\{", text)
        self.assertTrue(defs_funcs, "Could not find any settings_defaults functions")
        defs_b = "\n".join(func_body(text, name) for name in sorted(set(defs_funcs)))

        save_clean = strip_comments(save_b)
        load_clean = strip_comments(load_b)

        saved = {f for f in fields if re.search(r"s\." + f + r"\b", save_clean)}
        loaded = {f for f in fields if re.search(r"s->" + f + r"\b", load_clean)}
        load_globals = assigned_globals(load_clean, require_sref=True)
        def_globals = assigned_globals(strip_comments(defs_b))

        errors = []
        write_only = sorted(saved - loaded)
        if write_only:
            errors.append("saved but never loaded (write-only flash field): "
                          + ", ".join(write_only))
        loaded_not_defaulted = sorted(load_globals - def_globals)
        if loaded_not_defaulted:
            errors.append("loaded from flash but not seeded in settings_defaults(): "
                          + ", ".join(loaded_not_defaulted))

        if errors:
            self.fail("FAIL settings parity:\n" + "\n".join(f"  - {e}" for e in errors))


if __name__ == "__main__":
    unittest.main()
