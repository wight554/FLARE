## Context

FLARE's host integration is functional, but lacks polished UX. Polling updates run at 4Hz and generate continuous terminal clutter, while frontend interface buttons often trigger missing Happy Hare commands, throwing "Unknown command" in Klipper.

## Goals / Non-Goals

**Goals:**
- Zero terminal clutter during idle standby and steady-state printing.
- Full command parity with standard Fluidd/Mainsail MMU widget interface buttons.
- Real-time color updates in UI on gate map shifts.

**Non-Goals:**
- Simulating mechanical actions for stubs (e.g. `MMU_HOME` should not attempt firmware motion).
- Firmware changes.

## Decisions

### D1: Retire `_FLARE_STATE` State Mirror

The `_FLARE_STATE` G-code macro in `flare_mmu.cfg` was a duplicate mirror of daemon parameters. Research confirms no other Klipper macro or module references it.

- **Decision**: Completely delete `_FLARE_STATE` and all 13 corresponding `SET_GCODE_VARIABLE` lines sent by the daemon. Only `SET_MMU` remains.

### D2: Float Deadband for Change Detection

The daemon's `klipper_syncer` checks `changed = True` if any key in `keys` changed since the last push. Because `g_buf_pos` and `sps` are high-frequency float parameters, ADC noise causes them to vary slightly on every tick, triggering `changed = True` constantly at 4Hz.

- **Decision**: Remove `g_buf_pos` and `sps` from the change-detection list. Their values will still be updated and sent via `SET_MMU` whenever another digital state changes (e.g., sensor triggers, tasks, errors) or during the 10-second heartbeat, which is more than sufficient.

### D3: Dynamic RGB Parse in `cmd_SET_MMU`

When a spool color is updated via `SET_MMU` (e.g. `GATE_COLOR="ff0000"`), the Klipper side receives the string but does not update `gate_color_rgb` / `tool_color_rgb` (which are used by Klipper for native UI coloring).

- **Decision**: Update `cmd_SET_MMU` to inspect `GATE_COLOR` updates, parse their hexadecimal characters into RGB floats `[0.0 - 1.0]`, and update `self.gate_color_rgb` immediately:
  ```python
  # In cmd_SET_MMU:
  if gate_color_str is not None:
      # ... split gate colors
      for g_idx, color in enumerate(self.gate_color):
          color = color.lstrip('#')
          if len(color) == 8:  # Strip alpha
              color = color[:6]
          if len(color) == 6:
              try:
                  r = int(color[0:2], 16) / 255.0
                  g = int(color[2:4], 16) / 255.0
                  b = int(color[4:6], 16) / 255.0
                  self.gate_color_rgb[g_idx] = [r, g, b]
              except ValueError:
                  pass
  ```

### D4: Happy Hare Command Parity

Fluidd widget buttons and standard macro structures expect basic Happy Hare commands:
- `MMU_STATUS`: dumps all state in a human-readable format.
- `MMU_HOME`: stubbed to do nothing or log that FLARE homes automatically.
- `MMU_UNLOCK` / `MMU_PAUSE` / `MMU_RESET`: stubbed as no-ops to avoid errors.

- **Decision**: Register these commands in `klipper/mmu.py` and map them to appropriate stubs or dumps.
