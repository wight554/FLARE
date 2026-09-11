## 1. Gate the recovery-cap consumers to type-D

- [x] 1.1 `sync.c` `sync_tick()`: wrap the `sync_compression_recovery_active`
  feed-cap block (~L2470) in `BUF_SENSOR_TYPE == 0`. Type-D behavior byte-identical.
- [x] 1.2 `sync.c` `sync_tick()`: wrap the continuous-compression collapse block
  (~L2582) in `BUF_SENSOR_TYPE == 0` (same mechanism).
- [x] 1.3 Confirm the latch set at L1916 and its type-agnostic consumers
  (`baseline_update_on_settle` suppression L1934, post-compression boost) are
  untouched.

  2026-06-04: Confirmed only the two `sync_tick()` consumer conditions changed.
  `sync_on_transition()` still sets `sync_compression_recovery_active` for both
  sensor types; settle suppression and post-compression boost remain unchanged.

## 2. Build

- [x] 2.1 `ninja -C build_local` — clean, no warnings on `sync.c`.
- [x] 2.2 `openspec validate psf-soft-wall-owns-compression --strict` — passes.

  2026-06-04: `ninja -C build_local` rebuilt `sync.c` and linked cleanly.
  `openspec validate psf-soft-wall-owns-compression --strict` passed.

## 3. Rig Verification (hardware-blocked)

- [x] 3.1 **PSF rig** — Overfeed into the compression zone at slow / print-band /
  fast feed rates. Confirm `ST`/`BP` telemetry shows a gradual soft-wall blend,
  NOT a slam to `compression_floor_sps` (682 sps).

  2026-06-04 `flare_cmd.py --poll 100` trace (type-P, active sync `ST:1`):
  compression-side rows tracked demand, e.g. `BP +0.48 EST 1348`, `BP +0.59 EST
  1166`, `BP +0.55 EST 1195` — feed ≈ 1100-1500, far above the old 682 floor. The
  buffer held compression-side and oscillated (0.1↔0.59) instead of draining
  straight to tension, proving feed ≈ demand (cap OFF). On extruder stop
  (`MM 608→100`) feed decayed smoothly `EST 400→200→124→107` to the floor and
  the buffer pinned `BP +1.0` — wall-clock decay + soft wall, NOT a 682 snap.
  PASS.

- [x] 3.2 **PSF rig** — Transient overfeed dip → confirm no latched feed-starve at
  1.67 mm/s through the drain back to neutral (no hunting).

  2026-06-04: same trace — repeated compression excursions recovered toward
  neutral with feed tracking demand; no row latched at the 682 floor mid-sync.
  Relief-pause (`ST:3`) only on genuine sustained pin with the extruder stopped,
  not on transient dips. PASS.

- [ ] 3.3 **N/A this session** — soft wall did NOT under-brake (buffer stayed in
  range, decayed smoothly to the rail only when the extruder genuinely stopped).
  No `PSF_SOFT_WALL_START` / `KD_PSF` retune needed. Revisit only if aggressive
  overfeed shows the wall biting too late.

- [x] 3.4 **Regression** — type-D relay compression drain unchanged. Closed by
  construction: commit `e3b20b7` only prepends `BUF_SENSOR_TYPE == 0 &&` to the two
  conditions (L2470, L2582); inner blocks byte-identical, so `&&` short-circuits
  to the original expression for type-D. No type-D board attached (detached for
  the PSF rig); a live `flare_sync_check` run is optional confirmation if reattached.

## 4. Closeout

- [x] 4.1 Resolve the psf-analog-rig "compression_recovery / soft-wall overlap"
  open question (design D7) — reference this change as the resolution.
- [x] 4.2 Commit (one milestone): firmware gate. Per AGENTS.md one-milestone-per-commit.

  2026-06-04: `psf-analog-rig` design records soft-wall ownership as the D7
  resolution and references this change. Firmware gate, OpenSpec artifacts, and
  validation notes are committed as one milestone.
