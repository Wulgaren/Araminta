/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Modulation matrix: per track four slots, each SRC -> DST by AMT (-64..63), added to the fixed
 * routings (ENV DEST, LFO DEST). Runs in the audio ISR (fx.c mix_part, voice.c track_render).
 *
 * Sources, Q15:
 *   LFO   the track LFO with its FADE (as LFO DEST), bipolar; with LFO 2 POL UNI 0..1 (1.2)
 *   ENV   the amp envelope of the voice (after the engine's own amp curve, as ENV DEST), 0..1
 *   VEL   the note-on velocity (before UNISON's level scaling), 0..1
 *   KEY   the note relative to C4, bipolar: +-64 semitones = +-1 (an octave = 0.19)
 *   RAND  a new random value per note-on (its own generator: the shared rng is not touched), bipolar
 *   MODW  MIDI CC1, AT channel aftertouch, EXPR CC11 of the track's channel (seq.c), 0..1; until a
 *         controller arrives (and after CC121 RESET ALL CONTROLLERS) MODW and AT are 0, EXPR 1
 *   S&H   (#132) a random value held for one cycle of the track LFO: the value LFO WAVE S&H steps through
 *         (voice.c lfo_rand, drawn as each cycle starts, whatever the WAVE), without the LFO's FADE, POL or
 *         DEPTH; bipolar, 0 until the LFO's first cycle ends. It follows RATE and SYNC (a step per note value)
 *   SLEW  (#132) S&H slewed: it glides linearly from the cycle before's value to this cycle's over the cycle
 *         (a smooth random line, one cycle behind S&H, continuous where S&H steps); bipolar
 *   Both are per track (one value for every voice) and cost no state but the one value kept per cycle.
 * Destinations:
 *   per voice (vmod_t, like the fixed routings):
 *     PITCH  AMT 63 at a full source = +-11.8 semitones (as LFO DEST PIT)
 *     CUT    m->cutoff, SHP m->shape: AMT 63 = +-63 steps (as ENV DEST); the sums are clamped to what
 *            the fixed routings can reach, so the engines see no new range
 *     AMP    a gain <= 1: AMT > 0 the source opens it (AMT 64: silent at 0, full at 1), AMT < 0 closes it
 *            (bipolar sources as 0..1, as LFO DEST AMP); several AMP slots multiply
 *   per track, once per block: PAN, DIST CHO DLY REV (the sends), RATE (LFO rate), VIB (LFO DEST PIT, the
 *     vibrato depth), E1..E8 (the engine's parameters P_E0..P_E7, named by the engine): AMT 64 at a full
 *     source = the parameter's whole range; the sum is clamped to its range
 *   DEPTH (#132, per track): the depth of the track LFO, a gain <= 1 on it as AMP is on the voice (AMT > 0 the source
 *     opens it, AMT 64: none at 0, full at 1; AMT < 0 closes it; several DEPTH slots multiply): it scales the LFO
 *     wherever it goes, the four LFO DEST routings and the matrix's LFO source (not S&H / SLEW). With RATE (the LFO's
 *     rate) the matrix modulates the modulator: MODW -> DEPTH is a mod-wheel LFO, ENV -> DEPTH an LFO that
 *     follows the note's envelope. A DEPTH slot whose source is the LFO takes it unscaled
 *   SLPAT / SLRATE / SLDEP (#222, per track): the SLICER's pattern, step rate and depth (P_SLPAT P_SLRATE P_SLDEPTH),
 *     as the other per-block destinations (AMT 64 = the whole range)
 *   insert continuous (per track): TONE + DMIX (DIST), CRUSH BITS/RATE/LPF/MIX, FILTER CUT/RES/MIX,
 *     PHASR RATE/DEP/FBK/MIX, FLANG RATE/DEP/MIX, CHOR RATE/DEP (CHOR MIX is CHO); no MODE/TYPE enums.
 *     DIST drive and DLY/REV amounts stay DIST/DLY/REV
 * A per-voice source on a per-block destination takes the latest note-on's value (VEL, KEY, RAND), ENV the
 * envelope of the latest note-on's voice (one block late, 0 when it has ended).
 *
 * Per-block destinations are modulated in place: mod_begin writes the modulated values into t->p for the
 * part's block (its render and its mix in fx.c mix_part) and mod_end puts the stored values back. The shared
 * SEND knobs (fx_bus_trk / song.g) go to a per-part copy instead (core.h bus_knobs): the part's DSP reads it,
 * so two cores rendering parts at once (fx.c mix_block) never touch one knob and each part sees its own
 * modulation, as when the renders ran one after another. Both run
 * in the audio ISR, so nothing outside it ever sees a modulated value (the UI, the editor, projects and
 * presets read and write t->p from the main loop, which the ISR preempts; TIMER5 only fills the MIDI rings),
 * and the engines read t->p as before. An engine reads its parameters in note_on (events_block) unmodulated.
 * Nothing active (every slot with SRC, DST or AMT at 0): no work, and the sound is bit-identical. */
enum { MS_OFF, MS_LFO, MS_ENV, MS_VEL, MS_KEY, MS_RAND, MS_MODW, MS_AT, MS_EXPR, MS_SH, MS_SLEW, MS_N };
#define CLK_START 0xFFFFFFFFu
static uint32_t clk_pos = CLK_START, clk_step;  /* (seq.c click_tick) samples into the transport's 1/16; after
                                                 * events_block: the block's start. Declared here so fx.c / pfx.c
                                                 * (included before seq.c) can read the grid. */
enum { MD_OFF, MD_PITCH, MD_CUT, MD_SHP, MD_AMP, MD_PAN, MD_DIST, MD_CHO, MD_DLY, MD_REV, MD_RATE, MD_VIB, MD_E1,
       MD_DEPTH = MD_E1 + 8, MD_SLPAT, MD_SLRATE, MD_SLDEPTH, MD_CHRD, MD_VOIC,
       MD_TONE, MD_DMIX, MD_FCUT, MD_FRES, MD_FMIX, MD_BITS, MD_CRAT, MD_CLPF, MD_CMIX,
       MD_PRAT, MD_PDEP, MD_PFBK, MD_PMIX, MD_FRAT, MD_FDEP, MD_LMIX, MD_HRAT, MD_HDEP,
       MD_DTIME, MD_DFB, MD_DCOLOR, MD_RSIZE, MD_RDAMP, MD_TSPEED, MD_TWOW, MD_N }; /* (append-only) */
_Static_assert(NELEM(N_MSRC) == MS_N && NELEM(N_MDST) == MD_N, "N_MSRC / N_MDST == MS_* / MD_*");
_Static_assert(P_M2SRC == P_M1SRC + 3 && P_M4AMT == P_M1SRC + 11 && P_M4AMT + 1 == P_FM1_ATK, "matrix slots: 3 ids each");
#define NMSLOT 4u
#define MS_VOICE(s) ((s) >= MS_ENV && (s) <= MS_RAND)             /* a value per voice */
#define MS_BIPOLAR(s) ((s) == MS_LFO || (s) == MS_KEY || (s) == MS_RAND || (s) >= MS_SH)
#define MD_ENGINE(d) ((d) >= MD_E1 && (d) < MD_E1 + 8)           /* E1..E8: P_E0..P_E7 */
#define MD_SLICER(d) ((d) >= MD_SLPAT && (d) <= MD_SLDEPTH)
#define MD_CHORD(d) ((d) >= MD_CHRD && (d) <= MD_VOIC)
#define MD_INS(d) ((d) >= MD_TONE && (d) <= MD_HDEP)             /* insert continuous + MIX (CHOR MIX = CHO) */
#define MD_FX(d) ((d) >= MD_DTIME && (d) <= MD_RDAMP)
#define MD_TAPE(d) ((d) == MD_TSPEED || (d) == MD_TWOW)
static int16_t *fx_target_ptr(const track_t *t, uint32_t id);
static int16_t *mod_target_ptr(const track_t *t, uint32_t id);   /* a part's copy of the shared knobs (fx.c) */
static void bus_knobs_fill(const track_t *t);                    /* .. filled from the real ones (fx.c) */
static const param_desc_t *fx_target_desc(const track_t *t, uint32_t id);
static int fx_target_send(const track_t *t, uint32_t id);
static int fx_target_available(const track_t *t, uint32_t id);
static int16_t fx_time_value(int16_t raw);
static void motion_fx_route(track_t *t, uint32_t st, int capture);

/* a smooth value noise, -32768..32767: a new random point every 2^lg steps of clk, eased between (smoothstep);
 * seed picks the stream. No state. DRIFT's per voice (voice.c) */
static int32_t drift_noise(uint32_t seed, uint32_t clk, uint32_t lg)
{
    uint32_t k = clk >> lg, f = ((clk & ((1u << lg) - 1u)) << 8) >> lg, a = (seed + k) * 2654435761u,
             b = (seed + k + 1u) * 2654435761u;
    int32_t ya, yb;
    a ^= a >> 15; a *= 2246822519u; a ^= a >> 13;
    b ^= b >> 15; b *= 2246822519u; b ^= b >> 13;
    ya = (int32_t)(a >> 16) - 32768;
    yb = (int32_t)(b >> 16) - 32768;
    f = f * f * (768u - 2u * f) >> 16;                  /* smoothstep, 0..256 */
    return ya + (((yb - ya) * (int32_t)f) >> 8);
}

/* the track parameter of a per-block destination */
static uint32_t mod_param(uint32_t d)
{
    static const uint8_t ID[MD_E1] = {[MD_PAN] = P_PAN, [MD_DIST] = P_DIST, [MD_CHO] = P_IHMIX, [MD_DLY] = P_DLY,
                                      [MD_REV] = P_REV, [MD_RATE] = P_LRATE, [MD_VIB] = P_LD_PIT};
    static const uint8_t SL[3] = {P_SLPAT, P_SLRATE, P_SLDEPTH};
    static const uint8_t CH[2] = {P_CHRD, P_VOIC};
    static const uint8_t INS[] = {
        P_ED_FX, P_IDMIX, P_IFCUT, P_IFRES, P_IFMIX, P_ICBITS, P_ICRATE, P_ICLPF, P_ICMIX,
        P_IPRATE, P_IPDEP, P_IPFBK, P_IPMIX, P_ILRATE, P_ILDEP, P_ILMIX, P_IHRATE, P_IHDEP};
    if (MD_ENGINE(d))
        return P_E0 + d - MD_E1;
    if (MD_SLICER(d))
        return SL[d - MD_SLPAT];
    if (MD_CHORD(d))
        return CH[d - MD_CHRD];
    if (MD_INS(d))
        return INS[d - MD_TONE];
    if (MD_FX(d))
        return MFX_TIME + d - MD_DTIME;
    if (MD_TAPE(d))
        return d == MD_TSPEED ? MFX_TSPEED : MFX_TWOW;
    return ID[d];
}

typedef struct {                 /* a part's, while mix_part renders it (one per part: two cores render at once,
                                  * fx.c mix_block) */
    uint8_t on;                  /* a slot is active: track_render calls mod_voice */
    uint8_t amp;                 /* an AMP slot: the voice gain applies */
    uint8_t nv, nk;              /* voice terms, per-block parameters held */
    int32_t pit, cut, shp, gain; /* per voice, from the per-track sources (gain Q15) */
    int32_t ldep;                /* DEPTH: the track LFO's gain (Q15; 32767 = none, track_render leaves it alone) */
    uint8_t vsrc[NMSLOT], vdst[NMSLOT];
    int8_t vamt[NMSLOT];
    uint8_t kid[NMSLOT];         /* per-block: the parameter, its stored value */
    uint8_t kshared[NMSLOT];     /* bit 0: shared path; bit 1: stage its value for the bus DSP */
    int16_t keep[NMSLOT];
} modw_t;
static modw_t mod_w[C1_NP];
#define MOD(t) (mod_w[C1_PI(t)])
#define MOD_W_CLEAR() memset(&MOD(&trk[0]), 0, C1_NP * sizeof(modw_t))   /* (the host tests: every part's) */

/* SEND buses run after the parts. Hold their effective settings until that DSP has consumed them.
 * Each part stages its own holds (its render may run on the other core: fx.c mix_block); mod_bus_begin folds
 * them in part order into mod_bus, where a later part overwrites the value of the same setting, as when the
 * parts rendered one after another. */
static struct { int16_t *ptr, value, keep; } mod_bus[7] __attribute__((section(".pool")));
static struct { int16_t *ptr, value; } mod_bus_w[C1_NP][NMSLOT];
static uint8_t mod_bus_wn[C1_NP];
static uint32_t mod_bus_n;
static void mod_bus_begin(void)
{
    uint32_t k, j, i;
    mod_bus_n = 0;
    for (k = 0; k < C1_NP; k++)
        for (j = 0; j < mod_bus_wn[k]; j++) {
            for (i = 0; i < mod_bus_n && mod_bus[i].ptr != mod_bus_w[k][j].ptr; i++)
                ;
            if (i == mod_bus_n) {
                if (mod_bus_n >= NELEM(mod_bus))
                    continue;                       /* (the full list drops it, as mod_end did) */
                mod_bus_n++;
            }
            mod_bus[i].ptr = mod_bus_w[k][j].ptr;
            mod_bus[i].value = mod_bus_w[k][j].value;
        }
    for (i = 0; i < mod_bus_n; i++) {
        mod_bus[i].keep = *mod_bus[i].ptr;
        *mod_bus[i].ptr = mod_bus[i].value;
    }
}
static void mod_bus_end(void)
{
    uint32_t i;
    for (i = 0; i < mod_bus_n; i++)
        *mod_bus[i].ptr = mod_bus[i].keep;
    mod_bus_n = 0;
}

static uint32_t mod_seed = 0x2545F491u;
static int16_t mod_rand(void)    /* RAND of a note-on: xorshift32, bipolar */
{
    mod_seed ^= mod_seed << 13;
    mod_seed ^= mod_seed >> 17;
    mod_seed ^= mod_seed << 5;
    return (int16_t)(mod_seed >> 16);
}

static int32_t mod_key(uint32_t note) { return clamp(((int32_t)note - 60) * 512, -32767, 32767); }

/* S&H: the value of the LFO's cycle (voice.c lfo_rand; 0 before its first one ends), bipolar */
static int32_t mod_sh(const track_t *t) { return t->lfo_rnd ? (int32_t)(t->lfo_rnd >> 16) - 32768 : 0; }

/* source s of the track: the per-track ones, the per-voice ones of the latest note-on */
static int32_t mod_tsrc(const track_t *t, uint32_t s, int32_t lfo)
{
    switch (s) {
    case MS_SH:
        return mod_sh(t);
    case MS_SLEW:                                       /* the cycle before's value .. this one's, by the phase */
        return t->lfo_prev + (((mod_sh(t) - t->lfo_prev) * (int32_t)(t->lfo_ph >> 17)) >> 15);
    case MS_LFO:
        return lfo;
    case MS_ENV:
        return t->m_env;
    case MS_VEL:
        return t->m_vel * 258;
    case MS_KEY:
        return mod_key(t->m_key);
    case MS_RAND:
        return t->m_rnd;
    case MS_MODW:
        return t->mw * 258;
    case MS_AT:
        return t->at * 258;
    default:
        return (127 - t->ex_off) * 258;
    }
}

/* AMP: the gain (Q15, 0..32767) of source x (bipolar: as 0..1) at amount a */
static int32_t mod_gain(uint32_t s, int32_t x, int32_t a)
{
    int32_t u = MS_BIPOLAR(s) ? (x + 32768) >> 1 : x;
    return clamp(32767 - (a > 0 ? ((32767 - u) * a) >> 6 : (u * -a) >> 6), 0, 32767);
}

/* a per-voice destination d by source value x, amount a */
static void mod_voice_add(uint32_t s, uint32_t d, int32_t x, int32_t a, int32_t *pit, int32_t *cut, int32_t *shp,
                          int32_t *gain)
{
    if (d == MD_PITCH)
        *pit += (x * a * 3) >> 15;
    else if (d == MD_CUT)
        *cut += (x * a) >> 7;
    else if (d == MD_SHP)
        *shp += (x * a) >> 7;
    else
        *gain = mulq15(*gain, mod_gain(s, x, a));
}

/* DEPTH: the track LFO's gain (Q15) from the DEPTH slots, 32767 = none (lfo: the LFO unscaled; a per-voice source
 * takes the latest note's value, as on the other per-block destinations). Its own function: mod_begin's loop stays
 * what it was */
static __attribute__((noinline)) int32_t mod_depth(const track_t *t, int32_t lfo)
{
    const int16_t *p = t->p;
    int32_t g = 32767;
    uint32_t k;
    for (k = 0; k < NMSLOT; k++) {
        uint32_t s = (uint32_t)p[P_M1SRC + 3u * k];
        if (p[P_M1DST + 3u * k] == MD_DEPTH && p[P_M1AMT + 3u * k] && s && s < MS_N)
            g = mulq15(g, mod_gain(s == MS_LFO && p[P_LPOL] ? MS_ENV : s, mod_tsrc(t, s, lfo), p[P_M1AMT + 3u * k]));
    }
    return g;
}

/* before a part's block (fx.c mix_part): the per-track offsets, the per-block destinations into t->p
 * (or the part's copy of fx_bus_trk / song.g via mod_target_ptr: two cores render parts at once). 0 = no
 * slot active (then nothing changed) */
static __attribute__((noinline)) int mod_begin(track_t *t)
{
    modw_t *mw = &MOD(t);
    const int16_t *p = t->p;
    int32_t lfo, off[NMSLOT];
    int16_t *target[NMSLOT];
    const param_desc_t *desc[NMSLOT];
    uint32_t k, j;
    mw->on = 0;
    bus_knobs_on[C1_PI(t)] = 0;                     /* without a slot the render reads the real knobs */
    mod_bus_wn[C1_PI(t)] = 0;
    for (k = 0; k < NMSLOT; k++)
        if (p[P_M1SRC + 3u * k] && p[P_M1DST + 3u * k] && p[P_M1AMT + 3u * k])
            break;
    if (k == NMSLOT)
        return 0;
    bus_knobs_fill(t);                              /* the shared knobs as they are, before this part's modulation */
    bus_knobs_on[C1_PI(t)] = 1;
    lfo = mulq15(t->lfo_val, t->lfo_fade);              /* (the LFO of the block before: track_render ticks it) */
    mw->on = 1;
    mw->amp = 0;
    mw->nv = mw->nk = 0;
    mw->pit = mw->cut = mw->shp = 0;
    mw->gain = 32767;
    mw->ldep = mod_depth(t, lfo);                       /* DEPTH first: the other slots see the LFO it leaves */
    if (mw->ldep < 32767)
        lfo = mulq15(lfo, mw->ldep);
    for (; k < NMSLOT; k++) {
        uint32_t s = (uint32_t)p[P_M1SRC + 3u * k], d = (uint32_t)p[P_M1DST + 3u * k];
        int32_t a = p[P_M1AMT + 3u * k], x;
        if (!s || !d || !a || s >= MS_N || d >= MD_N || d == MD_DEPTH)
            continue;
        if (d <= MD_AMP) {                              /* per voice */
            mw->amp |= d == MD_AMP;
            if (MS_VOICE(s)) {
                mw->vsrc[mw->nv] = (uint8_t)s;
                mw->vdst[mw->nv] = (uint8_t)d;
                mw->vamt[mw->nv++] = (int8_t)a;
            } else {                                    /* (a UNI LFO, 0..1, takes AMP as ENV does) */
                mod_voice_add(s == MS_LFO && p[P_LPOL] ? MS_ENV : s, d, mod_tsrc(t, s, lfo), a, &mw->pit, &mw->cut,
                              &mw->shp, &mw->gain);
            }
            continue;
        }
        {                                               /* per block: AMT 64 = the whole range */
            uint32_t id = mod_param(d);
            int shared = MD_FX(d) || MD_INS(d) || MD_SLICER(d) || MD_TAPE(d);
            const param_desc_t *pd;
            int16_t *dst;
            if (d == MD_DIST) {                         /* SEND: bus; CHAIN: track p[] */
                dst = mod_target_ptr(t, id);
                shared = dst != &t->p[id];
                pd = shared ? fx_target_desc(t, id) : &TP[id];
                if (shared && !fx_target_available(t, id))
                    continue;
            } else if (shared) {
                pd = fx_target_desc(t, id);
                dst = mod_target_ptr(t, id);
                if (!fx_target_available(t, id))
                    continue;
            } else {
                pd = id >= P_E0 ? &ENGINES[t->engine]->edit[id - P_E0] : &TP[id];
                dst = &t->p[id];
            }
            x = mod_tsrc(t, s, lfo);
            for (j = 0; j < mw->nk && mw->kid[j] != id; j++)
                ;
            if (j == mw->nk) {
                mw->kid[j] = (uint8_t)id;
                mw->kshared[j] = (uint8_t)(shared |
                    (((MFX_PARAM(id) && fx_target_send(t, id)) || MFX_TAPE(id)) << 1));
                mw->keep[j] = *dst;
                target[j] = dst;
                desc[j] = pd;
                off[j] = 0;
                mw->nk++;
            }
            off[j] += (((x * a) >> 6) * (pd->max - pd->min)) >> 15;
        }
    }
    for (j = 0; j < mw->nk; j++) {
        uint32_t id = mw->kid[j];
        const param_desc_t *pd = desc[j];
        int32_t base = id == MFX_TIME && mw->keep[j] < pd->min ? fx_time_value(mw->keep[j]) : mw->keep[j];
        *target[j] = (int16_t)clamp(base + off[j], pd->min, pd->max);
    }
    return 1;
}

/* after the part's block (when mw->on): the stored values back. A shared knob stays as this part's render saw
 * it (its copy, core.h bus_knobs): the state its run left is computed from those values (fx.c ins_bus_join),
 * and the next mod_begin refills the copy. The holds for the bus DSP go to this part's staging list; the fold
 * is mod_bus_begin's, after the join. */
static __attribute__((noinline)) void mod_end(track_t *t)
{
    modw_t *mw = &MOD(t);
    uint32_t j, pi = C1_PI(t);
    for (j = 0; j < mw->nk; j++) {
        uint32_t id = mw->kid[j];
        int16_t *ptr = mw->kshared[j] ? mod_target_ptr(t, id) : &t->p[id];
        if (mw->kshared[j] & 2u) {
            mod_bus_w[pi][mod_bus_wn[pi]].ptr = fx_target_ptr(t, id);   /* the real knob: the fold's key */
            mod_bus_w[pi][mod_bus_wn[pi]].value = *ptr;
            mod_bus_wn[pi]++;
        }
        if (ptr == (int16_t *)&t->p[id])
            *ptr = mw->keep[j];
    }
    mw->on = 0;
}

/* a voice's modulation (voice.c track_render, when mw->on), onto its vmod_t m as track_render made it:
 * pitch (the increment again, with the fine factor fine as there), cutoff and shape (clamped to what the
 * fixed routings can reach), the amplitude (and v->env_out, the next block's start). env: its envelope */
static __attribute__((noinline)) void mod_voice(track_t *t, voice_t *v, vmod_t *m, int32_t fine)
{
    const modw_t *mw = &MOD(t);
    int32_t pit = mw->pit, cut = mw->cut, shp = mw->shp, gain = mw->gain, env = m->envq15;
    uint32_t k;
    if (v == &t->v[t->m_vi])
        t->m_env = env;                                 /* ENV of the latest note, for per-block destinations */
    for (k = 0; k < mw->nv; k++) {
        uint32_t s = mw->vsrc[k];
        int32_t x = s == MS_ENV ? env : s == MS_VEL ? v->mvel * 258 : s == MS_KEY ? mod_key(v->note) : v->mrnd;
        mod_voice_add(s, mw->vdst[k], x, mw->vamt[k], &pit, &cut, &shp, &gain);
    }
    if (pit) {
        m->pitch16 = clamp(m->pitch16 + pit, 0, 2047);
        m->inc = pitch_inc(m->pitch16);
        if (fine)
            m->inc += (uint32_t)((int32_t)(m->inc >> 12) * fine);
    }
    m->cutoff = clamp(m->cutoff + cut, -150 * 256, 150 * 256);
    m->shape = clamp(m->shape + shp, -62 * 256, 190 * 256);
    if (mw->amp) {
        m->amp1 = mulq15(m->amp1, gain);
        v->env_out = m->amp1;
    }
}

/* a note-on of track t (voice.c trk_note_on): the per-voice sources of the latest note */
static void mod_note(track_t *t, uint32_t note, uint32_t vel)
{
    t->m_vel = (uint8_t)vel;
    t->m_key = (uint8_t)note;
    t->m_rnd = mod_rand();
}

/* a MIDI controller for track t (seq.c events_block): CC1 / CC11 / channel aftertouch; CC121 resets them */
static __attribute__((noinline)) void mod_midi(track_t *t, uint32_t st, uint32_t d1, uint32_t d2)
{
    if (st == 0xD0u) {
        t->at = (uint8_t)d1;
    } else if (st == 0xB0u) {
        if (d1 == 1u)
            t->mw = (uint8_t)d2;
        else if (d1 == 11u)
            t->ex_off = (uint8_t)(127u - d2);
        else if (d1 == 121u)
            t->mw = t->at = t->ex_off = 0;
    }
}

/* the name of destination value v for track t (UI, <= 6 characters): E1..E8 by the engine's labels (of the
 * engine the UI shows; WAVETABLE's cards say WAVE / POS, its list names WAVE A / POSITION A), "E5" for a
 * parameter the engine does not use */
static const char *mod_dst_name(const track_t *t, int32_t v)
{
    v = clamp(v, 0, MD_N - 1);
    if (MD_ENGINE(v)) {
        const char *l = param_list_name(t, P_E0 + (uint32_t)(v - MD_E1), 0);
        if (l && l[0] && l[0] != '-')
            return l;
    }
    return N_MDST[v];
}
