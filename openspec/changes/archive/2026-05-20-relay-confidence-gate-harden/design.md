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

### G2 — `relay_min_flip_mm` motion-hysteresis: DROPPED (decision c)

Original intent: ship a non-zero default (`sync.c:695` distance
hysteresis) as a residual chatter damper after G1.

**On-hw regression (2026-05-19, default 0.5):** automatic sync stopped
working. Root cause: the guard gates **all** flips until
`g_relay_flip_travel_since_mm ≥ RELAY_MIN_FLIP_MM`, and that counter
accumulates from **actual motor motion** (`sync.c:1285`,
`lane_motion_sps`). The type-D COMPRESSION branch commands MMU
`SYNC_MIN ≈ 0`, so while the buffer is held in COMPRESSION no travel
accrues → the flip *out of* COMPRESSION never reaches the threshold →
the relay freezes (cannot leave a zero-feed state because leaving it is
what produces the travel the guard demands). At `0.0` the guard is
inert — that is why it worked before. The deadlock is intrinsic to
"flip-distance measured on the gated actuator's own motion."

**Second on-hw deadlock (2026-05-19, attempt (b), default 0.5):** (b)
exempted COMPRESSION-egress, but the **cold `NEUTRAL→TENSION` corrective
entry** has the identical topology. At print start `EST` is cold,
NEUTRAL feed ≈ 0, MMU idle → `g_relay_flip_travel_since_mm` never
reaches the threshold → the first TENSION flip is suppressed. Worse:
AUTO_MODE sync auto-start (`sync.c:1400`) keys off `g_buf.state ==
BUF_TENSION`, so suppressing that flip means **sync never even arms**
(`SM:0`, `BUF` frozen at NEUTRAL). Generalized root: *any* flip whose
acceptance is what produces the motion the guard measures deadlocks —
COMPRESSION-egress and corrective TENSION-entry are two instances; the
exemption list is whack-a-mole.

**Decision (c): DROP motion-distance hysteresis.** `relay_min_flip_mm`
default stays `0.0` permanently (committed `307fa11` / `8061974`); it
remains a config knob (0.0 = inert, `sync.c:695` guarded on `> 0.0f`)
with a documented deadlock caveat, but is **not** a shipped non-zero
default. Rationale: (1) G1-only (r7) already met the target — TENSION
%rows 10.9, ep/min 4.4, `BPmax` off the 12.5 wall, no ratchet;
(2) time-based `BUF_HYST_MS` is the existing deadlock-free chatter
guard; (3) the only states where the guard's travel accrues fast are
the catch-up moves, where it barely damps — low value, high
deadlock-surface (two on-hw failures). The `sync.c` (b) exemption code
stays as dormant defense-in-depth (harmless at 0.0). *Alternatives
rejected:* (a) accumulate on printer/buffer-relative travel — standalone
type-D has no continuous printer position; (b) refine the exemption to
also spare corrective entry — marginal damping (catch-up reaches the
threshold in ~ms) for continued deadlock risk; keep 0.5 — hangs sync.

**Stop smoothness is G3, not G2.** The user's "smooth print stops" goal
is governed by the deep-COMPRESSION collapse-ramp (G3), now config-
exposed and — since G1 (r7) stabilized the buffer enough to exercise the
COMPRESSION path — unblocked for on-hw tuning (§4.4). G2 never governed
stop smoothness; dropping it costs only marginal residual chatter, not
stop quality.

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

### 2026-05-19 G1/G2 provisional relay defaults

Files:
- `config.ini` + `config.ini.example`: keep
  `relay_confidence_cycles` at 8, shorten
  `relay_confidence_window_ms` to the r6-proven 1000 ms clamp floor,
  and enable `relay_min_flip_mm` at 0.5 mm as the smallest documented
  provisional anti-chatter value.
- `scripts/gen_config.py`: move the same generated defaults so missing
  config keys still produce the hardened gate and non-zero flip guard.
- `scripts/test_gen_config.py`: assert generated defaults for cycles,
  window, and min-flip so future default drift is caught.
- `openspec/changes/relay-confidence-gate-harden/tasks.md`: mark
  tasks 2.1-3.2 after validation passes.

Risk/invariant: no edit to `firmware/src/sync.c` relay branch behavior
or analyzer logic in this step. Values remain inside existing protocol
clamps (`RELAY_CONF_CYCLES` 1-64, `RELAY_CONF_WINDOW_MS` 1000-300000,
`RELAY_MIN_FLIP_MM` 0.0-100.0). These are provisional defaults pending
the hardware A/B in section 4.

### 2026-05-19 Runtime dump parity for `relay_min_flip_mm`

Files:
- `firmware/src/protocol.c`: expose `RELAY_MIN_FLIP_MM` through the
  same live `SET:`/`GET:` surface as the other relay field-tuning knobs
  and include it in `LIVE_TUNE_LOCK`.
- `scripts/flare_cmd.py`: add `RELAY_MIN_FLIP_MM` to `--dump` so live
  config snapshots include the default/field value.
- `MANUAL.md` + `TUNING.md`: correct the relay table/snippet so
  `relay_min_flip_mm` is no longer described as config-only, the
  hardened confidence-window/min-flip defaults match the current
  generator, and removed `relay_seed_rate` guidance is not copied.
- `openspec/changes/relay-confidence-gate-harden/tasks.md`: record the
  parity correction under the G1/G2 validation notes without marking
  hardware A/B tasks complete.

Risk/invariant: `RELAY_MIN_FLIP_MM` already exists in settings storage
and clamps at boot to 0.0-100.0 mm; adding runtime protocol access must
use the same clamp and must not change `settings_t`, relay control-law
branches, or analyzer behavior.

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
