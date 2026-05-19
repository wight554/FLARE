## Why

`relay-confidence-gate-harden` shipped and on-hw-validated G1 by keeping
flip-heavy bimodal prints **off** the confident type-D relay duty
estimator (`RDE` active ~0 %, r7 TENSION 26→10.9 %, `BPmax` off the
+12.5 mm wall, fallback drives). On every on-hw bimodal capture (r3–r6)
the confident blended `v_est` (`sync.c:257-258`) was **strictly worse**
than the `extruder_est_sps` fallback. D10 framed the estimator as a
*recovery arbitrator* for genuine low-flip single-regime cycles — but
that regime was never deliberately captured with the estimator actually
reaching and holding confidence. So the confident path has **never been
shown to earn its keep on any real print**, yet it carries firmware
state, telemetry, an analyzer subsystem, and config keys. This change
runs the missing experiment and records a keep-or-remove decision.

## What Changes

- **Define + run the missing capture.** A deliberate single-regime
  low-flip on-hw print (sustained ~constant demand, few flips, long
  enough that the confidence window is satisfied and `RDE` goes and
  *stays* active). A/B in that regime: confident-path vs forced-fallback
  (`SET:RELAY_CONF_WINDOW_MS` to force each), same model/speeds. Metric:
  does confident `v_est` ever beat fallback on buffer depth / TENSION %
  / ep-min / print quality?
- **Record the decision (not pre-decided here):**
  - **KEEP** — confident path retained, gate stays hardened; document
    the narrow regime where it measurably helps. No code change.
  - **REMOVE** — relay becomes fallback-only: a follow-on implementation
    change deletes the confident `v_est` NEUTRAL branch
    (`sync.c:1786-1820`), the `[lo,hi]` clamp, relay estimator state +
    telemetry (`RDE`/`RDCF`/`RDV`), the `flare_analyze.py`
    `relay_duty_recommendations` machinery (`relay_estimate_lo/hi`,
    duty-cycle stats), and the now-dead config keys; plus migration
    notes for which archived `relay-duty-estimator-and-tuning`
    machinery becomes dead.
- This change is **decision-only**: one on-hw capture + the A/B + the
  recorded verdict. No production code edits in this change; if REMOVE,
  the deletion is a separate scoped implementation change.

## Capabilities

### New Capabilities
- `relay-driver-decision`: the deterministic single-regime A/B
  procedure that decides whether the confident relay duty-estimator
  path is retained or the relay is fallback-only, and the recorded
  verdict with its evidence.

### Modified Capabilities
- (none — decision-only; a REMOVE verdict spawns a separate
  implementation change that will modify the relay/analyzer specs.)

## Impact

- On-hw: one deliberate single-regime calibration print + an A/B pass
  (`SET:RELAY_CONF_WINDOW_MS` toggling, no flash, no code).
- No firmware/analyzer/config edits in this change. A REMOVE verdict
  scopes a follow-on change touching `firmware/src/sync.c`,
  `scripts/flare_analyze.py`, `scripts/gen_config.py` / `config.ini`,
  telemetry (`protocol.c`), docs, and tests.
- References (read-only): archived `relay-duty-estimator-and-tuning`
  (D10/D12/D14), shipped `relay-confidence-gate-harden`, memory
  `relay-confident-estimator-bimodal-bangbang`.
