## Context

`sync-tuning-and-relief-finish` (landed `4675c45`) added a deterministic
dual-profile reducer in `flare_analyze.py` (`--emit-baseline`: sorted
buckets, n-weighted centroid, `BIAS_SAFE_MIN/MAX` clamp, no wall-clock
recency) that produces one scalar `baseline_sps` (+ `trailing_bias_frac`)
bracketed by a fast and a slow profile. The firmware consumes a single
scalar baseline/bias inside `SYNC_ACTIVE` reserve-target control; the
disciplined live learner (`sync.c` `baseline_update_on_settle`) ratchets a
global ephemeral `g_baseline_sps` up-only.

The remaining root issue: one scalar cannot serve a print whose flow varies
across regimes. The buckets already carry per-`v_fil_bin` structure — the
information for a flow→param curve exists; it is currently averaged away.

Constraints: pure stdlib + pyserial host tools; firmware tunables via
`config.ini` → generated `tune.h`; full-bias invariant and the
ephemeral/up-only/non-persistent live tier are untouched; offline analyzer
is the sole persistent authority; no host/Klipper/encoder coupling; bounded
float-light math on RP2040.

## Goals / Non-Goals

**Goals:**
- A deterministic flow→{baseline_sps, trailing_bias_frac} schedule emitted
  from existing buckets.
- Firmware interpolation on live `extruder_est_sps` replacing the scalar
  read, with the scalar config as an exact degenerate fallback.
- Zero regression for existing configs (1-point schedule ≡ today).

**Non-Goals:**
- No new control law, EKF, or smoothing beyond piecewise interpolation.
- No change to reserve target / `reserve_correction` / `zone_bias` /
  soft-wall / collapse ramp.
- No host extruder speed / planner / encoder input — flow key is the
  firmware's own `extruder_est_sps`.
- No persistence-format change beyond the additive schedule table.

## Decisions

### D1 — Schedule representation: bounded sorted breakpoint array

`tune.h` carries a fixed-capacity array
`CONF_FLOW_SCHED[N] = {{flow_sps, baseline_sps, bias_frac_milli}, ...}`
plus `CONF_FLOW_SCHED_LEN`. Breakpoints are strictly increasing in
`flow_sps`. `N` (cap) is a config tunable with a small default (e.g. 8).
`bias_frac` is stored as integer milli (×1000) to keep the table
integer-only; baseline is sps; flow key is sps — all native units, no
host coupling. Alternative (parametric curve fit) rejected: not
float-light, harder to make bit-deterministic, and overkill for a few
regime points.

### D2 — Degenerate equivalence is the compatibility contract

If the config has no schedule table, `gen_config.py` synthesizes a
length-1 schedule from the existing scalar `baseline_sps` /
`trailing_bias_frac` keys. Firmware interpolation with `LEN == 1` MUST
return exactly those scalars for all flow values (clamp-to-only-point).
This makes "no schedule" and "today" bit-identical and is the regression
gate. Alternative (separate scalar code path kept alive) rejected:
two code paths diverge over time; one interpolator with a 1-point table
is simpler and self-proves equivalence.

### D3 — Firmware interpolation: clamped linear, integer-light

`flow_param(flow_sps)`: binary/linear search the sorted table, clamp
below first / above last breakpoint (no extrapolation), linear interpolate
between the two bracketing points using integer math
(`a + (b-a)*(x-x0)/(x1-x0)`). Called where the scalar baseline/bias is
read today; the live ratchet `max()` is applied to the interpolated
segment baseline, not a global. Cost: one search + one lerp per use,
bounded by `N`. Alternative (nearest-breakpoint step) rejected: visible
discontinuities at regime edges; linear is still float-light.

### D4 — Analyzer emits the schedule deterministically

Reuse the `sync-tuning-and-relief-finish` reducer per flow bin: group
buckets by `v_fil_bin`, drop bins below the existing maturity gates,
produce one (flow, baseline, bias) point per surviving bin via the same
n-weighted centroid + `BIAS_SAFE_MIN/MAX` clamp, sort by flow, then
**deterministically reduce to ≤ N points** (fixed rule: keep endpoints,
then drop the lowest-curvature interior point until ≤ N — ties broken by
lowest flow). No wall-clock recency. Same buckets → same table. The
fast/slow dual-profile inputs become the schedule's bracketing endpoints
(scalar workflow is the LEN==1 special case). Alternative (emit every bin)
rejected: violates the bounded-size constraint and is noise-sensitive.

### D5 — Live learner scoped to active segment

`baseline_update_on_settle` continues (disciplined, multi-cycle,
variance-reject, cooldown, up-only, non-persistent) but ratchets the
baseline of the **currently active flow segment** only, not a global
scalar. On reboot the schedule reloads from config (offline authority);
the live delta is lost (unchanged ephemerality). Alternative (keep global
live ratchet) rejected: reintroduces the regime-averaging the schedule
exists to remove.

## Risks / Trade-offs

- [Degenerate path subtly diverges from today] → LEN==1 interpolation is
  the regression gate; host test asserts a scalar-only config generates a
  LEN==1 table and a parity test asserts identical commanded output vs
  pre-change for a flow sweep.
- [Schedule reduction non-deterministic across runs] → Fixed,
  data-only reduction rule (endpoints + lowest-curvature drop, deterministic
  tie-break); determinism test: same buckets twice → byte-identical table.
- [Interpolation cost / float on RP2040] → Integer lerp, table bounded by
  `N`, search ≤ `N`; no per-tick float curve eval.
- [Sparse buckets → degenerate or jumpy schedule] → Maturity gates reused;
  below threshold the analyzer emits the scalar (LEN==1) rather than a
  noisy multi-point table.
- [Operators confused by a table vs a number] → Config keeps the scalar
  keys working; the table is opt-in and documented as the deterministic
  output of the same two-profile procedure.

## Migration Plan

Built after `sync-tuning-and-relief-finish`. Phased:
1. Format + degenerate: define versioned schedule table in
   `config.ini`/`gen_config.py`; synthesize LEN==1 from scalar keys;
   firmware interpolator with LEN==1 ≡ scalar (parity test, no behavior
   change shipped).
2. Analyzer: emit multi-point schedule (D4) behind explicit flag; tests
   for determinism + degenerate-equivalence.
3. Firmware: switch baseline/bias reads to `flow_param()`; scope live
   ratchet to active segment; regression + flow-sweep parity.

Rollback: per phase. Reverting phase 3 restores scalar reads; the schedule
table is inert additive config until then.

## Implementation Notes (2026-05-18)

### Current source findings
- `scripts/gen_config.py` currently emits only scalar `CONF_BASELINE_SPS`
  and `CONF_SYNC_TRAILING_BIAS_FRAC`; no structured table support exists.
- `sync.c` owns scalar runtime state (`g_baseline_target_sps`,
  `g_baseline_sps`) and uses `SYNC_TRAILING_BIAS_FRAC` in
  `buf_target_reserve_mm()`. The live learner in
  `baseline_update_on_settle()` updates only the global `g_baseline_sps`.
- `settings_store.c` persists scalar baseline/bias. Phase 1 can keep
  persistence unchanged by synthesizing the schedule from the persisted scalar
  on defaults/load paths and by keeping schedule table compile-time only.
- `protocol.c` exposes scalar `BASELINE_RATE`/`BASELINE_SPS` and
  `TRAIL_BIAS_FRAC`; these remain valid scalar controls and should refresh the
  degenerate runtime schedule.
- `config.ini` and `firmware/include/tune.h` are ignored local artifacts; only
  `config.ini.example` and generator/source changes should be committed.

### File-level plan

#### `config.ini.example`
- Add commented `flow_schedule_cap` and a versioned commented
  `[flow_schedule.v1]` example near the scalar baseline/bias keys.
- Explain that absence of the table synthesizes a 1-point schedule from
  `baseline_rate` and `sync_trailing_bias_frac`.

#### `scripts/gen_config.py`
- Parse optional `flow_schedule.v1` entries from sections or flat keys into
  sorted `(flow_sps, baseline_sps, bias_milli)` points.
- Add `flow_schedule_cap` default and clamp emitted length to the cap.
- Emit `flow_schedule_point_t`, `CONF_FLOW_SCHED_CAP`,
  `CONF_FLOW_SCHED_LEN`, and `CONF_FLOW_SCHED[]`.
- If no table exists, synthesize one point from scalar `baseline_rate` and
  `sync_trailing_bias_frac`; this is the degenerate compatibility path.

#### `firmware/include/sync.h` + `firmware/src/sync.c`
- Add `flow_param_t` and `flow_param(int flow_sps)` using clamped integer
  linear interpolation over `CONF_FLOW_SCHED`.
- Add helpers to reset the runtime schedule from generated config and refresh
  a scalar single-point schedule when `SET:BASELINE_*` or
  `SET:TRAIL_BIAS_FRAC` changes.
- Keep `LEN==1` returning exact configured values for all flow inputs.

#### `firmware/src/settings_store.c`
- Call the reset/refresh helpers after defaults and load so reboot or
  settings reload discards live schedule deltas.
- Do not change `settings_t` layout in Phase 1; no settings version bump.

#### `firmware/src/protocol.c`
- After scalar baseline or bias SET handlers, refresh the degenerate runtime
  schedule from the updated scalar values so existing protocol controls stay
  coherent.

#### Validation
- Add a focused generator test that scalar-only config produces
  `CONF_FLOW_SCHED_LEN 1` and exact scalar-equivalent point values.
- Run `python3 -m py_compile scripts/*.py`, `ninja -C build_local`, and the
  new test before the Phase 1 commit.

### Phase 2 analyzer plan

#### Current source findings
- `scripts/flare_analyze.py` already has an additive deterministic
  `emit_baseline` branch in `run()`, but the CLI parser does not currently
  expose `--emit-baseline`, `--profile-fast`, or `--profile-slow`.
- The deterministic scalar branch groups MID rows by stable bucket label,
  uses sample-count weights, clamps bias with `BIAS_SAFE_MIN/MAX`, and writes
  scalar `baseline_rate` / `sync_trailing_bias_frac`.
- Existing analyzer regression tests call `run()` directly with
  `SimpleNamespace`, so schedule emission can be covered without shelling out.

#### `scripts/flare_analyze.py`
- Add a shared deterministic reducer helper for the current two-profile scalar
  behavior so `emit_baseline` output remains unchanged.
- Add `emit_flow_schedule` mode using the same two-profile inputs. Group MID
  rows by `v_fil` bin, reduce each mature bin with per-bucket medians and
  sample-count weights, clamp bias, and sort points by flow.
- Treat a bin as mature only when it has at least `MIN_RUN_BUCKET_ROWS`
  samples; if fewer than two bins survive, emit a one-point scalar-equivalent
  schedule.
- Reduce over-cap schedules deterministically: keep endpoints, then repeatedly
  drop the lowest-curvature interior point, breaking ties by lower flow. For
  cap 1, emit the scalar-equivalent one-point schedule.
- Expose `--emit-baseline`, `--emit-flow-schedule`, `--profile-fast`,
  `--profile-slow`, and `--flow-schedule-cap` in argparse while keeping normal
  review-patch validation in `run()`.

#### `scripts/test_flare_analyze.py`
- Add a deterministic schedule fixture test that writes the same two profiles
  twice and asserts byte-identical schedule output.
- Add a sparse fixture test that verifies schedule emission falls back to one
  point.
- Keep the existing deterministic scalar test unchanged to guard
  `--emit-baseline` compatibility.

## Open Questions

- Default breakpoint cap `N` and the curvature-drop metric exact form —
  pick conservative defaults from existing bucket data during impl; expose
  `N` as a config tunable.
- Whether `trailing_bias_frac` needs its own breakpoints or can share the
  baseline flow grid — default: shared grid (one flow axis) unless bucket
  data shows bias inflects at different flows than baseline.
