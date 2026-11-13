/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Sound categories (1.2, Discussion #90): every sound in the PRESETS list belongs to one, so KNOB 4 (LIST) can show
 * only the basses, the pads, .. (ui.c list_mode). Factory presets: a static table here (a letter per preset, in the
 * engine's preset order); user presets: a byte of their record (upreset.c up_cat, set on the NAME screen when saving
 * or renaming, ui_name.c). Included by ui.c and upreset.c (the host build of the user presets needs the numbers only).
 * The numbers are stored (a user preset's record, the editor's UP_LIST / UP_GET / UP_PUT): append-only. CAT_NONE is a
 * record of before (or the editor's without one): its category follows from its engine (cat_of_engine) */
#ifndef FELUCCA_CATEGORY
#define FELUCCA_CATEGORY 1
enum { CAT_NONE, CAT_BASS, CAT_LEAD, CAT_PAD, CAT_PLUCK, CAT_KEYS, CAT_DRUM, CAT_FX, CAT_OTHER, CAT_N };
static const char *const CAT_NAME[CAT_N] = {"-", "BASS", "LEAD", "PAD", "PLUCK", "KEYS", "DRUM", "FX", "OTHER"};
/* the category a sound without one is listed under: a kit (DRUM, SLICE: engines 10, 13) DRUM, any other OTHER */
static uint32_t cat_of_engine(uint32_t e) { return e == 10u || e == 13u ? CAT_DRUM : CAT_OTHER; }

#ifndef UP_HOST
/* the factory presets' categories: per engine number (engines.c ENGINES[]), a letter per preset in its order (B BASS,
 * L LEAD, P PAD, U PLUCK, K KEYS, D DRUM, F FX, O OTHER); a preset past its string (DIGITAL's, retired; SAMPLE's
 * retired PERC, past SMP_NPRESETS): its engine's (cat_of_engine). tests/ui_test.c checks every string
 * against its engine's preset count. A preset's category is a hint for browsing: nothing else reads it */
static const char *const PRESET_CAT[17] = {
    "LPBPBKLBULLPLLPLP",         /* 0 ANALOG: … SUPER SAW, SYNC LEAD, AIR PAD, FILTER SWP, SUB DRONE */
    "",                          /* 1 DIGITAL (retired) */
    "LKPLUUK",                   /* 2 PHASE: … WIRE, GLASS */
    "LBLLLL",                    /* 3 LOFI: … STEP LEAD, TRI LD */
    "KKLLKD",                    /* 4 SAMPLE: PIANO, TRANH→PIANO, FLUTE, SAX, PERC→PIANO, CR78 */
    "PLBFPL",                    /* 5 VOICE: … WHISPER, OOOH PAD, TALK LEAD */
    "BLLUPLLFL",                 /* 6 TRIO: … CHIP CHOIR, SAW STACK, FIFTHS, RING NOISE, SYNC STACK */
    "KKKKKKK",                   /* 7 WHEEL: the organs + CHURCH, PERC STOP */
    "PFPPP",                     /* 8 GRAIN: … SHIMMER, STRETCH */
    "UUUPUDDPUU",                /* 9 PHYS: … HARP, GLASS BOW */
    "DDDDDDDDDDDDDDDDD",         /* 10 DRUM: FM DRUMS, JIANT's kits (909 CLUB .. LATIN PERC), DRUMS STD .. 77 */
    "FFFFF",                     /* 11 NOISE: … METAL, REG BELL */
    "KUBLPUKULPKBLFB",           /* 12 FM6: … PLUCK, METAL VOX, HOLLOW FM, CRYSTAL, FM GROWL, OCT STACK, AIR NOISE, DUBSTEP BASS */
    "DD",                        /* 13 SLICE: CHOP, STUTTER */
    "BLKPBLUPLP",                /* 14 WAVETABLE: … SWEEP, WT FDBK, HOLLOW */
    "",                          /* 15 DRUMS (retired: its kits are DRUM-X's) */
};
static uint32_t preset_cat(uint32_t e, uint32_t k)    /* factory preset k of engine e */
{
    static const char L[] = "?BLPUKDFO";               /* (a letter's index is its CAT_*) */
    const char *s = e < NELEM(PRESET_CAT) && PRESET_CAT[e] ? PRESET_CAT[e] : "";
    uint32_t i, c;
    for (i = 0; i < k && s[i]; i++)
        ;
    for (c = 1; s[i] && L[c] && L[c] != s[i]; c++)
        ;
    return s[i] && L[c] ? c : cat_of_engine(e);
}
#endif
#endif
