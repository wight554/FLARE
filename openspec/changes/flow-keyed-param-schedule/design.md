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

## Open Questions

- Default breakpoint cap `N` and the curvature-drop metric exact form —
  pick conservative defaults from existing bucket data during impl; expose
  `N` as a config tunable.
- Whether `trailing_bias_frac` needs its own breakpoints or can share the
  baseline flow grid — default: shared grid (one flow axis) unless bucket
  data shows bias inflects at different flows than baseline.
