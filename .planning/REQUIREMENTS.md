# Requirements: FLARE

## Overview

Durable firmware and host requirements extracted from the 37 active technical specifications.

## Active Requirements

### REQ-acceptance-gate-parity-shared-recommenda
- **Source**: `.planning/specs/acceptance-gate-parity/spec.md`
- **Description**: The gate SHALL compare per-run recommendations from the same state-aware path as the patch.

### REQ-acceptance-gate-parity-backward-compatib
- **Source**: `.planning/specs/acceptance-gate-parity/spec.md`
- **Description**: `compute_recommendations` SHALL retain dictionary shape and semantics for existing callers.

### REQ-acceptance-gate-parity-run-classificatio
- **Source**: `.planning/specs/acceptance-gate-parity/spec.md`
- **Description**: The system SHALL classify runs (comparable or skipped) before checking consistency deltas.

### REQ-acceptance-gate-parity-diagnostic-visibi
- **Source**: `.planning/specs/acceptance-gate-parity/spec.md`
- **Description**: The generated patch MUST include per-run estimates regardless of gate outcome.

### REQ-acceptance-gate-parity-contributor-mass-
- **Source**: `.planning/specs/acceptance-gate-parity/spec.md`
- **Description**: The acceptance gate SHALL FAIL only on contributor mass, and WARN on raw row coverage.

### REQ-acceptance-gate-parity-placeholder-telem
- **Source**: `.planning/specs/acceptance-gate-parity/spec.md`
- **Description**: The analyzer MUST mark telemetry counters as pending until real log parsing exists.

### REQ-acceptance-gate-semantics-separate-rejec
- **Source**: `.planning/specs/acceptance-gate-semantics/spec.md`
- **Description**: The gate SHALL FAIL ONLY on reliability issues; stale config and incomplete soak are warnings.

### REQ-acceptance-gate-semantics-floored-denomi
- **Source**: `.planning/specs/acceptance-gate-semantics/spec.md`
- **Description**: The system SHALL avoid penalizing the operator for many immature buckets in mass calculation.

### REQ-acceptance-gate-semantics-mass-gray-band
- **Source**: `.planning/specs/acceptance-gate-semantics/spec.md`
- **Description**: The gate SHALL issue a WARNING when mass is between PASS and FAIL thresholds.

### REQ-acceptance-gate-semantics-sigma-ceiling
- **Source**: `.planning/specs/acceptance-gate-semantics/spec.md`
- **Description**: The analyzer SHALL warn and recommend correction when BP sigma is between reference and 5.0 mm.

### REQ-acceptance-gate-semantics-soak-maturity
- **Source**: `.planning/specs/acceptance-gate-semantics/spec.md`
- **Description**: The system MUST report run-count and duration without hiding stable recommendations.

### REQ-acceptance-gate-semantics-glob-input
- **Source**: `.planning/specs/acceptance-gate-semantics/spec.md`
- **Description**: The analyzer SHALL support shell-expanded CSV groups using the `--in` flag.

### REQ-analyzer-rigor-relative-noise-gate
- **Source**: `.planning/specs/analyzer-rigor/spec.md`
- **Description**: Bucket lock acceptability SHALL be derived from `sigma / rate` after warmup.

### REQ-analyzer-rigor-safe-mode-enforcement
- **Source**: `.planning/specs/analyzer-rigor/spec.md`
- **Description**: Safe mode MUST refuse recommendations if zero buckets are LOCKED in the state.

### REQ-analyzer-rigor-explicit-bootstrap-paths
- **Source**: `.planning/specs/analyzer-rigor/spec.md`
- **Description**: Aggressive and force modes SHALL allow pre-lock estimates with explicit warnings.

### REQ-analyzer-rigor-precision-weighted-recomm
- **Source**: `.planning/specs/analyzer-rigor/spec.md`
- **Description**: Recommendations SHALL use precision-weighted qualifying set (N / Var) with trimmed tails.

### REQ-analyzer-rigor-bp-derived-sigma
- **Source**: `.planning/specs/analyzer-rigor/spec.md`
- **Description**: The analyzer SHALL derive `buf_variance_blend_ref_mm` from BP samples, NOT BL field.

### REQ-analyzer-rigor-contributors-visibility
- **Source**: `.planning/specs/analyzer-rigor/spec.md`
- **Description**: The generated patch MUST include a contributor evidence block for learned values.

### REQ-bucket-locking-bucket-schema-4-shall-pre
- **Source**: `.planning/specs/bucket-locking/spec.md`
- **Description**: Bucket state schema 4 SHALL include scalar residual-statistics fields used by
the lock/unlock algorithm without storing per-sample histories.

### REQ-bucket-locking-schema-3-state-shall-migr
- **Source**: `.planning/specs/bucket-locking/spec.md`
- **Description**: The schema 3 to 4 migration SHALL preserve all existing bucket and `_meta`
content and keep LOCKED buckets locked.

### REQ-bucket-locking-unlocking-shall-use-three
- **Source**: `.planning/specs/bucket-locking/spec.md`
- **Description**: LOCKED buckets SHALL unlock only through the catastrophic, streak, or drift
channels defined by residual-aware logic.

### REQ-bucket-locking-locking-shall-be-noise-ga
- **Source**: `.planning/specs/bucket-locking/spec.md`
- **Description**: A bucket SHALL NOT enter or re-enter LOCKED state until required sample evidence,
noise criteria, and minimum locked dwell behavior are satisfied.

### REQ-bucket-locking-verbose-state-info-shall-
- **Source**: `.planning/specs/bucket-locking/spec.md`
- **Description**: Verbose tuner state output SHALL include residual and unlock diagnostics needed
to understand chatter, dwell, and lock decisions.

### REQ-buffer-geometry-vocabulary-buffer-geomet
- **Source**: `.planning/specs/buffer-geometry-vocabulary/spec.md`
- **Description**: Buffer geometry SHALL be configured through exactly two full-range tunables
with semantics aligned to Happy Hare / EMU Sync:

### REQ-buffer-geometry-vocabulary-full-range-to
- **Source**: `.planning/specs/buffer-geometry-vocabulary/spec.md`
- **Description**: The firmware SHALL convert the full-range `buf_switch_span_mm` to the
internal half-based geometry exactly once at the value-ingest boundary
(config ingest and the serial SET handler) as
`half = buf_switch_span_mm / 2`, and SHALL NOT apply the conversion anywhere
else. `buf_max_travel_mm` SHALL map 1:1 to the internal total-travel value
with no unit conversion. Internal `sync.c` geometry remains half-based;
only the *source* of the half value changes.

### REQ-buffer-geometry-vocabulary-emu-sync-defa
- **Source**: `.planning/specs/buffer-geometry-vocabulary/spec.md`
- **Description**: The compiled defaults SHALL be the EMU Sync reference values:
`buf_switch_span_mm = 10` and `buf_max_travel_mm = 25`. These replace the
prior `buf_half_travel_mm = 7.8` (an untuned calibration artifact) and
`buf_size_mm = 22`.

### REQ-buffer-geometry-vocabulary-switch-span-a
- **Source**: `.planning/specs/buffer-geometry-vocabulary/spec.md`
- **Description**: The two tunables SHALL preserve, in full-range terms, the clamp relationship
that existed in half-range terms. `buf_switch_span_mm` SHALL be clamped to
`[2.0, buf_max_travel_mm]`. `buf_max_travel_mm` SHALL be clamped to
`[10, 1000]`. Setting `buf_max_travel_mm` SHALL re-clamp `buf_switch_span_mm`
so the derived internal half never exceeds `buf_max_travel_mm / 2`.

### REQ-buffer-geometry-vocabulary-serial-vocabu
- **Source**: `.planning/specs/buffer-geometry-vocabulary/spec.md`
- **Description**: The serial SET/GET tokens SHALL be `BUF_SWITCH_SPAN` and `BUF_MAX_TRAVEL`,
carrying full-range values. The legacy tokens `BUF_HALF_TRAVEL`,
`BUF_TRAVEL`, and `BUF_SIZE` SHALL be removed with no compatibility alias.

### REQ-buffer-geometry-vocabulary-type-p-analog
- **Source**: `.planning/specs/buffer-geometry-vocabulary/spec.md`
- **Description**: The full→half ingest change SHALL NOT alter type-P (analog,
`BUF_SENSOR_TYPE != 0`) behavior. Only the *source* of the internal
half value changes; the analog consumers of
`buf_physical_half_travel_mm()` / `buf_threshold_mm()` SHALL behave
identically for an equivalent geometry.

### REQ-buffer-state-lock-bl-command-surface
- **Source**: `.planning/specs/buffer-state-lock/spec.md`
- **Description**: The firmware SHALL accept a host `BL:<state>` command that arms the active
lane to drive the buffer to the requested extreme and lock there, where
`<state>` is `T` (tension) or `C` (compression). `BL` with no argument
SHALL be treated as `BL:T`. The firmware SHALL expose the current lock arm
in status as `BL:T`, `BL:C`, or `BL:0` (disarmed).

### REQ-buffer-state-lock-bounded-half-travel-pr
- **Source**: `.planning/specs/buffer-state-lock/spec.md`
- **Description**: On `BL` the firmware SHALL drive the active lane toward the requested extreme
and stop as soon as either the corresponding raw buffer state
(`BUF_TENSION` or `BUF_COMPRESSION`) is reached or `BUF_MAX_TRAVEL_MM / 2`
mm of MMU travel is completed, whichever comes first. The prime MUST NOT
exceed the half-travel cap.

### REQ-buffer-state-lock-locked-hold-contract
- **Source**: `.planning/specs/buffer-state-lock/spec.md`
- **Description**: While locked the firmware SHALL energize the active lane motor with zero
commanded velocity, MUST NOT issue any closed-loop feed corrections from
the buffer state, and SHALL preserve estimator, drift observer, sigma,
confidence, and reserve integrator state.

### REQ-buffer-state-lock-lock-break-on-external
- **Source**: `.planning/specs/buffer-state-lock/spec.md`
- **Description**: The firmware SHALL treat any departure of the raw buffer state from the
locked extreme as a non-MMU (external) force lock-break and MUST transition
to the catch sub-state on the first raw edge, without waiting for the
`BUF_HYST_MS` debounce window.

### REQ-buffer-state-lock-instant-slam-catch-wit
- **Source**: `.planning/specs/buffer-state-lock/spec.md`
- **Description**: On lock-break the firmware SHALL drive the active lane in the mirror
direction (retract for `BL:T` break, feed for `BL:C` break) at
`GLOBAL_MAX_SPS` via an instant `current_sps = target`
write, bypassing `SYNC_RAMP_UP_SPS`. The catch MUST tolerate transient
over-drive back toward the armed extreme as a safe recoverable direction
and SHALL NOT throttle the catch to avoid it.

### REQ-buffer-state-lock-manual-release-via-bs
- **Source**: `.planning/specs/buffer-state-lock/spec.md`
- **Description**: The host `BS` (buffer stabilize) command SHALL release any active `BL`
lock or catch immediately, run normal buffer stabilization, and return the
controller to `SYNC_OFF`.

### REQ-buffer-state-lock-locked-state-watchdog
- **Source**: `.planning/specs/buffer-state-lock/spec.md`
- **Description**: The firmware SHALL emit `EV:BL:TIMEOUT` and auto-release the lock if no
lock-break, no `BS`, and no other release happens within a configurable
timeout (default 30 seconds) of entering the locked sub-state.

### REQ-calibration-workflow-calibration-shall-b
- **Source**: `.planning/specs/calibration-workflow/spec.md`
- **Description**: The calibration workflow SHALL collect evidence without mutating firmware
settings unless the operator passes explicit write flags.

### REQ-calibration-workflow-state-schema-migrat
- **Source**: `.planning/specs/calibration-workflow/spec.md`
- **Description**: Bucket state migrations SHALL be registered in a migration table and applied in
sequence without rewriting the migration loop for each new schema.

### REQ-calibration-workflow-bucket-locking-shal
- **Source**: `.planning/specs/calibration-workflow/spec.md`
- **Description**: A bucket SHALL lock only after satisfying cumulative evidence requirements for
samples, runs, layers, stability, and motion time.

### REQ-calibration-workflow-analyzer-patches-sh
- **Source**: `.planning/specs/calibration-workflow/spec.md`
- **Description**: `scripts/flare_analyze.py` SHALL emit review patches that preserve current values
for unavailable recommendations and label recommendation confidence.

### REQ-calibration-workflow-long-running-daemon
- **Source**: `.planning/specs/calibration-workflow/spec.md`
- **Description**: Daemon-mode calibration SHALL tolerate stale buckets and repeated runs without
allowing stale evidence to dominate current recommendations.

### REQ-calibration-workflow-firmware-live-learn
- **Source**: `.planning/specs/calibration-workflow/spec.md`
- **Description**: The firmware live baseline tier SHALL be ephemeral, up-only, and gated to
`SYNC_ACTIVE`, and SHALL never write persistent state. Persistent baseline
and compression-bias values SHALL change only through the reviewed offline
analyzer + config flash path.

### REQ-calibration-workflow-deterministic-dual-
- **Source**: `.planning/specs/calibration-workflow/spec.md`
- **Description**: `flare_analyze.py` SHALL provide an explicit two-profile baseline mode that
takes a fastest-cubic-flow capture and a slowest-cubic-flow capture and
derives exactly one baseline value. The derivation SHALL be a pure function
of the input rows: stable bucket ordering, sample-count weighting only, no
wall-clock recency weighting, and a fixed rounding rule. Identical input
captures SHALL produce a byte-identical baseline regardless of when the
analyzer runs. The existing recency-weighted config-patch path SHALL remain
unchanged and selected separately.

### REQ-calibration-workflow-offline-analyzer-re
- **Source**: `.planning/specs/calibration-workflow/spec.md`
- **Description**: The deterministic two-profile baseline SHALL be the only value written to
persistent memory for the baseline. The live tuner and the recommendation
script SHALL NOT write the persistent baseline; the live firmware baseline
SHALL remain ephemeral, up-only, and non-persistent.

### REQ-calibration-workflow-analyzer-emits-a-de
- **Source**: `.planning/specs/calibration-workflow/spec.md`
- **Description**: The offline analyzer SHALL be able to emit a flow-keyed schedule (multiple
flow→{baseline, bias} breakpoints) from the existing per-`(feature,
v_fil_bin)` velocity buckets, in addition to the scalar baseline. The
schedule derivation SHALL reuse the deterministic dual-profile reducer and
existing maturity gates, SHALL be bounded by the configured breakpoint
cap, and SHALL remain the sole persistent authority for baseline/bias.

### REQ-code-style-standard-enforced-format-conf
- **Source**: `.planning/specs/code-style-standard/spec.md`
- **Description**: Repo SHALL carry `.clang-format`, `.clang-tidy`, and `.editorconfig` at root, and
all firmware C sources (`firmware/src/*.c`, `firmware/include/*.h`) SHALL conform.

### REQ-code-style-standard-local-lint-invocatio
- **Source**: `.planning/specs/code-style-standard/spec.md`
- **Description**: `STYLE.md` SHALL document the local `clang-format` and `clang-tidy` invocation, and
the `clang-tidy` config SHALL enable the project check set. No CI lint gate is
required in this change.

### REQ-code-style-standard-naming-conventions
- **Source**: `.planning/specs/code-style-standard/spec.md`
- **Description**: Identifiers SHALL be intention-revealing per `STYLE.md`. Domain vocabulary terms
(`sps`, `mm`, `tmc`, `buf`, `psf`, `adc`, `pio`) MAY remain abbreviated and SHALL
be defined in `STYLE.md`. Single-letter and opaque identifiers SHALL NOT be used
for variables with non-trivial scope.

### REQ-code-style-standard-file-and-function-si
- **Source**: `.planning/specs/code-style-standard/spec.md`
- **Description**: `STYLE.md` SHALL state translation-unit and function size norms, and oversized
units SHALL be split into cohesive modules and oversized functions extracted.

### REQ-code-style-standard-magic-number-policy
- **Source**: `.planning/specs/code-style-standard/spec.md`
- **Description**: Non-trivial numeric literals in firmware SHALL be replaced by named constants or
documented tunables; values that are runtime-tunable SHALL follow the existing
`config.ini` → `tune.h` → `CONF_*` path.

### REQ-code-style-standard-comprehension-commen
- **Source**: `.planning/specs/code-style-standard/spec.md`
- **Description**: Each firmware `.c` SHALL carry a file-header doc-block stating what the unit owns,
its core algorithm, and a pointer to the relevant `BEHAVIOR.md`/spec section. Inline
comments SHALL explain why (intent, invariants, hardware quirks, edge cases), not
narrate obvious code. Every state machine SHALL carry a state-transition map comment.

### REQ-code-style-standard-doc-comment-format-a
- **Source**: `.planning/specs/code-style-standard/spec.md`
- **Description**: `STYLE.md` SHALL define the function/struct/macro doc-comment format, and existing
rationale comments SHALL be preserved.

### REQ-code-style-standard-behavior-preserving-
- **Source**: `.planning/specs/code-style-standard/spec.md`
- **Description**: All overhaul edits SHALL be behavior-preserving: no serial protocol, config key,
tunable, or runtime-behavior change, and the build SHALL pass before every commit.

### REQ-code-style-standard-shared-constants-are
- **Source**: `.planning/specs/code-style-standard/spec.md`
- **Description**: A numeric constant or small helper used by more than one translation unit SHALL be
defined once in a shared header, not copied per `.c`. Identical constants SHALL NOT
be redefined with divergent style (`#define` vs `static const`) across units.

### REQ-code-style-standard-global-naming-conven
- **Source**: `.planning/specs/code-style-standard/spec.md`
- **Description**: All firmware global variables SHALL be named `g_lower_case`, including config-backed
runtime tunables, and the `g_` prefix SHALL be enforced by `.clang-tidy` (no blanket
`GlobalVariableIgnoredRegexp` exemption). `STYLE.md` SHALL document this and SHALL state
that tunable-vs-state is distinguished by the `controller_shared.h` tunables section,
the `settings_t` mirror, and the `SET:`/`GET:` surface — not by casing. Protocol param
names and `config.ini` keys remain `UPPER_CASE` strings and are not affected by the
identifier naming.

### REQ-config-surface-tiers-configuration-param
- **Source**: `.planning/specs/config-surface-tiers/spec.md`
- **Description**: Every configuration parameter SHALL belong to exactly one tier, and its storage
and exposure SHALL follow that tier:

### REQ-config-surface-tiers-internal-constants-
- **Source**: `.planning/specs/config-surface-tiers/spec.md`
- **Description**: A T3 internal constant SHALL NOT appear in `config.ini.example`, in
`settings_t`, or in the release-build `SET:` / `GET:` handlers; a
unit-independent T3 constant additionally SHALL live in `tune_internal.h` and
not in `gen_config.py` `DEFAULTS`. Changing a T3 constant SHALL be a source edit
+ recompile and SHALL NOT require a `SETTINGS_VERSION` bump.

### REQ-config-surface-tiers-a-dev-build-may-exp
- **Source**: `.planning/specs/config-surface-tiers/spec.md`
- **Description**: A `FLARE_DEV_TUNING` build flag SHALL gate optional re-exposure of T3 constants
as `SET:`-only, non-persisted runtime overrides for bench experimentation. The
flag SHALL be undefined in release builds, and a dev override SHALL NOT survive a
reboot.

### REQ-config-surface-tiers-demoted-keys-are-mi
- **Source**: `.planning/specs/config-surface-tiers/spec.md`
- **Description**: A demoted (T3) parameter SHALL migrate gracefully: an existing `config.ini` that
still sets it SHALL build with a warning rather than a hard error, and the device
config dump SHALL NOT emit the demoted key.

### REQ-cross-platform-script-tooling-all-operat
- **Source**: `.planning/specs/cross-platform-script-tooling/spec.md`
- **Description**: Every operational script in `scripts/` SHALL be implemented in Python using only stdlib + pyserial. Operational `.sh` files SHALL NOT exist.

### REQ-cross-platform-script-tooling-linux-and-
- **Source**: `.planning/specs/cross-platform-script-tooling/spec.md`
- **Description**: Each ported script SHALL produce identical functional behavior on Linux (Raspberry Pi / Debian / Ubuntu / Fedora) and macOS. Platform-specific operations (device discovery, mount, `diskutil`) SHALL be branched via `platform.system()`.

### REQ-cross-platform-script-tooling-no-inline-
- **Source**: `.planning/specs/cross-platform-script-tooling/spec.md`
- **Description**: Scripts SHALL NOT embed Python code inside bash heredocs. Serial I/O, device communication, and other Python operations SHALL use direct imports from shared modules (`serial_utils`, `path_utils`).

### REQ-cross-platform-script-tooling-color-outp
- **Source**: `.planning/specs/cross-platform-script-tooling/spec.md`
- **Description**: Scripts with colored terminal output SHALL detect non-interactive terminals and `NO_COLOR` environment variable, disabling ANSI escape sequences when appropriate.

### REQ-cutter-feed-timeout-cutter-feed-timeout-
- **Source**: `.planning/specs/cutter-feed-timeout/spec.md`
- **Description**: `CUT_TIMEOUT_FEED_MS` — the per-phase motor-feed safety timeout used in `CUT_FEED_WAIT` — SHALL be a runtime-tunable parameter sourced from `config.ini` (`cut_feed_timeout_ms`), persisted in flash, and accessible via `GET:CUT_FEED_MS` / `SET:CUT_FEED_MS` serial protocol commands.

### REQ-cutter-feed-timeout-cutter-settle-timeou
- **Source**: `.planning/specs/cutter-feed-timeout/spec.md`
- **Description**: `CUT_TIMEOUT_SETTLE_MS` — the per-phase servo-settle safety timeout used in `CUT_OPEN_WAIT`, `CUT_CLOSE_WAIT`, and `CUT_REOPEN_WAIT` — SHALL be a runtime-tunable parameter sourced from `config.ini` (`cut_settle_timeout_ms`), persisted in flash, and accessible via `GET:CUT_SETTLE_MS` / `SET:CUT_SETTLE_MS` serial protocol commands.

### REQ-daemon-klipper-mirror-delta-set-mmu-mirr
- **Source**: `.planning/specs/daemon-klipper-mirror/spec.md`
- **Description**: The daemon SHALL push only the `SET_MMU` fields whose formatted value changed since the
last successful push, relying on `cmd_SET_MMU` keeping the current value for any absent
param. The resulting Klipper mock state SHALL be identical to a full push.

### REQ-daemon-klipper-mirror-full-resync-recove
- **Source**: `.planning/specs/daemon-klipper-mirror/spec.md`
- **Description**: The daemon SHALL emit a full `SET_MMU` (all fields) on the first push and on a
board-online transition. On the periodic resync tick the daemon SHALL read the Klipper
`mmu` object and emit a full `SET_MMU` only when the reported mock state diverges from the
desired field set; when they match the daemon SHALL emit nothing that tick. A restarted
Klipper/Moonraker SHALL recover complete state within one tick of the divergence becoming
observable.

### REQ-daemon-klipper-mirror-gate-state-diagnos
- **Source**: `.planning/specs/daemon-klipper-mirror/spec.md`
- **Description**: The daemon SHALL, when `FLARE_GATE_DEBUG` is set, log the gate-relevant mirror inputs
(`active_gate`, per-lane IN/OUT, computed gate status, toolchange state) when they change.
When the flag is unset there SHALL be no behavior or output change.

### REQ-daemon-klipper-mirror-host-busy-backpres
- **Source**: `.planning/specs/daemon-klipper-mirror/spec.md`
- **Description**: The daemon SHALL NOT queue mirror traffic behind a long-running blocking Klipper command.
When a gcode/script push (`SET_MMU`, `MMU_GATE_MAP`, or `_FLARE_SYNC_BOARD`) fails because
the Klipper gcode lock is busy, the daemon SHALL enter a host-busy state, suppress all
further gcode/script pushes, and poll a lock-free `objects/query` for `idle_timeout` until
`idle_timeout.state` reports Idle/Ready before resuming pushes. The daemon SHALL
distinguish host-busy (gcode lock held, host reachable) from Moonraker-offline; the
offline path retains its existing backoff and SHALL NOT be replaced by the busy path.

### REQ-deterministic-tuning-workflow-two-profil
- **Source**: `.planning/specs/deterministic-tuning-workflow/spec.md`
- **Description**: The tuning workflow SHALL be: run the same model in two profiles — fastest
cubic flow and slowest cubic flow — using the existing tuner and marker
scripts, then run the analyzer's deterministic two-profile mode to derive
one baseline, then write that baseline to config and re-flash. The procedure
SHALL be documented as a fixed, ordered sequence with no run-to-run
variability in the resulting baseline.

### REQ-deterministic-tuning-workflow-live-tuner
- **Source**: `.planning/specs/deterministic-tuning-workflow/spec.md`
- **Description**: `scripts/flare_baseline_recommender.py` SHALL be a host-only script
(stdlib + pyserial only) that reads the device tty, tracks the live tuner's
drift signal across a print, and at end-of-print reports a suggested
persistent `baseline_sps` with a supporting drift summary. It SHALL be
observe-only: no `SET` or `SV` writes. Given an identical captured input
stream it SHALL produce an identical recommendation (replayable for test).

### REQ-deterministic-tuning-workflow-tuning-wor
- **Source**: `.planning/specs/deterministic-tuning-workflow/spec.md`
- **Description**: The documented workflow SHALL NOT require interpreting different results
across repeated runs. Any value an operator is asked to commit SHALL be
reproducible from the captured inputs alone, independent of analysis time
or machine.

### REQ-filament-bypass-local-filament-bypass-st
- **Source**: `.planning/specs/filament-bypass/spec.md`
- **Description**: The host daemon and Klipper mock SHALL expose a unified `bypass` boolean and status field, mapped to Happy Hare gate/tool sentinel `-2`.

### REQ-filament-bypass-single-sensor-bypass-tel
- **Source**: `.planning/specs/filament-bypass/spec.md`
- **Description**: Under bypass mode, the system SHALL report exactly one active sensor: the toolhead sensor (`TS`). All other gate, pre-gate, and combiner sensors SHALL be forced to inactive.

### REQ-filament-bypass-manual-feed-and-autoload
- **Source**: `.planning/specs/filament-bypass/spec.md`
- **Description**: Filament SHALL be manually fed through the bypass lane without MMU drive assistance until it triggers the toolhead sensor, which SHALL immediately and automatically invoke the autoload sequence.

### REQ-filament-bypass-mmu-free-extruder-only-a
- **Source**: `.planning/specs/filament-bypass/spec.md`
- **Description**: Under bypass mode, all load and unload sequences SHALL ignore and completely suppress any physical MMU lane motor serial commands, executing strictly as toolhead extruder-only operations.

### REQ-filament-bypass-suppressed-eject-under-b
- **Source**: `.planning/specs/filament-bypass/spec.md`
- **Description**: All MMU lane eject procedures SHALL be skipped and safely suppressed under bypass mode, with no serial command executed.

### REQ-flow-keyed-schedule-versioned-bounded-fl
- **Source**: `.planning/specs/flow-keyed-schedule/spec.md`
- **Description**: The system SHALL define a versioned schedule table mapping estimated flow
to `{baseline_sps, compression_bias_frac}`. The table SHALL be a strictly
increasing-in-flow, sorted array bounded by a config-tunable maximum
breakpoint count. The format SHALL be additive: existing scalar
`baseline_sps` / `compression_bias_frac` config keys SHALL remain valid.

### REQ-flow-keyed-schedule-degenerate-single-po
- **Source**: `.planning/specs/flow-keyed-schedule/spec.md`
- **Description**: A length-1 schedule SHALL produce, for every flow value, exactly the
scalar `baseline_sps` and the milli-resolution `compression_bias_frac` it
was synthesized from. Bias fractions that are already aligned to integer
milli SHALL be exact; other bias fractions SHALL differ by no more than
0.0005 absolute bias after milli quantization. Firmware behavior with a
length-1 schedule SHALL match pre-change scalar behavior within that
milli-resolution bound.

### REQ-flow-keyed-schedule-firmware-interpolate
- **Source**: `.planning/specs/flow-keyed-schedule/spec.md`
- **Description**: The firmware SHALL derive the active baseline and compression-bias by
clamped linear interpolation of the schedule against the live
`extruder_est_sps`, with no extrapolation beyond the first/last
breakpoint. The flow key SHALL be the firmware's own `extruder_est_sps`
only; no host, Klipper, or encoder input SHALL be used. Interpolation
SHALL be float-light and bounded by the breakpoint count.

### REQ-flow-keyed-schedule-schedule-emission-is
- **Source**: `.planning/specs/flow-keyed-schedule/spec.md`
- **Description**: Identical bucket inputs SHALL produce a byte-identical schedule table,
independent of analysis wall-clock time or machine. The emission SHALL
reuse the deterministic dual-profile reducer and `BIAS_SAFE_MIN/MAX`
clamps and SHALL NOT use wall-clock recency weighting.

### REQ-klipper-integration-host-serial-control
- **Source**: `.planning/specs/klipper-integration/spec.md`
- **Description**: The Klipper host MUST interact with FLARE via single-command CDC serial transactions.

### REQ-klipper-integration-motion-tracking-side
- **Source**: `.planning/specs/klipper-integration/spec.md`
- **Description**: The sidecar (`--uds`) SHALL track Klipper's print state and forward speed events to FLARE.

### REQ-klipper-integration-macro-orchestration
- **Source**: `.planning/specs/klipper-integration/spec.md`
- **Description**: Toolchange macros (`_FLARE_CHANGE_LANE` / `T1` / `T2`) SHALL coordinate the
extruder, MMU, and toolhead state.

### REQ-klipper-integration-reusable-toolhead-un
- **Source**: `.planning/specs/klipper-integration/spec.md`
- **Description**: The include SHALL provide a standalone `FLARE_UNLOAD_TOOLHEAD` macro.

### REQ-klipper-integration-dashboard-load-and-e
- **Source**: `.planning/specs/klipper-integration/spec.md`
- **Description**: Dashboard `MMU_LOAD` and `MMU_EJECT` commands SHALL use the currently selected
gate, not only the board's active lane.

### REQ-klipper-integration-toolhead-sensor-opti
- **Source**: `.planning/specs/klipper-integration/spec.md`
- **Description**: `TC:` load completion SHALL NOT require an explicit host `TS:1` command.

### REQ-klipper-integration-klipper-md-scope-is-
- **Source**: `.planning/specs/klipper-integration/spec.md`
- **Description**: KLIPPER.md SHALL cover: serial port setup, shell command helper,
toolhead sensor wiring, reference to `flare_mmu.cfg`, and the
troubleshooting table. It SHALL NOT contain buffer sync tuning,
calibration print workflows, gcode_marker usage, or telemetry/analyzer
instructions — those belong exclusively in `TUNING.md`.

### REQ-klipper-integration-toolhead-sensor-sect
- **Source**: `.planning/specs/klipper-integration/spec.md`
- **Description**: KLIPPER.md SHALL document the physical sensor as the primary path.
The buffer-geometry fallback (TS_BUF_MS) SHALL appear as a brief note
explaining it is automatic — not as a parallel "Option B" requiring
user configuration.

### REQ-klipper-integration-automatic-bypass-too
- **Source**: `.planning/specs/klipper-integration/spec.md`
- **Description**: When the printer is in bypass mode and filament is manually inserted into the extruder entrance, Klipper macros SHALL automatically trigger the toolhead filament load sequence upon toolhead sensor trigger (insert edge).

### REQ-klipper-integration-slicer-toolchange-by
- **Source**: `.planning/specs/klipper-integration/spec.md`
- **Description**: The `MMU_CHANGE_TOOL` command SHALL gracefully accept and handle bypass sentinel values `-2` for tool or gate transitions, allowing seamless slicer-generated or UI-driven toolchanges to the bypass gate.

### REQ-klipper-mmu-config-single-file-klipper-m
- **Source**: `.planning/specs/klipper-mmu-config/spec.md`
- **Description**: `klipper/flare_mmu.cfg` SHALL provide a complete Klipper MMU integration
that users can activate with a single `[include flare_mmu.cfg]` line in
`printer.cfg`, with no other macro files required.

### REQ-klipper-mmu-config-variables-block-with-
- **Source**: `.planning/specs/klipper-mmu-config/spec.md`
- **Description**: `[gcode_macro _FLARE_VARS]` SHALL expose all user-configurable distances
using the same names as the LH-Stinger Pico MMU wiki
(`dist_sensor_to_extruder`, `dist_filament_park`,
`dist_extruder_to_meltzone`) and SHALL add `dist_meltzone_to_nozzle_tip` for
the hotend length needed by FLARE's tip-forming MMU assist.

### REQ-klipper-mmu-config-tip-forming-macro-wit
- **Source**: `.planning/specs/klipper-mmu-config/spec.md`
- **Description**: `_FLARE_TIP_FORMING` SHALL implement the full tip forming sequence
(post-pause push, cooldown pull, optional secondary moves, optional dip,
final fast retract to park position) reading parameters from
`_FLARE_TIP_FORMING_DEFAULTS`.

### REQ-klipper-mmu-config-load-hotend-macro-wit
- **Source**: `.planning/specs/klipper-mmu-config/spec.md`
- **Description**: `_FLARE_LOAD_HOTEND` SHALL advance filament from the park position to the
meltzone in three stages (50% fast / 25% normal / 25% slow) and call
`_FLARE_PURGE` when `purge_len` is greater than zero.

### REQ-klipper-mmu-config-purge-helper-is-simpl
- **Source**: `.planning/specs/klipper-mmu-config/spec.md`
- **Description**: `_FLARE_PURGE` SHALL own purge extrusion separately from `_FLARE_LOAD_HOTEND`.
It SHALL implement the simple upstream `_SP_PURGE` core shape: purge the
requested relative extrusion amount at `purge_speed`, then perform a small
0.4 mm retract. It SHALL call `_FLARE_HEAT_HOTEND` before purge extrusion and
call an empty `_FLARE_PARK` hook for user-provided park macros. Purge chute
parking, blob splitting, and brush moves SHALL be left to user-provided
`_FLARE_PARK` customization or wrapper macros.

### REQ-klipper-mmu-config-manual-load-and-eject
- **Source**: `.planning/specs/klipper-mmu-config/spec.md`
- **Description**: `FLARE_LOAD` and `FLARE_EJECT` SHALL preserve active-lane behavior when called
without `LANE`, and SHALL target a selected lane when `LANE=1` or `LANE=2` is
provided.

### REQ-klipper-mmu-config-toolchange-macro-with
- **Source**: `.planning/specs/klipper-mmu-config/spec.md`
- **Description**: `_FLARE_CHANGE_LANE` SHALL execute the full toolchange sequence: tip forming
with an ignore-buffer FLARE `MV:` retract → gear retract (derived) →
nonblocking `TC:` → toolhead-sensor-gated PICKUP → load hotend.
Gear retract distance SHALL be computed as
`dist_filament_park + dist_sensor_to_extruder + 5` with no separate
variable. Gear retract speed SHALL use `_FLARE_VARS.speed_hub_to_extruder`
converted to Klipper feedrate (`* 60`).

### REQ-klipper-mmu-config-boot-delayed-gcode-se
- **Source**: `.planning/specs/klipper-mmu-config/spec.md`
- **Description**: `[delayed_gcode _FLARE_BOOT]` SHALL send `SET:RELOAD_MODE:{enable_reload}`
to FLARE on every Klipper start without `SV:`, so FLARE reverts to
persisted flash default when running standalone.

### REQ-klipper-mmu-config-tip-forming-test-macr
- **Source**: `.planning/specs/klipper-mmu-config/spec.md`
- **Description**: `FLARE_TEST_TIP_FORMING` SHALL allow manual tip quality testing without
a full toolchange by loading the hotend, simulating a print pause, running
tip forming, and retracting for inspection. It SHALL accept SP-style
tip-forming override parameters and write them into
`_FLARE_TIP_FORMING_DEFAULTS` before running `_FLARE_TIP_FORMING`.

### REQ-klipper-mmu-config-removed-development-m
- **Source**: `.planning/specs/klipper-mmu-config/spec.md`
- **Description**: `FLARE_CUT`, `FLARE_CUT_BARE`, and `FLARE_CUT_TEST` SHALL NOT be present in
`flare_mmu.cfg`. The cutter cycle is driven by the firmware toolchange (`TC:`),
so no standalone Klipper cut macro is needed.

### REQ-klipper-mmu-config-preload-macro-routes-
- **Source**: `.planning/specs/klipper-mmu-config/spec.md`
- **Description**: `FLARE_PRELOAD` SHALL advance a selected lane to its gate (OUT) without loading
the toolhead. With `LANE=1` or `LANE=2` it SHALL send `T:{lane}` before `LO:`;
with `LANE=0` (or no `LANE`) it SHALL send `LO:` for the active lane; any other
`LANE` SHALL be rejected with an error and no command.

### REQ-klipper-motion-tracking-sidecar-metadata
- **Source**: `.planning/specs/klipper-motion-tracking/spec.md`
- **Description**: The system SHALL synthesize markers from slicer sidecar JSON rather than G-code strings.

### REQ-klipper-motion-tracking-uds-ingress-pari
- **Source**: `.planning/specs/klipper-motion-tracking/spec.md`
- **Description**: The Klipper UDS flow SHALL feed the existing `on_m118` ingress contract.

### REQ-klipper-motion-tracking-stable-matcher-s
- **Source**: `.planning/specs/klipper-motion-tracking/spec.md`
- **Description**: The `SegmentMatcher` SHALL remain compatible with existing tuner and test suites.

### REQ-klipper-motion-tracking-host-only-integr
- **Source**: `.planning/specs/klipper-motion-tracking/spec.md`
- **Description**: Motion tracking SHALL NOT require firmware changes to operate.

### REQ-klipper-motion-tracking-fallback-paths
- **Source**: `.planning/specs/klipper-motion-tracking/spec.md`
- **Description**: The workflow MUST retain manual G-code marker support when UDS or sidecar is unavailable.

### REQ-klipper-status-parity-flowguard-dict
- **Source**: `.planning/phases/14-klipper-mmu-status-parity/14-RESEARCH.md`
- **Description**: `klipper/mmu.py` SHALL publish a Happy-Hare-shaped `printer.mmu.flowguard` dict
(`enabled`/`active`/`trigger`/`reason`/`level`/`max_clog`/`max_tangle`) derived purely from
FLARE's existing `TT:`/`CT:` dwell-timer telemetry, with no firmware change. `level` SHALL
move toward +1.0 ("clog") as the compression dwell approaches its stop threshold and toward
-1.0 ("tangle") as the tension dwell approaches its stop threshold, and SHALL be 0.0 with
`active` False whenever sync is inactive, regardless of dwell state.

### REQ-klipper-status-parity-missing-keys
- **Source**: `.planning/phases/14-klipper-mmu-status-parity/14-RESEARCH.md`
- **Description**: Every `printer.mmu` status key read by current Fluidd/Mainsail `develop`
builds that has a FLARE-side source SHALL exist in `get_status()` with the correct type —
including `sync_drive`, `is_paused`, `reason_for_pause`, `sync_feedback_flow_rate`,
`has_bypass`, `unit`, `operation`, `drying_state`, `espooler`, `espooler_active`,
`extruder_filament_remaining`, `filament_direction`, `gate_temperature`, `grip`, `last_tool`,
`next_tool`, `slicer_tool_map`, `endless_spool_enabled`, `endless_spool_groups`,
`toolchange_purge_volume`, `encoder`, and `clog_detection_enabled` (with `clogs_enabled`
retained as a backward-compatible alias).

### REQ-klipper-status-parity-hh-version
- **Source**: `.planning/phases/14-klipper-mmu-status-parity/14-RESEARCH.md`
- **Description**: `mmu_machine`'s status SHALL expose `happy_hare_version` so Fluidd/Mainsail
version-gated MMU panel features render instead of falling back to a legacy/absent UI.

### REQ-klipper-status-parity-command-stubs
- **Source**: `.planning/phases/14-klipper-mmu-status-parity/14-RESEARCH.md`
- **Description**: `MMU_TEST_CONFIG`, `MMU_LED`, `MMU_GRIP`/`MMU_RELEASE`/`MMU_SERVO`,
`MMU_PRINT_START`/`MMU_PRINT_END`, and the `*_VARS` maintenance dialogs SHALL register as
ack/no-op commands so Fluidd/Mainsail MMU panel dialogs do not error with "Unknown command".

### REQ-klipper-status-parity-action-strings
- **Source**: `.planning/phases/14-klipper-mmu-status-parity/14-RESEARCH.md`
- **Description**: `action` SHALL report the full Happy-Hare action-string vocabulary (e.g.
`Cutting Filament`, `Preload`, `Loading`, `Unloading`, …) derived from FLARE's existing `EV:`
events, rather than the narrower `Loading`/`Unloading`/`Idle` set.

### REQ-klipper-status-parity-schema-test
- **Source**: `.planning/phases/14-klipper-mmu-status-parity/14-RESEARCH.md`
- **Description**: A `test_status_fields_exist_before_ready`-style unit test SHALL assert the
full `printer.mmu` status schema (every key Fluidd/Mainsail `develop` reads) is present with
the correct type before the daemon connects, catching future key/type drift automatically.

### REQ-moonraker-lane-data-push
- **Source**: `.planning/phases/15-daemon-moonraker-lane-data-maintenance-counters/15-SPEC.md`
- **Description**: `scripts/flare_daemon.py` SHALL push `lane_data` to Moonraker DB namespace `lane_data`
under keys `lane<N>` matching the schema expected by OrcaSlicer (`vendor_name`, `name`, `color`,
`material`, `bed_temp`, `nozzle_temp`, `scan_time`, `td`, `lane`, `spool_id`, `filament_id`), enriched
from Spoolman when connected and falling back to gate map defaults.

### REQ-moonraker-lane-data-sync-lifecycle
- **Source**: `.planning/phases/15-daemon-moonraker-lane-data-maintenance-counters/15-SPEC.md`
- **Description**: Moonraker DB `lane_data` synchronization SHALL trigger automatically on daemon startup,
upon any gate map modification (`/gatemap` or `_write_gate_map_db`), and upon Spoolman cache refresh,
executing via non-blocking background queue with retry resilience.

### REQ-moonraker-lane-data-cleanup
- **Source**: `.planning/phases/15-daemon-moonraker-lane-data-maintenance-counters/15-SPEC.md`
- **Description**: `scripts/flare_daemon.py` SHALL query the `lane_data` namespace and issue Moonraker DB
DELETE requests for any orphaned keys (`lane<N>` where `N >= NUM_GATES`) whenever synchronization runs.

### REQ-live-tuner-per-feature-velocity-buckets
- **Source**: `.planning/specs/live-tuner/spec.md`
- **Description**: The tuner SHALL aggregate telemetry into feature + velocity buckets (rate + bias).

### REQ-live-tuner-machine-scoped-persistence
- **Source**: `.planning/specs/live-tuner/spec.md`
- **Description**: The system SHALL persist bucket state in a machine-scoped JSON file.

### REQ-live-tuner-observe-only-default
- **Source**: `.planning/specs/live-tuner/spec.md`
- **Description**: The tuner SHALL NOT perform firmware writes without explicit permission flags.

### REQ-live-tuner-review-only-workflow
- **Source**: `.planning/specs/live-tuner/spec.md`
- **Description**: The calibration workflow SHALL prefer analyzer review patches over blind tuning.

### REQ-live-tuner-diagnostics
- **Source**: `.planning/specs/live-tuner/spec.md`
- **Description**: The tuner MUST explain bucket states (TRACKING, STABLE, LOCKED) in state output.

### REQ-live-tuner-sync-state-and-relief-effort-
- **Source**: `.planning/specs/live-tuner/spec.md`
- **Description**: Status and diagnostics output SHALL expose the current sync lifecycle state
and warn-only relief-effort counters (accumulated in commanded-MMU mm) so
operators and the offline analyzer can observe relief/fault behavior.

### REQ-live-tuner-relief-effort-counters-are-ac
- **Source**: `.planning/specs/live-tuner/spec.md`
- **Description**: The firmware SHALL accumulate relief effort in commanded-MMU mm:
`g_sync_refill_effort_mm` while the buffer is in TENSION and
`g_sync_relieve_effort_mm` while in COMPRESSION, derived from the existing
commanded-MMU mm integration. Both counters SHALL reset on sync state change
and buffer-state change (sustained-since-entry semantics). Both SHALL be
exposed in the `protocol.c` status line and as GET parameters. No control
behavior SHALL derive from these counters.

### REQ-live-tuner-warn-only-effort-threshold-ev
- **Source**: `.planning/specs/live-tuner/spec.md`
- **Description**: The firmware SHALL emit warn-only diagnostic events when sustained relief
effort exceeds a configured threshold. A `SYNC cannot_refill` event MUST be
emitted once per episode when refill effort crosses
`CONF_SYNC_CANNOT_REFILL_MM` while still in TENSION. A `SYNC cannot_relieve`
event MUST be emitted once per episode when relieve effort crosses
`CONF_SYNC_CANNOT_RELIEVE_MM` while still in COMPRESSION. These events MUST be
diagnostic only and MUST NOT alter control output.

### REQ-marker-capture-policy-sidecar-is-the-onl
- **Source**: `.planning/specs/marker-capture-policy/spec.md`
- **Description**: The Klipper sidecar path SHALL be the single live-capture mechanism. The
shell-marker capture path SHALL NOT exist: `gcode_marker.py` SHALL NOT
provide shell `--emit` modes (`m118`/`mark`/`file`/`both`) and SHALL NOT
provide `--every-layer`; `flare_live_tuner.py` SHALL NOT provide
`--klipper-mode off`, `--marker-file`, or `--keep-marker-file`. The
legacy `scripts/flare_marker.py` and `scripts/flare_logger.py` SHALL be
removed.

### REQ-marker-capture-policy-no-deprecation-not
- **Source**: `.planning/specs/marker-capture-policy/spec.md`
- **Description**: Because the deprecated features are removed, no deprecation notice SHALL
remain in tracked code or docs. There SHALL be no `deprecated` stderr
warning and no `**DEPRECATED**` documentation label for these paths, and
no documentation section SHALL describe a removed path.

### REQ-marker-capture-policy-docs-describe-curr
- **Source**: `.planning/specs/marker-capture-policy/spec.md`
- **Description**: Operator and context documentation SHALL describe the system as it exists
now. Internal "Phase 2.x"-style milestone labels SHALL NOT appear in
`CONTEXT.md`, `BEHAVIOR.md`, `KLIPPER.md`, or `MANUAL.md`. Surviving
technical content SHALL be reworded as current behavior rather than
deleted.

### REQ-motion-safety-dry-spin-protection
- **Source**: `.planning/specs/motion-safety/spec.md`
- **Description**: The system SHALL halt any spinning motor if no filament is detected at the intake and the buffer is not pulling.

### REQ-motion-safety-task-travel-limits
- **Source**: `.planning/specs/motion-safety/spec.md`
- **Description**: Automated tasks SHALL NOT spin indefinitely without hitting a physical checkpoint.

### REQ-motion-safety-safe-autopreload
- **Source**: `.planning/specs/motion-safety/spec.md`
- **Description**: Autopreload SHALL only engage for freshly inserted filament and MUST leave the path clear for the other lane.

### REQ-motion-safety-non-destructive-jam-relief
- **Source**: `.planning/specs/motion-safety/spec.md`
- **Description**: Trailing/overfull and hard-wall handling SHALL NOT discard the extruder
estimator, drift observer, or sigma/confidence state. Destructive reset SHALL
be reserved for true off transitions only.

### REQ-motion-safety-under-extrusion-direction-
- **Source**: `.planning/specs/motion-safety/spec.md`
- **Description**: The controller SHALL never pause local assist while the buffer is empty-side
(`BUF_TENSION`), and SHALL prioritize avoiding under-extrusion over relieving
overfull.

### REQ-motion-safety-terminal-jam-paths-enter-n
- **Source**: `.planning/specs/motion-safety/spec.md`
- **Description**: Hard-wall critical and tension-dwell stop SHALL transition the sync
controller to `SYNC_FAULT_HOLD` instead of calling destructive
`sync_disable(true)`. Entry MUST stop motion (`sync_current_sps = 0`) and
MUST NOT reset the extruder estimator, drift observer, sigma/confidence, or
reserve integrator. Each path SHALL emit a `SYNC FAULT_HOLD` event.

### REQ-motion-safety-fault-hold-auto-recovers-w
- **Source**: `.planning/specs/motion-safety/spec.md`
- **Description**: While in `SYNC_FAULT_HOLD` the controller SHALL remain motion-stopped until
`CONF_SYNC_FAULT_HOLD_RECOVERY_MS` has elapsed since entry, then transition
to `SYNC_OFF` and emit `SYNC FAULT_HOLD_RECOVERY`, allowing normal auto-arm
to re-enter `SYNC_ACTIVE` reusing the preserved estimator subject to
existing freshness aging. No host command SHALL be required to recover.

### REQ-motion-safety-bl-prime-respects-travel-c
- **Source**: `.planning/specs/motion-safety/spec.md`
- **Description**: The `BL` prime move SHALL terminate no later than `BUF_MAX_TRAVEL_MM / 2` mm
of MMU travel, regardless of whether the target buffer raw state has been
reached. The prime MUST NOT escalate to an unbounded drive on a stuck or
mis-wired buffer switch.

### REQ-motion-safety-bl-catch-bypasses-mv-buffe
- **Source**: `.planning/specs/motion-safety/spec.md`
- **Description**: The instant-slam catch driven by `BL` lock-break SHALL run as a sync-owned
drive and is exempt from the `TASK_MOVE` buffer-fault guards
(`FAULT:MOVE_TENSION` on retract-into-tension and `FAULT:MOVE_COMPRESSION`
on forward-into-compression). The exemption SHALL apply only while the
controller is in `SYNC_RETRACT_ASSIST` and the catch sub-state is active.

### REQ-operator-tuning-guide-self-contained-jar
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: The repository SHALL provide a `TUNING.md` that a user with no firmware or
internals knowledge can follow end to end. It MUST NOT contain internal
"Phase 2.x" (or similar internal-phase) labels. It MUST begin with a
"simplest path" TL;DR that gets defaults working, followed by a plain
explanation of what tuning does (baseline and compression-bias, what good
and bad behavior look like) with no assumed firmware knowledge.

### REQ-operator-tuning-guide-exact-copy-paste-c
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: Every command in `TUNING.md` MUST be copy-paste accurate against the
current scripts as they are (`flare_live_tuner.py`, `gcode_marker.py`,
`flare_analyze.py`, `flare_baseline_recommender.py`, `gen_config.py`,
`flash_flare.py`). The guide MUST cover prerequisites with exact commands
(find serial port, find Klipper socket, install pyserial, back up the
state file). Where a script cannot match an operator-friendly command
as-is, the guide MUST describe the script as-is and the limitation MUST be
recorded as an open question rather than changing code.

### REQ-operator-tuning-guide-recovery-path-for-
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: `TUNING.md` MUST provide a "scary behavior" recovery path, reachable from
the TL;DR, that is followed BEFORE any capture/tuning when the setup
misbehaves (repeated `FAULT_HOLD`, repeated `cannot_refill`/
`cannot_relieve`, jams, stalls, or a pinned buffer). It MUST instruct the
user to revert to the shipped scalar defaults and reflash, verify boring
behavior, and treat persistent faults as mechanical (with concrete checks)
rather than a tuning value, and MUST state that tuning on a misbehaving
setup is rejected by the analyzer.

### REQ-operator-tuning-guide-sidecar-is-the-onl
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: `TUNING.md` SHALL document the Klipper sidecar capture path as the single
live-capture mechanism.

### REQ-operator-tuning-guide-two-profile-determ
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: `TUNING.md` MUST state the two-profile bracket model up front: print the
same model twice (fastest-cubic-flow profile and slowest-cubic-flow
profile), capture each, and derive one deterministic result. It MUST give
the exact analyze command using `--profile-fast`, `--profile-slow`, and
`--emit-flow-schedule`, show what the output looks like, explain the
sparse→one-point fallback, and explain that identical inputs give
identical output (and that the scalar one-point path is the safe simple
choice).

### REQ-operator-tuning-guide-apply-recommender-
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: `TUNING.md` MUST give exact review/apply steps (which `config.ini` keys —
`flow_schedule_cap` + `[flow_schedule.v1]`, or scalar `baseline_rate` /
`sync_compression_bias_frac`), then exact `gen_config.py`, build, flash, and
watermark commands. It MUST document `flare_baseline_recommender.py` as
observe-only (suggests, never writes; offline analyzer remains the
authority) with its exact invocation. It MUST include a verification
section: the exact `STATUS` command, how to read relevant fields including
`SYNC_REFILL_MM` / `SYNC_RELIEVE_MM`, and the operator meaning of
`FAULT_HOLD`, `FAULT_HOLD_RECOVERY`, `cannot_refill`, and `cannot_relieve`
in observable terms only. It MUST explain acceptance-gate FAIL vs WARN in
plain language with the action for each.

### REQ-operator-tuning-guide-sync-feedback-sens
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: `TUNING.md` and `config.ini.example` SHALL describe buffer sensor mode with
Happy Hare Sync-Feedback Sensor type codes: `D` = Dual two-switch sensor
(`BUF_SENSOR_TYPE == 0`, D=0), `P` = Proportional analog sensor
(`BUF_SENSOR_TYPE == 1`, P=1), and `TO`/`CO` as recognized but not
implemented in FLARE. The docs SHALL name the sensor separately from the
control law: type-D two-level / hysteretic relay control law, type-P analog
PD/EKF reserve control law.

### REQ-operator-tuning-guide-tuning-md-uses-syn
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: TUNING.md SHALL use the Sync-Feedback Sensor vocabulary with Happy Hare
type codes (P, D; TO/CO noted as unimplemented) and SHALL document the
`BUF_SENSOR_TYPE` value contract (D=0, P=1) where sensor mode is
referenced, naming the sensor separately from the control law and not
using the legacy analog alias.

### REQ-operator-tuning-guide-tuning-md-relay-se
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: The TUNING.md relay content SHALL describe only the fallback relay law
(`relay_catchup_frac`, `relay_neutral_frac`), the deep-COMPRESSION
collapse-ramp keys, and the `relay_min_flip_mm` 0.0/deadlock caveat. It
SHALL NOT document a relay duty estimator, a confidence gate, an offline
relay capture/analyze/apply loop, or the bimodal duty-ratchet note —
that machinery no longer exists.

### REQ-operator-tuning-guide-type-d-tuning-guid
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: Operator-facing type-D tuning guidance SHALL attribute the relay limit cycle
and its COMPRESSION/TENSION drift to `relay_neutral_frac` (and
`relay_catchup_frac`), and SHALL NOT instruct the operator to change
`sync_kp_rate` for a type-D (`BUF_SENSOR_TYPE == 0`) buffer. This requirement
applies to both `TUNING.md` and the verdict/help text emitted by
`flare_sync_check.py` (`analyze_stability`, `analyze_drift`). `sync_kp_rate`
guidance MAY appear only for analog type P (`BUF_SENSOR_TYPE == 1`), whose
`psf_control_law` actually consumes it.

### REQ-operator-tuning-guide-default-relay-neut
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: The shipped default `relay_neutral_frac` SHALL match demand (`1.00`) for type-D
after the no-overshoot ramp fix, not deliberately overfeed. `TUNING.md` and
`config.ini.example` SHALL show this default, and it SHALL match the
`gen_config.py` default. Operators MAY raise it slightly only if hardware soak
shows steady TENSION drift.

### REQ-operator-tuning-guide-type-d-relay-trim-
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: For `BUF_SENSOR_TYPE == 0`, the volatile neutral feed trim SHALL only ever
*raise* NEUTRAL feed. A TENSION touch (starvation, the dangerous rail) SHALL
increase the trim by `SYNC_RELAY_TRIM_STEP_SPS`, clamped to
`+SYNC_RELAY_TRIM_CLAMP_SPS`, and the trim SHALL leak toward zero during
`BUF_NEUTRAL` dwell. A COMPRESSION touch SHALL NOT reduce the trim: COMPRESSION
is the tolerated/safe rail, and steady overfeed is corrected by the
switch-crossing demand estimator (`extruder_est_sps`), not by cutting feed.
Consequently the trim SHALL remain non-negative, so it can never drive NEUTRAL
feed below `demand × relay_neutral_frac` (the prior two-sided trim ratcheted
negative under compression-dominated dynamic flow and dragged the buffer toward
TENSION). The trim SHALL apply only to the type-D NEUTRAL relay feed and SHALL
NOT alter analog type-P feedforward.

### REQ-operator-tuning-guide-type-d-compression
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: For `BUF_SENSOR_TYPE == 0`, COMPRESSION feed SHALL be a bounded fraction of
estimated demand (`SYNC_COMPRESSION_DRAIN_FRAC × extruder_est_sps`) — not a hard
zero — while sync is active and the extruder is actively drawing filament
(estimated demand above a small threshold), so
the buffer drains a small bounded amount off the COMPRESSION rail instead of
dumping the full span toward TENSION and forcing a re-ramp from zero. The drain
fraction SHALL be clamped strictly below demand so the buffer cannot net-fill
while pinned. The partial-drain path SHALL also be bounded by
`SYNC_COMPRESSION_DRAIN_BUDGET_MM` of COMPRESSION relieve effort; once the budget
is reached, COMPRESSION feed SHALL true-stop at `0` until the buffer leaves
COMPRESSION. When estimated demand is ≈ 0 (end-of-feed / `TASK_IDLE`),
COMPRESSION feed SHALL remain a true zero to preserve the purge/idle no-grind
behavior. `SYNC_COMPRESSION_DRAIN_FRAC = 0.0` or
`SYNC_COMPRESSION_DRAIN_BUDGET_MM = 0.0` SHALL disable the partial-drain path and
restore the legacy hard-stop for A/B testing. Applies only to type-D; SHALL NOT
alter analog type-P.

### REQ-operator-tuning-guide-asymmetric-relay-c
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: A host analyzer SHALL parse the status poll stream and report, over a window:
TENSION touch count (the hard constraint; target `0`), COMPRESSION pin
duration, mean `EST − MM` during `BUF_NEUTRAL` (the underfeed / tension-drift
signature), the `BP` distribution and minimum, and the relay cycle period. The
analyzer SHALL emit an asymmetric-objective verdict: PASS only when TENSION
touches are zero; otherwise it SHALL recommend the controller-correct lever
(raise `relay_neutral_frac` or adjust the COMPRESSION drain / trim settings) and
SHALL NOT recommend `sync_kp_rate` for type-D. The analyzer SHALL operate
read-only from existing poll fields (`BP`, `BUF`, `MM`, `EST`) and SHALL NOT
require new firmware telemetry.

### REQ-operator-tuning-guide-type-d-estimator-a
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: For `BUF_SENSOR_TYPE == 0`, the firmware SHALL treat the
`BUF_NEUTRAL -> BUF_COMPRESSION` transition as the primary demand sample by
averaging the actual applied `sync_current_sps` over the NEUTRAL dwell,
preferring the pre-taper portion before compression-side braking when available,
and subtracting the measured fill rate. Degenerate fill samples SHALL be ignored,
slow near-converged fills SHALL remain eligible, and accepted demand samples
SHALL blend into `extruder_est_sps`. When later compression-side fill samples no
longer have known switch-to-switch travel, the pre-taper applied feed average
SHALL be eligible as an upper-bound demand sample, and a short
`BUF_COMPRESSION -> BUF_NEUTRAL` true-stop drain with near-zero applied feed
SHALL be eligible as a fallback demand sample. The residual neutral trim SHALL
leak toward zero during `BUF_NEUTRAL` dwell. This estimator correction and trim
leak SHALL NOT alter the analog type-P estimator/feedforward path.

### REQ-operator-tuning-guide-type-d-reserve-tar
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: For `BUF_SENSOR_TYPE == 0`, the firmware SHALL park the virtual neutral target
slightly toward the compression side using the existing `SYNC_RESERVE_PCT`
reserve percentage. This reserve SHALL give sharp real-print speed-ups physical
headroom before the buffer reaches TENSION. This SHALL NOT change analog type-P
control behavior, and it SHALL NOT require increasing `relay_neutral_frac` above
the demand-match default.

### REQ-operator-tuning-guide-type-d-estimator-a
- **Source**: `.planning/specs/operator-tuning-guide/spec.md`
- **Description**: For `BUF_SENSOR_TYPE == 0`, the estimator SHALL use a faster attack when a
switch-crossing demand sample is higher than the current `extruder_est_sps`.
The firmware SHALL blend that sample with `SYNC_EST_ATTACK_ALPHA`, a
runtime/non-persisted float clamped `[0.65, 1.0]`, and SHALL bypass the normal
`EST_ALPHA_MAX` clamp for that rising-demand update.
When the sample is lower than or equal to current `extruder_est_sps`, the
estimator SHALL keep the existing dwell-derived EMA clamped by
`EST_ALPHA_MIN..EST_ALPHA_MAX`, so falling demand remains slow and does not
reintroduce COMPRESSION chatter. This SHALL apply only to type-D; analog type-P
per-tick estimation and `psf_control_law` SHALL remain unchanged.

### REQ-persistence-contract-settings-version-bu
- **Source**: `.planning/specs/persistence-contract/spec.md`
- **Description**: The runtime settings layout MUST be protected by a strict schema version.

### REQ-persistence-contract-flash-loading-and-d
- **Source**: `.planning/specs/persistence-contract/spec.md`
- **Description**: Missing or corrupt flash SHALL NOT prevent safe boot.

### REQ-persistence-contract-runtime-tunables-fl
- **Source**: `.planning/specs/persistence-contract/spec.md`
- **Description**: Any **durable** tunable (tier T1 or T2) SHALL live in `config.ini` and flow
through `gen_config.py`. A T3 internal constant SHALL NOT enter this flow; it
lives in its owning source module (see the `config-surface-tiers` capability)
and is exempt from the config/persist/SET/GET path.

### REQ-persistence-contract-persisted-fields-ro
- **Source**: `.planning/specs/persistence-contract/spec.md`
- **Description**: Every field of `settings_t` written by `settings_save()` SHALL be read back by
`settings_load()` and SHALL have its owning runtime global initialized by
`settings_defaults()`. No field may be write-only (saved to flash but never
loaded), because such a field silently discards an operator's `SV:`-persisted
value on the next boot.

### REQ-persistence-contract-settings-round-trip
- **Source**: `.planning/specs/persistence-contract/spec.md`
- **Description**: `SETTINGS_VERSION` SHALL NOT be incremented by a fix that only completes the
load/default arms of fields already present in the `settings_t` layout, since
the on-flash byte layout is unchanged and persisted operator settings MUST
survive.

### REQ-project-architecture-firmware-shall-rema
- **Source**: `.planning/specs/project-architecture/spec.md`
- **Description**: FLARE firmware SHALL run as cooperative RP2040 firmware without an RTOS, with the
main loop calling non-blocking module ticks.

### REQ-project-architecture-module-ownership-sh
- **Source**: `.planning/specs/project-architecture/spec.md`
- **Description**: Each firmware module SHALL keep ownership aligned with the documented
architecture boundaries. A module MAY be split into multiple cohesive translation
units provided each unit keeps a single domain owner and the file map stays
documented.

### REQ-project-architecture-runtime-tunables-sh
- **Source**: `.planning/specs/project-architecture/spec.md`
- **Description**: Persistent runtime tunables SHALL be represented consistently across config
files, generated firmware headers, runtime storage, serial protocol, and docs.

### REQ-project-architecture-serial-protocol-cha
- **Source**: `.planning/specs/project-architecture/spec.md`
- **Description**: USB serial commands SHALL continue using `CMD:params\n` input and `OK:` / `ER:`
reply semantics, with best-effort `EV:` events where applicable.

### REQ-project-architecture-persistence-shall-r
- **Source**: `.planning/specs/project-architecture/spec.md`
- **Description**: Flash persistence commands SHALL be rejected while motion, toolchange, cutter
activity, or boot stabilization could make persistence unsafe.

### REQ-project-architecture-sync-shall-not-run-
- **Source**: `.planning/specs/project-architecture/spec.md`
- **Description**: Normal sync control SHALL remain guarded so it runs only when the toolchange
context is idle.

### REQ-project-architecture-load-and-unload-saf
- **Source**: `.planning/specs/project-architecture/spec.md`
- **Description**: Load, unload, autoload, and related lane tasks SHALL use distance limits and
sensor state rather than legacy names that imply time-only limits.

### REQ-project-architecture-shared-speed-conver
- **Source**: `.planning/specs/project-architecture/spec.md`
- **Description**: Speed conversion SHALL use shared helper functions rather than duplicate
conversions between slicer units, firmware steps-per-second, and protocol
values.

### REQ-project-architecture-board-pin-assumptio
- **Source**: `.planning/specs/project-architecture/spec.md`
- **Description**: Board-level pin assignments and hardware constants SHALL remain centralized in
`firmware/include/config.h` and generated tune headers where applicable.

### REQ-project-architecture-buffer-service-comm
- **Source**: `.planning/specs/project-architecture/spec.md`
- **Description**: `BS` SHALL cancel active sync, buffer lock, an existing buffer-stabilize drive,
and standalone lane commands before starting a fresh buffer stabilize, while
hard activities (`TC`, cutter, manual unload) SHALL still reject with `ER:BUSY`.
`BL:T` and `BL:C` SHALL cancel an active buffer-stabilize drive before arming
buffer lock so tip-form macros can transition from neutralization to lock without
a racy delay.

### REQ-psf-type-p-sensor-type-p-relief-pause-au
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: The firmware SHALL re-arm type-P sync from `SYNC_RELIEF_PAUSE` when the buffer is
under genuine extruder demand, via two complementary paths:

### REQ-psf-type-p-sensor-type-p-stabilize-rail-
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: The firmware SHALL allow type-P idle/boot buffer-stabilize to drive the buffer
off a saturated rail to goal. While the analog signal is saturated
(`g_buf_analog_saturated_since_ms != 0`), the stagnation guard SHALL NOT abort on
the short-window position-change test; it SHALL keep driving and re-baseline the
stagnation reference position, aborting only if the buffer remains saturated past
`PSF_STAB_RAIL_BREAK_MS` measured from stabilize start. Once the signal
desaturates, the firmware SHALL apply the standard dry-spin stagnation check
(`PSF_STAB_STAGNANT_MS` / `PSF_STAB_STAGNANT_NORM`) with its window measured from
desaturation, not from stabilize start. Type-D stabilize is unchanged.

### REQ-psf-type-p-sensor-type-p-tension-refill-
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: The firmware SHALL bypass the type-P distance-based feed smoothing on the
tension/refill side so a starved buffer is refilled without the EMA ramp lag.
When `BUF_SENSOR_TYPE == 1`, the buffer is in the tension soft-wall zone
(`buf_pos_norm() < -PSF_SOFT_WALL_START`), and the control target exceeds the
current feed (`target_sps > sync_current_sps`), the applied feed SHALL be set
directly to the soft-wall target (`max_sps`). On that snap the smoothing filter
SHALL be seeded at the demand estimate (`extruder_est_sps`), so that once the
buffer leaves the wall the feed eases to the extruder rate rather than remaining
at max and overshooting into compression. Outside the tension wall, and on the
compression/neutral side, the existing distance-EMA + wall-clock-decay smoothing
is unchanged. Type-D feed application is unchanged.

### REQ-psf-type-p-sensor-psf-endpoint-calibrati
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: The firmware SHALL support runtime calibration of PSF sensor endpoints via
`CAL:PSF_COMP`, `CAL:PSF_TENS`, and `CAL:PSF_NEUT` commands. Each command
SHALL sample the ADC at the moment of invocation, store the result in the
corresponding runtime variable, and persist it to NVM.

### REQ-psf-type-p-sensor-asymmetric-normalizati
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: The firmware SHALL normalize raw PSF ADC readings to [-1,1] using asymmetric
endpoint calibration. Polarity SHALL be auto-detected from calibration values:
if `BUF_PSF_MAX_COMP < BUF_PSF_MAX_TENS`, compression is the lower raw value
(reversed=true); otherwise compression is the higher raw value. No explicit
invert flag SHALL be required.

### REQ-psf-type-p-sensor-goal-relative-zone-bou
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: For type-P sensors, buffer zone boundaries SHALL be derived from `BUF_GOAL`
converted to normalized space (TENSION / NEUTRAL / COMPRESSION), not from a
symmetric `BUF_THR`. NEUTRAL SHALL mean "near goal," not "near raw 0.5."

### REQ-psf-type-p-sensor-buf-goal-user-param-in
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: `BUF_GOAL` SHALL be settable and gettable via the protocol in raw ADC fraction
[0,1] space — the same space as `BUF_PSF_MAX_COMP`, `BUF_PSF_MAX_TENS`, and
`BUF_PSF_NEUTRAL`. Default SHALL be 0.3 (between neutral 0.5 and compression
extreme 0.0 for normal PSF polarity). It SHALL be persisted in NVM.

### REQ-psf-type-p-sensor-remove-buf-range-and-b
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: `BUF_RANGE` and `BUF_INVERT` SHALL be removed from the protocol and NVM.
Polarity is handled by calibration (D2). `BUF_RANGE` is superseded by
asymmetric endpoint calibration.

### REQ-psf-type-p-sensor-continuous-extruder-es
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: For type-P, the firmware SHALL compute buffer velocity from the per-tick
position delta and update the extruder-rate estimate every control tick, using
`extruder_mm_s = mmu_mm_s + arm_vel` where `arm_vel = vel_norm *
half_travel_mm`. It SHALL NOT use crossing-event estimation for type-P.

### REQ-psf-type-p-sensor-gradual-pd-control-wit
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: For type-P, `psf_control_law()` SHALL produce a target feed rate as the sum of
a continuous feedforward (`extruder_est_sps`), a proportional term on position
error relative to goal, and a derivative term on filtered buffer velocity. The
proportional term SHALL be suppressed within `PSF_CTRL_DEADBAND` of goal; the
derivative term SHALL remain active regardless of dead zone.

### REQ-psf-type-p-sensor-filtered-derivative
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: The derivative term SHALL operate on a low-pass-filtered velocity computed from
the already-smoothed position, to avoid amplifying ADC noise (derivative kick).

### REQ-psf-type-p-sensor-soft-walls
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: For type-P, the control target SHALL progressively blend from the PD output
toward a safety limit as `|pos_norm|` enters `[PSF_SOFT_WALL_START, 1.0]`:
toward maximum feed on the tension side (urgent refill) and toward zero on the
compression side (stop overfeed).

### REQ-psf-type-p-sensor-hard-catch-and-print-s
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: For type-P, a rapid velocity spike toward compression SHALL trigger a
reversible fast brake. The firmware SHALL then disambiguate a transient
slowdown from a real print stop by observing subsequent buffer motion: if the
buffer drifts back toward tension within `PSF_STOP_CONFIRM_MS`, control SHALL
resume; if the buffer stays pinned at the compression wall, the controller
SHALL enter relief pause.

### REQ-psf-type-p-sensor-type-p-unload-uses-no-
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: For type-P sensors, `TASK_UNLOAD` SHALL NOT use a position-based over-tension
guard (relief jog or tension-dwell block); it SHALL rely on the `UNLOAD_MAX`
distance limit (`UNLOAD_TIMEOUT`) for the stuck case. The type-D guards (recover
jog + `UNLOAD_TENSION_BLOCK`) SHALL remain unchanged and gated `BUF_SENSOR_TYPE == 0`.

### REQ-psf-type-p-sensor-type-p-fault-timers-sc
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: The firmware SHALL scope the type-P tension-dwell and saturation fault timers to
the active-sync window so idle-accumulated state cannot fire a spurious fault on
engagement or deadlock fault recovery. On every type-P sync activation (normal
auto-start, relief-pause re-arm, fault-hold recovery) the firmware SHALL restart
`sync_tension_pin_since_ms` to `now` when the buffer is in `BUF_TENSION` and to `0`
otherwise. On `FAULT_HOLD_RECOVERY` the firmware SHALL reset
`g_buf_analog_saturated_since_ms` so the recovered active state gets a fresh
saturation window. Type-D fault handling is unchanged.

### REQ-psf-type-p-sensor-type-p-feed-quality-an
- **Source**: `.planning/specs/psf-type-p-sensor/spec.md`
- **Description**: Type-P feed control SHALL track extruder demand on a real print without sustained
buffer hunting or end-of-move overshoot that produces print artifacts, and a manual
`BS` SHALL drive the buffer to goal in a single invocation from any non-saturated
position. Acceptance is measured against a real print, not isolated bench bursts.

### REQ-python-host-tooling-style-ruff-lint-conf
- **Source**: `.planning/specs/python-host-tooling-style/spec.md`
- **Description**: Repo SHALL carry a `pyproject.toml` `[tool.ruff]` config pinning `line-length`,
`target-version`, and an explicit rule `select` set, and `scripts/*.py` SHALL pass
`ruff check` under it.

### REQ-python-host-tooling-style-python-lint-in
- **Source**: `.planning/specs/python-host-tooling-style/spec.md`
- **Description**: `scripts/validate_regression.py` SHALL run `ruff check scripts/` so lint regressions
fail the gate alongside the existing `py_compile` and `unittest` steps.

### REQ-python-host-tooling-style-behavior-prese
- **Source**: `.planning/specs/python-host-tooling-style/spec.md`
- **Description**: Lint fixes SHALL be behavior-preserving; the `scripts/` `unittest` suite SHALL stay
green and host-tooling behavior unchanged.

### REQ-python-host-tooling-style-diagnostic-scr
- **Source**: `.planning/specs/python-host-tooling-style/spec.md`
- **Description**: Every script under `scripts/` SHALL have at least one live (non-archived) reference: a
Python import, a live doc mention, or a backing spec. A standalone diagnostic whose only
references are archived OpenSpec changes is dead and SHALL be removed; its history remains
recoverable via `git show <rev>:scripts/<name>.py`. The regression gate (`py_compile`,
`ruff`, and `unittest discover -p test_*.py`) SHALL stay green across the removal, since a
dead script has no live importer or test.

### REQ-relay-fallback-only-type-d-relay-neutral
- **Source**: `.planning/specs/relay-fallback-only/spec.md`
- **Description**: For `BUF_SENSOR_TYPE == 0`, the NEUTRAL feed target SHALL always be
`clamp(extruder_est_sps, SYNC_MIN, relay_base) · RELAY_NEUTRAL_FRAC`.
There SHALL be no confidence-gated duty-estimator path, no `[lo,hi]`
estimate clamp, and no estimator/seed selection. The TENSION (catch-up)
and COMPRESSION (`SYNC_MIN`) branches SHALL be unchanged.

### REQ-relay-fallback-only-firmware-drops-the-d
- **Source**: `.planning/specs/relay-fallback-only/spec.md`
- **Description**: The firmware SHALL NOT contain the relay duty-estimator state, the
`v_est` blend, the confidence gate, the pair-history/travel
accumulators, or the cold-start estimate seed. No code path may
reference the deleted estimator state.

### REQ-relay-fallback-only-protocol-drops-estim
- **Source**: `.planning/specs/relay-fallback-only/spec.md`
- **Description**: The device protocol and `flare_cmd.py --dump` SHALL NOT expose the
`RDE`, `RDCF`, or `RDV` fields or any estimate/confidence `SET:`/`GET:`
parameters. Unrelated status fields (`BUF`, `BP`, `EST`, `NC`, …) SHALL
be unchanged.

### REQ-relay-fallback-only-analyzer-emits-no-re
- **Source**: `.planning/specs/relay-fallback-only/spec.md`
- **Description**: `flare_analyze.py` SHALL NOT compute or emit relay duty-cycle
recommendations (`relay_estimate_lo`/`relay_estimate_hi`/
`relay_seed_rate`) or a relay coverage verdict. All non-relay analyzer
output (`baseline_rate`, flow schedule, acceptance gate, verdicts) SHALL
be byte-identical to before this change on existing non-relay inputs.

### REQ-relay-fallback-only-config-surface-drops
- **Source**: `.planning/specs/relay-fallback-only/spec.md`
- **Description**: Config, the generator, and persisted settings SHALL NOT define
`relay_estimate_lo`, `relay_estimate_hi`, `relay_confidence_cycles`,
`relay_confidence_window_ms`, or `relay_seed_warmup_ms`, and
`SETTINGS_VERSION` SHALL be bumped. `relay_catchup_frac`,
`relay_neutral_frac`, `relay_min_flip_mm`, and the `relay_collapse_*`
keys SHALL be retained with unchanged behavior.

### REQ-reserve-safety-floor-reserve-bias-is-flo
- **Source**: `.planning/specs/reserve-safety-floor/spec.md`
- **Description**: The effective trailing-bias used to compute the reserve target SHALL be
`max(SYNC_TRAILING_BIAS_FRAC, schedule_bias)`. The flow schedule MAY
deepen the reserve (bias above the configured scalar) but SHALL NOT
reduce it below `SYNC_TRAILING_BIAS_FRAC` for any flow, segment, or
clamped endpoint.

### REQ-reserve-safety-floor-baseline-control-fl
- **Source**: `.planning/specs/reserve-safety-floor/spec.md`
- **Description**: `baseline_control_floor_sps()` SHALL return
`max(flow_param(extruder_est_sps).baseline_sps, g_baseline_target_sps)`,
restoring the guarantee that the control floor — and the ADVANCE recovery
gain derived from it — never drops below the configured persistent
baseline.

### REQ-reserve-safety-floor-degenerate-single-p
- **Source**: `.planning/specs/reserve-safety-floor/spec.md`
- **Description**: The floored bias and baseline SHALL equal the configured scalars when the
schedule has a single point equal to those scalars, so behavior MUST be
byte-identical to the pre-flow-keyed scalar controller.

### REQ-reserve-safety-floor-schedule-and-live-l
- **Source**: `.planning/specs/reserve-safety-floor/spec.md`
- **Description**: The flow schedule and the live per-segment learner SHALL only ever
strengthen reserve depth and the baseline floor relative to the
configured scalars; neither SHALL reduce reserve depth or baseline floor
below config. This is the controller's full-bias safety invariant.

### REQ-script-path-handling-tilde-and-glob-expa
- **Source**: `.planning/specs/script-path-handling/spec.md`
- **Description**: Host scripts SHALL expand `~` and full glob syntax (`*`, `?`, `[...]`,
recursive `**`) for input/read path arguments. Resolution SHALL be
deterministic and sorted. The affected input arguments are
`gcode_marker.py` positional `input`, `flare_baseline_recommender.py
--file`, `flare_analyze.py --in` / `--profile-fast` / `--profile-slow`,
and `flare_live_tuner.py --sidecar`. List-valued arguments accept
multiple matches; single-valued arguments require exactly one match.

### REQ-script-path-handling-output-paths-are-ne
- **Source**: `.planning/specs/script-path-handling/spec.md`
- **Description**: Write/output path arguments SHALL be `~`-expanded only and SHALL NOT be
glob-expanded. The affected arguments include `gcode_marker.py
--output`, `flare_analyze.py --out`, `flare_live_tuner.py --state` and
`--csv-out`.

### REQ-script-path-handling-path-errors-produce
- **Source**: `.planning/specs/script-path-handling/spec.md`
- **Description**: The scripts SHALL report path-argument failures as a single stderr line
of the form `Error: <path>: <reason>` and SHALL exit non-zero with no
Python traceback. This MUST cover missing files, no-glob-match,
not-a-regular-file, and permission-denied. Unrelated exceptions MUST
still propagate.

### REQ-script-path-handling-existing-analyzer-i
- **Source**: `.planning/specs/script-path-handling/spec.md`
- **Description**: The shared resolution helper SHALL preserve `flare_analyze.py --in`
behavior: the set and sorted order of resolved runs for a given glob or
explicit file list SHALL match the pre-change behavior.

### REQ-static-regression-validation-automated-r
- **Source**: `.planning/specs/static-regression-validation/spec.md`
- **Description**: The host regression validation test suite MUST automatically discover and execute all unit tests in the scripts directory.

### REQ-static-regression-validation-standard-te
- **Source**: `.planning/specs/static-regression-validation/spec.md`
- **Description**: All Python test modules in the repository MUST be compatible with standard test runners (such as unittest and pytest) without triggering module-import exits.

### REQ-static-regression-validation-dev-tuning-
- **Source**: `.planning/specs/static-regression-validation/spec.md`
- **Description**: The regression gate MUST build the firmware with `FLARE_DEV_TUNING=ON` so code behind
`#ifdef FLARE_DEV_TUNING` (e.g. `protocol.c` Tier-3 SET/GET handlers) is compiled and
validated, matching the configuration deployed on the Pi.

### REQ-sync-feedback-compression-recovery-cap-g
- **Source**: `.planning/specs/sync-feedback/spec.md`
- **Description**: The firmware SHALL apply the shared `sync_compression_recovery_active` feed cap
and its time-based collapse trim only when `BUF_SENSOR_TYPE == 0` (type-D). For type-P,
compression-overfeed backoff SHALL be owned solely by the soft wall (Layer 2) and
hard catch (Layer 3) in `psf_control_law` / `sync_tick`; the recovery cap SHALL
NOT reduce the type-P feed target.

### REQ-sync-feedback-common-normalized-scale-fo
- **Source**: `.planning/specs/sync-feedback/spec.md`
- **Description**: The sync PD loop SHALL operate on a common normalized position and target for
both type-D and type-P sensors, eliminating type-specific branches in shared
control math. `buf_pos_norm()` SHALL return normalized [-1,1] position for
both types; `buf_target_norm()` SHALL return normalized [-1,1] target.

### REQ-sync-feedback-isolated-control-laws
- **Source**: `.planning/specs/sync-feedback/spec.md`
- **Description**: Type-D relay bangbang and type-P continuous PD SHALL be implemented as
isolated static functions. No control law logic SHALL appear inline in
`sync_tick()`.

### REQ-sync-feedback-sync-apply-scaling-unified
- **Source**: `.planning/specs/sync-feedback/spec.md`
- **Description**: `sync_apply_scaling()` SHALL use a single code path operating on normalized
position and target for both sensor types. The type-P early-return branch
SHALL be removed.

### REQ-sync-feedback-compression-floor-removed-
- **Source**: `.planning/specs/sync-feedback/spec.md`
- **Description**: The firmware SHALL NOT force-raise the feed floor during `BUF_COMPRESSION` for
type-P (the L1750 block is removed). For type-P, COMPRESSION means buffer full;
forcing a feed floor fights drain and is incorrect.

### REQ-sync-refactor-foundation-firmware-sync-s
- **Source**: `.planning/specs/sync-refactor-foundation/spec.md`
- **Description**: FLARE firmware SHALL run sync behavior from compiled configuration and runtime
settings without requiring a Klipper plugin or host daemon during normal
printing.

### REQ-sync-refactor-foundation-sync-hardening-
- **Source**: `.planning/specs/sync-refactor-foundation/spec.md`
- **Description**: Instrumentation, estimator confidence, and buffer-behavior changes SHALL be
introduced so existing default behavior remains recognizable unless the operator
opts into new calibration-derived settings.

### REQ-sync-refactor-foundation-runtime-tunable
- **Source**: `.planning/specs/sync-refactor-foundation/spec.md`
- **Description**: Any durable sync tunable SHALL live in `config.ini` and `config.ini.example`,
flow through `scripts/gen_config.py`, and be consumed from generated
`firmware/include/tune.h` or the matching runtime settings path.

### REQ-sync-refactor-foundation-telemetry-shall
- **Source**: `.planning/specs/sync-refactor-foundation/spec.md`
- **Description**: Firmware and host tooling SHALL expose enough sync, buffer, and estimator
signals for offline calibration to infer stable operating parameters.

### REQ-sync-refactor-foundation-regression-impa
- **Source**: `.planning/specs/sync-refactor-foundation/spec.md`
- **Description**: Changes to sync behavior SHALL consider preload, load, unload, toolchange, sync,
RELOAD, persistence, protocol, and documentation effects before landing.

### REQ-sync-refactor-standalone-sync
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: FLARE SHALL run sync, toolchange, and RELOAD without host after calibration flash.

### REQ-sync-refactor-observe-only-calibration
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The system SHALL collect markers, buckets, and patches without mutation unless explicit opt-in.

### REQ-sync-refactor-sidecar-uds-tracking
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The system SHALL prefer sidecar JSON + Klipper UDS over shell markers when available.

### REQ-sync-refactor-durable-migratable-state
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The tuner MUST persist buckets and migrate schema without data loss across versions.

### REQ-sync-refactor-chatter-resistance
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: Buckets SHALL become LOCKED on evidence/noise pass and UNLOCK only on strong mismatch.

### REQ-sync-refactor-relative-noise-gate
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The tuner SHALL use `sigma/x` ratio, not absolute variance, for lock decisions.

### REQ-sync-refactor-state-aware-recommendation
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The analyzer SHALL weight by precision/count, not raw CSV clusters, during recommendation.

### REQ-sync-refactor-comparable-run-consistency
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The gate SHALL use recommendation path consistency, filtered to mature runs.

### REQ-sync-refactor-fail-vs-warn-separation
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The gate SHALL FAIL only on unreliable recommendations or pathological scatter.

### REQ-sync-refactor-bidirectional-drift-observ
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The residual drift observer SHALL measure errors on both `TENSION` and `COMPRESSION` boundaries to prevent directional blindness.

### REQ-sync-refactor-double-integrator-avoidanc
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The feedforward velocity estimator SHALL NOT bleed towards the PI controller output in the safe zone.

### REQ-sync-refactor-bias-accumulation
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The analyzer and tuner SHALL accumulate position error offsets onto the current configuration value, avoiding fixed setpoint anchors.

### REQ-sync-refactor-disciplined-live-baseline-
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The firmware live baseline learner SHALL update only in `SYNC_ACTIVE`, require
multi-cycle agreement, reject high-variance observations, and enforce a
time-and-distance cooldown. It SHALL remain non-persistent and up-only; the
offline analyzer remains the sole persistent baseline/bias authority.

### REQ-sync-refactor-non-destructive-lifecycle-
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: Replacing destructive disable with explicit non-destructive states SHALL NOT
require host involvement and SHALL keep standalone post-flash operation
intact.

### REQ-sync-refactor-baseline-and-bias-sourced-
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The sync controller SHALL obtain its baseline and compression-bias by
evaluating the flow-keyed schedule at the live `extruder_est_sps`,
replacing the single-scalar read, with a length-1 schedule as the exact
degenerate fallback. This SHALL NOT alter the full-bias invariant: the
schedule only supplies inputs that `SYNC_ACTIVE` reserve-target control
already consumes; reserve target, `reserve_correction`, `zone_bias`,
soft-wall trim, and collapse ramp SHALL be unchanged.

### REQ-sync-refactor-live-learner-ratchets-with
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The disciplined live baseline learner SHALL remain ephemeral, up-only,
non-persistent, and disciplined (multi-cycle, variance-reject, cooldown,
`SYNC_ACTIVE`-gated) but SHALL ratchet the baseline of the currently
active flow segment rather than a global scalar. On reboot or settings
reload the schedule SHALL reload from config (offline authority); the
live segment delta SHALL be lost.

### REQ-sync-refactor-buffer-states-use-tension-
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The buffer state vocabulary SHALL be `BUF_TENSION` (filament tensioned,
buffer empty, printer pulling faster than the MMU pushes), `BUF_COMPRESSION`
(filament compressed, buffer full, MMU pushing faster than the printer
pulls), `BUF_NEUTRAL` (neutral band), and `BUF_FAULT`. The legacy names
`BUF_ADVANCE`, `BUF_TRAILING`, and the buffer-state `BUF_MID`/state-derived
`mid` MUST NOT appear anywhere in firmware, scripts, live specs, or docs,
and no back-compat alias SHALL exist. Arithmetic `mid`/`midpoint`
unrelated to the buffer state is out of scope and MAY remain.

### REQ-sync-refactor-serial-protocol-tokens-and
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The serial protocol SHALL emit `BUF:TENSION|NEUTRAL|COMPRESSION`, the
corresponding `EV:BS:*` tokens, `EV:SYNC:TENSION_RISK_HIGH`, and renamed
short status field keys for any old-state-derived key (`AD`, `TD`, `APX`,
and similar) in place of the legacy spellings. In-repo parsing scripts
MUST be updated in the same change so they remain consistent.

### REQ-sync-refactor-config-keys-are-renamed-to
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: Configuration keys that named the legacy states SHALL be renamed
(`sync_tension_dwell_stop_ms`, `sync_tension_ramp_delay_ms`,
`sync_compression_bias_frac`, `compression_rate`, `neutral_creep_*`,
`sync_overshoot_neutral_extend`). Legacy keys SHALL be ignored by the
existing unknown-key handling (no new hard-error path is added); a stale
`config.ini` falls back to defaults for the renamed keys. No migration
guide is produced (active development — renames are safe).

### REQ-sync-refactor-the-rename-does-not-change
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: This rename SHALL be behavior-preserving. A status-line and event
semantics snapshot captured before and after MUST be numerically identical
(only token spellings differ); any behavioral delta is out of scope and
belongs to `audit-sync-polarity`.

### REQ-sync-refactor-sync-control-polarity-matc
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: Sync control SHALL feed faster when the buffer is `BUF_TENSION` (empty,
printer pulling faster than the MMU) and back off when the buffer is
`BUF_COMPRESSION` (full, MMU pushing faster than the printer), in both the
type-D two-level / hysteretic relay control law (`BUF_SENSOR_TYPE == 0`,
D=0) and the type-P analog PD/EKF reserve control law
(`BUF_SENSOR_TYPE == 1`, P=1). No control site SHALL invert this
relationship. `TO` and `CO` are recognized Happy Hare Sync-Feedback Sensor
types but are not implemented in FLARE.

### REQ-sync-refactor-pin-to-state-decode-is-ver
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The buffer-sensor decode SHALL map a pressed tension switch to
`BUF_TENSION` and a pressed compression switch to `BUF_COMPRESSION`. This
decode MUST be explicitly verified, as it is the origin of the historical
misnaming.

### REQ-sync-refactor-polarity-fixes-are-isolate
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: Behavior-changing polarity fixes SHALL be committed separately from the
prerequisite rename and from each other, each justified by the specific
contradiction it resolves. The rename change MUST remain behavior-preserving.

### REQ-sync-refactor-sensor-and-control-law-are
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: Live prose SHALL name the sensor (Sync-Feedback Sensor type P/D)
separately from the control law. The dual-switch path's law SHALL be
referred to as the "type-D two-level / hysteretic relay control law" and
the analog path's law as the "type-P PD/EKF (reserve) control law";
Wiring shorthand and "relay" MUST NOT be used as if they named the sensor.

### REQ-sync-refactor-vocabulary-rollout-is-beha
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: This change SHALL NOT alter any control logic, protocol token, config key,
or C symbol; it is prose and documented-contract only. The host build and
a captured status/event snapshot MUST be identical before and after.

### REQ-sync-refactor-type-d-standalone-buffer-c
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The controller SHALL drive the active-lane feed as a two-level / hysteretic
relay law in standalone Sync-Feedback Sensor type D mode
(`BUF_SENSOR_TYPE == 0`, D=0), not a continuous PI loop on a dead-reckoned
position. Per FLARE polarity (negative reserve target,
REFILL effort in TENSION, RELIEVE in COMPRESSION), `BUF_TENSION` is the
empty/starved side and `BUF_COMPRESSION` is the full reserve side. The relay
law SHALL command a strong fixed catch-up rate (off the baseline control
floor) while TENSION is engaged, a **true zero feed (0)** while COMPRESSION is
engaged so no filament is pushed into a full buffer (the output `SYNC_MIN`
clamp MUST be bypassed for COMPRESSION so feed actually reaches 0; the extruder
draw pulls the buffer off the wall and recovery uses the existing relieve /
`SYNC_AUTO_STOP_MS` path), and a demand-tracking rate
(`extruder_est_sps * SYNC_RELAY_NEUTRAL_FRAC`, clamped to `[SYNC_MIN, baseline
floor]`) while in NEUTRAL. The NEUTRAL rate MUST track
extruder demand, not the fixed baseline, so the buffer drifts slowly
instead of slamming the full wall. The existing ramp, rate clamp,
fast-brake and relief logic MUST still apply; the legacy compression floor
MUST be skipped in relay mode (it inherited the old empty/full assumption).

### REQ-sync-refactor-type-d-compression-relief-
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The controller SHALL stop sync feed (enter `RELIEF_PAUSE`) once a small bounded
overfill is reached while the buffer is pinned in `BUF_COMPRESSION` and not
relieving, rather than only after a fixed blind dwell timer. While pinned in
COMPRESSION with the virtual position at or deepening past `-threshold` and the
accumulated relieve effort exceeding a small budget (on the order of 1-2 mm),
the controller SHALL enter `RELIEF_PAUSE` and emit the `RELIEF_PAUSE` event.
This caps the filament force-fed into a full buffer to the budget. Behavior is
gated to `BUF_SENSOR_TYPE == 0`; type-P analog relief is unchanged. The normal
relay limit cycle, which touches COMPRESSION briefly and leaves it via extruder
draw before the budget accrues, MUST NOT trip this early relief.

### REQ-sync-refactor-normal-switch-contact-does
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The controller SHALL NOT trigger FAULT_HOLD on normal COMPRESSION or TENSION
switch contact in type-D standalone mode, because switch contact is the
relay-law control signal. The tension-dwell FAULT_HOLD and the
compression-wall-critical FAULT_HOLD SHALL be gated to type-P analog mode
(`BUF_SENSOR_TYPE != 0`, P=1). Genuine idle/runout handling via the existing
relief and continuous-compression auto-stop paths MUST remain unchanged.

### REQ-sync-refactor-type-d-relief-pause-re-arm
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: The controller SHALL re-arm sync from `RELIEF_PAUSE` to `SYNC_ACTIVE` when the
buffer recovers to `BUF_NEUTRAL` (e.g. via the reverse-relieve service), not only
when it reaches `BUF_TENSION`, in type-D standalone mode (`BUF_SENSOR_TYPE == 0`).
On that re-arm the controller SHALL reseed the virtual position toward the
reserve target and bootstrap the feed rate (as the FAULT_HOLD recovery does), so
a resuming print does not have to drain the buffer to empty before the MMU feeds
again. Type-P analog behavior MUST be unchanged.

### REQ-sync-refactor-type-d-estimator-does-not-
- **Source**: `.planning/specs/sync-refactor/spec.md`
- **Description**: In type-D standalone mode the velocity estimator (`extruder_est_sps`) SHALL NOT
be fully replaced by a value derived from a *modeled* (assumed full-span)
TENSION→COMPRESSION transition. The estimator update for that transition SHALL be
blended or rate-capped so a single short or partial transition cannot spike the
estimate and over-feed the subsequent NEUTRAL band. The TENSION catch-up path
(which refills regardless of the estimator) MUST still apply.

### REQ-sync-state-model-explicit-sync-lifecycle
- **Source**: `.planning/specs/sync-state-model/spec.md`
- **Description**: The sync controller SHALL maintain a single explicit state among
`SYNC_OFF`, `SYNC_ACTIVE`, `SYNC_RETRACT_ASSIST`, `SYNC_RELIEF_PAUSE`, and
`SYNC_FAULT_HOLD`. All sync lifecycle behavior SHALL be derived from this
state rather than independent ad-hoc flags.

### REQ-sync-state-model-non-destructive-relief-
- **Source**: `.planning/specs/sync-state-model/spec.md`
- **Description**: The controller SHALL enter `SYNC_RELIEF_PAUSE` instead of destructive disable
on a sustained compression/overfull condition, preserving the extruder
estimator, drift observer, sigma/confidence, and reserve integrator.

### REQ-sync-state-model-fault-hold-with-autonom
- **Source**: `.planning/specs/sync-state-model/spec.md`
- **Description**: The controller SHALL enter `SYNC_FAULT_HOLD` instead of destructive disable on
a hard-wall / jam condition, stopping the motor while preserving controller
state, and SHALL recover conservatively without host involvement.

### REQ-sync-state-model-retract-assist-gate-is-
- **Source**: `.planning/specs/sync-state-model/spec.md`
- **Description**: The host `BL` buffer-lock command SHALL place the controller in
`SYNC_RETRACT_ASSIST` (the buffer-lock lifecycle state), with normal
closed-loop sync off, post-print negative sync suppressed, controller state
preserved, and learning paused. While locked the gate SHALL NOT react to
buffer changes; on raw departure from the armed extreme it SHALL transition
into an instant-slam catch sub-state. The gate MUST NOT destructively reset
estimator, drift, sigma, or reserve integrator state. The legacy `RA:1` /
`RA:0` host commands and the `RA` status field SHALL be removed; no alias
is provided.

### REQ-sync-state-model-full-bias-invariant-pre
- **Source**: `.planning/specs/sync-state-model/spec.md`
- **Description**: The reserve/full-biased buffer target (between NEUTRAL and COMPRESSION) SHALL remain
owned exclusively by `SYNC_ACTIVE` control and SHALL be unchanged by this
state model. `SYNC_RELIEF_PAUSE` and `SYNC_FAULT_HOLD` SHALL NOT drain the
buffer below the reserve target by design.

### REQ-sync-state-model-creep-suppressed-in-non
- **Source**: `.planning/specs/sync-state-model/spec.md`
- **Description**: `neutral_creep` SHALL be active only in `SYNC_ACTIVE` and SHALL be suppressed in
`SYNC_RETRACT_ASSIST`, `SYNC_RELIEF_PAUSE`, and `SYNC_FAULT_HOLD`.

### REQ-sync-state-model-sync-feedback-sensor-ta
- **Source**: `.planning/specs/sync-state-model/spec.md`
- **Description**: Live sync docs and specs SHALL describe the buffer sensor as a
Sync-Feedback Sensor using Happy Hare type codes: `D` = Dual two-switch
sensor (`BUF_SENSOR_TYPE == 0`), `P` = Proportional analog sensor
(`BUF_SENSOR_TYPE == 1`), `TO` = Tension-Only (not implemented in FLARE),
and `CO` = Compression-Only (not implemented in FLARE). The sensor type
SHALL be named separately from the control law.

### REQ-sync-state-model-sync-feedback-sensor-ta
- **Source**: `.planning/specs/sync-state-model/spec.md`
- **Description**: Documentation and live specs SHALL use the umbrella concept Sync-Feedback
Sensor with Happy Hare's canonical type codes: P (Proportional, analog),
D (Dual, two-switch 3-state), TO (Tension-Only), CO (Compression-Only).
New acronyms (DSF/SFS) MUST NOT be minted, and wiring shorthand or "relay"
MUST NOT be used to denote the sensor in live prose.

### REQ-sync-state-model-buf-sensor-type-value-c
- **Source**: `.planning/specs/sync-state-model/spec.md`
- **Description**: Every live reference to `BUF_SENSOR_TYPE` SHALL document the value
contract as `D = 0` and `P = 1`. The integer values MUST remain unchanged
by this change.

### REQ-sync-state-model-to-and-co-documented-as
- **Source**: `.planning/specs/sync-state-model/spec.md`
- **Description**: Documentation SHALL list TO and CO as recognized Happy Hare Sync-Feedback
Sensor types that are not implemented in FLARE, so the taxonomy is
complete without implying FLARE support.

### REQ-task-workflow-load-context-first
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: Agents MUST read `AGENTS.md`, `openspec/README.md`, and relevant specs before starting work.

### REQ-task-workflow-openspec-changes
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: Agents SHALL record findings and a file-level plan in `openspec/changes/<id>/` before implementation.

### REQ-task-workflow-record-completion
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: The implementer SHALL update the change task list and target spec after durable work is complete.

### REQ-task-workflow-no-root-task-md
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: Handoff and scratch notes SHALL belong in `openspec/changes/` while active.

### REQ-task-workflow-small-commits
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: The agent MUST commit and push small, attributed units of work promptly.

### REQ-task-workflow-no-local-ai-config-in-comm
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: The repository SHALL keep `.agents/`, `.claude/`, `.gemini/` etc. OUT of the commits.

### REQ-task-workflow-ai-assisted-commit-attribu
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: Commits MUST retain the Claude `Co-Authored-By` trailer. When code in a
commit was generated or substantially assisted by another AI tool, the
commit MUST additionally carry a `Generated-By: <tool> (<model>)` trailer
— in addition to, not replacing, the Claude `Co-Authored-By` line. If
multiple tools contributed, each MUST appear on its own `Generated-By:`
line.

### REQ-task-workflow-tasks-file-completion-hygi
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: Implementation MUST NOT empty, truncate, or delete the content of a
change's `tasks.md`. Completing work MUST mark the corresponding items
`[x]` and MAY append dated validation notes beneath them. Task history
MUST remain reconstructable from `tasks.md` at archive time.

### REQ-task-workflow-doc-read-modes
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: The `AGENTS.md` Key Files table SHALL tag each entry with a read mode: `[always]` (read every session) or `[lookup]` (grep on demand, never wholesale). Agents MUST NOT read `[lookup]` docs wholesale; they grep the topic and read matched sections only. At minimum `MANUAL.md`, `BEHAVIOR.md`, `TEST_CASES.md`, `TUNING.md`, and `openspec/changes/archive/**` are `[lookup]`.

### REQ-task-workflow-flow-triage
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: Agents SHALL route work between direct implementation and the OpenSpec flow using measurable criteria. Direct only when ALL hold: no spec'd-behavior change (`grep -ril '<topic>' openspec/specs/` empty, or hits but behavior unchanged); no `settings_t`, protocol command, or runtime-tunable surface change; at most 2–3 files touched; single session; no hardware validation needed. Any other case — or uncertainty — SHALL use the OpenSpec flow (misrouted direct work loses spec sync; misrouted OpenSpec work loses only tokens).

### REQ-task-workflow-readiness-and-delivery-che
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: Generated `tasks.md` SHALL end with a final section named "Readiness and Delivery Checks" whose items gate archiving. The section MUST require: dev-tuning superset build passes (`ninja -C build_local` configured with `-DFLARE_DEV_TUNING=ON`) for firmware-touching changes; `python3 -m py_compile scripts/*.py` for script-touching changes; documentation sync verified for renamed/added parameters; `openspec validate <change-name> --strict` and `openspec validate --specs --strict` pass; and the team memory observation `memories/repo/<change-name>.md` appended.

### REQ-task-workflow-self-contained-tasks
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: Every task in generated `tasks.md` SHALL name its target file path, the exact change, and specific acceptance criteria so it can be executed without re-reading proposal or design. Mechanical steps SHALL be expressed as CLI commands rather than manual-edit instructions.

### REQ-task-workflow-hardware-task-tagging
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: Generated `tasks.md` SHALL prefix every hardware-dependent validation task with `HW:`. `HW:` tasks MUST NOT be checked off without explicit user confirmation backed by real-hardware test results.

### REQ-task-workflow-compression-tiers
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: OpenSpec prose SHALL follow two compression tiers: `openspec/specs/**` stays lightly compressed or uncompressed (stable long-lived contracts, human readability paramount); `openspec/changes/**` artifact prose is fully compressed per `openspec/COMPRESSION.md` (iteration-heavy drafts). The tier split is forward-only: existing compressed specs are not rewritten for style alone.

### REQ-task-workflow-pre-commit-self-review
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: Before committing non-trivial code changes, agents SHALL review the staged diff against the `REVIEW.md` checklist (settings versioning, protocol parity, config wiring, build superset, doc sync, regression impact) instead of re-reading full rule documents. Doc-only commits MAY skip the checklist.

### REQ-task-workflow-targeted-output-edits
- **Source**: `.planning/specs/task-workflow/spec.md`
- **Description**: Agents SHALL report edits as targeted changes only: never echo unchanged code blocks into chat, commit messages, or PR descriptions; reference file paths and line ranges instead.

### REQ-toolchange-orchestration-full-automated-
- **Source**: `.planning/specs/toolchange-orchestration/spec.md`
- **Description**: The system SHALL orchestrate an automated sequence to swap active lanes without host intervention.

### REQ-toolchange-orchestration-manual-cutter-e
- **Source**: `.planning/specs/toolchange-orchestration/spec.md`
- **Description**: The host SHALL be able to trigger the exact cutter sequence independently of a full toolchange.

### REQ-toolchange-orchestration-manual-unload-s
- **Source**: `.planning/specs/toolchange-orchestration/spec.md`
- **Description**: Manual MMU unload SHALL accept `UM`, `UM:`, `UM:1`, and `UM:2`. `UM` and
`UM:` SHALL preserve active-lane behavior. `UM:n` SHALL target the explicit
lane without changing `active_lane`.

### REQ-toolchange-orchestration-reload-buffer-d
- **Source**: `.planning/specs/toolchange-orchestration/spec.md`
- **Description**: During runout RELOAD, the new lane SHALL approach until physical buffer contact is detected.

### REQ-toolchange-orchestration-reload-bang-ban
- **Source**: `.planning/specs/toolchange-orchestration/spec.md`
- **Description**: During the RELOAD follow phase, the new lane SHALL over-feed to close the gap and maintain pressure on the old tail.

### REQ-type-d-dynamic-flow-a-tension-touch-slam
- **Source**: `.planning/specs/type-d-dynamic-flow/spec.md`
- **Description**: For `BUF_SENSOR_TYPE == 0`, a crossing into `BUF_TENSION` SHALL set a recovery
feed floor `SYNC_TENSION_RECOVERY_FLOOR` (≈ the fast-segment / catchup rate) and
SHALL apply that floor as a lower bound on the `BUF_NEUTRAL` relay feed, decaying
the floor to zero over `SYNC_TENSION_RECOVERY_MS`. The floor SHALL be a feed-side
term independent of `extruder_est_sps` so it is not pulled back down by the
NEUTRAL-fill / COMPRESSION-drain estimator samples during the recovery cycle.
This holds NEUTRAL feed high through the recovery window so the buffer does not
re-drain into a second/third TENSION touch (the burst), collapsing each
demand-step event to a single tension touch. The floor SHALL hand off to
`extruder_est_sps` as it decays (by which time the estimator has caught the new
demand). The recovery floor SHALL apply only in `BUF_NEUTRAL`: when its aggressive
feed overshoots the buffer into `BUF_COMPRESSION`, the floor SHALL stop applying
(it is NEUTRAL-only) so the existing COMPRESSION gated-drain / true-stop and
relieve budget stabilize the buffer off the rail — the floor SHALL NOT fight the
drain. A brief COMPRESSION-side excursion (one or a few transient touches) during
the recovery window is accepted as the cost of preventing the re-drain tension
burst, provided it stabilizes via the COMPRESSION path. This SHALL NOT alter
analog type-P.

### REQ-type-d-dynamic-flow-slow-drift-protectio
- **Source**: `.planning/specs/type-d-dynamic-flow/spec.md`
- **Description**: For `BUF_SENSOR_TYPE == 0`, slow-print anti-tension protection SHALL be provided
by the compression-side reserve bias (`SYNC_RESERVE_PCT`), which parks the buffer
off the TENSION rail, NOT by a high `SYNC_MIN_RATE` feed floor. The shipped
default `SYNC_MIN_RATE` SHALL be a quiet (low) value so real prints are not forced
into constant COMPRESSION clicking. `SYNC_MIN_RATE` SHALL remain operator-tunable
for those who prefer the loud zero-fast-step-skip behavior, and TUNING.md SHALL
document the trade-off.

### REQ-type-p-sync-relief-bounded-refill
- **Source**: `.planning/phases/13-type-p-sync-relief-fault-trip/13-RESEARCH.md`
- **Description**: The urgent-refill branch in the type-P TENSION soft wall SHALL target
`min(max_sps, max(est × SYNC_PSF_RELIEF_MULT, baseline_sps))`, never `max_sps` directly,
applied through the existing distance-EMA/slew path with a doubled slew cap while pegged.

### REQ-type-p-sync-relief-distance-trip
- **Source**: `.planning/phases/13-type-p-sync-relief-fault-trip/13-RESEARCH.md`
- **Description**: A new `SYNC_TENSION_STOP_MM` SHALL accumulate raw lane feed travel while
`BUF_TENSION` and trip `FAULT_HOLD` alongside the existing ms dwell trip, both arming only
after the first observed buffer-state transition in the active-sync window.

### REQ-type-p-sync-relief-feed-probe
- **Source**: `.planning/phases/13-type-p-sync-relief-fault-trip/13-RESEARCH.md`
- **Description**: A bounded, passive firmware-local probe SHALL distinguish "home rail, no
consumer" from "starved" at type-P +1.0 tension using the same accumulator, evaluated at
`PROBE_MM` before the mm trip fires.

### REQ-type-p-sync-relief-rail-relative-triggers
- **Source**: `.planning/phases/13-type-p-sync-relief-fault-trip/13-RESEARCH.md`
- **Description**: Every new threshold (relief snap, mm trip, probe) SHALL be expressed
relative to an observed per-window tension extreme, never as an absolute `±0.xx` compare,
mirroring the `51bdca8` BL fix.

### REQ-type-p-sync-relief-sim-coverage
- **Source**: `.planning/phases/13-type-p-sync-relief-fault-trip/13-RESEARCH.md`
- **Description**: `flare_sim` SHALL cover refill-without-overshoot, mm-trip vs ms-trip
ordering, and probe outcomes at both `type_p_rail_scale` 1.0 and 0.7 (plus a 0.5 tripwire),
with no regression in the existing type-P scenario set.

### REQ-type-p-sync-relief-no-relay-reintroduction
- **Source**: `.planning/phases/13-type-p-sync-relief-fault-trip/13-RESEARCH.md`
- **Description**: Nothing from the type-D relay path (confident estimator, mid-band
estimator, EST pivots) SHALL be reintroduced; `HW:` items remain unchecked until rig
validation.

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| REQ-acceptance-gate-parity-shared-recommenda | Phase 1 | Pending |
| REQ-acceptance-gate-parity-backward-compatib | Phase 2 | Pending |
| REQ-acceptance-gate-parity-run-classificatio | Phase 3 | Pending |
| REQ-acceptance-gate-parity-diagnostic-visibi | Phase 4 | Pending |
| REQ-acceptance-gate-parity-contributor-mass- | Phase 5 | Pending |
| REQ-acceptance-gate-parity-placeholder-telem | Phase 6 | Pending |
| REQ-acceptance-gate-semantics-separate-rejec | Phase 1 | Pending |
| REQ-acceptance-gate-semantics-floored-denomi | Phase 2 | Pending |
| REQ-acceptance-gate-semantics-mass-gray-band | Phase 3 | Pending |
| REQ-acceptance-gate-semantics-sigma-ceiling | Phase 4 | Pending |
| REQ-acceptance-gate-semantics-soak-maturity | Phase 5 | Pending |
| REQ-acceptance-gate-semantics-glob-input | Phase 6 | Pending |
| REQ-analyzer-rigor-relative-noise-gate | Phase 1 | Pending |
| REQ-analyzer-rigor-safe-mode-enforcement | Phase 2 | Pending |
| REQ-analyzer-rigor-explicit-bootstrap-paths | Phase 3 | Pending |
| REQ-analyzer-rigor-precision-weighted-recomm | Phase 4 | Pending |
| REQ-analyzer-rigor-bp-derived-sigma | Phase 5 | Pending |
| REQ-analyzer-rigor-contributors-visibility | Phase 6 | Pending |
| REQ-bucket-locking-bucket-schema-4-shall-pre | Phase 1 | Pending |
| REQ-bucket-locking-schema-3-state-shall-migr | Phase 2 | Pending |
| REQ-bucket-locking-unlocking-shall-use-three | Phase 3 | Pending |
| REQ-bucket-locking-locking-shall-be-noise-ga | Phase 4 | Pending |
| REQ-bucket-locking-verbose-state-info-shall- | Phase 5 | Pending |
| REQ-buffer-geometry-vocabulary-buffer-geomet | Phase 6 | Pending |
| REQ-buffer-geometry-vocabulary-full-range-to | Phase 1 | Pending |
| REQ-buffer-geometry-vocabulary-emu-sync-defa | Phase 2 | Pending |
| REQ-buffer-geometry-vocabulary-switch-span-a | Phase 3 | Pending |
| REQ-buffer-geometry-vocabulary-serial-vocabu | Phase 4 | Pending |
| REQ-buffer-geometry-vocabulary-type-p-analog | Phase 5 | Pending |
| REQ-buffer-state-lock-bl-command-surface | Phase 6 | Pending |
| REQ-buffer-state-lock-bounded-half-travel-pr | Phase 1 | Pending |
| REQ-buffer-state-lock-locked-hold-contract | Phase 2 | Pending |
| REQ-buffer-state-lock-lock-break-on-external | Phase 3 | Pending |
| REQ-buffer-state-lock-instant-slam-catch-wit | Phase 4 | Pending |
| REQ-buffer-state-lock-manual-release-via-bs | Phase 5 | Pending |
| REQ-buffer-state-lock-locked-state-watchdog | Phase 6 | Pending |
| REQ-calibration-workflow-calibration-shall-b | Phase 1 | Pending |
| REQ-calibration-workflow-state-schema-migrat | Phase 2 | Pending |
| REQ-calibration-workflow-bucket-locking-shal | Phase 3 | Pending |
| REQ-calibration-workflow-analyzer-patches-sh | Phase 4 | Pending |
| REQ-calibration-workflow-long-running-daemon | Phase 5 | Pending |
| REQ-calibration-workflow-firmware-live-learn | Phase 6 | Pending |
| REQ-calibration-workflow-deterministic-dual- | Phase 1 | Pending |
| REQ-calibration-workflow-offline-analyzer-re | Phase 2 | Pending |
| REQ-calibration-workflow-analyzer-emits-a-de | Phase 3 | Pending |
| REQ-code-style-standard-enforced-format-conf | Phase 4 | Pending |
| REQ-code-style-standard-local-lint-invocatio | Phase 5 | Pending |
| REQ-code-style-standard-naming-conventions | Phase 6 | Pending |
| REQ-code-style-standard-file-and-function-si | Phase 1 | Pending |
| REQ-code-style-standard-magic-number-policy | Phase 2 | Pending |
| REQ-code-style-standard-comprehension-commen | Phase 3 | Pending |
| REQ-code-style-standard-doc-comment-format-a | Phase 4 | Pending |
| REQ-code-style-standard-behavior-preserving- | Phase 5 | Pending |
| REQ-code-style-standard-shared-constants-are | Phase 6 | Pending |
| REQ-code-style-standard-global-naming-conven | Phase 1 | Pending |
| REQ-config-surface-tiers-configuration-param | Phase 2 | Pending |
| REQ-config-surface-tiers-internal-constants- | Phase 3 | Pending |
| REQ-config-surface-tiers-a-dev-build-may-exp | Phase 4 | Pending |
| REQ-config-surface-tiers-demoted-keys-are-mi | Phase 5 | Pending |
| REQ-cross-platform-script-tooling-all-operat | Phase 6 | Pending |
| REQ-cross-platform-script-tooling-linux-and- | Phase 1 | Pending |
| REQ-cross-platform-script-tooling-no-inline- | Phase 2 | Pending |
| REQ-cross-platform-script-tooling-color-outp | Phase 3 | Pending |
| REQ-cutter-feed-timeout-cutter-feed-timeout- | Phase 4 | Pending |
| REQ-cutter-feed-timeout-cutter-settle-timeou | Phase 5 | Pending |
| REQ-daemon-klipper-mirror-delta-set-mmu-mirr | Phase 6 | Pending |
| REQ-daemon-klipper-mirror-full-resync-recove | Phase 1 | Pending |
| REQ-daemon-klipper-mirror-gate-state-diagnos | Phase 2 | Pending |
| REQ-daemon-klipper-mirror-host-busy-backpres | Phase 3 | Pending |
| REQ-deterministic-tuning-workflow-two-profil | Phase 4 | Pending |
| REQ-deterministic-tuning-workflow-live-tuner | Phase 5 | Pending |
| REQ-deterministic-tuning-workflow-tuning-wor | Phase 6 | Pending |
| REQ-filament-bypass-local-filament-bypass-st | Phase 1 | Pending |
| REQ-filament-bypass-single-sensor-bypass-tel | Phase 2 | Pending |
| REQ-filament-bypass-manual-feed-and-autoload | Phase 3 | Pending |
| REQ-filament-bypass-mmu-free-extruder-only-a | Phase 4 | Pending |
| REQ-filament-bypass-suppressed-eject-under-b | Phase 5 | Pending |
| REQ-flow-keyed-schedule-versioned-bounded-fl | Phase 6 | Pending |
| REQ-flow-keyed-schedule-degenerate-single-po | Phase 1 | Pending |
| REQ-flow-keyed-schedule-firmware-interpolate | Phase 2 | Pending |
| REQ-flow-keyed-schedule-schedule-emission-is | Phase 3 | Pending |
| REQ-klipper-integration-host-serial-control | Phase 4 | Pending |
| REQ-klipper-integration-motion-tracking-side | Phase 5 | Pending |
| REQ-klipper-integration-macro-orchestration | Phase 6 | Pending |
| REQ-klipper-integration-reusable-toolhead-un | Phase 1 | Pending |
| REQ-klipper-integration-dashboard-load-and-e | Phase 2 | Pending |
| REQ-klipper-integration-toolhead-sensor-opti | Phase 3 | Pending |
| REQ-klipper-integration-klipper-md-scope-is- | Phase 4 | Pending |
| REQ-klipper-integration-toolhead-sensor-sect | Phase 5 | Pending |
| REQ-klipper-integration-automatic-bypass-too | Phase 6 | Pending |
| REQ-klipper-integration-slicer-toolchange-by | Phase 1 | Pending |
| REQ-klipper-mmu-config-single-file-klipper-m | Phase 2 | Pending |
| REQ-klipper-mmu-config-variables-block-with- | Phase 3 | Pending |
| REQ-klipper-mmu-config-tip-forming-macro-wit | Phase 4 | Pending |
| REQ-klipper-mmu-config-load-hotend-macro-wit | Phase 5 | Pending |
| REQ-klipper-mmu-config-purge-helper-is-simpl | Phase 6 | Pending |
| REQ-klipper-mmu-config-manual-load-and-eject | Phase 1 | Pending |
| REQ-klipper-mmu-config-toolchange-macro-with | Phase 2 | Pending |
| REQ-klipper-mmu-config-boot-delayed-gcode-se | Phase 3 | Pending |
| REQ-klipper-mmu-config-tip-forming-test-macr | Phase 4 | Pending |
| REQ-klipper-mmu-config-removed-development-m | Phase 5 | Pending |
| REQ-klipper-mmu-config-preload-macro-routes- | Phase 6 | Pending |
| REQ-klipper-motion-tracking-sidecar-metadata | Phase 1 | Pending |
| REQ-klipper-motion-tracking-uds-ingress-pari | Phase 2 | Pending |
| REQ-klipper-motion-tracking-stable-matcher-s | Phase 3 | Pending |
| REQ-klipper-motion-tracking-host-only-integr | Phase 4 | Pending |
| REQ-klipper-motion-tracking-fallback-paths | Phase 5 | Pending |
| REQ-live-tuner-per-feature-velocity-buckets | Phase 6 | Pending |
| REQ-live-tuner-machine-scoped-persistence | Phase 1 | Pending |
| REQ-live-tuner-observe-only-default | Phase 2 | Pending |
| REQ-live-tuner-review-only-workflow | Phase 3 | Pending |
| REQ-live-tuner-diagnostics | Phase 4 | Pending |
| REQ-live-tuner-sync-state-and-relief-effort- | Phase 5 | Pending |
| REQ-live-tuner-relief-effort-counters-are-ac | Phase 6 | Pending |
| REQ-live-tuner-warn-only-effort-threshold-ev | Phase 1 | Pending |
| REQ-marker-capture-policy-sidecar-is-the-onl | Phase 2 | Pending |
| REQ-marker-capture-policy-no-deprecation-not | Phase 3 | Pending |
| REQ-marker-capture-policy-docs-describe-curr | Phase 4 | Pending |
| REQ-motion-safety-dry-spin-protection | Phase 5 | Pending |
| REQ-motion-safety-task-travel-limits | Phase 6 | Pending |
| REQ-motion-safety-safe-autopreload | Phase 1 | Pending |
| REQ-motion-safety-non-destructive-jam-relief | Phase 2 | Pending |
| REQ-motion-safety-under-extrusion-direction- | Phase 3 | Pending |
| REQ-motion-safety-terminal-jam-paths-enter-n | Phase 4 | Pending |
| REQ-motion-safety-fault-hold-auto-recovers-w | Phase 5 | Pending |
| REQ-motion-safety-bl-prime-respects-travel-c | Phase 6 | Pending |
| REQ-motion-safety-bl-catch-bypasses-mv-buffe | Phase 1 | Pending |
| REQ-operator-tuning-guide-self-contained-jar | Phase 2 | Pending |
| REQ-operator-tuning-guide-exact-copy-paste-c | Phase 3 | Pending |
| REQ-operator-tuning-guide-recovery-path-for- | Phase 4 | Pending |
| REQ-operator-tuning-guide-sidecar-is-the-onl | Phase 5 | Pending |
| REQ-operator-tuning-guide-two-profile-determ | Phase 6 | Pending |
| REQ-operator-tuning-guide-apply-recommender- | Phase 1 | Pending |
| REQ-operator-tuning-guide-sync-feedback-sens | Phase 2 | Pending |
| REQ-operator-tuning-guide-tuning-md-uses-syn | Phase 3 | Pending |
| REQ-operator-tuning-guide-tuning-md-relay-se | Phase 4 | Pending |
| REQ-operator-tuning-guide-type-d-tuning-guid | Phase 5 | Pending |
| REQ-operator-tuning-guide-default-relay-neut | Phase 6 | Pending |
| REQ-operator-tuning-guide-type-d-relay-trim- | Phase 1 | Pending |
| REQ-operator-tuning-guide-type-d-compression | Phase 2 | Pending |
| REQ-operator-tuning-guide-asymmetric-relay-c | Phase 3 | Pending |
| REQ-operator-tuning-guide-type-d-estimator-a | Phase 4 | Pending |
| REQ-operator-tuning-guide-type-d-reserve-tar | Phase 5 | Pending |
| REQ-operator-tuning-guide-type-d-estimator-a | Phase 6 | Pending |
| REQ-persistence-contract-settings-version-bu | Phase 1 | Pending |
| REQ-persistence-contract-flash-loading-and-d | Phase 2 | Pending |
| REQ-persistence-contract-runtime-tunables-fl | Phase 3 | Pending |
| REQ-persistence-contract-persisted-fields-ro | Phase 4 | Pending |
| REQ-persistence-contract-settings-round-trip | Phase 5 | Pending |
| REQ-project-architecture-firmware-shall-rema | Phase 6 | Pending |
| REQ-project-architecture-module-ownership-sh | Phase 1 | Pending |
| REQ-project-architecture-runtime-tunables-sh | Phase 2 | Pending |
| REQ-project-architecture-serial-protocol-cha | Phase 3 | Pending |
| REQ-project-architecture-persistence-shall-r | Phase 4 | Pending |
| REQ-project-architecture-sync-shall-not-run- | Phase 5 | Pending |
| REQ-project-architecture-load-and-unload-saf | Phase 6 | Pending |
| REQ-project-architecture-shared-speed-conver | Phase 1 | Pending |
| REQ-project-architecture-board-pin-assumptio | Phase 2 | Pending |
| REQ-project-architecture-buffer-service-comm | Phase 3 | Pending |
| REQ-psf-type-p-sensor-type-p-relief-pause-au | Phase 4 | Pending |
| REQ-psf-type-p-sensor-type-p-stabilize-rail- | Phase 5 | Pending |
| REQ-psf-type-p-sensor-type-p-tension-refill- | Phase 6 | Pending |
| REQ-psf-type-p-sensor-psf-endpoint-calibrati | Phase 1 | Pending |
| REQ-psf-type-p-sensor-asymmetric-normalizati | Phase 2 | Pending |
| REQ-psf-type-p-sensor-goal-relative-zone-bou | Phase 3 | Pending |
| REQ-psf-type-p-sensor-buf-goal-user-param-in | Phase 4 | Pending |
| REQ-psf-type-p-sensor-remove-buf-range-and-b | Phase 5 | Pending |
| REQ-psf-type-p-sensor-continuous-extruder-es | Phase 6 | Pending |
| REQ-psf-type-p-sensor-gradual-pd-control-wit | Phase 1 | Pending |
| REQ-psf-type-p-sensor-filtered-derivative | Phase 2 | Pending |
| REQ-psf-type-p-sensor-soft-walls | Phase 3 | Pending |
| REQ-psf-type-p-sensor-hard-catch-and-print-s | Phase 4 | Pending |
| REQ-psf-type-p-sensor-type-p-unload-uses-no- | Phase 5 | Pending |
| REQ-psf-type-p-sensor-type-p-fault-timers-sc | Phase 6 | Pending |
| REQ-psf-type-p-sensor-type-p-feed-quality-an | Phase 1 | Pending |
| REQ-python-host-tooling-style-ruff-lint-conf | Phase 2 | Pending |
| REQ-python-host-tooling-style-python-lint-in | Phase 3 | Pending |
| REQ-python-host-tooling-style-behavior-prese | Phase 4 | Pending |
| REQ-python-host-tooling-style-diagnostic-scr | Phase 5 | Pending |
| REQ-relay-fallback-only-type-d-relay-neutral | Phase 6 | Pending |
| REQ-relay-fallback-only-firmware-drops-the-d | Phase 1 | Pending |
| REQ-relay-fallback-only-protocol-drops-estim | Phase 2 | Pending |
| REQ-relay-fallback-only-analyzer-emits-no-re | Phase 3 | Pending |
| REQ-relay-fallback-only-config-surface-drops | Phase 4 | Pending |
| REQ-reserve-safety-floor-reserve-bias-is-flo | Phase 5 | Pending |
| REQ-reserve-safety-floor-baseline-control-fl | Phase 6 | Pending |
| REQ-reserve-safety-floor-degenerate-single-p | Phase 1 | Pending |
| REQ-reserve-safety-floor-schedule-and-live-l | Phase 2 | Pending |
| REQ-script-path-handling-tilde-and-glob-expa | Phase 3 | Pending |
| REQ-script-path-handling-output-paths-are-ne | Phase 4 | Pending |
| REQ-script-path-handling-path-errors-produce | Phase 5 | Pending |
| REQ-script-path-handling-existing-analyzer-i | Phase 6 | Pending |
| REQ-static-regression-validation-automated-r | Phase 1 | Pending |
| REQ-static-regression-validation-standard-te | Phase 2 | Pending |
| REQ-static-regression-validation-dev-tuning- | Phase 3 | Pending |
| REQ-sync-feedback-compression-recovery-cap-g | Phase 4 | Pending |
| REQ-sync-feedback-common-normalized-scale-fo | Phase 5 | Pending |
| REQ-sync-feedback-isolated-control-laws | Phase 6 | Pending |
| REQ-sync-feedback-sync-apply-scaling-unified | Phase 1 | Pending |
| REQ-sync-feedback-compression-floor-removed- | Phase 2 | Pending |
| REQ-sync-refactor-foundation-firmware-sync-s | Phase 3 | Pending |
| REQ-sync-refactor-foundation-sync-hardening- | Phase 4 | Pending |
| REQ-sync-refactor-foundation-runtime-tunable | Phase 5 | Pending |
| REQ-sync-refactor-foundation-telemetry-shall | Phase 6 | Pending |
| REQ-sync-refactor-foundation-regression-impa | Phase 1 | Pending |
| REQ-sync-refactor-standalone-sync | Phase 2 | Pending |
| REQ-sync-refactor-observe-only-calibration | Phase 3 | Pending |
| REQ-sync-refactor-sidecar-uds-tracking | Phase 4 | Pending |
| REQ-sync-refactor-durable-migratable-state | Phase 5 | Pending |
| REQ-sync-refactor-chatter-resistance | Phase 6 | Pending |
| REQ-sync-refactor-relative-noise-gate | Phase 1 | Pending |
| REQ-sync-refactor-state-aware-recommendation | Phase 2 | Pending |
| REQ-sync-refactor-comparable-run-consistency | Phase 3 | Pending |
| REQ-sync-refactor-fail-vs-warn-separation | Phase 4 | Pending |
| REQ-sync-refactor-bidirectional-drift-observ | Phase 5 | Pending |
| REQ-sync-refactor-double-integrator-avoidanc | Phase 6 | Pending |
| REQ-sync-refactor-bias-accumulation | Phase 1 | Pending |
| REQ-sync-refactor-disciplined-live-baseline- | Phase 2 | Pending |
| REQ-sync-refactor-non-destructive-lifecycle- | Phase 3 | Pending |
| REQ-sync-refactor-baseline-and-bias-sourced- | Phase 4 | Pending |
| REQ-sync-refactor-live-learner-ratchets-with | Phase 5 | Pending |
| REQ-sync-refactor-buffer-states-use-tension- | Phase 6 | Pending |
| REQ-sync-refactor-serial-protocol-tokens-and | Phase 1 | Pending |
| REQ-sync-refactor-config-keys-are-renamed-to | Phase 2 | Pending |
| REQ-sync-refactor-the-rename-does-not-change | Phase 3 | Pending |
| REQ-sync-refactor-sync-control-polarity-matc | Phase 4 | Pending |
| REQ-sync-refactor-pin-to-state-decode-is-ver | Phase 5 | Pending |
| REQ-sync-refactor-polarity-fixes-are-isolate | Phase 6 | Pending |
| REQ-sync-refactor-sensor-and-control-law-are | Phase 1 | Pending |
| REQ-sync-refactor-vocabulary-rollout-is-beha | Phase 2 | Pending |
| REQ-sync-refactor-type-d-standalone-buffer-c | Phase 3 | Pending |
| REQ-sync-refactor-type-d-compression-relief- | Phase 4 | Pending |
| REQ-sync-refactor-normal-switch-contact-does | Phase 5 | Pending |
| REQ-sync-refactor-type-d-relief-pause-re-arm | Phase 6 | Pending |
| REQ-sync-refactor-type-d-estimator-does-not- | Phase 1 | Pending |
| REQ-sync-state-model-explicit-sync-lifecycle | Phase 2 | Pending |
| REQ-sync-state-model-non-destructive-relief- | Phase 3 | Pending |
| REQ-sync-state-model-fault-hold-with-autonom | Phase 4 | Pending |
| REQ-sync-state-model-retract-assist-gate-is- | Phase 5 | Pending |
| REQ-sync-state-model-full-bias-invariant-pre | Phase 6 | Pending |
| REQ-sync-state-model-creep-suppressed-in-non | Phase 1 | Pending |
| REQ-sync-state-model-sync-feedback-sensor-ta | Phase 2 | Pending |
| REQ-sync-state-model-sync-feedback-sensor-ta | Phase 3 | Pending |
| REQ-sync-state-model-buf-sensor-type-value-c | Phase 4 | Pending |
| REQ-sync-state-model-to-and-co-documented-as | Phase 5 | Pending |
| REQ-task-workflow-load-context-first | Phase 6 | Pending |
| REQ-task-workflow-openspec-changes | Phase 1 | Pending |
| REQ-task-workflow-record-completion | Phase 2 | Pending |
| REQ-task-workflow-no-root-task-md | Phase 3 | Pending |
| REQ-task-workflow-small-commits | Phase 4 | Pending |
| REQ-task-workflow-no-local-ai-config-in-comm | Phase 5 | Pending |
| REQ-task-workflow-ai-assisted-commit-attribu | Phase 6 | Pending |
| REQ-task-workflow-tasks-file-completion-hygi | Phase 1 | Pending |
| REQ-task-workflow-doc-read-modes | Phase 2 | Pending |
| REQ-task-workflow-flow-triage | Phase 3 | Pending |
| REQ-task-workflow-readiness-and-delivery-che | Phase 4 | Pending |
| REQ-task-workflow-self-contained-tasks | Phase 5 | Pending |
| REQ-task-workflow-hardware-task-tagging | Phase 6 | Pending |
| REQ-task-workflow-compression-tiers | Phase 1 | Pending |
| REQ-task-workflow-pre-commit-self-review | Phase 2 | Pending |
| REQ-task-workflow-targeted-output-edits | Phase 3 | Pending |
| REQ-toolchange-orchestration-full-automated- | Phase 4 | Pending |
| REQ-toolchange-orchestration-manual-cutter-e | Phase 5 | Pending |
| REQ-toolchange-orchestration-manual-unload-s | Phase 6 | Pending |
| REQ-toolchange-orchestration-reload-buffer-d | Phase 1 | Pending |
| REQ-toolchange-orchestration-reload-bang-ban | Phase 2 | Pending |
| REQ-type-d-dynamic-flow-a-tension-touch-slam | Phase 3 | Pending |
| REQ-type-d-dynamic-flow-slow-drift-protectio | Phase 4 | Pending |
