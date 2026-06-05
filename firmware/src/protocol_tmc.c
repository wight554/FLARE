/// @file protocol_tmc.c
/// @brief Advanced TMC2209 serial commands: register-level read/write and current
///        configuration exposed over the USB-CDC protocol for tuning/diagnostics.
/// @details Thin command layer over the tmc2209.c UART driver. See MANUAL.md TMC
///          commands; driver internals in tmc2209.c.

#include "controller_shared.h"
#include "protocol.h"
#include "protocol_internal.h"
#include "settings_store.h"
#include "tmc2209.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TMC_CURRENT_MAX_MA = 2000,
    TMC_VSENSE_THRESHOLD_MA = 980,
    TMC_REGISTER_MAX = 127,
    TMC_CHOPCONF_VSENSE_BIT = 17,
    TMC_SHORT_REPLY_MAX = 32,
    TMC_RAW_REPLY_MAX = 128,
    TMC_UART_ADDR_COUNT = 4,
    RAW_REPLY_SYNC_IDX = 0,
    RAW_REPLY_MASTER_IDX = 1,
    RAW_REPLY_REG_IDX = 2,
    RAW_REPLY_DATA3_IDX = 3,
    RAW_REPLY_DATA2_IDX = 4,
    RAW_REPLY_DATA1_IDX = 5,
    RAW_REPLY_DATA0_IDX = 6,
    RAW_REPLY_CRC_IDX = 7,
};

bool cmd_handle_tmc_advanced(const char *cmd, const char *p, uint32_t now_ms) {
    (void)now_ms;
    if (!strcmp(cmd, "CA")) {
        int ln = 0;
        int ma = 0;
        if (sscanf(p, "%d:%d", &ln, &ma) == 2 && (ln == 1 || ln == 2) && ma >= 0 &&
            ma <= TMC_CURRENT_MAX_MA) {
            tmc_t *t = (ln == 1) ? &g_tmc_l1 : &g_tmc_l2;
            int idx = lane_to_idx(ln);
            if (tmc_set_run_current_ma(t, ma, TMC_HOLD_CURRENT_MA[idx])) {
                TMC_RUN_CURRENT_MA[idx] = ma;
                g_shadow_vsense[idx] = (ma <= TMC_VSENSE_THRESHOLD_MA);
                g_shadow_ihold_irun[idx] = build_ihold_irun_reg(
                    TMC_RUN_CURRENT_MA[idx], TMC_HOLD_CURRENT_MA[idx], g_shadow_vsense[idx]);
                g_shadow_ihold_irun_valid[idx] = true;
                cmd_reply("OK", NULL);
            } else {
                cmd_reply("ER", "CA:NO_RESPONSE");
            }
        } else {
            cmd_reply("ER", "ARG");
        }
        return true;
    } else if (!strcmp(cmd, "TW")) {
        int ln, reg;
        uint32_t val;
        if (sscanf(p, "%d:%d:%i", &ln, &reg, &val) == 3 && (ln == 1 || ln == 2) && reg >= 0 &&
            reg <= TMC_REGISTER_MAX) {
            tmc_t *t = (ln == 1) ? &g_tmc_l1 : &g_tmc_l2;
            if (tmc_write(t, (uint8_t)reg, val)) {
                int idx = lane_to_idx(ln);
                if (reg == TMC_REG_IHOLD_IRUN) {
                    g_shadow_ihold_irun[idx] = val;
                    g_shadow_ihold_irun_valid[idx] = true;
                    sync_currents_from_ihold_irun(ln, val);
                } else if (reg == TMC_REG_CHOPCONF) {
                    g_shadow_vsense[idx] = ((val >> TMC_CHOPCONF_VSENSE_BIT) & 0x1u) != 0u;
                    if (g_shadow_ihold_irun_valid[idx]) {
                        sync_currents_from_ihold_irun(ln, g_shadow_ihold_irun[idx]);
                    }
                }
                cmd_reply("OK", NULL);
            } else {
                cmd_reply("ER", "TW:FAILED");
            }
        } else {
            cmd_reply("ER", "ARG");
        }
        return true;
    } else if (!strcmp(cmd, "TR")) {
        int ln, reg;
        if (sscanf(p, "%d:%d", &ln, &reg) == 2 && (ln == 1 || ln == 2) && reg >= 0 &&
            reg <= TMC_REGISTER_MAX) {
            int idx = lane_to_idx(ln);
            if (reg == TMC_REG_IHOLD_IRUN && g_shadow_ihold_irun_valid[idx]) {
                char out[TMC_SHORT_REPLY_MAX];
                snprintf(out, sizeof(out), "%d:%d:0x%08X", ln, reg,
                         (unsigned int)g_shadow_ihold_irun[idx]);
                cmd_reply("OK", out);
                return true;
            }
            tmc_t *t = (ln == 1) ? &g_tmc_l1 : &g_tmc_l2;
            uint32_t val = 0;
            if (tmc_read(t, (uint8_t)reg, &val)) {
                char out[TMC_SHORT_REPLY_MAX];
                snprintf(out, sizeof(out), "%d:%d:0x%08X", ln, reg, (unsigned int)val);
                cmd_reply("OK", out);
            } else {
                cmd_reply("ER", "TR:NO_RESPONSE");
            }
        } else {
            cmd_reply("ER", "ARG");
        }
        return true;
    } else if (!strcmp(cmd, "RR")) {
        int ln = atoi(p);
        if (ln != 1 && ln != 2) {
            cmd_reply("ER", "ARG");
        } else {
            tmc_t probe = (ln == 1) ? g_tmc_l1 : g_tmc_l2;
            char out[TMC_RAW_REPLY_MAX];
            int pos = snprintf(out, sizeof(out), "%d:", ln);
            for (int addr = 0; addr < TMC_UART_ADDR_COUNT; addr++) {
                probe.addr = (uint8_t)addr;
                uint8_t buf[TMC_RAW_REPLY_LEN] = {0};
                int n = tmc_read_raw(&probe, TMC_REG_GCONF, buf);
                pos += snprintf(out + pos, sizeof(out) - pos,
                                "A%u:N=%d:%02X%02X%02X%02X%02X%02X%02X%02X ", (unsigned int)addr, n,
                                buf[0], buf[1], buf[RAW_REPLY_REG_IDX], buf[RAW_REPLY_DATA3_IDX],
                                buf[RAW_REPLY_DATA2_IDX], buf[RAW_REPLY_DATA1_IDX],
                                buf[RAW_REPLY_DATA0_IDX], buf[RAW_REPLY_CRC_IDX]);
            }
            cmd_reply("OK", out);
        }
        return true;
    }
    return false;
}
