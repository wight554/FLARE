# Tasks: TMC Register Heartbeat and Recovery

- [ ] Implement `tmc_verify_configuration(uint8_t lane_idx)` in `tmc2209.c`.
- [ ] Add background watchdog-safe periodic poll in `main.c` (idle loop only, avoiding active motion timing jitter).
- [ ] On mismatch, re-execute `tmc_apply_settings()` and emit warning event `EV:TMC:RESTORED`.
- [ ] Add simulated brownout test case in `tests/host/`.
