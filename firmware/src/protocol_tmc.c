#include "controller_shared.h"
#include "protocol.h"
#include "protocol_internal.h"
#include "settings_store.h"
#include "tmc2209.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool cmd_handle_tmc_advanced(const char *cmd, const char *p, uint32_t now_ms) {
    (void)now_ms;
    if (!strcmp(cmd, "CA")) {
        int ln = 0;
        int ma = 0;
        if (sscanf(p, "%d:%d", &ln, &ma) == 2 && (ln == 1 || ln == 2) && ma >= 0 && ma <= 2000) {
            tmc_t *t = (ln == 1) ? &g_tmc_l1 : &g_tmc_l2;
            int idx = lane_to_idx(ln);
            if (tmc_set_run_current_ma(t, ma, TMC_HOLD_CURRENT_MA[idx])) {
                TMC_RUN_CURRENT_MA[idx] = ma;
                g_shadow_vsense[idx] = (ma <= 980);
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
            reg <= 127) {
            tmc_t *t = (ln == 1) ? &g_tmc_l1 : &g_tmc_l2;
            if (tmc_write(t, (uint8_t)reg, val)) {
                int idx = lane_to_idx(ln);
                if (reg == TMC_REG_IHOLD_IRUN) {
                    g_shadow_ihold_irun[idx] = val;
                    g_shadow_ihold_irun_valid[idx] = true;
                    sync_currents_from_ihold_irun(ln, val);
                } else if (reg == TMC_REG_CHOPCONF) {
                    g_shadow_vsense[idx] = ((val >> 17) & 0x1u) != 0u;
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
        if (sscanf(p, "%d:%d", &ln, &reg) == 2 && (ln == 1 || ln == 2) && reg >= 0 && reg <= 127) {
            int idx = lane_to_idx(ln);
            if (reg == TMC_REG_IHOLD_IRUN && g_shadow_ihold_irun_valid[idx]) {
                char out[32];
                snprintf(out, sizeof(out), "%d:%d:0x%08X", ln, reg,
                         (unsigned int)g_shadow_ihold_irun[idx]);
                cmd_reply("OK", out);
                return true;
            }
            tmc_t *t = (ln == 1) ? &g_tmc_l1 : &g_tmc_l2;
            uint32_t val = 0;
            if (tmc_read(t, (uint8_t)reg, &val)) {
                char out[32];
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
            char out[128];
            int pos = snprintf(out, sizeof(out), "%d:", ln);
            for (uint8_t a = 0; a < 4; a++) {
                probe.addr = a;
                uint8_t buf[8] = {0};
                int n = tmc_read_raw(&probe, TMC_REG_GCONF, buf);
                pos += snprintf(out + pos, sizeof(out) - pos,
                                "A%u:N=%d:%02X%02X%02X%02X%02X%02X%02X%02X ", a, n, buf[0], buf[1],
                                buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
            }
            cmd_reply("OK", out);
        }
        return true;
    }
    return false;
}
