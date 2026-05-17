# Firmware rebrand to FLARE

## Summary

Complete the firmware rebrand to FLARE:
**Filament Lane Automation and Reload Engine**.

## Motivation

The new name should describe the firmware control layer rather than the whole
hardware module. FLARE covers lane automation, cutter-assisted workflows under
automation, sync, and RELOAD behavior without implying a full mechanical system.
The firmware is still active-development software, so the rename can be clean
and breaking: no old namespace needs to survive for compatibility.

## Name Meaning

FLARE expands to **Filament Lane Automation and Reload Engine**.

- **Filament** anchors the domain and safety boundary.
- **Lane Automation** covers preload, unload, swaps, cutter sequencing, and
  sensor-driven motion.
- **Reload** covers autonomous runout/failover behavior.
- **Engine** makes clear that this is the firmware/tooling layer, not the whole
  mechanical product.

## Scope

- Update firmware-facing documentation titles and descriptions to FLARE.
- Update compiled firmware version prefix and build artifact name to FLARE.
- Rename helper files, marker namespaces, sidecar suffixes, state paths, and
  Klipper examples so no previous brand references remain.
- Document why FLARE is firmware-scoped and why breaking namespace changes are
  acceptable before first stable use.

## Non-goals

- Do not rename serial commands, events, or status fields.
- Do not change motion behavior, persistence layout, or runtime tunables.
