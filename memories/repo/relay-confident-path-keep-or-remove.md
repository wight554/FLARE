# relay-confident-path-keep-or-remove (archived 2026-05-20)

- Type-D relay confident-estimator path caused bimodal bang-bang; vase A/B print: confident path 42.6% TENSION+wall time vs fallback path 1.0%.
- Verdict: REMOVE confident path → relay fallback-only law (see `relay-fallback-only`, spec `openspec/specs/relay-fallback-only/spec.md`).
- Lesson: estimator confidence gating on 2-switch buffer unreliable — no mid-band ground truth between switch crossings.
- Compare control laws by A/B zone-residency metrics on real print, not by raw event counts.
