/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Shared DSP building blocks for the Felucca engines (all fixed point).
 * Voice output convention: add sample * amp to out[], where a full-scale
 * oscillator at amp = 1.0 (Q15 32767) contributes VOICE_FS. */
#define VOICE_FS 24000           /* per-voice level: one voice peaks near -6 dBFS before the master */

static inline int32_t mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
/* A signed left shift of a negative value is undefined in C. This shift, and a multiply that may wrap,
 * run in uint32_t and convert back to int32_t modulo 2^32 (GCC, Clang): the bits two's complement gave. */
static inline int32_t mul_wrap(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
static inline int32_t shl(int32_t x, uint32_t n) { return (int32_t)((uint32_t)x << n); }
/* a * k / 65536 without 64-bit: a up to 2^31, k Q16 */
static inline int32_t mulq16(int32_t a, uint32_t k)
{
    return (int32_t)(((a >> 16) * (int32_t)k) + (int32_t)(((uint32_t)(a & 0xFFFF) * k) >> 16));
}
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* sine, linearly interpolated between the 1024 table points (plain lookup: THD -55 dB) */
static inline int32_t sine_i(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = SINE[i], b = SINE[(i + 1u) & 1023u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}
static inline int32_t osc_sine(uint32_t ph) { return sine_i(ph); }

/* polyBLEP residual (Q15) around a wrap of a phase accumulator */
static inline int32_t blep(uint32_t ph, uint32_t inc)
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d)
        return 0;
    if (ph < inc) {
        x = (int32_t)(ph / d);                       /* 0..32767 */
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -(int32_t)((0xFFFFFFFFu - ph) / d);       /* -32767..0 */
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}

static inline int32_t osc_saw(uint32_t ph, uint32_t inc)
{
    return ((int32_t)(ph >> 16) - 32768) - blep(ph, inc);
}

static inline int32_t osc_pulse(uint32_t ph, uint32_t inc, uint32_t pw)
{
    return (osc_saw(ph, inc) - osc_saw(ph + pw, inc)) >> 1;
}

static inline int32_t osc_tri(uint32_t ph)
{
    int32_t s = (int32_t)(ph >> 15);                 /* 0..131071 */
    return s < 65536 ? s - 32768 : 98303 - s;
}

/* tanh(x / 32768) * 32767, linearly interpolated (a plain lookup gives
 * 256-unit steps, audible on quiet signals) */
static inline int32_t softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = TANH_Q15[256];                           /* continuous with the table */
    else
        y = TANH_Q15[i] + (((TANH_Q15[i + 1] - TANH_Q15[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}

/* MASTER's knobs (G_CLIP / G_PUNCH / G_DUCK), set once a block in fx.c mix_block: the clipper's coefficients,
 * the drum punch and the kick sidechain. Here for the engines' hit paths (DRUM-X, SAMPLE kits) */
static int16_t clip_eff, pnch_eff;
static volatile uint8_t duck_hit;                    /* a kick struck: fx.c ducks the other parts */
/* MASTER > PNCH: the punch gain of a hit by its age in blocks (0.73 ms), p 0..100: x1.46 attack, x1.70 body,
 * unity by 181 ms (JIANT 0.5; shared by every kit) */
static int32_t punch_gain(uint32_t age, int32_t p)
{
    int32_t up = 32768 + p * 150, body = 32768 + p * 230;
    return age < 4u ? up : age < 20u ? up + (body - up) * (int32_t)(age - 4u) / 16
                       : age < 160u ? body : age < 250u ? body + (32768 - body) * (int32_t)(age - 160u) / 90 : 32768;
}

/* xorshift32 */
static inline uint32_t noise32(int32_t *st)
{
    uint32_t s = (uint32_t)*st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = (int32_t)s;
    return s;
}

/* Trapezoidal SVF (A. Simper), unconditionally stable. Coefficients per
 * block in Q13; signals stay within +-150000 so products fit in 32 bits. */
typedef struct { int32_t a1, a2, a3; } tsvf_t;
/* Trapezoidal SVF (Simper), Q14 integrators (88 Hz needs the extra fraction bits). Damping k is Q12
 * (5793 = Butterworth). Coefs are shared across LR4 sections at the same cutoff; only the integrators
 * are per section. LP-only / HP-only helpers skip the unused output of the second half. */
typedef struct { int32_t g, a1, k; } ott_coef_t;
typedef struct { int64_t i1, i2; } ott_i_t;
static inline void ott_svf_coef(ott_coef_t *c, int32_t g, int32_t k)
{
    int32_t den = 4096 + ((g * (g + k)) >> 12);
    c->g = g;
    c->k = k;
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
}
static inline void ott_svf_run(const ott_coef_t *c, ott_i_t *s, int32_t in, int32_t *lp, int32_t *hp)
{
    int64_t inq = (int64_t)in << 14, v3 = inq - s->i2, v2, lim = (int64_t)4000000 << 14;
    int64_t v1 = ((int64_t)c->a1 * (s->i1 + (((int64_t)c->g * v3) >> 12))) >> 13;
    v2 = s->i2 + (((int64_t)c->g * v1) >> 12);
    s->i1 = 2 * v1 - s->i1;
    s->i2 = 2 * v2 - s->i2;
    if (s->i1 > lim) s->i1 = lim; else if (s->i1 < -lim) s->i1 = -lim;
    if (s->i2 > lim) s->i2 = lim; else if (s->i2 < -lim) s->i2 = -lim;
    *lp = (int32_t)(v2 >> 14);
    *hp = (int32_t)((inq - (((int64_t)c->k * v1) >> 12) - v2) >> 14);
}
static inline int32_t ott_svf_lp(const ott_coef_t *c, ott_i_t *s, int32_t in)
{
    int64_t v3 = ((int64_t)in << 14) - s->i2, v2, lim = (int64_t)4000000 << 14;
    int64_t v1 = ((int64_t)c->a1 * (s->i1 + (((int64_t)c->g * v3) >> 12))) >> 13;
    v2 = s->i2 + (((int64_t)c->g * v1) >> 12);
    s->i1 = 2 * v1 - s->i1;
    s->i2 = 2 * v2 - s->i2;
    if (s->i1 > lim) s->i1 = lim; else if (s->i1 < -lim) s->i1 = -lim;
    if (s->i2 > lim) s->i2 = lim; else if (s->i2 < -lim) s->i2 = -lim;
    return (int32_t)(v2 >> 14);
}
static inline int32_t ott_svf_hp(const ott_coef_t *c, ott_i_t *s, int32_t in)
{
    int64_t inq = (int64_t)in << 14, v3 = inq - s->i2, v2, lim = (int64_t)4000000 << 14;
    int64_t v1 = ((int64_t)c->a1 * (s->i1 + (((int64_t)c->g * v3) >> 12))) >> 13;
    v2 = s->i2 + (((int64_t)c->g * v1) >> 12);
    s->i1 = 2 * v1 - s->i1;
    s->i2 = 2 * v2 - s->i2;
    if (s->i1 > lim) s->i1 = lim; else if (s->i1 < -lim) s->i1 = -lim;
    if (s->i2 > lim) s->i2 = lim; else if (s->i2 < -lim) s->i2 = -lim;
    return (int32_t)((inq - (((int64_t)c->k * v1) >> 12) - v2) >> 14);
}
static inline void tsvf_coef_k(tsvf_t *c, int32_t cut, int32_t k)   /* cut: 0..127 << 8, k: damping Q12 */
{
    int32_t i, g, den;
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)                                       /* between table points: sweeps without 128 steps */
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
}
static inline void tsvf_coef(tsvf_t *c, int32_t cut, int32_t reso)   /* reso 0..127: damping 2.0 .. ~0.15 */
{
    tsvf_coef_k(c, cut, 8192 - reso * 7600 / 127);
}

static inline int32_t tsvf_lp(const tsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    return v2;
}

/* amplitude ramp over the block. Blocks are always CTL long, so x / CTL is a
 * shift (rounded towards zero, as a division); the compiler at -Os keeps
 * even constant divisions as a hardware divide, once per sample and voice */
#define CTL_LOG2 5
#if (1 << CTL_LOG2) != CTL
#error "CTL_LOG2 does not match CTL"
#endif
static inline int32_t amp_at(const vmod_t *m, uint32_t i)
{
    int32_t x = (m->amp1 - m->amp0) * (int32_t)i;
    return m->amp0 + ((x + ((x >> 31) & (CTL - 1))) >> CTL_LOG2);
}

/* a voice's sample s (Q15) at the amplitude ramp's sample i, scaled to the voice level (VOICE_FS);
 * engines whose signal peaks near half scale add it << 1 */
static inline int32_t voice_amp(int32_t s, const vmod_t *m, uint32_t i)
{
    return mulq15(mulq15(s, amp_at(m, i)), VOICE_FS);
}

/* linear up to k, then a soft knee: only the peaks above k saturate (|y| - k must stay < 2^30) */
static inline int32_t soft_knee(int32_t y, int32_t k)
{
    int32_t a = y < 0 ? -y : y;
    if (a <= k)
        return y;
    a = k + (softclip((a - k) * 2) >> 1);
    return y < 0 ? -a : a;
}

/* Feedback comb (ANALOG TYPE / insert FILTER COMB). CUT 0 = longest delay (lowest teeth), 127 =
 * shortest; RES sets feedback (0 = transparent). line[] stores samples at 1/4 scale.
 * line_len is a power of two (the callers' lines: AC_LEN, IC_LEN), so the wrap is a mask, and the
 * delay length (comb_delay_len) is block-constant: the caller passes it in. */
static inline uint32_t comb_delay_len(int32_t cut, uint32_t line_len)
{
    cut = clamp(cut, 0, 127);
    if (line_len < 3u)
        return 2u;
    return 2u + (uint32_t)(127 - cut) * (line_len - 3u) / 127u;
}
/* one sample; len = comb_delay_len(cut, line_len), once per block (the cut is block-constant: the
 * divide it holds would otherwise run once per sample, as would the three wraps below) */
static inline int32_t comb_process(int16_t *line, uint32_t line_len, uint16_t *wp,
                                   int32_t x, int32_t len, int32_t res)
{
    uint32_t mask = line_len - 1u;
    uint32_t w = (uint32_t)(*wp) & mask;
    int32_t d = line[(w + line_len - len) & mask];
    int32_t fbk = clamp(res, 0, 127) * 200;            /* Q15, up to ~0.77 */
    int32_t y = x + (mulq15(d, fbk) << 2);              /* restore stored 1/4 scale into the sum */
    y = clamp(y, -230000, 230000);
    line[w] = (int16_t)clamp(y >> 2, -32767, 32767);
    *wp = (uint16_t)((w + 1u) & mask);
    return y;
}

/* phase increment of pitch16 (1/16 semitone) plus ct cents: whole 1/16 semitones from the table,
 * the rest (and fine, in 1/4096) as a factor (1 ct = 2.367 / 4096) */
static inline uint32_t cents_inc(int32_t pitch16, int32_t ct, int32_t fine)
{
    int32_t d16 = ct * 16 / 100, rem = ct * 16 - d16 * 100;          /* rem: 1/1600 semitone */
    uint32_t inc = pitch_inc(clamp(pitch16 + d16, 0, 2047));
    return inc + (uint32_t)((int32_t)(inc >> 12) * (rem * 2367 / 16000 + fine));
}
