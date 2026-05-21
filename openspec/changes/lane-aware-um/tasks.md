## 1. Protocol behavior

- [x] 1.1 Add optional lane parsing for `UM`, accepting empty payload, `1`, or `2`.
- [x] 1.2 Preserve current active-lane `UM` behavior for `UM`, `UM:`, and
  `UM:n` where `n == active_lane`.
- [x] 1.3 Add inactive-lane standby eject behavior for `UM:n` where
  `n != active_lane`: require target idle, `IN=1`, `OUT=0`, then unload to
  `IN` clear without changing active print state.
- [x] 1.4 Reject unsafe inactive targets, including target lane not idle, no
  filament at `IN`, or filament present at `OUT`.

  2026-05-21 validation: `firmware/src/protocol.c` parses empty/`1`/`2`
  payloads exactly. Bare `UM`, `UM:`, and explicit active-lane `UM:n` use the
  previous active-lane side effects and shared-Y clear leg. Inactive `UM:n`
  requires `TC_IDLE`, target `TASK_IDLE`, `IN=1`, `OUT=0`, then starts only
  `TASK_UNLOAD` to IN-clear. Invalid payloads return `ER:ARG`; inactive busy
  returns `ER:BUSY`; inactive no-IN returns `ER:NOT_LOADED`; inactive OUT
  present returns `ER:NOT_PRELOADED`.

## 2. Documentation and specs

- [x] 2.1 Update `MANUAL.md` command reference for `UM[:lane]`.
- [x] 2.2 Update `BEHAVIOR.md` unload command section with active vs inactive
  lane behavior.
- [x] 2.3 Update `KLIPPER.md` / README examples or guidance if needed.
- [x] 2.4 Update `openspec/specs/toolchange-orchestration/spec.md` manual
  unload scenarios.

  2026-05-21 validation: `MANUAL.md`, `BEHAVIOR.md`, `KLIPPER.md`, and
  `README.md` document explicit lane forms and inactive standby constraints.
  `BEHAVIOR.md` also notes inactive standby eject preserves sync/retract-assist
  state. Added `lane-aware-um` change delta and folded the durable manual unload
  contract into `openspec/specs/toolchange-orchestration/spec.md`.

## 3. Validation

- [x] 3.1 Run `ninja -C build_local`.
- [x] 3.2 Run `python3 -m py_compile scripts/*.py` if scripts change.
- [x] 3.3 Review regression impact for preload, unload, toolchange/cutter,
  sync/RELOAD, protocol, and docs.
- [x] 3.4 Commit and push implementation with required `Generated-By` footer.

  2026-05-21 validation: `ninja -C build_local` passed.
  `python3 -m py_compile scripts/*.py` passed even though scripts were not
  changed. `openspec validate lane-aware-um --strict` and
  `openspec validate --specs --strict` passed.

  Regression impact review: preload remains unchanged; inactive `UM:n` is the
  inverse of standby preload and only starts from idle `IN=1`/`OUT=0`. Active
  unload behavior remains in the existing branch. Inactive eject has no
  `cutter_start()` path, does not inspect shared Y, and does not call
  `sync_disable()` or `set_toolhead_filament(false)`. RELOAD/toolchange-owned
  states reject inactive eject with `ER:BUSY`. Protocol docs and durable specs
  now describe the new grammar and error constraints.

  2026-05-21 closeout: implementation committed and pushed as `f4b9d58`
  (`protocol: support lane-aware um`) with required `Generated-By` footer.
