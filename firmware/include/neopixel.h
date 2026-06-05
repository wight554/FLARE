#pragma once

#include <stdint.h>

#include "pico/types.h"

void neopixel_init(uint pin);
void neopixel_set(uint8_t red, uint8_t green, uint8_t blue);
