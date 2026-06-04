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

- [ ] 3.1 **BLOCKER: requires PSF rig** — Overfeed into the compression zone at
  slow / print-band / fast feed rates. Confirm `ST`/`BP` telemetry shows a
  gradual soft-wall blend, NOT a slam to `compression_floor_sps` (682 sps).
- [ ] 3.2 **BLOCKER: requires PSF rig** — Transient overfeed dip → confirm no
  latched feed-starve at 1.67 mm/s through the drain back to neutral (no hunting).
- [ ] 3.3 **BLOCKER: requires PSF rig** — If the soft wall under-brakes (buffer
  rides deep into compression), tune `PSF_SOFT_WALL_START` / `KD_PSF` — do NOT
  re-enable the cap. Record the chosen values.
- [ ] 3.4 **Regression** — type-D relay compression drain unchanged (re-run the
  relay steady-state compression check).

  2026-06-04: Hardware validation remains unchecked. Requires PSF rig telemetry
  and explicit user-reported results before completion.

## 4. Closeout

- [x] 4.1 Resolve the psf-analog-rig "compression_recovery / soft-wall overlap"
  open question (design D7) — reference this change as the resolution.
- [x] 4.2 Commit (one milestone): firmware gate. Per AGENTS.md one-milestone-per-commit.

  2026-06-04: `psf-analog-rig` design records soft-wall ownership as the D7
  resolution and references this change. Firmware gate, OpenSpec artifacts, and
  validation notes are committed as one milestone.
