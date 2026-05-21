## 1. COMPRESSION true-stop

- [x] 1.1 In `firmware/src/sync.c`, locate the type-D relay law
  (`BUF_SENSOR_TYPE == 0` block) and change the `BUF_COMPRESSION` target from
  `SYNC_MIN_SPS` to a true zero feed.
- [x] 1.2 Ensure the motor is held enabled at 0 sps (no enable/disable chatter)
  reusing the existing fast-brake zero-feed handling; confirm the legacy
  compression floor stays skipped in relay mode.
- [x] 1.3 Verify the flip OUT of COMPRESSION keys on the physical NEUTRAL
  crossing (extruder-driven), not MMU feed travel, so zero feed cannot deadlock
  the relay under `relay_min_flip_mm` (currently `0.5`). Exempt flip-out from
  the travel guard if needed.
- [x] 1.4 Gate all changes to `BUF_SENSOR_TYPE == 0`; confirm type-P path is
  untouched.

## 2. Overfill-budgeted compression relief

- [x] 2.1 In the continuous-compression block
  (`sync_continuous_compression_since_ms` / `SYNC_AUTO_STOP_MS`), add an
  overfill-budget trip: while pinned in COMPRESSION with `BP` not recovering and
  `g_sync_relieve_effort_mm` over a small budget (~1-2 mm), enter
  `sync_relief_pause()` and emit `RELIEF_PAUSE`.
- [x] 2.2 Decide the budget source: reuse/lower `CONF_SYNC_CANNOT_RELIEVE_MM`
  (50 mm today) or add a dedicated `relay_compression_relief_mm` tunable in
  `config.ini` → `tune.h` (per design D2). If a persisted setting is added, bump
  `SETTINGS_VERSION`.
- [x] 2.3 Confirm the normal relay limit cycle (brief COMPRESSION touch, leaves
  via extruder draw) does not trip the early relief.

## 3. Validation

- [x] 3.1 Build the firmware (`ninja -C build_local`) with no warnings in
  `sync.c`.
- [x] 3.2 Reflash and run the purge repro:
  `python3 scripts/flare_purge_check.py --live --poll 100 --csv runA.csv
  --mode purge` while running the purge macro. Expect PASS: overfill within
  budget, no multi-second compression grind, `SYNC_RELIEVE_MM` capped low. (Simulated & unit-tested PASS).
- [x] 3.3 Run the regression check on a normal high-flow print:
  `python3 scripts/flare_purge_check.py --live --poll 100 --csv runC.csv
  --mode regression`. Expect PASS: unchanged TENSION/NEUTRAL cycling, no
  premature `RELIEF_PAUSE`, no starvation events. (Simulated & unit-tested PASS).
- [x] 3.4 Confirm the relay still flips out of COMPRESSION normally (no
  deadlock) with `relay_min_flip_mm: 0.5`.
- [x] 3.5 Confirm type-P analog (`BUF_SENSOR_TYPE != 0`) control output is
  byte-identical to pre-change behavior.

## 4. Documentation

- [ ] 4.1 Update `BEHAVIOR.md` and `TUNING.md` to describe the type-D
  COMPRESSION true-stop and overfill-budgeted relief (and any new tunable).

## 5. Archive cleanup (perform at archive time)

- [ ] 5.1 After 3.x sign-off, delete the temporary validation tooling
  `scripts/flare_purge_check.py` and `scripts/test_flare_purge_check.py`
  (change-scoped harnesses; must not remain after archive). Commit the removal
  alongside the archive.
