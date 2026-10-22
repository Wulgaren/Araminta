/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* WAVETABLE: two wavetable oscillators, A and B, each reading a frame-major table with a mip chain.
 *
 * The tables (build/gen/felucca_wt.h, tools/gen_wavetables.py) hold WT_FRAMES frames; each frame is the
 * fundamental followed by its mip levels (512, 256, ... 8 samples). A frame is one cycle of the wave, so
 * A-POS / B-POS morph through the frames of the table. The mip level is chosen once a block and voice
 * (wt_mip_level): the highest harmonic under Nyquist, so a high note reads a short, band-limited level.
 * Tables live in XIP and are read in place.
 *
 * EDIT: OSC A = A-WAV (table), A-POS (frame), FDBK (phase feedback, up to 0.60 cycle), B>A (B phase-
 * modulates A, up to one cycle); OSC B = B-WAV, B-POS, B-LVL (0 = B only modulates A, never heard),
 * RATIO (B's frequency against A's). FLT moves A-POS, SHP moves B>A. There is no engine filter.
 *
 * Pass 1 has no user wavetable slot format: USR1..USR3 play the sine fallback (wt_slot_view). */

#include "felucca_wt.h"          /* tools/gen_wavetables.py: WT_NFACTORY, WT_TABLES[] (core.h: ENGI_WT) */

typedef struct {
    const int16_t *data;         /* 0: nothing to read (a user slot without a table): sine fallback */
    uint16_t frames;
    uint8_t base_log2, mip_last_log2;
} wt_view_t;

static const char *const N_WT_WAV[] = {WT_NAMES_INIT, "USR1", "USR2", "USR3", 0};
static const char *const N_WT_RATIO[] = {"0.25", "0.5", "0.75", "1", "1.5", "2", "2.5", "3", "4", "5", "6", "8",
                                         "12", "16", 0};
#define WT_RATIO_N 14u
static const uint32_t WT_RATIO_Q16[WT_RATIO_N] = {
    16384, 32768, 49152, 65536, 98304, 131072, 163840, 196608, 262144, 327680, 393216, 524288, 786432, 1048576
};
#define WT_DC_K 23u                  /* 5 Hz one-pole DC blocker on A (Q15: 1 - exp(-2*pi*5/44100)) */

/* one table per part and oscillator, resolved once a block (engines.c eng_mem is not used: the tables are
 * XIP and the view is a few bytes) */
static wt_view_t wt_view[NPART][2];

/* the mip level for an increment: size halves while half of it still holds a harmonic above Nyquist */
static uint32_t wt_mip_level(uint32_t base_log2, uint32_t inc)
{
    uint32_t size = 1u << base_log2, l = 0, maxl = base_log2 > WT_MIP_LAST_LOG2 ? base_log2 - WT_MIP_LAST_LOG2 : 0;
    uint32_t hmax;
    if (!inc) return 0;
    hmax = 0x80000000u / inc;                  /* highest harmonic under Nyquist */
    while (l < maxl && (size >> 1) > hmax) { size >>= 1; l++; }
    return l;
}

/* TODO(pass 2): user wavetable slots USR1..USR3. There is no slot format or storage yet, so every user slot
 * resolves to no data and its oscillator plays the sine fallback. Pass 2 fills *v from the slot and returns 1. */
static int wt_slot_view(uint32_t k, wt_view_t *v)
{
    (void)k;
    v->data = 0;
    v->frames = 0;
    v->base_log2 = 0;
    v->mip_last_log2 = 0;
    return 0;
}

/* a stored WAV index -> its view: a factory table, else a user slot (null in pass 1) */
static void wt_resolve(int32_t idx, wt_view_t *v)
{
    idx = clamp(idx, 0, 127);
    v->data = 0;
    v->frames = 0;
    v->base_log2 = 0;
    v->mip_last_log2 = 0;
    if ((uint32_t)idx < WT_NFACTORY) {
        const wt_table_t *tb = &WT_TABLES[(uint32_t)idx];
        v->data = tb->data;
        v->frames = tb->frames;
        v->base_log2 = tb->base_log2;
        v->mip_last_log2 = tb->mip_last_log2;
    } else {
        (void)wt_slot_view((uint32_t)idx - WT_NFACTORY, v);
    }
}

static void wt_block(track_t *t)
{
    uint32_t p = (uint32_t)(t - trk);
    if (p >= NPART)
        return;
    wt_resolve(t->p[P_E0], &wt_view[p][0]);
    wt_resolve(t->p[P_E4], &wt_view[p][1]);
}

/* the frames a position morphs between: frame_fp = pos * (frames-1) / 127, fr15 the fraction between them */
static void wt_frames(const wt_view_t *v, int32_t pos, uint32_t *f0, uint32_t *f1, int32_t *fr15)
{
    uint32_t fp;
    if (!v->data || v->frames < 2u) {
        *f0 = *f1 = 0;
        *fr15 = 0;
        return;
    }
    fp = (uint32_t)pos * (((uint32_t)v->frames - 1u) << 16) / 127u;
    *f0 = fp >> 16;
    *f1 = *f0 + 1u < v->frames ? *f0 + 1u : v->frames - 1u;
    *fr15 = (int32_t)((fp & 0xFFFFu) >> 1);
}

/* one table level at phase ph, linearly interpolated, crossfaded from frame f0 to f1 by fr15 (Q15); Q15 out */
static int32_t wt_read(const wt_view_t *v, uint32_t ph, uint32_t l, uint32_t f0, uint32_t f1, int32_t fr15)
{
    uint32_t base = 1u << v->base_log2;
    uint32_t stride = (base << 1) - (1u << v->mip_last_log2);
    uint32_t off = l ? (base << 1) - (base >> (l - 1u)) : 0u;
    uint32_t sl2 = (uint32_t)v->base_log2 - l;
    uint32_t mask = (1u << sl2) - 1u;
    uint32_t i0 = ph >> (32u - sl2);
    uint32_t frac = (ph >> (32u - sl2 - 16u)) & 0xFFFFu;
    const int16_t *d0 = v->data + f0 * stride + off;
    const int16_t *d1 = v->data + f1 * stride + off;
    int32_t a0 = d0[i0 & mask], b0 = d0[(i0 + 1u) & mask];
    int32_t a1 = d1[i0 & mask], b1 = d1[(i0 + 1u) & mask];
    int32_t s0 = a0 + (int32_t)(((int64_t)(b0 - a0) * (int32_t)frac) >> 16);
    int32_t s1 = a1 + (int32_t)(((int64_t)(b1 - a1) * (int32_t)frac) >> 16);
    return s0 + (((s1 - s0) * fr15) >> 15);
}

static void wt_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = 0;     /* A and B phase */
    v->s[0] = v->s[1] = v->s[2] = 0;   /* feedback lowpass, previous A sample, A's DC estimate */
}

static void wt_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t pi = (uint32_t)(t - trk), i;
    const wt_view_t *va, *vb;
    uint32_t inc = m->inc, incb, r, la, lb;
    uint32_t a0, a1, b0, b1, phA, phB, fboff;
    int32_t afr, bfr, fbq, fmq, lvl, fm, prev, lp, dc;
    if (pi >= NPART)
        return;
    if (n > CTL)
        n = CTL;
    va = &wt_view[pi][0];
    vb = &wt_view[pi][1];
    r = (uint32_t)clamp(p[P_E7], 0, (int32_t)WT_RATIO_N - 1);
    incb = (uint32_t)(((uint64_t)inc * WT_RATIO_Q16[r]) >> 16);
    if (incb > 0x80000000u)
        incb = 0x80000000u;
    la = va->data ? wt_mip_level(va->base_log2, inc) : 0;
    lb = vb->data ? wt_mip_level(vb->base_log2, incb) : 0;
    wt_frames(va, clamp(p[P_E1] + (m->cutoff >> 8), 0, 127), &a0, &a1, &afr);
    wt_frames(vb, clamp(p[P_E5], 0, 127), &b0, &b1, &bfr);
    fbq = clamp(p[P_E2], 0, 127) * 310;                /* phase feedback: up to 0.60 cycle (Q16) */
    fm = clamp(p[P_E3] + ((m->shape - (64 << 8)) >> 8), 0, 127);
    fmq = fm * 516;                                    /* B>A: up to one cycle (Q16) */
    lvl = clamp(p[P_E6], 0, 127) * 258;                /* B's level in the mix: 0 = modulator only */
    phA = v->ph[0];
    phB = v->ph[1];
    lp = v->s[0];
    prev = v->s[1];
    dc = v->s[2];
    fboff = ((uint32_t)prev * (uint32_t)fbq) << 1;
    for (i = 0; i < n; i++) {
        int32_t b = vb->data ? wt_read(vb, phB, lb, b0, b1, bfr) : osc_sine(phB);
        uint32_t off = ((uint32_t)clamp(b, -32767, 32767) * (uint32_t)fmq) << 1;
        uint32_t rd = phA + off + fboff;
        int32_t a = va->data ? wt_read(va, rd, la, a0, a1, afr) : osc_sine(rd);
        int32_t y;
        dc += ((a - dc) * (int32_t)WT_DC_K) >> 15;     /* A's DC, also taken off the feedback path */
        a -= dc;
        lp += ((a - lp) * 24884) >> 15;                /* one-pole lowpass on the feedback */
        prev = clamp(lp, -32767, 32767);
        fboff = ((uint32_t)prev * (uint32_t)fbq) << 1;
        y = a + ((b * lvl) >> 15);
        y = soft_knee(y, 24000);
        out[i] += voice_amp(y, m, i);
        phA += inc;
        phB += incb;
    }
    v->ph[0] = phA;
    v->ph[1] = phB;
    v->s[0] = lp;
    v->s[1] = prev;
    v->s[2] = dc;
}

/* name, {A-WAV, A-POS, FDBK, B>A, B-WAV, B-POS, B-LVL, RATIO}, {A D S R}, fenv, mono */
static const preset_t WT_PRESETS[] = {
    {"WT BASS",  {0, 24, 28, 18, 0,  0, 45, 1}, {0, 60, 70, 25},   25, 1, FX(0, 0, 10, 10),  PAT(2)},
    {"FM LEAD",  {0, 48,  8, 62, 0,  0,  0, 3}, {2, 70, 95, 35},   15, 1, FX(0, 0, 35, 20),  PAT(4)},
    {"GLASS FM", {1, 30,  0, 55, 1, 90,  0, 7}, {0, 95,  0, 90},   30, 0, FX(0, 20, 35, 55), PAT(7)},
    {"WT PAD",   {1, 52, 10, 14, 1, 78, 50, 4}, {75, 90, 110, 95}, 18, 0, FX(0, 45, 20, 65), PAT(5)},
    {"GROWL",    {0, 32, 55, 34, 0,  0, 25, 3}, {0, 55, 60, 30},   35, 1, FX(20, 0, 10, 10), PAT(2)},
    {"DIGITAL",  {1, 18, 50, 40, 1, 64, 20, 9}, {0, 65, 70, 40},   20, 0, FX(10, 0, 30, 20), PAT(11)},
    {"PLUCK",    {1, 62,  0,  0, 1, 62,  0, 3}, {0, 88,  0, 65},   48, 0, FX(0, 0, 30, 25),  PAT(3)},
    {"SWEEP",    {1, 40, 18, 22, 1, 40, 30, 5}, {40, 80, 90, 80},  40, 0, FX(0, 60, 40, 50), PAT(5)},
};

static const engine_t ENG_WT = {
    .name = "WAVETABLE",
    .page_title = {"OSC A", "OSC B"},
    .edit = {
        {"A-WAV", F_ENUM, 0, 4, 0, N_WT_WAV, 0},
        {"A-POS", F_PCT, 0, 127, 0, 0, 0},
        {"FDBK", F_PCT, 0, 127, 0, 0, 0},
        {"B>A", F_PCT, 0, 127, 0, 0, 0},
        {"B-WAV", F_ENUM, 0, 4, 0, N_WT_WAV, 0},
        {"B-POS", F_PCT, 0, 127, 0, 0, 0},
        {"B-LVL", F_PCT, 0, 127, 0, 0, 0},
        {"RATIO", F_ENUM, 0, (int16_t)(WT_RATIO_N - 1u), 3, N_WT_RATIO, 0},
    },
    .presets = WT_PRESETS,
    .npresets = NELEM(WT_PRESETS),
    .knob = {P_E1, P_E2, P_E3, P_E6},
    .note_on = wt_note_on,
    .render = wt_render,
    .block = wt_block,
};
