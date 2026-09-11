## Why

Two leftovers from the 2026-09-11 review (`phases/12-*/12-REVIEW.md`) that are
real but not regressions:

1. Adding one persisted setting touches four parallel lists in
   `settings_store.c` — `settings_tag_t`, the `tlv_emit_*` calls in
   `settings_save()`, the 64-case `settings_load_tlv_tag()` switch, and
   `settings_load_v63()` (Shotgun Surgery). `test_settings_parity.py` catches
   omissions, but a tag→(ptr, size, clamp) table would collapse save + load to
   one loop and delete the switch.
2. `cut_feed_timeout_ms` / `cut_settle_timeout_ms` are Tier-2 per
   tier-config-surface but remain `FLARE_INT_*` build constants — no
   `SET:`/`GET:`/persistence surface (pre-dates Phase 6).

## What Changes

- Table-driven TLV schema in `settings_store.c`; parity test re-pointed at the
  table (keeps the 64/64 tag guarantee).
- Promote the two cutter timeouts to persisted tunables with full rule-8
  surface (SET/GET/--dump/docs); `SETTINGS_VERSION` 65.

## Out of Scope

- Pre-existing magic-number lint in `settings_store.c` clamps (noise, dropped).
