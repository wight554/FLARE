# psf-feed-quality (archived 2026-06-11)

- Hunting/end-burst: ACCEPTED no-action — real multi-pause print clean, transient-only swings; `KD_PSF` stays 0, snap target unmoderated.
- BS silent no-op root cause: `pick_boot_stabilize_lane` returned active lane blind; empty active lane passed presence gate → `OK` no motion. Fix: fall through to filament-bearing lane (`sync.c`). Predict-reached/stagnant-init hypotheses from proposal were wrong.
- Stagnant guard: 200 ms single check false-aborted BS from deep tension (spring drag + ~80 ms EMA lag); fix = `PSF_STAB_STAGNANT_MS` 600 + rolling re-anchor (also catches late stalls).
- Rail-break race: MV past rail puts piston beyond sensing range → BS has 1.4–2.1 s sensor-flat breakaway vs 1500 ms deadline = coin-flip strand at −1.00; `PSF_STAB_RAIL_BREAK_MS` default → 3000. Dead time scales with strand depth.
- MV lacked `buffer_stabilize_cancel()` (BL had it) → dual-controller on one motor; one-line fix in `protocol.c` MV handler.
- Lesson: daemon event history drops `BUF_STAB:*` even on working BS — bench-verify stabilize by 100 ms BP polling over daemon `/status`, never by event absence.
