---
phase: "16"
slug: "tmc-tension-current-boost"
# status lifecycle: draft (seeded by plan-phase) → validated (set by validate-phase §6)
# audit-milestone §5.5 distinguishes NOT-VALIDATED (draft) from PARTIAL (validated + nyquist_compliant: false) (#2117)
status: draft
nyquist_compliant: true
wave_0_complete: false
created: "2026-09-14"
---

# Phase 16 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Custom C Host Test Suite (`tests/host/`) & Python `unittest` |
| **Config file** | `tests/host/CMakeLists.txt` |
| **Quick run command** | `python3 scripts/test_status_line_budget.py && python3 scripts/test_settings_parity.py` |
| **Full suite command** | `cmake --build build_sim --target test_tmc_boost && ./build_sim/test_tmc_boost && ./build_sim/flare_sim --scenario sem_psf_tension_boost` |
| **Estimated runtime** | ~15 seconds |

---

## Sampling Rate

- **After every task commit:** Run `python3 scripts/test_status_line_budget.py && python3 scripts/test_settings_parity.py`
- **After every plan wave:** Run `cmake --build build_sim --target test_tmc_boost && ./build_sim/test_tmc_boost && ./build_sim/flare_sim --scenario sem_psf_tension_boost`
- **Before `/gsd-verify-work`:** Full test runner green and `python3 scripts/validate_regression.py` passes
- **Max feedback latency:** 15 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 16-01-01 | 01 | 1 | R1, R6 | T-16-01 | Hardware current clamp (<= 1200 mA), shadow sync | unit | `python3 scripts/test_settings_parity.py` | ✅ | ⬜ pending |
| 16-01-02 | 01 | 1 | R1, R2, R3 | T-16-02, T-16-03 | Edge-triggered boost, hysteresis release, state unwind | unit | `python3 scripts/test_settings_parity.py` | ✅ | ⬜ pending |
| 16-01-03 | 01 | 1 | R1, R2, R3, R4, R6 | T-16-01..04 | Comprehensive unit tests for activation, hysteresis, unwind, heartbeat, and clamps | unit | `cmake --build build_sim --target test_tmc_boost && ./build_sim/test_tmc_boost` | ❌ W0 | ⬜ pending |
| 16-02-01 | 02 | 2 | R5 | T-16-05 | Fixed status line budget <= STATUS_LINE_MAX | unit | `python3 scripts/test_status_line_budget.py` | ✅ | ⬜ pending |
| 16-02-02 | 02 | 2 | R7 | T-16-06, T-16-07 | Parameter bounds (BOOST_ON < BOOST_OFF, <= 1200 mA), SET/GET parity | parity | `python3 scripts/test_settings_parity.py` | ✅ | ⬜ pending |
| 16-02-03 | 02 | 2 | R4, R5, R7 | — | Host daemon/CLI dump parity, plant simulation scenario & doc sync | regression | `python3 scripts/validate_regression.py` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/host/test_tmc_boost.c` — unit test suite for R1, R2, R3, R6
- [ ] Update `tests/host/CMakeLists.txt` — add `test_tmc_boost` executable target
- [ ] `tests/host/sim_scenario.c` — add `sem_psf_tension_boost` integration scenario

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Motor thermal temperature rise under persistent boost | R1, R4 | Stepper coil thermodynamics and heat dissipation require physical hardware probe | `HW:` test on physical test rig: run active sync with tension boost for 10 min, probe motor case temp (< 65°C acceptable) |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 15s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending 2026-09-14
