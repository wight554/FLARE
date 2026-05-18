## Context

Depends on `rename-buffer-states-tension-compression`. Vocabulary then:
`BUF_TENSION` = empty, printer>MMU, BP>0 → FEED; `BUF_COMPRESSION` = full,
MMU>printer, BP<0 → BACK OFF; `BUF_NEUTRAL` ≈ 0. Both relay (2-endstop,
current focus) and PSF (analog) paths supported. Historical misnaming
caused `8f54bff` (relay rates swapped); analog path rarely exercised so
inversions there are unconfirmed and high-risk.

## Goals / Non-Goals

**Goals:** every polarity site verified against the explicit contract;
inversions fixed; both paths consistent; pin decode proven.

**Non-Goals:** no renaming (prerequisite change); no token/protocol change;
not a controller redesign — targeted polarity correctness only.

## Decisions

### D1 — Classification scheme

Per site: `✅ correct` · `⚠ inverted-active-relay` ·
`⚠ inverted-active-analog` · `❓ needs-hardware-confirmation`. The findings
table lives in this change as the durable audit artifact.

### D2 — Audit site list (verify each in new vocab)

```
 #  site                                    expected behavior
 1  pin→state decode (PIN_BUF_TENSION)      pressed tension switch ⇒ BUF_TENSION
 2  relay block (sync.c, 8f54bff/1d9ebe5)   TENSION→catchup, COMPRESSION→stop,
                                            NEUTRAL→demand-tracked
 3  buf_target_reserve_mm RT sign           RT<0 = toward COMPRESSION (reserve)
 4  RE = bp - target                        sign consistent with #3
 5  fast_brake (TENSION→COMPRESSION)        empty→full ⇒ brake feed ✓ verify
 6  compression_floor (was trailing_floor)  legacy raised feed when "trailing
                                            =empty" → suspect INVERTED-analog
 7  compression_recovery / collapse         assumes full=danger? verify analog
 8  neutral_anti_tension floor              prevents drift to TENSION(empty)
 9  estimator @ crossing (sync.c:722)       extruder = mmu + arm_vel; sign/state
 10 REFILL effort in TENSION /              ✅ already consistent (polarity
    RELIEVE in COMPRESSION                     was derived from this)
 11 AUTO_START gate state                   arms when TENSION(empty)? verify
 12 BP / BPV / RE telemetry sign            matches contract; doc it
```

### D3 — Fixes are separate, justified commits

Unlike the rename (byte-identical), these change behavior. One commit per
inversion (or tight group), each with the contradiction it resolves quoted
from the audit table. Relay fixes retestable on the Pi; analog `❓` items
gated on an analog rig and may ship behind that confirmation.

## Risks / Trade-offs

- [Analog path untested on current hardware] → classify `❓`, do not blind
  -fix unverifiable analog behavior; document and gate on an analog rig.
- [Audit misses a site] → D2 list is seeded from the grep inventory of the
  prerequisite change, not memory; cross-check against it.
- [Fix regresses relay] → each relay fix retested with the standard
  `flare_cmd "?:" --poll 500` capture.

## Migration Plan

1. Land prerequisite rename.
2. Walk D2 sites; fill the findings table (classification + evidence).
3. Fix `✅`→none, `⚠`→commit per fix, `❓`→document + gate.
4. Re-validate relay on the Pi; record analog items as pending-analog-rig.

Rollback: per-fix revert (each is isolated and justified).

## Open Questions

- Is there an analog/PSF test rig available, or do `❓` analog inversions
  ship documented-but-unverified until one exists?
