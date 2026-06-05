## 1. Shared constants header

- [ ] 1.1 Add `firmware/include/firmware_constants.h` defining `MS_PER_SECOND_F`, `HALF_F`, `FULL_SPAN_MULT_F` once (`#define`, with `#pragma once`)
- [ ] 1.2 Include `firmware_constants.h` from `controller_shared.h` so all TUs see it
- [ ] 1.3 Remove the per-`.c` duplicate definitions (`cutter.c`, `main.c`, `motion.c`, `protocol.c`, `settings_store.c`, `sync.c`, `sync_buf.c`, `sync_analog.c`, `sync_relay.c`)
- [ ] 1.4 `ninja -C build_local` passes; grep confirms zero remaining per-`.c` definitions

## 2. Lane-digit helper

- [ ] 2.1 Add `lane_id_str(char out[2], int lane_id)` inline helper to `controller_shared.h`
- [ ] 2.2 Replace the 11 `{(char)('0' + id), 0}` lane-digit initializers with the helper (`toolchange.c`, `cutter.c`, `main.c`, `motion.c`)
- [ ] 2.3 `ninja -C build_local` passes

## 3. Document naming convention

- [ ] 3.1 Add the global-naming convention to `STYLE.md` (`UPPER_CASE` = mutable tunable, `g_` = internal state, `CONF_*` = compile-time default)

## 4. Verify

- [ ] 4.1 Final `ninja -C build_local` pass; confirm DRY/helper/doc only, zero behavior/protocol/config/tunable change
