# Design

## Findings

Read `README.md`, `MANUAL.md`, `BEHAVIOR.md`, `KLIPPER.md`, `HARDWARE.md`,
`CONTEXT.md`, `openspec/config.yaml`, and current specs. The previous brand was
used both as firmware brand and as integration surface:

- Firmware brand text appears in docs, OpenSpec context, and `CONF_FW_VERSION`.
- Firmware build output already moved to `flare_controller`.
- Host helper filenames, Klipper examples, state paths, and marker protocol
  need to use `flare_*` and `FLARE_TUNE`.

## Rebrand Boundary

Treat FLARE as the firmware brand, artifact identity, and host-tool namespace:

- Rename Python script filenames (`flare_cmd.py`, `flare_live_tuner.py`, etc.).
- Rename Klipper shell command examples to `CMD=flare`.
- Rename marker protocol strings to `FLARE_TUNE` and sidecar suffixes to
  `.flare.*`.
- Rename analysis patch sections to `[flare_review]` and
  `[flare_contributors]`.

## File Plan

### Firmware build/version

- `firmware/include/config.h`: change `CONF_FW_VERSION` to `FLARE_0.2.0` and
  update the board config comment.
- `firmware/CMakeLists.txt`: use the `flare_controller` project/target.
- `scripts/flash_flare.sh`: use only `flare_controller` artifacts.
- `BUILD_FLASH.md`: document new artifact names.

Risk: target rename can break flash workflow if script is not updated. Validate
with local build.

### Operator docs

- Update headings and prose where FLARE means firmware brand.
- Rename helper filenames, protocol markers, sidecar suffixes, state dirs, and
  Klipper examples.

Risk: broad replacement can create stale prose. Check for any old-brand hits
after edits and read the surrounding docs.

### OpenSpec docs

- Update durable specs where FLARE means firmware brand.
- Update active implementation details to current script and marker names.

Risk: active `mmu-mode` change references may become confusing. Update prose to
  FLARE with current command/script identifiers.

## Regression Impact

No motion behavior, persistence layout, runtime tunables, serial commands, or
events change. Affected flows are build/flash/version reporting and docs only.
Validate with script compile, regression shell, and firmware build.
