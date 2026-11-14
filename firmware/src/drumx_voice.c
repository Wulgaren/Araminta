/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant)
 * JIANT (FELUCCA TONIC): DRUM-X, after the idea of Sonic Charge's Microtonic (a tuned oscillator with a pitch
 * envelope and FM, a filtered noise generator, their envelopes, two patches and a MORPH between them), built anew
 * small and in fixed point for the FM-1. The voice as JIANT V10.7.24 has it (the V10.6q quality pass, the V10.6z8
 * kicks, the V10.7.8 guiro), without what no factory kit uses: the kit's TONE / HANDS / GLUE, each sound's FEEDBK
 * and SEND, the EXTENDED key map's hit FX. */
/* A DRUM-X sound (a lane of a kit) is dx_lane_t, 11 bytes: the mode and five values for each of its patches A and B:
 *   PITCH  the oscillator, MIDI 24..119 (33 Hz .. 7.9 kHz), 3/4 semitone a step
 *   PMOD   the pitch envelope: up to +4 octaves at the hit, falling with an eighth of the decay's tau (1..25 ms;
 *          the KICK's a sixth, up to 40 ms); GR: the SCRAPE's length
 *   DECAY  the amplitude's tau, 3 ms .. 1.5 s (exponential)
 *   NOISE  the mix: 0 the oscillator only .. 127 the noise only (the KICK's on a cube: a knock, not a snare)
 *   COLOR  the oscillator's FM index (SINE: its drive) and the noise filter's cutoff (30 Hz .. 16 kHz)
 * The mode: the oscillator (SINE, FM at 1:1, METAL at 1:1.47, BELL at 1:2.76), the noise filter (LP, BP, HP, GR the
 * guiro's scrape), the noise's envelope (as the oscillator's, or SNAP: a quarter of its tau, 8 ms at most); (JIANT 0.4,
 * the mode's bits 5..7) the pitch modulation, as Microtonic's: DECAY (the envelope above), LONG (its tau half the
 * decay's, up to 250 ms: the 808 boom), NOISE (a random pitch every 64 samples, scaled by the envelope: glitch, grit),
 * SINE (a sine at 1/8 of the pitch, scaled by the envelope: wobble, laser); and DRIVE (the oscillator x3 into a soft
 * clip: dense kicks, fat toms). Each sound's LAYER (dx_lay) adds a second oscillator (below).
 * MORPH (0 A .. 127 B) mixes the two patches value by value, read every block: it may move while a hit rings.
 * The hit runs at half rate (22 kHz) and comes back to full rate at the block's end. Envelopes per block (Q30 values,
 * Q16 factors, dx_fall), ramped linearly inside it; the pitch ramps too. A hit that has rung out (-66 dB) stops
 * computing (live 0) and writes zeros. Only 32-bit products. */
enum { DV_KICK, DV_SNARE, DV_CLAP, DV_HATC, DV_HATO, DV_TOM, DV_RIM, DV_BELL, DV_NLANE };   /* the kit's lanes */
enum { DXW_SINE, DXW_FM, DXW_METAL, DXW_BELL };
enum { DXF_LP, DXF_BP, DXF_HP, DXF_GR };                 /* (V10.7.8) GR: the guiro's scrape (dx_scrape) */
#define DX_MODE(w, f, snap) ((uint8_t)((w) | (f) << 2 | (snap) << 4))
enum { DXM_DECAY, DXM_LONG, DXM_NOISE, DXM_SINE };       /* (JIANT 0.4) the pitch modulation: mode bits 5..6 */
#define DX_DRIVE 0x80u                                   /* .. the oscillator driven: bit 7 */
enum { DXP_PITCH, DXP_PMOD, DXP_DECAY, DXP_NOISE, DXP_COLOR, DXP_N };
typedef struct {
    uint8_t mode;
    uint8_t a[DXP_N], b[DXP_N];                          /* 0..127 */
} dx_lane_t;
typedef struct {                                         /* a hit */
    uint32_t ph, ph2, inc;                               /* the phases; the last block's end increment */
    int32_t ea, en, ep;                                  /* amplitude, noise (Q30), pitch (Q15) envelopes */
    int32_t ic1, ic2, rng;                               /* the noise filter's state, the noise */
    uint32_t ph3;                                        /* (JIANT 0.4) the SINE pitch modulation's phase */
    int32_t hl;                                          /* (0.6.17) the last half-rate sample (the upsampling) */
    uint32_t ph4;                                        /* (0.6.49) LAYER's oscillator phase */
    uint8_t live, trig, choke;
    uint8_t sv;                                          /* (V10.7.8) the hit's velocity (eng_drum.c; 0 a full one): GR's
                                                          * stroke, long or short */
    uint16_t hpos;                                       /* half-rate samples since the hit: the CLAP's three claps, GR */
    int16_t pn;                                          /* the NOISE pitch modulation's value, held 64 samples (JIANT's
                                                          * block; ours is CTL) */
    uint8_t pb;                                          /* .. blocks since it was drawn */
    uint8_t tn;                                          /* (MODEL TEETH) the teeth still to strike */
    uint16_t tc;                                         /* .. half-rate samples to the next */
    int32_t ec;                                          /* (TEETH) the tooth's envelope, (CLICK) the hit's, Q15 */
    uint32_t mph[6];                                     /* (METAL) the six squares' phases */
    int32_t mlp, ms1;                                    /* (METAL) the sizzle's high-pass: its last output and input */
    int32_t mhp, mhx;                                    /* (METAL) its high-pass's last output and input */
} dx_voice_t;
#define DX_PN_BLK (CTL < 64 ? 64u / CTL : 1u)

/* Each sound's MODEL (dx_mdl; project.c keeps it in the kit's spare bits, dx_kit_pack): what its voice is. CLASSIC is
 * DRUM-X's own (JIANT's). The others are Felucca's DRUMS voices (its drums_voice.c, retired after 3.10), each reading the MODE's fields and
 * the knobs its own way:
 *   SHELL  the snare's body: two sines, the second at a ratio and level by the WAVE (1x none, 1.42 x0.18, 1.94 x1,
 *          1.6 x0.5), under the oscillator's envelope; SNAP makes the body a quarter of DECAY and leaves the noise
 *          whole (CLASSIC's SNAP is the other way round); the rest as CLASSIC (no FM, LAYER, COLOR drive)
 *   METAL  the hats', bell's and cymbal's source: up to six square waves at Felucca's ratios (the row: WAVE, + 4 with
 *          DRIVE), with the NOISE, through the FILTER at COLOR, then a one-pole high-pass (the pitch modulation's field:
 *          off 6.9 8.8 11.7 kHz, Felucca's hats' second filter), at full rate; PMOD the sizzle (the squares high-passed
 *          at 13 kHz, the noise with them, 0 .. 2 x the band, Felucca's). The band-pass Felucca's hats' (Q 0.8; SNAP:
 *          Q 1.25). One envelope,
 *          DECAY's
 *   TEETH  the clap: the noise through the FILTER (BP: wide, Q 0.7) and NOISE's bright copy (the same filter's
 *          high-pass, Felucca's), struck again 3..6 times (WAVE) PMOD apart (3.6 .. 29 ms), each
 *          tooth falling in 3.5 / 5 / 7 / 10 ms (the pitch modulation's field), then a tail on DECAY at a level by
 *          SNAP and DRIVE (0.07 0.2 0.27 0.57) from the last tooth. Noise only
 *   CLICK  the rim, the clave: two sines (the second's ratio: WAVE, + 4 with the FILTER's low bit; COLOR their balance,
 *          the second up to full by 64, then the first down to 0.2)
 *          under a fast envelope (DECAY: 0.5 .. 32 ms, not 3 ms .. 1.5 s) into a soft clip driven x1 .. x8 (the pitch
 *          modulation's field). No noise */
enum { DXV_CLASSIC, DXV_SHELL, DXV_METAL, DXV_TEETH, DXV_CLICK, DXV_N };
static uint8_t dx_mdl[8];
static const uint16_t DX_SHELL_R[4] = {4096, 5808, 7946, 6554};          /* the second sine's ratio, Q12 */
static const uint16_t DX_SHELL_G[4] = {0, 5900, 32767, 16384};           /* .. its level, Q15 */
static const uint16_t DX_CLICK_R[8] = {12288, 15030, 19700, 10240, 3023, 5546, 13312, 6062};   /* Q12: Felucca's rims
                                                          * 3 3.67 4.81 2.5, its 0.74 rim and 1.35 clave, its own
                                                          * rim's 3.25, 1.48 */
static const uint16_t DX_CLICK_D[4] = {4096, 8192, 24576, 32000};       /* the drive, Q12 (x1 x2 x6 x7.8) */
static const uint16_t DX_TEETH_K[4] = {64692, 64944, 65113, 65239};      /* a tooth's fall a half-rate sample, Q16:
                                                          * tau 3.5 5 7 10 ms */
static const uint16_t DX_TEETH_T[4] = {2185, 6554, 8738, 18569};         /* the tail's level against a tooth's, Q15 */
static const uint16_t DX_METAL_HP[4][2] = {{0, 0}, {10985, 5585}, {10071, 3757}, {8936, 1488}};   /* METAL's high-pass, Felucca's
                                                          * bilinear one-pole (Felucca's fdv_hp1): y = g (x - x1) + p y1,
                                                          * g p Q14, u = pi fc / FS: off 6.9 8.8 11.7 kHz */
static const uint16_t DX_METAL_R[8][6] = {               /* Felucca's (its FDV_METAL_R), Q12; 0: still */
    {4096, 6073, 7374, 10428, 10774, 15961},             /* the classic analog hats */
    {4096, 5800, 6560, 7530, 8810, 10650},               /* the cymbal */
    {4096, 5989, 10562, 14799, 0, 0},                    /* 10: four squares */
    {4096, 4203, 4368, 4836, 6059, 7486},                /* 55 hats: a high cluster */
    {4096, 12254, 17607, 21692, 25765, 43372},           /* 55 bell: wide apart */
    {4096, 4695, 5646, 10855, 13031, 15117},             /* 77: two triads of pulses */
    {4096, 6062, 0, 0, 0, 0},                            /* the bell's two squares (x1.48) */
    {0, 0, 0, 0, 0, 0},                                  /* 66: none (noise only) */
};

/* (V10.6q) the SINE's drive: 1 / tanh(its gain), Q12, every 8 COLOR (0..128): the driven sine's peak back to the sine's */
static const uint16_t DX_NRM[17] = {16724, 11014, 8422, 6987, 6105, 5526, 5131, 4854, 4655, 4511, 4406, 4328, 4270, 4249, 4249,
                                    4249, 4249};
/* the oscillator's modulator ratios (Q12), by wave */
static const uint16_t DX_RATIO[4] = {4096, 4096, 6021, 11305};

/* x octaves (x / 4096 of them, below 16) as a Q16 factor 2^(x / 4096): the whole octaves a shift, the rest t (the low
 * 12 bits) a third-order polynomial 1 + c1 t + c2 t^2 + c3 t^3 (off by 0.01 % at most) */
static __attribute__((noinline)) uint32_t dx_pow2(uint32_t x)
{
    uint32_t t = x & 0xfffu, acc;
    acc = 14851u + ((t * 5092u) >> 12);
    acc = 45594u + ((t * acc) >> 12);
    acc = 65536u + ((t * acc) >> 12);
    return acc << (x >> 12);
}
/* FM (DRUM's E5, 0..127) -> the FM index the hit strikes with (dx_run's ix: 1 = 0.0245 rad): (V10.6q) x^2 (x + 0.5)
 * up to 114 (0.84 rad at 64, 2.3 at 96, 3.7 at 114: a beater, a click, a metal edge), then a line to 9.3 rad at 127 */
static __attribute__((noinline)) int32_t dx_fm_env(int32_t fm)
{
    fm = clamp(fm, 0, 127);
    return fm > 114 ? 150 + (fm - 114) * (381 - 150) / 13 : fm * fm * (fm + 64) / 15422;
}
/* .. and kept under the half rate's Nyquist (11 kHz): the hit's highest sideband (fc + (I + 1) fm, the carrier fc,
 * the modulator fm = fc ratio / 4096) at 10.5 kHz at most; COLOR's index too. Below it every index as asked, above it
 * no sideband folded back */
static __attribute__((noinline)) int32_t dx_fm_lim(int32_t e, uint32_t inc, uint32_t ratio)
{
    int32_t fc = (int32_t)(((inc >> 16) * (uint32_t)FS) >> 16), fmh = (int32_t)(((uint32_t)fc * ratio) >> 12), lim;
    lim = fc >= 10500 ? 0 : (10500 - fc) * 41 / (fmh > 0 ? fmh : 1) - 41;
    return e < lim ? e : lim > 0 ? lim : 0;
}
/* what an envelope with a time constant of `us` microseconds keeps over one block, Q16: e^-a with a = CTL / (FS tau),
 * from the series of e^-a up to its a^4 / 24 term; a held at 0.70 (46000), where the fourth order still falls;
 * 1.0 written as 65535 */
#define DX_BLOCK_A (CTL * 65536.0e6 / FS)
static uint32_t dx_fall(uint32_t us)
{
    uint32_t a = (uint32_t)DX_BLOCK_A / (us | 1u), pw, keep;
    if (a > 46000u)
        a = 46000u;
    pw = (a * a) >> 16;
    keep = 65536u - a + pw / 2u;
    pw = (a * pw) >> 16;
    keep -= pw / 6u;
    pw = (a * pw) >> 16;
    keep += pw / 24u;
    return keep < 65536u ? keep : 65535u;
}

static void dx_trigger(dx_voice_t *v) { v->trig = 1; v->choke = 0; }
static void dx_choke(dx_voice_t *v) { if (v->live) v->choke = 1; }

/* a decay value (0..127) -> its tau in us: 3 ms x 2^(9 d / 127) */
static uint32_t dx_tau(uint32_t d) { return ((dx_pow2(d * 9u * 4096u / 127u) >> 8) * 3000u) >> 8; }

/* (0.6.49) LAYER's oscillator: a triangle (no table, a few operations; its odd partials add to the clang) */
static inline int32_t dx_tri(uint32_t ph)
{
    int32_t s = (int32_t)(ph >> 15);                     /* 0 .. 131071 */
    return s < 65536 ? s - 32768 : 98303 - s;
}

/* (V10.6q) the cowbell's square: a triangle clipped (its edge k, Q8: ~2 samples) */
static __attribute__((noinline)) int32_t dx_sq(uint32_t ph, int32_t k) { return clamp((dx_tri(ph) * k) >> 8, -32767, 32767); }

static dx_lane_t dx_kit[8];                              /* the music's kit (DX_KITS below sets it) */
/* (JIANT 0.6.49) LAYER, each sound's (the kit's: project.c keeps it in the kit's spare bits, dx_kit_pack): 0..15, a
 * second oscillator (a triangle) at sqrt 2 of the pitch (the most out of tune with it there is) frequency-modulated by
 * the first, more as it goes up (1..15: a metal edge .. a clang with no note: the sound melts into the rhythm and stays
 * out of the harmony); 0 none */
static uint8_t dx_lay[8];

/* the FM / LAYER loop (the FM waves, and a SINE with FM or LAYER): the carrier phase-modulated by its
 * modulator (index ix, stepping dix a sample), and (0.6.49) LAYER's triangle at sqrt 2 of it, frequency modulated by
 * it, its index up as LAYER's square, up to ~70 % of the mix (LAYER 0: none of it, its phase kept) */
static __attribute__((noinline)) void dx_layer(dx_voice_t *v, int32_t *y, uint32_t n, uint32_t *php, uint32_t *ph2p,
                                               int32_t inc, int32_t dinc, int32_t ga, int32_t dga, int32_t ix, int32_t dix,
                                               uint32_t ratio, int32_t lay)
{
    uint32_t i, ph = *php, ph2 = *ph2p, ph4 = v->ph4;
    int32_t lx = lay * lay * 600, lm = lay * 3;
    uint32_t st4 = lay ? 1448u : 0u;
    if (!ix && !dix)                                     /* (no FM on it: the modulator stands still, its phase kept) */
        ratio = 0;
    for (i = 0; i < n; i++, inc += dinc, ga += dga, ix += dix) {
        int32_t o = sine_i(ph + (uint32_t)(sine_i(ph2) * ix) * 512u), o2 = dx_tri(ph4 + (uint32_t)(o * lx));
        y[i] = mulq15(o + (((o2 - o) * lm) >> 6), ga >> 8);
        ph += (uint32_t)inc << 4;
        ph2 += ((uint32_t)inc >> 8) * ratio;
        ph4 += ((uint32_t)inc >> 6) * st4;               /* (x 22.6: x 16 the step, sqrt 2) */
    }
    *php = ph;
    *ph2p = ph2;
    v->ph4 = ph4;
}

/* (V10.6q) the 808's clap: three claps 11.6 ms apart, each a falling saw (squared) on the noise's gain at the block's
 * start and end, gn and *g1 (Q23), then the tail as it is */
static __attribute__((noinline)) int32_t dx_claps(dx_voice_t *v, uint32_t n, int32_t gn, int32_t *g1)
{
    uint32_t p0 = v->hpos;
    if (p0 < 768u) {
        int32_t a = 256 - (int32_t)(p0 & 255u), b = 255 - (int32_t)((p0 + n - 1u) & 255u);
        gn = (gn >> 16) * a * a;
        if (p0 + n < 768u)
            *g1 = (*g1 >> 16) * b * b;
    }
    v->hpos = (uint16_t)(p0 + n < 65535u ? p0 + n : 65535u);
    return gn;
}
/* (V10.7.8) the filter GR's stroke, on a half-rate block y[n] from the hit's sample sp: the guiro's ridges ticks (a
 * phase step sinc, 2^16 a tick), each falling as a square, the first eight growing (the stroke's swell); none past the
 * tick holding the stroke's end slen (the hit over by then: 1 back) */
static __attribute__((noinline)) int dx_scrape(dx_voice_t *v, int32_t *y, uint32_t n, uint32_t sp, uint32_t slen, uint32_t sinc)
{
    uint32_t kl = (slen * sinc) >> 16, i;
    for (i = 0; i < n; i++) {
        uint32_t u = (sp + i) * sinc, d = (65535u - (u & 65535u)) >> 1, t = u >> 16;
        y[i] = (y[i] * (int32_t)(t > kl ? 0u : ((d * d) >> 19) * (t < 8u ? 8u + t : 16u))) >> 15;
    }
    if (v->hpos == sp)
        v->hpos = (uint16_t)(sp + n);
    return ((sp + n) * sinc) >> 16 > kl;
}

/* (MODEL SHELL) the snare's body at half rate: two sines, the second's ratio and level DX_SHELL_R / _G[w], their sum
 * back to a sine's peak, under the oscillator's gain ga (Q23, stepping dga) */
static __attribute__((noinline)) void dx_shell(int32_t *y, uint32_t n, uint32_t *php, uint32_t *ph2p, int32_t inc,
                                               int32_t dinc, int32_t ga, int32_t dga, uint32_t w)
{
    uint32_t i, ph = *php, ph2 = *ph2p, r = DX_SHELL_R[w & 3u];
    int32_t g2 = DX_SHELL_G[w & 3u], nrm = (int32_t)((1u << 30) / (uint32_t)(32768 + g2));
    for (i = 0; i < n; i++, inc += dinc, ga += dga) {
        int32_t o = sine_i(ph) + ((sine_i(ph2) * g2) >> 15);
        y[i] = mulq15((o * nrm) >> 15, ga >> 8);
        ph += (uint32_t)inc << 4;
        ph2 += ((uint32_t)inc >> 8) * r;
    }
    *php = ph;
    *ph2p = ph2;
}
/* (MODEL CLICK) the rim at half rate: two sines (the second's ratio DX_CLICK_R[ix]) balanced by COLOR (col, 0..127:
 * up to 64 the second rises to full, then the first falls to 0.2, so either is on top, as Felucca's rims) under a fast
 * envelope (v->ec, Q30, falling by k a sample, Q16) into a soft clip driven d (Q12) with its level kept, at og */
static __attribute__((noinline)) void dx_click(dx_voice_t *v, int32_t *y, uint32_t n, uint32_t *php, uint32_t *ph2p,
                                               int32_t inc, int32_t dinc, int32_t og, uint32_t ix, int32_t col, uint32_t k,
                                               int32_t d)
{
    uint32_t i, ph = *php, ph2 = *ph2p, r = DX_CLICK_R[ix & 7u];
    int32_t e = v->ec, mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * d) >> 12));
    int32_t g1 = col > 64 ? 32767 - (col - 64) * 416 : 32767, g2 = col < 64 ? col * 512 : 32767;
    for (i = 0; i < n; i++, inc += dinc) {
        int32_t o = (((sine_i(ph) * g1) >> 15) + ((sine_i(ph2) * g2) >> 15)) >> 1;
        o = mulq15(o, e >> 15);
        y[i] = mulq15((softclip((o * d) >> 12) * mk) >> 15, og);
        e = mulq16(e, k);
        ph += (uint32_t)inc << 4;
        ph2 += ((uint32_t)inc >> 8) * r;
    }
    v->ec = e;
    *php = ph;
    *ph2p = ph2;
}
/* (MODEL TEETH) the clap's envelope on the filtered noise y (half rate): a tooth (v->ec, Q30) struck again every sp
 * samples while teeth remain (v->tn), each falling by k a sample (Q16); the tail (a, Q15, stepping da: DECAY's
 * envelope, held at full until then) at tl (Q15) from the last tooth on, silent before it (Felucca's fdv_clap_run) */
static __attribute__((noinline)) void dx_teeth(dx_voice_t *v, int32_t *y, uint32_t n, uint32_t sp, uint32_t k, int32_t tl,
                                               int32_t a, int32_t da)
{
    uint32_t i;
    int32_t e = v->ec;
    for (i = 0; i < n; i++, a += da) {
        if (v->tn && !--v->tc) {
            e = 1 << 30;
            v->tc = (uint16_t)sp;
            v->tn--;
        }
        y[i] = clamp(mulq15(y[i], (e >> 15) + (v->tn ? 0 : mulq15(a, tl))), -32767, 32767);
        e = mulq16(e, k);
    }
    v->ec = e;
}
/* (MODEL METAL) a whole block at full rate: the squares of row DX_METAL_R[row] at the pitch inc (each Felucca's +-1,
 * summed) at og and the noise at ng, through the FILTER flt at cx and the high-pass DX_METAL_HP[hp]; the sizzle (the
 * source through Felucca's 13 kHz high-pass) at sz (Q15, up to x2); under the envelope ga (Q23, stepping dga) */
static __attribute__((noinline)) void dx_metal(dx_voice_t *v, int32_t *y, uint32_t n, uint32_t row, uint32_t inc,
                                               int32_t og, int32_t ng, uint32_t flt, uint32_t narrow, int32_t cx, uint32_t hp,
                                               int32_t sz, int32_t ga, int32_t dga)
{
    const uint16_t *r = DX_METAL_R[row & 7u];
    uint32_t i, j, st[6];
    int32_t ic1 = v->ic1, ic2 = v->ic2, rng = v->rng, lp = v->mlp, hy = v->mhp, hx = v->mhx;
    int32_t hg = DX_METAL_HP[hp & 3u][0], hq = DX_METAL_HP[hp & 3u][1];
    int32_t kd = flt != DXF_BP ? 6000 : narrow ? 3277 : 5120, mr, ck, cv, s1 = v->ms1;   /* (BP: Felucca's hats' Q 0.8,
                                                          * narrow Q 1.25) */
    tsvf_t c;
    for (j = 0; j < 6u; j++)
        st[j] = (inc >> 12) * r[j];
    tsvf_coef_k(&c, cx << 8, kd);
    mr = flt == DXF_HP ? -1 : 0;                         /* (as dx_run's noise: LP v2, BP v1, HP r - k v1 - v2) */
    ck = flt == DXF_LP ? 0 : flt == DXF_HP ? kd : -4096;
    cv = flt == DXF_LP ? -1 : flt == DXF_HP ? 1 : 0;
    for (i = 0; i < n; i++, ga += dga) {
        int32_t sq = 0, x, v1, v2, v3;
        for (j = 0; j < 6u; j++) {
            v->mph[j] += st[j];
            sq += (int32_t)(v->mph[j] >> 31);
        }
        sq = sq * 10922 - 32766;                         /* six +-1 squares, Q15 (Felucca's fdv_metal_run) */
        x = mulq15(sq, og) + mulq15((int32_t)noise32(&rng) >> 16, ng);
        lp = (8505 * (((x * 1489) >> 10) - s1) + 626 * lp) >> 14;   /* (the sizzle: the source, squares and noise, back to
                                                          * the squares' scale (x 16 / 11), through Felucca's bilinear
                                                          * high-pass at 13 kHz, fdv_hp1: its model kits') */
        s1 = (x * 1489) >> 10;
        v3 = x - ic2;
        v1 = (c.a1 * ic1 + c.a2 * v3) >> 13;
        v2 = ic2 + ((c.a2 * ic1 + c.a3 * v3) >> 13);
        ic1 = clamp(2 * v1 - ic1, -150000, 150000);
        ic2 = clamp(2 * v2 - ic2, -150000, 150000);
        x = (x & mr) - ((ck * v1) >> 12) - cv * v2;
        if (hg) {                                        /* (the high-pass) */
            int32_t h = (hg * (x - hx) + hq * hy) >> 14;
            hx = x;
            x = hy = h;
        }
        x += mulq15(lp, sz);
        y[i] = mulq15(clamp(x, -32767, 32767), ga >> 8);
    }
    v->ic1 = ic1;
    v->ic2 = ic2;
    v->rng = rng;
    v->mlp = lp;
    v->mhp = hy;
    v->mhx = hx;
    v->ms1 = s1;
}

/* one block of lane L's hit v into y (n <= CTL): Q15, peaks near full scale. morph 0..127; tune16 1/16 semitones;
 * dofs / cofs / nofs move DECAY / COLOR / NOISE (-64..63). warp (DRUM's FOLD, 0..127): the oscillator through a
 * wavefolder after its envelope: the hit's loud start folds into bright partials, its tail unfolds back to the plain
 * wave (a West Coast drum); 0: the patch as it is. fm (DRUM's FM, 0..127): how much FM the hit strikes with, its
 * index following the pitch envelope down to the lane's own: an attack's beater / click / metal edge, the tail as the
 * kit has it. The oscillator and the noise each run their own loop, skipped when the mix leaves them out */
static __attribute__((noinline)) void dx_run(const dx_lane_t *L, dx_voice_t *v, int32_t morph, int32_t tune16,
                                             int32_t dofs, int32_t cofs, int32_t nofs, int32_t warp, int32_t fm, int32_t *y,
                                             uint32_t n)
{
    int32_t q[DXP_N], p16, depth, ea0, ea1, en1, ep1, og, ng, ga, dga, gn, dgn, kd, inc, dinc, mt, cw, cx;
    uint32_t i, k, wave = L->mode & 3u, flt = (L->mode >> 2) & 3u, ta, to, tn, tp, kb, inc1, sp, gr = flt == DXF_GR, slen = 0, sinc = 0;
    tsvf_t c;
    uint32_t nf = n, ln = L >= dx_kit && L < dx_kit + 8 ? (uint32_t)(L - dx_kit) : 0u, md = dx_mdl[ln] % DXV_N;
    uint32_t pmk = md <= DXV_SHELL ? (L->mode >> 5) & 3u : DXM_DECAY;   /* (the pitch modulation's field: TEETH, CLICK
                                                          * read it as their own) */
    int32_t lay = (int32_t)(dx_lay[ln] & 15u);
    gr = gr && md == DXV_CLASSIC;
    if (!v->live && !v->trig) {
        for (i = 0; i < n; i++)
            y[i] = 0;
        return;
    }
    if (nf < 2u) {                                       /* (one sample: the last one held, nothing moves) */
        y[0] = v->hl;
        return;
    }
    n >>= 1;                                             /* (0.6.17) at half rate (22 kHz); the block back to full rate
                                                          * at the end, linearly */
    morph = clamp(morph, 0, 127);
    for (k = 0; k < DXP_N; k++)
        q[k] = L->a[k] + ((L->b[k] - L->a[k]) * morph) / 127;
    q[DXP_DECAY] = clamp(q[DXP_DECAY] + dofs, 0, 127);
    for (k = 0; k < 2u; k++) {                           /* (V10.6q) COLOR, NOISE: the offset as it is where it fits, scaled
                                                          * to the room where it would clip; DECAY stays in octaves */
        int32_t o = clamp(k ? nofs : cofs, -64, 63), *x = &q[k ? DXP_NOISE : DXP_COLOR];
        *x += o > 0 ? o * (127 - *x < 63 ? 127 - *x : 63) / 63 : o * (*x < 64 ? *x : 64) / 64;
    }
    if (!ln)                                             /* (V10.6z8) the KICK's NOISE on a cube: the kit's few as good as
                                                          * none, the body whole; the knob up to 127 still there */
        q[DXP_NOISE] = (q[DXP_NOISE] * q[DXP_NOISE] * q[DXP_NOISE]) >> 14;
    ta = dx_tau((uint32_t)q[DXP_DECAY]);
    tn = (L->mode >> 4) & 1u ? (ta >> 2 < 8000u ? ta >> 2 : 8000u) : ta;   /* SNAP: a quarter, 8 ms at most */
    to = ta;                                             /* (the oscillator's) */
    if (md == DXV_SHELL && (L->mode >> 4) & 1u) {        /* (SHELL: SNAP shortens the body to a quarter instead, the
                                                          * noise rings on: Felucca's snare) */
        to = ta >> 2;
        tn = ta;
    }
    tp = pmk == DXM_LONG ? (uint32_t)clamp((int32_t)(ta / 2u), 4000, 250000)
                                           : (uint32_t)clamp((int32_t)(ta / (ln ? 8u : 6u)), 1000, ln ? 25000 : 40000);
    p16 = 384 + q[DXP_PITCH] * 12 + tune16;
    depth = gr || md == DXV_METAL || md == DXV_TEETH ? 0 : q[DXP_PMOD] * 6;   /* (GR, METAL, TEETH: PMOD theirs) */
    ea0 = v->live ? v->ea : 1 << 30;                     /* (V10.6q) a hit struck again while it rings rises from where
                                                          * it was over the block, its phases and filters as they were */
    if (v->trig) {                                       /* the hit: from the top */
        if (!v->live) {
            v->ph = v->ph2 = v->ph4 = 0;
            v->ic1 = v->ic2 = 0;
        }
        v->trig = 0;
        v->live = 1;
        v->ea = v->en = 1 << 30;
        v->ep = 32767;
        if (!v->rng)
            v->rng = 0x2545F491;
        v->hpos = 0;
        v->pb = 0;
        v->ec = 1 << 30;                                 /* (TEETH: the first tooth now; CLICK: its envelope) */
        v->tn = (uint8_t)(2u + wave);                    /* (TEETH: 3 .. 6 in all) */
        v->tc = (uint16_t)(80 + q[DXP_PMOD] * 9 / 2);
        v->inc = pitch_inc((uint32_t)clamp(p16 + depth, 0, 2047));
    }
    sp = v->hpos;                                        /* (GR: half-rate samples since the hit) */
    if (gr) {                                            /* (V10.7.8) GR, a band-pass whose hit is a stroke over the
                                                          * guiro's ridges: ticks at 24 .. 64 Hz (PITCH), held and swelling
                                                          * for the SCRAPE (PMOD's place: 0 .. 173 ms) times the velocity's
                                                          * 4th power (an accent the long stroke, a soft hit a "chk"), the
                                                          * last tick decaying with DECAY */
        int32_t sv = v->sv ? v->sv : 127;
        sv = (sv * sv * sv * sv) >> 14;
        slen = (uint32_t)((q[DXP_PMOD] * 31 * sv) >> 14);
        sinc = (uint32_t)((3048 + 40 * q[DXP_PITCH]) * 3) >> 7;
        flt = DXF_BP;
    }
    k = 11890000u / ((pitch_inc((uint32_t)clamp(p16, 0, 2047)) >> 12) | 1u);   /* (V10.6q) half its period (us): no shorter */
    kb = v->choke ? dx_fall(1500u) : dx_fall(to > k ? to : k);   /* (a choked hit: gone in ~5 ms) */
    ea1 = mulq16(v->ea, kb);
    en1 = mulq16(v->en, v->choke ? kb : dx_fall(tn));
    ep1 = (int32_t)(((uint32_t)v->ep * dx_fall(tp)) >> 16);
    if (sp < slen || (md == DXV_TEETH && v->tn)) {      /* (GR: the stroke held; TEETH: the tail from the last tooth) */
        ea1 = v->ea;
        en1 = v->en;
    }
    {
        uint32_t pm = pmk;
        int32_t off = (depth * ep1) >> 15;              /* DECAY, LONG: the envelope */
        if (pm == DXM_NOISE) {                          /* (JIANT 0.4) a random pitch every 64 samples, the envelope's depth */
            if (!(v->pb % DX_PN_BLK)) {
                int32_t r = v->rng ? v->rng : 0x2545F491;
                v->pn = (int16_t)((int32_t)noise32(&r) >> 17);
                v->rng = r;
            }
            v->pb++;
            off = (off * v->pn) >> 14;
        } else if (pm == DXM_SINE) {                    /* .. a sine at 1/8 of the pitch */
            v->ph3 += v->inc * (CTL / 8u);
            off = (off * sine_i(v->ph3)) >> 15;
        }
        inc1 = pitch_inc((uint32_t)clamp(p16 + off, 0, 2047));
    }
    if (md == DXV_METAL) {                               /* (MODEL METAL: its own block, at full rate) */
        int32_t g0 = (ea0 >> 15) << 8, nz = q[DXP_NOISE] * 258;
        dx_metal(v, y, nf, wave | (L->mode & DX_DRIVE ? 4u : 0u), inc1, ((32767 - nz) * 11) >> 4, (nz * 11) >> 4,
                 flt == DXF_GR ? DXF_BP : flt, (L->mode >> 4) & 1u, q[DXP_COLOR], (L->mode >> 5) & 3u, q[DXP_PMOD] * 516, g0,
                 (((ea1 >> 15) << 8) - g0) / (int32_t)nf);
        v->hl = y[nf - 1u];
        v->ea = v->en = ea1;
        v->ep = ep1;
        v->inc = inc1;
        if (v->ea < (1 << 19)) {
            v->live = 0;
            v->hl = 0;
        }
        return;
    }
    ng = md == DXV_TEETH ? 127 * 258 : md == DXV_CLICK ? 0 : q[DXP_NOISE] * 258;   /* (TEETH noise only, CLICK none) */
    og = ((32767 - ng) * 11) >> 4;                       /* (x 11 / 16: about the DRUM kits' level) */
    ng = (ng * 11) >> 4;
    cw = wave == DXW_METAL && ln == DV_BELL && md == DXV_CLASSIC;             /* (V10.6q) the cowbell: the CMOS one (below), its band on its
                                                          * pitch, between its two squares (its COLOR the timbre) */
    cx = cw ? clamp(((p16 - 360) * 19 >> 8) + 4, 0, 113) : q[DXP_COLOR] + (!ln << 5);   /* (the filter's cutoff; the
                                                          * KICK's 2 1/4 octaves up: a knock, a beater) */
    if (flt < DXF_HP && cx < 96)                         /* (V10.6q) LP, BP: the gain back as 1 / sqrt(fc) to COLOR 96's
                                                          * (x8 at most): a dark snare or clap as loud as a bright one */
        ng = (ng * (int32_t)(dx_pow2((uint32_t)(cx < 12 ? 84 : 96 - cx) * 146u) >> 4)) >> 12;
    ga = mulq15(og, cw ? (ea0 >> 16) + (v->en >> 16) : ea0 >> 15) << 8;   /* the oscillator's gain, Q23, stepped (the
                                                          * cowbell's in two stages: half on the noise's SNAP, a click,
                                                          * half on the decay, the tail) */
    dga = ((mulq15(og, cw ? (ea1 >> 16) + (en1 >> 16) : ea1 >> 15) << 8) - ga) / (int32_t)n;
    gn = mulq15(ng >> 3, md == DXV_TEETH ? 32767 : v->en >> 15) << 11;   /* the noise's (ng up to x8: >> 3; TEETH:
                                                          * steady, its teeth after the filter) */
    {
        int32_t g1 = md == DXV_TEETH ? gn : mulq15(ng >> 3, en1 >> 15) << 11;
        if (ln == DV_CLAP && v->hpos < 1200u && md == DXV_CLASSIC)   /* (V10.6q) the 808's claps */
            gn = dx_claps(v, n, gn, &g1);
        dgn = (g1 - gn) / (int32_t)n;
    }
    inc = (int32_t)(v->inc >> 4) * 2;                    /* (>> 4: the step fits; x 2: half rate) */
    dinc = ((int32_t)(inc1 >> 4) * 2 - inc) / (int32_t)n;
    if (og) {                                            /* the oscillator */
        uint32_t ph = v->ph;
        int32_t fold = clamp(warp, 0, 127);
        fm = clamp(fm, 0, 127);
        if (cw) {                                        /* (V10.6q) the 808's cowbell: two squares at the pitch and x1.47,
                                                          * each a triangle clipped to an edge of ~2 samples, through the
                                                          * band-pass (below); COLOR the timbre: 0 their mix .. 64 their
                                                          * ring .. 127 A's phase driven by B (FM) */
            uint32_t ph2 = v->ph2, st = (uint32_t)inc << 4;
            int32_t u = (q[DXP_COLOR] - 64) * 520, t = u < 0 ? u + 33280 : 32767;
            int32_t ka = (int32_t)((1u << 29) / ((st >> 8) | 1u)), kq = (ka * 2787) >> 12, um;
            u = u < 0 ? 0 : u;
            um = u + ((fm ? dx_fm_env(fm) * v->ep >> 15 : 0) * 43);   /* (FM: A's modulation struck with the hit) */
            for (i = 0; i < n; i++, inc += dinc, ga += dga) {
                int32_t b = dx_sq(ph2, kq), a, o;
                a = dx_sq(ph + ((uint32_t)(dx_tri(ph2) * um) << 1), ka);   /* (A's phase driven by B's triangle) */
                o = (a + b) >> 2;                        /* (half scale: the sums fit) */
                o += mulq15(((mulq15(a, b) * 3) >> 2) - o, t);   /* (the ring x1.5) */
                o += mulq15(((a * 3) >> 2) - o, u);
                y[i] = mulq15(o << 1, ga >> 8);
                st = (uint32_t)inc << 4;
                ph += st;
                ph2 += (st >> 12) * 6021u;
            }
            v->ph2 = ph2;
        } else if (md == DXV_SHELL) {
            uint32_t ph2 = v->ph2;
            dx_shell(y, n, &ph, &ph2, inc, dinc, ga, dga, wave);
            v->ph2 = ph2;
        } else if (md == DXV_CLICK) {                    /* (DECAY: tau 0.5 .. 32 ms, a fall a half-rate sample) */
            uint32_t ph2 = v->ph2, tu = (dx_pow2((uint32_t)q[DXP_DECAY] * 6u * 4096u / 127u) >> 8) * 500u >> 8;
            uint32_t a = (uint32_t)(2.0 * 65536.0e6 / FS) / (tu | 1u);
            dx_click(v, y, n, &ph, &ph2, inc, dinc, og, wave | (flt & 1u) << 2, q[DXP_COLOR],
                     65536u - a + ((a * a) >> 17), DX_CLICK_D[(L->mode >> 5) & 3u]);
            v->ph2 = ph2;
            ea1 = v->ec;                                 /* (its envelope the hit's: it ends with it) */
        } else if (wave == DXW_SINE && !fm && !lay) {    /* drive x0.25 (as good as clean) .. x2.5 into the soft clip,
                                                          * its peak back to the sine's: a colour, not a level */
            int32_t g = 1024 + q[DXP_COLOR] * 72, j = q[DXP_COLOR] >> 3, nrm = DX_NRM[j] + (((DX_NRM[j + 1] - DX_NRM[j]) * (q[DXP_COLOR] & 7)) >> 3);
            for (i = 0; i < n; i++, inc += dinc, ga += dga) {
                y[i] = mulq15((softclip((sine_i(ph) * g) >> 12) * nrm) >> 12, ga >> 8);
                ph += (uint32_t)inc << 4;
            }
        } else {                                         /* FM (COLOR's index), and the FM knob's: the FM struck with the hit and
                                                          * gone with the pitch envelope; a SINE gets its octave as the
                                                          * modulator, the other waves their own ratio and COLOR's index
                                                          * under it */
            uint32_t ph2 = v->ph2, ratio = wave == DXW_SINE ? 8192u : DX_RATIO[wave];
            int32_t base = wave == DXW_SINE ? 0 : dx_fm_lim(q[DXP_COLOR] * 3, inc1, ratio), fe = fm ? dx_fm_lim(dx_fm_env(fm), inc1, ratio) : 0;
            int32_t ix = base + ((fe * v->ep) >> 15), dix = ((base + ((fe * ep1) >> 15)) - ix) / (int32_t)n;
            dx_layer(v, y, n, &ph, &ph2, inc, dinc, ga, dga, ix, dix, ratio, lay);
            v->ph2 = ph2;
        }
        v->ph = ph;
        if (L->mode & DX_DRIVE) {                       /* (JIANT 0.4) DRIVE: x3 into the soft clip, the level near */
            int32_t dl = 14000 + !ln * 8000;             /* (the KICK's 22000: its punch) */
            for (i = 0; i < n; i++)
                y[i] = (softclip(y[i] * 3) * dl) >> 15;
        }
        if (fold) {                                      /* (JIANT 0.5) FOLD: after the envelope, so it follows it */
            int32_t g = 16 + ((fold * fold * 5) >> 10), c2 = g * 6144 / (8 + g);   /* (Q4: x1 .. x5.9 on fold^2; out x 1.5
                                                          * g / (0.5 + g), Q12: x1 at the start) */
            for (i = 0; i < n; i++) {
                int32_t w = clamp((y[i] * g) >> 4, -5 * 16384, 5 * 16384), a;
                for (a = 0; a < 3; a++)
                    w = w > 16384 ? 32768 - w : w < -16384 ? -32768 - w : w;
                y[i] = (w * c2) >> 12;
            }
        }
    } else {
        for (i = 0; i < n; i++)
            y[i] = 0;
    }
    mt = md == DXV_CLASSIC && wave == DXW_METAL && (0x98u >> ln) & 1u ? -1 : 0;   /* (V10.6q) the hats' METAL through the filter with the
                                                          * noise, in their band (its clank at 1 .. 2.5 kHz was 13 % of a
                                                          * hat) */
    if (ng || mt) {                                      /* the noise (and METAL) through its filter */
        int32_t ic1 = v->ic1, ic2 = v->ic2, rng = v->rng;
        int32_t mr, ck, cv;                              /* the output r & mr - k v1 - cv v2: LP v2, BP v1, HP r - k v1 - v2 */
        int32_t bq = md == DXV_TEETH ? q[DXP_NOISE] * 258 : 0;   /* (TEETH: + the bright copy, the HP, at NOISE) */
        kd = flt == DXF_BP && md != DXV_TEETH ? 2400 : 6000;   /* (BP a little resonant; TEETH's wide, Q 0.7) */
        tsvf_coef_k(&c, (cx + 14) << 8, kd);             /* (+ an octave of the table: the same Hz at half rate) */
        mr = flt == DXF_LP || flt == DXF_BP ? 0 : -1;
        ck = flt == DXF_LP ? 0 : flt == DXF_BP ? -4096 : kd;
        cv = flt == DXF_LP ? -1 : flt == DXF_BP ? 0 : 1;
        for (i = 0; i < n; i++, gn += dgn) {
            int32_t r = mulq15((((int32_t)noise32(&rng) >> 17) * 23170) >> 15, gn >> 8) + ((y[i] & mt) >> 1), v3 = r - ic2, v1, v2;
                                                         /* (+-11585: 1/sqrt2, half rate: the same power in half the
                                                          * band; its gain before the filter) */
            v1 = (c.a1 * ic1 + c.a2 * v3) >> 13;
            v2 = ic2 + ((c.a2 * ic1 + c.a3 * v3) >> 13);
            ic1 = clamp(2 * v1 - ic1, -150000, 150000);
            ic2 = clamp(2 * v2 - ic2, -150000, 150000);
            y[i] = (y[i] & ~mt) + clamp(shl((r & mr) - ((ck * v1) >> 12) - cv * v2 +
                                            mulq15(clamp(r - ((kd * v1) >> 12) - v2, -65535, 65535), bq), 1), -32767, 32767);
        }
        v->ic1 = ic1;
        v->ic2 = ic2;
        v->rng = rng;
    }
    if (gr && dx_scrape(v, y, n, sp, slen, sinc))      /* (GR: past its last tick, the hit is over) */
        ea1 = en1 = 0;
    if (md == DXV_TEETH) {                               /* (the teeth and the tail on the filtered noise) */
        int32_t a0 = ea0 >> 15;
        dx_teeth(v, y, n, 80u + (uint32_t)q[DXP_PMOD] * 9u / 2u, DX_TEETH_K[(L->mode >> 5) & 3u],
                 DX_TEETH_T[((L->mode >> 4) & 1u) | (L->mode & DX_DRIVE ? 2u : 0u)], a0, ((ea1 >> 15) - a0) / (int32_t)n);
    }
    if (md >= DXV_TEETH)                                 /* (TEETH, CLICK: the hit ends with its own envelope) */
        en1 = ea1;
    for (i = n; i-- > 0;) {                              /* (0.6.17) back to full rate: each half-rate sample and the
                                                          * mean of it and the one before (the last block's: v->hl) */
        int32_t a = y[i], b = i ? y[i - 1] : v->hl;
        y[2u * i + 1u] = a;
        y[2u * i] = (a + b) >> 1;
    }
    if (nf & 1u)                                         /* (an odd block: its last sample held) */
        y[nf - 1u] = y[nf - 2u];
    v->hl = y[nf - 1u];
    v->ea = ea1;
    v->en = en1;
    v->ep = ep1;
    v->inc = inc1;
    if (v->ea < (1 << 19) && v->en < (1 << 19)) {       /* -66 dB */
        v->live = 0;
        v->hl = 0;
    }
}

/* the factory kits: DRUM's presets (eng_drum.c DRUM_PRESETS, one a kit, in this order), each the eight sounds ({mode,
 * A, B}, A and B {PITCH PMOD DECAY NOISE COLOR}), their LAYERs and their MODELs: JIANT V10.7.24's (its SENDs, all 0 since
 * V10.7.10, left out; every sound CLASSIC), then Felucca's DRUMS kits STD 80 10 66 55 77 translated from its numbers
 * (DRUMS retired: eng_drum.c drums_kit; a MODEL reads the MODE's fields its own way). The first is the
 * factory kit (DX_KIT_DEF). dx_kit is the music's kit: the project's (project.c
 * keeps it), edited on EDIT > SOUND (ui_dx.c). Loaded whole by a preset (ui.c apply_preset: dx_kit_load); INIT SOUND
 * and a new project take the first */
typedef struct { dx_lane_t lane[8]; uint8_t lay[8], mdl[8]; } dx_kit_t;
#define DX_KIT0_LANES {                                                                                                   \
    {DX_MODE(DXW_SINE, DXF_LP, 1), {9, 70, 72, 4, 20}, {14, 100, 88, 10, 60}},          /* KICK */                       \
    {DX_MODE(DXW_FM, DXF_BP, 0), {40, 30, 52, 80, 90}, {46, 18, 64, 104, 70}},          /* SNARE */                      \
    {DX_MODE(DXW_SINE, DXF_BP, 0), {60, 0, 46, 127, 84}, {54, 0, 58, 127, 76}},         /* CLAP */                       \
    {DX_MODE(DXW_METAL, DXF_HP, 0), {100, 0, 22, 96, 116}, {108, 0, 14, 112, 122}},     /* HAT CL */                     \
    {DX_MODE(DXW_METAL, DXF_HP, 0), {100, 0, 62, 96, 112}, {108, 0, 74, 112, 118}},     /* HAT OP */                     \
    {DX_MODE(DXW_SINE, DXF_HP, 1), {30, 40, 66, 14, 54}, {36, 60, 76, 20, 74}},         /* TOM */                        \
    {DX_MODE(DXW_BELL, DXF_HP, 1), {63, 10, 14, 16, 46}, {70, 24, 20, 30, 60}},         /* RIM */                        \
    {DX_MODE(DXW_METAL, DXF_BP, 1), {64, 0, 44, 34, 6}, {68, 0, 54, 46, 28}},           /* BELL */                       \
}
#define DX_KIT0_LAY {0, 6, 0, 0, 0, 0, 0, 0}
static const dx_kit_t DX_KITS[] = {
    {DX_KIT0_LANES, DX_KIT0_LAY},                        /* DRUM-X */
    {   /* 909 CLUB */
        {
            {DX_MODE(DXW_SINE, DXF_LP, 1) | DX_DRIVE, {7, 90, 68, 4, 50}, {12, 120, 84, 10, 90}},   /* KICK */
            {DX_MODE(DXW_FM, DXF_BP, 0), {40, 34, 60, 92, 104}, {46, 22, 72, 116, 84}},   /* SNARE */
            {DX_MODE(DXW_SINE, DXF_BP, 0), {60, 0, 52, 127, 88}, {54, 0, 64, 127, 80}},   /* CLAP */
            {DX_MODE(DXW_METAL, DXF_HP, 0), {106, 0, 16, 96, 126}, {114, 0, 8, 112, 127}},   /* HAT CL */
            {DX_MODE(DXW_METAL, DXF_HP, 0), {106, 0, 66, 96, 122}, {114, 0, 78, 112, 127}},   /* HAT OP */
            {DX_MODE(DXW_SINE, DXF_HP, 1), {32, 54, 66, 14, 68}, {38, 74, 76, 20, 88}},   /* TOM */
            {DX_MODE(DXW_BELL, DXF_HP, 1), {65, 10, 12, 16, 50}, {72, 24, 18, 30, 64}},   /* RIM */
            {DX_MODE(DXW_METAL, DXF_BP, 1), {64, 0, 44, 34, 6}, {68, 0, 54, 46, 28}},   /* BELL */
        },
        {0, 6, 0, 0, 0, 0, 0, 0},
    },
    {   /* 808 BOOM */
        {
            {DX_MODE(DXW_SINE, DXF_LP, 1) | DXM_LONG << 5, {3, 40, 102, 0, 6}, {8, 70, 118, 6, 46}},   /* KICK */
            {DX_MODE(DXW_SINE, DXF_BP, 0), {36, 18, 46, 90, 66}, {42, 6, 58, 114, 46}},   /* SNARE */
            {DX_MODE(DXW_SINE, DXF_BP, 0), {56, 0, 50, 127, 70}, {50, 0, 62, 127, 62}},   /* CLAP */
            {DX_MODE(DXW_METAL, DXF_HP, 0), {92, 0, 14, 96, 118}, {100, 0, 6, 112, 124}},   /* HAT CL */
            {DX_MODE(DXW_METAL, DXF_HP, 0), {92, 0, 60, 96, 114}, {100, 0, 72, 112, 120}},   /* HAT OP */
            {DX_MODE(DXW_SINE, DXF_HP, 1) | DXM_LONG << 5, {24, 26, 82, 14, 42}, {30, 46, 92, 20, 62}},   /* TOM */
            {DX_MODE(DXW_BELL, DXF_HP, 1), {65, 10, 14, 16, 38}, {72, 24, 20, 30, 52}},   /* RIM */
            {DX_MODE(DXW_METAL, DXF_BP, 1), {62, 0, 56, 34, 0}, {66, 0, 66, 46, 16}},   /* BELL */
        },
        {0, 4, 0, 0, 0, 0, 0, 0},
    },
    {   /* LOFI DUST */
        {
            {DX_MODE(DXW_SINE, DXF_LP, 1), {11, 78, 58, 14, 0}, {16, 108, 74, 20, 40}},   /* KICK */
            {DX_MODE(DXW_FM, DXF_BP, 1), {44, 30, 36, 104, 68}, {50, 18, 48, 127, 48}},   /* SNARE */
            {DX_MODE(DXW_SINE, DXF_BP, 0), {62, 0, 36, 127, 60}, {56, 0, 48, 127, 52}},   /* CLAP */
            {DX_MODE(DXW_METAL, DXF_BP, 0), {104, 0, 12, 96, 124}, {112, 0, 4, 112, 127}},   /* HAT CL */
            {DX_MODE(DXW_METAL, DXF_BP, 0), {104, 0, 48, 96, 120}, {112, 0, 60, 112, 126}},   /* HAT OP */
            {DX_MODE(DXW_SINE, DXF_HP, 1), {32, 40, 52, 20, 34}, {38, 60, 62, 30, 54}},   /* TOM */
            {DX_MODE(DXW_BELL, DXF_HP, 1), {69, 10, 8, 16, 30}, {76, 24, 14, 30, 44}},   /* RIM */
            {DX_MODE(DXW_METAL, DXF_BP, 1), {66, 0, 32, 34, 10}, {70, 0, 42, 46, 36}},   /* BELL */
        },
        {0, 9, 0, 0, 0, 0, 5, 0},
    },
    {   /* INDUSTRIAL */
        {
            {DX_MODE(DXW_FM, DXF_LP, 1) | DX_DRIVE, {7, 104, 80, 18, 64}, {12, 127, 96, 24, 104}},   /* KICK */
            {DX_MODE(DXW_METAL, DXF_HP, 0), {42, 54, 64, 102, 124}, {48, 42, 76, 126, 104}},   /* SNARE */
            {DX_MODE(DXW_SINE, DXF_BP, 0), {60, 0, 54, 127, 108}, {54, 0, 66, 127, 100}},   /* CLAP */
            {DX_MODE(DXW_METAL, DXF_HP, 0), {100, 0, 32, 106, 127}, {108, 0, 24, 122, 127}},   /* HAT CL */
            {DX_MODE(DXW_METAL, DXF_HP, 0), {100, 0, 76, 106, 127}, {108, 0, 88, 122, 127}},   /* HAT OP */
            {DX_MODE(DXW_SINE, DXF_HP, 1) | DX_DRIVE, {30, 64, 76, 20, 88}, {36, 84, 86, 30, 108}},   /* TOM */
            {DX_MODE(DXW_BELL, DXF_HP, 1) | DXM_NOISE << 5, {63, 22, 20, 28, 70}, {70, 36, 26, 42, 84}},   /* RIM */
            {DX_MODE(DXW_METAL, DXF_BP, 1) | DXM_NOISE << 5, {64, 0, 48, 46, 96}, {68, 0, 58, 58, 120}},   /* BELL */
        },
        {0, 12, 0, 0, 0, 0, 6, 0},
    },
    {   /* ACOUSTIC */
        {
            {DX_MODE(DXW_SINE, DXF_LP, 1), {9, 60, 64, 12, 0}, {14, 90, 80, 18, 34}},   /* KICK */
            {DX_MODE(DXW_BELL, DXF_BP, 0), {38, 30, 56, 98, 74}, {44, 18, 68, 122, 54}},   /* SNARE */
            {DX_MODE(DXW_SINE, DXF_BP, 0), {58, 0, 48, 127, 66}, {52, 0, 60, 127, 58}},   /* CLAP */
            {DX_MODE(DXW_METAL, DXF_HP, 0), {96, 0, 18, 96, 116}, {104, 0, 10, 112, 122}},   /* HAT CL */
            {DX_MODE(DXW_METAL, DXF_HP, 0), {96, 0, 66, 96, 112}, {104, 0, 78, 112, 118}},   /* HAT OP */
            {DX_MODE(DXW_BELL, DXF_HP, 1), {28, 28, 70, 20, 8}, {34, 48, 80, 30, 28}},   /* TOM */
            {DX_MODE(DXW_BELL, DXF_HP, 1), {61, 10, 16, 16, 26}, {68, 24, 22, 30, 40}},   /* RIM */
            {DX_MODE(DXW_METAL, DXF_BP, 1), {62, 0, 46, 34, 20}, {66, 0, 56, 46, 44}},   /* BELL */
        },
        {0, 5, 0, 0, 0, 0, 0, 0},
    },
    {   /* DUB ROOTS */
        {
            {DX_MODE(DXW_SINE, DXF_LP, 1) | DXM_LONG << 5, {3, 58, 92, 4, 0}, {8, 88, 108, 10, 38}},   /* KICK */
            {DX_MODE(DXW_FM, DXF_BP, 0), {34, 30, 64, 88, 80}, {40, 18, 76, 112, 60}},   /* SNARE */
            {DX_MODE(DXW_SINE, DXF_BP, 0), {58, 0, 52, 127, 72}, {52, 0, 64, 127, 64}},   /* CLAP */
            {DX_MODE(DXW_METAL, DXF_HP, 0), {98, 0, 20, 96, 122}, {106, 0, 12, 112, 127}},   /* HAT CL */
            {DX_MODE(DXW_METAL, DXF_HP, 0), {98, 0, 68, 96, 118}, {106, 0, 80, 112, 124}},   /* HAT OP */
            {DX_MODE(DXW_SINE, DXF_HP, 1), {26, 40, 76, 14, 44}, {32, 60, 86, 20, 64}},   /* TOM */
            {DX_MODE(DXW_BELL, DXF_HP, 1), {59, 10, 24, 16, 46}, {66, 24, 30, 30, 60}},   /* RIM */
            {DX_MODE(DXW_METAL, DXF_BP, 1), {64, 0, 52, 34, 8}, {68, 0, 62, 46, 40}},   /* BELL */
        },
        {0, 6, 0, 0, 0, 0, 0, 0},
    },
    {   /* TECHNO RAW */
        {
            {DX_MODE(DXW_FM, DXF_LP, 1) | DX_DRIVE, {7, 96, 74, 10, 56}, {12, 126, 90, 16, 96}},   /* KICK */
            {DX_MODE(DXW_FM, DXF_HP, 0), {40, 40, 46, 102, 110}, {46, 28, 58, 126, 90}},   /* SNARE */
            {DX_MODE(DXW_SINE, DXF_BP, 0), {60, 0, 48, 127, 94}, {54, 0, 60, 127, 86}},   /* CLAP */
            {DX_MODE(DXW_METAL, DXF_HP, 0), {104, 0, 14, 100, 127}, {112, 0, 6, 116, 127}},   /* HAT CL */
            {DX_MODE(DXW_METAL, DXF_HP, 0), {104, 0, 62, 100, 127}, {112, 0, 74, 116, 127}},   /* HAT OP */
            {DX_MODE(DXW_METAL, DXF_HP, 1), {30, 52, 68, 16, 46}, {36, 72, 78, 26, 66}},   /* TOM */
            {DX_MODE(DXW_BELL, DXF_HP, 1), {65, 10, 10, 22, 58}, {72, 24, 16, 36, 72}},   /* RIM */
            {DX_MODE(DXW_METAL, DXF_BP, 1), {64, 0, 44, 40, 70}, {68, 0, 54, 52, 96}},   /* BELL */
        },
        {0, 7, 0, 0, 0, 0, 0, 0},
    },
    {   /* BREAKS */
        {
            {DX_MODE(DXW_SINE, DXF_LP, 1), {11, 78, 68, 12, 32}, {16, 108, 84, 18, 72}},   /* KICK */
            {DX_MODE(DXW_FM, DXF_BP, 1), {42, 38, 54, 96, 112}, {48, 26, 66, 120, 92}},   /* SNARE */
            {DX_MODE(DXW_SINE, DXF_BP, 0), {60, 0, 48, 127, 94}, {54, 0, 60, 127, 86}},   /* CLAP */
            {DX_MODE(DXW_METAL, DXF_BP, 0), {100, 0, 26, 102, 116}, {108, 0, 18, 118, 122}},   /* HAT CL */
            {DX_MODE(DXW_METAL, DXF_BP, 0), {100, 0, 70, 102, 112}, {108, 0, 82, 118, 118}},   /* HAT OP */
            {DX_MODE(DXW_SINE, DXF_HP, 1), {32, 48, 66, 18, 66}, {38, 68, 76, 28, 86}},   /* TOM */
            {DX_MODE(DXW_BELL, DXF_HP, 1), {63, 10, 14, 20, 56}, {70, 24, 20, 34, 70}},   /* RIM */
            {DX_MODE(DXW_METAL, DXF_BP, 1), {64, 0, 44, 34, 6}, {68, 0, 54, 46, 28}},   /* BELL */
        },
        {0, 6, 0, 0, 0, 0, 0, 0},
    },
    {   /* HIP HOP: boom bap: a round kick with a knock, a fat cracking snare, a tight clap, dusty hats (band-passed), a
         * low tom, a woody rim, a shaker in the BELL's place */
        {
            {DX_MODE(DXW_SINE, DXF_LP, 1), {6, 84, 74, 8, 26}, {10, 112, 90, 16, 62}},   /* KICK */
            {DX_MODE(DXW_FM, DXF_BP, 1), {38, 34, 62, 106, 82}, {44, 22, 76, 124, 62}},   /* SNARE */
            {DX_MODE(DXW_SINE, DXF_BP, 0), {58, 0, 42, 127, 76}, {52, 0, 54, 127, 66}},   /* CLAP */
            {DX_MODE(DXW_METAL, DXF_BP, 0), {98, 0, 12, 96, 116}, {106, 0, 6, 112, 122}},   /* HAT CL */
            {DX_MODE(DXW_METAL, DXF_BP, 0), {98, 0, 54, 96, 110}, {106, 0, 66, 112, 116}},   /* HAT OP */
            {DX_MODE(DXW_SINE, DXF_HP, 1), {26, 36, 70, 14, 40}, {32, 56, 80, 20, 60}},   /* TOM */
            {DX_MODE(DXW_BELL, DXF_HP, 1), {61, 10, 10, 16, 34}, {68, 24, 16, 30, 48}},   /* RIM */
            {DX_MODE(DXW_METAL, DXF_BP, 0), {84, 0, 20, 110, 70}, {92, 0, 28, 124, 80}},   /* SHAKER */
        },
        {0, 8, 0, 0, 0, 0, 4, 0},
    },
    {   /* LATIN PERC: a bombo, a timbal, the llamador, the GUIRO on the closed hat (the filter GR: an accent its long
         * stroke, a soft hit its "chk"), maracas, a conga, the clave, the cowbell */
        {
            {DX_MODE(DXW_SINE, DXF_LP, 1), {9, 70, 66, 4, 20}, {14, 96, 80, 10, 50}},   /* BOMBO */
            {DX_MODE(DXW_BELL, DXF_BP, 0), {50, 20, 40, 70, 96}, {56, 12, 50, 90, 80}},   /* TIMBAL */
            {DX_MODE(DXW_SINE, DXF_HP, 1), {54, 30, 40, 20, 60}, {60, 40, 48, 30, 70}},   /* LLAMADOR */
            {DX_MODE(DXW_METAL, DXF_GR, 0), {100, 110, 20, 127, 106}, {108, 122, 26, 127, 112}},   /* GUIRO */
            {DX_MODE(DXW_METAL, DXF_BP, 0), {84, 0, 22, 118, 110}, {92, 0, 30, 124, 116}},   /* MARACAS */
            {DX_MODE(DXW_SINE, DXF_HP, 1), {44, 30, 50, 12, 40}, {50, 44, 58, 18, 56}},   /* CONGA */
            {DX_MODE(DXW_BELL, DXF_HP, 1), {76, 6, 10, 8, 30}, {82, 14, 14, 16, 44}},   /* CLAVE */
            {DX_MODE(DXW_METAL, DXF_BP, 1), {64, 0, 44, 34, 6}, {68, 0, 54, 46, 28}},   /* COWBELL */
        },
        {0, 4, 0, 0, 0, 0, 0, 0},
    },
    {   /* STD (Felucca's DRUMS kit STD, translated) */
        {
            {DX_MODE(DXW_SINE, DXF_LP, 1) | 1 << 5, {10, 69, 58, 0, 86}, {10, 69, 58, 0, 86}},   /* KICK: CLASSIC */
            {DX_MODE(DXW_BELL, DXF_BP, 1), {39, 2, 63, 36, 99}, {39, 2, 63, 36, 99}},   /* SNARE: SHELL */
            {DX_MODE(DXW_SINE, DXF_BP, 0) | 1 << 5 | DX_DRIVE, {64, 31, 62, 39, 73}, {64, 31, 62, 39, 73}},   /* CLAP: TEETH */
            {DX_MODE(DXW_SINE, DXF_BP, 0) | 3 << 5, {42, 112, 34, 0, 115}, {42, 112, 34, 0, 115}},   /* HAT CL: METAL */
            {DX_MODE(DXW_SINE, DXF_BP, 0) | 2 << 5, {42, 18, 71, 0, 110}, {42, 18, 71, 0, 110}},   /* HAT OP: METAL */
            {DX_MODE(DXW_SINE, DXF_LP, 1) | 1 << 5, {28, 5, 85, 15, 69}, {28, 5, 85, 15, 69}},   /* TOM: CLASSIC */
            {DX_MODE(DXW_METAL, DXF_BP, 0) | 2 << 5, {63, 0, 64, 0, 61}, {63, 0, 64, 0, 61}},   /* RIM: CLICK */
            {DX_MODE(DXW_METAL, DXF_BP, 0) | DX_DRIVE, {65, 0, 73, 0, 68}, {65, 0, 73, 0, 68}},   /* BELL: METAL */
        },
        {0, 0, 0, 0, 0, 0, 0, 0}, {0, 1, 3, 2, 2, 0, 4, 2},
    },
    {   /* 80 (Felucca's DRUMS kit 80, translated) */
        {
            {DX_MODE(DXW_SINE, DXF_LP, 1), {10, 4, 67, 0, 0}, {10, 4, 67, 0, 0}},   /* KICK: CLASSIC */
            {DX_MODE(DXW_METAL, DXF_BP, 0), {38, 0, 45, 77, 96}, {38, 0, 45, 77, 96}},   /* SNARE: SHELL */
            {DX_MODE(DXW_SINE, DXF_BP, 0), {64, 31, 69, 0, 74}, {64, 31, 69, 0, 74}},   /* CLAP: TEETH */
            {DX_MODE(DXW_SINE, DXF_BP, 0) | 3 << 5, {42, 112, 34, 0, 116}, {42, 112, 34, 0, 116}},   /* HAT CL: METAL */
            {DX_MODE(DXW_SINE, DXF_BP, 0) | 2 << 5, {42, 18, 71, 0, 111}, {42, 18, 71, 0, 111}},   /* HAT OP: METAL */
            {DX_MODE(DXW_SINE, DXF_LP, 1) | 1 << 5, {33, 1, 60, 5, 69}, {33, 1, 60, 5, 69}},   /* TOM: CLASSIC */
            {DX_MODE(DXW_FM, DXF_LP, 0) | 3 << 5, {61, 0, 48, 0, 111}, {61, 0, 48, 0, 111}},   /* RIM: CLICK */
            {DX_MODE(DXW_METAL, DXF_BP, 0) | DX_DRIVE, {65, 0, 66, 0, 68}, {65, 0, 66, 0, 68}},   /* BELL: METAL */
        },
        {0, 0, 0, 0, 0, 0, 0, 0}, {0, 1, 3, 2, 2, 0, 4, 2},
    },
    {   /* 10 (Felucca's DRUMS kit 10, translated) */
        {
            {DX_MODE(DXW_SINE, DXF_LP, 1), {15, 17, 82, 0, 0}, {15, 17, 82, 0, 0}},   /* KICK: CLASSIC */
            {DX_MODE(DXW_SINE, DXF_BP, 0), {40, 19, 73, 96, 107}, {40, 19, 73, 96, 107}},   /* SNARE: SHELL */
            {DX_MODE(DXW_SINE, DXF_BP, 1) | 3 << 5 | DX_DRIVE, {64, 119, 111, 127, 96}, {64, 119, 111, 127, 96}},   /* CLAP: TEETH */
            {DX_MODE(DXW_METAL, DXF_BP, 1), {53, 0, 67, 0, 115}, {53, 0, 67, 0, 115}},   /* HAT CL: METAL */
            {DX_MODE(DXW_METAL, DXF_BP, 1), {53, 0, 108, 0, 115}, {53, 0, 108, 0, 115}},   /* HAT OP: METAL */
            {DX_MODE(DXW_SINE, DXF_LP, 1) | 1 << 5, {30, 13, 75, 6, 79}, {30, 13, 75, 6, 79}},   /* TOM: CLASSIC */
            {DX_MODE(DXW_BELL, DXF_LP, 0) | 1 << 5, {72, 0, 70, 0, 26}, {72, 0, 70, 0, 26}},   /* RIM: CLICK */
            {DX_MODE(DXW_METAL, DXF_BP, 0), {53, 0, 122, 0, 104}, {53, 0, 122, 0, 104}},   /* BELL: METAL */
        },
        {0, 0, 0, 0, 0, 0, 0, 0}, {0, 1, 3, 2, 2, 0, 4, 2},
    },
    {   /* 66 (Felucca's DRUMS kit 66, translated) */
        {
            {DX_MODE(DXW_SINE, DXF_LP, 1), {16, 0, 56, 0, 0}, {16, 0, 56, 0, 0}},   /* KICK: CLASSIC */
            {DX_MODE(DXW_FM, DXF_BP, 0), {50, 0, 37, 122, 113}, {50, 0, 37, 122, 113}},   /* SNARE: SHELL */
            {DX_MODE(DXW_SINE, DXF_BP, 1) | 1 << 5, {64, 27, 61, 0, 111}, {64, 27, 61, 0, 111}},   /* CLAP: TEETH */
            {DX_MODE(DXW_BELL, DXF_BP, 1) | 1 << 5 | DX_DRIVE, {127, 0, 61, 127, 110}, {127, 0, 61, 127, 110}},   /* HAT CL: METAL */
            {DX_MODE(DXW_BELL, DXF_BP, 1) | 1 << 5 | DX_DRIVE, {127, 0, 66, 127, 110}, {127, 0, 66, 127, 110}},   /* HAT OP: METAL */
            {DX_MODE(DXW_SINE, DXF_HP, 1), {36, 0, 47, 6, 85}, {36, 0, 47, 6, 85}},   /* TOM: CLASSIC */
            {DX_MODE(DXW_SINE, DXF_LP, 0), {88, 0, 51, 0, 2}, {88, 0, 51, 0, 2}},   /* RIM: CLICK */
            {DX_MODE(DXW_SINE, DXF_BP, 0), {75, 0, 103, 0, 16}, {75, 0, 103, 0, 16}},   /* BELL: CLICK */
        },
        {0, 0, 0, 0, 0, 0, 0, 0}, {0, 1, 3, 2, 2, 0, 4, 4},
    },
    {   /* 55 (Felucca's DRUMS kit 55, translated) */
        {
            {DX_MODE(DXW_SINE, DXF_LP, 1) | 1 << 5, {12, 27, 52, 0, 69}, {12, 27, 52, 0, 69}},   /* KICK: CLASSIC */
            {DX_MODE(DXW_SINE, DXF_BP, 0), {47, 4, 41, 116, 106}, {47, 4, 41, 116, 106}},   /* SNARE: SHELL */
            {DX_MODE(DXW_SINE, DXF_BP, 1) | 1 << 5, {64, 22, 57, 0, 112}, {64, 22, 57, 0, 112}},   /* CLAP: TEETH */
            {DX_MODE(DXW_BELL, DXF_BP, 0) | 2 << 5, {127, 62, 43, 78, 110}, {127, 62, 43, 78, 110}},   /* HAT CL: METAL */
            {DX_MODE(DXW_BELL, DXF_BP, 0) | 2 << 5, {127, 62, 78, 78, 113}, {127, 62, 78, 78, 113}},   /* HAT OP: METAL */
            {DX_MODE(DXW_SINE, DXF_LP, 1) | 1 << 5, {37, 4, 60, 35, 85}, {37, 4, 60, 35, 85}},   /* TOM: CLASSIC */
            {DX_MODE(DXW_METAL, DXF_LP, 0) | 2 << 5, {63, 0, 43, 0, 64}, {63, 0, 43, 0, 64}},   /* RIM: CLICK */
            {DX_MODE(DXW_SINE, DXF_BP, 0) | DX_DRIVE, {71, 0, 49, 37, 96}, {71, 0, 49, 37, 96}},   /* BELL: METAL */
        },
        {0, 0, 0, 0, 0, 0, 0, 0}, {0, 1, 3, 2, 2, 0, 4, 2},
    },
    {   /* 77 (Felucca's DRUMS kit 77, translated) */
        {
            {DX_MODE(DXW_SINE, DXF_LP, 1) | 1 << 5, {16, 4, 76, 0, 0}, {16, 4, 76, 0, 0}},   /* KICK: CLASSIC */
            {DX_MODE(DXW_SINE, DXF_BP, 0), {45, 0, 56, 51, 89}, {45, 0, 56, 51, 89}},   /* SNARE: SHELL */
            {DX_MODE(DXW_BELL, DXF_BP, 0) | 1 << 5 | DX_DRIVE, {64, 18, 67, 24, 75}, {64, 18, 67, 24, 75}},   /* CLAP: TEETH */
            {DX_MODE(DXW_FM, DXF_BP, 0) | DX_DRIVE, {109, 0, 67, 0, 111}, {109, 0, 67, 0, 111}},   /* HAT CL: METAL */
            {DX_MODE(DXW_FM, DXF_BP, 0) | DX_DRIVE, {109, 0, 103, 0, 111}, {109, 0, 103, 0, 111}},   /* HAT OP: METAL */
            {DX_MODE(DXW_SINE, DXF_LP, 1) | 1 << 5, {34, 3, 75, 10, 85}, {34, 3, 75, 10, 85}},   /* TOM: CLASSIC */
            {DX_MODE(DXW_FM, DXF_BP, 0) | 1 << 5, {99, 0, 125, 0, 23}, {99, 0, 125, 0, 23}},   /* RIM: CLICK */
            {DX_MODE(DXW_FM, DXF_BP, 0) | DX_DRIVE, {109, 0, 97, 27, 117}, {109, 0, 97, 27, 117}},   /* BELL: METAL */
        },
        {0, 0, 0, 0, 0, 0, 0, 0}, {0, 1, 3, 2, 2, 0, 4, 2},
    },
};
#define DX_NKIT ((uint32_t)(sizeof DX_KITS / sizeof DX_KITS[0]))
#define DXK_LATIN 10u                                    /* LATIN PERC */
#define DXK_DRUMS 11u                                    /* STD, the first of Felucca's: then 80 10 66 55 77 */
/* the factory kit: what a project without one gets, INIT SOUND, the power-on (dx_kit / dx_lay, set here) */
#define DX_KIT_DEF (DX_KITS[0].lane)
#define DX_LAY_DEF (DX_KITS[0].lay)
#define DX_MDL_DEF (DX_KITS[0].mdl)
static dx_lane_t dx_kit[8] = DX_KIT0_LANES;
static uint8_t dx_lay[8] = DX_KIT0_LAY;

/* a factory kit into the music's: its sounds, their LAYERs and MODELs */
static void dx_kit_load(uint32_t k)
{
    const dx_kit_t *K = &DX_KITS[k % DX_NKIT];
    memcpy(dx_kit, K->lane, sizeof dx_kit);
    memcpy(dx_lay, K->lay, sizeof dx_lay);
    memcpy(dx_mdl, K->mdl, sizeof dx_mdl);
}

/* The kit as a project stores it (project.c, 88 bytes, the place and size of FUN11's): each sound's 11 bytes, its LAYER
 * (0..15) in bit 7 of A's PITCH PMOD DECAY NOISE (its bits 0..3) and its MODEL (0..7) in bit 7 of B's PITCH PMOD DECAY
 * (the values are 0..127, the bit was always 0: a kit saved before them loads with none, CLASSIC). A's COLOR, B's
 * NOISE and COLOR keep bit 7 clear */
static void dx_kit_pack(dx_lane_t *out, const dx_lane_t *kit, const uint8_t *lay, const uint8_t *mdl)
{
    uint32_t l, i;
    for (l = 0; l < 8u; l++) {
        out[l] = kit[l];
        for (i = 0; i < 4u; i++)
            out[l].a[i] = (uint8_t)((kit[l].a[i] & 127u) | ((lay[l] >> i) & 1u) << 7);
        for (i = 0; i < 3u; i++)
            out[l].b[i] = (uint8_t)((kit[l].b[i] & 127u) | ((mdl[l] >> i) & 1u) << 7);
    }
}
static void dx_kit_unpack(dx_lane_t *kit, uint8_t *lay, uint8_t *mdl, const dx_lane_t *in)
{
    uint32_t l, i;
    for (l = 0; l < 8u; l++) {
        kit[l] = in[l];
        lay[l] = mdl[l] = 0;
        for (i = 0; i < 4u; i++) {
            lay[l] = (uint8_t)(lay[l] | (in[l].a[i] >> 7) << i);
            kit[l].a[i] &= 127u;
        }
        for (i = 0; i < 3u; i++) {
            mdl[l] = (uint8_t)(mdl[l] | (in[l].b[i] >> 7) << i);
            kit[l].b[i] &= 127u;
        }
    }
}
/* a stored (packed) kit as the voice takes it: every mode (wave, filter LP BP HP GR, SNAP, pitch modulation, DRIVE),
 * values 0..127 where no LAYER or MODEL bit sits, a MODEL it has */
static int dx_kit_ok(const dx_lane_t *k)
{
    uint32_t l;
    for (l = 0; l < 8u; l++)
        if (k[l].a[DXP_COLOR] > 127u || k[l].b[DXP_NOISE] > 127u || k[l].b[DXP_COLOR] > 127u ||
            ((k[l].b[0] >> 7) | (k[l].b[1] >> 7) << 1 | (k[l].b[2] >> 7) << 2) >= DXV_N)
            return 0;
    return 1;
}
