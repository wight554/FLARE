# Phase 14 — Deferred Items

Out-of-scope discoveries logged during execution of 14-01-PLAN.md. Not fixed here per the
executor's scope boundary (pre-existing issues unrelated to the current task's files).

## test_sync_sim.py — missing `flare_sim` host binary

**Found during:** Task 3 full-regression run (`python3 -m unittest discover -s scripts -p 'test_*.py'`).

**Issue:** `scripts/test_sync_sim.py`'s `PsfTypePSensorTests` (10 test cases, some parameterized
across `sensor_type='d'/'p'`) shell out to a compiled `tests/host/flare_sim` binary via
`subprocess.run`. This worktree has no `build_local`/`build_host` directory, so the binary does
not exist and every such test errors with a `FileNotFoundError` inside `subprocess.run` —
unrelated to this plan's host-Python-only diff (`scripts/flare_daemon.py`, `klipper/mmu.py`,
`scripts/test_flare_mmu_status.py`).

**Not fixed because:** Building `tests/host/flare_sim` (or the firmware target) is out of this
plan's scope — Phase 14 is explicitly a no-firmware-changes, host-Python-only wave. Building the
sim binary is a pre-existing CI/dev-environment setup step, not something this plan's diff
touches or breaks.

**Recommendation:** Build `tests/host/` (see `tests/host/CMakeLists.txt`) before running the full
`python3 -m unittest discover -s scripts -p 'test_*.py'` regression suite in a fresh worktree/CI
runner, or skip `test_sync_sim.py` when the binary is absent.

## `python3 scripts/validate_regression.py` — no `PICO_SDK_PATH` in this worktree

**Found during:** 14-01-PLAN.md's plan-level `<verification>` step (run after Task 3's commit).

**Issue:** `validate_regression.py` configures `build_local` with CMake against
`firmware/CMakeLists.txt`, which requires `PICO_SDK_PATH` (or
`PICO_SDK_FETCH_FROM_GIT=ON`) to locate the Raspberry Pi Pico SDK. Neither is set in this
worktree's environment, so `cmake -S firmware -B build_local -G Ninja -DFLARE_DEV_TUNING=ON`
fails immediately with "SDK location was not specified" before any firmware or host-sim code
compiles.

**Not fixed because:** This plan makes no firmware changes (host-Python-only diff:
`scripts/flare_daemon.py`, `klipper/mmu.py`, `scripts/test_flare_mmu_status.py`,
`.planning/REQUIREMENTS.md`). Provisioning `PICO_SDK_PATH` is an environment/toolchain setup
concern, not something this plan's diff touches or breaks. `python3 -m py_compile scripts/*.py
klipper/*.py` and the full `python3 -m unittest discover -s scripts -p 'test_*.py'` (Python
layer) both ran clean apart from the pre-existing `test_sync_sim.py` gap above.

**Recommendation:** Set `PICO_SDK_PATH` (see `BUILD_FLASH.md`) in the worktree/CI environment
before relying on `validate_regression.py`'s firmware-build gate for host-Python-only phases.
