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

Concrete model/archetype, fixed before data: a practical vase-mode
container/lamp-shade style shell, represented by a tall simple cylinder
or rounded-rectangle container. This is the real-use claim behind the
"plausibly common" bar: single-wall decorative/utility shells with
long continuous perimeter motion, not a synthetic straight-line torture
path. Reproducible slicer recipe:
- vase/spiralize mode enabled
- one continuous wall, no infill, no top layers
- 0.20 mm layer height, 0.45 mm extrusion width (or printer-equivalent)
- constant external perimeter speed for the whole body; disable
  feature-speed variation, acceleration/jerk tricks, adaptive layers,
  ironing, fuzzy skin, and seam painting
- at least 100 mm body height after any first-layer/base setup so the
  steady-state window is long enough for confidence to reach and hold
- exclude first-layer/base rows from reduction; compare only the
  continuous body span

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

- The archetype under test is the vase-mode container/lamp-shade shell
  named in K1. A confident-path win only counts as KEEP if that same
  archetype is accepted as a plausibly common real print before looking
  at A/B data.
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

### Verdict (2026-05-19): REMOVE

Single-regime A/B (clean constant-60 mm³/s spiralize vase, the
confident path's designed-for home; both arms identical except the
forced gate). Confident arm **reached and held** confidence (RDE1%
57.5, longest hold ~79.5 s) — a valid exercise, not the
unreachable-evidence path. Result, body-span, demand-gated:

| metric | fallback | confident |
|---|---|---|
| TENSION %rows | 1.0 | 42.6 |
| ep/min | 0.8 | 22.4 |
| COMPRESSION | n22 gentle | None (starved) |
| BP min/max | −5.43 / 5.02 | −1.95 / 12.5 (wall) |

Confident pegged the +12.5 mm empty wall at 42.6 % TENSION — worse than
even bimodal r3 (26 %); fallback near-perfect in the identical regime.
Per the pre-committed K3 rule (KEEP requires measurably better; tie or
contrived-only or unreachable → REMOVE), this is an unambiguous
**REMOVE**: the confident relay duty-estimator path is catastrophically
worse than fallback in the one regime it was designed for, with
confidence demonstrably reached and held. It has no regime where it
earns its keep. The relay is fallback-only; deletion handed to a
separate scoped implementation change `relay-fallback-only` (K4 blast
radius).

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

- Resolved 2026-05-19 before A/B data: K3's "plausibly common" bar is
  judged against the K1 vase-mode container/lamp-shade shell archetype.
- Whether the `relay_confidence_*` config keys are removed too on
  REMOVE or kept as inert fallback-tuning no-ops — resolve in the
  follow-on implementation change, not here.
