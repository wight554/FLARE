# relay-fallback-only (archived 2026-05-21)

- Deleted type-D relay confident-estimator path after A/B verdict (see `relay-confident-path-keep-or-remove`); relay now fallback-only law in `sync_relay.c`, spec `relay-fallback-only`.
- Related gotcha: non-zero `relay_min_flip_mm` freezes type-D relay when COMPRESSION feed = SYNC_MIN — deadlock; reverted to 0.0 default.
- Settings persistence survives reflash — after firmware flash, GET/SET to confirm runtime values; don't assume defaults.
