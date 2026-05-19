## 1. Hardware prerequisite (blocked)

- [ ] 1.1 Acquire / build an analog buffer rig (`BUF_SENSOR_TYPE != 0`)
  capable of exercising the type-P feed path. **BLOCKER for all below.**

## 2. Resolve carried type-P inverted-polarity items (on rig only)

- [ ] 2.1 Audit #6 — `sync_compression_floor_sps()` (`sync.c:385-386`,
  applied `1655-1657`, gated `BUF_SENSOR_TYPE != 0`): confirm on rig that
  it force-raises feed FLOOR during `BUF_COMPRESSION` (=full) = inverted;
  fix so it does not fight drain. Validate on the analog rig.
- [ ] 2.2 Audit #7 — `compression_recovery` / collapse: same inverted
  bucket; verify + correct on rig.
- [ ] 2.3 H2 feed-trim comment (`sync.c:1499-1517`, dead under relay
  override): verify intended type-P behavior on rig; correct comment/code.
- [ ] 2.4 Regression: type-D relay path unchanged (re-run relay
  steady-state check); type-P behavior validated on rig.

## 3. Closeout

- [ ] 3.1 `cmake --build build_local`; `openspec validate
  pending-analog-rig --strict`; commit.
