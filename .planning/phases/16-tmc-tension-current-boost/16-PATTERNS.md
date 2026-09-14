# Phase 16: TMC Tension Current Boost - Pattern Map

**Mapped:** 2026-09-14  
**Files analyzed:** 18 (1 new, 17 modified)  
**Analogs found:** 18 / 18 (100% codebase analog match)

---

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `tests/host/test_tmc_boost.c` | test | request-response | `tests/host/test_tmc_recovery.c` | exact |
| `firmware/src/sync.c` | controller / service | event-driven | `firmware/src/sync.c` (lines 749-765, 1860-1940, 2466-2507) | exact |
| `firmware/include/sync.h` | header / config | request-response | `firmware/include/sync.h` (lines 15-40, 95-115) | exact |
| `firmware/src/motion.c` | service / driver | event-driven | `firmware/src/motion.c` (lines 275-281, 635-638) | exact |
| `firmware/include/motion.h` | header | request-response | `firmware/include/motion.h` (lines 40-70) | exact |
| `firmware/src/main.c` | config / model | initialization | `firmware/src/main.c` (lines 108, 180-205, 309-323) | exact |
| `firmware/include/controller_shared.h` | config / header | request-response | `firmware/include/controller_shared.h` (lines 212-215, 255-275) | exact |
| `firmware/include/settings_store.h` | config / model | transform | `firmware/include/settings_store.h` (lines 8-9, 70-87) | exact |
| `firmware/src/settings_store.c` | service / model | file-I/O / transform | `firmware/src/settings_store.c` (lines 480-545, 895-935) | exact |
| `firmware/src/protocol.c` | controller | request-response | `firmware/src/protocol.c` (lines 460-475, 990-1025, 1080-1115, 1285-1327) | exact |
| `firmware/src/protocol_status.c` | controller / utility | streaming / request-response | `firmware/src/protocol_status.c` (lines 100-125) | exact |
| `scripts/gen_config.py` | utility / transform | transform / file-I/O | `scripts/gen_config.py` (lines 115-125, 540-555) | exact |
| `config.ini.example` | config | file-I/O | `config.ini.example` (lines 18-27, 50-65, 207-227) | exact |
| `config.ini` | config | file-I/O | `config.ini` (lines 18-27, 50-65, 207-227) | exact |
| `scripts/flare_cmd.py` | utility / controller | request-response | `scripts/flare_cmd.py` (lines 60-125, 430-480) | exact |
| `scripts/flare_daemon.py` | service / middleware | streaming / request-response | `scripts/flare_daemon.py` (lines 858-950) | exact |
| `scripts/test_status_line_budget.py` | test / utility | transform | `scripts/test_status_line_budget.py` (lines 27-64, 140-175) | exact |
| `tests/host/CMakeLists.txt` | config / build | transform | `tests/host/CMakeLists.txt` (lines 109-131) | exact |
| `tests/host/sim_scenario.c` | test / plant | event-driven / streaming | `tests/host/sim_scenario.c` (lines 650-730) | exact |

---

## Pattern Assignments

### `tests/host/test_tmc_boost.c` (test, request-response)

**Analog:** `tests/host/test_tmc_recovery.c`

**Imports pattern** (`tests/host/test_tmc_recovery.c:5-18`):
```c
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "controller_shared.h"
#include "motion.h"
#include "settings_store.h"
#include "sim_fakes.h"
#include "sync.h"
#include "tmc2209.h"
#include "toolchange.h"
```

**Auth/Guard pattern** (Sim state initialization, `tests/host/test_tmc_recovery.c:30-36`):
```c
g_sim_event_count = 0;
sim_tmc_reset_counts();

g_lane_l1.task = TASK_IDLE;
g_lane_l2.task = TASK_IDLE;
g_tc_ctx.state = TC_IDLE;
g_boot_stabilizing = false;
```

**Core Pattern** (Step evaluation & assert check, `tests/host/test_tmc_recovery.c:104-126`):
```c
// Advance time / state and verify write counts and shadow invariants
int writes_before = sim_tmc_get_write_count(1);

// Inject condition and tick
sync_check_tension_boost(t);

int writes_after = sim_tmc_get_write_count(1);
assert(writes_after > writes_before);
assert(sync_is_tension_boost_active(1) == true);
assert(g_tmc_run_current_ma[0] == expected_base_ma);
```

**Error Handling / Assertion pattern** (Event logging query, `tests/host/test_tmc_recovery.c:19-26`):
```c
static bool event_logged(const char *expected) {
    for (int i = 0; i < g_sim_event_count; i++) {
        if (strstr(g_sim_events[i].text, expected) != NULL) {
            return true;
        }
    }
    return false;
}
```

**Testing pattern** (Main runner harness, `tests/host/test_tmc_recovery.c:228-237`):
```c
int main(void) {
    printf("=== TMC Tension Boost Unit Tests ===\n");
    test_boost_activation_and_hysteresis();
    test_boost_unconditional_exit_reset();
    test_boost_heartbeat_brownout_recovery();
    test_boost_prohibitions_and_clamps();
    printf("=== All TMC Tension Boost tests passed ===\n");
    return 0;
}
```

---

### `firmware/src/sync.c` (controller / service, event-driven)

**Analog:** `firmware/src/sync.c`

**Imports pattern** (`firmware/src/sync.c:1-25`):
```c
#include "sync.h"
#include "sync_internal.h"
#include "controller_shared.h"
#include "motion.h"
#include "protocol.h"
#include "settings_store.h"
```

**Auth/Guard pattern** (Sensor & state guard, `firmware/src/sync.c:2478-2485`):
```c
if (g_sync_state != SYNC_ACTIVE || g_buf_sensor_type != BUF_SENSOR_TYPE_P) {
    return;
}
lane_t *lane = lane_ptr(g_active_lane);
if (!lane)
    return;
```

**Core Pattern** (Hysteresis check & edge-triggered mutation, `firmware/src/sync.c:2485-2507`):
```c
void sync_check_tension_boost(uint32_t now_ms) {
    (void)now_ms;
    if (g_sync_state != SYNC_ACTIVE || g_buf_sensor_type != BUF_SENSOR_TYPE_P) {
        return;
    }
    lane_t *lane = lane_ptr(g_active_lane);
    if (!lane)
        return;
    int idx = lane_to_idx(g_active_lane);

    // Feature qualification: must be enabled and higher than baseline run current (D-05)
    int boost_ma = g_sync_tension_boost_irun[idx];
    if (boost_ma <= 0 || boost_ma <= g_tmc_run_current_ma[idx]) {
        return;
    }

    if (!g_sync_tension_boost_active[idx]) {
        // Activation edge: buffer pegged deep in tension
        if (g_buf_pos <= g_sync_tension_boost_on) {
            g_sync_tension_boost_active[idx] = true;
            tmc_apply_active_run_current(g_active_lane, boost_ma);
            char lane_s[4];
            snprintf(lane_s, sizeof(lane_s), "%d", g_active_lane);
            cmd_event("TMC:BOOST", lane_s);
        }
    } else {
        // Hysteresis release edge: buffer relaxed past off threshold
        if (g_buf_pos >= g_sync_tension_boost_off) {
            g_sync_tension_boost_active[idx] = false;
            tmc_apply_active_run_current(g_active_lane, g_tmc_run_current_ma[idx]);
            char lane_s[4];
            snprintf(lane_s, sizeof(lane_s), "%d", g_active_lane);
            cmd_event("TMC:NORMAL", lane_s);
        }
    }
}
```

**Error Handling / Unwind pattern** (`firmware/src/sync.c:749-767`):
```c
void sync_tension_boost_reset_lane(int lane_num) {
    int idx = lane_to_idx(lane_num);
    if (idx < 0 || idx >= NUM_LANES)
        return;
    if (g_sync_tension_boost_active[idx]) {
        g_sync_tension_boost_active[idx] = false;
        tmc_apply_active_run_current(lane_num, g_tmc_run_current_ma[idx]);
        char lane_s[4];
        snprintf(lane_s, sizeof(lane_s), "%d", lane_num);
        cmd_event("TMC:NORMAL", lane_s);
    }
}

void sync_tension_boost_reset_all(void) {
    for (int l = 1; l <= NUM_LANES; l++) {
        sync_tension_boost_reset_lane(l);
    }
}

// In sync_set_state(new_state):
if (g_sync_state == SYNC_ACTIVE && new_state != SYNC_ACTIVE) {
    sync_tension_boost_reset_all();
}
```

---

### `firmware/src/motion.c` (service / driver, event-driven)

**Analog:** `firmware/src/motion.c` and `firmware/src/main.c:309-323`

**Imports pattern** (`firmware/src/motion.c:1-20`):
```c
#include "motion.h"
#include "controller_shared.h"
#include "sync.h"
#include "tmc2209.h"
```

**Core Pattern** (TMC current apply & shadow synchronization, `firmware/src/motion.c:275-281` / `firmware/src/main.c:309-323`):
```c
void tmc_apply_active_run_current(int lane_num, int current_ma) {
    if (lane_num < 1 || lane_num > NUM_LANES)
        return;
    int idx = lane_to_idx(lane_num);
    lane_t *lane = lane_ptr(lane_num);
    if (!lane || !lane->tmc)
        return;

    // Hard hardware safety ceiling (Decision D-03, Constraint R1)
    int clamped_ma = clamp_i(current_ma, 0, 1200);

    // Apply over UART
    tmc_set_run_current_ma(lane->tmc, clamped_ma, g_tmc_hold_current_ma[idx]);

    // Update shadow registers for heartbeat and protocol consistency
    g_shadow_vsense[idx] = (clamped_ma <= TMC_VSENSE_THRESHOLD_MA);
    g_shadow_ihold_irun[idx] = build_ihold_irun_reg(
        clamped_ma, g_tmc_hold_current_ma[idx], g_shadow_vsense[idx]);
    g_shadow_ihold_irun_valid[idx] = true;
}
```

**Defense-in-depth unwind hook** (`firmware/src/motion.c:635-638`):
```c
void stop_all(void) {
    lane_stop(&g_lane_l1);
    lane_stop(&g_lane_l2);
    sync_tension_boost_reset_all();
}
```

---

### `firmware/src/settings_store.c` (service / model, file-I/O / transform)

**Analog:** `firmware/src/settings_store.c`

**Imports pattern** (`firmware/src/settings_store.c:1-25`):
```c
#include "settings_store.h"
#include "controller_shared.h"
#include "sync.h"
#include "tmc2209.h"
```

**Serialization pattern** (`firmware/src/settings_store.c:480-495`):
```c
tlv_emit(&w, TAG_SYNC_TENSION_BOOST_IRUN, sizeof(g_sync_tension_boost_irun), g_sync_tension_boost_irun);
tlv_emit_f32(&w, TAG_SYNC_TENSION_BOOST_ON, g_sync_tension_boost_on);
tlv_emit_f32(&w, TAG_SYNC_TENSION_BOOST_OFF, g_sync_tension_boost_off);
```

**Deserialization & Clamping pattern** (`firmware/src/settings_store.c:635-645, 920-930`):
```c
case TAG_SYNC_TENSION_BOOST_IRUN:
    if (len == sizeof(g_sync_tension_boost_irun))
        memcpy(g_sync_tension_boost_irun, val, sizeof(g_sync_tension_boost_irun));
    break;
case TAG_SYNC_TENSION_BOOST_ON:
    if (len == sizeof(float))
        memcpy(&g_sync_tension_boost_on, val, sizeof(float));
    break;
case TAG_SYNC_TENSION_BOOST_OFF:
    if (len == sizeof(float))
        memcpy(&g_sync_tension_boost_off, val, sizeof(float));
    break;
```

**Brownout recovery synchronization pattern** (`firmware/src/settings_store.c:534-541`):
```c
// Query active current: preserves boost if active, restores baseline otherwise
int active_run_ma = sync_get_active_run_current_ma(lane);
tmc_set_run_current_ma(tmc, active_run_ma, g_tmc_hold_current_ma[idx]);

// Synchronize shadow state for protocol reporting
g_shadow_vsense[idx] = (active_run_ma <= TMC_VSENSE_THRESHOLD_MA);
g_shadow_ihold_irun[idx] = build_ihold_irun_reg(
    active_run_ma, g_tmc_hold_current_ma[idx], g_shadow_vsense[idx]);
g_shadow_ihold_irun_valid[idx] = true;
```

---

### `firmware/src/protocol.c` (controller, request-response)

**Analog:** `firmware/src/protocol.c`

**GET query pattern** (`firmware/src/protocol.c:461-465, 553-555`):
```c
else if (!strcmp(param, "SYNC_TENSION_BOOST_IRUN"))
    snprintf(out, out_len, "SYNC_TENSION_BOOST_IRUN:%d", g_sync_tension_boost_irun[idx]);
else if (!strcmp(param, "SYNC_TENSION_BOOST_ON"))
    snprintf(out, out_len, "SYNC_TENSION_BOOST_ON:%.2f", (double)g_sync_tension_boost_on);
else if (!strcmp(param, "SYNC_TENSION_BOOST_OFF"))
    snprintf(out, out_len, "SYNC_TENSION_BOOST_OFF:%.2f", (double)g_sync_tension_boost_off);
```

**SET command & validation pattern** (`firmware/src/protocol.c:995-1010, 1080-1115, 1285-1327`):
```c
// Lane-aware boost current (clamped <= 1200 mA)
if (!strcmp(base_param, "SYNC_TENSION_BOOST_IRUN")) {
    SET_LANE({ g_sync_tension_boost_irun[idx] = clamp_i(iv, 0, 1200); });
}

// In cmd_set_buffer_params:
else if (!strcmp(base_param, "SYNC_TENSION_BOOST_ON")) {
    if (fv >= g_sync_tension_boost_off) {
        cmd_reply("ER", "INVALID_PARAM");
        return CMD_SET_REPLIED;
    }
    g_sync_tension_boost_on = clamp_f(fv, -1.0f, -0.05f);
}
else if (!strcmp(base_param, "SYNC_TENSION_BOOST_OFF")) {
    if (fv <= g_sync_tension_boost_on) {
        cmd_reply("ER", "INVALID_PARAM");
        return CMD_SET_REPLIED;
    }
    g_sync_tension_boost_off = clamp_f(fv, -0.95f, 0.0f);
}
```

---

### `firmware/src/protocol_status.c` (controller / utility, streaming)

**Analog:** `firmware/src/protocol_status.c`

**Status line extension pattern** (`firmware/src/protocol_status.c:104-120`):
```c
char tb_char = sync_is_tension_boost_active(g_active_lane) ? '1' : '0';

int tail_len = snprintf(
    b + blen, sizeof(b) - (size_t)blen,
    ",RT:%.2f,TT:%u,TM:%d,ARM:%c,CT:%u,SK:%u,CF:%.2f,ES:%.2f"
    ",TPX:%d,CB:%d,BPV:%d,MK:%u:%s"
    ",SYNC_REFILL_MM:%d,SYNC_RELIEVE_MM:%d,TF:%.1f,FL_RATE:%.1f,UL_RATE:%.1f"
    ",PR:%c,TB:%c",
    ...,
    (char)('0' + sync_type_p_probe_state()),
    tb_char);
```

---

### `scripts/flare_daemon.py` (service / middleware, streaming)

**Analog:** `scripts/flare_daemon.py`

**Status line token parsing** (`scripts/flare_daemon.py:890-948`):
```python
elif key == "TB":
    new_data["tension_boost"] = bool(int(val))
```

---

### `scripts/flare_cmd.py` (utility / controller, request-response)

**Analog:** `scripts/flare_cmd.py`

**DUMP_PARAMS addition** (`scripts/flare_cmd.py:115-125`):
```python
    # --- Tension Boost ---
    ("SYNC_TENSION_BOOST_IRUN", "sync_tension_boost_irun", True),
    ("SYNC_TENSION_BOOST_ON",   "sync_tension_boost_on",   False),
    ("SYNC_TENSION_BOOST_OFF",  "sync_tension_boost_off",  False),
```

**Poll status token parsing** (`scripts/flare_cmd.py:460-478`):
```python
line_parts.append(f"TB:{1 if status.get('tension_boost') else 0}")
```

---

### `scripts/gen_config.py` (utility / transform, transform / file-I/O)

**Analog:** `scripts/gen_config.py`

**DEFAULTS map addition** (`scripts/gen_config.py:115-125`):
```python
    "sync_tension_boost_irun": "0",       # mA boost during deep tension (0 = disabled, max 1200)
    "sync_tension_boost_on": "-0.50",     # buffer position to engage tension boost (Type-P only)
    "sync_tension_boost_off": "-0.30",    # buffer position to release tension boost via hysteresis
```

**Header emission** (`scripts/gen_config.py:545-555`):
```python
    f"#define CONF_SYNC_TENSION_BOOST_ON {get_float('sync_tension_boost_on')}f",
    f"#define CONF_SYNC_TENSION_BOOST_OFF {get_float('sync_tension_boost_off')}f",
    f"#define CONF_L1_SYNC_TENSION_BOOST_IRUN {get_int('sync_tension_boost_irun', lane=1)}",
    f"#define CONF_L2_SYNC_TENSION_BOOST_IRUN {get_int('sync_tension_boost_irun', lane=2)}",
```

---

### `tests/host/sim_scenario.c` (test / plant, event-driven)

**Analog:** `tests/host/sim_scenario.c` (`sem_psf_mm_trip`, lines 700-730)

**Scenario definition pattern** (`tests/host/sim_scenario.c:700-730`):
```c
{
    .name = "sem_psf_tension_boost",
    .demand = {.kind = DEMAND_STEP_UP, .level_mm_s = 10.0f, .level2_mm_s = 36.0f, .t1_ms = 2000},
    .demand_gain = {.bp = {{.t_ms = 0, .value = 1.0f},
                           {.t_ms = 2000, .value = 1.5f},
                           {.t_ms = 3500, .value = 1.0f}},
                    .count = 3},
    .active_lane = 1,
    .start_sync_active = true,
    .buf_max_travel_override = 16,
    .tick_ceiling = 2000,
    .tick_ceiling_reason = "observe boost engage when pegged <= -0.50 and release >= -0.30",
    .type_specific = true,
},
```

---

## Shared Patterns

### 1. Multi-Layer Unconditional State Unwind (Defense-in-Depth)
**Source:** `firmware/src/sync.c:749-767` and `firmware/src/motion.c:635-638`  
**Apply to:** All state transitions out of `SYNC_ACTIVE` (`sync_set_state`, `sync_disable`, `stop_all`)  
```c
// Centralized unwind helper
void sync_tension_boost_reset_lane(int lane_num) {
    int idx = lane_to_idx(lane_num);
    if (g_sync_tension_boost_active[idx]) {
        g_sync_tension_boost_active[idx] = false;
        tmc_apply_active_run_current(lane_num, g_tmc_run_current_ma[idx]);
        char lane_s[4];
        snprintf(lane_s, sizeof(lane_s), "%d", lane_num);
        cmd_event("TMC:NORMAL", lane_s);
    }
}
```

### 2. Edge-Triggered UART Mutation & Atomic Shadow State Sync
**Source:** `firmware/src/motion.c:275-281`, `firmware/src/main.c:309-323`, `firmware/src/settings_store.c:534-541`  
**Apply to:** TMC driver runtime current updates and heartbeat recovery  
```c
// Clamp to 1200 mA hardware ceiling, write UART, update shadow registers
int clamped_ma = clamp_i(current_ma, 0, 1200);
tmc_set_run_current_ma(lane->tmc, clamped_ma, g_tmc_hold_current_ma[idx]);
g_shadow_vsense[idx] = (clamped_ma <= TMC_VSENSE_THRESHOLD_MA);
g_shadow_ihold_irun[idx] = build_ihold_irun_reg(clamped_ma, g_tmc_hold_current_ma[idx], g_shadow_vsense[idx]);
g_shadow_ihold_irun_valid[idx] = true;
```

### 3. Budget-Preserving Status Line Formatting (`TB:%c`)
**Source:** `firmware/src/protocol_status.c:104-120` and `scripts/test_status_line_budget.py`  
**Apply to:** Status line extensions under tight character limits (759/760 chars worst-case)  
```c
char tb_char = sync_is_tension_boost_active(g_active_lane) ? '1' : '0';
snprintf(b + blen, sizeof(b) - (size_t)blen, "...,PR:%c,TB:%c", ..., probe_char, tb_char);
```

### 4. Safe Parameter Clamping & Order Validation
**Source:** `firmware/src/protocol.c:1040-1115`  
**Apply to:** Runtime configuration and SET validation for hysteresis thresholds  
```c
// Enforce BOOST_ON < BOOST_OFF invariant, reject with ER:INVALID_PARAM
if (fv >= g_sync_tension_boost_off) {
    cmd_reply("ER", "INVALID_PARAM");
    return CMD_SET_REPLIED;
}
```

---

## No Analog Found

*None. All 18 analyzed files map directly to existing, git-tracked patterns in the codebase.*

---

## Metadata

**Analog search scope:**
- `firmware/src/*.c`
- `firmware/include/*.h`
- `scripts/*.py`
- `tests/host/*.c`
- `tests/host/CMakeLists.txt`

**Files scanned:** 38 files  
**Tracked source verification:** Verified via `git ls-files` (#3645)  
**Pattern extraction date:** 2026-09-14  
