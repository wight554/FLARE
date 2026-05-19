## Context

Type-D relay NEUTRAL selects confident duty-estimator `v_est`
(`sync.c:257-258`, blended; clamped `[lo,hi]`) vs `extruder_est_sps`
fallback, gated by the confidence window/cycles
(`sync.c:228-231`). Archived `relay-duty-estimator-and-tuning` D10
asserted the estimator is a recovery arbitrator; D12 fill-anchored the
offline `relay_estimate_hi`; D14 (on-hw r3–r6) showed the confident
path strictly worse than fallback on bimodal. Shipped
`relay-confidence-gate-harden` G1 made the gate hard enough that
flip-heavy bimodal stays unconfident (r7: fallback drives, good). G2
dropped, G3 collapse-ramp runtime-tunable, defaults kept.

Gap: the confident path's *claimed* home — a genuine single-regime
low-flip print where the estimator reaches and **holds** confidence —
was never deliberately produced or measured. Every confident-path
datapoint to date is from the bimodal regime where it loses. A
keep-or-remove decision needs that missing A/B.

## Goals / Non-Goals

**Goals:**
- Produce the one regime the confident path was designed for and
  measure it head-to-head vs forced-fallback, deterministically.
- Record a defensible KEEP or REMOVE verdict with the evidence.
- If REMOVE: enumerate exactly what a follow-on change deletes.

**Non-Goals:**
- No production code change in this change (decision-only).
- Not re-litigating G1/G2/G3 (shipped, settled).
- Not implementing REMOVE here — that is a separate scoped change.
- Not changing the confidence gate defaults.

## Decisions

### K1 — The single-regime low-flip capture (the missing experiment)

Print a sustained ~constant-demand model (one feature speed, long
runs, minimal feature-boundary transitions) long enough that the
confidence window (`relay_confidence_window_ms` / `_cycles`) is
satisfied and `RDE` goes **and stays** active for a sustained span.
Confirm via `?:` / capture that `RDE1%` is materially > 0 (contrast:
bimodal r7 = 0 %). Without a capture where the confident path is
actually exercised, no keep/remove conclusion is valid — this is the
gating artifact.

### K2 — Forced A/B in that regime

Same model/speeds, two runs, only the gate forced:
- **fallback**: `SET:RELAY_CONF_WINDOW_MS:1000` (estimator never
  confident) — the r7-class driver.
- **confident**: `SET:RELAY_CONF_WINDOW_MS:<large>` +
  `SET:RELAY_CONF_CYCLES:<low>` so the estimator reaches/holds
  confidence in this low-flip regime.
Steady-state, startup-excluded compare (existing script): TENSION
%rows, ep/min, COMPRESSION dwell, `BPmin/max`, `RDE1%`, plus print
quality. Deterministic, no flash between (SET only).

### K3 — Decision rule (set before seeing data, to avoid bias)

- **KEEP** iff in the single-regime A/B the confident path is
  *measurably better* than fallback (lower TENSION% / tighter buffer /
  better quality) by a margin beyond run-to-run noise — AND that regime
  is plausibly common in real use. Then: retain, gate stays hardened,
  document the regime in TUNING.md (follow-up doc task).
- **REMOVE** otherwise (confident path ties or loses, or only wins in a
  contrived regime): the relay is fallback-only. Spawn a separate
  implementation change.
- Tie → REMOVE (simplicity wins; the path costs firmware state,
  telemetry, an analyzer subsystem, and config surface for no proven
  benefit).

### K4 — REMOVE blast radius (enumerated now, executed elsewhere)

If REMOVE: a follow-on change deletes — `sync.c` confident `v_est`
NEUTRAL branch + `[lo,hi]` clamp + estimator state; `RDE`/`RDCF`/`RDV`
telemetry (`protocol.c`, status string, `flare_cmd` dump);
`flare_analyze.py` `relay_duty_recommendations` +
`relay_estimate_lo/hi`/duty-coverage; `gen_config.py`/`config.ini`
`relay_estimate_*` keys (confidence keys may stay as fallback
no-ops or also go); TUNING.md relay estimator sections; related tests
(`relay-d12*`, coverage). Archived `relay-duty-estimator-and-tuning`
D2/D7/D12 machinery becomes dead history (left as-is; archives are
immutable). This enumeration is the design contract for that change;
it is **not** executed here.

## Risks / Trade-offs

- [Confident path wins only in a contrived/rare regime] → K3 requires
  the winning regime be *plausibly common*, not just existent; tie →
  REMOVE.
- [Single-regime capture still won't reach confidence] → that itself is
  evidence toward REMOVE (the path is unreachable in practice); record
  it as such, do not force an artificial scenario and call it a win.
- [Decision bias from seeing data first] → K3 fixes the decision rule
  before the A/B is run.
- [REMOVE loses a future-useful estimator] → mitigated: archived design
  + this change's evidence fully document the rationale; re-introducing
  is a deliberate new change if a real use case appears.

## Open Questions

- Exact "plausibly common" bar in K3 — name the concrete print
  archetype the confident regime corresponds to during K1 (before the
  A/B), so K3 is judged against a real-use claim, not hindsight.
- Whether the `relay_confidence_*` config keys are removed too on
  REMOVE or kept as inert fallback-tuning no-ops — resolve in the
  follow-on implementation change, not here.
