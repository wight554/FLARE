## 1. Tension refill snap

- [x] 1.1 `sync.c` type-P feed apply: add a branch before the distance-smoothing
  one — `buf_pos_norm() < -CONF_PSF_SOFT_WALL_START && target_sps >
  sync_current_sps` → `sync_current_sps = target_sps` (snap to wall max). (`fb490ad`)
- [x] 1.2 Seed `g_psf_target_filt = extruder_est_sps` (demand) in the snap so feed
  eases to the extruder rate on wall exit instead of overshooting compression.
  (`9a17441`)
- [x] 1.3 Confirm compression/neutral path + type-D unchanged (snap is `else if`
  ahead of the existing type-P smoothing branch; both gated `BUF_SENSOR_TYPE == 1`).

## 2. Build

- [x] 2.1 `ninja -C build_local` — clean, links.
- [x] 2.2 `openspec validate psf-tension-refill-snap --strict` — passes.

## 3. Rig Verification

- [x] 3.1 **Rig**: `M83; G1 E40 F1500; G4 P7000; G1 E40 F1500`. PASS.

  2026-06-04: pre-fix every burst → `cannot_refill` + buffer slammed `BS:TENSION
  −1.0`. After the snap (`fb490ad`): `cannot_refill` gone but compression overshoot
  to `+0.71`. After the demand-seed (`9a17441`): overshoot gone — mid-burst buffer
  rides `0.0 … +0.47` (goal band), no `cannot_refill`. End-of-burst compression pin
  → `RELIEF_PAUSE` is the expected auto-stop (extruder fully stopped by the dwell).

- [ ] 3.2 **Rig (optional)**: re-check at higher F (e.g. F2400) — confirm no
  under-shoot back to tension; if so, seed `extruder_est × 1.2`.
- [ ] 3.3 **Rig (optional)**: confirm the startup tension dip (~−0.68) stays benign
  across filaments / bowden lengths; lower `PSF_SOFT_WALL_START` if it reaches
  `cannot_refill` on any rig.

## 4. Closeout

- [x] 4.1 Commit(s): `fb490ad` (snap), `9a17441` (demand seed).
