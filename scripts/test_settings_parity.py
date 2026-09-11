#!/usr/bin/env python3
"""Persistence round-trip parity guard for firmware/src/settings_store.c and settings_store.h.

Enforces persistence invariants:
1. TLV Tag Parity: Every enum tag in settings_tag_t must be serialized in settings_save()
   and deserialized in settings_load_tlv_tag().
2. Default Initialization Parity: Every persistent global loaded from flash must be
   seeded in settings_defaults().
3. Legacy v63 Parity: Every frozen field in settings_t_v63 must be read in settings_load_v63().
4. Dump Rebuild Parity: Every config.ini key flare_cmd.py --dump emits must be a key
   gen_config.py accepts, so a dumped config always rebuilds (config-surface-tiers).
"""
import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flare_cmd  # noqa: E402
import gen_config  # noqa: E402

ROOT = os.path.dirname(os.path.abspath(__file__))
HDR = os.path.join(ROOT, "..", "firmware", "include", "settings_store.h")
SRC = os.path.join(ROOT, "..", "firmware", "src", "settings_store.c")

SKIP_FIELDS_V63 = {"magic", "version", "crc32", "seq"}
SKIP_GLOBALS = {"g_active_sector", "g_seq"}


def func_body(text, name):
    m = re.search(r"\bvoid\s+" + name + r"\s*\([^)]*\)\s*\{", text)
    if not m:
        # Also match static functions with non-void return types
        m = re.search(r"\b" + name + r"\s*\([^)]*\)\s*\{", text)
    if not m:
        raise ValueError(f"parity: cannot locate {name}()")
    i, depth = m.end(), 1
    while depth and i < len(text):
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    return text[m.end():i - 1]


def enum_tags(text):
    m = re.search(r"typedef enum \{(.*?)\}\s*settings_tag_t;", text, re.S)
    if not m:
        raise ValueError("parity: cannot locate settings_tag_t")
    tags = re.findall(r"\b(TAG_[A-Z0-9_]+)\b", m.group(1))
    return set(tags)


def struct_fields_v63(text):
    m = re.search(r"typedef struct \{(.*?)\}\s*settings_t_v63;", text, re.S)
    if not m:
        raise ValueError("parity: cannot locate settings_t_v63")
    fields = []
    for line in m.group(1).splitlines():
        line = line.split("//")[0].strip()
        if not line.endswith(";"):
            continue
        parts = line[:-1].split(None, 1)
        if len(parts) < 2:
            continue
        for nm in parts[1].split(","):
            nm = nm.strip().lstrip("*").split("[")[0].strip()
            if re.fullmatch(r"[a-z_]\w*", nm) and nm not in SKIP_FIELDS_V63:
                fields.append(nm)
    return set(fields)


def strip_comments(text):
    text = re.sub(r"//.*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return text


def extract_globals(body):
    assigned = set(re.findall(r"\b(g_[a-zA-Z0-9_]+)\s*(?:\[[^\]]*\])?\s*=", body))
    memcpy_targets = set(re.findall(r"memcpy\s*\(\s*(?:&)?(g_[a-zA-Z0-9_]+)", body))
    return assigned | memcpy_targets


class TestSettingsParity(unittest.TestCase):
    def test_settings_parity(self):
        with open(HDR, encoding="utf-8") as f:
            hdr_text = f.read()
        with open(SRC, encoding="utf-8") as f:
            src_text = f.read()

        tags = enum_tags(hdr_text)
        self.assertTrue(tags, "No tags found in settings_tag_t")

        save_b = strip_comments(func_body(src_text, "settings_save"))
        load_tlv_b = strip_comments(func_body(src_text, "settings_load_tlv_tag"))

        saved_tags = {t for t in tags if re.search(r"\b" + t + r"\b", save_b)}
        loaded_tags = {t for t in tags if re.search(r"\b" + t + r"\b", load_tlv_b)}

        errors = []
        unsaved_tags = sorted(tags - saved_tags)
        if unsaved_tags:
            errors.append(f"tags in settings_tag_t but never serialized in settings_save(): {', '.join(unsaved_tags)}")

        unloaded_tags = sorted(tags - loaded_tags)
        if unloaded_tags:
            errors.append(f"tags in settings_tag_t but not handled in settings_load_tlv_tag(): {', '.join(unloaded_tags)}")

        defs_funcs = re.findall(r"\b(settings_defaults\w*)\s*\([^)]*\)\s*\{", src_text)
        self.assertTrue(defs_funcs, "Could not find settings_defaults functions")
        defs_b = "\n".join(func_body(src_text, name) for name in sorted(set(defs_funcs)))

        load_funcs = re.findall(r"\b(settings_load\w*)\s*\([^)]*\)\s*\{", src_text)
        self.assertTrue(load_funcs, "Could not find settings_load functions")
        load_b = "\n".join(func_body(src_text, name) for name in sorted(set(load_funcs)))

        def_globals = extract_globals(strip_comments(defs_b))
        load_globals = extract_globals(strip_comments(load_b)) - SKIP_GLOBALS

        loaded_not_defaulted = sorted(load_globals - def_globals)
        if loaded_not_defaulted:
            errors.append(f"loaded from flash but not seeded in settings_defaults(): {', '.join(loaded_not_defaulted)}")

        # Legacy v63 struct check
        v63_fields = struct_fields_v63(src_text)
        load_v63_b = strip_comments(func_body(src_text, "settings_load_v63"))
        loaded_v63 = {f for f in v63_fields if re.search(r"s->" + f + r"\b", load_v63_b)}
        v63_unloaded = sorted(v63_fields - loaded_v63)
        if v63_unloaded:
            errors.append(f"v63 fields not loaded in settings_load_v63(): {', '.join(v63_unloaded)}")

        if errors:
            self.fail("FAIL settings parity:\n" + "\n".join(f"  - {e}" for e in errors))


    def test_dump_keys_rebuild_with_gen_config(self):
        """A `flare_cmd.py --dump` config must pass gen_config's unknown-key check.
        Runtime-only state (e.g. BYPASS) has no config.ini key and must not be dumped."""
        bad = sorted(
            key for (_, key, lane_aware) in flare_cmd.DUMP_PARAMS
            for probe in ([f"{key}_l1", f"{key}_l2"] if lane_aware else [key])
            if not gen_config.valid_config_key(probe) and probe not in gen_config.DEPRECATED_KEYS
        )
        self.assertEqual(bad, [], f"--dump emits keys gen_config.py rejects: {bad}")

if __name__ == "__main__":
    unittest.main()
