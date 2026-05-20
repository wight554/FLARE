## ADDED Requirements

### Requirement: Single-regime low-flip capture exists

The decision SHALL be based on an on-hardware capture from a deliberate
single-regime low-flip print (sustained ~constant demand, minimal
feature-boundary transitions) in which the type-D relay confidence gate
is satisfied and the estimate-active telemetry stays active for a
sustained span. A keep-or-remove conclusion drawn without such a capture
is invalid.

#### Scenario: Confident path is actually exercised

- **WHEN** the single-regime low-flip model is printed with the
  estimator able to reach confidence
- **THEN** the capture shows the estimate-active fraction materially
  greater than zero over a sustained span (in contrast to the bimodal
  r7 capture where it was ~0%)

#### Scenario: Confidence unreachable is itself evidence

- **WHEN** even a deliberate single-regime low-flip print cannot drive
  the estimator to hold confidence
- **THEN** that outcome is recorded as evidence toward REMOVE (the
  confident path is unreachable in practice) and no artificial scenario
  is substituted to manufacture a win

### Requirement: Forced confident-vs-fallback A/B in that regime

The decision SHALL include a deterministic A/B in the single-regime
capture: identical model and speeds, the only difference being the
confidence gate forced to fallback vs forced to confident via runtime
`SET:` (no reflash). The comparison MUST use the established
steady-state, startup-excluded metrics (TENSION %rows, ep/min,
COMPRESSION dwell, buffer BP min/max, estimate-active %) plus print
quality.

#### Scenario: Both arms captured under identical conditions

- **WHEN** the A/B is run
- **THEN** the only varied input between arms is the confidence-gate
  forcing, and both arms are reduced with the same steady-state
  compare procedure

### Requirement: Pre-committed decision rule and recorded verdict

The KEEP vs REMOVE decision rule SHALL be fixed before the A/B data is
viewed, and the final verdict SHALL be recorded in this change with its
evidence. KEEP requires the confident path to be measurably better than
fallback beyond run-to-run noise AND in a regime that is plausibly
common in real use; otherwise (including a tie) the verdict is REMOVE.

#### Scenario: KEEP verdict

- **WHEN** the confident path measurably beats fallback in the
  single-regime A/B and that regime maps to a plausibly common real
  print archetype
- **THEN** the verdict is KEEP: the confident path is retained, the
  gate stays hardened, and the helpful regime is documented

#### Scenario: REMOVE verdict

- **WHEN** the confident path ties or loses, or only wins in a
  contrived/rare regime
- **THEN** the verdict is REMOVE and a separate implementation change is
  scoped to make the relay fallback-only, using this change's
  enumerated blast radius

### Requirement: REMOVE blast radius is enumerated, not executed here

If the verdict is REMOVE, this change SHALL hand a separate
implementation change a complete enumeration of what becomes dead
(firmware confident `v_est` NEUTRAL branch and clamp, relay estimator
state and `RDE`/`RDCF`/`RDV` telemetry, analyzer duty-recommendation
machinery and `relay_estimate_*`, the now-unused config keys, docs, and
tests). This change MUST NOT itself delete production code.

#### Scenario: Decision-only boundary held

- **WHEN** this change is implemented/applied
- **THEN** no production firmware/analyzer/config code is modified; only
  the capture, the A/B reduction, the recorded verdict, and (on REMOVE)
  the enumerated hand-off are produced
