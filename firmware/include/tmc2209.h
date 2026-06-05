#pragma once
#include "hardware/pio.h"
#include "pico/types.h"
#include <stdbool.h>
#include <stdint.h>

/// @brief TMC2209 UART/PIO instance state.
typedef struct {
    uint tx_pin;
    uint rx_pin;
    uint8_t addr;
    PIO pio;
    uint sm_tx;
    uint sm_rx;
    uint offset_tx;
    uint offset_rx;
    uint32_t chopconf;
} tmc_t;

/// @brief Raw TMC UART reply frame length in bytes.
#define TMC_RAW_REPLY_LEN 8
/// @brief TMC2209 GCONF register address.
#define TMC_REG_GCONF 0x00
/// @brief TMC2209 GSTAT register address.
#define TMC_REG_GSTAT 0x01
/// @brief TMC2209 IFCNT register address.
#define TMC_REG_IFCNT 0x02
/// @brief TMC2209 IHOLD_IRUN register address.
#define TMC_REG_IHOLD_IRUN 0x10
/// @brief TMC2209 TPWMTHRS register address.
#define TMC_REG_TPWMTHRS 0x13
/// @brief TMC2209 TCOOLTHRS register address.
#define TMC_REG_TCOOLTHRS 0x14
/// @brief TMC2209 SGTHRS register address.
#define TMC_REG_SGTHRS 0x40
/// @brief TMC2209 SG_RESULT register address.
#define TMC_REG_SG_RESULT 0x41
/// @brief TMC2209 CHOPCONF register address.
#define TMC_REG_CHOPCONF 0x6C
/// @brief TMC2209 DRV_STATUS register address.
#define TMC_REG_DRV_STATUS 0x6F
/// @brief TMC2209 PWMCONF register address.
#define TMC_REG_PWMCONF 0x70
bool tmc_init(tmc_t *tmc, uint tx_pin, uint rx_pin, uint8_t address);
bool tmc_write(tmc_t *tmc, uint8_t reg, uint32_t value);
bool tmc_read(tmc_t *tmc, uint8_t reg, uint32_t *out_value);
uint8_t tmc_crc8(const uint8_t *data, uint8_t len);
bool tmc_set_run_current_ma(tmc_t *tmc, int run_ma, int hold_ma);
bool tmc_setup_chopconf(tmc_t *tmc, int microsteps, int toff, int tbl, int hstrt, int hend,
                        bool intpol);
bool tmc_set_spreadcycle(tmc_t *tmc, bool spreadcycle);
bool tmc_set_stealthchop_sps(tmc_t *tmc, int sps, int microsteps);
bool tmc_set_sgthrs(tmc_t *tmc, uint8_t sgthrs);
bool tmc_set_tcoolthrs(tmc_t *tmc, uint32_t value);
bool tmc_set_pwmconf(tmc_t *tmc);
bool tmc_read_sg_result(tmc_t *tmc, uint16_t *out_value);
int tmc_read_raw(tmc_t *tmc, uint8_t reg, uint8_t buffer[TMC_RAW_REPLY_LEN]);
