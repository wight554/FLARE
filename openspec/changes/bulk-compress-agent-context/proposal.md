## Why

OpenSpec specs and AI-facing docs still carry much verbose prose. Compression infra now exists; next step is reviewed bulk compression to cut agent context load without changing firmware or operator docs.

## What Changes

- Compress active `openspec/specs/*/spec.md` requirement/body prose per `openspec/COMPRESSION.md`.
- Preserve uncompressed `## Purpose` sections, headings, tables, code blocks, inline code, paths, commands, and all RFC-2119 normative clauses exactly.
- Compress AI-facing repo context files: `AGENTS.md`, `AI.md`, `CONTEXT.md`, `CLAUDE.md`, `GEMINI.md`, and `WORKFLOW.md`.
- Leave operator/user docs normal prose: `README.md`, `BUILD_FLASH.md`, `HARDWARE.md`, `KLIPPER.md`, `TUNING.md`, `MANUAL.md`, `BEHAVIOR.md`, `TEST_CASES.md`, and `MOTOR_PARAMS.md`.
- Ratchet `scripts/test_spec_compression.py` threshold after measured density proves compressed specs pass.

## Capabilities

### New Capabilities
- `agent-context-compression`: Reviewed compression workflow for AI-facing context files and existing OpenSpec specs.

### Modified Capabilities
<!-- none -->

## Impact

- Affects markdown only plus `scripts/test_spec_compression.py`.
- No firmware, protocol, config, persistence, Klipper integration, or runtime behavior change.
- Validation: OpenSpec strict validation, compression tripwire, Python compile, regression gate if script changes, and human diff review of representative compressed specs.
