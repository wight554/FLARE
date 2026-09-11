## Why

`CUT_TIMEOUT_FEED_MS` (the per-phase motor-feed safety timeout inside the cutter state machine) is hardcoded at 5000 ms in `firmware/include/config.h` and is not wired into the `config.ini` → `gen_config.py` → `tune.h` pipeline. Users with large `CUT_FEED_MM` values (e.g. 145 mm at 600 mm/min requires ~14 500 ms) hit this 5-second cap, triggering `cutter_abort()` mid-feed with a `CUT:ERROR ABORTED` event and no actual cut.

## What Changes

- Add `cut_feed_timeout_ms` key to `config.ini` / `config.ini.example` (default 30 000 ms).
- Wire through `gen_config.py` → `CONF_CUT_FEED_MS` macro (already consumed in `main.c`).
- Add `cutter_feed_timeout_ms` field to `settings_t`; save/load in `settings_store.c`; bump `SETTINGS_VERSION`.
- Add `SET:CUT_FEED_MS` / `GET:CUT_FEED_MS` handlers in `protocol.c` (mirrors existing `TC_CUT_MS` pattern).
- Add to `flare_cmd.py --dump` output.
- Document in `MANUAL.md`.
- Optionally wire `CUT_TIMEOUT_SETTLE_MS` (same pattern, hardcoded 1500 ms, `CONF_CUT_SETTLE_MS`) as `cut_settle_timeout_ms` in the same pass; avoids re-opening these files later.

## Capabilities

### New Capabilities

- `cutter-feed-timeout`: Runtime-tunable per-phase motor-feed timeout for the cutter state machine, preventing spurious aborts when `CUT_FEED_MM` is large.

### Modified Capabilities

<!-- No existing spec-level behavior changes — cutter behavior at default distances is unchanged. -->

## Impact

- `config.ini`, `config.ini.example`: new `cut_feed_timeout_ms` (and optionally `cut_settle_timeout_ms`) key.
- `scripts/gen_config.py`: emit `CONF_CUT_FEED_MS` (and `CONF_CUT_SETTLE_MS`) from config rather than relying on `config.h` fallback.
- `firmware/include/config.h`: keep `CONF_CUT_FEED_MS` / `CONF_CUT_SETTLE_MS` as fallback defaults only.
- `firmware/src/settings_store.c`: add field to `settings_t`, save/load, version bump.
- `firmware/src/protocol.c`: `SET:`/`GET:` handlers, `--dump` entry.
- `MANUAL.md`: document `CUT_FEED_MS` (and `CUT_SETTLE_MS`) parameters.
- No change to `cutter.c` logic; only the constant it reads changes.
