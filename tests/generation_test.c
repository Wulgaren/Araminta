/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Original Grids' top-left template at 50% density, using its 32 source ticks. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../firmware/src/generation.c"

int main(void) {
    /* node_10 source hits above 128. U8Mix attenuates the template twice. */
    const uint32_t expected[3] = {0x10041001u, 0x00011510u, 0x11010111u};
    for (uint32_t part = 0; part < 3; ++part) {
        uint32_t hits = 0;
        for (uint32_t step = 0; step < 32; ++step) {
            uint8_t level = generation_level(step, part, 0, 0);
            if (level > 128) hits |= 1u << step;
        }
        assert(hits == expected[part]);
    }
    assert(generation_level(0, 0, 0, 0) == 143);
    assert(generation_level(12, 0, 0, 0) == 253);
    puts("generation_test: PASS");
    return 0;
}
