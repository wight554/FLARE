# Requirements: FLARE

## Overview

Durable requirements extracted from the 42 OpenSpec technical contracts.

## v1 Requirements

### REQ-acceptance-gate-parity-shared-recommenda
- **Source**: `openspec/specs/acceptance-gate-parity/spec.md`
- **Description**: The gate SHALL compare per-run recommendations from the same state-aware path as the patch.

### REQ-acceptance-gate-parity-backward-compatib
- **Source**: `openspec/specs/acceptance-gate-parity/spec.md`
- **Description**: `compute_recommendations` SHALL retain dictionary shape and semantics for existing callers.

### REQ-acceptance-gate-parity-run-classificatio
- **Source**: `openspec/specs/acceptance-gate-parity/spec.md`
- **Description**: The system SHALL classify runs (comparable or skipped) before checking consistency deltas.

### REQ-acceptance-gate-parity-diagnostic-visibi
- **Source**: `openspec/specs/acceptance-gate-parity/spec.md`
- **Description**: The generated patch MUST include per-run estimates regardless of gate outcome.

### REQ-acceptance-gate-parity-contributor-mass-
- **Source**: `openspec/specs/acceptance-gate-parity/spec.md`
- **Description**: The acceptance gate SHALL FAIL only on contributor mass, and WARN on raw row coverage.

### REQ-acceptance-gate-parity-placeholder-telem
- **Source**: `openspec/specs/acceptance-gate-parity/spec.md`
- **Description**: The analyzer MUST mark telemetry counters as pending until real log parsing exists.

### REQ-acceptance-gate-semantics-separate-rejec
- **Source**: `openspec/specs/acceptance-gate-semantics/spec.md`
- **Description**: The gate SHALL FAIL ONLY on reliability issues; stale config and incomplete soak are warnings.

### REQ-acceptance-gate-semantics-floored-denomi
- **Source**: `openspec/specs/acceptance-gate-semantics/spec.md`
- **Description**: The system SHALL avoid penalizing the operator for many immature buckets in mass calculation.

### REQ-acceptance-gate-semantics-mass-gray-band
- **Source**: `openspec/specs/acceptance-gate-semantics/spec.md`
- **Description**: The gate SHALL issue a WARNING when mass is between PASS and FAIL thresholds.

### REQ-acceptance-gate-semantics-sigma-ceiling
- **Source**: `openspec/specs/acceptance-gate-semantics/spec.md`
- **Description**: The analyzer SHALL warn and recommend correction when BP sigma is between reference and 5.0 mm.

### REQ-acceptance-gate-semantics-soak-maturity
- **Source**: `openspec/specs/acceptance-gate-semantics/spec.md`
- **Description**: The system MUST report run-count and duration without hiding stable recommendations.

### REQ-acceptance-gate-semantics-glob-input
- **Source**: `openspec/specs/acceptance-gate-semantics/spec.md`
- **Description**: The analyzer SHALL support shell-expanded CSV groups using the `--in` flag.

### REQ-agent-comms-mode-portable-caveman-comms-
- **Source**: `openspec/specs/agent-comms-mode/spec.md`
- **Description**: The project SHALL maintain an in-repo file `openspec/COMMS.md` that fully defines the caveman-full chat-response style, such that any agent UI can adopt it by reading the file alone, with no dependency on a Claude-specific skill or plugin.

### REQ-agent-comms-mode-caveman-full-is-the-def
- **Source**: `openspec/specs/agent-comms-mode/spec.md`
- **Description**: The repository SHALL direct every agent, via `AGENTS.md`, to respond in caveman-full style by default, and the directive SHALL be phrased tool-agnostically rather than as an instruction to activate a Claude-specific skill.

### REQ-agent-comms-mode-human-readable-exclusio
- **Source**: `openspec/specs/agent-comms-mode/spec.md`
- **Description**: The caveman comms default SHALL NOT apply to human-readable surfaces. The ruleset SHALL exclude commit messages, pull-request descriptions, source code and code comments, user-facing documentation (including `README`, onboarding, and operator guides), and security warnings or irreversible-action confirmations, which SHALL remain normal prose.

### REQ-agent-context-compression-reviewed-bulk-
- **Source**: `openspec/specs/agent-context-compression/spec.md`
- **Description**: The project SHALL allow reviewed compression of active OpenSpec spec bodies and AI-facing repository context files, while excluding operator/user documentation from compression.

### REQ-agent-context-compression-contract-and-s
- **Source**: `openspec/specs/agent-context-compression/spec.md`
- **Description**: Bulk compression SHALL preserve RFC-2119 normative clauses, `

### REQ-agent-context-compression-headings-scena
- **Source**: `openspec/specs/agent-context-compression/spec.md`
- **Description**: #### Scenario: Normative spec compressed safely

### REQ-agent-context-compression-purpose-sectio
- **Source**: `openspec/specs/agent-context-compression/spec.md`
- **Description**: Every active spec's `## Purpose` section SHALL remain uncompressed human-readable prose and SHALL remain exempt from filler-density scoring.

### REQ-agent-context-compression-compression-th
- **Source**: `openspec/specs/agent-context-compression/spec.md`
- **Description**: The compression tripwire SHALL be ratcheted only after measuring compressed spec density and choosing a threshold that passes the reviewed compressed corpus.

### REQ-analyzer-rigor-relative-noise-gate
- **Source**: `openspec/specs/analyzer-rigor/spec.md`
- **Description**: Bucket lock acceptability SHALL be derived from `sigma / rate` after warmup.

### REQ-analyzer-rigor-safe-mode-enforcement
- **Source**: `openspec/specs/analyzer-rigor/spec.md`
- **Description**: Safe mode MUST refuse recommendations if zero buckets are LOCKED in the state.

### REQ-analyzer-rigor-explicit-bootstrap-paths
- **Source**: `openspec/specs/analyzer-rigor/spec.md`
- **Description**: Aggressive and force modes SHALL allow pre-lock estimates with explicit warnings.

### REQ-analyzer-rigor-precision-weighted-recomm
- **Source**: `openspec/specs/analyzer-rigor/spec.md`
- **Description**: Recommendations SHALL use precision-weighted qualifying set (N / Var) with trimmed tails.

### REQ-analyzer-rigor-bp-derived-sigma
- **Source**: `openspec/specs/analyzer-rigor/spec.md`
- **Description**: The analyzer SHALL derive `buf_variance_blend_ref_mm` from BP samples, NOT BL field.

### REQ-analyzer-rigor-contributors-visibility
- **Source**: `openspec/specs/analyzer-rigor/spec.md`
- **Description**: The generated patch MUST include a contributor evidence block for learned values.

### REQ-bucket-locking-bucket-schema-4-shall-pre
- **Source**: `openspec/specs/bucket-locking/spec.md`
- **Description**: Bucket state schema 4 SHALL include scalar residual-statistics fields used by

### REQ-bucket-locking-schema-3-state-shall-migr
- **Source**: `openspec/specs/bucket-locking/spec.md`
- **Description**: The schema 3 to 4 migration SHALL preserve all existing bucket and `_meta`

### REQ-bucket-locking-unlocking-shall-use-three
- **Source**: `openspec/specs/bucket-locking/spec.md`
- **Description**: LOCKED buckets SHALL unlock only through the catastrophic, streak, or drift

### REQ-bucket-locking-locking-shall-be-noise-ga
- **Source**: `openspec/specs/bucket-locking/spec.md`
- **Description**: A bucket SHALL NOT enter or re-enter LOCKED state until required sample evidence,

### REQ-bucket-locking-verbose-state-info-shall-
- **Source**: `openspec/specs/bucket-locking/spec.md`
- **Description**: Verbose tuner state output SHALL include residual and unlock diagnostics needed

### REQ-buffer-geometry-vocabulary-buffer-geomet
- **Source**: `openspec/specs/buffer-geometry-vocabulary/spec.md`
- **Description**: Buffer geometry SHALL be configured through exactly two full-range tunables

### REQ-buffer-geometry-vocabulary-full-range-to
- **Source**: `openspec/specs/buffer-geometry-vocabulary/spec.md`
- **Description**: The firmware SHALL convert the full-range `buf_switch_span_mm` to the

### REQ-buffer-geometry-vocabulary-emu-sync-defa
- **Source**: `openspec/specs/buffer-geometry-vocabulary/spec.md`
- **Description**: The compiled defaults SHALL be the EMU Sync reference values:

### REQ-buffer-geometry-vocabulary-switch-span-a
- **Source**: `openspec/specs/buffer-geometry-vocabulary/spec.md`
- **Description**: The two tunables SHALL preserve, in full-range terms, the clamp relationship

### REQ-buffer-geometry-vocabulary-serial-vocabu
- **Source**: `openspec/specs/buffer-geometry-vocabulary/spec.md`
- **Description**: The serial SET/GET tokens SHALL be `BUF_SWITCH_SPAN` and `BUF_MAX_TRAVEL`,

### REQ-buffer-geometry-vocabulary-type-p-analog
- **Source**: `openspec/specs/buffer-geometry-vocabulary/spec.md`
- **Description**: The full→half ingest change SHALL NOT alter type-P (analog,

### REQ-buffer-state-lock-bl-command-surface
- **Source**: `openspec/specs/buffer-state-lock/spec.md`
- **Description**: The firmware SHALL accept a host `BL:<state>` command that arms the active

### REQ-buffer-state-lock-bounded-half-travel-pr
- **Source**: `openspec/specs/buffer-state-lock/spec.md`
- **Description**: On `BL` the firmware SHALL drive the active lane toward the requested extreme

### REQ-buffer-state-lock-locked-hold-contract
- **Source**: `openspec/specs/buffer-state-lock/spec.md`
- **Description**: While locked the firmware SHALL energize the active lane motor with zero

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
| REQ-agent-comms-mode-portable-caveman-comms- | Phase 1 | Pending |
| REQ-agent-comms-mode-caveman-full-is-the-def | Phase 2 | Pending |
| REQ-agent-comms-mode-human-readable-exclusio | Phase 3 | Pending |
| REQ-agent-context-compression-reviewed-bulk- | Phase 4 | Pending |
| REQ-agent-context-compression-contract-and-s | Phase 5 | Pending |
| REQ-agent-context-compression-headings-scena | Phase 6 | Pending |
| REQ-agent-context-compression-purpose-sectio | Phase 1 | Pending |
| REQ-agent-context-compression-compression-th | Phase 2 | Pending |
| REQ-analyzer-rigor-relative-noise-gate | Phase 3 | Pending |
| REQ-analyzer-rigor-safe-mode-enforcement | Phase 4 | Pending |
| REQ-analyzer-rigor-explicit-bootstrap-paths | Phase 5 | Pending |
| REQ-analyzer-rigor-precision-weighted-recomm | Phase 6 | Pending |
| REQ-analyzer-rigor-bp-derived-sigma | Phase 1 | Pending |
| REQ-analyzer-rigor-contributors-visibility | Phase 2 | Pending |
| REQ-bucket-locking-bucket-schema-4-shall-pre | Phase 3 | Pending |
| REQ-bucket-locking-schema-3-state-shall-migr | Phase 4 | Pending |
| REQ-bucket-locking-unlocking-shall-use-three | Phase 5 | Pending |
| REQ-bucket-locking-locking-shall-be-noise-ga | Phase 6 | Pending |
| REQ-bucket-locking-verbose-state-info-shall- | Phase 1 | Pending |
| REQ-buffer-geometry-vocabulary-buffer-geomet | Phase 2 | Pending |
| REQ-buffer-geometry-vocabulary-full-range-to | Phase 3 | Pending |
| REQ-buffer-geometry-vocabulary-emu-sync-defa | Phase 4 | Pending |
| REQ-buffer-geometry-vocabulary-switch-span-a | Phase 5 | Pending |
| REQ-buffer-geometry-vocabulary-serial-vocabu | Phase 6 | Pending |
| REQ-buffer-geometry-vocabulary-type-p-analog | Phase 1 | Pending |
| REQ-buffer-state-lock-bl-command-surface | Phase 2 | Pending |
| REQ-buffer-state-lock-bounded-half-travel-pr | Phase 3 | Pending |
| REQ-buffer-state-lock-locked-hold-contract | Phase 4 | Pending |
