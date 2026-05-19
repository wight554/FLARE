## 1. Pre-commit the decision rule (before any data)

- [x] 1.1 In design (K3), name the concrete real-print archetype the
  single-regime low-flip capture represents (the "plausibly common"
  claim K3 is judged against). Fix the KEEP/REMOVE rule + tie→REMOVE
  in writing before printing anything.

  2026-05-19: Locked K1/K3 to a vase-mode container/lamp-shade shell
  archetype before A/B data. KEEP still requires a measured win beyond
  run-to-run noise; tie or contrived-only win still means REMOVE.

## 2. Single-regime low-flip capture (K1)

- [x] 2.1 Build/choose a sustained ~constant-demand model (one feature
  speed, long uninterrupted runs, minimal feature-boundary
  transitions). Document slicer settings so it is reproducible.

  2026-05-19: Chose a tall simple vase/spiralized shell and documented
  reproducible slicer constraints in K1: one continuous wall, no
  infill/top, constant body speed, no adaptive/feature speed variation,
  body height long enough for steady-state confidence.
- [ ] 2.2 Print it with the estimator able to reach confidence
  (`SET:RELAY_CONF_WINDOW_MS:<large>` + `SET:RELAY_CONF_CYCLES:<low>`);
  capture CSV. Confirm `RDE1%` materially > 0 over a sustained span
  (vs bimodal r7 = 0%). If confidence is unreachable, record that as
  REMOVE-leaning evidence (K1 scenario) and skip to §4.

## 3. Forced A/B in that regime (K2)

- [x] 3.1 Fallback arm captured (`relay-vase-fallback.csv`,
  `SET:RELAY_CONF_CYCLES:2`+`WINDOW_MS:1000`, vase 60 mm³/s).
- [x] 3.2 Confident arm captured (`relay-vase-confident.csv`, same
  model/speed, `CYCLES:2`+`WINDOW_MS:300000`). Only the gate differed.
- [x] 3.3 Reduced both (demand-gated body isolation, identical):

  | metric | fallback B | confident A |
  |---|---|---|
  | RDE1% / longest hold | 0.0 / 0 s | **57.5 / ~79.5 s** |
  | TENSION %rows | **1.0** | **42.6** |
  | ep/min | 0.8 | 22.4 |
  | COMPRESSION | n22 gentle | **None (starved)** |
  | BP min/max | −5.43 / 5.02 | −1.95 / **12.5 wall** |

  Confident reached AND held confidence (valid exercise) yet pegged the
  +12.5 empty wall at 42.6 % TENSION — worse than bimodal r3 (26 %);
  fallback near-perfect in the identical regime. Analyzer corroborates:
  confident `relay_estimate_lo`→237 (collapsed/underfed) vs fallback
  →517 (true demand floor).

## 4. Verdict (K3)

- [x] 4.1 **VERDICT: REMOVE** (2026-05-19). K3 applied: KEEP required
  confident *measurably better* on a plausibly-common archetype; it was
  catastrophically **worse** (43× TENSION, empty-wall slam) in its own
  ideal single-regime (clean constant-60 vase) with confidence reached
  and held. Not a tie, not unreachable — reachable and ruinous. The
  confident relay duty-estimator path has no regime where it earns its
  keep. Relay is fallback-only.
- [x] 4.2 KEEP path not taken.
- [x] 4.3 REMOVE: K4 blast-radius confirmed accurate; handed to a new
  scoped implementation change `relay-fallback-only` (proposed
  separately). No code deleted in this decision change.

## 5. Closeout

- [x] 5.1 `openspec validate … --strict` green (2026-05-19).
- [x] 5.2 Memory `relay-confident-estimator-bimodal-bangbang` updated
  with the REMOVE verdict (deferred open question resolved). Committed
  + pushed. Follow-on `relay-fallback-only` implementation change
  proposed separately.
