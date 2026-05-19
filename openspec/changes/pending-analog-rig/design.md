## Context

`relay-buffer-control-2switch` exhaustively validated the type-D relay path
is free of inverted-polarity assumptions. Three items survive in type-P
(analog) code only, inert under relay. No analog rig exists; policy is
never blind-fix analog. Previously parked as `relay-buffer-control-2switch`
7.3, blocking that change from archiving.

## Goals / Non-Goals

**Goals:**
- Single durable home for the deferred type-P inverted-polarity items.
- Make the deferral auditable: items recorded once, not re-derived.
- Unblock `relay-buffer-control-2switch` archival (7.3 → here).

**Non-Goals:**
- No code change now. No relay-path change (already correct).
- No speculative analog fix without a rig.

## Decisions

### D1 — Tracker change, not an implementation change
Tasks are intentionally hardware-blocked and stay unchecked until an analog
rig exists. The change remains in-progress as the canonical backlog entry;
it is not archived as "done" (nothing is done) and not applied.

- *Alternative (rejected):* leave 7.3 in `relay-buffer-control-2switch` —
  pins that otherwise-complete change at 23/24 indefinitely.
- *Alternative (rejected):* delete the items — guarantees re-derivation at
  the next polarity audit; loses the inert-under-relay reasoning.

### D2 — Resolution is rig-gated
Any fix MUST be developed and validated on an analog buffer rig
(`BUF_SENSOR_TYPE != 0`), never inferred from the type-D path.

## Risks / Trade-offs

- **Indefinitely-open change clutters `openspec list`** → acceptable; it is
  a known, labelled backlog item, cheaper than re-auditing.
- **Items drift from code as `sync.c` evolves** → line refs are
  approximate; the symptom description (inverted feed-floor under
  COMPRESSION) is the durable anchor, not the line numbers.

## Open Questions

- None. Resolution timing is purely "when an analog rig exists".
