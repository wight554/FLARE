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

## 3. Anti-chatter default (G2) — CLOSED, motion-hysteresis DROPPED (decision c)

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
- [x] 3.3 **On-hw (b) test FAILED → decision (c) (2026-05-19).** The
  (b) 0.5 build deadlocked again: cold `NEUTRAL→TENSION` entry
  suppressed (EST cold, MMU idle, travel 0 < 0.5), and AUTO_MODE sync
  auto-start (`sync.c:1400`) keys off `BUF_TENSION` → **sync never
  armed** (`SM:0`, BUF frozen NEUTRAL). Same root as 3.0, second
  instance. **Decision (c): drop motion-hysteresis.** Default
  re-reverted to `0.0` (committed `8061974`); `test_gen_config` asserts
  `0.0`; example annotated. `relay_min_flip_mm` stays a 0.0-default
  config knob with caveat; `sync.c:695` (b) exemption left dormant
  (harmless at 0.0). G2 closes: **not needed — G1 (r7) already meets
  the target**; time-based `BUF_HYST_MS` is the deadlock-free chatter
  guard. Stop smoothness is G3 (§4.4), never was G2.
  Recovery: post-flash `GET:RELAY_MIN_FLIP_MM` must read `0.0` (else
  `SET:0.0`+persist; prior 0.5 may be saved).

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
- [x] 4.2 **Slow-only hw test PASS (2026-05-19).** Slow-only model
  with the shipped G1 defaults: no startup COMPRESSION slam, no fault,
  completes. Slow regime confirmed the relay's best case (later
  slowA: TENSION 2.3 %, BP within ±5.3, fallback-driven, no ratchet).
- [x] 4.3 **A/B vs archived §0.1 locked 4.2 baseline (2026-05-19).**
  The hardware validation the archived 7.5 never did for the confident
  path. Steady-state, startup-excluded:

  | regime | mode | TENSION %rows | ep/min | BPmax | RDE1% |
  |---|---|---|---|---|---|
  | bimodal | confident (pre-fix, r3) | 26.0 | 13.8 | 12.5 wall | 71 |
  | bimodal | **G1 shipped (r7)** | **10.9** | **4.4** | **5.1** | 0 |
  | slow | G1 shipped (slowA) | 2.3 | 1.8 | 5.0 | 0 |

  G1 gate-harden moves the confident-path bimodal failure
  (deep-TENSION wall-slam) to fallback-driven, shallow, no ratchet —
  meeting the §0.1 slow/shallow/never-deep-TENSION intent the archived
  change asserted but never hw-checked on the confident path.
- [x] 4.4a **Collapse-ramp params made runtime-tunable**
  (2026-05-19). Mirrored the `RELAY_MIN_FLIP_MM` pattern: runtime
  globals (`main.c`), externs (`controller_shared.h`), settings
  field+apply+save+load (`settings_store.c`, `SETTINGS_VERSION`
  52→53), `SET:`/`GET:` + name-list (`protocol.c`), `flare_cmd.py
  --dump`. `sync.c:19-21` macros now alias the runtime vars (defaults
  from `CONF_*`, unchanged). Clamps: delay/cap `[0,5000]` ms, ramp_mult
  `[1,16]`. `ninja -C build_local`, `test_gen_config`, `py_compile`
  green. Enables on-hw iteration without reflash-per-tweak.
  **SETTINGS_VERSION bump → persisted settings reset to defaults on
  flash (defaults == prior behavior; re-apply any custom SET).**
- [x] 4.4b **Stop-smoothness — defaults retained (2026-05-19).**
  On-hw slow-profile A/B: slowA = default ramp (250/3/600),
  slowB = softened (delay↑/mult↓). Steady-state: slowB regressed —
  TENSION %rows 2.3→3.9, ep/min 1.8→3.3, COMPRESSION total 43→82 s,
  no smoothness gain. Print-end tail (the actual stop, prior script
  trimmed it): **default ramp already graceful** — feed tapers to ~0,
  buffer parks full at BP≈−5 (not the −12.5 wall), EST decays
  naturally; slowB only added a pre-stop TENSION/NEUTRAL spike.
  Conclusion: G1 (fallback-driven, off the empty wall) already
  delivers a smooth stop; G3 default is correct; softening is strictly
  worse. **Keep `config.ini` collapse-ramp defaults (250/3/600).**
  Runtime SET/GET (4.4a) stays shipped for advanced use. The original
  "no smooth stops without G2" concern is moot — never needed G2/G3
  tuning.

## 5. Docs

- [x] 5.1 TUNING.md relay section: hardened gate defaults, the
  `relay_min_flip_mm` knob, the collapse-ramp config keys, and the
  bimodal deep-TENSION failure-mode note. Keep copy-paste commands
  script-verified.

  2026-05-20: Added "always fallback-driven" declaration and bimodal
  deep-TENSION failure-mode note to the Type-D Relay Fallback Tuning
  section in TUNING.md. Note covers: no confidence gate, estimator
  removed, bimodal collapse root cause (26–43% TENSION rows, BPmax
  +12.5 wall vs fallback BPmax ~5 mm). Collapse-ramp keys and
  relay_min_flip_mm caveat were already present from relay-fallback-only
  5.1.

## 6. Closeout

- [x] 6.1 `openspec validate relay-confidence-gate-harden --type change
  --strict` green (2026-05-19).
- [x] 6.2 Full check green (2026-05-19): `py_compile scripts/*.py`,
  `test_gen_config.py`, `test_flare_analyze.py` (incl `relay-d12-real`),
  `ninja -C build_local`. Committed + pushed to main.
- [x] 6.3 **Open architectural question recorded (do not decide here):**
  the confident relay-estimator path was strictly worse than the
  fallback on every on-hw bimodal capture (r3–r6; G1 fixes it by
  *keeping the print off* the estimator). It has not been shown to
  earn its keep on any genuine single-regime low-flip print. Flagged
  for a **follow-up change** — decide keep-hardened vs remove the
  confident path entirely — pending a deliberate single-regime
  low-flip on-hw capture where the estimator actually reaches and
  holds confidence. Evidence in design G1/G2 + memory
  `relay-confident-estimator-bimodal-bangbang`. Not decided in this
  change.
