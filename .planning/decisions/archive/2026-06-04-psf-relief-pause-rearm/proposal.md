## Why

Type-P sync never recovers from `SYNC_RELIEF_PAUSE`. Once the buffer relief-pauses
(the normal end of a feed burst: extruder stops → buffer fills to the compression
rail → sustained pin → `ST:3`), a subsequent real extruder demand that yanks the
buffer to the tension rail does **not** restart sync. The buffer starves at
`BP −1.0` with `SM:0` indefinitely until a manual `BS`/`ST`.

Root cause (`sync.c:2199`): inside the `SYNC_RELIEF_PAUSE` branch of `sync_tick()`,
the proactive re-arm is gated `BUF_SENSOR_TYPE == 0` (type-D only). For type-P the
branch falls through to `return`, so `sync_tick` never reaches the normal
auto-start block at L2244. Type-P has a relief-pause entry path (D14 Layer-3
saturation catch) but no exit path.

Observed on rig: from idle (`ST:0`) auto-start works (buffer pulled to tension →
`SM:1 ST:1`). After a burst ends in relief-pause (`ST:3`), an identical tension
sweep produces no auto-start — the only difference is the latched relief-pause
state. This is why auto-sync "sometimes" fails: it fails specifically after a
prior relief-pause.

## What Changes

- Extend the proactive RELIEF_PAUSE re-arm (`sync.c:2199`) to type-P, reusing the
  same demand predicate as the normal type-P auto-start (`is_tension_active`,
  L2241): `g_buf_pos < -0.6f && (g_sync_tension_transitioned || g_vel_norm < -0.1f)`.
- Preserve the D18 idle/home guard: a static rest at the home rail (`g_buf_pos`
  ≈ `−1.0`, `vel ~ 0`, no fresh tension transition) MUST stay gated so a
  relief-paused buffer sitting at home does not busy-restart. The velocity term
  provides this — re-arm only fires when the buffer is *actively falling* toward
  tension (real demand), not at dead rest.
- Type-P re-arm SHALL NOT reseed `g_buf_pos` from `buf_target_reserve_mm()` (that
  is the type-D dead-reckon model seed; type-P measures position directly). It
  reuses the rest of the type-D re-arm sequence: `sync_bootstrap_sps()`,
  `sync_set_state(SYNC_ACTIVE)`, `sync_auto_started = true`,
  `sync_tail_assist_active` from lane sensors, `sync_idle_since_ms = 0`,
  `cmd_event("SYNC", "AUTO_START")`.
- No change to type-D re-arm behavior.

## Capabilities

### Modified Capabilities
- `psf-type-p-sensor`: type-P sync auto-start recovers from `SYNC_RELIEF_PAUSE`
  under genuine extruder demand, using the same demand discriminator as cold
  auto-start (D18), so a relief-pause is no longer a terminal state requiring
  manual intervention.

## Impact

- `firmware/src/sync.c`: `sync_tick()` `SYNC_RELIEF_PAUSE` branch (~L2199) — add a
  type-P re-arm condition mirroring `is_tension_active`; skip the type-D-only
  `g_buf_pos` reseed.
- Rig: from a relief-paused state (`ST:3`), command extruder demand → confirm
  auto-start (`SM:1 ST:1`); confirm a dead rest at home does NOT auto-restart.
- No NVM, protocol, or host changes. Independent of `psf-soft-wall-owns-compression`.
