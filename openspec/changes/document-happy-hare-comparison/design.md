# Design Notes

## Findings

- Happy Hare is a Klipper extension built mostly from Python extras, a
  Moonraker component, and Klipper macros/config. Its README describes it as a
  universal MMU driver for Klipper with support for many MMU designs, many
  sensors, Spoolman, Moonraker update-manager, Mainsail/Fluidd, and a
  KlipperScreen companion.
- Happy Hare's main control lives in Klipper (`extras/mmu/mmu.py`) and exposes
  rich G-code commands such as `MMU_CHANGE_TOOL`, `MMU_LOAD`, `MMU_UNLOAD`,
  `MMU_ENDLESS_SPOOL`, and `MMU_SPOOLMAN`.
- Happy Hare EndlessSpool maps tools to alternate gates when a spool runs out.
  It depends on host-side Happy Hare state, Klipper commands, gate maps, and
  optional Spoolman/UI integration.
- Happy Hare sync can observe Klipper extruder motion and adjust MMU gear
  rotation distance from sync-feedback sensors. That gives it stronger host
  context than FLARE.
- Happy Hare wiki describes it as a Klipper-expanded state machine whose
  parameters can mostly change at runtime.
- Happy Hare wiki now describes native Mainsail/Fluidd MMU panels/dashlets in
  addition to KlipperScreen, including tool-to-gate mapping, gate-map editing,
  and maintenance/state-recovery surfaces.
- FLARE is intentionally firmware-first: RP2040 owns the two lanes, sensors,
  TMC configuration, RELOAD state machine, and USB serial protocol. Klipper can
  command it, but FLARE can run RELOAD without Klipper or a plugin.

## Plan

### README.md

- Add a short section after "What Is In This Repo".
- State that FLARE is not trying to replace Happy Hare's universal Klipper UI
  and configuration stack.
- State why FLARE was developed: lower Klipper/API overhead, host-less RELOAD,
  and minimal serial command integration.
- State tradeoffs: two-lane ERB v2.0 focus, weaker host-context sync because
  firmware does not see Klipper's exact extruder baseline, and less polished UX
  until a UI/mediator exists.
- Mention future web UI carefully: possible only if serial ownership is solved,
  likely by making the UI a mediator rather than a second independent serial
  reader.

## Risks

- Avoid overstating Happy Hare limitations; it is broader and more mature.
- Avoid implying FLARE has Happy Hare's KlipperScreen/Mainsail/Fluidd plugin
  experience today.
