# Synthesized Constraints

## Acceptance Gate Parity Specification
- source: openspec/specs/acceptance-gate-parity/spec.md
- type: nfr
- content: Gate parity and mature-run consistency behavioral contracts and requirements.

## Acceptance Gate Semantics Specification
- source: openspec/specs/acceptance-gate-semantics/spec.md
- type: nfr
- content: Capture the OpenSpec-native contract for analyzer acceptance-gate

## agent-comms-mode Specification
- source: openspec/specs/agent-comms-mode/spec.md
- type: nfr
- content: Tool-agnostic caveman-full chat-response default for agents, sourced from `openspec/COMMS.md`, with explicit human-readable exclusions (commits/PRs, code, user docs, security/irreversible prose).

## agent-context-compression Specification
- source: openspec/specs/agent-context-compression/spec.md
- type: nfr
- content: Reviewed bulk-compression workflow for AI-facing context files and existing OpenSpec spec bodies, preserving normative clauses/structure and excluding operator/user documentation.

## Analyzer Rigor Specification
- source: openspec/specs/analyzer-rigor/spec.md
- type: nfr
- content: Analyzer requirements for weighting, safe mode, and contributors behavioral contracts.

## Bucket Locking Specification
- source: openspec/specs/bucket-locking/spec.md
- type: nfr
- content: Capture the OpenSpec-native contract for smarter bucket lock/unlock

## buffer-geometry-vocabulary Specification
- source: openspec/specs/buffer-geometry-vocabulary/spec.md
- type: nfr
- content: Defines shared buffer geometry terms so firmware, docs, and tuning guides use the same distance and switch-span vocabulary.

## buffer-state-lock Specification
- source: openspec/specs/buffer-state-lock/spec.md
- type: nfr
- content: Describes host-controlled buffer lock behavior used to drive type-D buffers to tension or compression for setup, service, and testing.

## Calibration Workflow Specification
- source: openspec/specs/calibration-workflow/spec.md
- type: nfr
- content: Capture the OpenSpec-native contract for observe-only calibration.

## code-style-standard Specification
- source: openspec/specs/code-style-standard/spec.md
- type: nfr
- content: Enforced C style + lint contract for FLARE firmware so a human maintainer can read and

## config-surface-tiers Specification
- source: openspec/specs/config-surface-tiers/spec.md
- type: nfr
- content: Classifies configuration knobs by audience and storage surface so operator tunables, developer constants, and board constants stay separated.

## cross-platform-script-tooling Specification
- source: openspec/specs/cross-platform-script-tooling/spec.md
- type: nfr
- content: Cross-platform Python host script contract — all operational scripts in `scripts/` SHALL be Python, working on Linux (Raspberry Pi, Debian/Ubuntu) and macOS without modification.

## cutter-feed-timeout Specification
- source: openspec/specs/cutter-feed-timeout/spec.md
- type: nfr
- content: Documents the cutter feed timeout tunable and its serial/config exposure for safe long cutter-feed moves.

## daemon-klipper-mirror Specification
- source: openspec/specs/daemon-klipper-mirror/spec.md
- type: nfr
- content: Contract for the `flare_daemon.py` -> Klipper `SET_MMU` mirror push: delta pushes of

## Deterministic Tuning Workflow Specification
- source: openspec/specs/deterministic-tuning-workflow/spec.md
- type: nfr
- content: Durable contract for the deterministic, reproducible tuning workflow that

## filament-bypass Specification
- source: openspec/specs/filament-bypass/spec.md
- type: nfr
- content: Defines the host and UI behavior for selecting filament bypass instead of a numbered MMU lane.

## Flow-Keyed Schedule
- source: openspec/specs/flow-keyed-schedule/spec.md
- type: nfr
- content: Defines how live estimated flow maps to sync baseline and compression bias.

## Klipper Integration Specification
- source: openspec/specs/klipper-integration/spec.md
- type: nfr
- content: Durable contract for FLARE Klipper integration (`flare_cmd.py`), extracted from `KLIPPER.md` and script sources.

## klipper-mmu-config Specification
- source: openspec/specs/klipper-mmu-config/spec.md
- type: nfr
- content: Specifies the bundled Klipper MMU config surface that lets users include one file for FLARE macros and UI state.

## Klipper Motion Tracking Specification
- source: openspec/specs/klipper-motion-tracking/spec.md
- type: nfr
- content: Klipper sidecar and UDS motion tracking behavioral contracts and requirements.

## Live Tuner Specification
- source: openspec/specs/live-tuner/spec.md
- type: nfr
- content: Capture the OpenSpec-native contract for the calibration tuner and

## marker-capture-policy Specification
- source: openspec/specs/marker-capture-policy/spec.md
- type: nfr
- content: Records the decision to use slicer sidecar metadata as the single live capture path and retire older marker-file flows.

## Motion Safety Specification
- source: openspec/specs/motion-safety/spec.md
- type: nfr
- content: Durable contract for FLARE motor, filament, and task safety limits, extracted from `firmware/src/motion.c` and `BEHAVIOR.md`.

## operator-tuning-guide Specification
- source: openspec/specs/operator-tuning-guide/spec.md
- type: nfr
- content: Defines what the human tuning guide must explain, which workflows it must support, and which internal details it must avoid.

## Persistence Contract Specification
- source: openspec/specs/persistence-contract/spec.md
- type: nfr
- content: Durable contract for FLARE flash-backed runtime parameters and config generation, extracted from `firmware/src/settings_store.c` and `AGENTS.md`.

## Project Architecture Specification
- source: openspec/specs/project-architecture/spec.md
- type: nfr
- content: Captures durable firmware architecture and workflow contracts for contributors.

## psf-type-p-sensor Specification
- source: openspec/specs/psf-type-p-sensor/spec.md
- type: nfr
- content: Captures behavior for the proportional Sync-Feedback Sensor path, including recovery, refill, and soft-wall control decisions.

## python-host-tooling-style Specification
- source: openspec/specs/python-host-tooling-style/spec.md
- type: nfr
- content: Enforced ruff lint contract + clean baseline for the `scripts/` Python host tooling,

## relay-fallback-only Specification
- source: openspec/specs/relay-fallback-only/spec.md
- type: nfr
- content: Keeps type-D relay control on the simple fallback law after the confidence estimator path was removed.

## reserve-safety-floor Specification
- source: openspec/specs/reserve-safety-floor/spec.md
- type: nfr
- content: Protects the configured reserve bias floor when flow schedules adjust sync feed targets.

## script-path-handling Specification
- source: openspec/specs/script-path-handling/spec.md
- type: nfr
- content: Defines path expansion behavior for host tools so file inputs, globs, and output paths resolve predictably.

## spec-compression-workflow Specification
- source: openspec/specs/spec-compression-workflow/spec.md
- type: nfr
- content: Tool-agnostic semantic compression convention for OpenSpec artifacts — the in-repo `openspec/COMPRESSION.md` ruleset, the cross-UI authoring directive, and the density tripwire that gates uncompressed specs in regression.

## spec-readability Specification
- source: openspec/specs/spec-readability/spec.md
- type: nfr
- content: Human-oriented navigation of the agent-facing specs — a per-spec uncompressed `

## static-regression-validation Specification
- source: openspec/specs/static-regression-validation/spec.md
- type: nfr
- content: Automated host-side Python unit testing and regression gating — discovers and runs all `scripts/test_*.py`, enforcing the static validation gate before commits.

## sync-feedback Specification
- source: openspec/specs/sync-feedback/spec.md
- type: nfr
- content: Defines shared Sync-Feedback Sensor behavior across type-D switch buffers and type-P proportional buffers.

## Sync Refactor Foundation Specification
- source: openspec/specs/sync-refactor-foundation/spec.md
- type: nfr
- content: Capture the OpenSpec-native contract for the original sync refactor foundation.

## Sync Refactor Specification
- source: openspec/specs/sync-refactor/spec.md
- type: nfr
- content: Durable contract for FLARE sync, tuning, tracking, and analyzer behavioral requirements and historical rationale.

## Sync State Model Specification
- source: openspec/specs/sync-state-model/spec.md
- type: nfr
- content: Defines the explicit sync controller lifecycle state machine and its

## Task Workflow Specification
- source: openspec/specs/task-workflow/spec.md
- type: nfr
- content: Workflow contract (supersedes AGENTS.md and former TASK.md) behavioral requirements.

## team-memory-store Specification
- source: openspec/specs/team-memory-store/spec.md
- type: nfr
- content: Define the git-tracked team memory store at `memories/repo/`: a tool-agnostic prior-art layer of curated per-change observations (decisions, gotchas, deviations) that agents read before proposing changes and write before archiving, complementing...

## Toolchange Orchestration Specification
- source: openspec/specs/toolchange-orchestration/spec.md
- type: nfr
- content: Durable contract for FLARE toolchange (TC) and RELOAD orchestration, defining phase boundaries, timeouts, and state expectations.

## type-d-dynamic-flow Specification
- source: openspec/specs/type-d-dynamic-flow/spec.md
- type: nfr
- content: Captures dynamic type-D relay feed behavior that helps the two-switch buffer recover demand without a mid-band position signal.

