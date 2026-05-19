## 1. Firmware Fix

- [ ] 1.1 Add `sync_disable(true)` before `return` in the NO_FILAMENT guard in `reload_trigger()` (`firmware/src/toolchange.c` ~line 89)

## 2. Build & Validate

- [ ] 2.1 Build firmware: `ninja -C build_local`
- [ ] 2.2 On-hardware: trigger runout with both lanes empty; verify `RELOAD:FAULT NO_FILAMENT` event emitted and `?:` shows `SM:0` without manual `ST`
