# type-d-dynamic-flow (archived 2026-06-04)

- Slow→fast TENSION step-skip = missing feed floor; demand-scaling erased baseline. Fix: `SYNC_MIN_RATE` floor pre-empts step (quiet default 100; ~1000 kills skip but compression-noisy — zero-skip is INHERENTLY compression-noisy on type-D; type-P is the quiet+skip-free path).
- Fast-step tension BURST fix = AIMD probe feed-floor latch: snap floor to demand on switch touch, probe UP in TENSION, ease DOWN in COMPRESSION, hold NEUTRAL + uncertainty-creep toward safe compression rail (creep = key fix; tension became structural step-ups only). Knobs `SYNC_TENSION_PROBE_MAX/_UP/_DOWN/_NEUTRAL` 3000/3000/1200/150. HW validated.
- FAILED + removed pivots: EST-escalation, decaying-floor, EST-clamp — all blind-inference attempts without mid-band observability.
- Do NOT port type-D demand-reconstruction (AIMD latch, NEUTRAL creep, raise-only EST) to type-P — type-P sees demand directly via analog PD; porting blind inference = regression.
- Spec: `openspec/specs/type-d-dynamic-flow/spec.md`. Compare control laws by RATES, not raw counts.
