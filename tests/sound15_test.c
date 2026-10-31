/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of 1.5's sound items (Discussions #104, #175), same sources as the firmware (hostsim.c):
 *   build/host/sound15_test [DEMO_DIR]      (run_tests.sh builds and runs it; demos in build/sound15_demo/)
 * 1. ANALOG's filter TYPE (P_FTYPE, eng_analog.c analog_tap / analog_comb): the parameter (LP BP HP COMB, default LP,
 *    not recordable, on EDIT > FILTER), LP untouched (the golden renders: regress.c) and TYPE ignored by every other
 *    engine (bit for bit), the responses (sines through a 1 kHz cutoff: LP falls above it, HP below, BP peaks at it;
 *    the three alike at the cutoff; RES raises BP's peak), COMB colours vs LP and RES raises its energy, bounded and
 *    without DC at the corners (RES and DRV 127, low and high notes, every WAVE, SYNC and SUB too), a TYPE change
 *    while a note sounds stays bounded.
 * 2. ENV SYNC (P_ESYNC, voice.c esync_coef): the parameter (OFF / ON, default OFF, not recordable, ENV DEST's KNOB 4);
 *    OFF takes the tables whatever esync_k holds (bit for bit); ON: ATK DEC REL show note values (1/64T .. 4BAR, 25
 *    names over 0..127, a knob detent each), and the attack reaches the top, the decay and the release -40 dB, after
 *    that note value at 60 / 120 / 173 BPM and on the external clock's measured tempo (within a block / 2 %).
 * 3. the cost: ANALOG 8 voices LP / BP / HP, ENV SYNC on / off (instructions per sample).
 * 4. demos (DEMO_DIR): analog_bp, analog_hp (TYPE BP / HP, the cutoff swept), env_sync (the same phrase at 90 and
 *    150 BPM: ATK 1/8, REL 1/4 follow the tempo). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("sound15: %-74s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t out_buf[2 * CTL];
static uint64_t hash;
static void blocks(uint32_t n)
{
    uint32_t i, k;
    while (n--) {
        mix_block(out_buf, CTL);
        for (i = 0; i < 2u * CTL; i++)
            for (k = 0; k < 4u; k++) {
                hash ^= ((uint32_t)out_buf[i] >> (8u * k)) & 0xFFu;
                hash *= 0x100000001B3ull;
            }
    }
}

static uint32_t eng_by_name(const char *n)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (str_eq(ENGINES[e]->name, n))
            return e;
    return 0;
}
static uint32_t preset_by_name(uint32_t e, const char *n)
{
    uint32_t k;
    for (k = 0; k < ENGINES[e]->npresets; k++)
        if (str_eq(ENGINES[e]->presets[k].name, n))
            return k;
    return 0;
}

static void fresh(uint32_t e, uint32_t pi)        /* the boot state (no FX tails), track 1 = engine e preset pi */
{
    uint32_t k;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(rev_tank, 0, sizeof rev_tank);
    R = &rev_tank[0];
    memset(&fx, 0, sizeof fx);
    memset(&pf, 0, sizeof pf);
    for (k = 0; k < NTRK; k++)
        pf.mg[k] = 32768;
    perf_held = perf_latched = 0;
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    vage = 0;
    rng_state = 0x1234567u;
    mod_seed = 0x2545F491u;
    memset(trk, 0, sizeof trk);
    MOD_W_CLEAR();
    memset(sl, 0, sizeof sl);
    host_tracks_init();
    for (k = 0; k < NPART; k++)
        host_preset(&trk[k], k ? 0u : e, k ? 0u : pi);
    song.sel = 0;
    song.g[G_CLOCK] = 0;
    song.g[G_BPM] = 120;
    midi_beat_samples = 0;
    hash = 0xCBF29CE484222325ull;
}
static void dry(track_t *t)
{
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
}

/* the hash of a phrase on track 1 in a fork()ed child (the seeds as they were: the same in each); setup(t) first */
static uint64_t phrase_child(uint32_t e, uint32_t pi, void (*setup)(track_t *t))
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    fflush(stdout);
    if (!(pid = fork())) {
        track_t *t = &trk[0];
        fresh(e, pi);
        if (setup)
            setup(t);
        trk_note_on(t, 60, 100);
        trk_note_on(t, 64, 70);
        trk_note_on(t, 67, 120);
        blocks(FS / 2u / CTL);
        trk_note_on(t, 72, 90);
        blocks(FS / 4u / CTL);
        trk_note_off(t, 60);
        trk_note_off(t, 64);
        trk_note_off(t, 67);
        trk_note_off(t, 72);
        blocks(FS / CTL);
        h = hash;
        if (write(fd[1], &h, sizeof h) != sizeof h)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != sizeof h)
        h = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}

/* ------------------------------------------------------------------ 1 --- */
#define NREND (FS / 2u / CTL)                     /* half a second rendered, the stats from 0.1 s */
static int32_t ren_l[NREND * CTL];
typedef struct { double mean, rms, peak; } stats_t;
static stats_t stats(uint32_t from)
{
    stats_t s = {0};
    uint32_t i, n = NREND * CTL - from;
    for (i = from; i < NREND * CTL; i++) {
        double x = ren_l[i];
        s.mean += x;
        s.rms += x * x;
        s.peak = fabs(x) > s.peak ? fabs(x) : s.peak;
    }
    s.mean /= n;
    s.rms = sqrt(s.rms / n);
    return s;
}
/* one ANALOG note (dry, POLY, the amp envelope open, no filter modulation): WAVE w, filter TYPE ft at CUT cut, RES res,
 * DRV drv, DTN dtn, MIX mix */
static stats_t analog_note_d(uint32_t w, uint32_t ft, int32_t cut, int32_t res, int32_t drv, uint32_t note,
                             int32_t dtn, int32_t mix)
{
    track_t *t = &trk[0];
    uint32_t i, k;
    fresh(0, 0);
    dry(t);
    t->p[P_VOICE] = V_POLY;
    t->p[P_FTYPE] = (int16_t)ft;
    t->p[P_E0] = (int16_t)w;
    t->p[P_E1] = (int16_t)dtn;
    t->p[P_E2] = (int16_t)mix;
    t->p[P_E3] = 0;
    t->p[P_E4] = (int16_t)cut;
    t->p[P_E5] = (int16_t)res;
    t->p[P_E6] = (int16_t)drv;
    t->p[P_E7] = 0;                                       /* KTR 0: the cutoff where CUT puts it */
    t->p[P_ED_FLT] = 0;
    t->p[P_LD_FLT] = 0;
    t->p[P_ATK] = 0;
    t->p[P_SUS] = 127;
    trk_note_on(t, note, 100);
    for (k = 0; k < NREND; k++) {
        blocks(1);
        for (i = 0; i < CTL; i++)
            ren_l[k * CTL + i] = out_buf[2u * i];
    }
    trk_note_off(t, note);
    blocks(FS / 2u / CTL);
    return stats(FS / 10u);
}
static stats_t analog_note(uint32_t w, uint32_t ft, int32_t cut, int32_t res, int32_t drv, uint32_t note)
{
    return analog_note_d(w, ft, cut, res, drv, note, 0, w >= 5u ? 64 : 0);
}
static uint64_t sig_hash(void)                        /* ren_l's last note, for "sounds other than" */
{
    uint64_t h = 0xCBF29CE484222325ull;
    uint32_t i;
    for (i = 0; i < NREND * CTL; i++) {
        h ^= (uint32_t)ren_l[i];
        h *= 0x100000001B3ull;
    }
    return h;
}

/* one ANALOG OSC A/B note through the filter TYPE ft: WAVE w, WAVE2 w2, MIX, INT (P_E7), MORPH, DRIFT,
 * CUT and RES; no noise or drive. Fills ren_l like analog_note and resets the hash, so each call's hash is
 * its own */
static stats_t analog_osc2_note3(uint32_t w, uint32_t w2, int32_t mix, int32_t intv, int32_t morph, int32_t drift,
                                 uint32_t note, uint32_t ft, int32_t cut, int32_t res)
{
    track_t *t = &trk[0];
    uint32_t i, k;
    fresh(0, 0);
    dry(t);
    t->p[P_VOICE] = V_POLY;
    t->p[P_FTYPE] = (int16_t)ft;
    t->p[P_E0] = (int16_t)w;
    t->p[P_WAVE2] = (int16_t)w2;
    t->p[P_E1] = 0;
    t->p[P_E2] = (int16_t)mix;
    t->p[P_E3] = 0;
    t->p[P_E4] = (int16_t)cut;
    t->p[P_E5] = (int16_t)res;
    t->p[P_E6] = 0;
    t->p[P_E7] = (int16_t)intv;
    t->p[P_MORPH] = (int16_t)morph;
    t->p[P_DRIFT] = (int16_t)drift;
    t->p[P_ED_FLT] = 0;
    t->p[P_LD_FLT] = 0;
    t->p[P_ATK] = 0;
    t->p[P_SUS] = 127;
    if (ft == IFT_COMB)
        memset(ana_comb, 0, sizeof ana_comb);   /* the shared delay line: each COMB render starts clean */
    hash = 0;
    trk_note_on(t, note, 100);
    for (k = 0; k < NREND; k++) {
        blocks(1);
        for (i = 0; i < CTL; i++)
            ren_l[k * CTL + i] = out_buf[2u * i];
    }
    trk_note_off(t, note);
    blocks(FS / 2u / CTL);
    return stats(FS / 10u);
}

static stats_t analog_osc2_note2(uint32_t w, uint32_t w2, int32_t mix, int32_t intv, int32_t morph, int32_t drift,
                                 uint32_t note)
{
    return analog_osc2_note3(w, w2, mix, intv, morph, drift, note, 0, 127, 0);   /* LP, the filter open */
}

static stats_t analog_osc2_note(uint32_t w, int32_t mix, int32_t intv, int32_t morph, int32_t drift, uint32_t note)
{
    return analog_osc2_note2(w, 0, mix, intv, morph, drift, note);   /* WAVE2 SAME: follows OSC A */
}

/* Hz of ren_l's upward zero crossings (a clean sine / square: the last 0.4 s) */
static double zc_hz(void)
{
    uint32_t i, n = 0, from = FS / 10u, end = NREND * CTL;
    int32_t prev = ren_l[from];
    for (i = from + 1u; i < end; i++) {
        if (prev <= 0 && ren_l[i] > 0)
            n++;
        prev = ren_l[i];
    }
    return (double)n * (double)FS / (double)(end - from);
}

static void test_osc2(void)
{
    stats_t a, a2, b, c, m0, m64, m127, d0, d1;
    uint64_t h0, h64, h127, hd0, hd1;
    double f0, f1, f2;
    char msg[160];
    a = analog_osc2_note(3, 127, 0, 0, 0, 60);   f0 = zc_hz();  h0 = hash;
    a2 = analog_osc2_note(3, 127, 0, 0, 0, 60);
    check("INT 0: the same render twice (bit for bit)", a.rms == a2.rms && hash == h0);
    b = analog_osc2_note(3, 127, 12, 0, 0, 60);  f1 = zc_hz();
    c = analog_osc2_note(3, 127, -12, 0, 0, 60); f2 = zc_hz();
    check("INT +12: OSC 2 an octave up", f0 > 200 && fabs(f1 / f0 - 2.0) < 0.03);
    check("INT -12: OSC 2 an octave down", f0 > 200 && fabs(f2 / f0 - 0.5) < 0.03);
    m0 = analog_osc2_note(0, 0, 0, 0, 0, 60);   h0 = hash;
    m64 = analog_osc2_note(0, 0, 0, 64, 0, 60); h64 = hash;
    m127 = analog_osc2_note(0, 0, 0, 127, 0, 60); h127 = hash;
    check("MORPH: 0, 64, 127 are three different waves", h0 != h64 && h0 != h127 && h64 != h127);
    check("MORPH 127 from SAW is the square (a flatter crest factor than the saw)",
          m0.rms > 200 && m127.peak * m0.rms < m0.peak * m127.rms * 0.75 && m127.peak < 32767);
    d0 = analog_osc2_note(0, 0, 0, 0, 0, 60);   hd0 = hash;
    (void)analog_osc2_note(0, 0, 0, 0, 0, 60);
    check("DRIFT 0: the same render twice (bit for bit)", hash == hd0);
    d1 = analog_osc2_note(0, 0, 0, 0, 127, 60); hd1 = hash;
    check("DRIFT 127: the voice moves (another render) and stays bounded", hd1 != hd0 && d1.peak < 32767);
    {
        stats_t sa = analog_osc2_note2(3, 0, 127, 0, 0, 0, 60);   /* WAVE2 SAME, OSC A sine */
        stats_t si = analog_osc2_note2(3, 4, 127, 0, 0, 0, 60);   /* WAVE2 SIN */
        check("WAVE2 SAME: OSC B follows OSC A", fabs(sa.rms - si.rms) < 2 && fabs(sa.peak - si.peak) < 2);
        (void)analog_osc2_note2(0, 0, 127, 0, 0, 0, 60); h0 = hash;
        (void)analog_osc2_note2(3, 0, 127, 0, 0, 0, 60);
        check("WAVE2 SAME: changing OSC A changes OSC B with it", hash != h0);
    }
    {
        stats_t bs = analog_osc2_note2(0, 4, 127, 0, 0, 0, 60);   /* OSC A saw, OSC B sine, B alone */
        stats_t ss = analog_osc2_note2(3, 4, 127, 0, 0, 0, 60);
        check("WAVE2: OSC B plays its own wave (A saw / B sine == A sine / B sine at MIX 127)",
              fabs(bs.rms - ss.rms) < 2 && fabs(bs.peak - ss.peak) < 2);
    }
    (void)analog_osc2_note2(0, 2, 0, 0, 0, 0, 60); h0 = hash;     /* A alone */
    (void)analog_osc2_note2(0, 4, 0, 0, 0, 0, 60);
    check("WAVE2: OSC B's wave does not touch OSC A (MIX 0)", hash == h0);
    {   /* OSC B's own WAVE through the other filter TYPEs: at MIX 127 OSC A's wave must not matter */
        static const uint32_t FT[3] = {1, 2, 3};                 /* BP, HP, COMB */
        static const char *const FN[3] = {"BP", "HP", "COMB"};
        uint32_t ft;
        (void)analog_osc2_note3(3, 4, 127, 0, 0, 0, 60, 0, 70, 96);   /* the same OSC B sine on LP, for the hash */
        h0 = hash;
        for (ft = 0; ft < 3u; ft++) {
            stats_t bs = analog_osc2_note3(0, 4, 127, 0, 0, 0, 60, FT[ft], 70, 0);   /* A saw, B sine */
            stats_t ss = analog_osc2_note3(3, 4, 127, 0, 0, 0, 60, FT[ft], 70, 0);   /* A sine, B sine */
            snprintf(msg, sizeof msg,
                     "WAVE2 %s: OSC B plays its own wave (A saw / B sine == A sine / B sine at MIX 127)", FN[ft]);
            check(msg, bs.rms > 100 && fabs(bs.rms - ss.rms) < 2 && fabs(bs.peak - ss.peak) < 2);
        }
        (void)analog_osc2_note3(3, 4, 127, 0, 0, 0, 60, 1, 70, 96);   /* BP */
        check("WAVE2: the same OSC B wave sounds different through BP than LP (the filter really applies)",
              hash != h0);
    }
    (void)analog_osc2_note2(5, 1, 64, 0, 0, 0, 60); h0 = hash;    /* SYNC */
    (void)analog_osc2_note2(5, 2, 64, 0, 0, 0, 60);
    check("WAVE2: SYNC defines OSC B (the stored wave does nothing)", hash == h0);
    (void)analog_osc2_note2(6, 1, 64, 0, 0, 0, 60); h0 = hash;    /* SUB */
    (void)analog_osc2_note2(6, 2, 64, 0, 0, 0, 60);
    check("WAVE2: SUB defines OSC B (the stored wave does nothing)", hash == h0);
    {
        /* OSC B's WAVE 5 (PWM) must survive the descriptor's range (projects, presets, the knob) */
        const param_desc_t *d = param_desc_of(ENGI_ANALOG, P_WAVE2);
        int32_t w2 = param_fit(d, 5);
        stats_t pwm;
        uint64_t hp;
        uint32_t i, pos = 0, n = 0;
        pwm = analog_osc2_note2(3, (uint32_t)w2, 127, 0, 0, 0, 60);   /* A sine, B alone */
        hp = hash;
        for (i = FS / 10u; i < NREND * CTL; i++, n++)
            pos += ren_l[i] > 0;
        (void)analog_osc2_note2(3, 4, 127, 0, 0, 0, 60);              /* SIN, for the hash */
        check("WAVE2 5: the descriptor takes PWM (max 5, param_fit keeps it)",
              d->max == 5 && w2 == 5 && str_eq(d->names[5], "PWM"));
        check("WAVE2 5: OSC B plays PWM, not SIN, at about 50 % duty",
              hp != hash && fabs((double)pos / n - 0.5) < 0.1 && pwm.rms > 200);
    }
    {   /* MORPH on PWM widens the pulse from 50 % toward ~54 % at 127 */
        double d0, d1;
        uint32_t i, n = 0, p0 = 0, p1 = 0;
        (void)analog_osc2_note2(4, 5, 127, 0, 0, 0, 60);
        for (i = FS / 10u; i < NREND * CTL; i++, n++)
            p0 += ren_l[i] > 0;
        (void)analog_osc2_note2(4, 5, 127, 0, 127, 0, 60);
        for (i = FS / 10u; i < NREND * CTL; i++)
            p1 += ren_l[i] > 0;
        d0 = (double)p0 / n;
        d1 = (double)p1 / n;
        check("MORPH on PWM: 0..127 widens the duty by about 4 points (50 % -> 54 %)",
              fabs(d0 - 0.5) < 0.02 && d1 - d0 > 0.03 && d1 - d0 < 0.05);
    }
}

static void type_bp(track_t *t) { t->p[P_FTYPE] = 1; }
static void type_hp(track_t *t) { t->p[P_FTYPE] = 2; }
static void type_comb(track_t *t) { t->p[P_FTYPE] = 3; }

static void test_filter(void)
{
    static const uint8_t NOTE[5] = {36, 60, 84, 108, 120};
    const param_desc_t *d = &TP[P_FTYPE];
    stats_t r[3][5];
    uint32_t ft, n, e, cut = 0, same = 1;
    char msg[200];
    track_t *t = &trk[0];

    check("TYPE: a track parameter LP BP HP COMB, default LP (as before), not recordable (a setting)",
          d->fmt == F_ENUM && d->min == 0 && d->max == 3 && d->def == 0 && str_eq(d->label, "TYPE") &&
          str_eq(d->names[0], "LP") && str_eq(d->names[1], "BP") && str_eq(d->names[2], "HP") &&
          str_eq(d->names[3], "COMB") && !motion_param(P_FTYPE));
    {
        uint32_t i, flt = 0, osca = 0, oscb = 0;
        for (i = 0; i < NPAGES; i++) {
            if (PAGES[i].fam == FAM_EDIT && PAGES[i].id[0] == P_FTYPE)
                flt = i + 1u;
            if (PAGES[i].fam == FAM_EDIT && PAGES[i].id[0] == P_E0 && PAGES[i].id[1] == P_MORPH)
                osca = i + 1u;
            if (PAGES[i].fam == FAM_EDIT && PAGES[i].id[0] == P_WAVE2 && PAGES[i].id[1] == P_E7)
                oscb = i + 1u;
        }
        check("EDIT > OSC A: WAVE MORPH DRIFT NOISE, page 1",
              osca && osca + 1u == oscb && PAGES[osca - 1u].id[0] == P_E0 &&
              PAGES[osca - 1u].id[1] == P_MORPH && PAGES[osca - 1u].id[2] == P_DRIFT &&
              PAGES[osca - 1u].id[3] == P_E3);
        check("EDIT > OSC B: WAVE INT DTN MIX, page 2",
              oscb && oscb + 1u == flt && PAGES[oscb - 1u].id[0] == P_WAVE2 &&
              PAGES[oscb - 1u].id[1] == P_E7 && PAGES[oscb - 1u].id[2] == P_E1 &&
              PAGES[oscb - 1u].id[3] == P_E2);
        check("EDIT > FILTER: TYPE CUT RES DRV (ANALOG's, page 3)",
              flt && PAGES[flt - 1u].id[0] == P_FTYPE && PAGES[flt - 1u].id[1] == P_E4 &&
              PAGES[flt - 1u].id[2] == P_E5 && PAGES[flt - 1u].id[3] == P_E6);
    }
    /* every engine but ANALOG ignores TYPE; ANALOG's LP is the golden renders' (regress.c) */
    for (e = 1; e < NENGINES; e++) {
        if (ENGINES[e] == ENGINES[0] || !ENGINES[e]->npresets)
            continue;
        same &= phrase_child(e, 0, 0) == phrase_child(e, 0, type_hp);
    }
    check("TYPE BP / HP on every other engine: its phrase bit for bit (only ANALOG reads it)", same);
    {
        uint64_t h0 = phrase_child(0, 0, 0), h1 = phrase_child(0, 0, type_bp), h2 = phrase_child(0, 0, type_hp),
                 h3 = phrase_child(0, 0, type_comb);
        check("ANALOG SAW LEAD: BP and HP sound other than LP (and each other)", h0 != h1 && h0 != h2 && h1 != h2);
        check("ANALOG SAW LEAD: COMB sounds other than LP (feedback comb, not a stub)", h0 != h3);
    }
    /* COMB: not a stub; RES / CUT move the render; high RES stays bounded */
    {
        stats_t c0 = analog_note(0, 3, 48, 0, 0, 60), c1 = analog_note(0, 3, 48, 110, 0, 60);
        stats_t clo = analog_note(0, 3, 20, 90, 0, 60), chi = analog_note(0, 3, 110, 90, 0, 60);
        check("COMB: RES 0 and RES 110 both sound, high RES bounded",
              c0.rms > 200 && c1.rms > 200 && c1.peak < 32767);
        check("COMB: RES changes the level or peak (feedback colours)",
              fabs(c1.rms - c0.rms) > c0.rms * 0.08 || fabs(c1.peak - c0.peak) > 200);
        check("COMB: CUT changes the timbre (delay length)",
              fabs(clo.rms - chi.rms) > clo.rms * 0.05 || fabs(clo.peak - chi.peak) > 100);
        snprintf(msg, sizeof msg, "COMB: RES 0 / 110 rms %.0f / %.0f; CUT 20 / 110 rms %.0f / %.0f",
                 c0.rms, c1.rms, clo.rms, chi.rms);
        printf("sound15: %s\n", msg);
    }
    /* the responses: sines (WAVE SIN) at 65 Hz .. 8.4 kHz through a cutoff at C6 (1047 Hz), RES 0 */
    for (n = 1; n < 128u; n++)
        if (abs((int)CUTOFF_HZ[n] - 1047) < abs((int)CUTOFF_HZ[cut] - 1047))
            cut = n;
    for (ft = 0; ft < 3u; ft++)
        for (n = 0; n < 5u; n++)
            r[ft][n] = analog_note(3, ft, (int32_t)cut - ((int32_t)NOTE[n] - 60) * 3 / 12, 0, 0, NOTE[n]);
    for (ft = 0; ft < 3u; ft++) {
        snprintf(msg, sizeof msg, "  %s (CUT %u = %u Hz): rms at 65 262 1047 4186 8372 Hz %.0f %.0f %.0f %.0f %.0f",
                 ft == 0 ? "LP" : ft == 1 ? "BP" : "HP", cut, CUTOFF_HZ[cut], r[ft][0].rms, r[ft][1].rms, r[ft][2].rms,
                 r[ft][3].rms, r[ft][4].rms);
        printf("sound15: %s\n", msg);
    }
    check("LP: flat below the cutoff, falling above it (12 dB / octave)",
          fabs(r[0][0].rms / r[0][1].rms - 1) < 0.1 && r[0][3].rms < r[0][1].rms * 0.12 && r[0][4].rms < r[0][3].rms * 0.4);
    check("HP: flat above the cutoff, falling below it (12 dB / octave)",
          fabs(r[2][4].rms / r[2][3].rms - 1) < 0.1 && r[2][1].rms < r[2][3].rms * 0.12 && r[2][0].rms < r[2][1].rms * 0.4);
    check("BP: its peak at the cutoff, falling both ways (6 dB / octave)",
          r[1][2].rms > r[1][1].rms * 2 && r[1][2].rms > r[1][3].rms * 2 && r[1][0].rms < r[1][1].rms * 0.4 &&
          r[1][4].rms < r[1][3].rms * 0.7);
    check("at the cutoff the three alike (within 25 %): TYPE keeps the level",
          fabs(r[1][2].rms / r[0][2].rms - 1) < 0.25 && fabs(r[2][2].rms / r[0][2].rms - 1) < 0.25);
    {
        stats_t q = analog_note(3, 1, (int32_t)cut, 100, 0, 84);
        snprintf(msg, sizeof msg, "BP: RES raises the peak (RES 100 %.0f vs RES 0 %.0f at the cutoff), bounded", q.rms,
                 r[1][2].rms);
        check(msg, q.rms > r[1][2].rms * 1.3 && q.peak < 32000);
    }
    /* the corners: every WAVE (SYNC, SUB too), RES and DRV at 127, low / high notes and cutoffs: bounded, no DC */
    {
        static const uint8_t CN[3] = {24, 60, 108}, CC[3] = {0, 70, 127};
        uint32_t w, okc = 1;
        double worst = 0, dc = 0;
        for (ft = 1; ft < 3u; ft++)
            for (w = 0; w < 7u; w++)
                for (n = 0; n < 3u; n++)
                    for (e = 0; e < 3u; e++) {
                        stats_t s = analog_note(w, ft, CC[e], 127, 127, CN[n]);
                        worst = s.peak > worst ? s.peak : worst;
                        if (s.rms >= 500)                 /* (a note far outside the band: its residue, -40 dB) */
                            dc = fabs(s.mean) / s.rms > dc ? fabs(s.mean) / s.rms : dc;
                        okc &= s.peak < 32767 && (s.rms < 500 || fabs(s.mean) < s.rms * 0.05);
                    }
        snprintf(msg, sizeof msg, "BP / HP at the corners (7 waves, RES DRV 127): bounded (peak %.0f), no DC (%.3f of rms)",
                 worst, dc);
        check(msg, okc);
        okc = 1;
        for (ft = 1; ft < 3u; ft++)
            for (w = 5; w < 7u; w++) {
                stats_t s = analog_note(w, ft, (int32_t)cut, 30, 0, 57);
                okc &= s.rms > 200;
            }
        check("SYNC / SUB through BP and HP: they sound", okc);
    }
    /* TYPE turned while a resonant note sounds: the filter's state carries over, bounded */
    {
        uint32_t k, i;
        int32_t pk = 0;
        fresh(0, preset_by_name(0, "ACID"));
        dry(t);
        t->p[P_E5] = 127;
        trk_note_on(t, 45, 120);
        for (k = 0; k < 3u * FS / CTL; k++) {
            if (!(k % 40u))
                t->p[P_FTYPE] = (int16_t)((k / 40u) % 4u);
            blocks(1);
            for (i = 0; i < CTL; i++)
                pk = abs(out_buf[2u * i]) > pk ? abs(out_buf[2u * i]) : pk;
        }
        trk_note_off(t, 45);
        blocks(FS / CTL);
        snprintf(msg, sizeof msg, "TYPE switched every 29 ms under a resonant ACID note (LP..COMB): bounded (peak %d)", pk);
        check(msg, pk < 32767);
    }
}

/* ------------------------------------------------------------------ 2 --- */
static void esync_garbage(track_t *t)
{
    t->esync_k[0] = 0x123456u;                       /* (the per-part state, since the second core) */
    t->esync_k[1] = 999u;
    t->esync_k[2] = 77u;
}
static int32_t value_of(const char *name)        /* the first value 0..127 ENV SYNC shows as name */
{
    int32_t v;
    char b[8];
    const char *u;
    for (v = 0; v < 128; v++) {
        param_format(&ESYNC_DESC[0], v, b, &u);
        if (str_eq(b, name))
            return v;
    }
    return -1;
}
static voice_t *the_voice(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active)
            return &t->v[i];
    return 0;
}
/* ENV SYNC on, SINE KEY: the samples the attack (to the top), the decay (SUS 0, to -40 dB) and the release (to -40 dB)
 * take with ATK / DEC / REL = the note value named; T: what that value lasts at the tempo (samples) */
typedef struct { double atk, dec, rel; } times_t;
static times_t env_times(const char *name)
{
    times_t r = {0, 0, 0};
    track_t *t = &trk[0];
    voice_t *v;
    uint32_t k;
    int32_t val = value_of(name);
    uint16_t g = song.g[G_BPM], c = song.g[G_CLOCK];
    uint32_t mb = midi_beat_samples;
    fresh(0, preset_by_name(0, "SINE KEY"));
    song.g[G_BPM] = (int16_t)g;
    song.g[G_CLOCK] = (int16_t)c;
    midi_beat_samples = mb;
    dry(t);
    t->p[P_VOICE] = V_MONO;
    t->p[P_ESYNC] = 1;
    t->p[P_ATK] = t->p[P_DEC] = t->p[P_REL] = (int16_t)val;
    t->p[P_SUS] = 127;
    trk_note_on(t, 60, 100);
    v = the_voice(t);
    for (k = 0; v && v->stage == 1u && k < 400000u; k++)
        blocks(1);
    r.atk = k * (double)CTL;
    t->p[P_SUS] = 0;                                      /* the decay: from the top to -40 dB */
    for (k = 0; v && v->env > (1 << 24) / 100 && k < 400000u; k++)
        blocks(1);
    r.dec = k * (double)CTL;
    t->p[P_SUS] = 127;                                    /* back up (an attack again), then the release */
    trk_note_off(t, 60);
    blocks(4);
    trk_note_on(t, 60, 100);
    v = the_voice(t);
    while (v && v->stage == 1u)
        blocks(1);
    blocks(2);
    trk_note_off(t, 60);
    for (k = 0; v && v->active && v->env > (1 << 24) / 100 && k < 400000u; k++)
        blocks(1);
    r.rel = k * (double)CTL;
    trk_note_off(t, 60);
    blocks(8);
    return r;
}
static double esync_T(const char *name)          /* samples the note value lasts now */
{
    uint32_t i;
    for (i = 0; i < ESYNC_N; i++)
        if (str_eq(ESYNC_NAMES[i], name))
            return beat_samples() * (double)ESYNC_UNITS[i] / 96.0;
    return 0;
}
static int times_ok(const char *name, const char *where)
{
    times_t m = env_times(name);
    double T = esync_T(name);
    char msg[200];
    double tol = T * (0.02 + 0.5 * T / 9646899.0) + 2 * CTL;   /* 2 %, and half a unit of the Q16 coefficient */
    int ok = fabs(m.atk - T) <= T * 0.001 + 2 * CTL && fabs(m.dec - T) <= tol && fabs(m.rel - T) <= tol;
    snprintf(msg, sizeof msg, "  %-5s %s: %.0f samples; ATK %.0f DEC %.0f REL %.0f", name, where, T, m.atk, m.dec, m.rel);
    check(msg, ok);
    return ok;
}

/* SUPER: one voice holds the centre saw and three detuned pairs; MIX 0 leaves the centre alone */
static void test_super(void)
{
    const param_desc_t *d = &ENGINES[0]->edit[0];
    stats_t saw, c0, s0, s1, hi;
    uint64_t h0, h1;
    check("SUPER: WAVE gains an eighth name, 0..7", d->fmt == F_ENUM && d->min == 0 && d->max == 7 &&
          str_eq(d->names[7], "SUPER"));
    saw = analog_note_d(0, 0, 110, 0, 0, 60, 0, 0);
    c0 = analog_note_d(7, 0, 110, 0, 0, 60, 100, 0);           /* WIDTH / MIX 0: centre only */
    s0 = analog_note_d(7, 0, 110, 0, 0, 60, 0, 64);
    h0 = sig_hash();
    s1 = analog_note_d(7, 0, 110, 0, 0, 60, 100, 64);
    h1 = sig_hash();
    hi = analog_note_d(7, 0, 120, 100, 0, 108, 127, 127);      /* the corners: high note, all detune */
    check("SUPER MIX 0: one saw, the same level as SAW", fabs(c0.rms - saw.rms) < saw.rms * 0.05);
    check("SUPER: DTN 0 and DTN 100 sound other than each other", h0 != h1);
    check("SUPER: DTN 0 / 100 both sound, bounded", s0.rms > 200 && s1.rms > 200 && s0.peak < 32767 && s1.peak < 32767);
    check("SUPER: bounded at the corners (note 108, DTN / MIX 127)", hi.rms > 200 && hi.peak < 32767);
    (void)h0;
    (void)h1;
}

/* a voice that last played SYNC / SUB (they keep their restart in s[3]), released or still sounding (a retrigger keeps
 * the filter and phases: not SUPER's), then plays SUPER: its seven phases are seeded apart all the same (in phase the
 * copies comb); the voice's own mark says so, not s[3] */
static void test_super_seed(void)
{
    track_t *t = &trk[0];
    uint32_t w, held, k, j, ok = 1, n = 0;
    for (w = 5; w <= 6u; w++)
        for (held = 0; held < 2u; held++) {
            const voice_t *nv = 0;
            fresh(0, 0);
            dry(t);
            t->p[P_VOICE] = V_POLY;
            t->p[P_FTYPE] = 0;
            t->p[P_E0] = (int16_t)w;
            t->p[P_E1] = 40;
            t->p[P_E2] = 64;
            t->p[P_ATK] = 0;
            t->p[P_SUS] = 127;
            trk_note_on(t, 60, 100);
            blocks(FS / 10u / CTL);
            if (!held) {
                trk_note_off(t, 60);
                blocks(FS / CTL);
            }
            t->p[P_E0] = 7;
            trk_note_on(t, 60, 100);
            for (k = 0; k < NVOICE; k++)
                if (t->v[k].gate && t->v[k].note == 60 && (!nv || t->v[k].age > nv->age))
                    nv = &t->v[k];
            if (nv) {
                uint32_t ph[7];
                n++;
                ph[0] = nv->ph[0]; ph[1] = nv->ph[1]; ph[2] = nv->ph[2];
                ph[3] = (uint32_t)nv->s[3]; ph[4] = (uint32_t)nv->s[4]; ph[5] = (uint32_t)nv->s[5];
                ph[6] = (uint32_t)nv->s[7];
                for (j = 0; j < 49u; j++)
                    if (j / 7u < j % 7u && ph[j / 7u] == ph[j % 7u])
                        ok = 0;
            }
        }
    check("SUPER after SYNC / SUB (released or sounding): the seven phases are seeded apart", n == 4u && ok);
}

/* a sounding SUPER voice keeps all seven phases through a retrigger: note_on must not seed again (s[6]'s mark),
 * and voice.c's keep mask (0x03) never rewrites the four phases in s[3..7] */
static void test_super_retrigger(void)
{
    track_t *t = &trk[0];
    uint32_t mode, ok = 1;
    for (mode = 0; mode < 3u; mode++) {              /* POLY (same note), MONO (new note), LEGATO (new note) */
        uint32_t ph[7], age;
        voice_t *v;
        fresh(0, 0);
        dry(t);
        t->p[P_VOICE] = mode == 0 ? V_POLY : mode == 1 ? V_MONO : V_LEGATO;
        t->p[P_E0] = 7;
        t->p[P_E1] = 100;
        t->p[P_E2] = 64;
        t->p[P_ATK] = 0;
        t->p[P_SUS] = 127;
        trk_note_on(t, 60, 100);
        blocks(FS / 10u / CTL);
        v = &t->v[0];                                /* one note: POLY's first free voice, or MONO's voice 0 */
        ph[0] = v->ph[0]; ph[1] = v->ph[1]; ph[2] = v->ph[2];
        ph[3] = (uint32_t)v->s[3]; ph[4] = (uint32_t)v->s[4];
        ph[5] = (uint32_t)v->s[5]; ph[6] = (uint32_t)v->s[7];
        age = v->age;
        ok &= v->s[6] == AS_SEEDED;
        trk_note_on(t, mode == 0 ? 60u : 64u, 90);   /* POLY: the same note retriggered; MONO / LEGATO: a new one */
        if (mode != 2u)                              /* POLY / MONO retrigger the voice; LEGATO does not */
            ok &= v->age != age;
        ok &= v->s[6] == AS_SEEDED;
        ok &= v->ph[0] == ph[0] && v->ph[1] == ph[1] && v->ph[2] == ph[2] &&
              (uint32_t)v->s[3] == ph[3] && (uint32_t)v->s[4] == ph[4] &&
              (uint32_t)v->s[5] == ph[5] && (uint32_t)v->s[7] == ph[6];
    }
    check("SUPER: a retrigger (POLY, MONO) / legato keeps all seven phases and the mark", ok);
}

static void test_esync(void)
{
    const param_desc_t *d = &TP[P_ESYNC];
    track_t *t = &trk[0];
    char b[8];
    const char *u;
    int32_t v, k, ok;

    check("SYNC: a track parameter OFF / ON, default OFF (as before), not recordable (a setting)",
          d->fmt == F_ENUM && d->min == 0 && d->max == 1 && d->def == 0 && str_eq(d->label, "SYNC") &&
          !motion_param(P_ESYNC));
    {
        uint32_t i, pg = 0;
        for (i = 0; i < NPAGES; i++)
            if (str_eq(PAGES[i].title, "ENV DEST"))
                pg = i + 1u;
        check("ENV DEST: FLT PIT SHP and SYNC (KNOB 4, the free one)", pg && PAGES[pg - 1u].id[3] == P_ESYNC &&
              PAGES[pg - 1u].id[0] == P_ED_FLT);
    }
    check("ESYNC OFF: the tables, whatever esync_k holds (a phrase bit for bit)",
          phrase_child(0, 0, 0) == phrase_child(0, 0, esync_garbage));
    /* the display and the knob */
    fresh(0, 0);
    param_format(track_desc(t, P_ATK), 70, b, &u);
    ok = track_desc(t, P_ATK) == &TP[P_ATK] && str_eq(u, "ms");
    t->p[P_ESYNC] = 1;
    ok &= track_desc(t, P_ATK) == &ESYNC_DESC[0] && track_desc(t, P_DEC) == &ESYNC_DESC[1] &&
          track_desc(t, P_REL) == &ESYNC_DESC[2] && track_desc(t, P_SUS) == &TP[P_SUS];
    for (k = 0; k < 3; k++) {
        static const uint8_t ID[3] = {P_ATK, P_DEC, P_REL};
        ok &= ESYNC_DESC[k].min == TP[ID[k]].min && ESYNC_DESC[k].max == TP[ID[k]].max && ESYNC_DESC[k].def == TP[ID[k]].def &&
              str_eq(ESYNC_DESC[k].label, TP[ID[k]].label);
    }
    check("ON: ATK DEC REL show note values (their range and defaults as ever), SUS as before; OFF: ms", ok);
    ok = 1;
    for (v = 0; v < 128; v++) {
        param_format(&ESYNC_DESC[1], v, b, &u);
        ok &= str_eq(b, ESYNC_NAMES[esync_idx(v)]) && !u[0];
    }
    ok &= str_eq(ESYNC_NAMES[0], "0") && str_eq(ESYNC_NAMES[1], "1/64T") && str_eq(ESYNC_NAMES[14], "1/4") &&
          str_eq(ESYNC_NAMES[24], "4BAR") && !ESYNC_NAMES[ESYNC_N];
    for (k = 1; k < (int32_t)ESYNC_N; k++)
        ok &= ESYNC_UNITS[k] > ESYNC_UNITS[k - 1] && str_len(ESYNC_NAMES[k]) <= 5u;
    check("25 values 0, 1/64T .. 4BAR (dotted D, triplets T), shortest first, the name the time esync_one takes", ok);
    v = 0;
    ok = 1;
    for (k = 1; k < (int32_t)ESYNC_N; k++) {
        v = param_turn(&ESYNC_DESC[0], v, 1);
        ok &= esync_idx(v) == (uint32_t)k;
    }
    ok &= param_turn(&ESYNC_DESC[0], v, 1) == v && esync_idx(param_turn(&ESYNC_DESC[0], 127, -1)) == ESYNC_N - 2u &&
          esync_idx(param_turn(&ESYNC_DESC[0], 64, 3)) == esync_idx(64) + 3u &&
          param_turn(&ESYNC_DESC[0], 0, -1) == 0;
    check("a knob detent a note value (up and down, clamped at 0 and 4BAR)", ok);
    /* the times at several tempi and on the external clock */
    {
        static const int16_t BPM[3] = {60, 120, 173};
        static const char *const NAMES[4] = {"1/16", "1/8T", "1/4", "1/2D"};
        uint32_t i, j;
        char where[32];
        ok = 1;
        for (i = 0; i < 3u; i++)
            for (j = 0; j < 4u; j++) {
                fresh(0, 0);
                song.g[G_BPM] = BPM[i];
                snprintf(where, sizeof where, "at %d BPM", BPM[i]);
                ok &= times_ok(NAMES[j], where);
            }
        fresh(0, 0);
        song.g[G_BPM] = 120;
        ok &= times_ok("1/64T", "at 120 BPM");
        song.g[G_BPM] = 90;
        ok &= times_ok("4BAR", "at 90 BPM");
        song.g[G_BPM] = 120;
        song.g[G_CLOCK] = 1;
        midi_clock.mode = 1;                              /* (events_block: no mode change, the tempo stays) */
        midi_beat_samples = FS * 60u / 100u;              /* the external clock at 100 BPM, as measured */
        ok &= times_ok("1/4", "ext 100 BPM");
        ok &= times_ok("1/16D", "ext 100 BPM");
        song.g[G_CLOCK] = 0;
        midi_beat_samples = 0;
        blocks(1);
        check("ENV SYNC: the times follow the tempo (BPM and the external clock): ATK 2 blocks, DEC REL 2 % (+ Q16)", ok);
    }
    {   /* "0": as ATK / DEC / REL 0 with SYNC OFF (the tables' shortest) */
        fresh(0, 0);
        check("value 0 (\"0\"): the tables' shortest, as OFF's 0",
              esync_one(beat_samples(), 0, 0) == ENV_LIN[0] && esync_one(beat_samples(), 0, 1) == ENV_EXP[0] &&
              esync_one(beat_samples(), 2, 2) == ENV_EXP[0]);
    }
}

/* ------------------------------------------------------------------ 3 --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
static double cost(uint32_t wave, uint32_t ft, uint32_t es)   /* ANALOG SOFT PAD POLY, 8 notes */
{
    static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    track_t *t = &trk[0];
    uint32_t i, rep;
    double best = 1e30;
    fresh(0, preset_by_name(0, "SOFT PAD"));
    t->p[P_VOICE] = V_POLY;
    t->p[P_SUS] = 127;
    t->p[P_E0] = (int16_t)wave;
    t->p[P_FTYPE] = (int16_t)ft;
    t->p[P_ESYNC] = (int16_t)es;
    for (i = 0; i < 8u; i++)
        trk_note_on(t, NOTES[i], 100);
    blocks(FS / 4u / CTL);
    for (rep = 0; rep < 3u; rep++) {
        uint64_t i0 = instr_now();
        double ipc;
        blocks(FS / 2u / CTL);
        ipc = (double)(instr_now() - i0) / (FS / 2u);
        best = ipc < best ? ipc : best;
    }
    return best;
}
static void test_cost(void)
{
    double lp, bp, hp, slp, shp, sp, e0, e1;
    if (!instr_now()) {
        printf("sound15: cost: no instruction counter on this host (proc_pid_rusage), skipped\n");
        return;
    }
    lp = cost(0, 0, 0);
    bp = cost(0, 1, 0);
    hp = cost(0, 2, 0);
    slp = cost(5, 0, 0);
    shp = cost(5, 2, 0);
    sp = cost(7, 0, 0);
    printf("sound15: cost: ANALOG 8 voices SAW LP %.0f, BP %.0f (%+.1f %%), HP %.0f (%+.1f %%); SYNC LP %.0f, HP %.0f "
           "(%+.1f %%); SUPER LP %.0f (%+.0f %%) instructions / sample\n", lp, bp, (bp - lp) * 100 / lp, hp,
           (hp - lp) * 100 / lp, slp, shp, (shp - slp) * 100 / slp, sp, (sp - lp) * 100 / lp);
    check("cost: BP / HP within 10 % of LP", bp < lp * 1.10 && hp < lp * 1.10 && shp < slp * 1.10);
    check("cost: SUPER 8 voices within 4x SAW", sp < lp * 4.0);
    e0 = cost(0, 0, 0);
    e1 = cost(0, 0, 1);
    printf("sound15: cost: ENV SYNC on an 8-voice part %.0f -> %.0f (%+.2f %%)\n", e0, e1, (e1 - e0) * 100 / e0);
    check("cost: ENV SYNC next to nothing (< 1 % of an 8-voice part)", e1 < e0 * 1.01);
}

/* ------------------------------------------------------------------ 4 --- */
static FILE *demo_open(const char *dir, const char *name, uint32_t frames)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    f = fopen(path, "wb");
    if (f)
        wav_hdr(f, frames);
    return f;
}
static void demo_blocks(FILE *f, uint32_t n)
{
    uint32_t i;
    while (n--) {
        blocks(1);
        for (i = 0; i < CTL; i++)
            wav_put(f, out_buf[2u * i], out_buf[2u * i + 1u]);
    }
}

static int demos(const char *dir)
{
    static const uint8_t LINE[16] = {36, 36, 48, 36, 39, 36, 46, 43, 36, 48, 36, 51, 36, 43, 46, 48};
    static const uint8_t PAD[4] = {48, 55, 60, 64}, PAD2[4] = {45, 52, 57, 64};
    uint32_t n = 0, k, j;
    FILE *f;
    track_t *t = &trk[0];
    /* BP: an ACID line, the band swept up and back over 8 s (two bars of 1/16 at 120 BPM, four times) */
    fresh(0, preset_by_name(0, "ACID"));
    t->p[P_FTYPE] = 1;
    t->p[P_E5] = 105;
    t->p[P_E6] = 50;                                      /* some drive: the band's edge */
    t->p[P_LEVEL] = 112;
    if ((f = demo_open(dir, "analog_bp", 9u * FS / CTL * CTL))) {
        uint32_t st = FS / 8u / CTL;                      /* a 1/16 at 120 BPM */
        for (k = 0; k < 64u; k++) {
            t->p[P_E4] = (int16_t)(55 + (k < 32u ? k : 63u - k) * 60 / 31);
            trk_note_on(t, LINE[k & 15u], (k & 3u) ? 90 : 120);
            demo_blocks(f, st / 2u);
            trk_note_off(t, LINE[k & 15u]);
            demo_blocks(f, st - st / 2u);
        }
        demo_blocks(f, 9u * FS / CTL - 64u * st);
        fclose(f);
        n++;
    }
    /* HP: STRINGS chords, the high-pass rising over each (thinning to air), then falling */
    fresh(0, preset_by_name(0, "STRINGS"));
    t->p[P_FTYPE] = 2;
    t->p[P_E5] = 50;
    if ((f = demo_open(dir, "analog_hp", 9u * FS / CTL * CTL))) {
        for (j = 0; j < 2u; j++) {
            const uint8_t *c = j ? PAD2 : PAD;
            for (k = 0; k < 4u; k++)
                trk_note_on(t, c[k], 100);
            for (k = 0; k < 4u * FS / CTL; k++) {
                uint32_t q = k * 2u < 4u * FS / CTL ? k : 4u * FS / CTL - k;
                t->p[P_E4] = (int16_t)(20 + q * 95u / (2u * FS / CTL));
                demo_blocks(f, 1);
            }
            for (k = 0; k < 4u; k++)
                trk_note_off(t, c[k]);
        }
        demo_blocks(f, FS / CTL);
        fclose(f);
        n++;
    }
    /* ENV SYNC: a plucked chord on every beat, ATK 1/8 REL 1/4 (SUS 50 %), two bars at 90 BPM then two at 150 */
    fresh(0, preset_by_name(0, "SAW LEAD"));
    t->p[P_VOICE] = V_POLY;
    t->p[P_ESYNC] = 1;
    t->p[P_ATK] = (int16_t)value_of("1/8");
    t->p[P_DEC] = (int16_t)value_of("1/8");
    t->p[P_REL] = (int16_t)value_of("1/4");
    t->p[P_SUS] = 64;
    t->p[P_REV] = 30;
    {
        uint32_t total = 0, b2 = 0;
        static const int16_t BPM[2] = {90, 150};
        for (j = 0; j < 2u; j++)
            total += 8u * (FS * 60u / (uint32_t)BPM[j] / CTL);
        total += 2u * FS / CTL;
        if ((f = demo_open(dir, "env_sync", total * CTL))) {
            for (j = 0; j < 2u; j++) {
                uint32_t beat = FS * 60u / (uint32_t)BPM[j] / CTL;
                song.g[G_BPM] = BPM[j];
                for (k = 0; k < 8u; k++, b2++) {
                    const uint8_t *c = (k & 2u) ? PAD2 : PAD;
                    uint32_t i;
                    for (i = 0; i < 4u; i++)
                        trk_note_on(t, c[i], 100);
                    demo_blocks(f, beat / 2u);
                    for (i = 0; i < 4u; i++)
                        trk_note_off(t, c[i]);
                    demo_blocks(f, beat - beat / 2u);
                }
            }
            demo_blocks(f, 2u * FS / CTL);
            fclose(f);
            n++;
        }
    }
    printf("sound15: %u demos in %s (analog_bp, analog_hp, env_sync)\n", n, dir);
    return n == 3u;
}

int main(int argc, char **argv)
{
    test_filter();
    test_super();
    test_super_seed();
    test_super_retrigger();
    test_osc2();
    test_esync();
    test_cost();
    if (argc > 1)
        check("demos written", demos(argv[1]));
    (void)eng_by_name;
    printf("sound15: %s\n", bad ? "FAILED" : "all ok");
    return bad ? 1 : 0;
}
