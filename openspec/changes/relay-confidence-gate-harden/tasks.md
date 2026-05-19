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

- [x] 2.1 Change `relay_confidence_cycles` and/or
  `relay_confidence_window_ms` defaults (`config.ini:67-68`,
  `gen_config.py:85-86`, `config.ini.example`) to the least-aggressive
  setting that keeps the bimodal cube unconfident. Provisional from r6
  (window 1000 ms); final value fixed in §4 on-hw. Keep within
  `protocol.c` clamp ranges (cycles 1–64, window 1000–300000).
- [x] 2.2 Confirm no control-law / analyzer edit: `sync.c:1786-1820`
  branches and the D12 fill-anchor analyzer are untouched; only the
  gate defaults move. `ninja -C build_local` green.

  2026-05-19: Kept `relay_confidence_cycles` at 8 and changed
  `relay_confidence_window_ms` default to the r6-proven 1000 ms clamp
  floor in config/example/generator. No relay control-law branch or
  analyzer file changed in this step. Validation green: `python3
  scripts/test_gen_config.py`, `python3 -m py_compile scripts/*.py`,
  `ninja -C build_local`.

## 3. Anti-chatter default (G2) — REVERTED, guard rework required

- [x] 3.0 **Regression + revert (2026-05-19):** default 0.5 mm
  deadlocked the type-D relay (flip-out-of-COMPRESSION needs motor
  travel that COMPRESSION=SYNC_MIN suppresses → automatic sync froze
  on hardware). Reverted default to `0.0`; `test_gen_config` asserts
  `0.0`; example annotated; committed `307fa11`. See design G2.
- [x] 3.1 **Guard rework — option (b) (design G2).** `sync.c:695`:
  added `g_buf_stable_state != BUF_COMPRESSION` to the suppression
  condition so the egress flip from the zero-feed COMPRESSION state is
  never gated; hysteresis applies only to actuator-moving
  NEUTRAL↔TENSION. Code comment documents the deadlock rationale.
  (Host unit check folded into on-hw §3.3 — no firmware logic-test
  harness exists; `ninja -C build_local` link-green.)
- [x] 3.2 Re-enabled the `relay_min_flip_mm` default at 0.5
  (deadlock-safe under (b)); `gen_config.py` + `config.ini.example`
  (note updated) + `test_gen_config` assert 0.5. `ninja -C
  build_local`, `test_gen_config`, `py_compile` green.
- [ ] 3.3 On-hw confirm: flash the (b) build; automatic sync engages,
  **no COMPRESSION freeze** (egress flip immediate), guard demonstrably
  damps NEUTRAL↔TENSION chatter vs the r7 G1-only baseline (expect
  TENSION %rows / ep-min below r7's 10.9 / 4.4, BPmax still off the
  12.5 wall). Settings-persistence: post-flash `GET:RELAY_MIN_FLIP_MM`
  must read 0.5 (else `SET:0.5`+persist; prior 0.0 may be saved).

  2026-05-19: Set the provisional anti-chatter default to 0.5 mm in
  config/example/generator and added generated-default coverage in
  `scripts/test_gen_config.py`. The `sync.c:695` guard remains
  `RELAY_MIN_FLIP_MM > 0.0f`, so `0.0` still compiles to the prior
  time-only path. Validation green: `python3 scripts/gen_config.py`,
  `python3 scripts/test_gen_config.py`, `python3 -m py_compile
  scripts/*.py`, `ninja -C build_local`; generated local `tune.h`
  carries `CONF_RELAY_MIN_FLIP_MM 0.5f`.

  2026-05-19 parity correction: `relay_min_flip_mm` is settings-backed,
  so it now has `SET:RELAY_MIN_FLIP_MM`, `GET:RELAY_MIN_FLIP_MM`, live
  tune lock coverage, and `scripts/flare_cmd.py --dump` output. Docs no
  longer describe it as config-only.

## 4. On-hardware A/B validation (G4, acceptance gate)

- [x] 4.1 **G1-only bimodal hw test PASS (r7, 2026-05-19).** Shipped
  default `relay_confidence_window_ms=1000`, `min_flip=0.0`,
  collapse-ramp defaults. r7 vs the r6 SET-hack proof: TENSION %rows
  16.0→**10.9**, ep/min 6.3→**4.4**, ep dur p50 1.45→1.31, `BPmax`
  5.09→**5.1** (off the 12.5 empty wall), `RDE1%` 0.0 (fallback
  drives). Relay coverage PASS (126 cyc, 63/63 buckets); baseline/hi
  2400 stable, no ratchet, lo→423. The hardware validation the
  archived 7.5 never did for the confident path. Residual 10.9 %
  TENSION is the G2 anti-chatter target (§3.3).
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
