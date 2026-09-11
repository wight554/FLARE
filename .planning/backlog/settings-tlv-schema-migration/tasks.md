# Tasks: Settings TLV Schema Migration

- [ ] Design lightweight TLV record structure (16-bit tag, 16-bit length, payload).
- [ ] Implement TLV encoder and decoder in `settings_store.c`.
- [ ] Provide backward-compatible import for legacy version `61u` struct on first boot.
- [ ] Update `settings_save()` to serialize active tunables as TLV streams.
- [ ] Update `settings_load()` to populate runtime globals from matching tags, leaving unconfigured keys at defaults.
- [ ] Add regression tests verifying schema upgrades without loss of existing keys.
