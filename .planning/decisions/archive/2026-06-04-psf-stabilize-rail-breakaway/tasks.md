## 1. Tunable params

- [x] 1.1 `tune.h`: add `CONF_PSF_STAB_STAGNANT_MS=200`,
  `CONF_PSF_STAB_STAGNANT_NORM=0.03f`, `CONF_PSF_STAB_RAIL_BREAK_MS=1500`.
- [x] 1.2 `main.c`: define `int PSF_STAB_STAGNANT_MS`, `float PSF_STAB_STAGNANT_NORM`,
  `int PSF_STAB_RAIL_BREAK_MS` (init from CONF_*).
- [x] 1.3 `controller_shared.h`: externs.
- [x] 1.4 `protocol.c`: GET/SET handlers (mirror `SYNC_PSF_FILTER_MM`; clamp ms ≥ 0,
  norm to [0,1]). No NVM persist.

  2026-06-04: Added generator defaults and generated local `firmware/include/tune.h`
  macros; `tune.h` remains gitignored. Runtime variables/external declarations
  are wired in `main.c` and `controller_shared.h`. `SET:` clamps ms values to
  >= 0 and norm to [0,1]; `GET:` returns all three. No `settings_t` changes.
  `flare_cmd.py --dump`, `config.ini.example`, and docs were updated for parity.

## 2. Rail-breakaway stagnant logic

- [x] 2.1 `sync.c` `buffer_stabilize_tick()` (~L906): while
  `g_buf_analog_saturated_since_ms != 0`, skip the norm/window abort and
  re-baseline `g_boot_stabilize_start_pos = g_buf_pos`; abort only if
  `now_ms - g_boot_stabilize_started_ms >= PSF_STAB_RAIL_BREAK_MS`.
- [x] 2.2 Do NOT reset `g_boot_stabilize_started_ms` in the saturated branch (D2).
- [x] 2.3 Desaturated path uses `PSF_STAB_STAGNANT_MS` / `PSF_STAB_STAGNANT_NORM`
  (replaces the `200` / `0.03f` literals), behavior otherwise unchanged.
- [x] 2.4 Confirm type-D path untouched (block already `BUF_SENSOR_TYPE == 1`).

  2026-06-04: Replaced the flat type-P stagnant guard with a saturated-rail
  branch that re-baselines only `g_boot_stabilize_start_pos`, never
  `g_boot_stabilize_started_ms`. The desaturated path uses the new live knobs.
  Type-D does not enter the block.

  2026-06-04 fix-forward: Rig showed breakaway works, but desaturation could
  false-fire the dry-spin check because the off-rail window still used
  `g_boot_stabilize_started_ms`. Added `g_stab_stagnant_since_ms`, initialized at
  stabilize start and refreshed while saturated, so the stagnant window starts
  at desaturation while the rail-break cap still measures from start.

  2026-06-04 fix-forward (D2c): boot-stab still false-aborted because
  `g_buf_analog_saturated_since_ms` is armed only in `buf_sensor_tick()`, which at
  boot runs after the debounce loop and after `buffer_stabilize_tick()` in the
  main loop — so the flag was 0 on boot-stab's first ticks. Rail test is now
  `saturated_flag || fabsf(g_buf_pos) >= 0.99f`. `boot_stabilize_start` flipped to
  `emit_events=true` for boot observability. `ninja` clean, links.

## 3. Build

- [x] 3.1 `ninja -C build_local` — clean, no warnings on `sync.c`.
- [x] 3.2 `openspec validate psf-stabilize-rail-breakaway --strict` — passes.

  2026-06-04: `python3 -m py_compile scripts/*.py` passed for script edits.
  `ninja -C build_local` regenerated `tune.h`, rebuilt affected firmware, and
  linked cleanly. `openspec validate psf-stabilize-rail-breakaway --strict`
  passed.

  2026-06-04 fix-forward: `ninja -C build_local` rebuilt `sync.c` and linked
  cleanly. `openspec validate psf-stabilize-rail-breakaway --strict` passed.

## 4. Rig Verification

- [x] 4.1 **Rig**: loaded buffer at `BP −1.0` (saturated) → `BS` drives off the
  rail to goal `≈ +0.40`, emits `BUF_STAB:DONE`. No `STAGNANT_TIMEOUT`. PASS.

  2026-06-04: required two fix-forwards beyond the original change — the
  desaturated stagnant window needed its own timer (`g_stab_stagnant_since_ms`,
  commit on `psf-stabilize-rail-breakaway`) and rail detection by position
  (`9afa3fb`). `BS` from cold `−1.0` now drives to `+0.40`, `CF 0.50→1.0`.

- [x] 4.2 **Rig**: boot with loaded buffer at the rail → boot-stab drives to goal.
  PASS via deferred + retried boot trigger.

  2026-06-04 (final): root cause = the boot-time trigger at ~750ms ran before the
  motor/TMC + buffer signal had settled, so the first drive was sluggish and hit
  the rail-break cap (`STAGNANT`). Fix: one-shot deferred boot stabilize at
  `BOOT_STAB_FIRST_MS = 2500` (`34f3a30`); `boot_stabilize_start` emits events.
  Single `BUF_STAB:START → DONE`, `BP +0.40`, holds. No retry — a STAGNANT would
  mean the delay needs raising, kept honest. (Interim retry approach `62e82ad`/
  `0e32360`/`91038b2`/`8741893` superseded; `boot_stabilize_settled()` left as an
  unused accessor.) Position-based rail detection (`9afa3fb`) + desaturated-window
  timer remain required for the cold `−1.0` breakaway.
- [ ] 4.3 **Rig**: measure breakaway distance (mm fed before `g_buf_pos` leaves
  `−1.0`); set `PSF_STAB_RAIL_BREAK_MS` ≥ that + margin; record value.
- [ ] 4.4 **Rig**: uncoupled / jammed buffer (won't track) → aborts at the cap
  with `STAGNANT_TIMEOUT`, NOT a 10 s grind, NOT a dry-spin.
- [ ] 4.5 **Rig**: off-rail stagnation (buffer mid-range, motor moves, buffer
  doesn't track) → fast abort at `PSF_STAB_STAGNANT_MS` (dry-spin guard intact).

  2026-06-04: Rig validation remains unchecked. Needs PSF rig evidence for
  breakaway success, boot behavior, rail-break timing, jam cap, and off-rail
  stagnant abort.

## 5. Closeout

- [ ] 5.1 Bake winning `PSF_STAB_RAIL_BREAK_MS` default into `tune.h`.
- [x] 5.2 Commit (one milestone): type-P stabilize rail breakaway. Per AGENTS.md.

  2026-06-04: Seed default is 1500 ms for first rig run. Task 5.1 remains open
  until measured breakaway distance confirms or changes the default. Firmware,
  protocol, docs, dump surface, and OpenSpec artifacts are committed as one
  milestone.
