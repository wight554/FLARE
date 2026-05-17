## 1. Verify script surface (accuracy gate, do first)

- [x] 1.1 Capture exact flags from `--help` for `flare_analyze.py`, `flare_live_tuner.py`, `gcode_marker.py`, `flare_baseline_recommender.py`; note `gen_config.py` takes no args; record any operator-friendly gaps (recommender has no machine-id / no end-of-print stop)
- [x] 1.2 Confirm the `config.ini` keys for both paths from `config.ini.example`: `flow_schedule_cap` + `[flow_schedule.v1]`, and scalar `baseline_rate` / `sync_trailing_bias_frac`
      Validation 2026-05-18: captured `--help` for analyzer, live tuner,
      marker, and recommender; inspected `gen_config.py` and
      `flash_flare.sh`; grepped `config.ini.example` for scalar and schedule
      keys. Recommender gaps recorded in `design.md`.

## 2. Write TUNING.md

- [ ] 2.1 TL;DR "simplest path" (defaults working) at the very top
- [ ] 2.2 Plain "what tuning does / good vs bad" section, zero firmware assumptions, no Phase labels
- [ ] 2.3 Prerequisites with exact commands: find serial port, find Klipper socket, install pyserial, back up state file
- [ ] 2.4 Two-profile bracket model up front: same model, fastest- vs slowest-cubic-flow slicer profile, concrete how-to-pick + one worked example
- [ ] 2.5 Capture — Klipper sidecar path: full `gcode_marker.py --emit sidecar` + `flare_live_tuner.py --observe-daemon --klipper-uds ... --sidecar ...` command lines + expected output
- [ ] 2.6 Capture — standalone shell-marker fallback: full command lines (`--klipper-mode off` / marker file) + expected output, presented first-class
- [ ] 2.7 Analyze: exact `flare_analyze.py --profile-fast --profile-slow --emit-flow-schedule [--flow-schedule-cap N] --out ...`; show sample output; explain sparse→one-point
- [ ] 2.8 Review/apply: which keys into `config.ini`; exact `gen_config.py`, `ninja -C build_local`, `flash_flare.sh`, watermark (`flare_analyze.py --commit-watermark --state ...`) commands
- [ ] 2.9 `flare_baseline_recommender.py`: exact `--port`/`--file` invocation, observe-only (no writes), how to read suggested baseline + drift summary, analyzer remains authority
- [ ] 2.10 Verification: exact `STATUS` command; read `SYNC_REFILL_MM`/`SYNC_RELIEVE_MM`; operator meaning of `FAULT_HOLD`/`FAULT_HOLD_RECOVERY`/`cannot_refill`/`cannot_relieve` (observable only, link BEHAVIOR.md)
- [ ] 2.11 Troubleshooting: acceptance-gate FAIL vs WARN in plain words + action each; "different numbers each run" → determinism + scalar one-point safe path

## 3. Link + de-jargon existing docs

- [ ] 3.1 `README.md`: add a TUNING.md link in the appropriate section
- [ ] 3.2 `MANUAL.md`: replace the stale "Trailing-Bias Tuning Quickstart (Phase 2.7)" heading with a one-line redirect to `TUNING.md` (no content duplication; reference detail stays, phase label gone)
- [ ] 3.3 Cross-link git workflow (`WORKFLOW.md`) instead of copying

## 4. Accuracy pass + closeout

- [ ] 4.1 Re-run every command/flag in TUNING.md against the scripts' `--help`; fix any mismatch in the doc (not the scripts); list residual gaps as open questions
- [ ] 4.2 Grep TUNING.md for "Phase" — zero internal phase labels
- [ ] 4.3 `openspec validate tuning-operator-guide --strict`; `scripts/validate_regression.sh`; confirm no `scripts/*` or firmware diff in this change; `git status` clean
