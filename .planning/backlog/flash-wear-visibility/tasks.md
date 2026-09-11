## 1. Implement

- [x] 1.1 `g_flash_erase_count` global (`main.c` definition,
      `controller_shared.h` extern).
- [x] 1.2 `settings_t.flash_erase_count` field, `SETTINGS_VERSION` bump
      (60 -> 61), `FLASH_WEAR_WARN_THRESHOLD` constant (80000).
- [x] 1.3 `settings_save()`: increment before erase+program, persist,
      one-time `EV:FLASH:WEAR_WARNING`.
- [x] 1.4 `settings_defaults_motion()`: seed to 0. `settings_load_motion()`:
      restore from flash.
- [x] 1.5 `protocol.c`: `GET:FLASH_ERASE_COUNT`.

## 2. Verify

- [x] 2.1 `scripts/test_settings_parity.py` passes (new field
      saved+loaded+defaulted, no write-only/loaded-not-defaulted gap).
- [x] 2.2 Clean `build_sim` rebuild (warning-free), 37/37
      `scripts.test_sync_sim` green.
- [x] 2.3 Clean `build_local` (dev-tuning superset) rebuild — real ARM
      firmware, no warnings.
- [x] 2.4 Full `python3 scripts/validate_regression.py` gate green
      end-to-end (also fixed two unrelated pre-existing gaps blocking it:
      ruff F841 in `flare_daemon.py`, stale mock-MMU self-test assertions
      from the `94e27a2` tip-animation removal — see
      `memories/repo/host-sync-sim.md`).
- [ ] 2.5 Hardware validation: flash several settings saves on a real
      board, confirm `GET:FLASH_ERASE_COUNT` increments and survives a
      reboot; confirm `RS:` resets it to 1 (0 seeded + immediate save).
      HW-gated, not run this session.

## 3. Record

- [x] 3.1 `MANUAL.md`: documented `GET:FLASH_ERASE_COUNT` and
      `EV:FLASH:WEAR_WARNING`.
- [x] 3.2 `memories/repo/host-sync-sim.md` / this change's own record.
