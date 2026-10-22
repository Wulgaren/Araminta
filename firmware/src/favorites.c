/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#define FELUCCA_FAVORITES 1
/* Stable engine/preset references; fixed capacity independent of optional engines.
 * Rows 0..12 are engines 0..12, row 13 is MIDI LEARN's map (midi_learn.c), row 14 is WAVETABLE (ENGI_WT), row 15
 * is MENU / UI settings bytes (settings_persist.c). Engine 13 SLICE is retired (eng_retire.c): never selectable, so
 * its favourites are unused and row 13 is free for the map. */
_Static_assert(ENGI_SLICE == 13u, "favorites.factory[13] is MIDI LEARN's row: engine 13 (SLICE) is retired");
typedef struct {
    uint8_t factory[16][32]; /* engine 0..12, MIDI LEARN (13), WAVETABLE (14), settings (15): preset 0..255 */
    uint32_t user, filter;
} favorites_t;
static favorites_t favorites __attribute__((section(".pool")));
static int favorite_has(uint32_t engine, uint32_t preset)
{
    if (engine == NENGINES)
        return preset < UP_SLOTS && ((favorites.user >> preset) & 1u);
    if (engine == ENGI_SLICE)                    /* retired SLICE: its row is MIDI LEARN's */
        return 0;
    return engine < NENGINES && engine < 16u && preset < 256u &&
        ((favorites.factory[engine][preset / 8u] >> (preset % 8u)) & 1u);
}
static int favorite_set(uint32_t engine, uint32_t preset, int on)
{
    if (engine == ENGI_SLICE)                    /* retired SLICE: never a favourite, row 13 is MIDI LEARN's */
        return 0;
    if (engine > NENGINES || (engine == NENGINES ? preset >= UP_SLOTS : engine >= 16u || preset >= 256u))
        return 0;
    if (favorite_has(engine, preset) == !!on) return 0;
    if (engine == NENGINES) {
        if (on) favorites.user |= 1u << preset;
        else favorites.user &= ~(1u << preset);
    } else {
        uint8_t *b = &favorites.factory[engine][preset / 8u];
        if (on) *b |= (uint8_t)(1u << (preset % 8u));
        else *b &= (uint8_t)~(1u << (preset % 8u));
    }
    return 1;
}
