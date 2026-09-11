---
phase: "14"
slug: "klipper-mmu-status-parity"
# status lifecycle: draft (seeded by plan-phase) → validated (set by validate-phase §6)
# audit-milestone §5.5 distinguishes NOT-VALIDATED (draft) from PARTIAL (validated + nyquist_compliant: false) (#2117)
status: draft
nyquist_compliant: false
wave_0_complete: false
created: "2026-09-12"
---

# Phase 14 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Python stdlib `unittest`, discovery-based (no pip dependency) |
| **Config file** | none — driven by `scripts/validate_regression.py` |
| **Quick run command** | `python3 -m unittest scripts.test_flare_mmu_status -v` |
| **Full suite command** | `python3 -m unittest discover -s scripts -p 'test_*.py'` |
| **Estimated runtime** | ~1 s quick / ~60 s full (full includes `test_sync_sim`, which needs `build_sim`) |

---

## Sampling Rate

- **After every task commit:** Run `python3 -m py_compile scripts/*.py klipper/*.py && python3 -m unittest scripts.test_flare_mmu_status -v`
- **After every plan wave:** Run `python3 -m unittest discover -s scripts -p 'test_*.py'`
- **Before `/gsd-verify-work`:** `python3 scripts/validate_regression.py` green
- **Max feedback latency:** 60 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| (filled by planner) | | | REQ-klipper-status-parity-* | — | N/A | unit | `python3 -m unittest scripts.test_flare_mmu_status -v` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `python3 -m py_compile klipper/*.py` — not covered by any existing gate (`validate_regression.py` globs `scripts/*.py` only); every plan touching `klipper/mmu.py` must run it explicitly, or extend the gate
- [ ] Schema-assertion block in `scripts/test_flare_mmu_status.py` (fresh mock, never `SET_MMU`'d → full HH key set present)
- [ ] Action-string test for the expanded `_derive_action` vocabulary
- Framework install: none — `unittest` stdlib, already in use

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| FlowGuard meter renders in Fluidd and Mainsail MMU panel | REQ-klipper-status-parity-flowguard-dict | needs a live Moonraker + browser | `HW:` open Fluidd/Mainsail MMU panel with daemon connected and sync active; meter visible and moves with tension/compression |
| Panel dialogs (LED, test config, grip/release) do not raise | REQ-klipper-status-parity-command-stubs | UI interaction | `HW:` click each dialog; console shows FLARE ack, no `Unknown command` |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 60s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
