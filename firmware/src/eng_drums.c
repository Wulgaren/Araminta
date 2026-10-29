/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DRUMS: Felucca's drum engine, ported as engine 15. A kit of 8 lanes (drums_voice.c): KICK, SNARE, CLAP,
 * HAT CL, HAT OP, TOM, RIM, BELL. DRUM-X stays engine 10; this engine is for the original Felucca kits.
 *
 * Keys and notes: the keys play the General MIDI drum map with the kick on the first C (C KICK, C# RIM,
 * D SNARE, D# CLAP, F# HAT CL, A# HAT OP, F G A B C D TOMS, C# CYM, G# BELL; OCT -/+ move it), and a note
 * plays its GM drum (35..81: kicks, snares, toms, hats, cymbals, bells, congas, claves; other notes as
 * their octave of 36..47), so GM patterns and MIDI parts play as they did on the old SAMPLE PERC.
 *
 * Parameters: KIT the kit: STD (Felucca's own: TOM RIM BELL on lanes 6..8, other GM notes their congas, claves,
 * cymbals) or a model kit (80 10 66 55 77, 1.5.6 DEEP TIGHT PERC: every lane that kit's own piece, drums_voice.c FDV_KIT; KICK does not
 * apply). Its stored values 1..3 were HAND CYM H+CYM (STD with CONGA CLAVE / CYM on lanes 6..8) until 1.0.4: they
 * play 66 10 77 now (FDK_PLAYS), and whatever stores them lands there (params.c enum_orig, param_fit: the knob
 * steps over them, the editor's SET, projects, user presets, motion). KICK the kick (PUNCH, ROUND). TUNE (64: as
 * designed, +-12 semitones), TONE, DECY and SNAP (each lane's extra: the kick's drive, the snare's snappiness,
 * the clap's spread, the hats' noise, the toms' bend, the rim's drive, the bell's strike) move every lane from its designed value. ACC
 * is the accent at full velocity (velocity scales it), DRV a soft clip on every hit (x1..x4, level kept).
 * Each lane has its LEVEL (P_LN0..P_LN7, EDIT > LANES / LANES 2, #97: square law, 100 % the kit as designed, the
 * default, so every older sound plays as it did), on the voice amplitude after the knee (a quieter lane is cleaner).
 * A hit keeps the drum and the variant it was struck with; the knobs move it while it rings.
 *
 * Polyphony: a lane per drum and part, mono: a hit reuses its lane's voice even at another pitch, the 8 lanes ring
 * together. A sounding voice plays one lane (s[0]); the lane remembers its voice (owner), and a voice
 * whose lane was struck again by another voice, or has rung out (-84 dB), ends itself (fdrum_amp). The
 * voices come from the shared budget (voice.c), up to 8 per part. A closed hat chokes the open one.
 *
 * What applies (engine_t.oneshot): the hit is the envelope. VOICE (always POLY), GLIDE, the ADSR, ENV DEST,
 * LFO DEST PIT / FLT / SHP, the global TUNE and the matrix's PITCH CUT SHP do nothing; LEVEL, PAN, the
 * sends, DIST, the SLICER, LFO DEST AMP, velocity, the matrix's AMP and its E1..E8 (the knobs, per
 * block) do. A released key does not end a hit.
 *
 * State: 8 lanes per part in the part's engine memory (engines.c eng_mem; fdrum_kit_of: the parameters, coefficients, voice and metal
 * source of each lane). */
#include "drums_voice.c"

/* The kit list: Felucca 1.5.6's five model kits and DEEP / TIGHT / PERC.
 * Stored values are this engine's own; DRUM-X keeps its separate KIT/MRPH parameter. */
enum { FDK_STD, FDK_80, FDK_10, FDK_66, FDK_55, FDK_77, FDK_DEEP, FDK_TIGHT, FDK_PERC, FDK_COUNT };
static uint32_t fdrum_kit_plays(int32_t v) { return (uint32_t)clamp(v, 0, FDK_COUNT - 1); }
/* the model kits' pieces where a lane plays another than its own (the lane's name): 1 TOM -> CONGA, 2 RIM -> CLAVE,
 * 4 BELL -> CYM */
static const uint8_t FDK_SWAP[FDV_NKIT] = {0, 4, 1, 0, 2 | 4, 4, 0, 1 | 2};
static const uint8_t FDV_TYPE_LANE[DVT_COUNT] = {
    FDV_KICK, FDV_KICK, FDV_SNARE, FDV_CLAP, FDV_HATC, FDV_HATO, FDV_TOM, FDV_TOM, FDV_RIM, FDV_RIM, FDV_BELL, FDV_BELL,
};

typedef struct {
    fdv_param_t key;              /* the parameters the coefficients were set up with */
    fdv_coef_t c;
    fdv_voice_t v;
    fdv_metal_t mb;
    uint8_t owner;               /* the voice playing the lane: index + 1, 0 = none */
    uint8_t role;                /* the drum struck (DVT_*) */
    int8_t st;                   /* its semitones from the designed pitch (the GM map) */
    uint8_t pad;
} fdrum_lane_t;

ENG_MEM_FITS("DRUMS", sizeof(fdrum_lane_t) * FDV_NLANE);
/* 1.4.1: each part's lanes muted (bit l: lane l), the EDIT layer's black keys 1..8 (ui_layer.c EQA_DRUM): a channel
 * mute. The lane plays on as always (hits, keys, MIDI: its voice, the CPU, the choke) and fdrum_render drops its output,
 * so unmuted it sounds as if it never was. Performance state: not saved, not recorded, no undo; a sound load into the
 * track (ui.c load_begin) and a project load (project_restore_runtime) clear it, power-off too */

static const char *const FN_KITS[] = {"STD", "80", "10", "66", "55", "77", "DEEP", "TIGHT", "PERC"};
static const char *const FN_KICKS[] = {"PUNCH", "ROUND"};

/* General MIDI notes 35..81 -> the drum (DVT_*; DVT_PUNCH: the kick KICK picks) and semitones from its
 * designed pitch */
static const int8_t FDRUM_GM[47][2] = {
    {DVT_PUNCH, -2}, {DVT_PUNCH, 0}, {DVT_RIM, 0}, {DVT_SNARE, 0}, {DVT_CLAP, 0}, {DVT_SNARE, 2},     /* 35 */
    {DVT_TOM, -7}, {DVT_HATC, 0}, {DVT_TOM, -4}, {DVT_HATC, -2}, {DVT_TOM, 0}, {DVT_HATO, 0},         /* 41 */
    {DVT_TOM, 3}, {DVT_TOM, 5}, {DVT_CYM, 0}, {DVT_TOM, 8}, {DVT_CYM, -3}, {DVT_CYM, 2},              /* 47 */
    {DVT_BELL, 5}, {DVT_HATC, 5}, {DVT_CYM, 4}, {DVT_BELL, 0}, {DVT_CYM, 1}, {DVT_CLAVE, -12},        /* 53 */
    {DVT_CYM, -2}, {DVT_CONGA, 5}, {DVT_CONGA, 2}, {DVT_CONGA, 0}, {DVT_CONGA, 0}, {DVT_CONGA, -5},   /* 59 */
    {DVT_TOM, 10}, {DVT_TOM, 7}, {DVT_BELL, 7}, {DVT_BELL, 3}, {DVT_HATC, 3}, {DVT_HATC, 7},          /* 65 */
    {DVT_CLAVE, 7}, {DVT_CLAVE, 5}, {DVT_HATC, -4}, {DVT_HATC, -6}, {DVT_CLAVE, 0}, {DVT_CLAVE, -4},  /* 71 */
    {DVT_CLAVE, -7}, {DVT_CONGA, 7}, {DVT_CONGA, 3}, {DVT_BELL, 12}, {DVT_BELL, 12},                  /* 77 */
};
/* the drum of a note (DVT_*, or a model kit's lane: FDV_KTYPE) and its semitones; KICK picks the kick; a model kit
 * plays its own piece on the note's lane */
static uint32_t fdrum_gm(const int16_t *p, uint32_t note, int32_t *st)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u, t = (uint32_t)FDRUM_GM[n - 35u][0];
    uint32_t kit = fdrum_kit_plays(p[P_E0]);           /* STD, the model kits */
    *st = FDRUM_GM[n - 35u][1];
    if (kit >= FDK_80)
        return FDV_KTYPE(kit - FDK_80 + 1u, FDV_TYPE_LANE[t]);
    return t == DVT_PUNCH && p[P_E6] > 0 ? DVT_ROUND : t;
}

/* ----------------------------------------------------- the grid's lanes --- */
/* A step's lane hits (step_t.hit / acc, the DRUM grid: SEQ > STEP on a DRUM track) play these GM notes, on any
 * engine: DRUM strikes its lanes, a synth plays the pitches. Each is the designed pitch of
 * its lane (st 0 in FDRUM_GM) */
static const uint8_t FDRUM_LANE_NOTE[NLANE] = {36, 38, 39, 42, 46, 45, 37, 56};

/* the lane a note strikes (KIT-proof: a kit plays its own piece inside the lane) */
static uint32_t fdrum_lane(uint32_t note)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u;
    return FDV_TYPE_LANE[FDRUM_GM[n - 35u][0]];
}

/* KIT's swaps of lanes 6..8 (1 TOM -> CONGA, 2 RIM -> CLAVE, 4 BELL -> CYM) */
static uint32_t fdrum_swaps(const track_t *t)
{
    uint32_t kit = fdrum_kit_plays(t->p[P_E0]);
    return kit >= FDK_80 ? FDK_SWAP[kit - FDK_80] : 0u;
}

/* the lane's name as the track's KIT plays it (5 characters at most) */
static const char *fdrum_lane_name(const track_t *t, uint32_t l)
{
    static const char *const N[NLANE] = {"KICK", "SNARE", "CLAP", "HATCL", "HATOP", "TOM", "RIM", "BELL"};
    uint32_t kit = fdrum_swaps(t);
    l &= NLANE - 1u;
    if (l == FDV_TOM && (kit & 1u))
        return "CONGA";
    if (l == FDV_RIM && (kit & 2u))
        return "CLAVE";
    return (kit & 4u) && l == FDV_BELL ? "CYM" : N[l];
}
/* .. in two letters, the drum machine way (the grid's lane column) */
static const char *fdrum_lane_abbr(const track_t *t, uint32_t l)
{
    static const char *const N[NLANE] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"};
    uint32_t kit = fdrum_swaps(t);
    l &= NLANE - 1u;
    if (l == FDV_TOM && (kit & 1u))
        return "CG";
    if (l == FDV_RIM && (kit & 2u))
        return "CL";
    return (kit & 4u) && l == FDV_BELL ? "CY" : N[l];
}

static fdrum_lane_t *fdrum_kit_of(const track_t *t)
{
    return t >= &trk[0] && t < &trk[NPART] ? (fdrum_lane_t *)ENG_MEM(t - trk) : 0;
}

/* the lane voice v plays, 0 when it plays none (any more) */
static fdrum_lane_t *fdrum_lane_of(track_t *t, const voice_t *v)
{
    fdrum_lane_t *K = fdrum_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v);
    if (!K || i >= NVOICE)
        return 0;
    L = &K[(uint32_t)v->s[0] & (FDV_NLANE - 1u)];
    return L->owner == i + 1u ? L : 0;
}

/* Different GM pitches on a lane still share its one sounding voice. */
static voice_t *fdrum_reuse(track_t *t, uint32_t note)
{
    fdrum_lane_t *K = fdrum_kit_of(t);
    uint32_t lane = fdrum_lane(note), owner = K ? K[lane].owner : 0;
    voice_t *v;
    if (!owner || owner > NVOICE)
        return 0;
    v = &t->v[owner - 1u];
    return v->active && (uint32_t)v->s[0] == lane ? v : 0;
}

static void fdrum_note_on(track_t *t, voice_t *v)
{
    fdrum_lane_t *K = fdrum_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v), role, lane;
    int32_t st;
    if (!K || i >= NVOICE)
        return;
    role = fdrum_gm(t->p, v->note, &st);
    lane = fdrum_lane(v->note);
    L = &K[(uint32_t)v->s[0] & (FDV_NLANE - 1u)];
    if (L->owner == i + 1u)                              /* this voice played another lane: it stops there */
        L->owner = 0;
    L = &K[lane];
    if (L->role != role || L->v.type != fdv_run_type(role)) {   /* another drum on this lane: from rest */
        fdv_init(&L->v, role);
        L->key.type = 0xFF;
        L->role = (uint8_t)role;
    }
    L->st = (int8_t)st;
    L->owner = (uint8_t)(i + 1u);                        /* (its last voice, if another, ends: fdrum_amp) */
    v->s[0] = (int32_t)lane;
    v->env_out = v->vel * 258;                           /* the hit starts at its level (no ramp from 0) */
    v->env = 1 << 24;                                    /* the ADSR held at full from here, not from fdrum_amp:
                                                          * a key-off before the first block (zero-length MIDI
                                                          * notes) would end a fresh voice at env 0 in env_tick */
    fdv_trigger(&L->v);
    if (lane == FDV_HATC && K[FDV_HATO].v.live)            /* a closed hat chokes the open one */
        fdv_choke(&K[FDV_HATO].v);
}

/* the voice's amplitude: the hit, not the ADSR (which still runs, held at full so a release never ends
 * it). A voice that plays no lane any more, or whose drum has rung out, ends here */
static int32_t fdrum_amp(track_t *t, voice_t *v, int32_t adsr)
{
    const fdrum_lane_t *L = fdrum_lane_of(t, v);
    (void)adsr;
    if (!v->active)                                      /* (taken for another part: env_tick ended it) */
        return 0;
    if (!L || (!L->v.live && !L->v.trig)) {
        v->active = v->gate = 0;
        v->stage = 0;
        v->env = 0;
        return 0;
    }
    v->env = 1 << 24;
    return 32767;
}

static void fdrum_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    fdrum_lane_t *L = fdrum_lane_of(t, v);
    fdv_param_t k;
    int32_t y[CTL], mb[CTL], acc = clamp(p[P_E5] * v->vel * 4, 0, 65536), drv = p[P_E7], g = 0, mk = 0;   /* acc Q16 */
    int32_t lv = clamp(p[P_LN0 + ((uint32_t)v->s[0] & (FDV_NLANE - 1u))], 0, 127);
    uint32_t i, r;
    vmod_t ml;                                           /* (only its amplitude ramp is read: voice_amp) */
    if (!L)
        return;
    if (lv < 127) {                                      /* the lane's LEVEL (square law) on the block's amplitude
                                                          * ramp, not per sample; 100 %: the ramp as it was */
        int32_t gl = lv * lv * 2 + (lv * lv >> 6);       /* Q15, 127: 32508 (not used), 64: 8256 */
        ml.amp0 = (m->amp0 >> 4) * gl >> 11;
        ml.amp1 = (m->amp1 >> 4) * gl >> 11;
        m = &ml;
    }
    if (n > CTL)
        n = CTL;
    r = L->role;
    fdv_default(&k, r);                                   /* the knobs move every lane from its design (64) */
    k.decay = (uint8_t)clamp(k.decay + p[P_E3] - 64, 0, 127);
    k.tone = (uint8_t)clamp(k.tone + p[P_E2] - 64, 0, 127);
    k.extra = (uint8_t)clamp(k.extra + p[P_E4] - 64, 0, 127);
    k.accent = (uint8_t)clamp(acc >> 9, 0, 127);         /* (Q16 -> 0..127) */
    k.tune = (int16_t)(L->st * 16 + (p[P_E1] - 64) * 3);   /* +-12 semitones */
    if (((const uint32_t *)&k)[0] != ((const uint32_t *)&L->key)[0] ||
        ((const uint32_t *)&k)[1] != ((const uint32_t *)&L->key)[1]) {
        fdv_setup(&L->c, &k);                             /* (a parameter moved) */
        L->key = k;
        if (fdv_uses_metal(L->c.type))
            fdv_metal_tune(&L->mb, &L->c);
    }
    if (fdv_uses_metal(L->c.type) && (L->v.live || L->v.trig))
        fdv_metal_run(&L->mb, mb, n);
    fdv_run(&L->c, &L->v, mb, y, n);
    if ((drum_mute[(uint32_t)(t - trk) % NPART] >> ((uint32_t)v->s[0] & (FDV_NLANE - 1u))) & 1u)
        return;                                          /* (a muted lane rings on unheard) */
    if (drv > 0) {                                       /* DRV: x1..x4 into the soft clip, the level kept (Q12) */
        g = 4096 + drv * 3 * 4096 / 127;
        mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));
    }
    for (i = 0; i < n; i++) {                            /* x1.25 and the knee below, in 32 bits */
        int32_t s = y[i];
        if (g)
            s = (softclip((s * g) >> 12) * mk) >> 15;
        out[i] += voice_amp(soft_knee(s + (s >> 2), 24000), m, i) * 2;
    }
}

/* the GM drum map, the first C (key 7) is the kick (C2, 36) */
static int32_t fdrum_keys(const track_t *t, uint32_t k)
{
    (void)t;
    return clamp(29 + 12 * song.octave + (int32_t)k, 0, 127);
}

/* {KIT, TUNE, TONE, DECY, SNAP, ACC, KICK, DRV}; every kit suggests the BEAT pattern (GM notes) */
#define FDRUMS_KIT_E {FDK_STD, 64, 70, 64, 64, 100, 0, 0}
static const preset_t FDRUM_PRESETS[] = {
    {"DRUM KIT", FDRUMS_KIT_E, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), PAT(12)},   /* (core.h: SAMPLE PERC's too) */
    /* 1.5.6 (Discussion #216): the new kits, appended */
    {"DEEP KIT", {FDK_DEEP, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 28), PAT(12)},
    {"TIGHT KIT", {FDK_TIGHT, 64, 64, 56, 64, 100, 0, 16}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 8), PAT(12)},
    {"PERC KIT", {FDK_PERC, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 24), PAT(12)},
};

static const engine_t ENG_DRUMS = {
    .name = "DRUMS",
    .page_title = {"KIT", "HIT"},
    .edit = {
        {"KIT", F_ENUM, 0, FDK_COUNT - 1, FDK_STD, FN_KITS, 0},
        {"TUNE", F_PCT, 0, 127, 64, 0, 0},
        {"TONE", F_PCT, 0, 127, 64, 0, 0},
        {"DECY", F_PCT, 0, 127, 64, 0, 0},
        {"SNAP", F_PCT, 0, 127, 64, 0, 0},
        {"ACC", F_PCT, 0, 127, 100, 0, 0},
        {"KICK", F_ENUM, 0, 1, 0, FN_KICKS, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = FDRUM_PRESETS,
    .npresets = NELEM(FDRUM_PRESETS),
    .note_on = fdrum_note_on,
    .render = fdrum_render,
    .amp = fdrum_amp,
    .knob = {P_E1, P_E2, P_E3, P_E4},
    .poly = FDV_NLANE,
    .oneshot = 1,
    .keys = fdrum_keys,
    .mem = sizeof(fdrum_lane_t) * FDV_NLANE,
};
