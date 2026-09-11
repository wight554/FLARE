## 1. Real-print evidence (gates everything)

- [x] 1.1 **Rig**: real print (few minutes) + end pause. PASS — no `FAULT_HOLD` /
  `cannot_refill` the entire print; `SM:1 ST:1` throughout. Buffer hunts
  `≈ −0.5 … +0.6` (compression-biased ~+0.2), feed surges `MM 100↔2000`, but
  **never saturates mid-print** → extruder always fed → flow unaffected (hunting is
  mechanical surge/noise, not a flow defect in this trace). End-of-print: extruder
  stops → buffer rides to `+1.0` as feed decays to floor → `RELIEF_PAUSE` → settles
  to goal `+0.40` (correct Layer-3 behavior; brief, feed already at floor). The
  fault-timer fixes hold under real printing.

  Open: the hunting amplitude is benign for flow here but mechanically active
  (motor surge). Pursue 2.x only if a print *surface* shows buffer-correlated
  artifacts; the cleanest damp is a small `KD_PSF` (2.2).

## 2. Feed hunting / end-burst overshoot

- [x] 2.1 **ACCEPTED — no action.** Multi-pause real print came out clean (operator
  confirmed surface); the compression-biased hunting never saturates mid-print and
  does not affect flow. Snap-moderation not needed.
- [x] 2.2 **ACCEPTED — `KD_PSF` stays 0.** No buffer-driven artifacts on the printed
  surface, so no derivative damping required. Revisit only if a future print shows
  banding tracking the buffer swings.
- [x] 2.3 **CONFIRMED.** End-burst `+1.0` → `RELIEF_PAUSE` occurs only at genuine
  full stops (pauses / end of print), recovers to goal, and re-arms correctly on
  resume across repeated pauses. Not a mid-print issue.

## 3. BS no-op from mid-tension

- [x] 3.1 **Root cause narrowed by audit-hardening-fixes code audit (2026-06-10)** —
  live-recurrence wait no longer needed; bench repro below. Decision tree closed by
  code reading:
  - Every *visible* BS failure replies `ER` (`ER:BUSY` protocol.c:1636/1659,
    `ER:BUF_STAB_UNAVAILABLE` on `false` return protocol.c:1671-1673). The
    "check `L1T/L2T/TC` active task" hypothesis is **ruled out** — task-active
    paths are never silent.
  - No-op with `OK` = `buffer_stabilize_start_internal` returned **true without
    arming**. Exactly two reachable silent-success paths, both BEFORE the
    `BUF_STAB:START` emit (sync.c:245-246, unconditional once armed):
    1. **Type-P presence gate (sync.c:197-207) — prime suspect.**
       `pick_boot_stabilize_lane()` (sync.c:514-516) returns the ACTIVE lane
       unconditionally when set — never falls through to OUT-sensor checks. Active
       lane with empty IN+OUT → `return true` → `OK`, no motion, no START. Matches
       hand-loading incident: filament in flux on the *other* lane, buffer at −0.4,
       gate inspects wrong lane's sensors.
    2. Raw-NEUTRAL skip (sync.c:221-222) — secondary; −0.4 vs compression-side goal
       should classify TENSION, unlikely here.
  - Caveat: `BUF_STAB:START` is budget-limited best-effort (8/100 ms shared window);
    during hand-loading event chatter a real START can drop on the wire — absence of
    START alone is weak evidence, require "no motion" too. (audit F6 fixes fault-class
    events only; START stays droppable.)
- [x] 3.2 **Bench repro CONFIRMED (2026-06-11, type-P rig).** Presence gate convicted:
  lane 1 loaded (`I1:1,O1:1`), lane 2 empty, buffer parked at `BP −0.58` (SM:0),
  `T:2` then `BS` → `OK`, BP frozen at −0.58, `TF` unchanged, no gear motion.
  Hypothesis 1 (blind active-lane pick) confirmed; hypothesis 2 (raw-NEUTRAL skip)
  excluded by tension-side start. Control case: same BP band with loaded active
  lane → BS arms and parks at goal `+0.40`. Bonus empirical confirmation of the
  3.1 caveat: a *working* BS produced zero `BUF_STAB:*` entries in daemon event
  history — event absence is no evidence; motion/BP trace is the only reliable
  channel.
- [x] 3.3 **Fix (shape decided 2026-06-11):** `pick_boot_stabilize_lane` falls
  through to the filament-bearing lane (IN or OUT present) when the active lane
  is empty, so hand-load BS does a real stabilize instead of silent `OK`.
  (Loud-refusal `ER:BUF_STAB_UNAVAILABLE` alternative rejected — operator intent
  during hand-load is motion; presence gate at sync.c:197-207 already requires
  filament on the picked lane, so the fall-through closes the silent path.)
- [x] 3.4 **NEW — stagnant guard false-positive aborts BS from deep tension.**
  Found during 3.2 bench session: loaded lane, `BP −0.57`, `BS` → motor stops
  ~0.2–0.5 s in, buffer stranded at ≈ −0.49; second `BS` completes to `+0.40`
  (looks like "2-step recovery"). 100 ms BP polling shows motion only in the
  first ~0.5 s then 8 s flat — rail-break (1.5 s) and window deadline (10 s)
  ruled out. Cause: single stagnant check (sync.c:291-299) fires 200 ms after
  start requiring ≥0.03 norm displacement (`CONF_PSF_STAB_STAGNANT_MS 200`,
  `CONF_PSF_STAB_STAGNANT_NORM 0.03`, tune.h:61-62); deep-tension spring drag +
  motor spin-up + ~80 ms EMA lag leave the lagged reading at/under threshold →
  spurious `STAGNANT_TIMEOUT`. From −0.41 it passes (control run) — deeper start
  = slower first 200 ms = flaky abort. Same guard is also blind late: window
  never re-arms, so a genuine stall after the first check goes undetected.
  **Fix decided:** raise window 200 → ~600 ms AND re-anchor
  `g_boot_stabilize_start_pos` + `g_stab_stagnant_since_ms` on each passing
  check (rolling stagnation window) — fixes both early false-positive and
  late-stall blindness. Repro: `SM:0`, park buffer ≤ −0.55 via `MV` nudges,
  `BS`, poll BP at 100 ms.
- [x] 3.5 **NEW — MV does not cancel in-flight stabilize (dual-controller race).**
  Found during 3.3/3.4 bench re-verify (2026-06-11): MV issued while a
  relief/stabilize is running leaves BOTH controllers driving the same motor —
  `cmd_handle_mv_command` (protocol.c:1396-1437) disables sync but never calls
  `buffer_stabilize_cancel()` (BL handler does, protocol.c:1641). Stabilize
  predict-reach can `motor_stop` a live MV; an MV can stomp the compression
  relief mid-flight, stranding the buffer at a rail (observed: second `MV:-12`
  during relief → parked at −1.00, no recovery until manual `BS`).
  **Fix:** add `buffer_stabilize_cancel()` to the MV handler before
  `lane_start`. Out of scope by decision: type-P MOVE edge guard
  (motion.c:499-509 is type-D only; MV stays unguarded for raw recovery per
  protocol.c:1592 comment) and tension-rail auto-recover (idle-at-tension is a
  legal rest state; `BS` recovers one-shot — verified from −1.00 → +0.42).
  Bench check after fix: start relief (MV to compression rail, wait for
  recovery motion), fire `MV:-2` mid-recovery → relief must stop cleanly, MV
  runs alone, no rail stranding without an explicit operator move.

- [x] 3.6 **NEW — rail-break deadline races sensor-flat pull-off from deep rail.**
  Found post-3.5 (2026-06-11 evening bench): MV chain drives piston PAST the
  sensing range (BP clamps −1.00, real position deeper); `BS` recovery then has
  ~1.4 s of sensor-flat driving before BP unclamps (rewind several mm at stab
  speed ≈2 mm/s). `psf_stab_rail_break_ms` default 1500 races that dead time:
  100 ms traces show run 1 abort at the 1.5 s deadline → stranded at −1.00
  (second BS recovers), run 2 motion surfaced at ~1.46 s → one-shot park +0.40
  by 2.9 s. Coin-flip at default — explains intermittent "BS dead at rail".
  Knob is live-tunable (`SET:PSF_STAB_RAIL_BREAK_MS`). **Fix:** bump default
  1500 → 3000 (config.ini.example + gen_config.py + tune.h regen).
  **Live confirm (same evening):** `SET:...:3000`, re-strand deep via MV chain →
  dead time ~2.1 s this run (would abort at 1500), motion at 2.18 s, one-shot
  park +0.40 by 3.6 s. Dead time scales with strand depth → 3000 = headroom,
  hard stop bounds worst case. Default bump remains to land (SET is runtime
  only, not persisted). Tonight's MV-during-relief chains also serve as the 3.5
  bench check: clean takeovers, no dual-controller fights. Companion
  observations, no action: MV chains can still strand at rail post-3.5 (each MV
  legitimately cancels relief — raw-tool semantics, accepted); auto-start
  re-engages during MV chains per 4.1 gate (accepted bench footgun).

## 4. Auto-start trigger sensitivity

- [x] 4.1 **ACCEPTED — `-0.6` gate kept.** Real print: auto-start re-armed correctly
  on every pause-resume (buffer dives past `-0.6` under demand → `SM:1 ST:1`). The
  threshold is not too conservative in practice; no late-engage starvation observed.
- [x] 4.2 **No action.** No easier trigger needed (4.1). The `-0.6` + velocity gate
  is doing its job without the boot/idle spurious auto-sync the D18 + stale-timer
  fixes exist to prevent. Revisit only if a real workflow shows it engaging late.
- [x] 4.3 **CONFIRMED.** Stabilize/`BS` parks at goal `+0.40` (not `-0.4`) in normal
  operation, so the shallow-tension band is not the resting point. (The `-0.4`
  resting case ties to the BS no-op below, not the auto-start gate.)

## 5. Closeout

- [x] 5.1 **3.3 + 3.4 BENCH RE-VERIFIED (2026-06-11, e81e48b flashed):**
  Shot A — empty active lane (`T:2`) + `BS` from −0.56 → parks +0.40 one shot
  (old behavior: silent freeze). Shot B — loaded lane, `BS` from −0.55 → +0.41
  one shot, no stagnant abort. Bonus — `BS` from saturated tension rail −1.00 →
  +0.42 one shot (rail-break race won that time — see 3.6). Regression gate
  green (29/29 + dev-superset build). Remaining gates: **3.5 bench check**
  (MV mid-relief → clean takeover) and **3.6 rail-break default bump**
  (live `SET:PSF_STAB_RAIL_BREAK_MS:3000` confirm → default 1500→3000), then
  archive. Hunting / auto-start items accepted per the real print.
  **CLOSED 2026-06-11:** 3.5 bench check satisfied (clean MV-during-relief
  takeovers, no dual-controller fights); 3.6 default bump landed
  (config.ini.example + gen_config.py + MANUAL.md 1500→3000, tune.h regen);
  regression gate green; spec delta extended with shipped scenarios
  (lane fall-through, rail breakaway, MV takeover); observation file
  `memories/repo/psf-feed-quality.md` written. Archiving.
