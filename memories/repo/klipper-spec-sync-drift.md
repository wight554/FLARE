# klipper-spec-sync-drift

- klipper-mmu-config spec had drifted from flare_mmu.cfg; cfg is source of truth.
  Fixed: Single-file list T0/T1 (was T1/T2) + FLARE_PRELOAD/_FLARE_SET_PURGE;
  removed-dev-macros now FLARE_CUT/FLARE_CUT_BARE/FLARE_CUT_TEST; new Preload
  requirement; tip-form dip defaults synced (dip_melt_gap=0.1, dip_pause=10).
- FLARE_CUT macro removed from cfg: cutter is sequenced by firmware TC:
  (toolchange.c TC_UNLOAD_CUT), so no standalone Klipper cut macro needed.
- dist_meltzone_to_nozzle_tip removed (dead after buffer-lock tip-form rewrite);
  its spec clause/scenario + the stale "ignore-buffer MV" tip-form wording live
  in the OVERLAPPING klipper-slicer-purge-and-load-tuning change (partitioned by
  requirement owner so two changes never edit the same requirement).
- Gotcha: two unarchived changes editing the same OpenSpec requirement = archive
  order silently reverts one. Partition edits by requirement, or fold.
