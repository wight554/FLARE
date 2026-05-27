## 1. klipper/mmu.py: Command Parity & RGB Sync

- [x] 1.1 In `cmd_SET_MMU`: parse `gate_color` and recompute `self.gate_color_rgb` using hex-to-float translation for all gates.
- [x] 1.2 Implement `cmd_MMU_STATUS(self, gcmd)`: prints a clean, formatted text output of all key status variables (active gate, tool, gate status, spool IDs, sensors, board status).
- [x] 1.3 Register `MMU_STATUS` command in `__init__`.
- [x] 1.4 Implement and register stubs for:
  - `MMU_HOME` (respond that FLARE requires no selector homing)
  - `MMU_UNLOCK` (no-op stub)
  - `MMU_PAUSE` (no-op stub)
  - `MMU_RESET` (no-op stub)

## 2. scripts/flare_daemon.py: Spam Cleanup

- [x] 2.1 In `klipper_syncer` thread: remove high-frequency float keys `"g_buf_pos"` and `"sps"` from the `changed` detection keys.
- [x] 2.2 In `klipper_syncer` thread: completely delete the loop that builds and appends `SET_GCODE_VARIABLE MACRO=_FLARE_STATE ...` commands.
- [x] 2.3 Verify `lines` in `klipper_syncer` only appends the `SET_MMU` command (and optional `_FLARE_SYNC_BOARD`).

## 3. klipper/flare_mmu.cfg: Macro Cleanup

- [x] 3.1 Delete the entire `[gcode_macro _FLARE_STATE]` block.

## 4. Validation

- [ ] 4.1 Python compile validation: run `python3 -m py_compile scripts/flare_daemon.py klipper/mmu.py`.
- [ ] 4.2 Run Klipper config check or validation (if applicable).
- [ ] 4.3 Verify no syntax errors are introduced.
