# Synthesized Requirements

## REQ-acceptance-gate-parity-shared-recommenda
- source: openspec/specs/acceptance-gate-parity/spec.md
- description: The gate SHALL compare per-run recommendations from the same state-aware path as the patch.
- acceptance: Scenarios in spec satisfied
- scope: acceptance, gate, parity

## REQ-acceptance-gate-parity-backward-compatib
- source: openspec/specs/acceptance-gate-parity/spec.md
- description: `compute_recommendations` SHALL retain dictionary shape and semantics for existing callers.
- acceptance: Scenarios in spec satisfied
- scope: acceptance, gate, parity

## REQ-acceptance-gate-parity-run-classificatio
- source: openspec/specs/acceptance-gate-parity/spec.md
- description: The system SHALL classify runs (comparable or skipped) before checking consistency deltas.
- acceptance: Scenarios in spec satisfied
- scope: acceptance, gate, parity

## REQ-acceptance-gate-parity-diagnostic-visibi
- source: openspec/specs/acceptance-gate-parity/spec.md
- description: The generated patch MUST include per-run estimates regardless of gate outcome.
- acceptance: Scenarios in spec satisfied
- scope: acceptance, gate, parity

## REQ-acceptance-gate-parity-contributor-mass-
- source: openspec/specs/acceptance-gate-parity/spec.md
- description: The acceptance gate SHALL FAIL only on contributor mass, and WARN on raw row coverage.
- acceptance: Scenarios in spec satisfied
- scope: acceptance, gate, parity

## REQ-acceptance-gate-parity-placeholder-telem
- source: openspec/specs/acceptance-gate-parity/spec.md
- description: The analyzer MUST mark telemetry counters as pending until real log parsing exists.
- acceptance: Scenarios in spec satisfied
- scope: acceptance, gate, parity

## REQ-acceptance-gate-semantics-separate-rejec
- source: openspec/specs/acceptance-gate-semantics/spec.md
- description: The gate SHALL FAIL ONLY on reliability issues; stale config and incomplete soak are warnings.
- acceptance: Scenarios in spec satisfied
- scope: acceptance, gate, semantics

## REQ-acceptance-gate-semantics-floored-denomi
- source: openspec/specs/acceptance-gate-semantics/spec.md
- description: The system SHALL avoid penalizing the operator for many immature buckets in mass calculation.
- acceptance: Scenarios in spec satisfied
- scope: acceptance, gate, semantics

## REQ-acceptance-gate-semantics-mass-gray-band
- source: openspec/specs/acceptance-gate-semantics/spec.md
- description: The gate SHALL issue a WARNING when mass is between PASS and FAIL thresholds.
- acceptance: Scenarios in spec satisfied
- scope: acceptance, gate, semantics

## REQ-acceptance-gate-semantics-sigma-ceiling
- source: openspec/specs/acceptance-gate-semantics/spec.md
- description: The analyzer SHALL warn and recommend correction when BP sigma is between reference and 5.0 mm.
- acceptance: Scenarios in spec satisfied
- scope: acceptance, gate, semantics

## REQ-acceptance-gate-semantics-soak-maturity
- source: openspec/specs/acceptance-gate-semantics/spec.md
- description: The system MUST report run-count and duration without hiding stable recommendations.
- acceptance: Scenarios in spec satisfied
- scope: acceptance, gate, semantics

## REQ-acceptance-gate-semantics-glob-input
- source: openspec/specs/acceptance-gate-semantics/spec.md
- description: The analyzer SHALL support shell-expanded CSV groups using the `--in` flag.
- acceptance: Scenarios in spec satisfied
- scope: acceptance, gate, semantics

## REQ-agent-comms-mode-portable-caveman-comms-
- source: openspec/specs/agent-comms-mode/spec.md
- description: The project SHALL maintain an in-repo file `openspec/COMMS.md` that fully defines the caveman-full chat-response style, such that any agent UI can adopt it by reading the file alone, with no dependency on a Claude-specific skill or plugin.
- acceptance: Scenarios in spec satisfied
- scope: agent, comms, mode

## REQ-agent-comms-mode-caveman-full-is-the-def
- source: openspec/specs/agent-comms-mode/spec.md
- description: The repository SHALL direct every agent, via `AGENTS.md`, to respond in caveman-full style by default, and the directive SHALL be phrased tool-agnostically rather than as an instruction to activate a Claude-specific skill.
- acceptance: Scenarios in spec satisfied
- scope: agent, comms, mode

## REQ-agent-comms-mode-human-readable-exclusio
- source: openspec/specs/agent-comms-mode/spec.md
- description: The caveman comms default SHALL NOT apply to human-readable surfaces. The ruleset SHALL exclude commit messages, pull-request descriptions, source code and code comments, user-facing documentation (including `README`, onboarding, and operator guides), and security warnings or irreversible-action confirmations, which SHALL remain normal prose.
- acceptance: Scenarios in spec satisfied
- scope: agent, comms, mode

## REQ-agent-context-compression-reviewed-bulk-
- source: openspec/specs/agent-context-compression/spec.md
- description: The project SHALL allow reviewed compression of active OpenSpec spec bodies and AI-facing repository context files, while excluding operator/user documentation from compression.
- acceptance: Scenarios in spec satisfied
- scope: agent, context, compression

## REQ-agent-context-compression-contract-and-s
- source: openspec/specs/agent-context-compression/spec.md
- description: Bulk compression SHALL preserve RFC-2119 normative clauses, `
- acceptance: Scenarios in spec satisfied
- scope: agent, context, compression

## REQ-agent-context-compression-headings-scena
- source: openspec/specs/agent-context-compression/spec.md
- description: #### Scenario: Normative spec compressed safely
- **WHEN** a spec body is compressed
- **THEN** normative clauses and scenario structure remain semantically unchanged
- **AND** code and command regions remain byte-identical
- acceptance: Scenarios in spec satisfied
- scope: agent, context, compression

## REQ-agent-context-compression-purpose-sectio
- source: openspec/specs/agent-context-compression/spec.md
- description: Every active spec's `## Purpose` section SHALL remain uncompressed human-readable prose and SHALL remain exempt from filler-density scoring.
- acceptance: Scenarios in spec satisfied
- scope: agent, context, compression

## REQ-agent-context-compression-compression-th
- source: openspec/specs/agent-context-compression/spec.md
- description: The compression tripwire SHALL be ratcheted only after measuring compressed spec density and choosing a threshold that passes the reviewed compressed corpus.
- acceptance: Scenarios in spec satisfied
- scope: agent, context, compression

## REQ-analyzer-rigor-relative-noise-gate
- source: openspec/specs/analyzer-rigor/spec.md
- description: Bucket lock acceptability SHALL be derived from `sigma / rate` after warmup.
- acceptance: Scenarios in spec satisfied
- scope: analyzer, rigor

## REQ-analyzer-rigor-safe-mode-enforcement
- source: openspec/specs/analyzer-rigor/spec.md
- description: Safe mode MUST refuse recommendations if zero buckets are LOCKED in the state.
- acceptance: Scenarios in spec satisfied
- scope: analyzer, rigor

## REQ-analyzer-rigor-explicit-bootstrap-paths
- source: openspec/specs/analyzer-rigor/spec.md
- description: Aggressive and force modes SHALL allow pre-lock estimates with explicit warnings.
- acceptance: Scenarios in spec satisfied
- scope: analyzer, rigor

## REQ-analyzer-rigor-precision-weighted-recomm
- source: openspec/specs/analyzer-rigor/spec.md
- description: Recommendations SHALL use precision-weighted qualifying set (N / Var) with trimmed tails.
- acceptance: Scenarios in spec satisfied
- scope: analyzer, rigor

## REQ-analyzer-rigor-bp-derived-sigma
- source: openspec/specs/analyzer-rigor/spec.md
- description: The analyzer SHALL derive `buf_variance_blend_ref_mm` from BP samples, NOT BL field.
- acceptance: Scenarios in spec satisfied
- scope: analyzer, rigor

## REQ-analyzer-rigor-contributors-visibility
- source: openspec/specs/analyzer-rigor/spec.md
- description: The generated patch MUST include a contributor evidence block for learned values.
- acceptance: Scenarios in spec satisfied
- scope: analyzer, rigor

## REQ-bucket-locking-bucket-schema-4-shall-pre
- source: openspec/specs/bucket-locking/spec.md
- description: Bucket state schema 4 SHALL include scalar residual-statistics fields used by
the lock/unlock algorithm without storing per-sample histories.
- acceptance: Scenarios in spec satisfied
- scope: bucket, locking

## REQ-bucket-locking-schema-3-state-shall-migr
- source: openspec/specs/bucket-locking/spec.md
- description: The schema 3 to 4 migration SHALL preserve all existing bucket and `_meta`
content and keep LOCKED buckets locked.
- acceptance: Scenarios in spec satisfied
- scope: bucket, locking

## REQ-bucket-locking-unlocking-shall-use-three
- source: openspec/specs/bucket-locking/spec.md
- description: LOCKED buckets SHALL unlock only through the catastrophic, streak, or drift
channels defined by residual-aware logic.
- acceptance: Scenarios in spec satisfied
- scope: bucket, locking

## REQ-bucket-locking-locking-shall-be-noise-ga
- source: openspec/specs/bucket-locking/spec.md
- description: A bucket SHALL NOT enter or re-enter LOCKED state until required sample evidence,
noise criteria, and minimum locked dwell behavior are satisfied.
- acceptance: Scenarios in spec satisfied
- scope: bucket, locking

## REQ-bucket-locking-verbose-state-info-shall-
- source: openspec/specs/bucket-locking/spec.md
- description: Verbose tuner state output SHALL include residual and unlock diagnostics needed
to understand chatter, dwell, and lock decisions.
- acceptance: Scenarios in spec satisfied
- scope: bucket, locking

## REQ-buffer-geometry-vocabulary-buffer-geomet
- source: openspec/specs/buffer-geometry-vocabulary/spec.md
- description: Buffer geometry SHALL be configured through exactly two full-range tunables
with semantics aligned to Happy Hare / EMU Sync:
- acceptance: Scenarios in spec satisfied
- scope: buffer, geometry, vocabulary

## REQ-buffer-geometry-vocabulary-full-range-to
- source: openspec/specs/buffer-geometry-vocabulary/spec.md
- description: The firmware SHALL convert the full-range `buf_switch_span_mm` to the
internal half-based geometry exactly once at the value-ingest boundary
(config ingest and the serial SET handler) as
`half = buf_switch_span_mm / 2`, and SHALL NOT apply the conversion anywhere
else. `buf_max_travel_mm` SHALL map 1:1 to the internal total-travel value
with no unit conversion. Internal `sync.c` geometry remains half-based;
only the *source* of the half value changes.
- acceptance: Scenarios in spec satisfied
- scope: buffer, geometry, vocabulary

## REQ-buffer-geometry-vocabulary-emu-sync-defa
- source: openspec/specs/buffer-geometry-vocabulary/spec.md
- description: The compiled defaults SHALL be the EMU Sync reference values:
`buf_switch_span_mm = 10` and `buf_max_travel_mm = 25`. These replace the
prior `buf_half_travel_mm = 7.8` (an untuned calibration artifact) and
`buf_size_mm = 22`.
- acceptance: Scenarios in spec satisfied
- scope: buffer, geometry, vocabulary

## REQ-buffer-geometry-vocabulary-switch-span-a
- source: openspec/specs/buffer-geometry-vocabulary/spec.md
- description: The two tunables SHALL preserve, in full-range terms, the clamp relationship
that existed in half-range terms. `buf_switch_span_mm` SHALL be clamped to
`[2.0, buf_max_travel_mm]`. `buf_max_travel_mm` SHALL be clamped to
`[10, 1000]`. Setting `buf_max_travel_mm` SHALL re-clamp `buf_switch_span_mm`
so the derived internal half never exceeds `buf_max_travel_mm / 2`.
- acceptance: Scenarios in spec satisfied
- scope: buffer, geometry, vocabulary

## REQ-buffer-geometry-vocabulary-serial-vocabu
- source: openspec/specs/buffer-geometry-vocabulary/spec.md
- description: The serial SET/GET tokens SHALL be `BUF_SWITCH_SPAN` and `BUF_MAX_TRAVEL`,
carrying full-range values. The legacy tokens `BUF_HALF_TRAVEL`,
`BUF_TRAVEL`, and `BUF_SIZE` SHALL be removed with no compatibility alias.
- acceptance: Scenarios in spec satisfied
- scope: buffer, geometry, vocabulary

## REQ-buffer-geometry-vocabulary-type-p-analog
- source: openspec/specs/buffer-geometry-vocabulary/spec.md
- description: The full→half ingest change SHALL NOT alter type-P (analog,
`BUF_SENSOR_TYPE != 0`) behavior. Only the *source* of the internal
half value changes; the analog consumers of
`buf_physical_half_travel_mm()` / `buf_threshold_mm()` SHALL behave
identically for an equivalent geometry.
- acceptance: Scenarios in spec satisfied
- scope: buffer, geometry, vocabulary

## REQ-buffer-state-lock-bl-command-surface
- source: openspec/specs/buffer-state-lock/spec.md
- description: The firmware SHALL accept a host `BL:<state>` command that arms the active
lane to drive the buffer to the requested extreme and lock there, where
`<state>` is `T` (tension) or `C` (compression). `BL` with no argument
SHALL be treated as `BL:T`. The firmware SHALL expose the current lock arm
in status as `BL:T`, `BL:C`, or `BL:0` (disarmed).
- acceptance: Scenarios in spec satisfied
- scope: buffer, state, lock

## REQ-buffer-state-lock-bounded-half-travel-pr
- source: openspec/specs/buffer-state-lock/spec.md
- description: On `BL` the firmware SHALL drive the active lane toward the requested extreme
and stop as soon as either the corresponding raw buffer state
(`BUF_TENSION` or `BUF_COMPRESSION`) is reached or `BUF_MAX_TRAVEL_MM / 2`
mm of MMU travel is completed, whichever comes first. The prime MUST NOT
exceed the half-travel cap.
- acceptance: Scenarios in spec satisfied
- scope: buffer, state, lock

## REQ-buffer-state-lock-locked-hold-contract
- source: openspec/specs/buffer-state-lock/spec.md
- description: While locked the firmware SHALL energize the active lane motor with zero
commanded velocity, MUST NOT issue any closed-loop feed corrections from
the buffer state, and SHALL preserve estimator, drift observer, sigma,
confidence, and reserve integrator state.
- acceptance: Scenarios in spec satisfied
- scope: buffer, state, lock

## REQ-buffer-state-lock-lock-break-on-external
- source: openspec/specs/buffer-state-lock/spec.md
- description: The firmware SHALL treat any departure of the raw buffer state from the
locked extreme as a non-MMU (external) force lock-break and MUST transition
to the catch sub-state on the first raw edge, without waiting for the
`BUF_HYST_MS` debounce window.
- acceptance: Scenarios in spec satisfied
- scope: buffer, state, lock

## REQ-buffer-state-lock-instant-slam-catch-wit
- source: openspec/specs/buffer-state-lock/spec.md
- description: On lock-break the firmware SHALL drive the active lane in the mirror
direction (retract for `BL:T` break, feed for `BL:C` break) at
`GLOBAL_MAX_SPS` via an instant `current_sps = target`
write, bypassing `SYNC_RAMP_UP_SPS`. The catch MUST tolerate transient
over-drive back toward the armed extreme as a safe recoverable direction
and SHALL NOT throttle the catch to avoid it.
- acceptance: Scenarios in spec satisfied
- scope: buffer, state, lock

## REQ-buffer-state-lock-manual-release-via-bs
- source: openspec/specs/buffer-state-lock/spec.md
- description: The host `BS` (buffer stabilize) command SHALL release any active `BL`
lock or catch immediately, run normal buffer stabilization, and return the
controller to `SYNC_OFF`.
- acceptance: Scenarios in spec satisfied
- scope: buffer, state, lock

## REQ-buffer-state-lock-locked-state-watchdog
- source: openspec/specs/buffer-state-lock/spec.md
- description: The firmware SHALL emit `EV:BL:TIMEOUT` and auto-release the lock if no
lock-break, no `BS`, and no other release happens within a configurable
timeout (default 30 seconds) of entering the locked sub-state.
- acceptance: Scenarios in spec satisfied
- scope: buffer, state, lock

## REQ-calibration-workflow-calibration-shall-b
- source: openspec/specs/calibration-workflow/spec.md
- description: The calibration workflow SHALL collect evidence without mutating firmware
settings unless the operator passes explicit write flags.
- acceptance: Scenarios in spec satisfied
- scope: calibration, workflow

## REQ-calibration-workflow-state-schema-migrat
- source: openspec/specs/calibration-workflow/spec.md
- description: Bucket state migrations SHALL be registered in a migration table and applied in
sequence without rewriting the migration loop for each new schema.
- acceptance: Scenarios in spec satisfied
- scope: calibration, workflow

## REQ-calibration-workflow-bucket-locking-shal
- source: openspec/specs/calibration-workflow/spec.md
- description: A bucket SHALL lock only after satisfying cumulative evidence requirements for
samples, runs, layers, stability, and motion time.
- acceptance: Scenarios in spec satisfied
- scope: calibration, workflow

## REQ-calibration-workflow-analyzer-patches-sh
- source: openspec/specs/calibration-workflow/spec.md
- description: `scripts/flare_analyze.py` SHALL emit review patches that preserve current values
for unavailable recommendations and label recommendation confidence.
- acceptance: Scenarios in spec satisfied
- scope: calibration, workflow

## REQ-calibration-workflow-long-running-daemon
- source: openspec/specs/calibration-workflow/spec.md
- description: Daemon-mode calibration SHALL tolerate stale buckets and repeated runs without
allowing stale evidence to dominate current recommendations.
- acceptance: Scenarios in spec satisfied
- scope: calibration, workflow

## REQ-calibration-workflow-firmware-live-learn
- source: openspec/specs/calibration-workflow/spec.md
- description: The firmware live baseline tier SHALL be ephemeral, up-only, and gated to
`SYNC_ACTIVE`, and SHALL never write persistent state. Persistent baseline
and compression-bias values SHALL change only through the reviewed offline
analyzer + config flash path.
- acceptance: Scenarios in spec satisfied
- scope: calibration, workflow

## REQ-calibration-workflow-deterministic-dual-
- source: openspec/specs/calibration-workflow/spec.md
- description: `flare_analyze.py` SHALL provide an explicit two-profile baseline mode that
takes a fastest-cubic-flow capture and a slowest-cubic-flow capture and
derives exactly one baseline value. The derivation SHALL be a pure function
of the input rows: stable bucket ordering, sample-count weighting only, no
wall-clock recency weighting, and a fixed rounding rule. Identical input
captures SHALL produce a byte-identical baseline regardless of when the
analyzer runs. The existing recency-weighted config-patch path SHALL remain
unchanged and selected separately.
- acceptance: Scenarios in spec satisfied
- scope: calibration, workflow

## REQ-calibration-workflow-offline-analyzer-re
- source: openspec/specs/calibration-workflow/spec.md
- description: The deterministic two-profile baseline SHALL be the only value written to
persistent memory for the baseline. The live tuner and the recommendation
script SHALL NOT write the persistent baseline; the live firmware baseline
SHALL remain ephemeral, up-only, and non-persistent.
- acceptance: Scenarios in spec satisfied
- scope: calibration, workflow

## REQ-calibration-workflow-analyzer-emits-a-de
- source: openspec/specs/calibration-workflow/spec.md
- description: The offline analyzer SHALL be able to emit a flow-keyed schedule (multiple
flow→{baseline, bias} breakpoints) from the existing per-`(feature,
v_fil_bin)` velocity buckets, in addition to the scalar baseline. The
schedule derivation SHALL reuse the deterministic dual-profile reducer and
existing maturity gates, SHALL be bounded by the configured breakpoint
cap, and SHALL remain the sole persistent authority for baseline/bias.
- acceptance: Scenarios in spec satisfied
- scope: calibration, workflow

## REQ-code-style-standard-enforced-format-conf
- source: openspec/specs/code-style-standard/spec.md
- description: Repo SHALL carry `.clang-format`, `.clang-tidy`, and `.editorconfig` at root, and
all firmware C sources (`firmware/src/*.c`, `firmware/include/*.h`) SHALL conform.
- acceptance: Scenarios in spec satisfied
- scope: code, style, standard

## REQ-code-style-standard-local-lint-invocatio
- source: openspec/specs/code-style-standard/spec.md
- description: `STYLE.md` SHALL document the local `clang-format` and `clang-tidy` invocation, and
the `clang-tidy` config SHALL enable the project check set. No CI lint gate is
required in this change.
- acceptance: Scenarios in spec satisfied
- scope: code, style, standard

## REQ-code-style-standard-naming-conventions
- source: openspec/specs/code-style-standard/spec.md
- description: Identifiers SHALL be intention-revealing per `STYLE.md`. Domain vocabulary terms
(`sps`, `mm`, `tmc`, `buf`, `psf`, `adc`, `pio`) MAY remain abbreviated and SHALL
be defined in `STYLE.md`. Single-letter and opaque identifiers SHALL NOT be used
for variables with non-trivial scope.
- acceptance: Scenarios in spec satisfied
- scope: code, style, standard

## REQ-code-style-standard-file-and-function-si
- source: openspec/specs/code-style-standard/spec.md
- description: `STYLE.md` SHALL state translation-unit and function size norms, and oversized
units SHALL be split into cohesive modules and oversized functions extracted.
- acceptance: Scenarios in spec satisfied
- scope: code, style, standard

## REQ-code-style-standard-magic-number-policy
- source: openspec/specs/code-style-standard/spec.md
- description: Non-trivial numeric literals in firmware SHALL be replaced by named constants or
documented tunables; values that are runtime-tunable SHALL follow the existing
`config.ini` → `tune.h` → `CONF_*` path.
- acceptance: Scenarios in spec satisfied
- scope: code, style, standard

## REQ-code-style-standard-comprehension-commen
- source: openspec/specs/code-style-standard/spec.md
- description: Each firmware `.c` SHALL carry a file-header doc-block stating what the unit owns,
its core algorithm, and a pointer to the relevant `BEHAVIOR.md`/spec section. Inline
comments SHALL explain why (intent, invariants, hardware quirks, edge cases), not
narrate obvious code. Every state machine SHALL carry a state-transition map comment.
- acceptance: Scenarios in spec satisfied
- scope: code, style, standard

## REQ-code-style-standard-doc-comment-format-a
- source: openspec/specs/code-style-standard/spec.md
- description: `STYLE.md` SHALL define the function/struct/macro doc-comment format, and existing
rationale comments SHALL be preserved.
- acceptance: Scenarios in spec satisfied
- scope: code, style, standard

## REQ-code-style-standard-behavior-preserving-
- source: openspec/specs/code-style-standard/spec.md
- description: All overhaul edits SHALL be behavior-preserving: no serial protocol, config key,
tunable, or runtime-behavior change, and the build SHALL pass before every commit.
- acceptance: Scenarios in spec satisfied
- scope: code, style, standard

## REQ-code-style-standard-shared-constants-are
- source: openspec/specs/code-style-standard/spec.md
- description: A numeric constant or small helper used by more than one translation unit SHALL be
defined once in a shared header, not copied per `.c`. Identical constants SHALL NOT
be redefined with divergent style (`#define` vs `static const`) across units.
- acceptance: Scenarios in spec satisfied
- scope: code, style, standard

## REQ-code-style-standard-global-naming-conven
- source: openspec/specs/code-style-standard/spec.md
- description: All firmware global variables SHALL be named `g_lower_case`, including config-backed
runtime tunables, and the `g_` prefix SHALL be enforced by `.clang-tidy` (no blanket
`GlobalVariableIgnoredRegexp` exemption). `STYLE.md` SHALL document this and SHALL state
that tunable-vs-state is distinguished by the `controller_shared.h` tunables section,
the `settings_t` mirror, and the `SET:`/`GET:` surface — not by casing. Protocol param
names and `config.ini` keys remain `UPPER_CASE` strings and are not affected by the
identifier naming.
- acceptance: Scenarios in spec satisfied
- scope: code, style, standard

## REQ-config-surface-tiers-configuration-param
- source: openspec/specs/config-surface-tiers/spec.md
- description: Every configuration parameter SHALL belong to exactly one tier, and its storage
and exposure SHALL follow that tier:
- acceptance: Scenarios in spec satisfied
- scope: config, surface, tiers

## REQ-config-surface-tiers-internal-constants-
- source: openspec/specs/config-surface-tiers/spec.md
- description: A T3 internal constant SHALL NOT appear in `config.ini.example`, in
`settings_t`, or in the release-build `SET:` / `GET:` handlers; a
unit-independent T3 constant additionally SHALL live in `tune_internal.h` and
not in `gen_config.py` `DEFAULTS`. Changing a T3 constant SHALL be a source edit
+ recompile and SHALL NOT require a `SETTINGS_VERSION` bump.
- acceptance: Scenarios in spec satisfied
- scope: config, surface, tiers

## REQ-config-surface-tiers-a-dev-build-may-exp
- source: openspec/specs/config-surface-tiers/spec.md
- description: A `FLARE_DEV_TUNING` build flag SHALL gate optional re-exposure of T3 constants
as `SET:`-only, non-persisted runtime overrides for bench experimentation. The
flag SHALL be undefined in release builds, and a dev override SHALL NOT survive a
reboot.
- acceptance: Scenarios in spec satisfied
- scope: config, surface, tiers

## REQ-config-surface-tiers-demoted-keys-are-mi
- source: openspec/specs/config-surface-tiers/spec.md
- description: A demoted (T3) parameter SHALL migrate gracefully: an existing `config.ini` that
still sets it SHALL build with a warning rather than a hard error, and the device
config dump SHALL NOT emit the demoted key.
- acceptance: Scenarios in spec satisfied
- scope: config, surface, tiers

## REQ-cross-platform-script-tooling-all-operat
- source: openspec/specs/cross-platform-script-tooling/spec.md
- description: Every operational script in `scripts/` SHALL be implemented in Python using only stdlib + pyserial. Operational `.sh` files SHALL NOT exist.
- acceptance: Scenarios in spec satisfied
- scope: cross, platform, script, tooling

## REQ-cross-platform-script-tooling-linux-and-
- source: openspec/specs/cross-platform-script-tooling/spec.md
- description: Each ported script SHALL produce identical functional behavior on Linux (Raspberry Pi / Debian / Ubuntu / Fedora) and macOS. Platform-specific operations (device discovery, mount, `diskutil`) SHALL be branched via `platform.system()`.
- acceptance: Scenarios in spec satisfied
- scope: cross, platform, script, tooling

## REQ-cross-platform-script-tooling-no-inline-
- source: openspec/specs/cross-platform-script-tooling/spec.md
- description: Scripts SHALL NOT embed Python code inside bash heredocs. Serial I/O, device communication, and other Python operations SHALL use direct imports from shared modules (`serial_utils`, `path_utils`).
- acceptance: Scenarios in spec satisfied
- scope: cross, platform, script, tooling

## REQ-cross-platform-script-tooling-color-outp
- source: openspec/specs/cross-platform-script-tooling/spec.md
- description: Scripts with colored terminal output SHALL detect non-interactive terminals and `NO_COLOR` environment variable, disabling ANSI escape sequences when appropriate.
- acceptance: Scenarios in spec satisfied
- scope: cross, platform, script, tooling

## REQ-cutter-feed-timeout-cutter-feed-timeout-
- source: openspec/specs/cutter-feed-timeout/spec.md
- description: `CUT_TIMEOUT_FEED_MS` — the per-phase motor-feed safety timeout used in `CUT_FEED_WAIT` — SHALL be a runtime-tunable parameter sourced from `config.ini` (`cut_feed_timeout_ms`), persisted in flash, and accessible via `GET:CUT_FEED_MS` / `SET:CUT_FEED_MS` serial protocol commands.
- acceptance: Scenarios in spec satisfied
- scope: cutter, feed, timeout

## REQ-cutter-feed-timeout-cutter-settle-timeou
- source: openspec/specs/cutter-feed-timeout/spec.md
- description: `CUT_TIMEOUT_SETTLE_MS` — the per-phase servo-settle safety timeout used in `CUT_OPEN_WAIT`, `CUT_CLOSE_WAIT`, and `CUT_REOPEN_WAIT` — SHALL be a runtime-tunable parameter sourced from `config.ini` (`cut_settle_timeout_ms`), persisted in flash, and accessible via `GET:CUT_SETTLE_MS` / `SET:CUT_SETTLE_MS` serial protocol commands.
- acceptance: Scenarios in spec satisfied
- scope: cutter, feed, timeout

## REQ-daemon-klipper-mirror-delta-set-mmu-mirr
- source: openspec/specs/daemon-klipper-mirror/spec.md
- description: The daemon SHALL push only the `SET_MMU` fields whose formatted value changed since the
last successful push, relying on `cmd_SET_MMU` keeping the current value for any absent
param. The resulting Klipper mock state SHALL be identical to a full push.
- acceptance: Scenarios in spec satisfied
- scope: daemon, klipper, mirror

## REQ-daemon-klipper-mirror-full-resync-recove
- source: openspec/specs/daemon-klipper-mirror/spec.md
- description: The daemon SHALL emit a full `SET_MMU` (all fields) on the first push and on a
board-online transition. On the periodic resync tick the daemon SHALL read the Klipper
`mmu` object and emit a full `SET_MMU` only when the reported mock state diverges from the
desired field set; when they match the daemon SHALL emit nothing that tick. A restarted
Klipper/Moonraker SHALL recover complete state within one tick of the divergence becoming
observable.
- acceptance: Scenarios in spec satisfied
- scope: daemon, klipper, mirror

## REQ-daemon-klipper-mirror-gate-state-diagnos
- source: openspec/specs/daemon-klipper-mirror/spec.md
- description: The daemon SHALL, when `FLARE_GATE_DEBUG` is set, log the gate-relevant mirror inputs
(`active_gate`, per-lane IN/OUT, computed gate status, toolchange state) when they change.
When the flag is unset there SHALL be no behavior or output change.
- acceptance: Scenarios in spec satisfied
- scope: daemon, klipper, mirror

## REQ-daemon-klipper-mirror-host-busy-backpres
- source: openspec/specs/daemon-klipper-mirror/spec.md
- description: The daemon SHALL NOT queue mirror traffic behind a long-running blocking Klipper command.
When a gcode/script push (`SET_MMU`, `MMU_GATE_MAP`, or `_FLARE_SYNC_BOARD`) fails because
the Klipper gcode lock is busy, the daemon SHALL enter a host-busy state, suppress all
further gcode/script pushes, and poll a lock-free `objects/query` for `idle_timeout` until
`idle_timeout.state` reports Idle/Ready before resuming pushes. The daemon SHALL
distinguish host-busy (gcode lock held, host reachable) from Moonraker-offline; the
offline path retains its existing backoff and SHALL NOT be replaced by the busy path.
- acceptance: Scenarios in spec satisfied
- scope: daemon, klipper, mirror

## REQ-deterministic-tuning-workflow-two-profil
- source: openspec/specs/deterministic-tuning-workflow/spec.md
- description: The tuning workflow SHALL be: run the same model in two profiles — fastest
cubic flow and slowest cubic flow — using the existing tuner and marker
scripts, then run the analyzer's deterministic two-profile mode to derive
one baseline, then write that baseline to config and re-flash. The procedure
SHALL be documented as a fixed, ordered sequence with no run-to-run
variability in the resulting baseline.
- acceptance: Scenarios in spec satisfied
- scope: deterministic, tuning, workflow

## REQ-deterministic-tuning-workflow-live-tuner
- source: openspec/specs/deterministic-tuning-workflow/spec.md
- description: `scripts/flare_baseline_recommender.py` SHALL be a host-only script
(stdlib + pyserial only) that reads the device tty, tracks the live tuner's
drift signal across a print, and at end-of-print reports a suggested
persistent `baseline_sps` with a supporting drift summary. It SHALL be
observe-only: no `SET` or `SV` writes. Given an identical captured input
stream it SHALL produce an identical recommendation (replayable for test).
- acceptance: Scenarios in spec satisfied
- scope: deterministic, tuning, workflow

## REQ-deterministic-tuning-workflow-tuning-wor
- source: openspec/specs/deterministic-tuning-workflow/spec.md
- description: The documented workflow SHALL NOT require interpreting different results
across repeated runs. Any value an operator is asked to commit SHALL be
reproducible from the captured inputs alone, independent of analysis time
or machine.
- acceptance: Scenarios in spec satisfied
- scope: deterministic, tuning, workflow

## REQ-filament-bypass-local-filament-bypass-st
- source: openspec/specs/filament-bypass/spec.md
- description: The host daemon and Klipper mock SHALL expose a unified `bypass` boolean and status field, mapped to Happy Hare gate/tool sentinel `-2`.
- acceptance: Scenarios in spec satisfied
- scope: filament, bypass

## REQ-filament-bypass-single-sensor-bypass-tel
- source: openspec/specs/filament-bypass/spec.md
- description: Under bypass mode, the system SHALL report exactly one active sensor: the toolhead sensor (`TS`). All other gate, pre-gate, and combiner sensors SHALL be forced to inactive.
- acceptance: Scenarios in spec satisfied
- scope: filament, bypass

## REQ-filament-bypass-manual-feed-and-autoload
- source: openspec/specs/filament-bypass/spec.md
- description: Filament SHALL be manually fed through the bypass lane without MMU drive assistance until it triggers the toolhead sensor, which SHALL immediately and automatically invoke the autoload sequence.
- acceptance: Scenarios in spec satisfied
- scope: filament, bypass

## REQ-filament-bypass-mmu-free-extruder-only-a
- source: openspec/specs/filament-bypass/spec.md
- description: Under bypass mode, all load and unload sequences SHALL ignore and completely suppress any physical MMU lane motor serial commands, executing strictly as toolhead extruder-only operations.
- acceptance: Scenarios in spec satisfied
- scope: filament, bypass

## REQ-filament-bypass-suppressed-eject-under-b
- source: openspec/specs/filament-bypass/spec.md
- description: All MMU lane eject procedures SHALL be skipped and safely suppressed under bypass mode, with no serial command executed.
- acceptance: Scenarios in spec satisfied
- scope: filament, bypass

## REQ-flow-keyed-schedule-versioned-bounded-fl
- source: openspec/specs/flow-keyed-schedule/spec.md
- description: The system SHALL define a versioned schedule table mapping estimated flow
to `{baseline_sps, compression_bias_frac}`. The table SHALL be a strictly
increasing-in-flow, sorted array bounded by a config-tunable maximum
breakpoint count. The format SHALL be additive: existing scalar
`baseline_sps` / `compression_bias_frac` config keys SHALL remain valid.
- acceptance: Scenarios in spec satisfied
- scope: flow, keyed, schedule

## REQ-flow-keyed-schedule-degenerate-single-po
- source: openspec/specs/flow-keyed-schedule/spec.md
- description: A length-1 schedule SHALL produce, for every flow value, exactly the
scalar `baseline_sps` and the milli-resolution `compression_bias_frac` it
was synthesized from. Bias fractions that are already aligned to integer
milli SHALL be exact; other bias fractions SHALL differ by no more than
0.0005 absolute bias after milli quantization. Firmware behavior with a
length-1 schedule SHALL match pre-change scalar behavior within that
milli-resolution bound.
- acceptance: Scenarios in spec satisfied
- scope: flow, keyed, schedule

## REQ-flow-keyed-schedule-firmware-interpolate
- source: openspec/specs/flow-keyed-schedule/spec.md
- description: The firmware SHALL derive the active baseline and compression-bias by
clamped linear interpolation of the schedule against the live
`extruder_est_sps`, with no extrapolation beyond the first/last
breakpoint. The flow key SHALL be the firmware's own `extruder_est_sps`
only; no host, Klipper, or encoder input SHALL be used. Interpolation
SHALL be float-light and bounded by the breakpoint count.
- acceptance: Scenarios in spec satisfied
- scope: flow, keyed, schedule

## REQ-flow-keyed-schedule-schedule-emission-is
- source: openspec/specs/flow-keyed-schedule/spec.md
- description: Identical bucket inputs SHALL produce a byte-identical schedule table,
independent of analysis wall-clock time or machine. The emission SHALL
reuse the deterministic dual-profile reducer and `BIAS_SAFE_MIN/MAX`
clamps and SHALL NOT use wall-clock recency weighting.
- acceptance: Scenarios in spec satisfied
- scope: flow, keyed, schedule

## REQ-klipper-integration-host-serial-control
- source: openspec/specs/klipper-integration/spec.md
- description: The Klipper host MUST interact with FLARE via single-command CDC serial transactions.
- acceptance: Scenarios in spec satisfied
- scope: klipper, integration

## REQ-klipper-integration-motion-tracking-side
- source: openspec/specs/klipper-integration/spec.md
- description: The sidecar (`--uds`) SHALL track Klipper's print state and forward speed events to FLARE.
- acceptance: Scenarios in spec satisfied
- scope: klipper, integration

## REQ-klipper-integration-macro-orchestration
- source: openspec/specs/klipper-integration/spec.md
- description: Toolchange macros (`_FLARE_CHANGE_LANE` / `T1` / `T2`) SHALL coordinate the
extruder, MMU, and toolhead state.
- acceptance: Scenarios in spec satisfied
- scope: klipper, integration

## REQ-klipper-integration-reusable-toolhead-un
- source: openspec/specs/klipper-integration/spec.md
- description: The include SHALL provide a standalone `FLARE_UNLOAD_TOOLHEAD` macro.
- acceptance: Scenarios in spec satisfied
- scope: klipper, integration

## REQ-klipper-integration-dashboard-load-and-e
- source: openspec/specs/klipper-integration/spec.md
- description: Dashboard `MMU_LOAD` and `MMU_EJECT` commands SHALL use the currently selected
gate, not only the board's active lane.
- acceptance: Scenarios in spec satisfied
- scope: klipper, integration

## REQ-klipper-integration-toolhead-sensor-opti
- source: openspec/specs/klipper-integration/spec.md
- description: `TC:` load completion SHALL NOT require an explicit host `TS:1` command.
- acceptance: Scenarios in spec satisfied
- scope: klipper, integration

## REQ-klipper-integration-klipper-md-scope-is-
- source: openspec/specs/klipper-integration/spec.md
- description: KLIPPER.md SHALL cover: serial port setup, shell command helper,
toolhead sensor wiring, reference to `flare_mmu.cfg`, and the
troubleshooting table. It SHALL NOT contain buffer sync tuning,
calibration print workflows, gcode_marker usage, or telemetry/analyzer
instructions — those belong exclusively in `TUNING.md`.
- acceptance: Scenarios in spec satisfied
- scope: klipper, integration

## REQ-klipper-integration-toolhead-sensor-sect
- source: openspec/specs/klipper-integration/spec.md
- description: KLIPPER.md SHALL document the physical sensor as the primary path.
The buffer-geometry fallback (TS_BUF_MS) SHALL appear as a brief note
explaining it is automatic — not as a parallel "Option B" requiring
user configuration.
- acceptance: Scenarios in spec satisfied
- scope: klipper, integration

## REQ-klipper-integration-automatic-bypass-too
- source: openspec/specs/klipper-integration/spec.md
- description: When the printer is in bypass mode and filament is manually inserted into the extruder entrance, Klipper macros SHALL automatically trigger the toolhead filament load sequence upon toolhead sensor trigger (insert edge).
- acceptance: Scenarios in spec satisfied
- scope: klipper, integration

## REQ-klipper-integration-slicer-toolchange-by
- source: openspec/specs/klipper-integration/spec.md
- description: The `MMU_CHANGE_TOOL` command SHALL gracefully accept and handle bypass sentinel values `-2` for tool or gate transitions, allowing seamless slicer-generated or UI-driven toolchanges to the bypass gate.
- acceptance: Scenarios in spec satisfied
- scope: klipper, integration

## REQ-klipper-mmu-config-single-file-klipper-m
- source: openspec/specs/klipper-mmu-config/spec.md
- description: `klipper/flare_mmu.cfg` SHALL provide a complete Klipper MMU integration
that users can activate with a single `[include flare_mmu.cfg]` line in
`printer.cfg`, with no other macro files required.
- acceptance: Scenarios in spec satisfied
- scope: klipper, mmu, config

## REQ-klipper-mmu-config-variables-block-with-
- source: openspec/specs/klipper-mmu-config/spec.md
- description: `[gcode_macro _FLARE_VARS]` SHALL expose all user-configurable distances
using the same names as the LH-Stinger Pico MMU wiki
(`dist_sensor_to_extruder`, `dist_filament_park`,
`dist_extruder_to_meltzone`) and SHALL add `dist_meltzone_to_nozzle_tip` for
the hotend length needed by FLARE's tip-forming MMU assist.
- acceptance: Scenarios in spec satisfied
- scope: klipper, mmu, config

## REQ-klipper-mmu-config-tip-forming-macro-wit
- source: openspec/specs/klipper-mmu-config/spec.md
- description: `_FLARE_TIP_FORMING` SHALL implement the full tip forming sequence
(post-pause push, cooldown pull, optional secondary moves, optional dip,
final fast retract to park position) reading parameters from
`_FLARE_TIP_FORMING_DEFAULTS`.
- acceptance: Scenarios in spec satisfied
- scope: klipper, mmu, config

## REQ-klipper-mmu-config-load-hotend-macro-wit
- source: openspec/specs/klipper-mmu-config/spec.md
- description: `_FLARE_LOAD_HOTEND` SHALL advance filament from the park position to the
meltzone in three stages (50% fast / 25% normal / 25% slow) and call
`_FLARE_PURGE` when `purge_len` is greater than zero.
- acceptance: Scenarios in spec satisfied
- scope: klipper, mmu, config

## REQ-klipper-mmu-config-purge-helper-is-simpl
- source: openspec/specs/klipper-mmu-config/spec.md
- description: `_FLARE_PURGE` SHALL own purge extrusion separately from `_FLARE_LOAD_HOTEND`.
It SHALL implement the simple upstream `_SP_PURGE` core shape: purge the
requested relative extrusion amount at `purge_speed`, then perform a small
0.4 mm retract. It SHALL call `_FLARE_HEAT_HOTEND` before purge extrusion and
call an empty `_FLARE_PARK` hook for user-provided park macros. Purge chute
parking, blob splitting, and brush moves SHALL be left to user-provided
`_FLARE_PARK` customization or wrapper macros.
- acceptance: Scenarios in spec satisfied
- scope: klipper, mmu, config

## REQ-klipper-mmu-config-manual-load-and-eject
- source: openspec/specs/klipper-mmu-config/spec.md
- description: `FLARE_LOAD` and `FLARE_EJECT` SHALL preserve active-lane behavior when called
without `LANE`, and SHALL target a selected lane when `LANE=1` or `LANE=2` is
provided.
- acceptance: Scenarios in spec satisfied
- scope: klipper, mmu, config

## REQ-klipper-mmu-config-toolchange-macro-with
- source: openspec/specs/klipper-mmu-config/spec.md
- description: `_FLARE_CHANGE_LANE` SHALL execute the full toolchange sequence: tip forming
with an ignore-buffer FLARE `MV:` retract → gear retract (derived) →
nonblocking `TC:` → toolhead-sensor-gated PICKUP → load hotend.
Gear retract distance SHALL be computed as
`dist_filament_park + dist_sensor_to_extruder + 5` with no separate
variable. Gear retract speed SHALL use `_FLARE_VARS.speed_hub_to_extruder`
converted to Klipper feedrate (`* 60`).
- acceptance: Scenarios in spec satisfied
- scope: klipper, mmu, config

## REQ-klipper-mmu-config-boot-delayed-gcode-se
- source: openspec/specs/klipper-mmu-config/spec.md
- description: `[delayed_gcode _FLARE_BOOT]` SHALL send `SET:RELOAD_MODE:{enable_reload}`
to FLARE on every Klipper start without `SV:`, so FLARE reverts to
persisted flash default when running standalone.
- acceptance: Scenarios in spec satisfied
- scope: klipper, mmu, config

## REQ-klipper-mmu-config-tip-forming-test-macr
- source: openspec/specs/klipper-mmu-config/spec.md
- description: `FLARE_TEST_TIP_FORMING` SHALL allow manual tip quality testing without
a full toolchange by loading the hotend, simulating a print pause, running
tip forming, and retracting for inspection. It SHALL accept SP-style
tip-forming override parameters and write them into
`_FLARE_TIP_FORMING_DEFAULTS` before running `_FLARE_TIP_FORMING`.
- acceptance: Scenarios in spec satisfied
- scope: klipper, mmu, config

## REQ-klipper-mmu-config-removed-development-m
- source: openspec/specs/klipper-mmu-config/spec.md
- description: `FLARE_CUT`, `FLARE_CUT_BARE`, and `FLARE_CUT_TEST` SHALL NOT be present in
`flare_mmu.cfg`. The cutter cycle is driven by the firmware toolchange (`TC:`),
so no standalone Klipper cut macro is needed.
- acceptance: Scenarios in spec satisfied
- scope: klipper, mmu, config

## REQ-klipper-mmu-config-preload-macro-routes-
- source: openspec/specs/klipper-mmu-config/spec.md
- description: `FLARE_PRELOAD` SHALL advance a selected lane to its gate (OUT) without loading
the toolhead. With `LANE=1` or `LANE=2` it SHALL send `T:{lane}` before `LO:`;
with `LANE=0` (or no `LANE`) it SHALL send `LO:` for the active lane; any other
`LANE` SHALL be rejected with an error and no command.
- acceptance: Scenarios in spec satisfied
- scope: klipper, mmu, config

## REQ-klipper-motion-tracking-sidecar-metadata
- source: openspec/specs/klipper-motion-tracking/spec.md
- description: The system SHALL synthesize markers from slicer sidecar JSON rather than G-code strings.
- acceptance: Scenarios in spec satisfied
- scope: klipper, motion, tracking

## REQ-klipper-motion-tracking-uds-ingress-pari
- source: openspec/specs/klipper-motion-tracking/spec.md
- description: The Klipper UDS flow SHALL feed the existing `on_m118` ingress contract.
- acceptance: Scenarios in spec satisfied
- scope: klipper, motion, tracking

## REQ-klipper-motion-tracking-stable-matcher-s
- source: openspec/specs/klipper-motion-tracking/spec.md
- description: The `SegmentMatcher` SHALL remain compatible with existing tuner and test suites.
- acceptance: Scenarios in spec satisfied
- scope: klipper, motion, tracking

## REQ-klipper-motion-tracking-host-only-integr
- source: openspec/specs/klipper-motion-tracking/spec.md
- description: Motion tracking SHALL NOT require firmware changes to operate.
- acceptance: Scenarios in spec satisfied
- scope: klipper, motion, tracking

## REQ-klipper-motion-tracking-fallback-paths
- source: openspec/specs/klipper-motion-tracking/spec.md
- description: The workflow MUST retain manual G-code marker support when UDS or sidecar is unavailable.
- acceptance: Scenarios in spec satisfied
- scope: klipper, motion, tracking

## REQ-live-tuner-per-feature-velocity-buckets
- source: openspec/specs/live-tuner/spec.md
- description: The tuner SHALL aggregate telemetry into feature + velocity buckets (rate + bias).
- acceptance: Scenarios in spec satisfied
- scope: live, tuner

## REQ-live-tuner-machine-scoped-persistence
- source: openspec/specs/live-tuner/spec.md
- description: The system SHALL persist bucket state in a machine-scoped JSON file.
- acceptance: Scenarios in spec satisfied
- scope: live, tuner

## REQ-live-tuner-observe-only-default
- source: openspec/specs/live-tuner/spec.md
- description: The tuner SHALL NOT perform firmware writes without explicit permission flags.
- acceptance: Scenarios in spec satisfied
- scope: live, tuner

## REQ-live-tuner-review-only-workflow
- source: openspec/specs/live-tuner/spec.md
- description: The calibration workflow SHALL prefer analyzer review patches over blind tuning.
- acceptance: Scenarios in spec satisfied
- scope: live, tuner

## REQ-live-tuner-diagnostics
- source: openspec/specs/live-tuner/spec.md
- description: The tuner MUST explain bucket states (TRACKING, STABLE, LOCKED) in state output.
- acceptance: Scenarios in spec satisfied
- scope: live, tuner

## REQ-live-tuner-sync-state-and-relief-effort-
- source: openspec/specs/live-tuner/spec.md
- description: Status and diagnostics output SHALL expose the current sync lifecycle state
and warn-only relief-effort counters (accumulated in commanded-MMU mm) so
operators and the offline analyzer can observe relief/fault behavior.
- acceptance: Scenarios in spec satisfied
- scope: live, tuner

## REQ-live-tuner-relief-effort-counters-are-ac
- source: openspec/specs/live-tuner/spec.md
- description: The firmware SHALL accumulate relief effort in commanded-MMU mm:
`g_sync_refill_effort_mm` while the buffer is in TENSION and
`g_sync_relieve_effort_mm` while in COMPRESSION, derived from the existing
commanded-MMU mm integration. Both counters SHALL reset on sync state change
and buffer-state change (sustained-since-entry semantics). Both SHALL be
exposed in the `protocol.c` status line and as GET parameters. No control
behavior SHALL derive from these counters.
- acceptance: Scenarios in spec satisfied
- scope: live, tuner

## REQ-live-tuner-warn-only-effort-threshold-ev
- source: openspec/specs/live-tuner/spec.md
- description: The firmware SHALL emit warn-only diagnostic events when sustained relief
effort exceeds a configured threshold. A `SYNC cannot_refill` event MUST be
emitted once per episode when refill effort crosses
`CONF_SYNC_CANNOT_REFILL_MM` while still in TENSION. A `SYNC cannot_relieve`
event MUST be emitted once per episode when relieve effort crosses
`CONF_SYNC_CANNOT_RELIEVE_MM` while still in COMPRESSION. These events MUST be
diagnostic only and MUST NOT alter control output.
- acceptance: Scenarios in spec satisfied
- scope: live, tuner

## REQ-marker-capture-policy-sidecar-is-the-onl
- source: openspec/specs/marker-capture-policy/spec.md
- description: The Klipper sidecar path SHALL be the single live-capture mechanism. The
shell-marker capture path SHALL NOT exist: `gcode_marker.py` SHALL NOT
provide shell `--emit` modes (`m118`/`mark`/`file`/`both`) and SHALL NOT
provide `--every-layer`; `flare_live_tuner.py` SHALL NOT provide
`--klipper-mode off`, `--marker-file`, or `--keep-marker-file`. The
legacy `scripts/flare_marker.py` and `scripts/flare_logger.py` SHALL be
removed.
- acceptance: Scenarios in spec satisfied
- scope: marker, capture, policy

## REQ-marker-capture-policy-no-deprecation-not
- source: openspec/specs/marker-capture-policy/spec.md
- description: Because the deprecated features are removed, no deprecation notice SHALL
remain in tracked code or docs. There SHALL be no `deprecated` stderr
warning and no `**DEPRECATED**` documentation label for these paths, and
no documentation section SHALL describe a removed path.
- acceptance: Scenarios in spec satisfied
- scope: marker, capture, policy

## REQ-marker-capture-policy-docs-describe-curr
- source: openspec/specs/marker-capture-policy/spec.md
- description: Operator and context documentation SHALL describe the system as it exists
now. Internal "Phase 2.x"-style milestone labels SHALL NOT appear in
`CONTEXT.md`, `BEHAVIOR.md`, `KLIPPER.md`, or `MANUAL.md`. Surviving
technical content SHALL be reworded as current behavior rather than
deleted.
- acceptance: Scenarios in spec satisfied
- scope: marker, capture, policy

## REQ-motion-safety-dry-spin-protection
- source: openspec/specs/motion-safety/spec.md
- description: The system SHALL halt any spinning motor if no filament is detected at the intake and the buffer is not pulling.
- acceptance: Scenarios in spec satisfied
- scope: motion, safety

## REQ-motion-safety-task-travel-limits
- source: openspec/specs/motion-safety/spec.md
- description: Automated tasks SHALL NOT spin indefinitely without hitting a physical checkpoint.
- acceptance: Scenarios in spec satisfied
- scope: motion, safety

## REQ-motion-safety-safe-autopreload
- source: openspec/specs/motion-safety/spec.md
- description: Autopreload SHALL only engage for freshly inserted filament and MUST leave the path clear for the other lane.
- acceptance: Scenarios in spec satisfied
- scope: motion, safety

## REQ-motion-safety-non-destructive-jam-relief
- source: openspec/specs/motion-safety/spec.md
- description: Trailing/overfull and hard-wall handling SHALL NOT discard the extruder
estimator, drift observer, or sigma/confidence state. Destructive reset SHALL
be reserved for true off transitions only.
- acceptance: Scenarios in spec satisfied
- scope: motion, safety

## REQ-motion-safety-under-extrusion-direction-
- source: openspec/specs/motion-safety/spec.md
- description: The controller SHALL never pause local assist while the buffer is empty-side
(`BUF_TENSION`), and SHALL prioritize avoiding under-extrusion over relieving
overfull.
- acceptance: Scenarios in spec satisfied
- scope: motion, safety

## REQ-motion-safety-terminal-jam-paths-enter-n
- source: openspec/specs/motion-safety/spec.md
- description: Hard-wall critical and tension-dwell stop SHALL transition the sync
controller to `SYNC_FAULT_HOLD` instead of calling destructive
`sync_disable(true)`. Entry MUST stop motion (`sync_current_sps = 0`) and
MUST NOT reset the extruder estimator, drift observer, sigma/confidence, or
reserve integrator. Each path SHALL emit a `SYNC FAULT_HOLD` event.
- acceptance: Scenarios in spec satisfied
- scope: motion, safety

## REQ-motion-safety-fault-hold-auto-recovers-w
- source: openspec/specs/motion-safety/spec.md
- description: While in `SYNC_FAULT_HOLD` the controller SHALL remain motion-stopped until
`CONF_SYNC_FAULT_HOLD_RECOVERY_MS` has elapsed since entry, then transition
to `SYNC_OFF` and emit `SYNC FAULT_HOLD_RECOVERY`, allowing normal auto-arm
to re-enter `SYNC_ACTIVE` reusing the preserved estimator subject to
existing freshness aging. No host command SHALL be required to recover.
- acceptance: Scenarios in spec satisfied
- scope: motion, safety

## REQ-motion-safety-bl-prime-respects-travel-c
- source: openspec/specs/motion-safety/spec.md
- description: The `BL` prime move SHALL terminate no later than `BUF_MAX_TRAVEL_MM / 2` mm
of MMU travel, regardless of whether the target buffer raw state has been
reached. The prime MUST NOT escalate to an unbounded drive on a stuck or
mis-wired buffer switch.
- acceptance: Scenarios in spec satisfied
- scope: motion, safety

## REQ-motion-safety-bl-catch-bypasses-mv-buffe
- source: openspec/specs/motion-safety/spec.md
- description: The instant-slam catch driven by `BL` lock-break SHALL run as a sync-owned
drive and is exempt from the `TASK_MOVE` buffer-fault guards
(`FAULT:MOVE_TENSION` on retract-into-tension and `FAULT:MOVE_COMPRESSION`
on forward-into-compression). The exemption SHALL apply only while the
controller is in `SYNC_RETRACT_ASSIST` and the catch sub-state is active.
- acceptance: Scenarios in spec satisfied
- scope: motion, safety

## REQ-operator-tuning-guide-self-contained-jar
- source: openspec/specs/operator-tuning-guide/spec.md
- description: The repository SHALL provide a `TUNING.md` that a user with no firmware or
internals knowledge can follow end to end. It MUST NOT contain internal
"Phase 2.x" (or similar internal-phase) labels. It MUST begin with a
"simplest path" TL;DR that gets defaults working, followed by a plain
explanation of what tuning does (baseline and compression-bias, what good
and bad behavior look like) with no assumed firmware knowledge.
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-exact-copy-paste-c
- source: openspec/specs/operator-tuning-guide/spec.md
- description: Every command in `TUNING.md` MUST be copy-paste accurate against the
current scripts as they are (`flare_live_tuner.py`, `gcode_marker.py`,
`flare_analyze.py`, `flare_baseline_recommender.py`, `gen_config.py`,
`flash_flare.py`). The guide MUST cover prerequisites with exact commands
(find serial port, find Klipper socket, install pyserial, back up the
state file). Where a script cannot match an operator-friendly command
as-is, the guide MUST describe the script as-is and the limitation MUST be
recorded as an open question rather than changing code.
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-recovery-path-for-
- source: openspec/specs/operator-tuning-guide/spec.md
- description: `TUNING.md` MUST provide a "scary behavior" recovery path, reachable from
the TL;DR, that is followed BEFORE any capture/tuning when the setup
misbehaves (repeated `FAULT_HOLD`, repeated `cannot_refill`/
`cannot_relieve`, jams, stalls, or a pinned buffer). It MUST instruct the
user to revert to the shipped scalar defaults and reflash, verify boring
behavior, and treat persistent faults as mechanical (with concrete checks)
rather than a tuning value, and MUST state that tuning on a misbehaving
setup is rejected by the analyzer.
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-sidecar-is-the-onl
- source: openspec/specs/operator-tuning-guide/spec.md
- description: `TUNING.md` SHALL document the Klipper sidecar capture path as the single
live-capture mechanism.
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-two-profile-determ
- source: openspec/specs/operator-tuning-guide/spec.md
- description: `TUNING.md` MUST state the two-profile bracket model up front: print the
same model twice (fastest-cubic-flow profile and slowest-cubic-flow
profile), capture each, and derive one deterministic result. It MUST give
the exact analyze command using `--profile-fast`, `--profile-slow`, and
`--emit-flow-schedule`, show what the output looks like, explain the
sparse→one-point fallback, and explain that identical inputs give
identical output (and that the scalar one-point path is the safe simple
choice).
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-apply-recommender-
- source: openspec/specs/operator-tuning-guide/spec.md
- description: `TUNING.md` MUST give exact review/apply steps (which `config.ini` keys —
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
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-sync-feedback-sens
- source: openspec/specs/operator-tuning-guide/spec.md
- description: `TUNING.md` and `config.ini.example` SHALL describe buffer sensor mode with
Happy Hare Sync-Feedback Sensor type codes: `D` = Dual two-switch sensor
(`BUF_SENSOR_TYPE == 0`, D=0), `P` = Proportional analog sensor
(`BUF_SENSOR_TYPE == 1`, P=1), and `TO`/`CO` as recognized but not
implemented in FLARE. The docs SHALL name the sensor separately from the
control law: type-D two-level / hysteretic relay control law, type-P analog
PD/EKF reserve control law.
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-tuning-md-uses-syn
- source: openspec/specs/operator-tuning-guide/spec.md
- description: TUNING.md SHALL use the Sync-Feedback Sensor vocabulary with Happy Hare
type codes (P, D; TO/CO noted as unimplemented) and SHALL document the
`BUF_SENSOR_TYPE` value contract (D=0, P=1) where sensor mode is
referenced, naming the sensor separately from the control law and not
using the legacy analog alias.
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-tuning-md-relay-se
- source: openspec/specs/operator-tuning-guide/spec.md
- description: The TUNING.md relay content SHALL describe only the fallback relay law
(`relay_catchup_frac`, `relay_neutral_frac`), the deep-COMPRESSION
collapse-ramp keys, and the `relay_min_flip_mm` 0.0/deadlock caveat. It
SHALL NOT document a relay duty estimator, a confidence gate, an offline
relay capture/analyze/apply loop, or the bimodal duty-ratchet note —
that machinery no longer exists.
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-type-d-tuning-guid
- source: openspec/specs/operator-tuning-guide/spec.md
- description: Operator-facing type-D tuning guidance SHALL attribute the relay limit cycle
and its COMPRESSION/TENSION drift to `relay_neutral_frac` (and
`relay_catchup_frac`), and SHALL NOT instruct the operator to change
`sync_kp_rate` for a type-D (`BUF_SENSOR_TYPE == 0`) buffer. This requirement
applies to both `TUNING.md` and the verdict/help text emitted by
`flare_sync_check.py` (`analyze_stability`, `analyze_drift`). `sync_kp_rate`
guidance MAY appear only for analog type P (`BUF_SENSOR_TYPE == 1`), whose
`psf_control_law` actually consumes it.
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-default-relay-neut
- source: openspec/specs/operator-tuning-guide/spec.md
- description: The shipped default `relay_neutral_frac` SHALL match demand (`1.00`) for type-D
after the no-overshoot ramp fix, not deliberately overfeed. `TUNING.md` and
`config.ini.example` SHALL show this default, and it SHALL match the
`gen_config.py` default. Operators MAY raise it slightly only if hardware soak
shows steady TENSION drift.
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-type-d-relay-trim-
- source: openspec/specs/operator-tuning-guide/spec.md
- description: For `BUF_SENSOR_TYPE == 0`, the volatile neutral feed trim SHALL only ever
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
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-type-d-compression
- source: openspec/specs/operator-tuning-guide/spec.md
- description: For `BUF_SENSOR_TYPE == 0`, COMPRESSION feed SHALL be a bounded fraction of
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
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-asymmetric-relay-c
- source: openspec/specs/operator-tuning-guide/spec.md
- description: A host analyzer SHALL parse the status poll stream and report, over a window:
TENSION touch count (the hard constraint; target `0`), COMPRESSION pin
duration, mean `EST − MM` during `BUF_NEUTRAL` (the underfeed / tension-drift
signature), the `BP` distribution and minimum, and the relay cycle period. The
analyzer SHALL emit an asymmetric-objective verdict: PASS only when TENSION
touches are zero; otherwise it SHALL recommend the controller-correct lever
(raise `relay_neutral_frac` or adjust the COMPRESSION drain / trim settings) and
SHALL NOT recommend `sync_kp_rate` for type-D. The analyzer SHALL operate
read-only from existing poll fields (`BP`, `BUF`, `MM`, `EST`) and SHALL NOT
require new firmware telemetry.
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-type-d-estimator-a
- source: openspec/specs/operator-tuning-guide/spec.md
- description: For `BUF_SENSOR_TYPE == 0`, the firmware SHALL treat the
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
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-type-d-reserve-tar
- source: openspec/specs/operator-tuning-guide/spec.md
- description: For `BUF_SENSOR_TYPE == 0`, the firmware SHALL park the virtual neutral target
slightly toward the compression side using the existing `SYNC_RESERVE_PCT`
reserve percentage. This reserve SHALL give sharp real-print speed-ups physical
headroom before the buffer reaches TENSION. This SHALL NOT change analog type-P
control behavior, and it SHALL NOT require increasing `relay_neutral_frac` above
the demand-match default.
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-operator-tuning-guide-type-d-estimator-a
- source: openspec/specs/operator-tuning-guide/spec.md
- description: For `BUF_SENSOR_TYPE == 0`, the estimator SHALL use a faster attack when a
switch-crossing demand sample is higher than the current `extruder_est_sps`.
The firmware SHALL blend that sample with `SYNC_EST_ATTACK_ALPHA`, a
runtime/non-persisted float clamped `[0.65, 1.0]`, and SHALL bypass the normal
`EST_ALPHA_MAX` clamp for that rising-demand update.
When the sample is lower than or equal to current `extruder_est_sps`, the
estimator SHALL keep the existing dwell-derived EMA clamped by
`EST_ALPHA_MIN..EST_ALPHA_MAX`, so falling demand remains slow and does not
reintroduce COMPRESSION chatter. This SHALL apply only to type-D; analog type-P
per-tick estimation and `psf_control_law` SHALL remain unchanged.
- acceptance: Scenarios in spec satisfied
- scope: operator, tuning, guide

## REQ-persistence-contract-settings-version-bu
- source: openspec/specs/persistence-contract/spec.md
- description: The runtime settings layout MUST be protected by a strict schema version.
- acceptance: Scenarios in spec satisfied
- scope: persistence, contract

## REQ-persistence-contract-flash-loading-and-d
- source: openspec/specs/persistence-contract/spec.md
- description: Missing or corrupt flash SHALL NOT prevent safe boot.
- acceptance: Scenarios in spec satisfied
- scope: persistence, contract

## REQ-persistence-contract-runtime-tunables-fl
- source: openspec/specs/persistence-contract/spec.md
- description: Any **durable** tunable (tier T1 or T2) SHALL live in `config.ini` and flow
through `gen_config.py`. A T3 internal constant SHALL NOT enter this flow; it
lives in its owning source module (see the `config-surface-tiers` capability)
and is exempt from the config/persist/SET/GET path.
- acceptance: Scenarios in spec satisfied
- scope: persistence, contract

## REQ-persistence-contract-persisted-fields-ro
- source: openspec/specs/persistence-contract/spec.md
- description: Every field of `settings_t` written by `settings_save()` SHALL be read back by
`settings_load()` and SHALL have its owning runtime global initialized by
`settings_defaults()`. No field may be write-only (saved to flash but never
loaded), because such a field silently discards an operator's `SV:`-persisted
value on the next boot.
- acceptance: Scenarios in spec satisfied
- scope: persistence, contract

## REQ-persistence-contract-settings-round-trip
- source: openspec/specs/persistence-contract/spec.md
- description: `SETTINGS_VERSION` SHALL NOT be incremented by a fix that only completes the
load/default arms of fields already present in the `settings_t` layout, since
the on-flash byte layout is unchanged and persisted operator settings MUST
survive.
- acceptance: Scenarios in spec satisfied
- scope: persistence, contract

## REQ-project-architecture-firmware-shall-rema
- source: openspec/specs/project-architecture/spec.md
- description: FLARE firmware SHALL run as cooperative RP2040 firmware without an RTOS, with the
main loop calling non-blocking module ticks.
- acceptance: Scenarios in spec satisfied
- scope: project, architecture

## REQ-project-architecture-module-ownership-sh
- source: openspec/specs/project-architecture/spec.md
- description: Each firmware module SHALL keep ownership aligned with the documented
architecture boundaries. A module MAY be split into multiple cohesive translation
units provided each unit keeps a single domain owner and the file map stays
documented.
- acceptance: Scenarios in spec satisfied
- scope: project, architecture

## REQ-project-architecture-runtime-tunables-sh
- source: openspec/specs/project-architecture/spec.md
- description: Persistent runtime tunables SHALL be represented consistently across config
files, generated firmware headers, runtime storage, serial protocol, and docs.
- acceptance: Scenarios in spec satisfied
- scope: project, architecture

## REQ-project-architecture-serial-protocol-cha
- source: openspec/specs/project-architecture/spec.md
- description: USB serial commands SHALL continue using `CMD:params\n` input and `OK:` / `ER:`
reply semantics, with best-effort `EV:` events where applicable.
- acceptance: Scenarios in spec satisfied
- scope: project, architecture

## REQ-project-architecture-persistence-shall-r
- source: openspec/specs/project-architecture/spec.md
- description: Flash persistence commands SHALL be rejected while motion, toolchange, cutter
activity, or boot stabilization could make persistence unsafe.
- acceptance: Scenarios in spec satisfied
- scope: project, architecture

## REQ-project-architecture-sync-shall-not-run-
- source: openspec/specs/project-architecture/spec.md
- description: Normal sync control SHALL remain guarded so it runs only when the toolchange
context is idle.
- acceptance: Scenarios in spec satisfied
- scope: project, architecture

## REQ-project-architecture-load-and-unload-saf
- source: openspec/specs/project-architecture/spec.md
- description: Load, unload, autoload, and related lane tasks SHALL use distance limits and
sensor state rather than legacy names that imply time-only limits.
- acceptance: Scenarios in spec satisfied
- scope: project, architecture

## REQ-project-architecture-shared-speed-conver
- source: openspec/specs/project-architecture/spec.md
- description: Speed conversion SHALL use shared helper functions rather than duplicate
conversions between slicer units, firmware steps-per-second, and protocol
values.
- acceptance: Scenarios in spec satisfied
- scope: project, architecture

## REQ-project-architecture-board-pin-assumptio
- source: openspec/specs/project-architecture/spec.md
- description: Board-level pin assignments and hardware constants SHALL remain centralized in
`firmware/include/config.h` and generated tune headers where applicable.
- acceptance: Scenarios in spec satisfied
- scope: project, architecture

## REQ-project-architecture-buffer-service-comm
- source: openspec/specs/project-architecture/spec.md
- description: `BS` SHALL cancel active sync, buffer lock, an existing buffer-stabilize drive,
and standalone lane commands before starting a fresh buffer stabilize, while
hard activities (`TC`, cutter, manual unload) SHALL still reject with `ER:BUSY`.
`BL:T` and `BL:C` SHALL cancel an active buffer-stabilize drive before arming
buffer lock so tip-form macros can transition from neutralization to lock without
a racy delay.
- acceptance: Scenarios in spec satisfied
- scope: project, architecture

## REQ-psf-type-p-sensor-type-p-relief-pause-au
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: The firmware SHALL re-arm type-P sync from `SYNC_RELIEF_PAUSE` when the buffer is
under genuine extruder demand, via two complementary paths:
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-psf-type-p-sensor-type-p-stabilize-rail-
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: The firmware SHALL allow type-P idle/boot buffer-stabilize to drive the buffer
off a saturated rail to goal. While the analog signal is saturated
(`g_buf_analog_saturated_since_ms != 0`), the stagnation guard SHALL NOT abort on
the short-window position-change test; it SHALL keep driving and re-baseline the
stagnation reference position, aborting only if the buffer remains saturated past
`PSF_STAB_RAIL_BREAK_MS` measured from stabilize start. Once the signal
desaturates, the firmware SHALL apply the standard dry-spin stagnation check
(`PSF_STAB_STAGNANT_MS` / `PSF_STAB_STAGNANT_NORM`) with its window measured from
desaturation, not from stabilize start. Type-D stabilize is unchanged.
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-psf-type-p-sensor-type-p-tension-refill-
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: The firmware SHALL bypass the type-P distance-based feed smoothing on the
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
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-psf-type-p-sensor-psf-endpoint-calibrati
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: The firmware SHALL support runtime calibration of PSF sensor endpoints via
`CAL:PSF_COMP`, `CAL:PSF_TENS`, and `CAL:PSF_NEUT` commands. Each command
SHALL sample the ADC at the moment of invocation, store the result in the
corresponding runtime variable, and persist it to NVM.
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-psf-type-p-sensor-asymmetric-normalizati
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: The firmware SHALL normalize raw PSF ADC readings to [-1,1] using asymmetric
endpoint calibration. Polarity SHALL be auto-detected from calibration values:
if `BUF_PSF_MAX_COMP < BUF_PSF_MAX_TENS`, compression is the lower raw value
(reversed=true); otherwise compression is the higher raw value. No explicit
invert flag SHALL be required.
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-psf-type-p-sensor-goal-relative-zone-bou
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: For type-P sensors, buffer zone boundaries SHALL be derived from `BUF_GOAL`
converted to normalized space (TENSION / NEUTRAL / COMPRESSION), not from a
symmetric `BUF_THR`. NEUTRAL SHALL mean "near goal," not "near raw 0.5."
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-psf-type-p-sensor-buf-goal-user-param-in
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: `BUF_GOAL` SHALL be settable and gettable via the protocol in raw ADC fraction
[0,1] space — the same space as `BUF_PSF_MAX_COMP`, `BUF_PSF_MAX_TENS`, and
`BUF_PSF_NEUTRAL`. Default SHALL be 0.3 (between neutral 0.5 and compression
extreme 0.0 for normal PSF polarity). It SHALL be persisted in NVM.
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-psf-type-p-sensor-remove-buf-range-and-b
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: `BUF_RANGE` and `BUF_INVERT` SHALL be removed from the protocol and NVM.
Polarity is handled by calibration (D2). `BUF_RANGE` is superseded by
asymmetric endpoint calibration.
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-psf-type-p-sensor-continuous-extruder-es
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: For type-P, the firmware SHALL compute buffer velocity from the per-tick
position delta and update the extruder-rate estimate every control tick, using
`extruder_mm_s = mmu_mm_s + arm_vel` where `arm_vel = vel_norm *
half_travel_mm`. It SHALL NOT use crossing-event estimation for type-P.
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-psf-type-p-sensor-gradual-pd-control-wit
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: For type-P, `psf_control_law()` SHALL produce a target feed rate as the sum of
a continuous feedforward (`extruder_est_sps`), a proportional term on position
error relative to goal, and a derivative term on filtered buffer velocity. The
proportional term SHALL be suppressed within `PSF_CTRL_DEADBAND` of goal; the
derivative term SHALL remain active regardless of dead zone.
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-psf-type-p-sensor-filtered-derivative
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: The derivative term SHALL operate on a low-pass-filtered velocity computed from
the already-smoothed position, to avoid amplifying ADC noise (derivative kick).
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-psf-type-p-sensor-soft-walls
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: For type-P, the control target SHALL progressively blend from the PD output
toward a safety limit as `|pos_norm|` enters `[PSF_SOFT_WALL_START, 1.0]`:
toward maximum feed on the tension side (urgent refill) and toward zero on the
compression side (stop overfeed).
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-psf-type-p-sensor-hard-catch-and-print-s
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: For type-P, a rapid velocity spike toward compression SHALL trigger a
reversible fast brake. The firmware SHALL then disambiguate a transient
slowdown from a real print stop by observing subsequent buffer motion: if the
buffer drifts back toward tension within `PSF_STOP_CONFIRM_MS`, control SHALL
resume; if the buffer stays pinned at the compression wall, the controller
SHALL enter relief pause.
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-psf-type-p-sensor-type-p-unload-uses-no-
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: For type-P sensors, `TASK_UNLOAD` SHALL NOT use a position-based over-tension
guard (relief jog or tension-dwell block); it SHALL rely on the `UNLOAD_MAX`
distance limit (`UNLOAD_TIMEOUT`) for the stuck case. The type-D guards (recover
jog + `UNLOAD_TENSION_BLOCK`) SHALL remain unchanged and gated `BUF_SENSOR_TYPE == 0`.
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-psf-type-p-sensor-type-p-fault-timers-sc
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: The firmware SHALL scope the type-P tension-dwell and saturation fault timers to
the active-sync window so idle-accumulated state cannot fire a spurious fault on
engagement or deadlock fault recovery. On every type-P sync activation (normal
auto-start, relief-pause re-arm, fault-hold recovery) the firmware SHALL restart
`sync_tension_pin_since_ms` to `now` when the buffer is in `BUF_TENSION` and to `0`
otherwise. On `FAULT_HOLD_RECOVERY` the firmware SHALL reset
`g_buf_analog_saturated_since_ms` so the recovered active state gets a fresh
saturation window. Type-D fault handling is unchanged.
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-psf-type-p-sensor-type-p-feed-quality-an
- source: openspec/specs/psf-type-p-sensor/spec.md
- description: Type-P feed control SHALL track extruder demand on a real print without sustained
buffer hunting or end-of-move overshoot that produces print artifacts, and a manual
`BS` SHALL drive the buffer to goal in a single invocation from any non-saturated
position. Acceptance is measured against a real print, not isolated bench bursts.
- acceptance: Scenarios in spec satisfied
- scope: psf, type, p, sensor

## REQ-python-host-tooling-style-ruff-lint-conf
- source: openspec/specs/python-host-tooling-style/spec.md
- description: Repo SHALL carry a `pyproject.toml` `[tool.ruff]` config pinning `line-length`,
`target-version`, and an explicit rule `select` set, and `scripts/*.py` SHALL pass
`ruff check` under it.
- acceptance: Scenarios in spec satisfied
- scope: python, host, tooling, style

## REQ-python-host-tooling-style-python-lint-in
- source: openspec/specs/python-host-tooling-style/spec.md
- description: `scripts/validate_regression.py` SHALL run `ruff check scripts/` so lint regressions
fail the gate alongside the existing `py_compile` and `unittest` steps.
- acceptance: Scenarios in spec satisfied
- scope: python, host, tooling, style

## REQ-python-host-tooling-style-behavior-prese
- source: openspec/specs/python-host-tooling-style/spec.md
- description: Lint fixes SHALL be behavior-preserving; the `scripts/` `unittest` suite SHALL stay
green and host-tooling behavior unchanged.
- acceptance: Scenarios in spec satisfied
- scope: python, host, tooling, style

## REQ-python-host-tooling-style-diagnostic-scr
- source: openspec/specs/python-host-tooling-style/spec.md
- description: Every script under `scripts/` SHALL have at least one live (non-archived) reference: a
Python import, a live doc mention, or a backing spec. A standalone diagnostic whose only
references are archived OpenSpec changes is dead and SHALL be removed; its history remains
recoverable via `git show <rev>:scripts/<name>.py`. The regression gate (`py_compile`,
`ruff`, and `unittest discover -p test_*.py`) SHALL stay green across the removal, since a
dead script has no live importer or test.
- acceptance: Scenarios in spec satisfied
- scope: python, host, tooling, style

## REQ-relay-fallback-only-type-d-relay-neutral
- source: openspec/specs/relay-fallback-only/spec.md
- description: For `BUF_SENSOR_TYPE == 0`, the NEUTRAL feed target SHALL always be
`clamp(extruder_est_sps, SYNC_MIN, relay_base) · RELAY_NEUTRAL_FRAC`.
There SHALL be no confidence-gated duty-estimator path, no `[lo,hi]`
estimate clamp, and no estimator/seed selection. The TENSION (catch-up)
and COMPRESSION (`SYNC_MIN`) branches SHALL be unchanged.
- acceptance: Scenarios in spec satisfied
- scope: relay, fallback, only

## REQ-relay-fallback-only-firmware-drops-the-d
- source: openspec/specs/relay-fallback-only/spec.md
- description: The firmware SHALL NOT contain the relay duty-estimator state, the
`v_est` blend, the confidence gate, the pair-history/travel
accumulators, or the cold-start estimate seed. No code path may
reference the deleted estimator state.
- acceptance: Scenarios in spec satisfied
- scope: relay, fallback, only

## REQ-relay-fallback-only-protocol-drops-estim
- source: openspec/specs/relay-fallback-only/spec.md
- description: The device protocol and `flare_cmd.py --dump` SHALL NOT expose the
`RDE`, `RDCF`, or `RDV` fields or any estimate/confidence `SET:`/`GET:`
parameters. Unrelated status fields (`BUF`, `BP`, `EST`, `NC`, …) SHALL
be unchanged.
- acceptance: Scenarios in spec satisfied
- scope: relay, fallback, only

## REQ-relay-fallback-only-analyzer-emits-no-re
- source: openspec/specs/relay-fallback-only/spec.md
- description: `flare_analyze.py` SHALL NOT compute or emit relay duty-cycle
recommendations (`relay_estimate_lo`/`relay_estimate_hi`/
`relay_seed_rate`) or a relay coverage verdict. All non-relay analyzer
output (`baseline_rate`, flow schedule, acceptance gate, verdicts) SHALL
be byte-identical to before this change on existing non-relay inputs.
- acceptance: Scenarios in spec satisfied
- scope: relay, fallback, only

## REQ-relay-fallback-only-config-surface-drops
- source: openspec/specs/relay-fallback-only/spec.md
- description: Config, the generator, and persisted settings SHALL NOT define
`relay_estimate_lo`, `relay_estimate_hi`, `relay_confidence_cycles`,
`relay_confidence_window_ms`, or `relay_seed_warmup_ms`, and
`SETTINGS_VERSION` SHALL be bumped. `relay_catchup_frac`,
`relay_neutral_frac`, `relay_min_flip_mm`, and the `relay_collapse_*`
keys SHALL be retained with unchanged behavior.
- acceptance: Scenarios in spec satisfied
- scope: relay, fallback, only

## REQ-reserve-safety-floor-reserve-bias-is-flo
- source: openspec/specs/reserve-safety-floor/spec.md
- description: The effective trailing-bias used to compute the reserve target SHALL be
`max(SYNC_TRAILING_BIAS_FRAC, schedule_bias)`. The flow schedule MAY
deepen the reserve (bias above the configured scalar) but SHALL NOT
reduce it below `SYNC_TRAILING_BIAS_FRAC` for any flow, segment, or
clamped endpoint.
- acceptance: Scenarios in spec satisfied
- scope: reserve, safety, floor

## REQ-reserve-safety-floor-baseline-control-fl
- source: openspec/specs/reserve-safety-floor/spec.md
- description: `baseline_control_floor_sps()` SHALL return
`max(flow_param(extruder_est_sps).baseline_sps, g_baseline_target_sps)`,
restoring the guarantee that the control floor — and the ADVANCE recovery
gain derived from it — never drops below the configured persistent
baseline.
- acceptance: Scenarios in spec satisfied
- scope: reserve, safety, floor

## REQ-reserve-safety-floor-degenerate-single-p
- source: openspec/specs/reserve-safety-floor/spec.md
- description: The floored bias and baseline SHALL equal the configured scalars when the
schedule has a single point equal to those scalars, so behavior MUST be
byte-identical to the pre-flow-keyed scalar controller.
- acceptance: Scenarios in spec satisfied
- scope: reserve, safety, floor

## REQ-reserve-safety-floor-schedule-and-live-l
- source: openspec/specs/reserve-safety-floor/spec.md
- description: The flow schedule and the live per-segment learner SHALL only ever
strengthen reserve depth and the baseline floor relative to the
configured scalars; neither SHALL reduce reserve depth or baseline floor
below config. This is the controller's full-bias safety invariant.
- acceptance: Scenarios in spec satisfied
- scope: reserve, safety, floor

## REQ-script-path-handling-tilde-and-glob-expa
- source: openspec/specs/script-path-handling/spec.md
- description: Host scripts SHALL expand `~` and full glob syntax (`*`, `?`, `[...]`,
recursive `**`) for input/read path arguments. Resolution SHALL be
deterministic and sorted. The affected input arguments are
`gcode_marker.py` positional `input`, `flare_baseline_recommender.py
--file`, `flare_analyze.py --in` / `--profile-fast` / `--profile-slow`,
and `flare_live_tuner.py --sidecar`. List-valued arguments accept
multiple matches; single-valued arguments require exactly one match.
- acceptance: Scenarios in spec satisfied
- scope: script, path, handling

## REQ-script-path-handling-output-paths-are-ne
- source: openspec/specs/script-path-handling/spec.md
- description: Write/output path arguments SHALL be `~`-expanded only and SHALL NOT be
glob-expanded. The affected arguments include `gcode_marker.py
--output`, `flare_analyze.py --out`, `flare_live_tuner.py --state` and
`--csv-out`.
- acceptance: Scenarios in spec satisfied
- scope: script, path, handling

## REQ-script-path-handling-path-errors-produce
- source: openspec/specs/script-path-handling/spec.md
- description: The scripts SHALL report path-argument failures as a single stderr line
of the form `Error: <path>: <reason>` and SHALL exit non-zero with no
Python traceback. This MUST cover missing files, no-glob-match,
not-a-regular-file, and permission-denied. Unrelated exceptions MUST
still propagate.
- acceptance: Scenarios in spec satisfied
- scope: script, path, handling

## REQ-script-path-handling-existing-analyzer-i
- source: openspec/specs/script-path-handling/spec.md
- description: The shared resolution helper SHALL preserve `flare_analyze.py --in`
behavior: the set and sorted order of resolved runs for a given glob or
explicit file list SHALL match the pre-change behavior.
- acceptance: Scenarios in spec satisfied
- scope: script, path, handling

## REQ-spec-compression-workflow-portable-compr
- source: openspec/specs/spec-compression-workflow/spec.md
- description: The project SHALL maintain an in-repo file `openspec/COMPRESSION.md` that fully defines the semantic compression applied to OpenSpec artifact prose, such that any agent UI can apply it by reading the file alone, with no dependency on a Claude-specific skill, plugin, API, or binary.
- acceptance: Scenarios in spec satisfied
- scope: spec, compression, workflow

## REQ-spec-compression-workflow-contract-prese
- source: openspec/specs/spec-compression-workflow/spec.md
- description: The compression ruleset SHALL forbid altering RFC-2119 normative keywords (SHALL, MUST, SHOULD, MAY, REQUIRED) and SHALL forbid dropping, merging, or reordering any normative clause, and SHALL treat `
- acceptance: Scenarios in spec satisfied
- scope: spec, compression, workflow

## REQ-spec-compression-workflow-and-scenario-h
- source: openspec/specs/spec-compression-workflow/spec.md
- description: #### Scenario: Normative clause survives compression
- **WHEN** a spec body containing a `SHALL`/`MUST` requirement is compressed per `openspec/COMPRESSION.md`
- **THEN** the keyword and the full normative clause remain present and unchanged
- **AND** requirement/scenario headers and WHEN/THEN markers are byte-identical to the source
- acceptance: Scenarios in spec satisfied
- scope: spec, compression, workflow

## REQ-spec-compression-workflow-cross-ui-autho
- source: openspec/specs/spec-compression-workflow/spec.md
- description: The repository SHALL instruct every agent, via `AGENTS.md` and the `openspec/config.yaml` `rules:` block, to author OpenSpec spec and change bodies in compressed form per `openspec/COMPRESSION.md`.
- acceptance: Scenarios in spec satisfied
- scope: spec, compression, workflow

## REQ-spec-compression-workflow-compression-re
- source: openspec/specs/spec-compression-workflow/spec.md
- description: The project SHALL provide `scripts/test_spec_compression.py`, a stdlib-only regression test consistent with existing `scripts/test_*.py`, that detects uncompressed spec prose by filler-word density and never modifies any file.
- acceptance: Scenarios in spec satisfied
- scope: spec, compression, workflow

## REQ-spec-readability-per-spec-human-purpose-
- source: openspec/specs/spec-readability/spec.md
- description: Every `openspec/specs/*/spec.md` SHALL begin with an uncompressed `## Purpose` section of 1-3 plain-prose lines that states, for a human reader, what the spec governs and why it exists. The Purpose text SHALL be exempt from caveman/token compression and SHALL NOT restate normative requirements.
- acceptance: Scenarios in spec satisfied
- scope: spec, readability

## REQ-spec-readability-central-spec-to-doc-ind
- source: openspec/specs/spec-readability/spec.md
- description: `openspec/README.md` SHALL contain an index table mapping each capability spec to a one-line human summary and to its paired human-facing document (for example `TUNING.md`, `KLIPPER.md`) where one exists, or marking specs that have no paired human doc.
- acceptance: Scenarios in spec satisfied
- scope: spec, readability

## REQ-static-regression-validation-automated-r
- source: openspec/specs/static-regression-validation/spec.md
- description: The host regression validation test suite MUST automatically discover and execute all unit tests in the scripts directory.
- acceptance: Scenarios in spec satisfied
- scope: static, regression, validation

## REQ-static-regression-validation-standard-te
- source: openspec/specs/static-regression-validation/spec.md
- description: All Python test modules in the repository MUST be compatible with standard test runners (such as unittest and pytest) without triggering module-import exits.
- acceptance: Scenarios in spec satisfied
- scope: static, regression, validation

## REQ-static-regression-validation-dev-tuning-
- source: openspec/specs/static-regression-validation/spec.md
- description: The regression gate MUST build the firmware with `FLARE_DEV_TUNING=ON` so code behind
`#ifdef FLARE_DEV_TUNING` (e.g. `protocol.c` Tier-3 SET/GET handlers) is compiled and
validated, matching the configuration deployed on the Pi.
- acceptance: Scenarios in spec satisfied
- scope: static, regression, validation

## REQ-sync-feedback-compression-recovery-cap-g
- source: openspec/specs/sync-feedback/spec.md
- description: The firmware SHALL apply the shared `sync_compression_recovery_active` feed cap
and its time-based collapse trim only when `BUF_SENSOR_TYPE == 0` (type-D). For type-P,
compression-overfeed backoff SHALL be owned solely by the soft wall (Layer 2) and
hard catch (Layer 3) in `psf_control_law` / `sync_tick`; the recovery cap SHALL
NOT reduce the type-P feed target.
- acceptance: Scenarios in spec satisfied
- scope: sync, feedback

## REQ-sync-feedback-common-normalized-scale-fo
- source: openspec/specs/sync-feedback/spec.md
- description: The sync PD loop SHALL operate on a common normalized position and target for
both type-D and type-P sensors, eliminating type-specific branches in shared
control math. `buf_pos_norm()` SHALL return normalized [-1,1] position for
both types; `buf_target_norm()` SHALL return normalized [-1,1] target.
- acceptance: Scenarios in spec satisfied
- scope: sync, feedback

## REQ-sync-feedback-isolated-control-laws
- source: openspec/specs/sync-feedback/spec.md
- description: Type-D relay bangbang and type-P continuous PD SHALL be implemented as
isolated static functions. No control law logic SHALL appear inline in
`sync_tick()`.
- acceptance: Scenarios in spec satisfied
- scope: sync, feedback

## REQ-sync-feedback-sync-apply-scaling-unified
- source: openspec/specs/sync-feedback/spec.md
- description: `sync_apply_scaling()` SHALL use a single code path operating on normalized
position and target for both sensor types. The type-P early-return branch
SHALL be removed.
- acceptance: Scenarios in spec satisfied
- scope: sync, feedback

## REQ-sync-feedback-compression-floor-removed-
- source: openspec/specs/sync-feedback/spec.md
- description: The firmware SHALL NOT force-raise the feed floor during `BUF_COMPRESSION` for
type-P (the L1750 block is removed). For type-P, COMPRESSION means buffer full;
forcing a feed floor fights drain and is incorrect.
- acceptance: Scenarios in spec satisfied
- scope: sync, feedback

## REQ-sync-refactor-foundation-firmware-sync-s
- source: openspec/specs/sync-refactor-foundation/spec.md
- description: FLARE firmware SHALL run sync behavior from compiled configuration and runtime
settings without requiring a Klipper plugin or host daemon during normal
printing.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor, foundation

## REQ-sync-refactor-foundation-sync-hardening-
- source: openspec/specs/sync-refactor-foundation/spec.md
- description: Instrumentation, estimator confidence, and buffer-behavior changes SHALL be
introduced so existing default behavior remains recognizable unless the operator
opts into new calibration-derived settings.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor, foundation

## REQ-sync-refactor-foundation-runtime-tunable
- source: openspec/specs/sync-refactor-foundation/spec.md
- description: Any durable sync tunable SHALL live in `config.ini` and `config.ini.example`,
flow through `scripts/gen_config.py`, and be consumed from generated
`firmware/include/tune.h` or the matching runtime settings path.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor, foundation

## REQ-sync-refactor-foundation-telemetry-shall
- source: openspec/specs/sync-refactor-foundation/spec.md
- description: Firmware and host tooling SHALL expose enough sync, buffer, and estimator
signals for offline calibration to infer stable operating parameters.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor, foundation

## REQ-sync-refactor-foundation-regression-impa
- source: openspec/specs/sync-refactor-foundation/spec.md
- description: Changes to sync behavior SHALL consider preload, load, unload, toolchange, sync,
RELOAD, persistence, protocol, and documentation effects before landing.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor, foundation

## REQ-sync-refactor-standalone-sync
- source: openspec/specs/sync-refactor/spec.md
- description: FLARE SHALL run sync, toolchange, and RELOAD without host after calibration flash.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-observe-only-calibration
- source: openspec/specs/sync-refactor/spec.md
- description: The system SHALL collect markers, buckets, and patches without mutation unless explicit opt-in.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-sidecar-uds-tracking
- source: openspec/specs/sync-refactor/spec.md
- description: The system SHALL prefer sidecar JSON + Klipper UDS over shell markers when available.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-durable-migratable-state
- source: openspec/specs/sync-refactor/spec.md
- description: The tuner MUST persist buckets and migrate schema without data loss across versions.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-chatter-resistance
- source: openspec/specs/sync-refactor/spec.md
- description: Buckets SHALL become LOCKED on evidence/noise pass and UNLOCK only on strong mismatch.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-relative-noise-gate
- source: openspec/specs/sync-refactor/spec.md
- description: The tuner SHALL use `sigma/x` ratio, not absolute variance, for lock decisions.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-state-aware-recommendation
- source: openspec/specs/sync-refactor/spec.md
- description: The analyzer SHALL weight by precision/count, not raw CSV clusters, during recommendation.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-comparable-run-consistency
- source: openspec/specs/sync-refactor/spec.md
- description: The gate SHALL use recommendation path consistency, filtered to mature runs.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-fail-vs-warn-separation
- source: openspec/specs/sync-refactor/spec.md
- description: The gate SHALL FAIL only on unreliable recommendations or pathological scatter.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-bidirectional-drift-observ
- source: openspec/specs/sync-refactor/spec.md
- description: The residual drift observer SHALL measure errors on both `TENSION` and `COMPRESSION` boundaries to prevent directional blindness.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-double-integrator-avoidanc
- source: openspec/specs/sync-refactor/spec.md
- description: The feedforward velocity estimator SHALL NOT bleed towards the PI controller output in the safe zone.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-bias-accumulation
- source: openspec/specs/sync-refactor/spec.md
- description: The analyzer and tuner SHALL accumulate position error offsets onto the current configuration value, avoiding fixed setpoint anchors.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-disciplined-live-baseline-
- source: openspec/specs/sync-refactor/spec.md
- description: The firmware live baseline learner SHALL update only in `SYNC_ACTIVE`, require
multi-cycle agreement, reject high-variance observations, and enforce a
time-and-distance cooldown. It SHALL remain non-persistent and up-only; the
offline analyzer remains the sole persistent baseline/bias authority.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-non-destructive-lifecycle-
- source: openspec/specs/sync-refactor/spec.md
- description: Replacing destructive disable with explicit non-destructive states SHALL NOT
require host involvement and SHALL keep standalone post-flash operation
intact.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-baseline-and-bias-sourced-
- source: openspec/specs/sync-refactor/spec.md
- description: The sync controller SHALL obtain its baseline and compression-bias by
evaluating the flow-keyed schedule at the live `extruder_est_sps`,
replacing the single-scalar read, with a length-1 schedule as the exact
degenerate fallback. This SHALL NOT alter the full-bias invariant: the
schedule only supplies inputs that `SYNC_ACTIVE` reserve-target control
already consumes; reserve target, `reserve_correction`, `zone_bias`,
soft-wall trim, and collapse ramp SHALL be unchanged.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-live-learner-ratchets-with
- source: openspec/specs/sync-refactor/spec.md
- description: The disciplined live baseline learner SHALL remain ephemeral, up-only,
non-persistent, and disciplined (multi-cycle, variance-reject, cooldown,
`SYNC_ACTIVE`-gated) but SHALL ratchet the baseline of the currently
active flow segment rather than a global scalar. On reboot or settings
reload the schedule SHALL reload from config (offline authority); the
live segment delta SHALL be lost.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-buffer-states-use-tension-
- source: openspec/specs/sync-refactor/spec.md
- description: The buffer state vocabulary SHALL be `BUF_TENSION` (filament tensioned,
buffer empty, printer pulling faster than the MMU pushes), `BUF_COMPRESSION`
(filament compressed, buffer full, MMU pushing faster than the printer
pulls), `BUF_NEUTRAL` (neutral band), and `BUF_FAULT`. The legacy names
`BUF_ADVANCE`, `BUF_TRAILING`, and the buffer-state `BUF_MID`/state-derived
`mid` MUST NOT appear anywhere in firmware, scripts, live specs, or docs,
and no back-compat alias SHALL exist. Arithmetic `mid`/`midpoint`
unrelated to the buffer state is out of scope and MAY remain.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-serial-protocol-tokens-and
- source: openspec/specs/sync-refactor/spec.md
- description: The serial protocol SHALL emit `BUF:TENSION|NEUTRAL|COMPRESSION`, the
corresponding `EV:BS:*` tokens, `EV:SYNC:TENSION_RISK_HIGH`, and renamed
short status field keys for any old-state-derived key (`AD`, `TD`, `APX`,
and similar) in place of the legacy spellings. In-repo parsing scripts
MUST be updated in the same change so they remain consistent.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-config-keys-are-renamed-to
- source: openspec/specs/sync-refactor/spec.md
- description: Configuration keys that named the legacy states SHALL be renamed
(`sync_tension_dwell_stop_ms`, `sync_tension_ramp_delay_ms`,
`sync_compression_bias_frac`, `compression_rate`, `neutral_creep_*`,
`sync_overshoot_neutral_extend`). Legacy keys SHALL be ignored by the
existing unknown-key handling (no new hard-error path is added); a stale
`config.ini` falls back to defaults for the renamed keys. No migration
guide is produced (active development — renames are safe).
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-the-rename-does-not-change
- source: openspec/specs/sync-refactor/spec.md
- description: This rename SHALL be behavior-preserving. A status-line and event
semantics snapshot captured before and after MUST be numerically identical
(only token spellings differ); any behavioral delta is out of scope and
belongs to `audit-sync-polarity`.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-sync-control-polarity-matc
- source: openspec/specs/sync-refactor/spec.md
- description: Sync control SHALL feed faster when the buffer is `BUF_TENSION` (empty,
printer pulling faster than the MMU) and back off when the buffer is
`BUF_COMPRESSION` (full, MMU pushing faster than the printer), in both the
type-D two-level / hysteretic relay control law (`BUF_SENSOR_TYPE == 0`,
D=0) and the type-P analog PD/EKF reserve control law
(`BUF_SENSOR_TYPE == 1`, P=1). No control site SHALL invert this
relationship. `TO` and `CO` are recognized Happy Hare Sync-Feedback Sensor
types but are not implemented in FLARE.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-pin-to-state-decode-is-ver
- source: openspec/specs/sync-refactor/spec.md
- description: The buffer-sensor decode SHALL map a pressed tension switch to
`BUF_TENSION` and a pressed compression switch to `BUF_COMPRESSION`. This
decode MUST be explicitly verified, as it is the origin of the historical
misnaming.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-polarity-fixes-are-isolate
- source: openspec/specs/sync-refactor/spec.md
- description: Behavior-changing polarity fixes SHALL be committed separately from the
prerequisite rename and from each other, each justified by the specific
contradiction it resolves. The rename change MUST remain behavior-preserving.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-sensor-and-control-law-are
- source: openspec/specs/sync-refactor/spec.md
- description: Live prose SHALL name the sensor (Sync-Feedback Sensor type P/D)
separately from the control law. The dual-switch path's law SHALL be
referred to as the "type-D two-level / hysteretic relay control law" and
the analog path's law as the "type-P PD/EKF (reserve) control law";
Wiring shorthand and "relay" MUST NOT be used as if they named the sensor.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-vocabulary-rollout-is-beha
- source: openspec/specs/sync-refactor/spec.md
- description: This change SHALL NOT alter any control logic, protocol token, config key,
or C symbol; it is prose and documented-contract only. The host build and
a captured status/event snapshot MUST be identical before and after.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-type-d-standalone-buffer-c
- source: openspec/specs/sync-refactor/spec.md
- description: The controller SHALL drive the active-lane feed as a two-level / hysteretic
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
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-type-d-compression-relief-
- source: openspec/specs/sync-refactor/spec.md
- description: The controller SHALL stop sync feed (enter `RELIEF_PAUSE`) once a small bounded
overfill is reached while the buffer is pinned in `BUF_COMPRESSION` and not
relieving, rather than only after a fixed blind dwell timer. While pinned in
COMPRESSION with the virtual position at or deepening past `-threshold` and the
accumulated relieve effort exceeding a small budget (on the order of 1-2 mm),
the controller SHALL enter `RELIEF_PAUSE` and emit the `RELIEF_PAUSE` event.
This caps the filament force-fed into a full buffer to the budget. Behavior is
gated to `BUF_SENSOR_TYPE == 0`; type-P analog relief is unchanged. The normal
relay limit cycle, which touches COMPRESSION briefly and leaves it via extruder
draw before the budget accrues, MUST NOT trip this early relief.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-normal-switch-contact-does
- source: openspec/specs/sync-refactor/spec.md
- description: The controller SHALL NOT trigger FAULT_HOLD on normal COMPRESSION or TENSION
switch contact in type-D standalone mode, because switch contact is the
relay-law control signal. The tension-dwell FAULT_HOLD and the
compression-wall-critical FAULT_HOLD SHALL be gated to type-P analog mode
(`BUF_SENSOR_TYPE != 0`, P=1). Genuine idle/runout handling via the existing
relief and continuous-compression auto-stop paths MUST remain unchanged.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-type-d-relief-pause-re-arm
- source: openspec/specs/sync-refactor/spec.md
- description: The controller SHALL re-arm sync from `RELIEF_PAUSE` to `SYNC_ACTIVE` when the
buffer recovers to `BUF_NEUTRAL` (e.g. via the reverse-relieve service), not only
when it reaches `BUF_TENSION`, in type-D standalone mode (`BUF_SENSOR_TYPE == 0`).
On that re-arm the controller SHALL reseed the virtual position toward the
reserve target and bootstrap the feed rate (as the FAULT_HOLD recovery does), so
a resuming print does not have to drain the buffer to empty before the MMU feeds
again. Type-P analog behavior MUST be unchanged.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-refactor-type-d-estimator-does-not-
- source: openspec/specs/sync-refactor/spec.md
- description: In type-D standalone mode the velocity estimator (`extruder_est_sps`) SHALL NOT
be fully replaced by a value derived from a *modeled* (assumed full-span)
TENSION→COMPRESSION transition. The estimator update for that transition SHALL be
blended or rate-capped so a single short or partial transition cannot spike the
estimate and over-feed the subsequent NEUTRAL band. The TENSION catch-up path
(which refills regardless of the estimator) MUST still apply.
- acceptance: Scenarios in spec satisfied
- scope: sync, refactor

## REQ-sync-state-model-explicit-sync-lifecycle
- source: openspec/specs/sync-state-model/spec.md
- description: The sync controller SHALL maintain a single explicit state among
`SYNC_OFF`, `SYNC_ACTIVE`, `SYNC_RETRACT_ASSIST`, `SYNC_RELIEF_PAUSE`, and
`SYNC_FAULT_HOLD`. All sync lifecycle behavior SHALL be derived from this
state rather than independent ad-hoc flags.
- acceptance: Scenarios in spec satisfied
- scope: sync, state, model

## REQ-sync-state-model-non-destructive-relief-
- source: openspec/specs/sync-state-model/spec.md
- description: The controller SHALL enter `SYNC_RELIEF_PAUSE` instead of destructive disable
on a sustained compression/overfull condition, preserving the extruder
estimator, drift observer, sigma/confidence, and reserve integrator.
- acceptance: Scenarios in spec satisfied
- scope: sync, state, model

## REQ-sync-state-model-fault-hold-with-autonom
- source: openspec/specs/sync-state-model/spec.md
- description: The controller SHALL enter `SYNC_FAULT_HOLD` instead of destructive disable on
a hard-wall / jam condition, stopping the motor while preserving controller
state, and SHALL recover conservatively without host involvement.
- acceptance: Scenarios in spec satisfied
- scope: sync, state, model

## REQ-sync-state-model-retract-assist-gate-is-
- source: openspec/specs/sync-state-model/spec.md
- description: The host `BL` buffer-lock command SHALL place the controller in
`SYNC_RETRACT_ASSIST` (the buffer-lock lifecycle state), with normal
closed-loop sync off, post-print negative sync suppressed, controller state
preserved, and learning paused. While locked the gate SHALL NOT react to
buffer changes; on raw departure from the armed extreme it SHALL transition
into an instant-slam catch sub-state. The gate MUST NOT destructively reset
estimator, drift, sigma, or reserve integrator state. The legacy `RA:1` /
`RA:0` host commands and the `RA` status field SHALL be removed; no alias
is provided.
- acceptance: Scenarios in spec satisfied
- scope: sync, state, model

## REQ-sync-state-model-full-bias-invariant-pre
- source: openspec/specs/sync-state-model/spec.md
- description: The reserve/full-biased buffer target (between NEUTRAL and COMPRESSION) SHALL remain
owned exclusively by `SYNC_ACTIVE` control and SHALL be unchanged by this
state model. `SYNC_RELIEF_PAUSE` and `SYNC_FAULT_HOLD` SHALL NOT drain the
buffer below the reserve target by design.
- acceptance: Scenarios in spec satisfied
- scope: sync, state, model

## REQ-sync-state-model-creep-suppressed-in-non
- source: openspec/specs/sync-state-model/spec.md
- description: `neutral_creep` SHALL be active only in `SYNC_ACTIVE` and SHALL be suppressed in
`SYNC_RETRACT_ASSIST`, `SYNC_RELIEF_PAUSE`, and `SYNC_FAULT_HOLD`.
- acceptance: Scenarios in spec satisfied
- scope: sync, state, model

## REQ-sync-state-model-sync-feedback-sensor-ta
- source: openspec/specs/sync-state-model/spec.md
- description: Live sync docs and specs SHALL describe the buffer sensor as a
Sync-Feedback Sensor using Happy Hare type codes: `D` = Dual two-switch
sensor (`BUF_SENSOR_TYPE == 0`), `P` = Proportional analog sensor
(`BUF_SENSOR_TYPE == 1`), `TO` = Tension-Only (not implemented in FLARE),
and `CO` = Compression-Only (not implemented in FLARE). The sensor type
SHALL be named separately from the control law.
- acceptance: Scenarios in spec satisfied
- scope: sync, state, model

## REQ-sync-state-model-sync-feedback-sensor-ta
- source: openspec/specs/sync-state-model/spec.md
- description: Documentation and live specs SHALL use the umbrella concept Sync-Feedback
Sensor with Happy Hare's canonical type codes: P (Proportional, analog),
D (Dual, two-switch 3-state), TO (Tension-Only), CO (Compression-Only).
New acronyms (DSF/SFS) MUST NOT be minted, and wiring shorthand or "relay"
MUST NOT be used to denote the sensor in live prose.
- acceptance: Scenarios in spec satisfied
- scope: sync, state, model

## REQ-sync-state-model-buf-sensor-type-value-c
- source: openspec/specs/sync-state-model/spec.md
- description: Every live reference to `BUF_SENSOR_TYPE` SHALL document the value
contract as `D = 0` and `P = 1`. The integer values MUST remain unchanged
by this change.
- acceptance: Scenarios in spec satisfied
- scope: sync, state, model

## REQ-sync-state-model-to-and-co-documented-as
- source: openspec/specs/sync-state-model/spec.md
- description: Documentation SHALL list TO and CO as recognized Happy Hare Sync-Feedback
Sensor types that are not implemented in FLARE, so the taxonomy is
complete without implying FLARE support.
- acceptance: Scenarios in spec satisfied
- scope: sync, state, model

## REQ-task-workflow-load-context-first
- source: openspec/specs/task-workflow/spec.md
- description: Agents MUST read `AGENTS.md`, `openspec/README.md`, and relevant specs before starting work.
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-task-workflow-openspec-changes
- source: openspec/specs/task-workflow/spec.md
- description: Agents SHALL record findings and a file-level plan in `openspec/changes/<id>/` before implementation.
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-task-workflow-record-completion
- source: openspec/specs/task-workflow/spec.md
- description: The implementer SHALL update the change task list and target spec after durable work is complete.
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-task-workflow-no-root-task-md
- source: openspec/specs/task-workflow/spec.md
- description: Handoff and scratch notes SHALL belong in `openspec/changes/` while active.
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-task-workflow-small-commits
- source: openspec/specs/task-workflow/spec.md
- description: The agent MUST commit and push small, attributed units of work promptly.
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-task-workflow-no-local-ai-config-in-comm
- source: openspec/specs/task-workflow/spec.md
- description: The repository SHALL keep `.agents/`, `.claude/`, `.gemini/` etc. OUT of the commits.
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-task-workflow-ai-assisted-commit-attribu
- source: openspec/specs/task-workflow/spec.md
- description: Commits MUST retain the Claude `Co-Authored-By` trailer. When code in a
commit was generated or substantially assisted by another AI tool, the
commit MUST additionally carry a `Generated-By: <tool> (<model>)` trailer
— in addition to, not replacing, the Claude `Co-Authored-By` line. If
multiple tools contributed, each MUST appear on its own `Generated-By:`
line.
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-task-workflow-tasks-file-completion-hygi
- source: openspec/specs/task-workflow/spec.md
- description: Implementation MUST NOT empty, truncate, or delete the content of a
change's `tasks.md`. Completing work MUST mark the corresponding items
`[x]` and MAY append dated validation notes beneath them. Task history
MUST remain reconstructable from `tasks.md` at archive time.
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-task-workflow-doc-read-modes
- source: openspec/specs/task-workflow/spec.md
- description: The `AGENTS.md` Key Files table SHALL tag each entry with a read mode: `[always]` (read every session) or `[lookup]` (grep on demand, never wholesale). Agents MUST NOT read `[lookup]` docs wholesale; they grep the topic and read matched sections only. At minimum `MANUAL.md`, `BEHAVIOR.md`, `TEST_CASES.md`, `TUNING.md`, and `openspec/changes/archive/**` are `[lookup]`.
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-task-workflow-flow-triage
- source: openspec/specs/task-workflow/spec.md
- description: Agents SHALL route work between direct implementation and the OpenSpec flow using measurable criteria. Direct only when ALL hold: no spec'd-behavior change (`grep -ril '<topic>' openspec/specs/` empty, or hits but behavior unchanged); no `settings_t`, protocol command, or runtime-tunable surface change; at most 2–3 files touched; single session; no hardware validation needed. Any other case — or uncertainty — SHALL use the OpenSpec flow (misrouted direct work loses spec sync; misrouted OpenSpec work loses only tokens).
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-task-workflow-readiness-and-delivery-che
- source: openspec/specs/task-workflow/spec.md
- description: Generated `tasks.md` SHALL end with a final section named "Readiness and Delivery Checks" whose items gate archiving. The section MUST require: dev-tuning superset build passes (`ninja -C build_local` configured with `-DFLARE_DEV_TUNING=ON`) for firmware-touching changes; `python3 -m py_compile scripts/*.py` for script-touching changes; documentation sync verified for renamed/added parameters; `openspec validate <change-name> --strict` and `openspec validate --specs --strict` pass; and the team memory observation `memories/repo/<change-name>.md` appended.
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-task-workflow-self-contained-tasks
- source: openspec/specs/task-workflow/spec.md
- description: Every task in generated `tasks.md` SHALL name its target file path, the exact change, and specific acceptance criteria so it can be executed without re-reading proposal or design. Mechanical steps SHALL be expressed as CLI commands rather than manual-edit instructions.
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-task-workflow-hardware-task-tagging
- source: openspec/specs/task-workflow/spec.md
- description: Generated `tasks.md` SHALL prefix every hardware-dependent validation task with `HW:`. `HW:` tasks MUST NOT be checked off without explicit user confirmation backed by real-hardware test results.
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-task-workflow-compression-tiers
- source: openspec/specs/task-workflow/spec.md
- description: OpenSpec prose SHALL follow two compression tiers: `openspec/specs/**` stays lightly compressed or uncompressed (stable long-lived contracts, human readability paramount); `openspec/changes/**` artifact prose is fully compressed per `openspec/COMPRESSION.md` (iteration-heavy drafts). The tier split is forward-only: existing compressed specs are not rewritten for style alone.
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-task-workflow-pre-commit-self-review
- source: openspec/specs/task-workflow/spec.md
- description: Before committing non-trivial code changes, agents SHALL review the staged diff against the `REVIEW.md` checklist (settings versioning, protocol parity, config wiring, build superset, doc sync, regression impact) instead of re-reading full rule documents. Doc-only commits MAY skip the checklist.
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-task-workflow-targeted-output-edits
- source: openspec/specs/task-workflow/spec.md
- description: Agents SHALL report edits as targeted changes only: never echo unchanged code blocks into chat, commit messages, or PR descriptions; reference file paths and line ranges instead.
- acceptance: Scenarios in spec satisfied
- scope: task, workflow

## REQ-team-memory-store-team-memory-store-loca
- source: openspec/specs/team-memory-store/spec.md
- description: The project SHALL keep a git-tracked team memory store at `memories/repo/` with one observation file per archived change, named `<change-name>.md` matching the `openspec/changes/` directory name. Each file MUST contain 3–5 compressed lines covering decisions made, gotchas hit, and deviations from design with their reasons, MUST name affected specs/components explicitly so files stay greppable, and MUST NOT contain secrets, tokens, credentialed URLs, or firmware/source code snippets.
- acceptance: Scenarios in spec satisfied
- scope: team, memory, store

## REQ-team-memory-store-read-protocol-before-r
- source: openspec/specs/team-memory-store/spec.md
- description: Agents SHALL search the team memory store before re-deriving prior art: before drafting a proposal or design that touches existing specs or components, run `grep -ril '<topic>' memories/repo/` and cite relevant hits in the artifact instead of re-investigating from source or git history. Recalled observations MUST be verified against the current tree (named files, parameters, behaviors may have changed since written).
- acceptance: Scenarios in spec satisfied
- scope: team, memory, store

## REQ-team-memory-store-tool-agnostic-store-fo
- source: openspec/specs/team-memory-store/spec.md
- description: The memory store SHALL be plain markdown readable by any agent tool via file read and `grep`. Files MUST NOT depend on tool-specific frontmatter, skills, plugins, or MCP servers to be consumed. `memories/repo/README.md` SHALL document the write rules and read protocol.
- acceptance: Scenarios in spec satisfied
- scope: team, memory, store

## REQ-team-memory-store-personal-memory-layers
- source: openspec/specs/team-memory-store/spec.md
- description: Personal memory layers (cavemem, Claude auto-memory) SHALL remain personal and uncommitted. Agents MUST NOT commit personal memory content wholesale into `memories/repo/`; only curated per-change observations belong in the team store.
- acceptance: Scenarios in spec satisfied
- scope: team, memory, store

## REQ-toolchange-orchestration-full-automated-
- source: openspec/specs/toolchange-orchestration/spec.md
- description: The system SHALL orchestrate an automated sequence to swap active lanes without host intervention.
- acceptance: Scenarios in spec satisfied
- scope: toolchange, orchestration

## REQ-toolchange-orchestration-manual-cutter-e
- source: openspec/specs/toolchange-orchestration/spec.md
- description: The host SHALL be able to trigger the exact cutter sequence independently of a full toolchange.
- acceptance: Scenarios in spec satisfied
- scope: toolchange, orchestration

## REQ-toolchange-orchestration-manual-unload-s
- source: openspec/specs/toolchange-orchestration/spec.md
- description: Manual MMU unload SHALL accept `UM`, `UM:`, `UM:1`, and `UM:2`. `UM` and
`UM:` SHALL preserve active-lane behavior. `UM:n` SHALL target the explicit
lane without changing `active_lane`.
- acceptance: Scenarios in spec satisfied
- scope: toolchange, orchestration

## REQ-toolchange-orchestration-reload-buffer-d
- source: openspec/specs/toolchange-orchestration/spec.md
- description: During runout RELOAD, the new lane SHALL approach until physical buffer contact is detected.
- acceptance: Scenarios in spec satisfied
- scope: toolchange, orchestration

## REQ-toolchange-orchestration-reload-bang-ban
- source: openspec/specs/toolchange-orchestration/spec.md
- description: During the RELOAD follow phase, the new lane SHALL over-feed to close the gap and maintain pressure on the old tail.
- acceptance: Scenarios in spec satisfied
- scope: toolchange, orchestration

## REQ-type-d-dynamic-flow-a-tension-touch-slam
- source: openspec/specs/type-d-dynamic-flow/spec.md
- description: For `BUF_SENSOR_TYPE == 0`, a crossing into `BUF_TENSION` SHALL set a recovery
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
- acceptance: Scenarios in spec satisfied
- scope: type, d, dynamic, flow

## REQ-type-d-dynamic-flow-slow-drift-protectio
- source: openspec/specs/type-d-dynamic-flow/spec.md
- description: For `BUF_SENSOR_TYPE == 0`, slow-print anti-tension protection SHALL be provided
by the compression-side reserve bias (`SYNC_RESERVE_PCT`), which parks the buffer
off the TENSION rail, NOT by a high `SYNC_MIN_RATE` feed floor. The shipped
default `SYNC_MIN_RATE` SHALL be a quiet (low) value so real prints are not forced
into constant COMPRESSION clicking. `SYNC_MIN_RATE` SHALL remain operator-tunable
for those who prefer the loud zero-fast-step-skip behavior, and TUNING.md SHALL
document the trade-off.
- acceptance: Scenarios in spec satisfied
- scope: type, d, dynamic, flow

