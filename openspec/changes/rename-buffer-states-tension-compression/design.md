## Context

Five rounds of sync debugging repeatedly tripped on `ADVANCE`/`TRAILING`
meaning the opposite of intuition; one became a real polarity bug
(`8f54bff`). Happy Hare uses tension/compression. User decisions: (1)
`MID → NEUTRAL`; (2) no back-compat, zero old references anywhere; (3)
both relay (2-endstop) and PSF (analog) stay supported — no code deleted;
(4) archived OpenSpec changes left historical unless live-referenced
(checked: only live specs reference the terms).

## Goals / Non-Goals

**Goals:** names state the physics; one canonical contract obeyed
everywhere; rename is byte-identical in behavior; diff reviewable as
"faithful rename".

**Non-Goals:** no control-logic change (that is `audit-sync-polarity`);
no back-compat/alias; no deletion of the analog/PI path.

## Decisions

### D1 — Canonical contract (frozen)

```
 BUF_ADVANCE  → BUF_TENSION      tensioned · EMPTY · printer>MMU · BP>0 · FEED
 BUF_TRAILING → BUF_COMPRESSION  compressed · FULL · MMU>printer · BP<0 · BACK OFF
 BUF_MID      → BUF_NEUTRAL      neutral band · BP≈0
 BUF_FAULT    → BUF_FAULT        unchanged
```
Sign conventions (`BP`, `RT`, `RE`, `BPV`) are **unchanged** — labels only.
Mnemonic: TENSION = empty = feed; COMPRESSION = full = stop.

### D2 — Two-change methodology (hard split)

This change = rename only, behavior byte-identical. Polarity inversions the
new names expose are fixed in `audit-sync-polarity` (depends on this).
Rationale: mixing rename churn with logic changes makes the diff
unreviewable; reviewer of this change asks only "is the rename faithful".

### D3 — Surface classes and safety nets

- Enum + derived C identifiers (~78 enum refs, ~50 derived): compiler
  proves completeness after the enum rename.
- String tokens (wire/protocol, ~9) and config keys (≥6 incl. `mid_creep_*`)
  and scripts: NOT compiler-checked. An exhaustive grep inventory of
  `advance|trailing|ADVANCE|TRAILING` and state-`mid` is a required Phase 1
  deliverable; CI/grep must return zero matches at the end.
- `PIN_BUF_ADVANCE → PIN_BUF_TENSION`: pin→state decode is the historical
  bug site; rename only here, correctness verified in `audit-sync-polarity`.

### D4 — Behavior-preserving gate

Host build green + a captured status-line + event-token semantics snapshot
identical pre/post (same numeric behavior, only token spellings differ).
Any numeric/behavioral delta = stop, it is not a faithful rename.

### D5 — Specs/docs

Reword 5 live specs (`sync-state-model`, `sync-refactor`,
`toolchange-orchestration`, `motion-safety`, `live-tuner`) and 6 docs.
Archived `openspec/changes/archive/*` left as historical record (no live
contract references them).

## Risks / Trade-offs

- [Non-compiler-checked strings/config silently missed] → exhaustive grep
  inventory + zero-match gate; scripts updated in the same change.
- [Breaking config keys for users] → accepted by decision (pre-stable);
  document the key map in `MANUAL.md` migration note.
- [Old logs read in old vocab] → accepted (dev); historical only.
- [Rename accidentally changes behavior] → D4 snapshot gate.

## Migration Plan

P0 freeze D1 table. P1 enum → derived identifiers → tokens+config+scripts
in lockstep (grep inventory deliverable). P2 protocol emit + scripts +
docs + `TEST_CASES.md` snapshots together. P3 live specs reworded. Gate D4.
Rollback: revert; pure rename has no state/format migration beyond config
keys.

## Open Questions

- `EV:SYNC:*` substrings beyond `ADV_RISK_HIGH` that encode a state — sweep
  during P1 grep inventory; none expected but verify.
