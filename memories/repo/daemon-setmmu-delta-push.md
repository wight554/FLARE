# daemon-setmmu-delta-push (archived 2026-06-05)

- Klipper gcode lock starves SET_MMU mirror: synchronous `mmu.py` commands hold the gcode lock, so daemon-pushed state (`gate_sensor_active` etc.) freezes during command execution.
- Never poll Klipper-mirrored MMU state mid-command — read daemon `/status` HTTP endpoint directly for live values.
- Spec area: `klipper-mmu-config`, `klipper-integration`; daemon delta-push is the authoritative state path.
