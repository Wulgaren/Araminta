/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* WAVETABLE engine test (src/eng_wt.c) on the Mac, through hostsim.c as regress.c.
 *   build/host/wt_test [DEMODIR]          (run_tests.sh: build/wt_demo)
 * 1. the engine: index 14 (ENGI_WT), WAVETABLE, pages OSC A / OSC B, 8 parameters, 8 presets, the knob row
 *    (A-POS, FDBK, B>A, B-LVL).
 * 2. the tables (build/gen/felucca_wt.h): two factory tables, 16 frames each, a 512-sample base (1016 a frame),
 *    every stored sample in range, every frame's base level non-zero; the mip helper (wt_mip_level) for a
 *    9-bit base: 0 at no or a very low increment, non-decreasing, 6 at a high one, clamped.
 * 3. presets: each of the 8 at C2 and C4 sounds; no DC (the mix's |mean| after its master DC blockers settle,
 *    below 1 % of VOICE_FS), no clipping (a voice below 2 x VOICE_FS, the mix below full scale).
 * 4. a note from silence renders bit-identically twice (every preset: the phases reset at note-on).
 * 5. controls: A-POS 0 / 64 / 127, FDBK 0 / 32 / 64 / 127 (127 without clipping or DC), B>A 0 / 64, B-LVL 0 / 100
 *    (RMS) and RATIO 0.25 / 2.0 all change the sound; USR1..USR3 (no user slot format in pass 1) play the sine
 *    fallback, the same one, non-silent.
 * 6. cost: host instructions per sample and voice (8 notes held, the idle mix taken off) for every preset;
 *    fails above WT_COST_MAX.
 * 7. demos into DEMODIR: every preset playing its suggested pattern. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define WT_COST_MAX 220.0         /* host instructions per sample and voice (~3.7 % on the device at 1.7 % per 100) */
#define NNOTE ((FS / 2u / CTL) * CTL)       /* 0.5 s: the measured window */
#define NRENDER (2u * NNOTE)                /* 1 s: the settle, then the window (the master's DC blockers) */

static int bad;
static void check(const char *what, int ok)
{
    printf("wt: %-90s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static uint32_t eng_wt(void)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (ENGINES[e] == &ENG_WT)
            return e;
    return 0;
}

/* preset pi's E values (int8_t in preset_t) */
static const int16_t *pe(uint32_t pi)
{
    static int16_t e[8];
    uint32_t i;
    for (i = 0; i < 8u; i++)
        e[i] = ENG_WT.presets[pi].e[i];
    return e;
}

/* the master's DC blockers and limiter, as a fresh boot (fx.c fx_boot): the voices' bit-stable renders start
 * from the same master state too */
static void master_reset(void)
{
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = 0;
}

/* track 0 the WAVETABLE engine with e[0..7], a flat envelope (ATK 0, SUS full), no sends, POLY */
static void setup(const int16_t *e)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], eng_wt(), 0);
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = e[i];
    trk[0].p[P_ATK] = 0;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_REL] = 10;
    trk[0].p[P_ED_FLT] = 0;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    master_reset();
}

/* the voices' sum (track_render, before the mix) of one note from the note-on */
static void voice_note(const int16_t *e, uint32_t note, uint32_t vel, double *y, uint32_t n)
{
    static int32_t b[CTL];
    uint32_t i, k;
    setup(e);
    trk_note_on(&trk[0], note, vel);
    for (i = 0; i < n; i += CTL) {
        track_render(&trk[0], b, CTL);
        for (k = 0; k < CTL && i + k < n; k++)
            y[i + k] = b[k];
    }
}

/* the mono sum of the stereo mix (L + R) / 2 of one note from the note-on */
static void mix_note(const int16_t *e, uint32_t note, uint32_t vel, double *y, uint32_t n)
{
    static int32_t o[2 * CTL];
    uint32_t i, k;
    setup(e);
    trk_note_on(&trk[0], note, vel);
    for (i = 0; i < n; i += CTL) {
        mix_block(o, CTL);
        for (k = 0; k < CTL && i + k < n; k++)
            y[i + k] = (o[2 * k] + o[2 * k + 1]) / 2.0;
    }
}

static double rms(const double *y, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += y[i] * y[i];
    return sqrt(s / n);
}

static double mean(const double *y, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += y[i];
    return s / n;
}

static double peak(const double *y, uint32_t n)
{
    double p = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        if (fabs(y[i]) > p)
            p = fabs(y[i]);
    return p;
}

/* the mean absolute difference of two renders, in sample units */
static double diff_mean(const double *a, const double *b, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += fabs(a[i] - b[i]);
    return s / n;
}

/* ---------------------------------------------------------- engine --- */
static void engine(void)
{
    const engine_t *e = &ENG_WT;
    uint32_t i, labels = 1;
    char what[160];
    host_tracks_init();
    trk[0].eng_req = (uint8_t)ENGI_WT;
    for (i = 0; i < 8u; i++)
        labels &= e->edit[i].label && e->edit[i].label[0];
    snprintf(what, sizeof what, "engine: index %u (ENGI_WT %u), \"%s\", pages \"%s\" / \"%s\"", eng_wt(), (uint32_t)ENGI_WT,
             e->name, e->page_title[0], e->page_title[1]);
    check(what, eng_wt() == 14u && eng_wt() == ENGI_WT && ENGINES[eng_wt()] == &ENG_WT && !strcmp(e->name, "WAVETABLE") &&
          !strcmp(e->page_title[0], "OSC A") && !strcmp(e->page_title[1], "OSC B"));
    snprintf(what, sizeof what, "engine: 8 EDIT parameters, %u presets, knob POS FDBK B>A B-LVL", (uint32_t)e->npresets);
    check(what, labels && e->npresets == 8u && e->knob[0] == P_E1 && e->knob[1] == P_E2 && e->knob[2] == P_E3 &&
          e->knob[3] == P_E6);
    check("names: cards WAVE / POS, MOD card WAVE A / POS B",
          !strcmp(e->edit[0].label, "WAVE") && !strcmp(e->edit[1].label, "POS") &&
          !strcmp(e->edit[4].label, "WAVE") && !strcmp(e->edit[5].label, "POS") &&
          !strcmp(mod_dst_name(&trk[0], MD_E1), "WAVE A") && !strcmp(mod_dst_name(&trk[0], MD_E1 + 1), "POS A") &&
          !strcmp(mod_dst_name(&trk[0], MD_E1 + 4), "WAVE B") && !strcmp(mod_dst_name(&trk[0], MD_E1 + 5), "POS B"));
}

/* ---------------------------------------------------------- tables --- */
static void tables(void)
{
    uint32_t t, f, i, meta = 1, range = 1, base = 1;
    for (t = 0; t < WT_NFACTORY; t++) {
        const wt_table_t *tb = &WT_TABLES[t];
        meta &= tb->data != 0 && tb->frames == 16u && tb->base_log2 == 9u && tb->mip_last_log2 == 3u;
        for (f = 0; f < tb->frames; f++) {
            uint32_t nonzero = 0;
            for (i = 0; i < WT_FRAME_SAMPLES; i++) {
                int32_t s = tb->data[f * WT_FRAME_SAMPLES + i];
                range &= s >= -32767 && s <= 32767;
                if (i < (1u << tb->base_log2) && s)
                    nonzero = 1;
            }
            base &= nonzero;
        }
    }
    check("tables: two factory tables, 16 frames, base 2^9 (1016 samples a frame)", WT_NFACTORY == 2u && meta);
    check("tables: every frame's base level non-zero, every stored sample in [-32767, 32767]", base && range);
}

static void mips(void)
{
    static const uint32_t INC[] = {0, 1, 64, 4096, 65536, 1u << 20, 1u << 24, 1u << 27, 0x10000000u, 1u << 29,
                                   0x40000000u, 0x80000000u, 0xFFFFFFFFu};
    uint32_t i, l, prev = 0, mono = 1, maxl = 0;
    char what[160];
    for (i = 0; i < NELEM(INC); i++) {
        l = wt_mip_level(9, INC[i]);
        mono &= l >= prev;
        prev = l;
        maxl = l > maxl ? l : maxl;
    }
    snprintf(what, sizeof what, "mips: inc 0 -> %u, inc 1 -> %u, inc 2^29 -> %u, inc 0xFFFFFFFF -> %u (max %u), monotonic %s",
             wt_mip_level(9, 0), wt_mip_level(9, 1), wt_mip_level(9, 1u << 29), wt_mip_level(9, 0xFFFFFFFFu), maxl,
             mono ? "yes" : "no");
    check(what, wt_mip_level(9, 0) == 0u && wt_mip_level(9, 1) == 0u && wt_mip_level(9, 4096) == 0u &&
          wt_mip_level(9, 1u << 29) == 6u && wt_mip_level(9, 0xFFFFFFFFu) == 6u && maxl == 6u && mono);
}

/* --------------------------------------------------------- presets --- */
static void presets(void)
{
    static const uint32_t NOTE[2] = {36, 60};        /* C2, C4 */
    static double y[NRENDER];
    uint32_t pi, k;
    int sound = 1, dc = 1, clip = 1;
    double worst_dc = 0, quietest = 1e9, vpeak = 0, mpeak = 0;
    char what[200];
    for (pi = 0; pi < ENG_WT.npresets; pi++)
        for (k = 0; k < 2u; k++) {
            double r, m, p, vm;
            voice_note(pe(pi), NOTE[k], 100, y, NRENDER);
            p = peak(y, NRENDER);
            vm = mean(y + NNOTE, NNOTE);             /* (the voice's own offset: the feedback's, before the mix's blocker) */
            clip &= p < 2.0 * VOICE_FS;
            vpeak = p > vpeak ? p : vpeak;
            mix_note(pe(pi), NOTE[k], 100, y, NRENDER);
            r = rms(y + NNOTE, NNOTE);
            m = mean(y + NNOTE, NNOTE);
            p = peak(y + NNOTE, NNOTE);
            sound &= r > 0.005 * VOICE_FS;
            dc &= fabs(m) < 0.01 * VOICE_FS;
            clip &= p < 32767.0;
            quietest = r < quietest ? r : quietest;
            worst_dc = fabs(m) > worst_dc ? fabs(m) : worst_dc;
            mpeak = p > mpeak ? p : mpeak;
            if (getenv("VERBOSE"))
                printf("wt:   %-10s %s RMS %6.1f  voice mean %+7.1f  mix mean %+7.2f  mix peak %6.0f\n",
                       ENG_WT.presets[pi].name, k ? "C4" : "C2", r, vm, m, p);
        }
    snprintf(what, sizeof what, "presets: all 8 sound at C2 and C4 (quietest RMS %.0f, %.2f %% of VOICE_FS)", quietest,
             quietest / VOICE_FS * 100);
    check(what, sound);
    snprintf(what, sizeof what, "no DC: worst |mean| %.1f (%.2f %% of VOICE_FS)", worst_dc, worst_dc / VOICE_FS * 100);
    check(what, dc);
    snprintf(what, sizeof what, "no clipping: voice peak %.0f (limit %.0f), mix peak %.0f (limit 32767)", vpeak,
             2.0 * VOICE_FS, mpeak);
    check(what, clip);
}

/* ------------------------------------------------------- determinism --- */
static void determinism(void)
{
    static double a[NNOTE], b[NNOTE];
    uint32_t pi, i, same = 1;
    for (pi = 0; pi < ENG_WT.npresets; pi++) {
        mix_note(pe(pi), 62, 100, a, NNOTE);
        mix_note(pe(pi), 62, 100, b, NNOTE);
        for (i = 0; i < NNOTE; i++)
            same &= a[i] == b[i];
    }
    check("a note from silence renders bit-identically twice (every preset, phases reset)", same);
}

/* ---------------------------------------------------------- controls --- */
#define WT_DIFF (0.005 * VOICE_FS)

static void controls(void)
{
    static double a[NRENDER], b[NRENDER], c[NRENDER];
    int16_t e[8] = {0, 24, 0, 0, 0, 0, 0, 3};
    double d01, d02, d12, r0, r1;
    char what[200];
    /* A-POS: three positions of the fixed BASIC / SPECTRAL frames */
    e[1] = 0;
    mix_note(e, 48, 100, a, NNOTE);
    e[1] = 64;
    mix_note(e, 48, 100, b, NNOTE);
    e[1] = 127;
    mix_note(e, 48, 100, c, NNOTE);
    d01 = diff_mean(a, b, NNOTE);
    d02 = diff_mean(a, c, NNOTE);
    d12 = diff_mean(b, c, NNOTE);
    snprintf(what, sizeof what, "A-POS 0 / 64 / 127 differ: mean |diff| %.0f / %.0f / %.0f (threshold %.0f)", d01, d02,
             d12, WT_DIFF);
    check(what, d01 > WT_DIFF && d02 > WT_DIFF && d12 > WT_DIFF);
    /* FDBK: four amounts, each against 0; the highest still bounded (the mix measured after the master's
     * DC blockers have settled) */
    {
        static const int16_t FB[4] = {0, 32, 64, 127};
        double d[3], mdc;
        uint32_t k;
        e[1] = 24;
        e[2] = FB[0];
        mix_note(e, 48, 100, a, NRENDER);
        for (k = 1; k < 4u; k++) {
            e[2] = FB[k];
            mix_note(e, 48, 100, b, NRENDER);
            d[k - 1u] = diff_mean(a + NNOTE, b + NNOTE, NNOTE);
        }
        mdc = mean(b + NNOTE, NNOTE);
        voice_note(e, 48, 100, c, NRENDER);
        snprintf(what, sizeof what, "FDBK 32 / 64 / 127 change the sound: mean |diff| %.0f / %.0f / %.0f (threshold %.0f); "
                 "127 voice peak %.0f, mix |mean| %.1f, mix peak %.0f", d[0], d[1], d[2], WT_DIFF, peak(c, NRENDER),
                 fabs(mdc), peak(b + NNOTE, NNOTE));
        check(what, d[0] > WT_DIFF && d[1] > WT_DIFF && d[2] > WT_DIFF && peak(c, NRENDER) < 2.0 * VOICE_FS &&
              fabs(mdc) < 0.01 * VOICE_FS && peak(b + NNOTE, NNOTE) < 32767.0);
    }
    /* B>A: B phase-modulates A */
    e[2] = 8;
    e[3] = 0;
    e[5] = 0;
    e[6] = 0;
    mix_note(e, 48, 100, a, NNOTE);
    e[3] = 64;
    mix_note(e, 48, 100, b, NNOTE);
    snprintf(what, sizeof what, "B>A 0 vs 64 change the sound: mean |diff| %.0f (threshold %.0f)", diff_mean(a, b, NNOTE),
             WT_DIFF);
    check(what, diff_mean(a, b, NNOTE) > WT_DIFF);
    /* B-LVL: B in the mix */
    e[3] = 0;
    e[6] = 0;
    mix_note(e, 48, 100, a, NNOTE);
    e[6] = 100;
    mix_note(e, 48, 100, b, NNOTE);
    r0 = rms(a, NNOTE);
    r1 = rms(b, NNOTE);
    snprintf(what, sizeof what, "B-LVL 0 vs 100 raises the RMS: %.0f -> %.0f (x%.2f)", r0, r1, r1 / (r0 + 1e-9));
    check(what, r1 > r0 * 1.02 && r1 > r0 + WT_DIFF);
    /* RATIO: 0.25 vs 2.0 */
    e[6] = 60;
    e[3] = 64;
    e[7] = 0;
    mix_note(e, 48, 100, a, NNOTE);
    e[7] = 5;
    mix_note(e, 48, 100, b, NNOTE);
    snprintf(what, sizeof what, "RATIO 0.25 vs 2.0 change the sound: mean |diff| %.0f (threshold %.0f)",
             diff_mean(a, b, NNOTE), WT_DIFF);
    check(what, diff_mean(a, b, NNOTE) > WT_DIFF);
    /* USR1..USR3: no user slot format in pass 1 -> the sine fallback */
    {
        static const int16_t WAV[3] = {2, 3, 4};
        int sound = 1;
        uint32_t k;
        e[0] = WAV[0];
        e[1] = 64;
        e[2] = 0;
        e[3] = 0;
        e[4] = 0;
        e[5] = 0;
        e[6] = 0;
        e[7] = 3;
        mix_note(e, 48, 100, a, NNOTE);
        for (k = 1; k < 3u; k++) {
            int same;
            uint32_t i;
            e[0] = WAV[k];
            mix_note(e, 48, 100, b, NNOTE);
            sound &= rms(b, NNOTE) > 0.005 * VOICE_FS;
            same = 1;
            for (i = 0; i < NNOTE; i++)
                same &= a[i] == b[i];
            if (!same)
                sound = 0;
        }
        sound &= rms(a, NNOTE) > 0.005 * VOICE_FS;
        snprintf(what, sizeof what, "USR1..USR3 (A-WAV 2..4) play the sine fallback, non-silent, the same one (RMS %.0f)",
                 rms(a, NNOTE));
        check(what, sound);
    }
}

/* the EDIT graph's preview: A's processed wave and B's raw wave; FM, feedback and ratio must change A */
static double pv_diff(const int32_t *a, const int32_t *b)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < WT_PREVIEW_N; i++)
        s += fabs((double)(a[i] - b[i]));
    return s / WT_PREVIEW_N;
}

static void preview_math(void)
{
    int32_t a[WT_PREVIEW_N], b[WT_PREVIEW_N], base[WT_PREVIEW_N];
    uint32_t i, peak = 0;
    char what[160];
    host_tracks_init();
    trk[0].eng_req = (uint8_t)ENGI_WT;
    trk[0].p[P_E0] = 0;
    trk[0].p[P_E1] = 0;
    trk[0].p[P_E2] = 0;
    trk[0].p[P_E3] = 0;
    trk[0].p[P_E4] = 0;
    trk[0].p[P_E5] = 0;
    trk[0].p[P_E6] = 0;
    trk[0].p[P_E7] = 3;                                /* x1 */
    wt_preview(&trk[0], a, b, WT_PREVIEW_N);
    for (i = 0; i < WT_PREVIEW_N; i++) {
        peak = (uint32_t)(fabs((double)a[i]) > peak ? fabs((double)a[i]) : peak);
        peak = (uint32_t)(fabs((double)b[i]) > peak ? fabs((double)b[i]) : peak);
        base[i] = a[i];
    }
    snprintf(what, sizeof what, "preview: A and B sound at x1 (peak %u)", peak);
    check(what, peak > 100u);
    trk[0].p[P_E3] = 64;
    wt_preview(&trk[0], a, b, WT_PREVIEW_N);
    snprintf(what, sizeof what, "preview: B>A changes A (mean diff %.0f)", pv_diff(base, a));
    check(what, pv_diff(base, a) > 40.0);
    for (i = 0; i < WT_PREVIEW_N; i++) base[i] = a[i];
    trk[0].p[P_E7] = 7;                                /* x3 */
    wt_preview(&trk[0], a, b, WT_PREVIEW_N);
    snprintf(what, sizeof what, "preview: RATIO changes the FM result in A (mean diff %.0f)", pv_diff(base, a));
    check(what, pv_diff(base, a) > 40.0);
    for (i = 0; i < WT_PREVIEW_N; i++) base[i] = a[i];
    trk[0].p[P_E2] = 80;
    wt_preview(&trk[0], a, b, WT_PREVIEW_N);
    snprintf(what, sizeof what, "preview: FDBK changes A (mean diff %.0f)", pv_diff(base, a));
    check(what, pv_diff(base, a) > 40.0);
    for (i = 0; i < WT_PREVIEW_N; i++) base[i] = a[i];
    trk[0].p[P_E1] = 64;
    wt_preview(&trk[0], a, b, WT_PREVIEW_N);
    snprintf(what, sizeof what, "preview: POS changes A (mean diff %.0f)", pv_diff(base, a));
    check(what, pv_diff(base, a) > 40.0);
}

/* ---------------------------------------------------------------- cost --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}

static double mix_cost(const int16_t *e, uint32_t notes)
{
    static int32_t o[2 * CTL];
    uint32_t i, nb = FS * 2u / CTL;
    uint64_t i0;
    setup(e);
    for (i = 0; i < notes; i++)
        trk_note_on(&trk[0], 48 + 3 * i, 100);
    for (i = 0; i < 64u; i++)
        mix_block(o, CTL);
    i0 = instr_now();
    for (i = 0; i < nb; i++)
        mix_block(o, CTL);
    return (double)(instr_now() - i0) / (nb * CTL);
}

static void cost(void)
{
    double idle, c, worst = 0;
    uint32_t pi;
    char what[200];
    if (!instr_now()) {
        printf("wt: cost: no instruction counter on this host (proc_pid_rusage); not measured\n");
        return;
    }
    idle = mix_cost(pe(0), 0);
    printf("wt: cost, host instructions per sample and voice (8 held; device estimate 1.7 %% per 100):\n");
    for (pi = 0; pi < ENG_WT.npresets; pi++) {
        c = (mix_cost(pe(pi), 8) - idle) / 8;
        printf("wt:   %-10s %5.0f  ~%.2f %%\n", ENG_WT.presets[pi].name, c, c * 0.017);
        worst = c > worst ? c : worst;
    }
    snprintf(what, sizeof what, "cost: at most %.0f instructions a sample and voice (~%.1f %% on the device; limit %.0f)",
             worst, worst * 0.017, WT_COST_MAX);
    check(what, worst <= WT_COST_MAX);
}

/* --------------------------------------------------------------- demos --- */
static void demo(const char *dir, uint32_t pi)
{
    static int32_t o[2 * CTL];
    const preset_t *pr = &ENG_WT.presets[pi];
    uint32_t pat = pr->pat ? pr->pat - 1u : 4u, step = FS / 8u, s, i, held = 0, frames = 0;
    char path[512], nm[32];
    FILE *w;
    for (i = 0; pr->name[i] && i < 31u; i++)
        nm[i] = pr->name[i] == ' ' ? '_' : pr->name[i];
    nm[i] = 0;
    snprintf(path, sizeof path, "%s/wt_%02u_%s.wav", dir, pi, nm);
    if (!(w = fopen(path, "wb")))
        return;
    wav_hdr(w, 0);
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], eng_wt(), pi);
    master_reset();
    for (s = 0; s < 48u; s++) {                       /* three times through the pattern's 16 steps, 120 BPM 1/16 */
        uint32_t k = s % 16u, note = PATTERNS[pat].note[k], fl = PATTERNS[pat].flags[k];
        if (!(fl & 4u) && held) {
            trk_note_off(&trk[0], held);
            held = 0;
        }
        if (note && !(fl & 4u)) {
            trk_note_on(&trk[0], note, fl & 1u ? 120 : 90);
            held = note;
        }
        for (i = 0; i < step; i += CTL) {
            uint32_t j;
            mix_block(o, CTL);
            for (j = 0; j < CTL; j++)
                wav_put(w, o[2 * j], o[2 * j + 1]);
            frames += CTL;
        }
    }
    if (held)
        trk_note_off(&trk[0], held);
    for (i = 0; i < FS * 2u; i += CTL) {
        uint32_t j;
        mix_block(o, CTL);
        for (j = 0; j < CTL; j++)
            wav_put(w, o[2 * j], o[2 * j + 1]);
        frames += CTL;
    }
    fseek(w, 0, SEEK_SET);
    wav_hdr(w, frames);
    fclose(w);
}

static void demos(const char *dir)
{
    uint32_t pi;
    for (pi = 0; pi < ENG_WT.npresets; pi++)
        demo(dir, pi);
    printf("wt: demos in %s: the %u presets, each playing its suggested pattern\n", dir, (uint32_t)ENG_WT.npresets);
}

int main(int argc, char **argv)
{
    engine();
    tables();
    mips();
    presets();
    determinism();
    controls();
    preview_math();
    if (!getenv("NOCOST"))
        cost();
    if (argc > 1)
        demos(argv[1]);
    printf("wt_test: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
