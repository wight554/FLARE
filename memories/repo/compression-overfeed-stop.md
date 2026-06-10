# compression-overfeed-stop (archived 2026-05-22)

- Type-D 2-switch buffer has NO demand signal between switch crossings; mid-band estimator fix FAILED + reverted — fix compression at switch crossings instead.
- Firmware need was minimal: 1-line COMPRESSION true-stop in sync feed law.
- Purge grind/jam root cause was NOT firmware: Klipper purge macro ran G90 (absolute E) and retracted into buffer; fix = M83 in macro. Check macro E-mode BEFORE suspecting firmware.
- Lesson: when host macros touch extruder, audit their G90/M83 state before firmware changes.
