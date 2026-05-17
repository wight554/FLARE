# Firmware rebrand to FLARE

## Summary

Complete the firmware rebrand to FLARE:
**Filament Lane Automation and Reload Engine**.

## Motivation

The new name should describe the firmware control layer rather than the whole
hardware module. FLARE covers lane automation, cutter-assisted workflows under
automation, sync, and RELOAD behavior without implying a full mechanical system.

## Scope

- Update firmware-facing documentation titles and descriptions to FLARE.
- Update compiled firmware version prefix and build artifact name to FLARE.
- Rename helper files, marker namespaces, sidecar suffixes, state paths, and
  Klipper examples so no previous brand references remain.

## Non-goals

- Do not rename serial commands, events, or status fields.
- Do not change motion behavior, persistence layout, or runtime tunables.
