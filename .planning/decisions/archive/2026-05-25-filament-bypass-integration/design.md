## Context

FLARE is a dual-lane MMU controller without a physical selector stepper motor. When an operator wants to print using an ad-hoc spool, they bypass the MMU drive steppers entirely. The bypass lane is physically completely disconnected from the MMU drive gears. The operator manually feeds the filament through the bypass tube straight to the extruder. To support this:
- **Klipper extras** must allow G-code slicer toolchanges to tool `-2` (Bypass).
- **Macro Orchestration** must automate filament grabbing and meltzone pushes when filament is manually inserted and hits the toolhead sensor.
- **Single-Sensor Telemetry**: The toolhead sensor is the *only* physical sensor in bypass mode; all other MMU lane, gate, and combiner sensors must be forced to inactive to mirror physical reality.
- **Daemon Syncer** must synchronize the bypass state without clobbering Klipper's state.
- **Standalone WebUI** must represent the disengaged bypass lane.

---

## Goals / Non-Goals

**Goals:**
- Gracefully accept and handle bypass sentinel `-2` inside `MMU_CHANGE_TOOL` and `MMU_SELECT` in Klipper.
- Automatically execute `MMU_LOAD` (extruder-only push) when filament is manually fed and triggers the toolhead sensor under bypass mode.
- Suppress and skip all MMU lane eject movements (`MMU_EJECT` / `FLARE_EJECT`) when bypassed, as the operator manually pulls the strand out.
- Force all gate, gear, and combiner sensors to inactive under bypass mode, leaving the toolhead sensor as the single active source of truth.
- Synchronize bypass status dynamically in Klipper's `get_status` and the daemon's telemetry queues.
- Render a premium "Filament Bypass" card in the standalone WebUI, disengaging MMU steppers (`T:0`) and gating control buttons.

**Non-Goals:**
- Modifying C firmware steppers or motion logic (all suppression is handled cleanly at the host-side/Klipper level).

---

## Decisions

### D1: Auto-Load G-Code Trigger
- **Decision**: Update `_FLARE_ON_TOOLHEAD_INSERT` macro inside `klipper/flare_mmu.cfg` to check `printer.mmu.bypass` and trigger `MMU_LOAD`.
  ```gcode
  [gcode_macro _FLARE_ON_TOOLHEAD_INSERT]
  gcode:
      RUN_SHELL_COMMAND CMD=flare PARAMS="TS:1"
      {% if printer.mmu and printer.mmu.bypass %}
          RESPOND MSG="FLARE: Bypass active. Auto-loading filament on toolhead sensor insert."
          MMU_LOAD
      {% endif %}
  ```
- **Rationale**: Keeps event triggers aligned inside standard G-code macros rather than adding complex, opaque Python-level listeners.

### D2: Suppress MMU Eject in Bypass
- **Decision**: Update `cmd_MMU_EJECT` in `klipper/mmu.py` to check `self.bypass` and return early without calling `FLARE_EJECT`.
  ```python
  if self.bypass:
      gcmd.respond_info("FLARE: Bypass active; no physical eject needed. Please manually pull the filament strand out.")
      return
  ```
- **Rationale**: Prevents Klipper from dispatching invalid serial commands like `UM:-1` to the board which would return errors. Since bypass has no drive gear engagement, eject is a physical no-op.

### D3: Slicer Toolchange Bypass Sentinel
- **Decision**: Add bypass check early in `cmd_MMU_CHANGE_TOOL` inside `klipper/mmu.py`.
  ```python
  if gate == -2 or tool == -2:
      self._select_bypass(gcmd)
      return
  ```
- **Rationale**: Allows slicer-generated `T-2` toolchanges to succeed instantly without throwing G-code index validation errors.

### D4: Unified Telemetry Synchronization
- **Decision**: Cache `bypass` inside the daemon's `status_cache` and expose it to client WebUIs.
- **Rationale**: Eliminates synchronization Battles between Klipper and the daemon, maintaining 100% telemetry parity.

### D5: Single-Sensor Bypass Telemetry
- **Decision**: In `get_status` inside `klipper/mmu.py`, when `self.bypass` is true, force `pre_gate_sensor_active = False`, `gate_sensor_active = False`, `hub_sensor_active = False`, and `extruder_sensor_active = False`. In the returned `sensors` dictionary, set all keys to `False` except `toolhead` which mirrors the physical toolhead sensor state.
- **Rationale**: Since the bypass lane is physically completely separate from the MMU lanes, it has no contact with the gate sensors (`IN`), gear sensors (`OUT`), or Y-combiner (`YS`). Forcing these clear ensures that Fluidd/Mainsail's 5-dot track widget correctly shows all other dots hollow and only highlights the toolhead sensor dot, accurately reflecting the single-sensor physical setup.

---

## Risks / Trade-offs

- **Risk**: Auto-load triggers when not bypassed.
  - *Mitigation*: Strictly guard with `printer.mmu.bypass` so it never fires during active MMU lane prints.
- **Risk**: Stale active lane state on board.
  - *Mitigation*: When bypass is selected, Klipper/WebUI dispatches `T:0` to disengage active lane, preventing auto-preload and sync feedback algorithms from driving.

