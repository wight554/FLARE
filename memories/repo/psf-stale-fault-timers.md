# psf-stale-fault-timers (archived 2026-06-04)

- Type-P: BUF_GOAL sits compression-side → idle buffer rests in control-TENSION zone → tension-dwell + saturation fault timers accumulated while sync OFF → spurious FAULT_HOLD on engage + deadlock loop.
- Fix: scope fault timers to active-sync window — restart timers on sync activation (`sync_analog.c`, spec `psf-type-p-sensor`).
- Lesson: any fault timer fed by buffer-zone residency must only run while the controller that can change the zone is active.
