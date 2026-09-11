# Phase 10: TMC2209 Register Heartbeat & Auto-Recovery — Specification

**Goal**: Continuous idle-loop TMC2209 register integrity verification, deterministic brownout recovery, zero motion-jitter guarantee, and host event escalation.
**Scope**: Firmware (`tmc2209.c`, `tmc2209.h`, `main.c`, `protocol.c`, `protocol_status.c`, `settings_store.c`), host simulation (`tests/host/sim_fakes.c`, `tests/host/test_tmc_recovery.c`), and host tooling (`scripts/flare_daemon.py`).

---

## 1. Sentinel Register Selection

### Requirements
1. **Sentinel Register**: Use `CHOPCONF` (`0x6C`) as the primary heartbeat sentinel.
2. **Integrity Rule**:
   - Compare readback `CHOPCONF` directly against cached `tmc->chopconf` stored in firmware memory.
   - Silicon reset default (`0x10000053`) diverges reliably from configured microsteps/timing (e.g. 16/32 microsteps with `mres=4` or `3`, tuned `toff`, `tbl`, `hstrt`, `hend`, and `vsense=1`).
   - Zero false positives from dynamic counters (`IFCNT`) or volatile status bits (`DRV_STATUS`, `GSTAT`).

---

## 2. Motion Lockout & Polling Cadence

### Requirements
1. **Strict Idle Lockout**:
   - Polling SHALL execute ONLY when `controller_activity_in_progress()` returns `false`.
   - Never initiate UART read transactions when any lane motor is active, toolchange is in progress, sync buffer control is driving, cutter is active, or boot stabilization is settling.
2. **Cadence**:
   - Heartbeat tick interval: 1000 ms.
   - Alternating lane inspection: Lane 1 polled at $t = 0\text{s}$, Lane 2 at $t = 1\text{s}$, Lane 1 at $t = 2\text{s}$, etc.
   - Maximum loop delay per check bounded by UART timeouts. In normal operation, a successful frame exchange takes ~3 ms.

---

## 3. Recovery Escalation & Error Handling

### Requirements
1. **Recovery Trigger**:
   - Triggered when readback `CHOPCONF != tmc->chopconf` or when UART read fails (CRC error or frame timeout).
2. **Re-Apply Policy**:
   - Attempt full lane re-configuration (`sync_tmc_settings(lane)` including `PWMCONF`, `STEALTHCHOP`, `CHOPCONF`, and run/hold currents).
   - Up to 3 re-apply attempts with a 50 ms backoff between attempts.
3. **Success / Recovery**:
   - On successful re-apply verified by matching readback `CHOPCONF`:
     - Clear fault state for that lane (`health = 1`).
     - Emit event: `EV:TMC:RESTORED:<lane>`.
4. **Escalation / Persistent Fault**:
   - If all 3 attempts fail to restore and verify `CHOPCONF`:
     - Mark lane as faulty (`health = 0`).
     - Immediately halt all motion (`stop_all()`).
     - Emit event: `EV:TMC:FAULT:<lane>:COMM_FAIL`.
     - Host daemon mirrors event to Klipper to execute print `PAUSE`.

---

## 4. Telemetry & Event Contract

### Requirements
1. **Wire Protocol Events**:
   - `EV:TMC:RESTORED:<lane>` on successful re-apply and verification.
   - `EV:TMC:FAULT:<lane>:<reason>` on persistent communication or configuration failure.
2. **Status Telemetry (`ST:` / `?:`)**:
   - Add field `TMC:<l1_health><l2_health>` to status dump (e.g. `TMC:11` when both healthy, `TMC:01` if L1 faulty, `TMC:10` if L2 faulty).
3. **Daemon Mirroring**:
   - `scripts/flare_daemon.py` parses `TMC` field and tracks `tmc_health` in status cache.

---

## 5. Host Simulation & Test Verification

### Requirements
1. **Stateful Host TMC Mock**:
   - Extend `tests/host/sim_fakes.c` with per-instance TMC register storage (`chopconf`, etc.).
   - Support test fault injection:
     - `sim_tmc_inject_brownout(int lane)`: Resets mock `chopconf` to silicon default `0x10000053`.
     - `sim_tmc_set_comm_fail(int lane, bool fail)`: Forces `tmc_read()` failure.
2. **Dedicated Unit Test Suite**:
   - Create `tests/host/test_tmc_recovery.c` verifying:
     - Normal idle tick produces zero spurious re-applies.
     - Motion active completely suppresses heartbeat reads.
     - Brownout injection triggers re-apply and emits `EV:TMC:RESTORED:<lane>`.
     - Persistent comm failure halts motion and emits `EV:TMC:FAULT:<lane>:COMM_FAIL`.
