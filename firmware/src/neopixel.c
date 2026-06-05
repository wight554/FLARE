/// @file neopixel.c
/// @brief WS2812 status-LED driver via PIO: packs RGB into the GRB wire order and
///        clocks it out on the PIO state machine.
/// @details Used by main.c for status indication only. Color/state mapping lives at
///          the call sites in main.c.

#include "neopixel.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"

static PIO g_pio = pio1;
static uint g_sm;

#define GRB_GREEN_SHIFT 16u
#define GRB_RED_SHIFT 8u
#define WS2812_RGB_BITS 24u
#define WS2812_RGBW_BITS 32u
#define WS2812_DATA_HZ 800000.0f
#define WS2812_TX_SHIFT 8u

static inline uint32_t rgb_to_grb(uint8_t red, uint8_t green, uint8_t blue) {
    return ((uint32_t)green << GRB_GREEN_SHIFT) | ((uint32_t)red << GRB_RED_SHIFT) | (uint32_t)blue;
}

static void ws2812_program_init_local(PIO pio, uint sm, uint offset, uint pin, float freq,
                                      bool rgbw) {
    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);

    pio_sm_config sm_config = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&sm_config, pin);
    sm_config_set_out_shift(&sm_config, false, true, rgbw ? WS2812_RGBW_BITS : WS2812_RGB_BITS);
    sm_config_set_fifo_join(&sm_config, PIO_FIFO_JOIN_TX);

    int cycles_per_bit = ws2812_T1 + ws2812_T2 + ws2812_T3;
    float div = (float)clock_get_hz(clk_sys) / (freq * (float)cycles_per_bit);
    sm_config_set_clkdiv(&sm_config, div);

    pio_sm_init(pio, sm, offset, &sm_config);
    pio_sm_set_enabled(pio, sm, true);
}

void neopixel_init(uint pin) {
    uint offset = (uint)pio_add_program(g_pio, &ws2812_program);
    g_sm = pio_claim_unused_sm(g_pio, true);
    ws2812_program_init_local(g_pio, g_sm, offset, pin, WS2812_DATA_HZ, false);
    neopixel_set(0, 0, 0);
}

void neopixel_set(uint8_t red, uint8_t green, uint8_t blue) {
    uint32_t grb = rgb_to_grb(red, green, blue);
    pio_sm_put_blocking(g_pio, g_sm, grb << WS2812_TX_SHIFT);
}
