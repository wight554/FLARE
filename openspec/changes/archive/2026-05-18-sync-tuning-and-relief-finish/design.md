## Context

`standalone-sync-relief-model` (shipped `86fe5b6`, pending archive) defined a
five-state sync model. Audit of the shipped code found two gaps against its
own design D1/D5:

- `sync_fault_hold()` (`sync.c:787`) and its recovery timer (`sync.c:1018`)
  exist but are unreachable — hard-wall critical (`sync.c:1353`) and
  advance-dwell stop (`sync.c:1303`) still call destructive
  `sync_disable(true)` and emit `AUTO_STOP` / `ADV_DWELL_STOP`.
- `g_sync_refill_effort_mm` / `g_sync_relieve_effort_mm` are declared and
  reset in three places but never accumulated; no `cannot_*` events; not in
  `protocol.c` status (commit message claims exposure — it is absent).

Separately, offline baseline tuning uses a single capture and recency-weighted
bucket means (`flare_analyze.py`: `weight = n / (1 + age_seconds/86400)`),
so the same data yields different baselines depending on wall-clock age at
analysis time. Operators want one deterministic baseline derived from two
bracketing profiles (fastest and slowest cubic flow), plus a way to see what
the in-print live tuner would recommend.

Constraints: pure stdlib + pyserial for host tools; firmware tunables via
`config.ini` → generated `tune.h`; full-bias invariant and disciplined/
ephemeral/up-only/non-persistent live baseline are untouched; offline
analyzer remains the sole persistent authority; R8 flow-keyed schedule out
of scope; no host/Klipper/encoder coupling.

## Goals / Non-Goals

**Goals:**
- Make `SYNC_FAULT_HOLD` actually entered on the two terminal jam paths,
  non-destructively, with the existing auto-recovery becoming live.
- Make relief-effort diagnostics functional and observable, warn-only.
- Produce a deterministic baseline from a fixed two-profile procedure:
  identical input captures → byte-identical recommended baseline.
- Provide an observe-only host script that surfaces the live tuner's
  in-print baseline recommendation.

**Non-Goals:**
- No re-opening of `standalone-sync-relief-model` behavior beyond the two
  documented gaps (RELIEF_PAUSE, disciplined learner, HD→HOLD stay as-is).
- No full-bias / reserve-target / collapse-ramp change.
- No flow-keyed parameter schedule (R8, deferred follow-up).
- No automatic baseline writes from the recommender (observe-only, like
  `flare_live_tuner.py` default).
- No persistence-format or protocol-coupling change.

## Decisions

### D1 — FAULT_HOLD wiring, byte-minimal swap

At `sync.c:1303` (advance-dwell) and `sync.c:1353` (hard-wall) replace
`sync_disable(true)` with `sync_fault_hold()` and change the `cmd_event`
payload to `FAULT_HOLD` (keep the existing `extruder_est_last_update_ms` /
`sync_apply_to_active()` / `return` lines). `sync_fault_hold()` already sets
`sync_current_sps = 0` and records `g_sync_fault_hold_entry_ms`; it does not
touch the estimator — that is the non-destructive contract. Recovery at
`sync.c:1018` already transitions `SYNC_FAULT_HOLD → SYNC_OFF` after
`CONF_SYNC_FAULT_HOLD_RECOVERY_MS` and emits `FAULT_HOLD_RECOVERY`; normal
auto-arm then re-enters `SYNC_ACTIVE` reusing the preserved estimator (subject
to existing `SYNC_EST_FRESH_MS` aging). Both empty-side (advance-dwell) and
full-side (hard-wall) use the same conservative recovery — per the prior
change's open question default; no evidence yet to split them. Alternative
(distinct recovery intervals) rejected: adds a tunable and a state field with
no data justifying it.

### D2 — Effort accumulation co-located with mm integrator

`buf_sensor_tick` already integrates `g_sync_mmu_total_mm` per tick. Capture
the per-tick delta there; when `g_buf.state == BUF_ADVANCE` add it to
`g_sync_refill_effort_mm`, when `BUF_TRAILING` add to
`g_sync_relieve_effort_mm`. Existing resets in `buf_update` /
`sync_set_state` already clear them on state change, which gives correct
"sustained since entry" semantics for free. On crossing
`CONF_SYNC_CANNOT_REFILL_MM` while still ADVANCE, emit warn-only
`SYNC cannot_refill` once per episode (latch `g_sync_cannot_refill_warned`,
already declared); symmetric for relieve. No rate-limit timer needed — the
per-episode latch plus existing reset is simpler than the 30 s
`ADV_RISK_HIGH` window and cannot spam. Expose both as integer-mm fields in
the `protocol.c` status line and `GET` (`SYNC_REFILL_MM`, `SYNC_RELIEVE_MM`).
No control path reads them. Alternative (separate accumulator + its own state
tracking) rejected: duplicates the mm integrator and the reset wiring.

### D3 — Deterministic dual-profile baseline path in the analyzer

Add an explicit two-input mode to `flare_analyze.py` (e.g.
`--profile-fast CAP --profile-slow CAP --emit-baseline`) that derives the
baseline by a pure function of the input rows only: sort buckets by a stable
key, drop wall-clock recency weighting, use sample-count (`n`) weighting
only, and round with a fixed rule. Same input files → identical output,
independent of when run. The existing recency-weighted config-patch path is
left intact for the live-tuner workflow; the deterministic path is additive
and selected explicitly. The two-profile bracket (fastest + slowest cubic
flow) gives one scalar that bounds the regime range — the R8 schedule's
degenerate single point, preserved. Alternative (make the existing path
deterministic in place) rejected: would change established live-tuner
output and risk regression to that workflow.

### D4 — Recommender is observe-only tty reader

`scripts/flare_baseline_recommender.py`: stdlib + pyserial, reads status /
marker / `SYNC` lines, tracks the live tuner's drift signal across a print,
and at end-of-print prints a suggested persistent `baseline_sps` with the
supporting drift summary. No `SET`/`SV` writes (mirrors
`flare_live_tuner.py` observe-only default); operator copies the value into
config and re-flashes, keeping the offline analyzer the sole persistent
authority. Deterministic given a captured input stream (replayable from a
log file for test). Alternative (firmware emits the recommendation) rejected
by the scoping decision — keeps firmware unchanged for this part.

### D5 — Completion, not reopen

This change only closes the two audited gaps and adds host tooling. It does
not modify RELIEF_PAUSE, the disciplined learner, HD→HOLD, `mid_creep`
gating, or any full-bias control. `standalone-sync-relief-model` is archived
as-is; its `motion-safety` / `live-tuner` requirements are completed here via
delta specs.

## Risks / Trade-offs

- [Event rename `AUTO_STOP`/`ADV_DWELL_STOP` → `FAULT_HOLD` breaks log
  parsers / dashboards] → Documented as BREAKING in proposal; update
  workflow docs and any `scripts/` parsers in the same change; the rename is
  required for the relief vs fault distinction to be observable.
- [Per-tick effort math on RP2040] → Reuses the existing
  `g_sync_mmu_total_mm` integration delta; one float add + one compare per
  tick; warn-only, per-episode latched.
- [Preserved estimator stale after long FAULT_HOLD] → Unchanged behavior:
  existing `SYNC_EST_FRESH_MS` aging applies on re-arm, same as today's
  post-disable bootstrap fallback.
- [Deterministic analyzer path diverges from live-tuner path] → Intentional
  and additive; existing path untouched; covered by a determinism test
  (same inputs twice → identical bytes).
- [Recommender misreads tty / partial lines] → Observe-only, never writes;
  worst case is a wrong suggested number a human reviews; line parser reuses
  `serial_utils` patterns and is unit-tested against a captured stream.

## Migration Plan

Phased, each phase independently shippable/revertable:
1. Phase 1 (firmware): D1 FAULT_HOLD wiring + D2 effort accumulation/events/
   status. Local `cmake --build build_local`; `scripts/validate_regression.sh`.
2. Phase 2 (host, no firmware): D3 deterministic analyzer path + D4
   recommender script + tests + workflow docs.

Rollback: revert per phase. Reverting Phase 1 restores destructive
`sync_disable(true)` on the two paths (prior shipped behavior) without
touching host tooling.

## Resolved Decisions (locked — no implementer discretion)

These were open; now fixed so implementation carries zero tuning judgement:

- **Effort thresholds:** `CONF_SYNC_CANNOT_REFILL_MM = 50.0`,
  `CONF_SYNC_CANNOT_RELIEVE_MM = 50.0` (symmetric). Rationale: counter is
  commanded-MMU distance (flow-normalized); 50mm ≈ 31% of the ~160mm
  nominal-flow terminal jam ≈ ~2s precursor lead, longer at slow flow.
  Config tunables, not magic numbers.
- **Deterministic reducer clamp:** reuse the existing `BIAS_SAFE_MIN/MAX`
  guards from `flare_analyze.py`. Identical safety envelope to the recency
  path; clamp is deterministic so purity is preserved. No new tunable.
- **FAULT_HOLD recovery interval:** keep shipped
  `CONF_SYNC_FAULT_HOLD_RECOVERY_MS = 5000`. No field data to retune; mark
  with a `VERIFY:` comment to revisit using the now-emitted `FAULT_HOLD` /
  `FAULT_HOLD_RECOVERY` event logs.
- **Empty-side vs full-side recovery:** single shared
  `CONF_SYNC_FAULT_HOLD_RECOVERY_MS` for both advance-dwell and hard-wall.
  No split tunable. Revisit only if event logs show asymmetric re-fault
  rates.

R8 `flow-keyed-param-schedule` remains a separate follow-up, built after
this change lands. No other open questions.
