## 1. Shared constants header

- [x] 1.1 Add `firmware/include/firmware_constants.h` defining `MS_PER_SECOND_F`, `HALF_F`, `FULL_SPAN_MULT_F` once (`#define`, with `#pragma once`)
- [x] 1.2 Include `firmware_constants.h` from `controller_shared.h` so all TUs see it
- [x] 1.3 Remove the per-`.c` duplicate definitions (`cutter.c`, `main.c`, `motion.c`, `protocol.c`, `settings_store.c`, `sync.c`, `sync_buf.c`, `sync_analog.c`, `sync_relay.c`)
- [x] 1.4 `ninja -C build_local` passes; grep confirms zero remaining per-`.c` definitions
  - 2026-06-05: deleted 7x `MS_PER_SECOND_F` + 4x `HALF_F` + 2x `FULL_SPAN_MULT_F` defs; grep `(define|const float) NAME = val` in src = 0. Commit `1447b37`.

## 2. Lane-digit helper

- [x] 2.1 Add `lane_id_str(char out[2], int lane_id)` inline helper to `controller_shared.h`
- [x] 2.2 Replace the lane-digit initializers with the helper (`toolchange.c` 7, `cutter.c` 1, `main.c` 1, `motion.c` 1 = 10 sites)
- [x] 2.3 `ninja -C build_local` passes
  - 2026-06-05: perl-replaced 10 sites (indent + expr preserved); 0 old idioms left. Commit `1447b37`.

## 3. Document naming convention

- [x] 3.1 Add the global-naming convention to `STYLE.md` (`UPPER_CASE` = mutable tunable, `g_` = internal state, `CONF_*` = compile-time default)
  - 2026-06-05: added "Global Naming Categories" to STYLE.md §2 + "Single definition (DRY)" rule to §4.

## 4. Verify

- [x] 4.1 Final `ninja -C build_local` pass; confirm DRY/helper/doc only, zero behavior/protocol/config/tunable change
  - 2026-06-05: final build `no work to do`. No edits to `config.ini`/`config.ini.example`/`tune.h`/`scripts/`; `SETTINGS_VERSION` stays `59u`. Diff is shared header + dup removal + helper + STYLE.md only.
