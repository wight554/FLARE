## 1. Protocol behavior

- [ ] 1.1 Add optional lane parsing for `UM`, accepting empty payload, `1`, or `2`.
- [ ] 1.2 Preserve current active-lane `UM` behavior for `UM`, `UM:`, and
  `UM:n` where `n == active_lane`.
- [ ] 1.3 Add inactive-lane standby eject behavior for `UM:n` where
  `n != active_lane`: require target idle, `IN=1`, `OUT=0`, then unload to
  `IN` clear without changing active print state.
- [ ] 1.4 Reject unsafe inactive targets, including target lane not idle, no
  filament at `IN`, or filament present at `OUT`.

## 2. Documentation and specs

- [ ] 2.1 Update `MANUAL.md` command reference for `UM[:lane]`.
- [ ] 2.2 Update `BEHAVIOR.md` unload command section with active vs inactive
  lane behavior.
- [ ] 2.3 Update `KLIPPER.md` / README examples or guidance if needed.
- [ ] 2.4 Update `openspec/specs/toolchange-orchestration/spec.md` manual
  unload scenarios.

## 3. Validation

- [ ] 3.1 Run `ninja -C build_local`.
- [ ] 3.2 Run `python3 -m py_compile scripts/*.py` if scripts change.
- [ ] 3.3 Review regression impact for preload, unload, toolchange/cutter,
  sync/RELOAD, protocol, and docs.
- [ ] 3.4 Commit and push implementation with required `Generated-By` footer.
