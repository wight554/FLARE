## 1. Type-P relief-pause re-arm

- [x] 1.1 `sync.c` `sync_tick()` `SYNC_RELIEF_PAUSE` branch (~L2199): add a type-P
  re-arm condition using the **exact** `is_tension_active` type-P expression
  (`g_buf_pos < -0.6f && (g_sync_tension_transitioned || g_vel_norm < -0.1f)`).
- [x] 1.2 Type-P re-arm path SHALL skip `g_buf_pos = buf_target_reserve_mm()` (D2,
  type-D dead-reckon reseed only). Keep `sync_bootstrap_sps()`,
  `sync_set_state(SYNC_ACTIVE)`, `sync_auto_started = true`,
  `sync_tail_assist_active`, `sync_idle_since_ms = 0`,
  `cmd_event("SYNC","AUTO_START")`.
- [x] 1.3 Confirm type-D re-arm path byte-identical (only an added type-P arm).
- [ ] 1.4 Decide (D3/open-Q) whether to also apply the cold-start `any_lane_loaded
  && !both_loaded` guards to relief recovery; add if so.

  2026-06-04: Added `relief_rearm` with the type-D predicate unchanged and the
  type-P arm identical to the cold-start type-P demand expression. The reseed now
  runs only under `BUF_SENSOR_TYPE == 0`; type-P keeps the live ADC position.
  D3 guard parity remains open for rig confirmation and is not included in this
  firmware patch.

## 2. Build

- [x] 2.1 `ninja -C build_local` — clean, no warnings on `sync.c`.
- [x] 2.2 `openspec validate psf-relief-pause-rearm --strict` — passes.

  2026-06-04: `ninja -C build_local` rebuilt `sync.c` and linked cleanly.
  `openspec validate psf-relief-pause-rearm --strict` passed.

## 3. Rig Verification

- [ ] 3.1 **Rig**: drive a feed burst that ends in relief-pause (`ST:3`, buffer
  pinned compression `BP +1.0`, extruder stopped). Then command extruder demand
  (buffer sweeps toward tension) → confirm **auto-start** (`SM:1 ST:1`), feed
  resumes. Reproduces the failing `--poll` log; must now recover.
- [ ] 3.2 **Rig**: from relief-pause with the buffer at a **static home rest**
  (`BP −1.0`, no motion) → confirm it does **NOT** auto-restart (D18/D3 preserved).
- [ ] 3.3 **Rig**: confirm no relief-pause ↔ active oscillation at the threshold
  (predicate agrees with the cold-start gate, D1).
- [ ] 3.4 **Regression**: type-D relief recovery unchanged (re-arm on TENSION /
  NEUTRAL-during-FEED as before).

  2026-06-04: Rig validation remains unchecked. Needs type-P run showing
  relief-pause re-arms under demand and does not restart at static home rest,
  plus type-D regression confirmation if a relay board is attached.

## 4. Closeout

- [x] 4.1 Commit (one milestone): type-P relief-pause re-arm. Per AGENTS.md.

  2026-06-04: Firmware gate, OpenSpec artifacts, and validation notes are
  committed as one milestone. D3 and rig tasks remain open.
