/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* ANALOG: two band-limited oscillators (saw / square / tri / sine / PWM),
 * noise, drive and a trapezoidal state-variable filter: low-pass, or (1.5, TYPE) band-pass /
 * high-pass / feedback COMB (CUT = delay, RES = feedback; dsp.c comb_process).
 * INT (P_E7, -24..24 semitones): OSC 2 this far from OSC 1, on every WAVE (was KTR; the filter's
 * keytracking is fixed at its old default, half).
 * WAVE (P_E0) is OSC A's; WAVE2 (P_WAVE2, SAME then SAW SQR TRI SIN PWM) OSC B's. SAME follows OSC A.
 * SYNC / SUB define OSC B's shape themselves and ignore WAVE2.
 * MORPH (P_MORPH, 0..127): each plain WAVE (SAW SQR TRI SIN) slides toward its own next one, PWM widens
 * toward ~54 %; the WAVE still picks the shape the knob starts from.
 * DRIFT (P_DRIFT, voice.c): each voice's slow pitch and filter wander.
 * WAVE SYNC and SUB (#104, appended: WAVE 0..4 play as they always did) are two more oscillator pairs (analog_osc_x):
 *   SYNC  OSC 2 a saw hard-synced to OSC 1 (a saw): it restarts with OSC 1's every cycle, band-limited at the restart
 *         (polyBLEP scaled by the step the restart makes). DTN (shown SYNC) sets OSC 2 above OSC 1, 3/16 semitone a
 *         step (127: +23.8 semitones), INT adds semitones, and the SHP routings sweep DTN (ENV DEST / LFO DEST SHP,
 *         the matrix's SHP: 3/16 semitone a step of shape, as PWM takes them for its width): the sync sweep. MIX
 *         between the two as for every pair (OSC 2 alone at 127)
 *   SUB   OSC 1 a saw, OSC 2 a square an octave below it (detuned by DTN in cents and moved by INT): MIX (shown SUB)
 *         is the sub oscillator's level
 *   SUPER one saw plus six detuned copies (three pairs), all in this voice and through its one filter: DTN (shown SPRD)
 *         is the spread, MIX (shown SWARM) the level of the copies against the centre (0: a plain saw) */
static const char *const N_ANALOG_WAVE_ALL[] = {"SAME", "SAW", "SQR", "TRI", "SIN", "PWM", "SYNC", "SUB", "SUPER"};
#define N_ANALOG_WAVE  (N_ANALOG_WAVE_ALL + 1)   /* WAVE: SAW .. SUPER */
#define N_ANALOG_WAVE2 N_ANALOG_WAVE_ALL        /* OSC B: SAME, SAW .. PWM */
enum { AW_SYNC = 5, AW_SUB = 6, AW_SUPER = 7 };
static const param_desc_t ANALOG_SYNC = {"SYNC", F_INT, 0, 127, 10, 0, 0};   /* DTN's range and default */
static const param_desc_t ANALOG_SUB = {"SUB", F_PCT, 0, 127, 64, 0, 0};      /* MIX's */
static const param_desc_t ANALOG_SPRD = {"SPRD", F_INT, 0, 127, 10, 0, 0};    /* DTN's range and default: SUPER's spread */
static const param_desc_t ANALOG_SWARM = {"SWARM", F_PCT, 0, 127, 64, 0, 0};  /* MIX's: SUPER's copies' level */

/* WAVE 0..3 (SAW SQR TRI SIN) at a phase: the plain shapes MORPH blends */
static __attribute__((noinline)) int32_t ana_shape(uint32_t w, uint32_t ph, uint32_t inc)
{
    switch (w & 3u) {
    case 1: return osc_pulse(ph, inc, 0x80000000u);
    case 2: return osc_tri(ph);
    case 3: return osc_sine(ph);
    default: return osc_saw(ph, inc);
    }
}

/* MORPH at pos (Q8: WAVE i is i << 8, the knob's end is the next WAVE, SIN wrapping to SAW). noinline: the
 * normal path keeps its switch, and this code stays out of it (the ISR's size and CPU budgets) */
static __attribute__((noinline)) int32_t ana_morph(int32_t pos, uint32_t ph, uint32_t inc)
{
    int32_t a = ana_shape((uint32_t)pos >> 8, ph, inc), f = pos & 255;
    if (!f)
        return a;
    return a + (((ana_shape(((uint32_t)pos >> 8) + 1u, ph, inc) - a) * f) >> 8);
}

/* the filter's TYPE (P_FTYPE, EDIT > FILTER, page 3; its KNOB 4 is DRIVE): 0 LP, 1 BP, 2 HP, 3 COMB. */
static inline int32_t analog_tap(const tsvf_t *c, int32_t k, int32_t in, int32_t *ic1, int32_t *ic2, uint32_t ft)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    if (!ft)
        return v2;                                    /* LP: tsvf_lp's (the same steps) */
    if (ft == 1u)
        return v1;                                    /* BP */
    return in - ((k * v1) >> 12) - v2;                /* HP (COMB uses analog_comb below) */
}
#define ANALOG_K(res) (8192 - (res) * 7600 / 127)     /* tsvf_coef's damping, Q12 */
#define AC_LEN 128u                                   /* COMB delay (~2.9 ms) */
_Static_assert((AC_LEN & (AC_LEN - 1u)) == 0u, "ANALOG COMB line is a power of two (dsp.c comb_process)");
static int16_t ana_comb[NPART][NVOICE][AC_LEN] __attribute__((section(".pool")));

/* SUPER: centre + three pairs. Three phases live in v->ph[], four more in the voice's free state slots
 * (s[3] s[4] s[5] s[7]); the filter and the noise state keep s[0..2] */
#define AS_N 7u
#define AS_SEEDED 0x53555052   /* s[6] ("SUPR"): this voice's SUPER phases were seeded; any other wave clears it
                                * (SYNC / SUB write s[3] as the restart value: s[3] alone cannot tell) */

/* COMB: feedback comb on the dry tap; ic1 holds the write index (kept across retriggers) */
static inline int32_t analog_comb(track_t *t, voice_t *v, int32_t in, int32_t len, int32_t res, int32_t *wp)
{
    uint32_t pi = (uint32_t)(t - trk) % NPART, vi = (uint32_t)(v - t->v) % NVOICE;
    uint16_t w = (uint16_t)(*wp);
    int32_t y = comb_process(ana_comb[pi][vi], AC_LEN, &w, in, len, res);
    *wp = (int32_t)w;
    return y;
}

static const param_desc_t *analog_desc(const track_t *t, uint32_t k)
{
    if (k == 1u && t->p[P_E0] == AW_SYNC)
        return &ANALOG_SYNC;
    if (k == 1u && t->p[P_E0] == AW_SUPER)
        return &ANALOG_SPRD;
    if (k == 2u && t->p[P_E0] == AW_SUB)
        return &ANALOG_SUB;
    if (k == 2u && t->p[P_E0] == AW_SUPER)
        return &ANALOG_SWARM;
    return 0;
}

static void analog_note_on(track_t *t, voice_t *v)
{
    if (t->p[P_E0] == AW_SUPER) {                     /* first use: the copies start apart (in phase they comb) */
        if (v->s[6] != AS_SEEDED) {                   /* another wave clears the mark: this is the first SUPER note */
            v->s[6] = AS_SEEDED;
            uint32_t x = 0x9E3779B9u * (v->age + 1u);
#define SUPER_NEXT() (x = x * 1664525u + 1013904223u)   /* as UNISON's start phases (voice.c) */
            v->ph[0] = SUPER_NEXT();
            v->ph[1] = SUPER_NEXT();
            v->ph[2] = SUPER_NEXT();
            v->s[3] = (int32_t)SUPER_NEXT();
            v->s[4] = (int32_t)SUPER_NEXT();
            v->s[5] = (int32_t)SUPER_NEXT();
            v->s[7] = (int32_t)SUPER_NEXT();
#undef SUPER_NEXT
        }
        /* marked: a retrigger of a sounding SUPER voice. s[3..7] and ph[] are left exactly as the last block
         * left them (voice.c's keep mask 0x03 restores only the filter): reseeding here would jump the four
         * copies. This runs before voice.c's restore, which does not overwrite these slots. */
    } else {
        v->ph[1] = v->ph[0] + 0x40000000u;
        v->s[3] = 0;                                  /* SYNC: no restart pending */
        v->s[6] = 0;                                  /* SUPER: seed again */
    }
    v->s[0] = v->s[1] = 0;                            /* filter */
    if (!v->s[2])
        v->s[2] = 0x1234567 + (int32_t)v->age;        /* noise state */
}

/* WAVE SYNC / SUB / SUPER: OSC 1 a saw, OSC 2 the synced saw / the sub square; SUPER the seven-saw stack;
 * then noise, drive and the filter as analog_render. Its own function: analog_render's loop stays what it was */
static __attribute__((noinline)) void analog_render_x(track_t *t, voice_t *v, int32_t *out, uint32_t n,
                                                      const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t sub = p[P_E0] == AW_SUB, super = p[P_E0] == AW_SUPER, i, k;
    int32_t det = p[P_E1], mix = p[P_E2], noise = p[P_E3];
    int32_t intv = p[P_E7] * 16;                       /* INT semitones (1/16 semitone units) */
    int32_t cut = (p[P_E4] << 8) + m->cutoff + shl(v->pitch16 - 960, 2);   /* keytracking: half, fixed */
    tsvf_t flt;
    int32_t drive = 32768 + p[P_E6] * 512;
    int32_t m2 = mix * 258, m1 = 32767 - m2;
    int32_t nz = noise * 200, drv = p[P_E6];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], inc1 = m->inc, inc2 = 0;
    uint32_t sph[AS_N], sinc[AS_N];
    int32_t cg[AS_N], norm = 32767;
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2];
    int32_t jl = v->s[3];                             /* SYNC: 1 + the step of a restart a sample ago (16 bits), 0 none */
    if (super) {                                      /* the centre and three pairs, one voice, one filter */
        sph[0] = v->ph[0]; sph[1] = v->ph[1]; sph[2] = v->ph[2];
        sph[3] = (uint32_t)v->s[3]; sph[4] = (uint32_t)v->s[4];
        sph[5] = (uint32_t)v->s[5]; sph[6] = (uint32_t)v->s[7];
        /* the pairs are not mirror images, and the outer ones sit lower: a little analog unevenness */
        static const int8_t CT[6] = {-6, 7, -17, 19, -43, 46};      /* cents at SPRD 127 */
        static const uint8_t LV[6] = {100, 96, 90, 86, 78, 74};     /* copy level, % of SWARM */
        int32_t d = (det * det / 127 + det) / 2;                    /* ease-in: gentle low, wide at the top */
        int32_t sg = mix * 258 / 6, sum = 32767;
        sinc[0] = inc1;
        for (k = 1; k < AS_N; k++) {
            int32_t ct = CT[k - 1u] * d / 127;
            sinc[k] = cents_inc(m->pitch16, ct, m->fine);
        }
        cg[0] = 32767;
        for (k = 1; k < AS_N; k++) {
            cg[k] = sg * LV[k - 1u] / 100;
            sum += cg[k];
        }
        norm = (32767 * 32767) / sum;                 /* keeps the peak where one saw's was */
    } else if (sub) {                                 /* an octave below OSC 1, INT and DTN on top */
        inc2 = ((det || intv) ? cents_inc(m->pitch16 + intv, det, 0) : inc1) >> 1;
    } else {                                          /* INT, DTN and the shape above OSC 1, 3/16 semitone a step */
        inc2 = pitch_inc(clamp(m->pitch16 + intv + det * 3 + (((m->shape - (64 << 8)) * 3) >> 8), 0, 2047));
        if (m->fine)
            inc2 += (uint32_t)((int32_t)(inc2 >> 12) * m->fine);
    }
    uint32_t ft = (uint32_t)p[P_FTYPE];
    int32_t fk = ANALOG_K(p[P_E5]);
    tsvf_coef(&flt, cut, p[P_E5]);
    uint32_t clen = ft == IFT_COMB ? comb_delay_len(clamp(cut >> 8, 0, 127), AC_LEN) : 0u;   /* block-constant */
    for (i = 0; i < n; i++) {
        int32_t s;
        if (super) {
            s = mulq15(osc_saw(sph[0], sinc[0]), cg[0]);
            for (k = 1; k < AS_N; k++)
                s += mulq15(osc_saw(sph[k], sinc[k]), cg[k]);
            s = (s * norm) >> 15;
            for (k = 0; k < AS_N; k++)
                sph[k] += sinc[k];
        } else {
            int32_t a = osc_saw(ph0, inc1), b;
            if (sub) {
                b = osc_pulse(ph1, inc2, 0x80000000u);
            } else {
                int32_t bl = blep(ph1, inc2);
                if (jl) {                             /* just restarted: the step was jl - 1, not a whole cycle */
                    bl = (bl * ((jl - 1) >> 1)) >> 15;
                    jl = 0;
                }
                b = (int32_t)(ph1 >> 16) - 32768 - bl;
                if (ph0 > 0xFFFFFFFFu - inc1)         /* OSC 1 restarts it before the next sample: round that step */
                    b -= (blep(ph0, inc1) * (int32_t)(ph1 >> 17)) >> 15;
            }
            ph0 += inc1;
            ph1 += inc2;
            if (!sub && ph0 < inc1) {                 /* OSC 1 wrapped: OSC 2 restarts at the same point */
                uint32_t f = ph0 / ((inc1 >> 16) | 1u);   /* the part of a sample since, 0..65535 */
                uint32_t at = ph1 - f * (inc2 >> 16);     /* OSC 2 at the wrap */
                jl = (int32_t)(at >> 16) + 1;
                ph1 = f * (inc2 >> 16);
            }
            s = mulq15(a, m1) + mulq15(b, m2);
        }
        if (nz)
            s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, nz);
        if (drv)
            s = softclip(((s >> 2) * (drive >> 2)) >> 11);
        if (ft == IFT_COMB)
            s = soft_knee(analog_comb(t, v, s >> 1, (int32_t)clen, p[P_E5], &ic1), 16000) * 2;
        else
            s = soft_knee(analog_tap(&flt, fk, s >> 1, &ic1, &ic2, ft), 16000) * 2;
        out[i] += voice_amp(s, m, i) * 2;
    }
    if (super) {
        v->ph[0] = sph[0]; v->ph[1] = sph[1]; v->ph[2] = sph[2];
        v->s[3] = (int32_t)sph[3]; v->s[4] = (int32_t)sph[4];
        v->s[5] = (int32_t)sph[5]; v->s[7] = (int32_t)sph[6];
    } else {
        v->ph[0] = ph0;
        v->ph[1] = ph1;
        v->s[3] = jl;
        v->s[6] = 0;                  /* SUPER's seeding mark: s[3] is not its phase now */
    }
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
}

/* OSC B's effective wave: SAME (0) follows OSC A; 1..5 pick SAW SQR TRI SIN PWM */
static inline __attribute__((always_inline)) uint32_t analog_wave_b(const track_t *t)
{
    uint32_t w = (uint32_t)t->p[P_WAVE2];
    return w ? w - 1u : (uint32_t)t->p[P_E0];
}

/* WAVE 0..4 through the filter TYPE ft: analog_render's loop; ft 0 (LP) is the path it always took,
 * BP / HP / COMB the one #104 added. WAVE2 (OSC B) picks its own shape when it is not SAME: that split, the
 * rare sound, takes ana_morph per oscillator (its MORPH 0 case is ana_shape); the common path (both waves
 * equal) keeps its switch and pays only the split test.
 * noinline, one copy: always_inline duplicated it, MORPH doubled that, and one loop keeps the filter tail
 * written once (the split used to be a second function that had to duplicate the tail to fit the XIP slot) */
static __attribute__((noinline)) void analog_osc(track_t *t, voice_t *v, int32_t *out, uint32_t n,
                                                 const vmod_t *m, uint32_t ft)
{
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0], i;
    /* OSC B's own WAVE, but only while ANALOG is the requested engine: on an engine switch's fade-out the
     * slot holds the new engine's parameter (as the split dispatch did) */
    uint32_t wave2 = t->eng_req % NENGINES == ENGI_ANALOG ? analog_wave_b(t) : wave;
    int32_t split = wave2 != wave;
    int32_t det = p[P_E1], mix = p[P_E2], noise = p[P_E3];
    int32_t cut = (p[P_E4] << 8) + m->cutoff + shl(v->pitch16 - 960, 2);   /* keytracking: half, fixed */
    tsvf_t flt;
    int32_t drive = 32768 + p[P_E6] * 512;                       /* 1x .. 3x */
    uint32_t inc1 = m->inc;
    uint32_t inc2 = det || p[P_E7] ? cents_inc(m->pitch16 + p[P_E7] * 16, det, 0) : inc1;   /* INT semitones, DTN cents */
    uint32_t pw = 0x80000000u + ((uint32_t)clamp(m->shape - (64 << 8) + (int32_t)t->mo_cur * 41, -16384, 65000) << 15);
    int32_t mpos = (int32_t)(wave << 8);                          /* MORPH position: the WAVE, on to the next */
    if (t->mo_cur)
        mpos += (int32_t)((uint32_t)t->mo_cur * 256u / 127u);
    int32_t mpos2 = mpos + (((int32_t)wave2 - (int32_t)wave) << 8);   /* its own WAVE, same MORPH offset */
    int32_t pwm = wave == 4u, pwm2 = wave2 == 4u;
    int32_t m2 = mix * 258, m1 = 32767 - m2;                    /* osc mix Q15 */
    int32_t nz = noise * 200, drv = p[P_E6];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1];                  /* state in locals: out[] may alias v->s[] */
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2];
    int32_t fk = ft ? ANALOG_K(p[P_E5]) : 0;
    tsvf_coef(&flt, cut, p[P_E5]);
    uint32_t clen = ft == IFT_COMB ? comb_delay_len(clamp(cut >> 8, 0, 127), AC_LEN) : 0u;   /* block-constant */
    for (i = 0; i < n; i++) {
        int32_t a, b, s;
        if (split) {                                 /* rare: each oscillator its own shape */
            a = pwm ? osc_pulse(ph0, inc1, pw) : ana_morph(mpos, ph0, inc1);
            b = pwm2 ? osc_pulse(ph1, inc2, pw) : ana_morph(mpos2, ph1, inc2);
        } else if (pwm) {
            a = osc_pulse(ph0, inc1, pw);
            b = osc_pulse(ph1, inc2, pw);
        } else if (!t->mo_cur) {                     /* no MORPH: the WAVE's own shape, as it always was */
            switch (wave) {
            case 1: a = osc_pulse(ph0, inc1, 0x80000000u); b = osc_pulse(ph1, inc2, 0x80000000u); break;
            case 2: a = osc_tri(ph0); b = osc_tri(ph1); break;
            case 3: a = osc_sine(ph0); b = osc_sine(ph1); break;
            default: a = osc_saw(ph0, inc1); b = osc_saw(ph1, inc2); break;
            }
        } else {
            a = ana_morph(mpos, ph0, inc1);
            b = ana_morph(mpos, ph1, inc2);
        }
        ph0 += inc1;
        ph1 += inc2;
        s = mulq15(a, m1) + mulq15(b, m2);
        if (nz)
            s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, nz);
        if (drv)
            s = softclip(((s >> 2) * (drive >> 2)) >> 11);   /* pre-shifts: drive is up to 3x, no overflow */
        {   /* filter, linear up to half scale, then a soft knee (only resonance peaks saturate); soft_knee()
             * written out for SVF: the host compiler makes this loop 1 % slower with the call */
            int32_t y;
            if (ft == IFT_COMB)
                y = analog_comb(t, v, s >> 1, (int32_t)clen, p[P_E5], &ic1);
            else if (ft)
                y = analog_tap(&flt, fk, s >> 1, &ic1, &ic2, ft);
            else
                y = tsvf_lp(&flt, s >> 1, &ic1, &ic2);
            int32_t a = y < 0 ? -y : y;
            if (a > 16000) {
                a = 16000 + (softclip((a - 16000) * 2) >> 1);
                y = y < 0 ? -a : a;
            }
            s = y * 2;                          /* soft knee bounds y; multiplication permits negatives */
        }
        out[i] += voice_amp(s, m, i) * 2;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
}

static void analog_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    if (t->p[P_E0] >= AW_SYNC)
        analog_render_x(t, v, out, n, m);
    else
        analog_osc(t, v, out, n, m, (uint32_t)t->p[P_FTYPE]);
}

static const preset_t ANALOG_PRESETS[] = {
    {"SAW LEAD", {0, 12, 64, 0, 90, 30, 10, 0}, {4, 70, 100, 50}, 20, 1, FX(0, 10, 45, 30), PAT(4)},
    {"SOFT PAD", {0, 20, 64, 4, 60, 10, 0, 0}, {80, 90, 110, 95}, 10, 0, FX(0, 60, 20, 70), PAT(5)},
    {"SQR BASS", {1, 0, 0, 0, 50, 70, 40, 0}, {0, 60, 40, 30}, 30, 1, FX(5, 0, 10, 10), PAT(2)},
    {"PWM STR", {4, 8, 40, 0, 75, 20, 0, 0}, {60, 80, 110, 85}, 8, 0, FX(0, 50, 20, 60), PAT(5)},
    {"ACID", {0, 0, 0, 0, 50, 100, 25, 0}, {0, 55, 20, 30}, 48, 1, FX(20, 0, 45, 15), PAT(1)},
    {"SINE KEY", {3, 6, 50, 0, 127, 0, 0, 0}, {2, 80, 30, 70}, 0, 0, FX(0, 30, 25, 40), PAT(6)},
    {"RAVE", {4, 30, 64, 0, 85, 20, 30, 0}, {20, 80, 110, 60}, 10, 1, FX(30, 40, 30, 30), PAT(13)},
    {"SUB BASS", {3, 0, 0, 0, 40, 0, 20, 0}, {0, 60, 100, 20}, 0, 1, FX(0, 0, 0, 10), PAT(8)},
    {"PLUCK", {0, 8, 50, 0, 30, 40, 0, 0}, {0, 88, 0, 60}, 55, 0, FX(0, 20, 50, 30), PAT(3)},
    {"BRASS", {0, 10, 64, 0, 45, 20, 10, 0}, {35, 70, 90, 45}, 40, 0, FX(0, 20, 20, 40), PAT(6)},
    {"WIND", {0, 0, 0, 90, 30, 90, 0, 0}, {60, 90, 60, 80}, 50, 0, FX(0, 30, 30, 70), PAT(5)},
    {"STRINGS", {0, 25, 64, 0, 70, 10, 0, 0}, {70, 90, 115, 90}, 5, 0, FX(0, 60, 20, 70), PAT(5)},
    {"SUPER SAW", {7, 96, 112, 0, 100, 20, 0, 0}, {6, 80, 110, 55}, 0, 0, FX(0, 20, 25, 35), PAT(6)},
};

static const engine_t ENG_ANALOG = {
    .name = "ANALOG",
    .page_title = {"OSC A", "OSC B"}, /* the editor names; the device uses the OSC A / OSC B / FILTER pages */
    .edit = {
        {"WAVE", F_ENUM, 0, 7, 0, N_ANALOG_WAVE, 0},
        {"DTN", F_INT, 0, 127, 10, 0, "ct"},
        {"MIX", F_PCT, 0, 127, 64, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"INT", F_SEMI, -24, 24, 0, 0, 0},   /* OSC 2 interval from OSC 1 (was KTR) */
    },
    .presets = ANALOG_PRESETS,
    .npresets = NELEM(ANALOG_PRESETS),
    .note_on = analog_note_on,
    .render = analog_render,
    .desc = analog_desc,
    .knob = {P_E4, P_E5, P_ATK, P_REL},
    .keep = 0x03,                /* the filter; SUPER's s[3..7] phases are left alone by note_on on a retrigger */
};
