## Context

`openspec/COMPRESSION.md` now defines safe markdown compression. Active specs still contain mostly uncompressed requirement prose, and AI-facing docs carry repeated onboarding/reference text that every agent reads.

## Goals / Non-Goals

**Goals:**
- Reduce token load for agent startup and OpenSpec apply flows.
- Compress only AI-facing/context prose and OpenSpec requirement bodies.
- Preserve human Purpose blocks, normative clauses, code, commands, paths, links, and structure.
- Measure density before/after and ratchet tripwire only after compressed files pass.

**Non-Goals:**
- No firmware, Klipper, config, protocol, or runtime behavior change.
- No operator/user doc compression.
- No mechanical regex rewrite of normative clauses.
- No archive of unrelated completed OpenSpec changes in this pass.

## Decisions

### D1 - Compress reviewed prose only

Use manual/LLM-assisted markdown editing under `openspec/COMPRESSION.md`; do not run auto-rewrite scripts. Reason: contract meaning matters more than maximum byte savings.

### D2 - Split scope by audience

Compress `openspec/specs/*/spec.md` requirement/body prose and AI-facing docs (`AGENTS.md`, `AI.md`, `CONTEXT.md`, `CLAUDE.md`, `GEMINI.md`, `WORKFLOW.md`). Leave operator docs normal prose so printer users get readable instructions.

### D3 - Preserve Purpose sections

Each spec's `## Purpose` stays uncompressed. The body below may be compressed, but Purpose remains readable human orientation.

### D4 - Ratchet after measurement

Measure filler density before and after compression. Lower `MAX_FILLER_DENSITY_PCT` only to a value current compressed specs pass with margin.

## Risks / Trade-offs

- Normative meaning drift -> preserve RFC-2119 clauses and scenario structure exactly; review representative diffs.
- Human readability loss -> leave Purpose sections and operator docs uncompressed.
- Threshold false failures -> ratchet only after measured compressed density.
- AI instruction ambiguity -> keep critical order/safety instructions clear even when compressed.
