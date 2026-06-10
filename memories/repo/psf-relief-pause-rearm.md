# psf-relief-pause-rearm (archived 2026-06-04)

- Type-P +1.0 tension rail is ambiguous (home vs starvation); resolve via (mode × filament_present), not tension value alone.
- Sync safety stays firmware-local: NO host-pause actuator — Happy-Hare `cmd_MMU_PAUSE` is a stub; firmware relief/fault path must not depend on host pausing.
- Spec: `psf-type-p-sensor` (relief pause/rearm scenarios). Rig-validated at archive.
