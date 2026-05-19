## 1. config.ini + gen_config.py

- [ ] 1.1 Add `cut_feed_timeout_ms: 30000` and `cut_settle_timeout_ms: 3000` to `config.ini` and `config.ini.example` under the Cutter/Servo section.
- [ ] 1.2 Add defaults `"cut_feed_timeout_ms": "30000"` and `"cut_settle_timeout_ms": "3000"` to the `DEFAULTS` dict in `gen_config.py`.
- [ ] 1.3 Emit `#define CONF_CUT_FEED_MS   {get('cut_feed_timeout_ms')}` and `#define CONF_CUT_SETTLE_MS {get('cut_settle_timeout_ms')}` from `gen_config.py`; remove (or keep as fallback) the hardcoded lines in `config.h`. Verify `python3 scripts/gen_config.py` produces updated `tune.h` with correct values.
- [ ] 1.4 Run `python3 scripts/test_gen_config.py` — update any assertions for removed/changed macros; confirm green.

## 2. settings_store.c — struct + save/load + version bump

- [ ] 2.1 Add `int cutter_feed_timeout_ms` and `int cutter_settle_timeout_ms` fields to `settings_t` in `settings_store.c`.
- [ ] 2.2 In `settings_defaults()`: add `CUT_TIMEOUT_FEED_MS = CONF_CUT_FEED_MS;` and `CUT_TIMEOUT_SETTLE_MS = CONF_CUT_SETTLE_MS;` (mirrors the pattern for `CUT_FEED_SPS` etc.).
- [ ] 2.3 In `settings_save()`: add `s.cutter_feed_timeout_ms = CUT_TIMEOUT_FEED_MS;` and `s.cutter_settle_timeout_ms = CUT_TIMEOUT_SETTLE_MS;`.
- [ ] 2.4 In `settings_load()`: add `CUT_TIMEOUT_FEED_MS = s->cutter_feed_timeout_ms;` and `CUT_TIMEOUT_SETTLE_MS = s->cutter_settle_timeout_ms;`. (Remove existing lone `CUT_TIMEOUT_SETTLE_MS = s->cutter_settle_ms;` line — that field is being superseded.)
- [ ] 2.5 Bump `SETTINGS_VERSION` by 1. Grep for current value first. Confirm `_Static_assert(sizeof(settings_t) <= 512, ...)` still passes.
- [ ] 2.6 `ninja -C build_local` green.

## 3. protocol.c — GET/SET + dump

- [ ] 3.1 Add `SET:` handler for `CUT_FEED_MS`: `CUT_TIMEOUT_FEED_MS = clamp_i(iv, 1000, 120000);` followed by `settings_save();` (mirror `TC_CUT_MS` pattern).
- [ ] 3.2 Add `SET:` handler for `CUT_SETTLE_MS`: `CUT_TIMEOUT_SETTLE_MS = clamp_i(iv, 500, 10000);` followed by `settings_save();`.
- [ ] 3.3 Add `GET:` handlers (both lane-scoped and global) for `CUT_FEED_MS` and `CUT_SETTLE_MS` returning `CUT_FEED_MS:<value>` and `CUT_SETTLE_MS:<value>`.
- [ ] 3.4 `ninja -C build_local` green.

## 4. flare_cmd.py — dump entries

- [ ] 4.1 Add `GET:CUT_FEED_MS` and `GET:CUT_SETTLE_MS` to the `--dump` section of `scripts/flare_cmd.py` alongside existing cutter dump entries.
- [ ] 4.2 `python3 -m py_compile scripts/*.py` green.

## 5. MANUAL.md — documentation

- [ ] 5.1 Add `CUT_FEED_MS` and `CUT_SETTLE_MS` parameter entries to `MANUAL.md` under the cutter parameter section, documenting range, default, and effect.

## 6. Build + validation

- [ ] 6.1 Full host check: `ninja -C build_local`, `python3 -m py_compile scripts/*.py`, `python3 scripts/test_gen_config.py`.
- [ ] 6.2 On-hardware: `GET:CUT_FEED_MS` returns 30000; `GET:CUT_SETTLE_MS` returns 3000; `SET:CUT_FEED_MS:15000` + `SV:` + reflash → `GET:CUT_FEED_MS` returns 15000; `CU` with large `CUT_FEED_MM` completes without `CUT:ERROR ABORTED`.
- [ ] 6.3 Commit + push.
