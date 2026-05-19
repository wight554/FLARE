## 1. Firmware Fix

- [x] 1.1 Add `sync_disable(true)` before `return` in the NO_FILAMENT guard in `reload_trigger()` (`firmware/src/toolchange.c` ~line 89)
  - 2026-05-20: Added hard sync disable before `RELOAD:FAULT NO_FILAMENT` event.

## 2. Build & Validate

- [x] 2.1 Build firmware: `ninja -C build_local`
  - 2026-05-20: Build passed.
- [ ] 2.2 On-hardware: trigger runout with both lanes empty; verify `RELOAD:FAULT NO_FILAMENT` event emitted and `?:` shows `SM:0` without manual `ST`
