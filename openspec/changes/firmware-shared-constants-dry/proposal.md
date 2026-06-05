## Why

`code-readability-overhaul` named per-file magic numbers but did not dedup across
translation units. A newbie audit found the same constants defined many times with
inconsistent style, plus two undocumented global-naming conventions — both erode
trust ("which definition is canonical?") and readability:

- `MS_PER_SECOND_F = 1000.0f` defined 7× (3× `#define`, 4× `static const`).
- `HALF_F = 0.5f` 4×; `FULL_SPAN_MULT_F = 2.0f` 2×.
- `char lane_s[2] = {(char)('0' + id), 0}` lane-digit idiom repeated 11×.
- 132 runtime-mutable tunables named `UPPER_CASE` (`FEED_SPS`) look like compile-time
  constants; `g_lower_case` is used for internal state — the rule is real but unwritten.

## What Changes

- Add `firmware/include/firmware_constants.h` with the shared numeric constants
  (`MS_PER_SECOND_F`, `HALF_F`, `FULL_SPAN_MULT_F`), included via
  `controller_shared.h` so every TU sees one definition. Remove the per-TU copies.
- Add a `lane_id_str()` inline helper (in `controller_shared.h`) and replace the 11
  hand-rolled lane-digit initializers.
- Document the global-naming convention in `STYLE.md`: `UPPER_CASE` = runtime-mutable
  config-backed tunable; `g_` = internal module/runtime state; `CONF_*` = generated
  compile-time default. Documentation only — no identifier renames this change.

No behavior change. No serial protocol, `config.ini` key, `tune.h`, runtime tunable,
or `settings_t`/`SETTINGS_VERSION` change. Build-verified.

## Capabilities

### Modified Capabilities
- `code-style-standard`: adds a single-definition (DRY) rule for shared constants and
  a documented global-naming-convention requirement.

## Impact

- New file: `firmware/include/firmware_constants.h`.
- Touched: `firmware/include/controller_shared.h` (include + `lane_id_str`), and the
  `.c` files holding duplicate constants / lane-digit idioms (`cutter.c`, `main.c`,
  `motion.c`, `protocol.c`, `settings_store.c`, `sync.c`, `sync_buf.c`, `sync_relay.c`,
  `sync_analog.c`, `toolchange.c`), plus `STYLE.md`.
- Out of scope: renaming the 132 `UPPER_CASE` tunables (high churn / regression risk —
  separate change if desired).
