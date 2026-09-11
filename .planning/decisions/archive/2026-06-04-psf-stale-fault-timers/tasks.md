## 1. Timer resets

- [x] 1.1 `sync.c`: reset `g_buf_analog_saturated_since_ms = 0` in the
  `FAULT_HOLD_RECOVERY` block before re-entering `SYNC_ACTIVE`. (`17e4152`)
- [x] 1.2 `sync.c`: restart `sync_tension_pin_since_ms = (g_buf.state ==
  BUF_TENSION) ? now_ms : 0` at the three type-P activation sites — normal
  auto-start, relief-pause re-arm, fault-hold recovery. (`f6af580`)
- [x] 1.3 Confirm type-D paths untouched (type-D relief-rearm in
  `sync_on_transition` left alone; resets are in type-P-reachable `sync_tick`
  blocks).

## 2. Build

- [x] 2.1 `ninja -C build_local` — clean, links.
- [x] 2.2 `openspec validate psf-stale-fault-timers --strict` — passes.

## 3. Rig Verification

- [x] 3.1 **Rig**: `M83; G1 E150 F1200` from idle (buffer resting in the control
  tension zone). PASS — `SYNC:AUTO_START`, buffer dips to `BP −0.84` and recovers
  via the refill snap, no `FAULT_HOLD`, no 5 s stall, ends at goal. Pre-fix it
  faulted instantly at `−0.27` then deadlocked `FAULT_HOLD ↔ RECOVERY`.

- [ ] 3.2 **Rig (follow-up, quality)**: residual wide hunting (`−0.84 ↔ +0.79`) and
  end-of-burst compression overshoot to `+1.0 → RELIEF_PAUSE` — feed-tracking
  quality, not a fault. Decide whether to moderate the snap target / enable a small
  `KD_PSF`. Tracked here as a follow-up, not blocking the fault fix.

## 4. Closeout

- [x] 4.1 Commits: `17e4152` (saturation timer / deadlock), `f6af580`
  (tension-dwell restart).
