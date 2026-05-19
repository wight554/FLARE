## Context

Type-D relay (`BUF_SENSOR_TYPE==0`) NEUTRAL feed selects between a
confidence-gated duty estimator (`g_relay_estimate_sps`, blended
`v_est`, `sync.c:257-258`) and an `extruder_est_sps` fallback
(`sync.c:1794-1819`). The archived `relay-duty-estimator-and-tuning`
D10 states the estimator is a *recovery arbitrator*, the fallback the
intended steady-state driver; D12 fixed the **offline** analyzer blend
collapsing under the never-COMPRESSION lean (fill-anchor, shipped
`164de66`); D13 fixed cold-seed (shipped). That change is archived
(`archive/2026-05-19-relay-duty-estimator-and-tuning/`, immutable) with
task 7.5 marked complete claiming slow/shallow/never-TENSION.

On-hw Phase-A reprints (60×60 bimodal cube; `buf_max_travel_mm=25` →
physical half-wall ±12.5 mm, switch span ±5 mm), steady-state, startup
excluded:

| capture | mode | TENSION %rows | ep/min | BPmax | RDE active |
|---|---|---|---|---|---|
| r3 | confident, base 2200 / FRAC 1.25 | 26.0 | 13.8 | **12.5** | 71 % |
| r4 | confident, base 2400 / FRAC 1.25 | 21.3 | 11.0 | **12.5** | 69 % |
| r5 | confident, FRAC 1.40 | 17.3 | 9.9 | **12.5** | 66 % |
| r6 | **gate off → fallback** | 16.0 | 6.3 | **5.09** | **0 %** |

The confident gate is satisfied ~67–71 % of a flip-heavy bimodal print
(default 8 cycles / 60000 ms window), so the collapsed blended `v_est`
(runtime twin of the D12 offline defect) drives NEUTRAL and chronically
underfeeds → buffer rides the +12.5 mm empty wall, deep wall-slam every
fast burst. `baseline_rate` and `RELAY_NEUTRAL_FRAC` knobs are
palliative only (≤20 % frequency, `BPmax` stays pegged 12.5). Forcing
never-confident (r6, `SET:RELAY_CONF_WINDOW_MS:1000`) is the only thing
that pulls the buffer off the wall (`BPmax`→5.09, shallow within the ±5
switch span). The control law is sound; the gate is mis-defaulted.

## Goals / Non-Goals

**Goals:**
- Make a flip-heavy bimodal print stay unconfident by default →
  fallback drives → r6-class behavior shipped, not a `SET:` hack.
- Add a residual flip damper (D8 `relay_min_flip_mm`, currently off).
- Make the deep-COMPRESSION collapse-ramp tunable (config) for later.
- Hardware-validate vs the archived §0.1 locked 4.2 baseline.

**Non-Goals:**
- No relay control-law change (TENSION/COMPRESSION/NEUTRAL branches
  untouched).
- No analyzer change (D12 fill-anchor stays).
- No edit to the archived change (immutable).
- Not deciding whether to delete the confident path — recorded as an
  open question only.
- No collapse-ramp *value* tuning in this change (gated on the gate fix
  rebalancing the buffer first).

## Decisions

### G1 — Harden the confidence-gate default (primary fix)

Raise `relay_confidence_cycles` and/or shorten
`relay_confidence_window_ms` so the gate is reached only by a sustained,
genuinely stable single-regime run, not by ordinary flip-heavy bimodal
traffic. r6 used window=1000 ms (min) as the proof; the shipped default
should be the least-aggressive setting that still keeps the cube
unconfident — pick on-hw in G4, not guessed here. Rationale: this is
D10-as-intended (fallback = driver); the estimator survives unchanged
for true low-flip cycles. *Alternatives rejected:* (a) keep defaults +
rely on `baseline`/`FRAC` — proven palliative only, `BPmax` stays 12.5;
(b) delete the confident path — premature on bimodal-only evidence (see
Open Questions); (c) change the control law — unnecessary, the law is
correct, only the driver-selection gate is mis-tuned.

### G2 — `relay_min_flip_mm` default ON

Ship a non-zero default (`sync.c:695` distance hysteresis: suppress a
flip until commanded travel since the last accepted flip ≥ value).
Damps residual chatter on both buffer sides after G1. Value on-hw (G4);
start small (≈0.5–1 mm). It is additive and behavior-identical when 0,
so the only risk is over-damping (delayed legitimate flips) — bounded by
keeping the default small and validating.

### G3 — Collapse-ramp constants → config (no value change yet)

`SYNC_COMPRESSION_COLLAPSE_DELAY_MS=250`, `_RAMP_MULT=3`,
`_CAP_MS=600` (`sync.c:19-21`) become generated config keys with
defaults **equal to today's constants** (zero behavior change in this
change). Tuning waits until G1 rebalances the buffer toward the intended
full-lean — COMPRESSION is currently under-exercised (r6 `BPmin` −5.6,
never reaches the full wall), so softening the deep-stop now would be
blind. Exposing the surface here, tuning in a follow-up / G4 notes.

### G4 — On-hw A/B is the acceptance gate

After G1–G3 land, A/B vs the archived §0.1 locked 4.2 baseline across
**two** models: a slow-only (~300–600 mm/min, exercises D13 cold-seed)
and a fast/bimodal cube (the r3–r6 model). Pass = bimodal `BPmax` off
the 12.5 wall (r6-class), no persistent mid-print TENSION, no slam,
shallow cycle; slow-only no startup COMPRESSION slam. The reprint-then-
re-analyze loop is the existing TUNING.md relay procedure.

## Risks / Trade-offs

- [Gate hardened so far the estimator never engages on any real print →
  confident path is dead weight] → recorded as the Open Question with
  r3–r6 evidence; not decided here. G1 picks the *least* aggressive
  setting that fixes the cube, preserving estimator engagement for
  genuine low-flip runs.
- [`relay_min_flip_mm` over-damps → legitimate flips delayed → late
  catch-up/relieve] → small default + G4 validation; additive, 0 = exact
  current behavior (instant rollback via config).
- [Collapse-ramp exposed but mis-tuned later] → G3 ships defaults ==
  current constants (no behavior change); any tuning is a separate
  validated step.
- [Slow-only model regression from a shorter confidence window] →
  G4 explicitly A/Bs a slow-only model; fallback + fixed catch-up
  (D11(a), un-clamped) already carry slow prints (archived evidence).
- [Defaults diverge from the archived §0.1 locked 4.2 baseline] →
  intended: 7.5 never validated the confident path; G4 re-establishes
  the baseline comparison with hardware data.

## Migration Plan

1. G3 first (pure config-surface, defaults == current constants, zero
   behavior change) → host build + `test_gen_config` green.
2. G1 + G2 defaults (config + generated header) → host build.
3. Flash; G4 on-hw A/B (slow-only + bimodal) vs §0.1 baseline; record
   captures + the compare metrics (TENSION %rows, ep/min, BPmax,
   RDE active %).
4. TUNING.md relay section update.

Rollback: every lever is a config default — restore prior
`relay_confidence_*`, set `relay_min_flip_mm:0`, collapse-ramp keys ==
old constants → exact pre-change behavior, no firmware revert.

## Implementation Plan

### 2026-05-19 G3 collapse-ramp config surface

Files:
- `config.ini` + `config.ini.example`: add
  `relay_collapse_delay_ms`, `relay_collapse_ramp_mult`, and
  `relay_collapse_cap_ms` beside the type-D relay knobs, with defaults
  equal to the current firmware constants (250 / 3 / 600).
- `scripts/gen_config.py`: add the three defaults and emit integer
  `CONF_RELAY_COLLAPSE_*` macros into `tune.h`.
- `firmware/src/sync.c`: keep the existing local
  `SYNC_COMPRESSION_COLLAPSE_*` names, but source them from generated
  macros instead of literals so behavior remains identical.
- `scripts/test_gen_config.py`: assert default macro values and an
  override case so both fallback defaults and explicit config values are
  covered.
- `openspec/changes/relay-confidence-gate-harden/tasks.md`: mark
  tasks 1.1-1.3 complete after `py_compile`, `test_gen_config`, and
  `ninja -C build_local` pass.

Risk/invariant: this unit must not touch relay confidence defaults,
`relay_min_flip_mm`, the runtime relay control law, analyzer behavior,
or settings layout. Generated defaults must remain byte-for-byte
equivalent in effective collapse timing: delay 250 ms, ramp multiplier
3, cap 600 ms.

## Open Questions

- **Keep vs remove the confident relay-estimator path.** r3–r6 show the
  fallback strictly better on bimodal; D12 already questioned emitting
  blend-derived values at all. Defer: needs a genuine single-regime
  low-flip on-hw capture to see if the confident path ever earns its
  keep. Record evidence, decide in a follow-up — do not delete on
  bimodal-only data.
- Exact G1 values (cycles vs window, magnitudes) — resolve on-hw in G4;
  must keep `protocol.c` clamp ranges (`RELAY_CONF_CYCLES` 1–64,
  `RELAY_CONF_WINDOW_MS` 1000–300000) valid.
- G2 `relay_min_flip_mm` magnitude — on-hw in G4.
