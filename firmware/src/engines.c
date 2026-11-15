/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table (order = PRESETS browsing order and the engine numbers of the editor protocol), and the
 * parts' sounds at power-on. */
#include "dsp.c"

/* The parts' engine memory (1.5): a part plays one engine at a time, so the engines with large per-part state
 * (engine_t.mem: PHYS's slots, ANALOG's COMB lines, WHEEL's bars / rotors / voices, GRAIN's grains and index,
 * DRUM's lanes, FM6's notes) keep it in one region per part
 * instead of an array each. The region holds the state of the engine that last claimed it (eng_mem_owner); a switch
 * to another such engine zeroes its engine_t.mem bytes. Sized by the largest, PHYS (3 x SYMP's slot).
 * Zero-initialised pool section: main.c fm1_cstart clears it. */
#define ENG_MEM_PART 12888u
static union {
    uint64_t align;
    uint8_t b[ENG_MEM_PART];
} eng_mem[NPART] __attribute__((section(".pool")));
static uint8_t eng_mem_owner[NPART];       /* engine index + 1 whose state is in the region, 0 = none (zeros) */
#define ENG_MEM(p) ((void *)eng_mem[p].b)  /* part p's region (p < NPART) */
#define ENG_MEM_FITS(name, size) _Static_assert((size) <= ENG_MEM_PART, name " state over ENG_MEM_PART")

#include "eng_analog.c"
#include "eng_phase.c"
#include "eng_lofi.c"
#include "eng_sample.c"
#include "eng_formant.c"
#include "eng_trio.c"
#include "eng_wheel.c"
#include "eng_grain.c"
#include "eng_phys.c"           /* PHYS: DaisySP physical models (phys_dsp.c, MIT) */
#include "eng_drum.c"           /* DRUM: the 8-lane DRUM-X kit (drumx_voice.c) */
#include "eng_noise.c"
#include "eng_wt.c"             /* WAVETABLE: two wavetable oscillators (XIP tables) */
#include "eng_fm6.c"            /* FM6: 6-operator FM, msfa ported (fm6_core.c, Apache-2.0) */
#include "eng_retire.c"         /* SLICE reserved stub */

/* the editor protocol, user presets and projects store these indices: append, never reorder */
static const engine_t *const ENGINES[NENGINES] = {
    &ENG_ANALOG,                 /* 0 */
    &ENG_FM4_GONE,               /* 1: reserved (DIGITAL, retired: silent stub, no conversion) */
    &ENG_PHASE,                  /* 2 */
    &ENG_LOFI,                   /* 3 */
    &ENG_SAMPLE,                 /* 4 */
    &ENG_FORMANT,                /* 5 VOICE (eng_formant.c: "voice" is a sounding note in voice.c) */
    &ENG_TRIO,                   /* 6 */
    &ENG_WHEEL,                  /* 7 */
    &ENG_GRAIN,                  /* 8 */
    &ENG_PHYS,                   /* 9 (ENGI_PHYS) */
    &ENG_DRUM,                   /* 10 (ENGI_DRUM) */
    &ENG_NOISE,                  /* 11 */
    &ENG_FM6,                    /* 12 (ENGI_FM6) */
    &ENG_SLICE_GONE,             /* 13: reserved (SLICE retired) */
    &ENG_WT,                     /* 14 (ENGI_WT) */
    &ENG_SLICE_GONE,             /* 15: reserved (DRUMS retired: its kits are DRUM-X's, eng_drum.c drums_kit) */
};

/* a track's engine number as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }

/* part t's engine memory goes to its engine (t->engine; voice.c engine_block, once a block: on a switch, after
 * the old engine's fade, before any note on the new one): zeroed (its engine_t.mem bytes) when another
 * engine's state is in it. An engine without memory leaves it as it is: a switch back finds its state. */
static void eng_mem_claim(const track_t *t)
{
    uint32_t p = (uint32_t)(t - trk), e, k, n;
    uint32_t *w;
    if (p >= NPART)
        return;
    e = eng_idx(t->engine);
    n = ENGINES[e]->mem;
    if (!n || eng_mem_owner[p] == e + 1u)
        return;
    w = (uint32_t *)ENG_MEM(p);
    for (k = 0; k < (n + 3u) / 4u; k++)
        w[k] = 0;
    eng_mem_owner[p] = (uint8_t)(e + 1u);
}

/* rewrite p[] of a retired-engine sound into the target. Returns the target engine.
 * SLICE -> DRUM kit. DRUMS -> DRUM: its TUNE TONE DECY SNAP (as NOIS) DRV where DRUM-X has them, MRPH at its middle, FM
 * ENV and FOLD off (its KIT's kit: project.c proj_retire). SAMPLE PERC stays drum_from_perc on its own path. */
static uint32_t retire_remap_params(uint32_t engine, int16_t *p)
{
    uint32_t i;
    static const int16_t KIT[8] = DRUM_KIT_E;
    if (!eng_retired(engine))
        return engine;
    if (engine == ENGI_DRUMS) {
        p[P_E0] = 64;
        p[P_E5] = p[P_E6] = 0;
    } else
        for (i = 0; i < 8u; i++)
            p[P_E0 + i] = KIT[i];
    return eng_play_as(engine);
}

/* the sample source a track's sound plays that has no data (SAMPLE's SET, GRAIN's SRC: an empty or
 * invalid user slot, a set this build lacks): its name (it plays a sine: eng_sample.c smp_sine), 0 = none;
 * *code a small number for it (1.., per engine and source) so the UI says it once (ui_input.c sample_notice) */
static uint8_t snd_said[NTRK];   /* per track: the code last said, 0 = none (a project load clears them: said again) */
static const char *snd_missing(const track_t *t, uint32_t *code)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t si = (uint32_t)t->p[P_E0] % SMP_NALL;
    if (e != &ENG_SAMPLE && e != &ENG_GRAIN)
        return 0;
    *code = (e == &ENG_GRAIN ? 32u : 1u) + si % 32u;
    return smp_set_missing(si) ? SMP_ALL_NAMES[si] : 0;
}

/* the order the engines are shown in (PRESETS browsing and its ENG knob, the EDIT layer's keys, the editor's list):
 * engine indices, never DIGITAL's reserved 1, never retired SLICE or DRUMS.
 * The indices stay as they are (the stores and the protocol hold them); only this table orders them */
static const uint8_t ENGINE_ORDER[NENG_SHOWN] = {
    0,                           /* ANALOG */
    12,                          /* FM6 */
    14,                          /* WAVETABLE */
    2, 3, 5, 6, 7,               /* PHASE LOFI VOICE TRIO WHEEL */
    9, 11, 8, 4,                 /* PHYS NOISE GRAIN SAMPLE */
    10,                          /* DRUM */
};
_Static_assert(NELEM(ENGINE_ORDER) == NENG_SHOWN, "ENGINE_ORDER / NENG_SHOWN");

/* the engines one can pick (not DIGITAL's reserved 1, not retired SLICE or DRUMS) */
static int eng_ok(uint32_t e)
{
    return e < NENGINES && e != ENGI_DIGITAL && !eng_retired(e);
}
static uint32_t eng_vis(uint32_t n) { return ENGINE_ORDER[n % NENG_SHOWN]; }
static uint32_t eng_rank(uint32_t e)
{
    uint32_t n;
    if (!eng_ok(e))
        e = eng_play_as(e);
    for (n = 0; n < NENG_SHOWN && ENGINE_ORDER[n] != e; n++)
        ;
    return n < NENG_SHOWN ? n : 0u;
}
static uint32_t eng_step(uint32_t e, int32_t dir)
{
    return eng_vis((eng_rank(e % NENGINES) + (dir > 0 ? 1u : NENG_SHOWN - 1u)) % NENG_SHOWN);
}

/* the parts at power-on (engine, preset): bass, pad, lead, drums */
static const uint8_t TRK_DEF[NPART][2] = {{0, 4}, {ENGI_FM6, 4}, {3, 0}, {ENGI_DRUM, 0}}; /* ANALOG ACID,
                                                                       * FM6 PAD (was DIGITAL PAD), LOFI PULSE LD, DRUM-X */
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NPART][0]; }
