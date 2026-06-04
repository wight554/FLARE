## Context

`sync_tick()` handles a paused machine before the auto-start logic:

```c
} else if (g_sync_state == SYNC_RETRACT_ASSIST || g_sync_state == SYNC_RELIEF_PAUSE) {
    if (g_sync_state == SYNC_RETRACT_ASSIST) { ...; return; }
    else {  // SYNC_RELIEF_PAUSE
        buf_state_t s = g_buf.state;
        if (BUF_SENSOR_TYPE == 0 &&
            (s == BUF_TENSION || (s == BUF_NEUTRAL && A->task == TASK_FEED))) {
            g_buf_pos = buf_target_reserve_mm();        // type-D dead-reckon reseed
            sync_current_sps = sync_bootstrap_sps();
            sync_set_state(SYNC_ACTIVE);
            sync_auto_started = true;
            sync_tail_assist_active = !lane_in_present(A) && lane_out_present(A);
            sync_idle_since_ms = 0;
            cmd_event("SYNC", "AUTO_START");
        } else {
            return;                                     // type-P always lands here
        }
    }
}
```

The normal auto-start lower down (L2241) already has the correct type-P demand
discriminator:

```c
bool is_tension_active = (BUF_SENSOR_TYPE == 1)
    ? ((g_buf_pos < -0.6f) && (g_sync_tension_transitioned || g_vel_norm < -0.1f))
    : (s == BUF_TENSION);
```

The relief-pause branch just never gets to use it for type-P, because it `return`s
first.

## Goals / Non-Goals

**Goals:**
- Type-P recovers from `SYNC_RELIEF_PAUSE` on genuine demand, no manual `BS`/`ST`.
- Reuse the D18 demand discriminator verbatim — one source of truth for "is the
  extruder actually pulling," shared between cold auto-start and relief recovery.

**Non-Goals:**
- Changing type-D relief recovery.
- Changing when type-P *enters* relief-pause (D14 Layer-3 catch unchanged).
- Touching `SYNC_RETRACT_ASSIST` (BL path), unchanged.

## Decisions

### D1 — Reuse `is_tension_active`, do not invent a second predicate

The re-arm gate and the cold-start gate must agree, or the buffer can oscillate
between relief-pause and active (re-arm fires on a signal the main loop then
treats as idle, relief-pauses again). Both use
`g_buf_pos < -0.6f && (g_sync_tension_transitioned || g_vel_norm < -0.1f)`.

Implementation: hoist the predicate or duplicate the exact expression in the
relief branch. Hoisting is cleaner but the relief branch runs before `s`/`A`
guards used by the main block — a local duplicate of the *type-P* arm is the
minimal, safe edit. Final form left to implementation; the **expression must be
identical** to L2241's type-P arm.

### D2 — Skip the `g_buf_pos` reseed for type-P

`g_buf_pos = buf_target_reserve_mm()` reseeds the type-D dead-reckoned model to a
fictional reserve target after the hold (the model drifts during a feed=0 hold).
Type-P reads `g_buf_pos` from the live ADC every tick — reseeding it would stomp a
real measurement with a stale target. Type-P keeps its measured position; only
`sync_current_sps = sync_bootstrap_sps()` (feed bootstrap) carries over.

### D3 — Idle/home rest stays gated (D18 preserved)

A relief-paused buffer that simply rests at the home tension rail
(`g_buf_pos ≈ −1.0`, `g_vel_norm ≈ 0`) and has no fresh transition armed
(`g_sync_tension_transitioned == false`) MUST NOT re-arm — otherwise relief-pause
becomes a no-op and the gear is driven against a static home. The velocity term
`g_vel_norm < -0.1f` is the discriminator: re-arm only when the buffer is
*actively falling* toward tension (extruder pulling), as in the rig log where the
buffer swept `−0.07 → −1.0` under real demand.

## Risks / Trade-offs

- **Re-arm / relief-pause oscillation** if the predicate disagrees with the main
  loop → mitigated by D1 (identical expression). Rig-verify no flapping at the
  threshold.
- **Spurious re-arm from ADC noise on `g_vel_norm`** near the `−0.1` threshold →
  `g_vel_norm` is already the filtered derivative (`PSF_VEL_ALPHA`); same signal
  the cold start trusts. If noisy, raise the magnitude, do not special-case.

## Open Questions

- Should relief recovery also require `any_lane_loaded && !both_loaded` like cold
  auto-start (L2244)? Probably yes for symmetry — a relief-paused buffer with no
  loaded lane should not re-arm. Confirm on rig; fold the same guards in if the
  cold-start guards prove necessary.
