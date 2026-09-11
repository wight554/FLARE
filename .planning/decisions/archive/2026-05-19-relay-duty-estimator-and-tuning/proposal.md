## Why

In Sync-Feedback Sensor type D standalone mode (`BUF_SENSOR_TYPE == 0`,
D=0), the buffer has only TENSION/COMPRESSION microswitches — zero position
information between flips — so the historical estimator collapsed and the
controller was a relaxation oscillator by construction.
`relay-buffer-control-2switch` stopped the oscillation by
*abandoning* estimation: a hand-tuned fixed NEUTRAL feed (`extruder_est_sps
* SYNC_RELAY_NEUTRAL_FRAC`, locked 4.2 baseline = **1.25**, round-2). Happy
Hare solves the same zero-info
problem without abandoning it: a two-level duty-cycle estimator that learns
effective feed from the *cadence* of switch flips (the only signal a
type-D sensor has). Adopting it removes the hand guess while keeping the
never-TENSION compression lean. Two adjacent gaps are fixed in the same
pass: the relay knobs are compile-time `#define`s (no config/SET loop, so
on-Pi tuning means recompile-per-tweak), and TUNING.md documents only the
analog flow plus a stale status token.

## What Changes

- **Relay duty-cycle estimator (firmware, runtime, volatile).** Track
  per-state filament travel between TENSION↔COMPRESSION flips; compute the
  duty-weighted effective feed `fh = dh/(dl+dh)`, `v_est = (1-fh)·v_low +
  fh·v_high`; drive the relay **NEUTRAL** target with `v_est` instead of
  the hand-tuned guess. Clamped to offline-provided `[lo, hi]` bounds. The
  existing never-TENSION compression lean (`SYNC_COMPRESSION_BIAS_FRAC` /
  the `×SYNC_RELAY_NEUTRAL_FRAC` lean) is applied **on top**, unchanged.
  The fixed `×1.25` demand path (locked 4.2 baseline) remains the fallback
  whenever the estimator is unconfident — which, in a good low-flip cycle,
  is the steady state by design (see estimator-role decision D10). At print
  start the cold-EST fallback is **seeded from the offline relay baseline**
  (D10) instead of a cold `extruder_est_sps`, fixing the 4.2 round-2
  startup-bangbang transient. TENSION→catch-up and COMPRESSION→stop branches
  are **not touched**. No flash write.
- **Motion-based anti-chatter (secondary, from HH).** Optional
  distance-hysteresis flip guard (`os_min_flip_mm` style) alongside the
  existing time-based `BUF_HYST_MS`; HH relief-fraction snap captured as a
  cleaner-formulation reference note only (analog stays no-rig).
- **Relay knobs become config keys. BREAKING (config surface).**
  `SYNC_RELAY_CATCHUP_FRAC` / `SYNC_RELAY_NEUTRAL_FRAC` and the new
  estimator bounds move from `#define` in `sync.c` to `config.ini` keys via
  `gen_config.py` / generated `tune.h`, so relay tuning is a
  config→flash (and where applicable `SET:`) loop.
- **Deterministic offline relay autotuning.** Extend `flare_analyze` (and
  capture as needed) to compute switch-flip duty statistics and emit
  recommended relay baseline + estimator `[lo, hi]` bounds into the same
  `config.ini`/schedule mechanism. Same inputs → same output;
  acceptance-gate parity with existing analyzer behavior. Happy Hare's
  online autotune-flash-save is **explicitly not** adopted.
- **TUNING.md.** Add a type-D relay (`BUF_SENSOR_TYPE == 0`, D=0)
  section: new config keys, the relay
  capture/analyze loop, and the runtime estimator. Fix the stale
  status-field token `TB` → `CB` (`TUNING.md:360`; firmware emits `CB:` at
  `protocol.c:197` — `rename-buffer-states-tension-compression` mapped
  `TB→CB`).
- **Happy-Hare polarity landmine recorded.** HH convention is
  `+1 = compression / -1 = tension`, **inverted** vs FLARE
  (`+1 = tension / -1 = compression`). Any analog port from HH (the
  `audit-sync-polarity` D4 reference) must flip every sign. Recorded
  prominently where the analog reference is cited; cross-links
  `relay-buffer-control-2switch` task 7.3.

Cross-change bookkeeping (no edits to the other change from here):
`relay-buffer-control-2switch` is **archived** (2026-05-19, 24/24); its
task **4.2** baseline landed and is the hard input here: locked pair
**`CATCHUP=1.30` / `NEUTRAL=1.25`** + round-2 `?:` log (slow/shallow/
never-TENSION/never-fault steady state). Scale caveat carried: the pair is
switch-state driven, geometry-config-independent; round-2 BP-mm was under
the pre-`align-buffer-range-vocab` half=7.8 scale (non-authoritative for
the frac/bounds). Its task **7.2** was **already decided A — neutral_creep
is intended-inert telemetry, kept computing, NOT removed** (committed
`a78d864`). This change therefore does **not** delete neutral_creep; it
adds estimator telemetry as a separate field and leaves the 7.2-A
disposition intact (see D5). 7.3 was split to the `pending-analog-rig`
change. The 6.x `×1.25` fallback (not ×1.10) is the estimator's
unconfident path and is **not** stale.

## Capabilities

### New Capabilities

- `relay-duty-estimator`: the type-D relay-law NEUTRAL feed is driven by
  a runtime duty-cycle estimator learned from switch-flip cadence, bounded
  by offline-recommended limits, with the never-TENSION compression lean
  preserved on top and a fixed-demand unconfident fallback.

### Modified Capabilities

- `sync-refactor`: the relay-mode NEUTRAL control law changes from a fixed
  hand-tuned multiplier to the bounded duty-cycle estimate; relay tuning
  knobs become config-driven; TENSION/COMPRESSION branches unchanged.
- `deterministic-tuning-workflow`: the deterministic offline analyzer is
  extended to ingest switch-flip duty statistics and emit relay baseline /
  estimator bounds, same-inputs→same-output preserved.
- `operator-tuning-guide`: TUNING.md gains a type-D relay-law tuning
  section and the stale `TB`→`CB` status token is corrected.

## Impact

- Firmware: `firmware/src/sync.c` (relay NEUTRAL branch + estimator state),
  `firmware/include/config.h`, generated `tune.h`; `protocol.c` only if a
  duty/estimator telemetry field is added. TENSION→catch-up,
  COMPRESSION→stop, and the type-P analog (`BUF_SENSOR_TYPE != 0`, P=1)
  path are
  untouched / byte-identical.
- Host: `scripts/gen_config.py` (new config keys), `scripts/flare_analyze.py`
  (relay duty stats + bound emission), related capture/tests; pure stdlib +
  pyserial preserved.
- Config: new `config.ini` keys for relay catch-up / NEUTRAL / estimator
  bounds; legacy `#define`s removed (active-dev rename policy: stale keys
  ignored, no migration guide).
- Docs/specs: `TUNING.md`; specs `sync-refactor`,
  `deterministic-tuning-workflow`, `operator-tuning-guide`, new
  `relay-duty-estimator`.
- Dependency: `relay-buffer-control-2switch` **archived**; its 4.2 locked
  baseline (`1.30/1.25` + round-2 log) is the input. 7.2 already decided A
  (honored here, not reopened). 7.3 → `pending-analog-rig`.
- Out of scope: no online flash-save; no change to the compression-lean
  policy; no blind analog code (analog stays `pending-analog-rig`,
  HH-modelled spec only); no change to TENSION→catch-up or
  COMPRESSION→stop branches.
- **Accepted limitation (D10):** the 4.2 round-2 end-of-print COMPRESSION
  `SYNC_MIN`-floor grind is **not** addressed here — fixing it requires
  touching the COMPRESSION branch, which D1 explicitly forbids (minimal
  blast radius). It is a print-tail artifact at draw≈0 already caught by
  the existing RELIEF_PAUSE/BUF_STAB auto-stop with no print-quality
  impact. Documented + cross-linked, not silently dropped; a future
  dedicated change may revisit if it ever shows real harm.
