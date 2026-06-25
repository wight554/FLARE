## 1. Spec sync (no cfg changes — cfg is already correct)

- [x] 1.1 `specs/klipper-mmu-config` "Single-file" scenario: `T0`/`T1` (not
  `T1`/`T2`); add `FLARE_PRELOAD` and `_FLARE_SET_PURGE`. Acceptance: macro list
  matches `klipper/flare_mmu.cfg`.
- [x] 1.2 Remove the `FLARE_CUT` macro from `klipper/flare_mmu.cfg`; drop it from
  the "Single-file" macro list. Acceptance: `FLARE_CUT` absent from cfg.
- [x] 1.2b "Removed development macros": drop `FLARE_PRELOAD`; add `FLARE_CUT`;
  keep `FLARE_CUT_BARE` / `FLARE_CUT_TEST`. Acceptance: requirement + scenario
  name `FLARE_CUT`, `FLARE_CUT_BARE`, `FLARE_CUT_TEST`.
- [x] 1.3 Add "Preload macro routes selected lanes to the gate" requirement
  matching `FLARE_PRELOAD` in cfg (`T:{lane}`+`LO:` for 1/2, `LO:` for 0/none,
  error otherwise). Acceptance: scenarios mirror the cfg branches.
- [x] 1.4 "Tip forming macro" dip-defaults scenario: `dip_melt_gap=0.1`,
  `dip_speed=30.0`, `dip_pause=10` (matches `_FLARE_TIP_FORMING_DEFAULTS`).

## 2. Validation

- [x] 2.1 `openspec validate klipper-spec-sync-drift --strict` passes.
- [x] 2.2 `openspec validate --specs --strict` passes.
- [ ] 2.3 Cross-check every macro named in the corrected "Single-file" scenario
  exists in `klipper/flare_mmu.cfg`, and `FLARE_CUT_BARE`/`FLARE_CUT_TEST` do
  not (re-grep before archive).

## Readiness and Delivery Checks

- [ ] No firmware/script touched; build + py_compile N/A (spec-only change).
- [ ] Confirm no cfg edit is required (cfg is the source of truth and already
  matches the corrected spec).
- [x] `openspec validate klipper-spec-sync-drift --strict` passing.
- [x] `openspec validate --specs --strict` passing.
- [ ] Deferred items tracked: `dist_meltzone_to_nozzle_tip` dead-var + stale
  "full hotend path" / "ignore-buffer MV" tip-form descriptions are NOT resolved
  here (overlap `klipper-slicer-purge-and-load-tuning`; need tip-form domain
  decision). Resolve when that change finalizes.
- [ ] Append observation `memories/repo/klipper-spec-sync-drift.md` (3-5 lines:
  drift items fixed, deferred tip-form/dist_meltzone entanglement) before
  archiving.
