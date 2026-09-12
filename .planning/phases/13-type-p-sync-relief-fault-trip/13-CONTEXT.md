# Phase 13: Type-P Sync Relief & Fault Trip - Context

**Gathered:** 2026-09-12
**Status:** Ready for planning

<domain>
## Phase Boundary

Type-P (analog buffer) firmware sync loop only: bound the urgent refill so it never snaps to
`max_sps`, add a distance-based tension fault trip alongside the existing ms timer, and add a
bounded firmware-local feed probe that resolves the "+1.0 tension = home rail vs starved"
ambiguity. Plus `flare_sim` coverage for all of it, including shallow-rail twins.

Out of scope: daemon/Klipper/UI logic (Phases 14–15), gear-current boost (Phase 16), anything from
the type-D relay path, compression-side relief or compression mm trip (deferred), the
`psf_control_law` blend shape (follow-up), host-side interpretation of the probe result.

</domain>

<decisions>
## Implementation Decisions

### Relief snap (SC#1) — bounded, slew-limited refill in the TENSION soft wall
- **D-01:** Relief target = `min(max_sps, max(est × SYNC_PSF_RELIEF_MULT, baseline_sps))`, falling
  to `SYNC_MIN_RATE` only if the learned per-lane baseline is also 0. Never `max_sps` directly.
  Default multiplier 1.33 (HH `extreme_relief_frac=0.25`).
- **D-02:** The bounded target is applied through the existing distance-EMA/slew path
  (`sync_apply_type_p_smoothing`) with the slew cap doubled while pegged — no instant jump.
  The current `g_sync_current_sps = target_sps` bypass is removed. `g_psf_target_filt` still seeds
  at `g_extruder_est_sps`.
- **D-03:** The 2× slew stays distance-clocked with the existing 1 sps minimum step; no wall-clock
  minimum. A stopped extruder has no demand, so a frozen refill is correct (see memory
  `typep-sync-overfeed-frozen-clock` for why wall-clock UP is dangerous).
- **D-04:** The `SYNC_TENSION_RAMP_DELAY_MS` ramp-to-`max_sps` path in
  `sync_check_tension_dwell_and_ramp` is capped by the same relief bound. Invariant: in type-P mode
  **no path may command raw `max_sps` while pegged at tension** — sim asserts this unconditionally.
- **D-05:** Tension-side only. Compression-side bounding is deferred; the compression overshoot in
  the baseline is expected to shrink once the refill is bounded — re-measure first.
- **D-06:** Observability: `EV:SYNC:RELIEF_ON` / `EV:SYNC:RELIEF_OFF` edge events on soft-wall
  entry/exit (no per-tick spam). Rates remain visible via existing `?:` polling (`SC:`/`EST:`). No
  new fixed-position `ST:` field for relief.

### Distance trip (SC#2) — `SYNC_TENSION_STOP_MM`
- **D-07:** Accumulates **raw lane feed travel** (commanded steps × mm/step) while
  `g_buf.state == BUF_TENSION`; resets when the state leaves TENSION. Not "excess over demand" —
  FLARE's demand estimate is derived from the same pinned buffer, so that integral is circular.
- **D-08:** Default **32 mm** (2× the 16 mm buffer travel). At typical flow (~5 mm/s) it trips before
  the 6000 ms timer, making mm the primary trip and ms the slow-flow fallback. Shipped **on by
  default**; `0` disables.
- **D-09:** Trip path is the same block as the ms trip: `sync_try_runout_escalation` first, then
  `sync_fault_hold` + `EV:SYNC:FAULT_HOLD`. Emit the reason so logs show which won:
  `EV:SYNC:TENSION_STOP:MM` vs `EV:SYNC:TENSION_STOP:MS`.
- **D-10:** Arming: both mm and ms trips arm on the **first BUF state change after AUTO_START**
  (any TENSION↔NEUTRAL↔COMPRESSION edge in the active-sync window). No `|pos| < 0.12` near-neutral
  clause — that is an absolute compare. This **supersedes** the restart-on-activation fix from
  `psf-stale-fault-timers` (memory `typep-stale-fault-timers`); the compression ms timer
  (`CONF_PSF_WALL_SAT_MS`) follows the same arming rule.
- **D-11:** Telemetry: `TM:<mm>` (accumulated) and `ARM:<0|1>` added to the **extended `ST:` tail**
  next to `TT:` — the tail already tolerates extra keys in `flare_analyze.py` / watch parsers.
- **D-12:** The accumulator and arm flag reset at every site where `g_sync_tension_pin_since_ms`
  resets (`sync.c` ~`:1154, :1192, :1297, :1305, :1518`) and the arm flag clears on every
  AUTO_START / `sync_rearm_active`, so mm and ms can never drift apart.
- **D-13:** No compression mm trip this phase (deferred).
- **D-14:** Type-P only. Type-D relay TENSION contact is the normal refill signal — same exclusion
  the ms path already carries.

### Feed probe (SC#3) — "home rail, no consumer" vs "starved"
- **D-15:** The probe is a **passive observation window over the relief feed**, not a separate
  motion: after `PROBE_MM` of pinned lane travel (firmware constant = `BUF_MAX_TRAVEL_MM`, 16 mm),
  check whether `g_buf_pos` moved off the observed rail by the rail-relative delta. Moved →
  `CONSUMER` (real starvation, keep relieving under the bound). Not moved → `NO_CONSUMER`.
- **D-16:** Single accumulator shared with the mm trip (`TM:`): probe evaluates at `PROBE_MM` (16),
  trip at `SYNC_TENSION_STOP_MM` (32). Ordering is structural — the probe always decides before the
  mm trip can fire.
- **D-17:** Fires automatically **once per pinned episode, only when armed** (D-10). An explicit
  `PROBE:` serial command forces the window to start now (bench use). Never fires in type-D.
- **D-18:** `NO_CONSUMER` short-circuits the trip budget: go straight to `sync_try_runout_escalation`
  (the existing RELOAD path), not after 32 mm / 6 s. `CONSUMER` changes nothing — mm/ms keep
  running. This is the third input to the existing `(mode × filament_present)` resolution
  (memory `type-p-tension-klipper-agnostic`).
- **D-19:** Encoding: `PR:<0 none|1 running|2 consumer|3 no-consumer>` in the extended `ST:` tail,
  latched until the pinned episode ends, plus `EV:SYNC:PROBE:CONSUMER` / `EV:SYNC:PROBE:NO_CONSUMER`
  on completion.
- **D-20:** Firmware-only consumer this phase. The daemon mirrors `PR:` like any other `ST:` field;
  no daemon interpretation (belongs with Phase 14/15 status parity).
- **D-21:** Sensor truth wins: if the lane's own IN sensor is clear, neither the probe nor the mm
  trip runs — the existing runout path already handles it. The probe exists only for the ambiguous
  case (sensor says present, buffer says nothing moves).

### Rail-relative triggers (carried caveat from `51bdca8`)
- **D-22:** Soft-wall entry becomes rail-relative: track a per-sync-window tension extreme
  (`g_sync_tension_extreme`, deepens while ACTIVE, reset on AUTO_START/rearm — same pattern as
  `g_bl_lock_extreme`). Relief fires when `g_buf_pos <= extreme + SOFT_WALL_MARGIN_NORM`
  (firmware constant ≈ 0.15) once an extreme has been observed. Before any extreme is seen, fall
  back to `g_buf.state == BUF_TENSION`; **never** the `-CONF_PSF_SOFT_WALL_START` (-0.8) literal.
- **D-23:** "Pinned" (for the mm accumulator) and "moved off the rail" (for the probe) both reuse
  `BL_BREAK_DELTA_NORM` relative to the observed extreme — one hardware-validated delta, no new
  magic number.
- **D-24:** `psf_control_law`'s `CONF_PSF_SOFT_WALL_START` blend is left alone this phase (its output
  is now capped by the relief bound, so the absolute compare only shapes the ramp below the bound).
  Record as a follow-up in the plan so the caveat is not lost.
- **D-25:** Every new sim scenario runs twice: `type_p_rail_scale` 1.0 and 0.7 (the
  `bl_retract_immediate` pattern). Plus a dedicated tripwire: `EV:SYNC:RELIEF_ON` must be emitted at
  `rail_scale` 0.5 — the exact `51bdca8` failure class.

### Deliberate-hold suppression
- **D-26:** While a deliberate rail hold is in progress (BL: lock / tip-form, RELOAD FOLLOW, tail
  assist, buffer stabilize), relief, the mm trip and the probe are **all fully paused**; counters
  and arm flag reset when the hold ends, so the sync loop restarts clean and must see one buffer
  transition before it can trip again.
- **D-27:** Across a print pause / idle extruder with the buffer resting at tension: **freeze and
  keep progress**. Nothing is fed while stopped, so the counter naturally does not grow; no reset.

### Knob surface & persistence
- **D-28:** Live, flash-persisted, in `config.ini` + `config.ini.example` + SET/GET + `--dump` +
  MANUAL.md: **`SYNC_PSF_RELIEF_MULT`** (1.33) and **`SYNC_TENSION_STOP_MM`** (32). Adding TLV tags
  follows the settings v63 lazy-migration pattern (memory: rail-break 3000 needed a reflash — verify
  GET after reboot). — **Reversibility:** costly — a persisted TLV tag is part of the settings
  schema and the CLI `--dump` parity test; removing it later is a schema change.
- **D-29:** Firmware constants (tune.h), not knobs: `PROBE_MM` (= `BUF_MAX_TRAVEL_MM`),
  `SOFT_WALL_MARGIN_NORM`. Derived from buffer geometry; not per-rig.

### HW validation acceptance (SC#5 `HW:` items)
- **D-30:** A/B print = the same 2 h / ~10-swap print, same rig, same
  `scripts/verify_hw_open_items.py watch` capture as `baseline-capture.md` (2026-09-12).
- **D-31:** Pass bar (all four):
  1. ≤ 1 `EV:SYNC:RELIEF_PAUSE` per 60 s during sustained sync (baseline: dozens every ~3 s).
  2. Fewer `SYNC:TENSION_RISK_HIGH` than baseline (13 in 2 h).
  3. Zero false faults: no `FAULT_HOLD`, no `TENSION_STOP:MM`, no `PROBE:NO_CONSUMER` on a print
     that completes normally.
  4. No rail hits during print sync: `BP` never within `BL_BREAK_DELTA_NORM` of either observed
     extreme outside BL:/TC operations (expressed rail-relative, not as a literal ±0.8).
- **D-32:** **Sim first, filament last.** Everything that can be validated deterministically in
  `flare_sim` (bound invariant, ordering, probe outcomes, arming, suppression, shallow-rail twins)
  must pass there before any rig flash; the rig print only confirms what sim cannot (real spring
  dynamics, real extruder grip).

### Claude's Discretion
- Exact placement of the new state within `sync.c` (branch ordering, helper naming), `EV:` string
  spelling consistent with existing `SYNC:*` events, TLV tag numbering, `SOFT_WALL_MARGIN_NORM`
  exact value within 0.10–0.20, sim scenario naming (`sem_psf_*` family).
- How the `PROBE:` command is wired in `protocol.c` (use the `new-cmd` project skill pattern).
- Which existing PSF scenarios need expected-trace updates from removing the instant snap — as long
  as the 16 PSF scenarios still pass semantically ("no regression" = same pass/fail outcome, traces
  may change shape).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase source & baseline
- `.planning/ROADMAP.md` § Phase 13 — goal, the five success criteria, and the **carried caveat**
  paragraph (rail-relative thresholds, `type_p_rail_scale` shallow-rail requirement).
- `.planning/research/2026-09-11-happy-hare-borrow-scan.md` §1.1 (bounded relief snap), §1.2
  (FlowGuard distance trip + arming), §1.4 (type-P specifics), §4 (proportional-sensor probe,
  rec #6) — the HH mechanics being borrowed, with HH `ef8431c` line cites.
- `.planning/phases/13-type-p-sync-relief-fault-trip/baseline-capture.md` — the 2026-09-12 real-print
  baseline (hunting cadence, ±0.8 span, 13 TENSION_RISK_HIGH) that D-31 compares against, and the
  `UNLOAD_TIMEOUT` root-cause narrative that motivates D-22..D-25.

### Project rules
- `AGENTS.md` — session protocol, build/lint gates (build the dev superset: memory
  `dev-tuning-build-blindspot`), global naming (`g_lower_case`).
- `STYLE.md`, `TEST_CASES.md`, `TUNING.md` — code style, scenario conventions, knob documentation.
- `MANUAL.md` — where the two new knobs and `PROBE:` must be documented.
- `new-cmd` skill (user-level, `~/.claude-personal/skills/new-cmd/SKILL.md`; invoke via
  `Skill(new-cmd)`) — protocol command scaffolding pattern for `PROBE:`.

### Related prior decisions (memory, verify still current)
- `typep-feed-hunting-open` — the decision that produced SC#1.
- `typep-stale-fault-timers` — superseded by D-10 arming rule.
- `type-p-tension-klipper-agnostic` — the `(mode × filament_present)` resolution D-18 feeds.
- `typep-bl-absolute-threshold-unload-timeout` — the `51bdca8` rail-relative fix D-22/D-23 mirror.
- `typed-not-portable-to-typep` — why nothing from the type-D path is reused (SC#5).

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `g_bl_lock_extreme` + `BL_BREAK_DELTA_NORM` (`firmware/src/sync.c:121, :932-964`): the
  hardware-validated "track deepest reading, break relative to it" pattern — D-22/D-23 clone it for
  the sync window.
- `sync_apply_type_p_smoothing` (`sync.c:~1860-1905`): distance-EMA + slew + wall-clock decay; D-02
  routes relief through it with a doubled `g_sync_psf_slew_per_mm`.
- `sync_check_tension_dwell_and_ramp` (`sync.c:1638`): the ms trip block with
  `sync_try_runout_escalation` → `sync_fault_hold`; D-09 adds the mm condition here.
- `flow_param(...).baseline_sps` (already shown as `BF:` in `ST:`): the D-01 floor.
- Extended `ST:` tail in `firmware/src/protocol_status.c:~60-75` (`TT:`, `CT:`, …): where `TM:`,
  `ARM:`, `PR:` go.
- `tests/host/sim_scenario.c` `sem_psf_*` family + `type_p_rail_scale` plant knob
  (`sim_plant.c:178`) and `bl_retract_immediate` twin pattern (`sim_scenario.c:576-590`).
- `scripts/verify_hw_open_items.py watch` — the rig capture tool for D-30.

### Established Patterns
- Live knobs = `CONF_*` default in `tune.h` → `g_*` runtime var → settings TLV tag → SET/GET in
  `protocol.c` → `config.ini(.example)` → CLI `--dump` parity test → MANUAL.md. All seven touchpoints
  or the parity test fails.
- Dev-tuning knobs (`FLARE_INT_*` in `tune_internal.h`) are compiled only with
  `-DFLARE_DEV_TUNING=ON` — D-04 touches one; build/lint the dev superset.
- Type-D vs type-P branching is explicit on `g_buf_sensor_type`; every new behaviour is gated
  `BUF_SENSOR_TYPE_P`.
- `cmd_event("SYNC", "...")` for edge events; no per-tick events.

### Integration Points
- Urgent-refill branch `sync.c:2012-2025` — replaced by D-01/D-02/D-22.
- Tension pin bookkeeping (`g_sync_tension_pin_since_ms` reset sites) — D-12 piggybacks.
- `sync_rearm_active` / AUTO_START — arm-flag and extreme reset (D-10, D-22, D-26).
- Deliberate-hold states (BL: lock states, RELOAD FOLLOW, tail assist `g_sync_tail_assist_active`,
  buffer stabilize) — D-26 gate.
- `protocol.c` SET/GET table + `settings_store.c` TLV schema — D-28.

</code_context>

<specifics>
## Specific Ideas

- "Act as PO": the user wants product-level framing; technical mechanism is Claude's to choose
  within the decisions above.
- "Validate in sim whatever can be certainly validated, to avoid wasting real filament" — D-32 is a
  hard ordering rule for the plan: sim scenarios and the bound invariant are Wave 1; rig validation
  is the last, human-gated step.
- The invariant worth a single sim assertion: *type-P never commands raw `max_sps` while pegged at
  tension, on any rail scale.*

</specifics>

<deferred>
## Deferred Ideas

- Compression-side bounded relief (target `est / mult` instead of the current cut / relief-pause)
  — re-measure after the tension-side bound lands (D-05).
- Compression-side mm trip (`SYNC_COMPRESSION_STOP_MM`, HH "clog" side) (D-13).
- Making the `psf_control_law` soft-wall blend rail-relative (D-24 follow-up).
- Daemon-side interpretation of `PR:` (reason strings for Klipper/UI) — Phase 14/15 parity work
  (D-20).
- Continuous `g_sync_current_sps`/`target_sps` step-rate trace in the watch capture (noted in
  `baseline-capture.md` as a nice-to-have for multiplier tuning).

</deferred>

---

*Phase: 13-type-p-sync-relief-fault-trip*
*Context gathered: 2026-09-12*
