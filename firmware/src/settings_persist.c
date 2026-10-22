/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared PER5 layout: each feature updates only its own fields, preserving
 * the other feature's saved preferences when either is built independently.
 * PER1/PER2 are upstream; PER3 added bold; PER4 added favorites; PER5 moved MIDI LEARN's map from
 * favorites.factory[14] to [13] for WAVETABLE (row 13 was the retired SLICE's favourites, never selectable).
 * palette: UI_PAL_TAG + index; an old id (below 20, earlier firmware) is migrated on import.
 * bold: no longer used (one font weight); kept as it was saved, unless it holds the HOLD setting (panel.c).
 * zoom: no longer used (the large readout); kept as it was saved, unless it holds the LEDS setting (panel.c).
 * favorites.factory rows: 0..12 engines 0..12, 13 MIDI LEARN (midi_learn.c; engine 13 SLICE is retired, its
 * favourites unused), 14 WAVETABLE (ENGI_WT, favorites.c), 15 MENU / UI settings bytes (below).
 * favorites.factory[15][28..31]: CLICK / CLICK LEVEL / COUNT-IN (1.1), STYLE, MENU's flags, the layers seen (bytes no
 * engine uses; 0 in older settings = every one's default); [15][27]: SCREEN OFF (1.1.5; 0 = 30 MIN, the default);
 * [15][26]: MENU > DISPLAY > HOME (1.3; 0 = SCOPE, 1 TRACKS, 2 LEVELS (1.5), ui.c ui_home_view);
 * [15][24]: MENU's 1.2 flags SCOPE MIX, STEP PREVIEW ON, CHORD ENTRY ADD (bits 0-2, ui.c ui_prefs2; 0 = the defaults);
 * [15][10]: MENU > ANIM IDLE (1.2, Discussion #135, ui.c ui_idle: bit 0; 0 = off, ANIM as its bit in MENU's flags says);
 * [15][11]: PRESETS > LIST's category (1.2, Discussion #90, ui.c ui_lcat: 0 none = ALL / FAV as favorites.filter says);
 * [15][14..23]: the FX layer's key map (1.2, retired with the reassignment: the pages are fixed maps, perform.c PF_DEF / PF2_DEF; the
 * 10 bytes are kept as saved, unused).
 * MENU's flags (ui.c PREF_*) are append-only bits whose 0 is the default: BPM LOCK (16) is clear in every older setting
 * = unlocked, LARGE (32) = OFF, RESTORE LAST OFF (128, 1.2) = ON, read as saved (nothing to migrate, so importing twice changes nothing). */
typedef struct { uint8_t factory[16][32]; uint32_t user, filter; } persist_fav_t;
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
    uint32_t bold;
    persist_fav_t favorites;
} persist_t;
#define PERSIST_MAGIC    0x50455235u                 /* "PER5": PER4 with MIDI LEARN's map in favorites.factory[13] */
#define PERSIST_MAGIC_V4 0x50455234u                 /* "PER4": the map in [14] (WAVETABLE's row today) */
#define PERSIST_MAGIC_V3 0x50455233u                 /* "PER3": PER4 without favorites */
#define PERSIST_MAGIC_V2 0x50455232u                 /* "PER2": .. and without bold */
#define PERSIST_MAGIC_V1 0x50455231u                 /* "PER1": the panel table alone */
/* the layouts, oldest first: a record of one migrates forward, each keeping the fields it has */
enum { PERSIST_V1, PERSIST_V2, PERSIST_V3, PERSIST_V4, PERSIST_VNOW, PERSIST_NVERS };
static const struct { uint32_t magic; uint16_t size; } PERSIST_LAYOUT[PERSIST_NVERS] = {
    { PERSIST_MAGIC_V1, (uint16_t)(8u + sizeof(panel_t)) },
    { PERSIST_MAGIC_V2, (uint16_t)(sizeof(persist_t) - sizeof(persist_fav_t) - sizeof(uint32_t)) },
    { PERSIST_MAGIC_V3, (uint16_t)(sizeof(persist_t) - sizeof(persist_fav_t)) },
    { PERSIST_MAGIC_V4, (uint16_t)sizeof(persist_t) },
    { PERSIST_MAGIC,    (uint16_t)sizeof(persist_t) },
};

/* Normalize in place; 1 = current, 2 = migrated, 0 = invalid. */
static int settings_import(persist_t *p, int n)
{
    uint32_t v;
    int current;
    for (v = 0; v < PERSIST_NVERS && !(n == (int)PERSIST_LAYOUT[v].size && p->magic == PERSIST_LAYOUT[v].magic); v++)
        ;
    if (v == PERSIST_NVERS) return 0;
    current = v == PERSIST_VNOW;
    if (v == PERSIST_V1) {
        panel_t old;
        memcpy(&old, (uint8_t *)p + 8, sizeof old);
        p->panel = old;
        p->lowcut = p->zoom = 0;
    }
    if (v <= PERSIST_V2) p->bold = 0;
    /* PER1..PER3 had no favorites; PER4's row 14 was MIDI LEARN's map and row 13 the retired SLICE's favourites.
     * PER5: MIDI LEARN is row 13 and 14 is WAVETABLE's, so move the old map over the SLICE row (unused: SLICE is
     * retired) and clear 14. This runs before the copy into favorites below. */
    if (v < PERSIST_V4) memset(&p->favorites, 0, sizeof p->favorites);
    else if (v == PERSIST_V4) {
        memcpy(p->favorites.factory[13], p->favorites.factory[14], 32);
        memset(p->favorites.factory[14], 0, 32);
    }
    p->favorites.factory[15][29] = 0;            /* STYLE retired: always FLAT (LINE / PIXEL ignored) */
    p->favorites.factory[15][30] |= 2u;          /* ANIM retired: always OFF (PREF_ANIM_OFF); clear IDLE */
    p->favorites.factory[15][10] &= (uint8_t)~1u;
    {   /* CLICK, CLICK LEVEL, COUNT-IN (ui.c ui_rec_prefs): two bits each, 3 is no value: the default (0) */
        uint32_t b = p->favorites.factory[15][28], f;
        for (f = 0; f < 3u; f++)
            if (((b >> (2u * f)) & 3u) == 3u)
                b &= ~(3u << (2u * f));
        p->favorites.factory[15][28] = (uint8_t)b;
    }
    if (p->favorites.factory[15][27] > 7u)         /* SCREEN OFF (1.1.5, ui.c SCR_CODE): unknown = 0 (NEVER since 1.1.5.1) */
        p->favorites.factory[15][27] = 0;
    if (p->favorites.factory[15][11] > 8u)         /* PRESETS LIST's category (1.2, ui.c ui_lcat, CAT_*): unknown = none */
        p->favorites.factory[15][11] = 0;
    /* [15][26] HOME (1.3, ui.c ui_home_view): kept as saved; a value this firmware does not know draws SCOPE */
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(palette_from_stored(p->palette));
    settings.magic = SETTINGS_MAGIC;
    settings.palette = palette_from_stored(p->palette);
    settings.lowcut = p->lowcut;
    settings.zoom = p->zoom;
    settings_hold = (uint8_t)hold_from_stored(p->bold);
    settings_leds = (uint8_t)leds_from_stored(p->zoom);
#ifdef FELUCCA_FAVORITES
    memcpy(&favorites, &p->favorites, sizeof favorites);
    favorites.filter = favorites.filter == 1u;
#if defined(FM4_NPRESETS) && !FELUCCA_FM4
    {   /* DIGITAL's starred presets (engine 1, retired) -> the FM6 presets that cover them (fm4_convert.c) */
        uint32_t k;
        for (k = 0; k < FM4_NPRESETS; k++)
            if ((favorites.factory[ENGI_DIGITAL][0] >> k) & 1u)
                favorites.factory[ENGI_FM6][FM4_TO_FM6[k] / 8u] |= (uint8_t)(1u << (FM4_TO_FM6[k] % 8u));
        memset(favorites.factory[ENGI_DIGITAL], 0, sizeof favorites.factory[ENGI_DIGITAL]);
    }
#endif
#ifdef SMP_SET_PERC
    if ((favorites.factory[ENGI_SAMPLE][0] >> SMP_SET_PERC) & 1u) {   /* SAMPLE's PERC (preset 4, retired): DRUM's */
        favorites.factory[ENGI_DRUM][0] |= 1u;                         /* kit (core.h drum_from_perc) */
        favorites.factory[ENGI_SAMPLE][0] &= (uint8_t)~(1u << SMP_SET_PERC);
    }
#endif
#endif
    if (p->panel.magic == PANEL_MAGIC) panel = p->panel;
    return current ? 1 : 2;
}

/* Start with the last imported/saved object, including fields owned by a
 * feature absent from this build. No save-on-boot or extra flash writes. */
static void settings_export(persist_t *p)
{
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(settings.palette);
    p->lowcut = settings.lowcut;
    p->zoom = settings.zoom = leds_to_stored(settings.zoom, settings_leds);
    p->panel = panel;
    p->bold = hold_to_stored(p->bold, settings_hold);
#ifdef FELUCCA_FAVORITES
    memcpy(&p->favorites, &favorites, sizeof favorites);
#endif
}
