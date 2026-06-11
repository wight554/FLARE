# quiet-set-mmu-mirror (archived 2026-06-11)

- Idle `SET_MMU` console flood = daemon's blind 10 s force-full timer, NOT buffer noise (Hall wobble ±0.01 < 0.05 push threshold). Fix: silent reconcile — read `/printer/objects/query?mmu`, full push only on divergence/absence (`flare_daemon.py`, spec `daemon-klipper-mirror`).
- Reconcile compare must mirror `cmd_SET_MMU` formatting exactly (floats `%.3f`/`%.2f`, quoted strings) — false-positive re-introduces spam, false-negative leaves stale UI. Comparator unit-tested in `test_flare_mmu_status.py`.
- Live validation via Moonraker gcode-store counting (scriptable, beats console eyeballing): idle 70 s = 0 lines, jog = small deltas only, Moonraker restart = 1 full push, Klipper restart = 2 full pushes (klippy settling at first push; benign, converges next tick, then silent).
- Layer B (firmware `buf_status_label` ±0.1 debounce) closed NOT-NEEDED — no flap evidence; layer A was the entire spam.
- Lesson: blind periodic full-push is a poll standing in for an event — detect the restart (state divergence) instead of rebroadcasting on a timer.
