# Design

## Findings

Read `README.md`, `MANUAL.md`, `BEHAVIOR.md`, `KLIPPER.md`, `HARDWARE.md`,
`CONTEXT.md`, `openspec/config.yaml`, and current specs. The old NOSF name is
used both as firmware brand and as stable integration surface:

- Firmware brand text appears in docs, OpenSpec context, and `CONF_FW_VERSION`.
- Firmware build output is `nosf_controller`.
- Host helper filenames, Klipper examples, state paths, and marker protocol
  use `nosf_*` and `NOSF_TUNE`.

## Compatibility Boundary

Treat FLARE as the firmware brand and artifact identity. Keep integration names
that users may already have in Klipper configs and calibration state:

- Keep Python script filenames (`nosf_cmd.py`, `nosf_live_tuner.py`, etc.).
- Keep Klipper shell command examples using `CMD=nosf`.
- Keep marker protocol strings (`NOSF_TUNE`) and sidecar suffixes (`.nosf.*`).
- Keep analysis patch sections such as `[nosf_review]` and
  `[nosf_contributors]`.

## File Plan

### Firmware build/version

- `firmware/include/config.h`: change `CONF_FW_VERSION` to `FLARE_0.2.0` and
  update the board config comment.
- `firmware/CMakeLists.txt`: rename project/target from `nosf_controller` to
  `flare_controller`.
- `scripts/flash_nosf.sh`: prefer `flare_controller` artifacts and fall back to
  old `nosf_controller` artifacts so older build trees still flash.
- `BUILD_FLASH.md`: document new artifact names.

Risk: target rename can break flash workflow if script is not updated. Validate
with local build.

### Operator docs

- Update headings and prose where NOSF means firmware brand.
- Preserve helper filenames and protocol tokens.
- Add short compatibility notes so old `nosf_*` tool names do not look wrong.

Risk: blind replacement could rewrite stable protocol names. Check remaining
`NOSF` hits after edits and classify intentional legacy surfaces.

### OpenSpec docs

- Update durable specs where NOSF means firmware brand.
- Leave active implementation details and stable helper names intact where they
  refer to current scripts or marker strings.

Risk: active `mmu-mode` change references may become confusing. Update prose to
  FLARE while keeping command/script identifiers unchanged.

## Regression Impact

No motion behavior, persistence layout, runtime tunables, serial commands, or
events change. Affected flows are build/flash/version reporting and docs only.
Validate with script compile, regression shell, and firmware build.
