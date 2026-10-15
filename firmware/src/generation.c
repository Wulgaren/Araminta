/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2011 Emilie Gillet.
 * Adapted from Mutable Instruments Grids pattern_generator.cc and avrlib/op.h:
 * https://github.com/pichenettes/eurorack/tree/master/grids
 * https://github.com/pichenettes/avril/blob/master/op.h */
/* Stateless Grids map lookup. Source rhythms have 32 ticks per 4/4 bar. */
#include <stdint.h>
#include "grids_data.h"

/* Original X-major Grids map: [horizontal][vertical]. */
static const uint8_t generation_nodes[5][5] = {
    {10, 8, 0, 9, 11},
    {15, 7, 13, 12, 6},
    {18, 14, 4, 5, 3},
    {23, 16, 21, 1, 2},
    {24, 19, 17, 20, 22}
};

/* Preserves the original AVR mix, including its one-unit attenuation. */
static uint8_t generation_mix(uint8_t a, uint8_t b, uint8_t balance) {
    return ((uint32_t)a * (255u - balance) + (uint32_t)b * balance) >> 8;
}

/* Reads a kick, snare or hat level at one of the 32 original rhythm ticks. */
static uint8_t generation_level(uint32_t step, uint32_t part, uint8_t x, uint8_t y) {
    uint32_t i = x >> 6, j = y >> 6;
    uint32_t offset = part * 32u + (step & 31u);
    uint8_t a = grids_nodes[generation_nodes[i][j]][offset];
    uint8_t b = grids_nodes[generation_nodes[i + 1][j]][offset];
    uint8_t c = grids_nodes[generation_nodes[i][j + 1]][offset];
    uint8_t d = grids_nodes[generation_nodes[i + 1][j + 1]][offset];
    return generation_mix(generation_mix(a, b, (uint8_t)(x << 2)),
                          generation_mix(c, d, (uint8_t)(x << 2)), (uint8_t)(y << 2));
}

/* MAP 01..25 traverses rows left to right; the byte-sized map ends at 255. */
static void generation_map_xy(uint32_t map, uint8_t *x, uint8_t *y) {
    static const uint8_t axis[5] = {0, 64, 128, 192, 255};
    *x = axis[(map - 1u) % 5u];
    *y = axis[(map - 1u) / 5u];
}

/* Returns MAP 01..25 at a template coordinate, or zero for CUSTOM. */
static uint8_t generation_map_index(uint8_t x, uint8_t y) {
    for (uint32_t map = 1; map <= 25; ++map) {
        uint8_t mx, my;
        generation_map_xy(map, &mx, &my);
        if (x == mx && y == my) return map;
    }
    return 0;
}
