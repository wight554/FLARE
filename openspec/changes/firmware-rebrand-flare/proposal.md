# Firmware rebrand to FLARE

## Summary

Rename the user-facing firmware brand from NOSF to FLARE:
**Filament Lane Automation and Reload Engine**.

## Motivation

The new name should describe the firmware control layer rather than the whole
hardware module. FLARE covers lane automation, cutter-assisted workflows under
automation, sync, and RELOAD behavior without implying a full mechanical system.

## Scope

- Update firmware-facing documentation titles and descriptions to FLARE.
- Update compiled firmware version prefix and build artifact name to FLARE.
- Keep legacy host-tool and protocol surfaces compatible unless explicitly
  changed later.

## Non-goals

- Do not rename serial commands, events, or status fields.
- Do not rename Python helper files in this change.
- Do not change `NOSF_TUNE` marker semantics or state directory names.
