## Context

Decision change `relay-confident-path-keep-or-remove` returned REMOVE
with a pre-committed rule and on-hw evidence (vase A/B: confident TENSION
42.6 % / +12.5 wall vs fallback 1.0 % / ±5, confidence reached+held). Its
design K4 enumerated the blast radius; this change executes it. The
fallback (`extruder_est_sps · RELAY_NEUTRAL_FRAC`, clamped only
`[SYNC_MIN, relay_base]`, D11(a)) is already the validated steady-state
driver (`relay-confidence-gate-harden` G1/r7); removing the confident
path is deletion, not new control logic.

## Goals / Non-Goals

**Goals:**
- Make NEUTRAL unconditionally the fallback; delete all confident-path
  machinery (firmware/telemetry/analyzer/config/settings/docs/tests).
- Keep behavior at defaults byte-identical to today's *fallback*
  behavior (which is already the shipped steady state).
- Preserve non-relay analyzer output exactly (parity tests green).

**Non-Goals:**
- No change to the fallback relay law (TENSION catch-up, COMPRESSION
  `SYNC_MIN`, NEUTRAL = `extruder_est_sps·neutral_frac`).
- No change to G3 collapse-ramp or the `relay_min_flip_mm` guard.
- No edit to the archived `relay-duty-estimator-and-tuning` (immutable).
- No re-tuning; this is purely subtractive.

## Decisions

### R1 — NEUTRAL is unconditionally the fallback

In the `BUF_SENSOR_TYPE==0` relay block (`sync.c:~1786-1820`), the
NEUTRAL target becomes `clamp(extruder_est_sps, SYNC_MIN, relay_base) ·
RELAY_NEUTRAL_FRAC` (today's unconfident path, byte-identical). Delete
the `use_estimate` branch, `g_relay_estimate_sps`, the `[lo,hi]` clamp,
and `relay_estimator_confident`/`relay_seed_active` selection. The
cold-start seed (D13, from `relay_estimate_lo`) is also removed —
`relay_estimate_lo` is going away; cold start uses the same fallback
expression (a cold `extruder_est_sps` is bounded by `[SYNC_MIN,
relay_base]`, the documented D11(a) behavior). *Alternative rejected:*
keep the gate permanently unreachable instead of deleting — leaves dead
state, telemetry, config surface, and the exact maintenance burden the
REMOVE verdict exists to shed.

### R2 — Delete estimator state + accumulation

Remove `relay_estimator_accumulate`, `relay_estimator_complete_phase`,
the `v_est` blend (`sync.c:~257-258`), the duty/travel accumulators,
the confidence gate (`~228-231`), and pair-history state. Confirm no
remaining caller. The TENSION (catch-up) and COMPRESSION (`SYNC_MIN`)
branches are not touched.

### R3 — Telemetry surface shrink (BREAKING, accepted)

Remove `RDE`/`RDCF`/`RDV` from the `protocol.c` status string and any
`SET:`/`GET:` for confidence/estimate, plus `flare_cmd.py --dump`
entries and host parsers. Status parsers are generic key/value readers
(per archived D5 validation) so dropping keys is safe; the field-surface
break is accepted under the active-dev stale-keys policy and documented.
`neutral_creep`/`NC` is unrelated and stays (committed 7.2-A).

### R4 — Analyzer subtraction with parity contract

`flare_analyze.py`: delete `relay_duty_recommendations`,
`relay_duty_coverage`, the relay duty-cycle stats, and the
`relay_estimate_lo/hi`/`relay_seed_rate` emit + the `current` keys they
read. `baseline_rate`, flow schedule, acceptance gate, and every
non-relay code path must produce **byte-identical** output on existing
non-relay fixtures — this is the hard acceptance gate (mirrors archived
D7 parity). Retire relay tests (`relay-d12`, `relay-d12-real`,
`relay-cov-pass`, `relay-cov-warn`, `relay-d11`) and
`tests/fixtures/relay_review{1,2}.csv` (grep-confirm unused elsewhere).

### R5 — Config/settings removal + version bump

Remove `relay_estimate_lo`, `relay_estimate_hi`,
`relay_confidence_cycles`, `relay_confidence_window_ms`,
`relay_seed_warmup_ms` from `config.ini`, `config.ini.example`,
`gen_config.py` defaults + `CONF_*` emit, and `settings_store.c`
fields/apply/save/load. Bump `SETTINGS_VERSION` (persisted settings
reset to defaults on flash — defaults == fallback behavior; documented).
Keep `relay_catchup_frac`, `relay_neutral_frac`, `relay_min_flip_mm`
(0.0 inert; `sync.c:695` guard stays), `relay_collapse_*` (G3). The
`relay_confidence_*` keys are **removed**, not kept as no-ops — they
only ever gated the deleted path (design-resolved; supersedes the
deferred open question in `relay-confident-path-keep-or-remove`).

### R6 — Docs

`TUNING.md`: delete the relay duty-estimator + confidence-gate
sections, the offline relay capture/analyze/apply loop, and the
bimodal-ratchet note; keep the fallback relay law
(`relay_catchup_frac`/`relay_neutral_frac`), the collapse-ramp keys,
and the `relay_min_flip_mm` 0.0/deadlock caveat. `MANUAL.md`: drop any
`RDE`/relay-estimator references.

## Risks / Trade-offs

- [Hidden coupling: deleting estimator state breaks the fallback path]
  → R1 keeps the *exact* fallback expression already shipped (G1/r7);
  diff the relay block to confirm only the confident branch is gone;
  on-hw smoke (SM:1, vase/cube fallback-class) before closeout.
- [Analyzer parity regression] → R4 byte-identical gate on existing
  non-relay fixtures is mandatory; non-relay suite must stay green.
- [Status-field break for external consumers] → accepted (active dev);
  documented; parsers are generic k/v.
- [SETTINGS_VERSION bump resets persisted settings] → defaults ==
  fallback behavior; documented; re-apply any custom SET (catchup/
  neutral_frac/collapse) post-flash.
- [Archived change references dangling] → archived
  `relay-duty-estimator-and-tuning` left immutable; its D2/D7/D12 are
  explicitly dead history, noted in memory.

## Migration Plan

1. Firmware R1/R2 (relay block + estimator deletion) → `ninja
   -C build_local` green; diff shows only confident path removed.
2. R3 telemetry shrink → build + host parsers green.
3. R5 config/settings removal + `SETTINGS_VERSION` bump →
   `gen_config` + `test_gen_config` green.
4. R4 analyzer subtraction → non-relay parity byte-identical, relay
   tests retired, `py_compile` + non-relay suite green.
5. R6 docs.
6. On-hw smoke: flash, `GET` confirms removed keys gone, `SM:1` auto-arm,
   vase + cube run fallback-class (low TENSION, BP off wall). Commit +
   push; update memory.

Rollback: revert the change set; no data migration (settings just
re-default). The archived design retains full estimator history if it
must ever be reconstructed.

## Open Questions

- None blocking. (The `relay_confidence_*` keep-as-no-op question is
  resolved by R5: remove.)
