## 1. Klipper Mock & Macro Hardening

- [x] 1.1 Expose `'bypass': self.bypass` key inside Klipper MMU mock `klipper/mmu.py` status dictionary.
- [x] 1.2 Update `cmd_MMU_CHANGE_TOOL` inside `klipper/mmu.py` to recognize bypass gate/tool sentinel index `-2` and transition to the bypass mode via `self._select_bypass`.
- [x] 1.3 Refine `cmd_MMU_EJECT` inside `klipper/mmu.py` to early return if `self.bypass` is active, completely suppressing any serial MMU eject command.
- [x] 1.4 Refine `get_status` cascade logic inside `klipper/mmu.py` under bypass mode to only expose the toolhead sensor state and set other path sensors and synthetic filament position appropriately.
- [x] 1.5 Update G-code macro `_FLARE_ON_TOOLHEAD_INSERT` inside `klipper/flare_mmu.cfg` to automatically execute `MMU_LOAD` if `printer.mmu.bypass` is true.

## 2. Daemon Telemetry Synchronization

- [x] 2.1 Add `bypass` boolean caching to `status_cache` in `scripts/flare_daemon.py`.
- [x] 2.2 Update Klipper status variables sync loop in `scripts/flare_daemon.py` to prevent Loop Battles by mapping `active_gate = -2` and `klipper_gate = -2` when Klipper is bypassed.
- [x] 2.3 Ensure disengage serial commands are safely dispatched on disengagement requests.

## 3. Premium Standalone WebUI Bypass Integration

- [x] 3.1 Inject the virtual Filament Bypass card directly next to/under physical lane cards in `scripts/webui/app.js` and render its disengaged status text.
- [x] 3.2 Update WebUI action button gating state logic in `scripts/webui/app.js` so that **Preload** and **Eject** are disabled when bypassed, and **Load** and **Unload** act purely as extruder-only operations.
- [x] 3.3 Style the Filament Bypass card inside `scripts/webui/style.css` using HSL neon accents, glassmorphism borders, and outfit typography.

## 4. Verification & Testing

- [x] 4.1 Validate Python syntax and compilation on all modified files (`python3 -m py_compile klipper/mmu.py scripts/*.py`).
- [x] 4.2 Verify local firmware builds successfully (`ninja -C build_local`).
- [x] 4.3 Verify local regression test suite passes cleanly (`bash scripts/validate_regression.sh`).

## Validation Notes (2026-05-25)

- **Python Syntax Verification**: Executed `python3 -m py_compile klipper/mmu.py scripts/*.py` and confirmed no syntax errors.
- **Local Compilation Check**: Ran `ninja -C build_local` successfully, verifying compiler output is 100% up-to-date.
- **Static Regression Testing**: Executed `bash scripts/validate_regression.sh` and confirmed all static regression gates passed cleanly with perfect diff hygiene.

