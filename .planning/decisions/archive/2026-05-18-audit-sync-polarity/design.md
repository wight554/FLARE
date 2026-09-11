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
from the audit table. Relay fixes retestable on the Pi.

### D4 — Analog/PSF: no rig (resolved fork)

No analog hardware exists. Analog findings are handled per site as either
(a) **Happy-Hare-modelled** — implement the analog polarity to match
moggieuk/Happy-Hare `extras/mmu/mmu_sync_controller.py3` (the reference
2-/analog buffer controller) where it gives a clear analogue, or (b)
**basic spec only** — document expected analog behavior in the findings
table + `TEST_CASES.md` as `pending-analog-rig`, ship no code. Never
blind-fix analog from guesswork. Relay remains the verified path.

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

## Findings — 2026-05-18

| # | Site | Classification | Evidence |
|---|------|----------------|----------|
| 1 | Pin→state decode | ✅ correct | `buf_state_raw()` maps `g_buf_tension_din` to `BUF_TENSION`, `g_buf_compression_din` to `BUF_COMPRESSION`, and swaps only under `BUF_INVERT`; analog maps `g_buf_pos > BUF_THR` to `BUF_TENSION` and `< -BUF_THR` to `BUF_COMPRESSION`. |
| 2 | Relay block | ✅ correct | Relay override in `sync_tick()` uses `BUF_TENSION → relay_base * SYNC_RELAY_CATCHUP_FRAC`, `BUF_COMPRESSION → SYNC_MIN_SPS`, and `BUF_NEUTRAL → min(extruder_est_sps * SYNC_RELAY_NEUTRAL_FRAC, relay_base)`. This preserves `8f54bff` and `1d9ebe5` in the new vocabulary. |
| 3 | `buf_target_reserve_mm()` / `RT` sign | ✅ correct | Target starts negative (`-(threshold * pct)`) and bias subtracts more negative distance, so reserve target remains on the `BUF_COMPRESSION` side as required. |
| 4 | `RE = bp - target` | ✅ correct | `sync_reserve_error_mm()` returns `g_buf_pos - buf_target_reserve_mm()`. With compression-side target negative, positive error means too tension-side/empty and increases feed; negative error means too compression-side/full and backs off. |
| 5 | Fast brake | ✅ correct | `sync_on_transition()` arms `sync_fast_brake_until_ms` only for `BUF_TENSION → BUF_COMPRESSION`, then `sync_tick()` forces `target_sps = 0` while active. Empty-to-full transition brakes feed. |
| 6 | `compression_floor` | ❓ pending-analog-rig | Relay mode correctly skips the floor in `BUF_COMPRESSION`. Analog/PSF mode still enforces `max(sync_current_sps, compression_floor)` in `BUF_COMPRESSION`; this may be a low coasting floor rather than inversion, but needs analog hardware to confirm physical behavior. No blind fix shipped. |
| 7 | `compression_recovery` / collapse | ❓ pending-analog-rig | Recovery caps target while in `BUF_COMPRESSION`, tightens ramp-down after dwell, and auto-stops only after collapsed-to-floor dwell. Relay behavior is correct; analog dynamic response needs a PSF/analog rig before changing behavior. |
| 8 | `neutral_anti_tension` floor | ✅ correct | Floor applies only in `BUF_NEUTRAL`, active feed task, and non-positive reserve error (at/near compression-side target). It raises a baseline-derived feed floor to prevent drift to `BUF_TENSION` and is not active in `BUF_COMPRESSION`. |
| 9 | Estimator at crossing | ✅ correct | `travel_mm` is positive for NEUTRAL→TENSION / COMPRESSION→TENSION and negative for NEUTRAL→COMPRESSION / TENSION→COMPRESSION; `extruder_mm_s = mmu_mm_s + arm_vel_mm_s` matches positive arm velocity as printer pulling faster. |
| 10 | REFILL / RELIEVE effort | ✅ correct | `g_sync_refill_effort_mm` accumulates only in `BUF_TENSION`; `g_sync_relieve_effort_mm` accumulates only in `BUF_COMPRESSION`, matching empty/refill and full/relieve semantics. |
| 11 | AUTO_START gate | ✅ correct | Auto-start requires `s == BUF_TENSION`, so sync arms when the buffer is empty/starved and needs refill. Tail-assist auto-stop still waits for sustained `BUF_COMPRESSION`. |
| 12 | `BP` / `BPV` / `RE` telemetry sign | ✅ correct | `buf_signal.pos_norm` documents `-1 = full compression`, `+1 = full tension`; status `BPV` emits `g_buf_pos * 100`, `RT` emits the negative compression-side target, and `RE` emits `bp - target`. |
| 13 | Audit text/comments | ✅ corrected | Two post-rename comments still described TENSION/COMPRESSION backwards around tension dwell and compression-wall critical handling; comments were corrected with no behavior change. |

Relay result: no active relay inversion found after the prerequisite rename.
Analog result: no code change shipped without rig; sites #6 and #7 are
recorded as `pending-analog-rig` in `TEST_CASES.md`.

## Open Questions

- Resolved: no analog rig — see D4 (Happy-Hare-modelled or basic-spec-only,
  never blind). Re-open only if an analog rig appears.
