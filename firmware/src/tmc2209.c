#include "tmc2209.h"
#include "config.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "tmc_uart.pio.h"
#include <math.h>

#define TMC_BAUD 40000u

enum {
    TMC_PIO_CACHE_BLOCKS = 2,
    TMC_CRC_BITS_PER_BYTE = 8,
    TMC_CRC_TOP_BIT_SHIFT = 7,
    TMC_CRC_POLYNOMIAL = 0x07,
    TMC_UART_SYNC = 0x05,
    TMC_WRITE_FLAG = 0x80,
    TMC_READ_REG_MASK = 0x7F,
    TMC_BYTE_MASK = 0xFF,
    TMC_WRITE_SYNC_IDX = 0,
    TMC_WRITE_ADDR_IDX = 1,
    TMC_WRITE_REG_IDX = 2,
    TMC_WRITE_DATA3_IDX = 3,
    TMC_WRITE_DATA2_IDX = 4,
    TMC_WRITE_DATA1_IDX = 5,
    TMC_WRITE_DATA0_IDX = 6,
    TMC_WRITE_CRC_IDX = 7,
    TMC_READ_REQUEST_LEN = 4,
    TMC_REPLY_ATTEMPTS = 2,
    TMC_UART_SEND_TIMEOUT_US = 2000,
    TMC_INTERFRAME_GAP_US = 30,
    TMC_ECHO_SETTLE_US = 10,
    TMC_REPLY_TIMEOUT_US = 5000,
    TMC_DATA_BYTE_SHIFT = 8,
    TMC_DATA3_SHIFT = 24,
    TMC_DATA2_SHIFT = 16,
    TMC_DATA1_SHIFT = 8,
    TMC_CURRENT_SCALE = 32,
    TMC_CS_MAX = 31,
    TMC_VSENSE_THRESHOLD_MA = 980,
    TMC_CHOPCONF_VSENSE_BIT = 17,
    TMC_IHOLDDELAY = 8u,
    TMC_IHOLDDELAY_SHIFT = 16,
    TMC_MRES_256 = 0,
    TMC_MRES_128 = 1,
    TMC_MRES_64 = 2,
    TMC_MRES_32 = 3,
    TMC_MRES_16 = 4,
    TMC_MRES_8 = 5,
    TMC_MRES_4 = 6,
    TMC_MRES_2 = 7,
    TMC_MRES_1 = 8,
    TMC_TOFF_MASK = 0x0F,
    TMC_TBL_MASK = 0x03,
    TMC_HSTRT_MASK = 0x07,
    TMC_HEND_MASK = 0x0F,
    TMC_CHOPCONF_HSTRT_SHIFT = 4,
    TMC_CHOPCONF_HEND_SHIFT = 7,
    TMC_CHOPCONF_TBL_SHIFT = 15,
    TMC_CHOPCONF_MRES_SHIFT = 24,
    TMC_CHOPCONF_INTPOL_BIT = 28,
    TMC_GCONF_EN_SPREADCYCLE_BIT = 2,
    TMC_GCONF_PDN_DISABLE_BIT = 6,
    TMC_GCONF_MSTEP_REG_SELECT_BIT = 7,
    TMC_GCONF_MULTISTEP_FILT_BIT = 8,
    TMC_STEALTHCHOP_WRITE_SETTLE_US = 100,
    TMC_INTERNAL_MICROSTEPS = 256,
    TMC_MICROSTEPS_128 = 128,
    TMC_MICROSTEPS_64 = 64,
    TMC_MICROSTEPS_32 = 32,
    TMC_MICROSTEPS_16 = 16,
    TMC_MICROSTEPS_8 = 8,
    TMC_MICROSTEPS_4 = 4,
    TMC_MICROSTEPS_2 = 2,
    TMC_MICROSTEPS_1 = 1,
    TMC_CLOCK_HZ = 12000000,
    TMC_TPWMTHRS_MAX = 0xFFFFF,
    TMC_PWMCONF_PWM_OFS = 36u,
    TMC_PWMCONF_PWM_GRAD = 14u,
    TMC_PWMCONF_PWM_FREQ = 1u,
    TMC_PWMCONF_AUTOSCALE_BIT = 18,
    TMC_PWMCONF_AUTOGRAD_BIT = 19,
    TMC_PWMCONF_FREEWHEEL = 1u,
    TMC_PWMCONF_PWM_REG = 8u,
    TMC_PWMCONF_PWM_LIM = 12u,
    TMC_PWM_BYTE_MASK = 0xFFu,
    TMC_PWM_FREQ_MASK = 0x03u,
    TMC_PWM_NIBBLE_MASK = 0x0Fu,
    TMC_PWM_GRAD_SHIFT = 8,
    TMC_PWM_FREQ_SHIFT = 16,
    TMC_PWM_FREEWHEEL_SHIFT = 24,
    TMC_PWM_LIM_SHIFT = 28,
    TMC_SG_RESULT_MASK = 0x03FFu,
};

static const float MA_PER_AMP_F = 1000.0f;
static const float TMC_VFS_HIGH_SENSITIVITY_V = 0.180f;
static const float TMC_VFS_LOW_SENSITIVITY_V = 0.325f;
static const float TMC_RSENSE_SERIES_OHM = 0.020f;
static const float SQRT_2_F = 1.41421356f;
static const float ROUND_TO_NEAREST_F = 0.5f;

// PIO program cache — keyed per PIO block (index 0 = pio0, 1 = pio1)
// Allows both PIO blocks to be used independently by different TMC instances.
typedef struct {
    bool loaded;
    uint offset_tx;
    uint offset_rx;
} tmc_pio_cache_t;

static tmc_pio_cache_t pio_cache[TMC_PIO_CACHE_BLOCKS];

static int pio_block_index(PIO pio) {
    return pio == pio1 ? 1 : 0;
}

uint8_t tmc_crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        for (uint8_t j = 0; j < TMC_CRC_BITS_PER_BYTE; j++) {
            uint8_t mix = (crc >> TMC_CRC_TOP_BIT_SHIFT) ^ (b & 1u);
            crc <<= 1;
            if (mix) {
                crc ^= TMC_CRC_POLYNOMIAL;
            }
            b >>= 1;
        }
    }
    return crc;
}

bool tmc_init(tmc_t *tmc, uint tx_pin, uint rx_pin, uint8_t address) {
    tmc->tx_pin = tx_pin;
    tmc->rx_pin = rx_pin; // Note: For ERB v2.0 hardware, tx_pin is single-wire bidirectional.
                          // rx_pin is typically unused or DIAG.
    tmc->addr = address;
    tmc->pio = pio0;

    int pidx = pio_block_index(tmc->pio);
    tmc_pio_cache_t *cache = &pio_cache[pidx];

    if (!cache->loaded) {
        if (pio_can_add_program(tmc->pio, &tmc_uart_tx_program) &&
            pio_can_add_program(tmc->pio, &tmc_uart_rx_program)) {
            cache->offset_tx = pio_add_program(tmc->pio, &tmc_uart_tx_program);
            cache->offset_rx = pio_add_program(tmc->pio, &tmc_uart_rx_program);
            cache->loaded = true;
        } else {
            return false;
        }
    }

    tmc->offset_tx = cache->offset_tx;
    tmc->offset_rx = cache->offset_rx;

    tmc->sm_tx = pio_claim_unused_sm(tmc->pio, true);
    tmc->sm_rx = pio_claim_unused_sm(tmc->pio, true);

    tmc_uart_tx_program_init(tmc->pio, tmc->sm_tx, tmc->offset_tx, tmc->tx_pin, TMC_BAUD);
    tmc_uart_rx_program_init(tmc->pio, tmc->sm_rx, tmc->offset_rx, tmc->tx_pin, TMC_BAUD);

    pio_sm_set_enabled(tmc->pio, tmc->sm_tx,
                       true); // Keep TX SM enabled permanently to manage pin state
    pio_sm_set_enabled(tmc->pio, tmc->sm_rx,
                       true); // Keep RX SM enabled permanently to autonomously track line state

    // MUST enable internal pull-up so that when we switch to INPUT during reads,
    // the line is held HIGH (if TMC2209 pdn_disable=1 turns off its internal pull-down)
    gpio_pull_up(tmc->tx_pin);

    // Guarantee the pin is actively driven HIGH (OUTPUT) while idle
    pio_sm_set_consecutive_pindirs(tmc->pio, tmc->sm_tx, tmc->tx_pin, 1, true);

    return true;
}

static void tmc_uart_send_bytes(tmc_t *tmc, const uint8_t *buffer, size_t len) {
    for (size_t i = 0; i < len; i++) {
        pio_sm_put_blocking(tmc->pio, tmc->sm_tx, buffer[i]);
    }

    // Wait for FIFO to empty
    uint64_t timeout_us = time_us_64() + TMC_UART_SEND_TIMEOUT_US;
    while (!pio_sm_is_tx_fifo_empty(tmc->pio, tmc->sm_tx) && time_us_64() < timeout_us)
        tight_loop_contents();

    // Wait for the state machine to stall at the 'pull' instruction (offset_tx).
    // NOTE: the PC moves to 'pull' the moment it fetches the stop-bit instruction,
    // before the 7-cycle [7] delay executes. So the SM stalls at pull ~21us BEFORE
    // the stop bit actually finishes on the wire. We add a mandatory 30us inter-frame
    // gap here so back-to-back calls never collide on the bus.
    timeout_us = time_us_64() + TMC_UART_SEND_TIMEOUT_US;
    while (pio_sm_get_pc(tmc->pio, tmc->sm_tx) != tmc->offset_tx && time_us_64() < timeout_us)
        tight_loop_contents();
    busy_wait_us_32(
        TMC_INTERFRAME_GAP_US); // inter-frame gap: ensures stop bit fully clears the wire
}

bool tmc_write(tmc_t *tmc, uint8_t reg, uint32_t value) {
    uint8_t buffer[TMC_RAW_REPLY_LEN];

    buffer[TMC_WRITE_SYNC_IDX] = TMC_UART_SYNC;
    buffer[TMC_WRITE_ADDR_IDX] = tmc->addr;
    buffer[TMC_WRITE_REG_IDX] = reg | TMC_WRITE_FLAG;
    buffer[TMC_WRITE_DATA3_IDX] = (uint8_t)((value >> TMC_DATA3_SHIFT) & TMC_BYTE_MASK);
    buffer[TMC_WRITE_DATA2_IDX] = (uint8_t)((value >> TMC_DATA2_SHIFT) & TMC_BYTE_MASK);
    buffer[TMC_WRITE_DATA1_IDX] = (uint8_t)((value >> TMC_DATA1_SHIFT) & TMC_BYTE_MASK);
    buffer[TMC_WRITE_DATA0_IDX] = (uint8_t)(value & TMC_BYTE_MASK);
    buffer[TMC_WRITE_CRC_IDX] = tmc_crc8(buffer, TMC_WRITE_CRC_IDX);

    tmc_uart_send_bytes(tmc, buffer, TMC_RAW_REPLY_LEN);
    return true;
}

// Perform the bus turnaround and receive 8 bytes from the TMC2209.
// Returns the number of bytes received (0-8).
static int tmc_read_bytes(tmc_t *tmc, uint8_t reg, uint8_t *buffer) {
    uint8_t request[TMC_READ_REQUEST_LEN];
    request[TMC_WRITE_SYNC_IDX] = TMC_UART_SYNC;
    request[TMC_WRITE_ADDR_IDX] = tmc->addr;
    request[TMC_WRITE_REG_IDX] = reg & TMC_READ_REG_MASK;
    request[TMC_WRITE_DATA3_IDX] = tmc_crc8(request, TMC_WRITE_DATA3_IDX);

    // Force-restart the RX SM so it is cleanly waiting for a start bit.
    // The RX SM runs permanently and may have been triggered by noise or
    // DIAG glitches on the shared pin, leaving it stuck neutral-frame in bitloop
    // or wait_high.  A simple FIFO clear doesn't fix a stuck PC.
    pio_sm_set_enabled(tmc->pio, tmc->sm_rx, false);
    pio_sm_clear_fifos(tmc->pio, tmc->sm_rx);
    pio_sm_exec(tmc->pio, tmc->sm_rx, pio_encode_mov(pio_isr, pio_null));
    pio_sm_exec(tmc->pio, tmc->sm_rx, pio_encode_jmp(tmc->offset_rx));
    pio_sm_set_enabled(tmc->pio, tmc->sm_rx, true);

    tmc_uart_send_bytes(tmc, request, TMC_READ_REQUEST_LEN);

    // tmc_uart_send_bytes includes a 30us inter-frame gap covering the stop bit.
    // Add 10 more us to ensure the RX SM has pushed the final echo byte to FIFO.
    busy_wait_us_32(TMC_ECHO_SETTLE_US);

    // Completely drain the RX FIFO of all echo bytes (4 bytes) and any preceding garbage
    while (!pio_sm_is_rx_fifo_empty(tmc->pio, tmc->sm_rx)) {
        pio_sm_get(tmc->pio, tmc->sm_rx);
    }

    // Now release the pin to INPUT so the TMC2209 can reply.
    // (Requires internal pull-up and pdn_disable=1 so it stays HIGH during idle)
    pio_sm_set_consecutive_pindirs(tmc->pio, tmc->sm_tx, tmc->tx_pin, 1, false);

    uint64_t timeout_us = time_us_64() + TMC_REPLY_TIMEOUT_US;
    int bytes_received = 0;

    while (bytes_received < TMC_RAW_REPLY_LEN && time_us_64() < timeout_us) {
        if (!pio_sm_is_rx_fifo_empty(tmc->pio, tmc->sm_rx)) {
            buffer[bytes_received++] =
                (uint8_t)(pio_sm_get(tmc->pio, tmc->sm_rx) >> TMC_DATA3_SHIFT);
        }
    }

    // Restore pin to OUTPUT HIGH
    pio_sm_set_consecutive_pindirs(tmc->pio, tmc->sm_tx, tmc->tx_pin, 1, true);

    return bytes_received;
}

bool tmc_read(tmc_t *tmc, uint8_t reg, uint32_t *out_value) {
    uint8_t reply[TMC_RAW_REPLY_LEN];

    for (int attempt = 0; attempt < TMC_REPLY_ATTEMPTS; attempt++) {
        int bytes_read = tmc_read_bytes(tmc, reg, reply);
        if (bytes_read < TMC_RAW_REPLY_LEN)
            continue;
        if (reply[TMC_WRITE_SYNC_IDX] != TMC_UART_SYNC ||
            reply[TMC_WRITE_ADDR_IDX] != TMC_BYTE_MASK ||
            (reply[TMC_WRITE_REG_IDX] & TMC_READ_REG_MASK) != reg)
            continue;
        if (tmc_crc8(reply, TMC_WRITE_CRC_IDX) != reply[TMC_WRITE_CRC_IDX])
            continue;

        *out_value = ((uint32_t)reply[TMC_WRITE_DATA3_IDX] << TMC_DATA3_SHIFT) |
                     ((uint32_t)reply[TMC_WRITE_DATA2_IDX] << TMC_DATA2_SHIFT) |
                     ((uint32_t)reply[TMC_WRITE_DATA1_IDX] << TMC_DATA1_SHIFT) |
                     (uint32_t)reply[TMC_WRITE_DATA0_IDX];
        return true;
    }
    return false;
}

int tmc_read_raw(tmc_t *tmc, uint8_t reg, uint8_t *buffer_out) {
    return tmc_read_bytes(tmc, reg, buffer_out);
}

static uint8_t calculate_cs(int ma, bool vsense) {
    if (ma <= 0)
        return 0;
    float irms = (float)ma / MA_PER_AMP_F;
    float vfs = vsense ? TMC_VFS_HIGH_SENSITIVITY_V : TMC_VFS_LOW_SENSITIVITY_V;
    float r_sense = CONF_RSENSE_OHM;
    float r_total = r_sense + TMC_RSENSE_SERIES_OHM;
    float sqrt2 = SQRT_2_F;

    // CS = (I_RMS * 32 * R_TOTAL * sqrt(2) / V_FS) - 1
    float cs_f = (irms * (float)TMC_CURRENT_SCALE * r_total * sqrt2 / vfs) - 1.0f;
    int cs = (int)(cs_f + ROUND_TO_NEAREST_F); // Round to nearest

    if (cs < 0)
        cs = 0;
    if (cs > TMC_CS_MAX)
        cs = TMC_CS_MAX;
    return (uint8_t)cs;
}

bool tmc_set_run_current_ma(tmc_t *tmc, int run_ma, int hold_ma) {
    // Klipper-style vsense toggling: use high sensitivity (vsense=1, 0.180V) up to 0.98A
    // and low sensitivity (vsense=0, 0.325V) above that for better resolution at lower currents.
    bool vsense = (run_ma <= TMC_VSENSE_THRESHOLD_MA);

    uint8_t irun = calculate_cs(run_ma, vsense);
    uint8_t ihold = calculate_cs(hold_ma, vsense);

    // Update CHOPCONF if vsense bit (bit 17) needs to change
    uint32_t current_vsense = (tmc->chopconf >> TMC_CHOPCONF_VSENSE_BIT) & 1u;
    if (current_vsense != (uint32_t)vsense) {
        if (vsense) {
            tmc->chopconf |= (1u << TMC_CHOPCONF_VSENSE_BIT);
        } else {
            tmc->chopconf &= ~(1u << TMC_CHOPCONF_VSENSE_BIT);
        }
        tmc_write(tmc, TMC_REG_CHOPCONF, tmc->chopconf);
    }

    uint32_t reg = ((uint32_t)ihold) | ((uint32_t)irun << TMC_DATA_BYTE_SHIFT) |
                   (TMC_IHOLDDELAY << TMC_IHOLDDELAY_SHIFT);
    return tmc_write(tmc, TMC_REG_IHOLD_IRUN, reg);
}

bool tmc_setup_chopconf(tmc_t *tmc, int microsteps, int toff, int tbl, int hstrt, int hend,
                        bool intpol) {
    int mres;
    switch (microsteps) {
    case TMC_INTERNAL_MICROSTEPS:
        mres = TMC_MRES_256;
        break;
    case TMC_MICROSTEPS_128:
        mres = TMC_MRES_128;
        break;
    case TMC_MICROSTEPS_64:
        mres = TMC_MRES_64;
        break;
    case TMC_MICROSTEPS_32:
        mres = TMC_MRES_32;
        break;
    case TMC_MICROSTEPS_16:
        mres = TMC_MRES_16;
        break;
    case TMC_MICROSTEPS_8:
        mres = TMC_MRES_8;
        break;
    case TMC_MICROSTEPS_4:
        mres = TMC_MRES_4;
        break;
    case TMC_MICROSTEPS_2:
        mres = TMC_MRES_2;
        break;
    case TMC_MICROSTEPS_1:
        mres = TMC_MRES_1;
        break;
    default:
        return false;
    }

    uint32_t reg_toff = (uint32_t)(toff & TMC_TOFF_MASK);
    uint32_t reg_tbl = (uint32_t)(tbl & TMC_TBL_MASK);
    uint32_t reg_hstrt = (uint32_t)(hstrt & TMC_HSTRT_MASK);
    uint32_t reg_hend = (uint32_t)(hend & TMC_HEND_MASK);

    uint32_t chop = 0;
    chop |= (reg_toff << 0);
    chop |= (reg_hstrt << TMC_CHOPCONF_HSTRT_SHIFT);
    chop |= (reg_hend << TMC_CHOPCONF_HEND_SHIFT);
    chop |= (reg_tbl << TMC_CHOPCONF_TBL_SHIFT);
    chop |= (1u << TMC_CHOPCONF_VSENSE_BIT);
    chop |= ((uint32_t)mres << TMC_CHOPCONF_MRES_SHIFT);
    if (intpol) {
        chop |= (1u << TMC_CHOPCONF_INTPOL_BIT);
    }
    tmc->chopconf = chop;
    return tmc_write(tmc, TMC_REG_CHOPCONF, chop);
}

bool tmc_set_spreadcycle(tmc_t *tmc, bool spreadcycle) {
    uint32_t gconf = 0;
    if (spreadcycle)
        gconf |= (1u << TMC_GCONF_EN_SPREADCYCLE_BIT);
    gconf |= (1u << TMC_GCONF_PDN_DISABLE_BIT);
    gconf |= (1u << TMC_GCONF_MSTEP_REG_SELECT_BIT);
    gconf |= (1u << TMC_GCONF_MULTISTEP_FILT_BIT);
    return tmc_write(tmc, TMC_REG_GCONF, gconf);
}

bool tmc_set_stealthchop_sps(tmc_t *tmc, int sps, int microsteps) {
    uint32_t gconf = (1u << TMC_GCONF_PDN_DISABLE_BIT) | (1u << TMC_GCONF_MSTEP_REG_SELECT_BIT) |
                     (1u << TMC_GCONF_MULTISTEP_FILT_BIT);
    if (sps <= 0) {
        // Always SpreadCycle
        gconf |= (1u << TMC_GCONF_EN_SPREADCYCLE_BIT);
        tmc_write(tmc, TMC_REG_GCONF, gconf);
        busy_wait_us_32(TMC_STEALTHCHOP_WRITE_SETTLE_US);
        tmc_write(tmc, TMC_REG_TPWMTHRS, 0);
        busy_wait_us_32(TMC_STEALTHCHOP_WRITE_SETTLE_US);
        tmc_write(tmc, TMC_REG_TCOOLTHRS, 0);
    } else {
        // StealthChop enabled, switching to SpreadCycle above threshold_sps
        tmc_set_pwmconf(tmc);
        busy_wait_us_32(TMC_STEALTHCHOP_WRITE_SETTLE_US);
        tmc_write(tmc, TMC_REG_GCONF, gconf);
        busy_wait_us_32(TMC_STEALTHCHOP_WRITE_SETTLE_US);

        // TSTEP is measured in units of internal 256-microsteps.
        // If we send pulses at frequency 'sps' while in 'microsteps' mode,
        // each pulse corresponds to (256/microsteps) internal units.
        // TPWMTHRS = f_clk / (sps * (256 / microsteps))
        uint32_t scale = TMC_INTERNAL_MICROSTEPS / (uint32_t)microsteps;
        uint32_t tpwmthrs = TMC_CLOCK_HZ / ((uint32_t)sps * scale);
        if (tpwmthrs > TMC_TPWMTHRS_MAX)
            tpwmthrs = TMC_TPWMTHRS_MAX;

        tmc_write(tmc, TMC_REG_TPWMTHRS, tpwmthrs);
        busy_wait_us_32(TMC_STEALTHCHOP_WRITE_SETTLE_US);

        // Ensure TCOOLTHRS is 0 so it doesn't force SpreadCycle early
        tmc_write(tmc, TMC_REG_TCOOLTHRS, 0);
    }
    return true;
}

bool tmc_set_pwmconf(tmc_t *tmc) {
    uint32_t value = 0;
    value |= (TMC_PWMCONF_PWM_OFS & TMC_PWM_BYTE_MASK);
    value |= (TMC_PWMCONF_PWM_GRAD & TMC_PWM_BYTE_MASK) << TMC_PWM_GRAD_SHIFT;
    value |= (TMC_PWMCONF_PWM_FREQ & TMC_PWM_FREQ_MASK) << TMC_PWM_FREQ_SHIFT;
    value |= (1u << TMC_PWMCONF_AUTOSCALE_BIT);
    value |= (1u << TMC_PWMCONF_AUTOGRAD_BIT);
    value |= (TMC_PWMCONF_PWM_REG & TMC_PWM_NIBBLE_MASK) << TMC_PWM_FREEWHEEL_SHIFT;
    value |= (TMC_PWMCONF_PWM_LIM & TMC_PWM_NIBBLE_MASK) << TMC_PWM_LIM_SHIFT;
    return tmc_write(tmc, TMC_REG_PWMCONF, value);
}

bool tmc_set_sgthrs(tmc_t *tmc, uint8_t sgthrs) {
    return tmc_write(tmc, TMC_REG_SGTHRS, (uint32_t)sgthrs);
}

bool tmc_set_tcoolthrs(tmc_t *tmc, uint32_t value) {
    return tmc_write(tmc, TMC_REG_TCOOLTHRS, value);
}

bool tmc_read_sg_result(tmc_t *tmc, uint16_t *out_value) {
    uint32_t value = 0;
    if (!tmc_read(tmc, TMC_REG_SG_RESULT, &value)) {
        return false;
    }
    *out_value = (uint16_t)(value & TMC_SG_RESULT_MASK);
    return true;
}
