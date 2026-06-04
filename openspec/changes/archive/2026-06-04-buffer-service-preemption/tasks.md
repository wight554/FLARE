# Tasks — buffer-service-preemption

- [x] 1. Add buffer-stabilize cancel API.
  - 2026-06-03: Added `buffer_stabilize_cancel()` wrapper around the existing
    stabilize stop helper.
- [x] 2. Make `BS` cancel active sync/BL/BS/simple lane commands while keeping hard activities busy.
  - 2026-06-03: `BS` rejects TC/cutter/manual unload, then cancels sync/BL,
    active stabilize, and standalone lane tasks before requesting fresh
    stabilize.
- [x] 3. Make `BL:T` / `BL:C` cancel active `BS` stabilize before arming.
  - 2026-06-03: BL parses args first, rejects hard activity, cancels stabilize,
    preempts active sync, then arms the requested lock.
- [x] 4. Update docs.
  - 2026-06-03: Updated `MANUAL.md` and `BEHAVIOR.md`.
- [x] 5. Validate build, regression, Python tests, and OpenSpec strict.
  - 2026-06-03: Passed `ninja -C build_local`,
    `bash scripts/validate_regression.sh`, `python3 -m py_compile scripts/*.py`,
    `python3 scripts/test_*.py`, and
    `openspec validate buffer-service-preemption --strict`.
