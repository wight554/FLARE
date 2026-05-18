## 1. Verify script surface (accuracy gate, do first)

- [x] 1.1 Capture exact flags from `--help` for `flare_analyze.py`, `flare_live_tuner.py`, `gcode_marker.py`, `flare_baseline_recommender.py`; note `gen_config.py` takes no args; record any operator-friendly gaps (recommender has no machine-id / no end-of-print stop)
- [x] 1.2 Confirm the `config.ini` keys for both paths from `config.ini.example`: `flow_schedule_cap` + `[flow_schedule.v1]`, and scalar `baseline_rate` / `sync_trailing_bias_frac`
      Validation 2026-05-18: captured `--help` for analyzer, live tuner,
      marker, and recommender; inspected `gen_config.py` and
      `flash_flare.sh`; grepped `config.ini.example` for scalar and schedule
      keys. Recommender gaps recorded in `design.md`.

## 2. Write TUNING.md

- [x] 2.1 TL;DR "simplest path" (defaults working) at the very top
- [x] 2.2 Plain "what tuning does / good vs bad" section, zero firmware assumptions, no Phase labels
- [x] 2.3 Prerequisites with exact commands: find serial port, find Klipper socket, install pyserial, back up state file
- [x] 2.4 Two-profile bracket model up front: same model, fastest- vs slowest-cubic-flow slicer profile, concrete how-to-pick + one worked example
- [x] 2.5 Capture — Klipper sidecar path: full `gcode_marker.py --emit sidecar` + `flare_live_tuner.py --observe-daemon --klipper-uds ... --sidecar ...` command lines + expected output
- [x] 2.6 Capture — sidecar-only: full command lines + expected output.
      Amendment 2026-05-18: shell-marker capture removed from the guide to
      match repository cleanup policy. Sidecar is the only documented path.
- [x] 2.7 Analyze: exact `flare_analyze.py --profile-fast --profile-slow --emit-flow-schedule [--flow-schedule-cap N] --out ...`; show sample output; explain sparse→one-point
- [x] 2.8 Review/apply: which keys into `config.ini`; exact `gen_config.py`, `ninja -C build_local`, `flash_flare.sh`, watermark (`flare_analyze.py --commit-watermark --state ...`) commands
- [x] 2.9 `flare_baseline_recommender.py`: exact `--port`/`--file` invocation, observe-only (no writes), how to read suggested baseline + drift summary, analyzer remains authority
- [x] 2.10 Verification: exact `STATUS` command; read `SYNC_REFILL_MM`/`SYNC_RELIEVE_MM`; operator meaning of `FAULT_HOLD`/`FAULT_HOLD_RECOVERY`/`cannot_refill`/`cannot_relieve` (observable only, link BEHAVIOR.md)
- [x] 2.11 Troubleshooting: acceptance-gate FAIL vs WARN in plain words + action each; "different numbers each run" → determinism + scalar one-point safe path
      Validation 2026-05-18: `TUNING.md` created from captured help output;
      includes both capture paths, flow-schedule analyze/apply, recommender,
      verification, troubleshooting, and open questions.

## 3. Link + de-jargon existing docs

- [x] 3.1 `README.md`: add a TUNING.md link in the appropriate section
- [x] 3.2 `MANUAL.md`: replace the stale "Trailing-Bias Tuning Quickstart (Phase 2.7)" heading with a one-line redirect to `TUNING.md` (no content duplication; reference detail stays, phase label gone)
- [x] 3.3 Cross-link git workflow (`WORKFLOW.md`) instead of copying
      Validation 2026-05-18: README links `TUNING.md`; MANUAL quickstart is
      now a redirect; TUNING links `WORKFLOW.md` without copying workflow text.

- [x] 2.12 Add "If Behavior Is Scary (Do This First)" recovery section
      (revert to shipped scalar defaults → reflash → verify → mechanical
      checklist) reachable from the TL;DR; explain `flow_schedule_cap` in
      "What Tuning Does"
      Validation 2026-05-18: review feedback gap fix; `rg -n "Phase"
      TUNING.md` still 0; `openspec validate tuning-operator-guide
      --strict`; `scripts/validate_regression.sh`. Added matching spec
      requirement "Recovery path for misbehaving setups".

## 4. Accuracy pass + closeout

- [x] 4.1 Re-run every command/flag in TUNING.md against the scripts' `--help`; fix any mismatch in the doc (not the scripts); list residual gaps as open questions
- [x] 4.2 Grep TUNING.md for "Phase" — zero internal phase labels
- [x] 4.3 `openspec validate tuning-operator-guide --strict`; `scripts/validate_regression.sh`; confirm no `scripts/*` or firmware diff in this change; `git status` clean
      Validation 2026-05-18: re-ran `--help` for analyzer, live tuner,
      marker, and recommender; `rg -n "Phase" TUNING.md` returned no matches;
      `openspec validate tuning-operator-guide --strict`;
      `scripts/validate_regression.sh`; `git status` clean before closeout
      task update. No `scripts/*` or firmware files changed in this docs pass.
