# Tasks: Flash Ping-Pong Atomic Persistence

- [ ] Define dual sector offsets `SETTINGS_FLASH_OFFSET_A` and `SETTINGS_FLASH_OFFSET_B` in `firmware/src/settings_store.c`.
- [ ] Add sequence counter and commit header to flash payload.
- [ ] Update `settings_load()` to scan both sectors and select the newest valid record.
- [ ] Update `settings_save()` to target the inactive sector and write atomically.
- [ ] Add host sim unit tests simulating interrupted flash writes and verifying recovery of prior valid sector.
