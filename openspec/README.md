# FLARE OpenSpec

This directory is the project-owned place for current specs and future
spec-driven changes.

Tool-specific OpenSpec/OpsX skills live in global AI config directories; do not
commit `.claude/`, `.codex/`, `.gemini/`, `.agent/`, or `.github/skills` here.

## Current Layout

| Path | Purpose |
|---|---|
| `config.yaml` | Project context used by OpenSpec-aware agents. |
| `specs/` | Current and historical-phase behavioral contracts agents should read before changing an area. |

## Spec Index

| Spec | Human summary | Paired human doc |
|---|---|---|
| [`acceptance-gate-parity`](specs/acceptance-gate-parity/spec.md) | Analyzer gate parity and mature-run consistency checks. | [`TUNING.md`](../TUNING.md) |
| [`acceptance-gate-semantics`](specs/acceptance-gate-semantics/spec.md) | Analyzer acceptance-gate failure and warning semantics. | [`TUNING.md`](../TUNING.md) |
| [`analyzer-rigor`](specs/analyzer-rigor/spec.md) | Analyzer weighting, safe mode, and contributor reporting. | [`TUNING.md`](../TUNING.md) |
| [`bucket-locking`](specs/bucket-locking/spec.md) | Calibration bucket lock/unlock behavior and residual statistics. | [`TUNING.md`](../TUNING.md) |
| [`buffer-geometry-vocabulary`](specs/buffer-geometry-vocabulary/spec.md) | Buffer distance and switch-span vocabulary. | [`TUNING.md`](../TUNING.md), [`HARDWARE.md`](../HARDWARE.md) |
| [`buffer-state-lock`](specs/buffer-state-lock/spec.md) | Host-controlled buffer lock behavior for service/testing. | none |
| [`calibration-workflow`](specs/calibration-workflow/spec.md) | Observe-only calibration workflow and review patches. | [`TUNING.md`](../TUNING.md) |
| [`config-surface-tiers`](specs/config-surface-tiers/spec.md) | Operator/developer/board configuration surface tiers. | [`TUNING.md`](../TUNING.md), [`MANUAL.md`](../MANUAL.md) |
| [`cutter-feed-timeout`](specs/cutter-feed-timeout/spec.md) | Cutter feed timeout tunable and safety bounds. | [`MANUAL.md`](../MANUAL.md) |
| [`deterministic-tuning-workflow`](specs/deterministic-tuning-workflow/spec.md) | Reproducible two-profile baseline tuning workflow. | [`TUNING.md`](../TUNING.md) |
| [`filament-bypass`](specs/filament-bypass/spec.md) | Filament bypass selection and host/UI status behavior. | [`KLIPPER.md`](../KLIPPER.md) |
| [`flow-keyed-schedule`](specs/flow-keyed-schedule/spec.md) | Flow-keyed sync schedule format and interpolation behavior. | [`TUNING.md`](../TUNING.md), [`MANUAL.md`](../MANUAL.md) |
| [`klipper-integration`](specs/klipper-integration/spec.md) | Serial helper, macros, and Klipper integration scope. | [`KLIPPER.md`](../KLIPPER.md) |
| [`klipper-mmu-config`](specs/klipper-mmu-config/spec.md) | Single-file Klipper MMU config and UI state. | [`KLIPPER.md`](../KLIPPER.md) |
| [`klipper-motion-tracking`](specs/klipper-motion-tracking/spec.md) | Sidecar and UDS motion tracking for tuning telemetry. | [`TUNING.md`](../TUNING.md), [`KLIPPER.md`](../KLIPPER.md) |
| [`live-tuner`](specs/live-tuner/spec.md) | Live tuner bucket learning, guards, and chatter resistance. | [`TUNING.md`](../TUNING.md) |
| [`marker-capture-policy`](specs/marker-capture-policy/spec.md) | Sidecar-only capture policy and retired marker flows. | [`TUNING.md`](../TUNING.md) |
| [`motion-safety`](specs/motion-safety/spec.md) | Motor, filament, and task safety limits. | [`BEHAVIOR.md`](../BEHAVIOR.md), [`MANUAL.md`](../MANUAL.md) |
| [`operator-tuning-guide`](specs/operator-tuning-guide/spec.md) | Required content and wording for the human tuning guide. | [`TUNING.md`](../TUNING.md) |
| [`persistence-contract`](specs/persistence-contract/spec.md) | Runtime settings persistence and config generation contract. | [`MANUAL.md`](../MANUAL.md), [`CONTEXT.md`](../CONTEXT.md) |
| [`project-architecture`](specs/project-architecture/spec.md) | Firmware architecture and workflow contracts for contributors. | [`CONTEXT.md`](../CONTEXT.md), [`BEHAVIOR.md`](../BEHAVIOR.md) |
| [`psf-type-p-sensor`](specs/psf-type-p-sensor/spec.md) | Type-P proportional buffer control and recovery behavior. | [`TUNING.md`](../TUNING.md), [`HARDWARE.md`](../HARDWARE.md) |
| [`relay-fallback-only`](specs/relay-fallback-only/spec.md) | Type-D relay fallback law after estimator removal. | [`TUNING.md`](../TUNING.md) |
| [`reserve-safety-floor`](specs/reserve-safety-floor/spec.md) | Reserve bias floor protection for sync feed targets. | [`TUNING.md`](../TUNING.md) |
| [`script-path-handling`](specs/script-path-handling/spec.md) | Host tool path, glob, and output resolution behavior. | none |
| [`sync-feedback`](specs/sync-feedback/spec.md) | Shared behavior for type-D and type-P Sync-Feedback Sensors. | [`TUNING.md`](../TUNING.md), [`HARDWARE.md`](../HARDWARE.md) |
| [`sync-refactor-foundation`](specs/sync-refactor-foundation/spec.md) | Original standalone sync refactor foundation contract. | [`BEHAVIOR.md`](../BEHAVIOR.md), [`TUNING.md`](../TUNING.md) |
| [`sync-refactor`](specs/sync-refactor/spec.md) | Core sync, tuning, tracking, and analyzer behavior. | [`BEHAVIOR.md`](../BEHAVIOR.md), [`TUNING.md`](../TUNING.md) |
| [`sync-state-model`](specs/sync-state-model/spec.md) | Explicit sync lifecycle states and relief/fault behavior. | [`BEHAVIOR.md`](../BEHAVIOR.md), [`MANUAL.md`](../MANUAL.md) |
| [`task-workflow`](specs/task-workflow/spec.md) | Agent workflow, task tracking, and commit hygiene. | [`AGENTS.md`](../AGENTS.md) |
| [`toolchange-orchestration`](specs/toolchange-orchestration/spec.md) | Toolchange and RELOAD phase orchestration. | [`BEHAVIOR.md`](../BEHAVIOR.md), [`MANUAL.md`](../MANUAL.md) |
| [`type-d-dynamic-flow`](specs/type-d-dynamic-flow/spec.md) | Dynamic type-D relay feed behavior for two-switch buffers. | [`TUNING.md`](../TUNING.md) |

## Proposed Working Model

- Put active change proposals in `openspec/changes/<change-id>/`.
- Put current expected behavior in `openspec/specs/<area>/spec.md`.
- Prefer OpenSpec-native specs over raw phase notes for agent startup context.
- Do not recreate repo-root `TASK.md`; keep substantial active work in
  `openspec/changes/`.
- Do not keep migrated historical phase/task archives in-tree; use git history
  for old prose and implementation prompts.
- When a change ships, update the relevant spec and keep root docs focused on
  operator-facing usage.
