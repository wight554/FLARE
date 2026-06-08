## 1. Delete dead diagnostic scripts

- [x] 1.1 Delete `scripts/flare_trace_filament.py`.
- [x] 1.2 Delete `scripts/flare_unload_tracker.py`.
- [x] 1.3 Confirm no live import/doc/spec reference remains:
  `git grep -n -E 'flare_trace_filament|flare_unload_tracker' -- ':!openspec/changes/archive'`
  returns nothing.

## 2. Verify gate unaffected

- [x] 2.1 `python3 -m py_compile scripts/*.py` → pass.
- [x] 2.2 `python3 -m unittest discover -s scripts -p "test_*.py"` → 46 tests pass (no
  test targeted either script).
- [x] 2.3 `ruff check scripts/` → only a pre-existing `I001` in
  `test_flare_mmu_status.py` (owned by `quiet-set-mmu-mirror`, present in HEAD before
  this change); deletions introduce no new violation.

## 3. Closeout

- [ ] 3.1 Archive once deletions land and the gate is green.

## Validation notes

- 2026-06-08: Deleted both scripts via `git rm`. `git grep` residual live refs → NONE.
- 2026-06-08: `py_compile scripts/*.py` pass; `unittest discover` → 46 passed.
- 2026-06-08: `ruff check scripts/` → 1 error, pre-existing `I001` in
  `test_flare_mmu_status.py`, confirmed in `git show HEAD:` — out of scope (belongs to
  in-progress `quiet-set-mmu-mirror`).
