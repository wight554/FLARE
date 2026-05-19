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

- [ ] 3.1 Fallback arm: same model/speeds,
  `SET:RELAY_CONF_WINDOW_MS:1000` (estimator never confident);
  capture.
- [ ] 3.2 Confident arm: same model/speeds, gate forced confident;
  capture. Only the gate forcing differs between arms (no reflash).
- [ ] 3.3 Reduce both with the established steady-state,
  startup-excluded compare script (TENSION %rows, ep/min, COMPRESSION
  dwell, BP min/max, RDE1%) + note print-quality differences.

## 4. Verdict (K3)

- [ ] 4.1 Apply the pre-committed K3 rule to the A/B result. Record
  **KEEP** or **REMOVE** in this change with the evidence table.
  Tie → REMOVE.
- [ ] 4.2 If KEEP: add a TUNING.md follow-up note documenting the
  narrow regime where the confident path measurably helps; gate stays
  hardened; no code change.
- [ ] 4.3 If REMOVE: confirm the design K4 blast-radius enumeration is
  complete and accurate against current code (firmware confident
  branch + clamp + estimator state, `RDE`/`RDCF`/`RDV` telemetry,
  `flare_analyze.py` duty machinery + `relay_estimate_*`, config keys,
  docs, tests). Hand it to a new scoped implementation change
  (`relay-fallback-only` or similar). Do not delete code here.

## 5. Closeout

- [ ] 5.1 `openspec validate relay-confident-path-keep-or-remove
  --type change --strict` green.
- [ ] 5.2 Update memory `relay-confident-estimator-bimodal-bangbang`
  with the verdict (resolves the deferred open question). Commit +
  push to main. If REMOVE: the follow-on implementation change is
  proposed separately.
