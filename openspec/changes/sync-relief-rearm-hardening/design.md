## Context

Weakness audit of the type-D buffer-sync control flow in `firmware/src/sync.c`
(state = baseline + the `compression-overfeed-stop` true-stop). Type-D has only
two microswitches (TENSION/COMPRESSION, NEUTRAL between), no analog position, no
extruder feed-forward; `g_buf_pos` is dead-reckoned and only re-anchored at
switch crossings; `extruder_est_sps` is learned only at crossings. This document
is the record of the audit; the proposal scopes which findings to fix.

## Recovery state machine (the central fragility)

```
end of feed (extruder idle)
        │
COMPRESSION, feed=0 ──5s SYNC_AUTO_STOP_MS──► RELIEF_PAUSE   (sync.c:1731-1737)
        │                                          │ sync OFF, lane stopped
        │                       NEG_SYNC reverse-relieve (buffer_stabilize_tick)
        │                                          ▼
        │                        buf_force_stable_state(NEUTRAL)   (sync.c:708)
        │                                          │
        │                  buf_update(NEUTRAL) → sync.c:846 checks TENSION ONLY
        ▼                                          │
sits NEUTRAL, sync = RELIEF_PAUSE ◄───────────────┘ (NEUTRAL does not clear it)
        │
sync_tick: RELIEF_PAUSE → return (sync.c:1271)   ← auto-start cannot re-arm
        │
ONLY exit: buffer physically reaches TENSION → sync.c:846 → ACTIVE
```

## Findings (ranked)

| # | sev | finding | file:line | trigger | fix locus |
|---|-----|---------|-----------|---------|-----------|
| 1 | high | RELIEF_PAUSE re-arms only on TENSION; after relief→relieve→NEUTRAL sync stays paused until a full drain to empty; high-flow resume starves before re-arm | sync.c:846, 1271, 1731 | relief then high-flow resume / mid-print pause | firmware |
| 2 | high | Estimator full-overwrite on TENSION→COMPRESSION from a *modeled* travel (2·threshold ÷ dwell); short/partial transition → huge est → next NEUTRAL over-feeds → cycle | sync.c:762, 787-788, 801-802 | any fast/partial TENSION→COMPRESSION | firmware |
| 3 | med | Steady-feed relay limit cycle: NEUTRAL = est·RELAY_NEUTRAL_FRAC clamped to baseline_floor; if > true demand the buffer creeps to COMPRESSION every ~5s → RELIEF_PAUSE → reverse-relieve → re-arm, repeatedly | sync.c:1678-1683, 1731 | baseline_floor / NEUTRAL_FRAC > sustained demand | config/firmware |
| 4 | med | NEG_SYNC reverse-relieve gated on controller_idle = FLARE lane idle only (no extruder-motion knowledge) → can reverse-pull against a slow active extrusion | sync.c:611-614, 677-681 | slow-flow + over-feed → compression 5s mid-print | firmware |
| 5 | med | extruder_est_sps and g_buf_pos are global, not per-lane; a toolchange carries est (sps) + reserve across lanes, mis-scaled if lanes differ in MM_PER_STEP | sync.c:124, 788, 852 | per-lane different rotation_distance | firmware (rescale on swap) / N/A if identical |
| 6 | low | buf_read_stable flip-guard comment stale ("COMPRESSION commands SYNC_MIN" — now 0); logic OK, only active if relay_min_flip_mm>0 (default 0) | sync.c:552-563 | relay_min_flip_mm>0 | comment + config caveat |
| 7 | low | est_sps = …/MM_PER_STEP[idx] no runtime div-by-zero guard (relies on gen_config mandatory rotation_distance) | sync.c:788 | rotation_distance=0 misconfig | firmware guard / config |
| 8 | low | MV (TASK_MOVE) not tracked by buf_virtual_position_tick → g_buf_pos stale across an MV until next crossing re-anchors | sync.c:346-349 | MV feed-forward / retract | firmware |
| 9 | low | est update gated prev_dwell > BUF_HYST_MS; marginal (~BUF_HYST) transitions skip the update → staleness | sync.c:779 | rapid near-switch flapping | firmware |
| 10 | low | expf() (software float, no FPU) at every NEUTRAL↔wall crossing for the drift observer | sync.c:822 | frequent crossings | perf |
| 11 | low | mixes passed now_ms and global g_now_ms (fast-brake check) → minor timing skew | sync.c:1114 | — | tidy |
| 12 | med | fast-brake arms only on TENSION→COMPRESSION (`sync.c:1112-1113`); a NEUTRAL→COMPRESSION wall hit — the exact path an item-2 est spike produces — skips the instant brake and ramps down via SYNC_RAMP_DN_SPS (`sync.c:1704`), so the MMU keeps feeding into a full buffer for the ramp duration | sync.c:1112, 1141-1143, 1704 | est spike / over-feed drives NEUTRAL→COMPRESSION with no prior TENSION | firmware |
| 13 | low | `mmu_sps_dwell_sum` (uint32, `controller_shared.h:123`) accumulates per feed tick (`sync.c:1321`), averaged on the next crossing (`sync.c:744`), reset only on a crossing (`sync.c:854`). A ~12h demand-matched NEUTRAL ride with zero crossings overflows it → corrupt avg → corrupt est at the eventual crossing. Unreachable on a COMPRESSION ride (true-stop reaches a crossing in ~5s); needs a long, perfectly demand-matched NEUTRAL hold | sync.c:1321, 744, 854 | very long no-crossing NEUTRAL steady feed | firmware / N-A |

Items 1–11 are the initial audit; 12–13 were added after independent cross-checks
(Gemini, GPT-5.5) confirmed the two high items and isolated the NEUTRAL→COMPRESSION
fast-brake gap (12) and the dwell-sum overflow (13).

## Checked, NOT issues
- Mid-band demand estimation correctly absent (no ground truth between crossings).
- Type-D both-switches → feed 0 + `BS:FAULT`, never FAULT_HOLD (`sync.c:1314`); FAULT_HOLD recovery is type-P only.
- COMPRESSION→0 does not deadlock: flip-out exempt from the travel guard (`sync.c:560`) + relay_min_flip_mm defaults 0.
- Same-switch return (TENSION→NEUTRAL→TENSION) skips the estimator update: entry_pos is re-anchored to the switch (`sync.c:775`) so travel=0 (`sync.c:758`, gated at `779`). Correct — travel between same-switch returns is unknowable with no mid-band sensor; est stays *stale, not corrupt*. NEUTRAL feed uses the stale est (`sync.c:1679`); TENSION refill ignores est (`sync.c:1674`). (overlaps item 9)
- TENSION egress is also gated by the flip guard (the exemption at `sync.c:560` is COMPRESSION-egress-only), but only when `RELAY_MIN_FLIP_MM>0` (default 0 → inert); when enabled, refill feed accrues travel (`sync.c:1172`) and clears it. (= item 6)

## Decisions (proposed, for the scoped fixes)

- **D1 (item 1):** add a RELIEF_PAUSE→ACTIVE exit when the buffer reaches NEUTRAL
  via the relieve service, reseeding `g_buf_pos`/`sync_current` like the
  FAULT_HOLD recovery (`sync.c:1258-1262`) so resume needs no full drain.
- **D2 (item 2):** replace the hard overwrite at `sync.c:801-802` with a blended
  / rate-capped update so a single modeled transition can't spike `est`.
- **D3 (items 3-4):** evaluate gating the reverse-relieve on a longer idle and/or
  tightening the NEUTRAL hold; may be config-only. Decide during apply.
- **D5 (item 5):** rescale `est` on active-lane change if `MM_PER_STEP` differs;
  no-op for identical lanes.
- **D6 (item 12):** under type-D (`BUF_SENSOR_TYPE == 0`), arm the fast-brake
  (instant `sync_current_sps = 0`) on *any* →COMPRESSION entry, not only
  TENSION→COMPRESSION (`sync.c:1112`) — a 1-line extension of the existing
  true-stop intent. The item-2 blend (D2) only reduces the trigger; it does not
  close the NEUTRAL→COMPRESSION ramp gap. Decide at apply whether to scope here
  or defer.

## Risks / Trade-offs

- [Re-arming on NEUTRAL could resume into a still-leaning buffer] → reseed to the
  reserve target + bootstrap sps (proven safe by the FAULT_HOLD recovery).
- [Blended estimator reacts slower to a genuine fast demand jump] → the relay
  catch-up at TENSION already covers refill regardless of est; the blend only
  slows the NEUTRAL feed-forward, which is the safe direction.

## Regression impact (rule 10)

Per-fix blast radius across the touched flows. All four fixes must be gated
`BUF_SENSOR_TYPE == 0` so type-P is byte-for-byte unchanged; today the re-arm
(`846`), fast-brake arm (`1112`), and estimator overwrite (`801-802`) are all
type-agnostic.

### D1 — RELIEF_PAUSE re-arm on NEUTRAL
- **Spurious re-arm at true end-of-print (highest risk).** The NEUTRAL that would
  re-arm is produced by the relieve service (`buffer_stabilize_tick` →
  `buf_force_stable_state(NEUTRAL)`, `708`), which runs only when
  `buffer_stabilize_controller_idle()` (`611-614`: `TC_IDLE && !cutter_busy &&
  !sync_enabled && both lanes TASK_IDLE`) — i.e. end-of-print idle. A naive
  "re-arm on NEUTRAL" re-engages the motor exactly when the print is done.
  Mid-print-pause resume (the goal) and end-of-print look identical to type-D
  (no extruder signal).
- **Precondition to verify first:** `sync_relief_pause()` (`984`) does not clear
  `sync_enabled`. If it stays set, `controller_idle` is false → relieve never runs
  → COMPRESSION only clears by the extruder draining toward TENSION (already
  covered at `846`). Confirm what actually drives COMPRESSION→NEUTRAL *in*
  RELIEF_PAUSE before wiring D1, or the new exit fires never / only at idle.
- Boot stabilize uses the same `buf_force_stable_state(NEUTRAL)` (`723`); if the
  re-arm lives in `buf_update` it fires during boot. `sync_tick` guards
  `g_boot_stabilizing` (`1247`); `buf_update` does not → gate D1 on
  `!g_boot_stabilizing`.
- Reseed to reserve target while the buffer leans → brief model/physical mismatch
  until the next crossing; bounded (FAULT_HOLD proves it).

### D2 — estimator blend / rate-cap
- Slower reaction to a genuine fast demand jump → NEUTRAL feed lags on a real
  ramp-up; self-corrects via TENSION catch-up (`1675`, est-independent). Under-feed
  = safe direction.
- Cold start from est=0 ramps slower over the first crossings → weak initial
  NEUTRAL feed. Minor. Reuse the existing `alpha` path (`794`); add no tunable.

### D6 — fast-brake on any →COMPRESSION
- **Interacts with item 3 (limit cycle, possibly unfixed).** Normal NEUTRAL feed
  creeps to COMPRESSION ~every 5s and today ramps down gently; a 250ms instant
  brake on every creep stops harder → buffer relaxes faster → quicker re-lean →
  may deepen the oscillation. Verify against item 3 before scoping.
- `baseline_update_on_settle` skips while `fast_brake_active` (`1142-1144`); a
  brake overlapping the next COMPRESSION→NEUTRAL settle drops one baseline sample.
  Low.

### D3 (items 3-4) / D5 (item 5)
- D3 longer relieve gate → relief fires later → longer COMPRESSION dwell →
  post-print stabilize delayed. Config-only = low code risk.
- D5 rescale est on lane swap: isolated to toolchange, no-op for identical
  `MM_PER_STEP`; unit-check the ratio direction. est not persisted.

### Cross-cutting
- No `settings_t` field if no new tunable → **no `SETTINGS_VERSION` bump**;
  hold the no-new-tunables line (else full SET/GET/dump/doc parity, rule 8).
- `BEHAVIOR.md` sync state-machine + the RELIEF_PAUSE diagram must gain the
  NEUTRAL exit; `MANUAL.md` only if a tunable lands.
- Untouched: preload / load / unload / cutter. RELOAD only via tail-assist
  (D6 benign there).

## Testing notes (extends tasks 5.x)
- **D1 end-of-print isolation:** finish a print (lanes → idle), let COMPRESSION →
  RELIEF_PAUSE → relieve → NEUTRAL; assert sync stays paused, no AUTO_START,
  motor quiet. Then a mid-print pause→resume at high flow must re-arm without an
  empty-wall/grind. Watch `SYNC` events for a stray `AUTO_START` at idle.
- **D1 boot:** power-cycle with the buffer at COMPRESSION; confirm boot stabilize
  reaches NEUTRAL without tripping ACTIVE early.
- **D2 spike:** force a fast/partial TENSION→COMPRESSION; capture `extruder_est_sps`
  before/after — must not jump to the rail; next NEUTRAL feed must not over-shoot.
  Also a genuine fast ramp-up: confirm TENSION catch-up still refills (no starve).
- **D6 × item 3:** long steady print; log NEUTRAL↔COMPRESSION crossing period and
  depth with and without D6 — D6 must not shorten the period or deepen the lean.
- **D5:** two lanes with different `rotation_distance`; toolchange mid-sync;
  confirm est rescales (no over/under-feed on the first crossings post-swap).
  Identical-lane setup must be bit-identical to today.
- **Regression guard (all):** `flare_sync_check.py --mode regression` clean;
  re-confirm `compression-overfeed-stop` (purge + constant feed + end-of-feed
  true-stop, no -11 slam). type-P smoke (analog rig) unchanged.

## Daemon-safe live capture for HW validation

During HW validation the persistent `flare_daemon.py` may own `/dev/ttyACM0`.
Direct `flare_sync_check.py --live` then races the daemon and can fail with
pyserial disconnect / multiple-access errors. The checker should grow an
explicit daemon live source:

### scripts/flare_sync_check.py
- Add `--daemon` as a live source that reads the daemon telemetry stream instead
  of opening serial directly.
- Convert daemon JSON frames into the same `OK:...` and `EV:...` stream consumed
  by existing parsers, preserving analyzer behavior and CSV output.
- Keep `--live` direct serial behavior unchanged for no-daemon setups.
- Risk: daemon telemetry is cached and lower-rate than direct serial; use it as
  an observer-friendly capture path, not a replacement for direct raw serial
  when the daemon is stopped.

### scripts/test_flare_sync_check.py
- Add focused unit coverage for daemon JSON-to-stream conversion so analyzer
  parsing does not drift from direct serial field names.

## Open Questions

- Whether items 3-4 need firmware or are acceptable as config guidance.
- Real-print sign-off of the recovery changes once implemented.
