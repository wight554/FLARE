## 1. Collapse-ramp config surface (G3, zero behavior change first)

- [x] 1.1 Add config keys for the deep-COMPRESSION collapse-ramp:
  `relay_collapse_delay_ms`, `relay_collapse_ramp_mult`,
  `relay_collapse_cap_ms` in `config.ini`, `config.ini.example`,
  `scripts/gen_config.py` defaults. Defaults **equal current constants**
  (250 / 3 / 600).
- [x] 1.2 `gen_config.py` emits matching `#define`s into the generated
  tune header; `firmware/src/sync.c:19-21` consumes the generated values
  instead of literal `#define`s. No behavior change.
- [x] 1.3 `python3 scripts/test_gen_config.py` updated for the new keys;
  `python3 -m py_compile scripts/*.py` + `ninja -C build_local` green;
  status snapshot byte-identical to pre-change (defaults == old
  constants).

  2026-05-19: Added generator defaults and `CONF_RELAY_COLLAPSE_*`
  macros, wired `sync.c` collapse constants to generated values, and
  added default/override coverage in `scripts/test_gen_config.py`.
  Validation green: `python3 scripts/gen_config.py`,
  `python3 scripts/test_gen_config.py`, `python3 -m py_compile
  scripts/*.py`, `ninja -C build_local`. Effective firmware defaults
  remain delay 250 ms / ramp multiplier 3 / cap 600 ms, so the collapse
  ramp behavior and status-facing values are unchanged.

## 2. Confidence-gate hardened default (G1, primary fix)

- [ ] 2.1 Change `relay_confidence_cycles` and/or
  `relay_confidence_window_ms` defaults (`config.ini:67-68`,
  `gen_config.py:85-86`, `config.ini.example`) to the least-aggressive
  setting that keeps the bimodal cube unconfident. Provisional from r6
  (window 1000 ms); final value fixed in §4 on-hw. Keep within
  `protocol.c` clamp ranges (cycles 1–64, window 1000–300000).
- [ ] 2.2 Confirm no control-law / analyzer edit: `sync.c:1786-1820`
  branches and the D12 fill-anchor analyzer are untouched; only the
  gate defaults move. `ninja -C build_local` green.

## 3. Anti-chatter default (G2)

- [ ] 3.1 Set a non-zero `relay_min_flip_mm` default (`config.ini`,
  `config.ini.example`, `gen_config.py`); provisional ≈0.5–1 mm, final
  in §4. Assert `0.0` still compiles to behavior-identical
  (time-based) path (`sync.c:695`).
- [ ] 3.2 Host build + `py_compile` green; generated header carries the
  new default.

## 4. On-hardware A/B validation (G4, acceptance gate)

- [ ] 4.1 Flash the new defaults. Reprint the fast/bimodal 60×60 cube;
  capture steady-state; run the analyzer + the compare metrics
  (TENSION %rows, ep/min, BPmax, RDE active %). Assert r6-class:
  `BPmax` off the physical empty wall, no persistent mid-print TENSION,
  shallow cycle. Tune §2.1 / §3.1 values here and record finals.
- [ ] 4.2 Print a slow-only (~300–600 mm/min) model with the new
  defaults; assert no startup COMPRESSION slam, no fault, completes.
- [ ] 4.3 Record both captures + the A/B table vs the archived §0.1
  locked 4.2 baseline in this change (the hardware validation the
  archived 7.5 never did for the confident path).

## 5. Docs

- [ ] 5.1 TUNING.md relay section: hardened gate defaults, the
  `relay_min_flip_mm` knob, the collapse-ramp config keys, and the
  bimodal deep-TENSION failure-mode note. Keep copy-paste commands
  script-verified.

## 6. Closeout

- [ ] 6.1 `openspec validate relay-confidence-gate-harden --type change
  --strict` green.
- [ ] 6.2 Full check: `py_compile scripts/*.py`,
  `python3 scripts/test_gen_config.py`, analyzer test suite,
  `ninja -C build_local`. Commit + push to main.
- [ ] 6.3 Record the open architectural question (keep vs remove the
  confident relay-estimator path) with the r3–r6 evidence, flagged for
  a follow-up change pending a genuine single-regime low-flip on-hw
  capture. Do not decide here.
