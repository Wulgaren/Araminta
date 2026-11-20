/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Combinatorial laws suite: separate axis tables (matrix covering, engines, FX, LFO DEST, locks,
 * punch) plus stacked stress; host cost % of half + click; structural asserts.
 *
 *   build/host/laws_test
 */
#include <stdint.h>
/* empty USR1..3 sample/wavetable slots (wt_test.c): a WT track must not read unmapped XIP */
static uint8_t host_smp_slots[3][0x14000] __attribute__((aligned(4)));
#define SMP_USER_XIP(k) (host_smp_slots[(k)])
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
#include "../firmware/src/project.c"

#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/syscall.h>
#endif
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define HALF_US (HALF_FRAMES * 1000000u / FS)          /* 2902 us at 44.1 kHz */
#define IPC_TO_PCT 0.017                               /* perform_test: 100 ipc ≈ 1.7 % device */
#define SETTLE_BLK (FS / (10u * CTL))                  /* 0.1 s */
#define MEAS_BLK (FS / (4u * CTL))                     /* 0.25 s */

static int bad;
static int check(const char *what, int ok)
{
    printf("laws: %-72s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
    return ok ? 0 : 1;
}

/* ---------------------------------------------- instruction / cycle count --- */
static int ipc_kind;                                   /* 0 none, 1 instr, 2 cycles */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri)) {
        ipc_kind = 2;
        return ri.ri_cycles;
    }
#endif
#ifdef __linux__
    {
        static int fd = -2;
        if (fd == -2) {
            struct perf_event_attr pe;
            memset(&pe, 0, sizeof pe);
            pe.type = PERF_TYPE_HARDWARE;
            pe.size = sizeof pe;
            pe.config = PERF_COUNT_HW_INSTRUCTIONS;
            pe.disabled = 0;
            pe.exclude_kernel = 1;
            pe.exclude_hv = 1;
            fd = (int)syscall(__NR_perf_event_open, &pe, 0, -1, -1, 0);
        }
        if (fd >= 0) {
            uint64_t v = 0;
            if (read(fd, &v, sizeof v) == (ssize_t)sizeof v) {
                ipc_kind = 1;
                return v;
            }
        }
    }
#endif
#if defined(__x86_64__) || defined(__i386__)
    {
        uint32_t lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        ipc_kind = 2;
        return ((uint64_t)hi << 32) | lo;
    }
#else
    return 0;
#endif
}

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define LAWS_COST_MEANINGFUL 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
    __has_feature(undefined_behavior_sanitizer)
#define LAWS_COST_MEANINGFUL 0
#endif
#endif
#ifndef LAWS_COST_MEANINGFUL
#define LAWS_COST_MEANINGFUL 1
#endif

static double pct_from_ipc(double ipc)
{
    return ipc * (ipc_kind == 1 ? 1.0 : 2.0) * IPC_TO_PCT;
}

static int32_t max_d2(const int32_t *o, int32_t prev[2][2])
{
    uint32_t i, c;
    int32_t mx = 0;
    for (i = 0; i < CTL; i++)
        for (c = 0; c < 2u; c++) {
            int32_t x = o[2u * i + c], d = x - 2 * prev[c][0] + prev[c][1];
            if (d < 0)
                d = -d;
            if (d > mx)
                mx = d;
            prev[c][1] = prev[c][0];
            prev[c][0] = x;
        }
    return mx;
}

typedef struct { double ipc, pct; int32_t click; } laws_run_t;

static int32_t laws_meas_pk;                           /* peak |sample| of the last measure window */

static laws_run_t laws_measure(void (*setup)(void), void (*engage)(void))
{
    laws_run_t r;
    int32_t o[2 * CTL], prev[2][2] = {{0, 0}, {0, 0}};
    uint64_t acc_i = 0, i0;
    uint32_t k, nblk = SETTLE_BLK + MEAS_BLK;
    int32_t mx = 0, pk = 0;
    memset(&r, 0, sizeof r);
    laws_meas_pk = 0;
    if (setup)
        setup();
    for (k = 0; k < nblk; k++) {
        if (k == SETTLE_BLK && engage)
            engage();
        if (k >= SETTLE_BLK) {
            i0 = instr_now();
            mix_block(o, CTL);
            if (i0)
                acc_i += instr_now() - i0;
            {
                int32_t d2 = max_d2(o, prev);
                uint32_t i;
                if (d2 > mx)
                    mx = d2;
                for (i = 0; i < 2u * CTL; i++) {
                    int32_t a = o[i] < 0 ? -o[i] : o[i];
                    if (a > pk)
                        pk = a;
                }
            }
        } else {
            mix_block(o, CTL);
            (void)max_d2(o, prev);                 /* warm the second-difference state */
        }
    }
    r.click = mx;
    laws_meas_pk = pk;
    r.ipc = (acc_i && instr_now()) ? (double)acc_i / (double)(MEAS_BLK * CTL) : 0;
    r.pct = r.ipc ? pct_from_ipc(r.ipc) : 0;
    return r;
}

static void laws_assert_audio(const char *label, const laws_run_t *r, int32_t click_lim)
{
    char what[160];
    if (click_lim > 0) {
        snprintf(what, sizeof what, "%s: click %d <= %d", label, (int)r->click, (int)click_lim);
        check(what, r->click <= click_lim);
    }
    if (LAWS_COST_MEANINGFUL && ipc_kind == 1 && r->ipc) {
        snprintf(what, sizeof what, "%s: cost %.1f %% of half", label, r->pct);
        check(what, r->pct < 100.0);
    } else {
        printf("laws: %-72s survey (%.1f %% %s)\n", label, r->pct,
               ipc_kind == 1 ? "instr" : ipc_kind == 2 ? "cycles" : "none");
    }
}

/* ---------------------------------------------------------------- fixtures --- */
static void slot(track_t *t, uint32_t k, int32_t s, int32_t d, int32_t a)
{
    MOD_ROW(t, k)[MF_SRC] = (int16_t)s;
    MOD_ROW(t, k)[MF_DST] = (int16_t)d;
    MOD_ROW(t, k)[MF_AMT] = (int16_t)a;
}

static void lfo_at(track_t *t, int32_t v)
{
    t->lfo_val = v;
    t->lfo_fade = 32767;
}

static void clear_matrix(track_t *t)
{
    uint32_t k;
    for (k = 0; k < NMSLOT; k++)
        slot(t, k, MS_OFF, MD_OFF, 0);
}

static uint32_t soft_pad(void)
{
    uint32_t i;
    for (i = 0; i < ENGINES[ENGI_ANALOG]->npresets; i++)
        if (!strcmp(ENGINES[ENGI_ANALOG]->presets[i].name, "SOFT PAD"))
            return i;
    return 0;
}

/* Light ANALOG pad: a few inserts and sends audible, cheap enough that matrix windows stay under half. */
static void laws_light_mix(void)
{
    track_t *t = &trk[0];
    uint32_t s, k;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(rev_tank, 0, sizeof rev_tank);
    R = &rev_tank[0];
    memset(&fx, 0, sizeof fx);
    lim_env = LIM_T;
    memset(trk, 0, sizeof trk);
    MOD_W_CLEAR();
    host_tracks_init();
    for (k = 0; k < NPART; k++)
        host_preset(&trk[k], k ? 0u : ENGI_ANALOG, k ? 0u : soft_pad());
    song.sel = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_E0] = 4;
    t->p[P_E0 + 2] = 40;
    t->p[P_LD_AMP] = 30;
    t->p[P_LD_PIT] = 16;
    t->p[P_LRATE] = 48;
    for (s = 0; s < INS_N; s++)
        ins_send_set(t, s, 0);
    t->p[P_ED_FX] = 12;
    t->p[P_IDMIX] = 24;
    t->p[P_IFMIX] = 24;
    t->p[P_ICMIX] = 16;
    t->p[P_IPMIX] = 16;
    t->p[P_ILMIX] = 16;
    t->p[P_IHMIX] = 16;
    t->p[P_DLY] = 24;
    t->p[P_REV] = 24;
    t->p[P_CHRD] = CH_DIA3;
    t->p[P_VOIC] = VC_CLOSE;
    t->p[P_LEVEL] = 80;
}

static void laws_hold_on(track_t *t)
{
    trk_note_on(t, 48, 90);
    trk_note_on(t, 55, 80);
    trk_note_on(t, 60, 100);
}

static void laws_hold_notes(void)
{
    laws_hold_on(&trk[0]);
}

static int crush_dest(int d)
{
    return d == MD_BITS || d == MD_CRAT || d == MD_CLPF || d == MD_CMIX;
}

static int window_has_crush(const int *dests, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (crush_dest(dests[i]))
            return 1;
    return 0;
}

/* Structural: slots stay configured; for DEPTH / voice / param dests, something moved and bases restore. */
static void laws_struct_window(track_t *t, const int *dests, int n)
{
    const int16_t *rows[NMSLOT];
    char what[128];
    int i;
    uint32_t nact;
    int16_t snap[NMSLOT];
    uint32_t ids[NMSLOT];
    int nsnap = 0;

    nact = mod_active(t, rows, 0);
    snprintf(what, sizeof what, "matrix: %d dests active", n);
    check(what, nact == (uint32_t)n);
    for (i = 0; i < n; i++) {
        const int16_t *q = MOD_ROW(t, (uint32_t)i);
        int want_src = (dests[i] == MD_AMP || dests[i] == MD_VEL) ? MS_KEY : MS_LFO;
        int want_amt = dests[i] == MD_CHRD ? 20 : 63;
        snprintf(what, sizeof what, "matrix: slot %d DST=%d configured", i, dests[i]);
        check(what, q[MF_SRC] == want_src && q[MF_DST] == dests[i] && q[MF_AMT] == want_amt);
    }

    for (i = 0; i < n; i++) {
        int d = dests[i];
        if (d == MD_VEL || d <= MD_AMP || d == MD_DEPTH)
            continue;
        ids[nsnap] = mod_param((uint32_t)d);
        if (ids[nsnap] < P_COUNT)
            snap[nsnap++] = t->p[ids[nsnap]];
    }

    lfo_at(t, 32767);
    mod_begin(t);
    for (i = 0; i < n; i++) {
        int d = dests[i];
        if (d == MD_VEL)
            continue;
        if (d == MD_DEPTH) {
            snprintf(what, sizeof what, "matrix: DEPTH ldep moved");
            check(what, MOD(t).on && MOD(t).ldep < 32767);
        } else if (d == MD_PITCH) {
            check("matrix: PITCH pit moved", MOD(t).on && MOD(t).pit != 0);
        } else if (d == MD_CUT) {
            check("matrix: CUT cut moved", MOD(t).on && MOD(t).cut != 0);
        } else if (d == MD_SHP) {
            check("matrix: SHP shp moved", MOD(t).on && MOD(t).shp != 0);
        } else if (d == MD_AMP) {
            check("matrix: AMP gain path on", MOD(t).on && MOD(t).amp);
        }
    }
    {
        int moved = 0;
        for (i = 0; i < nsnap; i++)
            if (t->p[ids[i]] != snap[i])
                moved = 1;
        if (nsnap)
            check("matrix: a param dest moved under mod_begin", moved || MOD(t).nk > 0);
    }
    mod_end(t);
    for (i = 0; i < nsnap; i++) {
        snprintf(what, sizeof what, "matrix: param %u restored after mod_end", ids[i]);
        check(what, t->p[ids[i]] == snap[i]);
    }
}

static int g_win_dests[NMSLOT];
static int g_win_n;

static void laws_engage_lfo(void)
{
    lfo_at(&trk[0], 32767);
}

static void laws_setup_window(void)
{
    track_t *t = &trk[0];
    int i;
    laws_light_mix();
    clear_matrix(t);
    for (i = 0; i < g_win_n; i++) {
        int d = g_win_dests[i];
        int src = (d == MD_AMP || d == MD_VEL) ? MS_KEY : MS_LFO;
        int amt = d == MD_CHRD ? 20 : 63;
        slot(t, (uint32_t)i, src, d, amt);
    }
    laws_hold_notes();
    lfo_at(t, 32767);
}

static void laws_setup_dry(void)
{
    laws_light_mix();
    clear_matrix(&trk[0]);
    laws_hold_notes();
}

static int law_matrix(void)
{
    laws_run_t dry, wet;
    int base, covered = 0, d;
    int32_t lim;
    char label[96];

    (void)instr_now();
    dry = laws_measure(laws_setup_dry, 0);
    lim = 3 * dry.click;
    if (laws_meas_pk / 16 > lim)
        lim = laws_meas_pk / 16;
    if (lim < 1000)
        lim = 1000;
    printf("laws: matrix dry click=%d pk=%d lim=%d ipc=%.1f%%\n", (int)dry.click, (int)laws_meas_pk,
           (int)lim, dry.pct);

    for (base = 1; base < MD_N; base += (int)NMSLOT) {
        int n = MD_N - base;
        int skip_click;
        if (n > (int)NMSLOT)
            n = (int)NMSLOT;
        g_win_n = n;
        for (d = 0; d < n; d++)
            g_win_dests[d] = base + d;

        laws_setup_window();
        laws_struct_window(&trk[0], g_win_dests, n);

        skip_click = window_has_crush(g_win_dests, n);
        wet = laws_measure(laws_setup_window, laws_engage_lfo);
        snprintf(label, sizeof label, "matrix [%d..%d]", base, base + n - 1);
        laws_assert_audio(label, &wet, skip_click ? 0 : lim);
        covered += n;
    }

    printf("laws: matrix covered %d destinations (MD_N-1=%d)\n", covered, MD_N - 1);
    check("matrix: covered every destination", covered == MD_N - 1);
    return bad;
}

/* fill out[] with up to 5 corners; return count */
static int corners(const param_desc_t *d, int16_t out[5])
{
    int16_t cand[5];
    int nc = 0, n = 0, i, j;
    int32_t min = d->min, max = d->max, mid = (min + max) / 2;
    cand[nc++] = (int16_t)min;
    cand[nc++] = (int16_t)mid;
    cand[nc++] = (int16_t)max;
    if (max - min >= 2)
        cand[nc++] = (int16_t)(min + 1);
    if (max - min >= 3)
        cand[nc++] = (int16_t)(max - 1);
    for (i = 0; i < nc; i++) {
        for (j = 0; j < n; j++)
            if (out[j] == cand[i])
                break;
        if (j == n)
            out[n++] = cand[i];
    }
    return n;
}

static int32_t laws_click_lim(const laws_run_t *dry)
{
    int32_t lim = 3 * dry->click;
    if (laws_meas_pk / 16 > lim)
        lim = laws_meas_pk / 16;
    if (lim < 1000)
        lim = 1000;
    return lim;
}

/* ---------------------------------------------------------------- engines axis --- */
static uint32_t g_eng_e, g_eng_ti, g_eng_id;
static int16_t g_eng_val;
static int g_eng_set;

static void laws_setup_eng(void)
{
    track_t *t;
    uint32_t k;
    laws_light_mix();
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_LEVEL] = 0;
    t = &trk[g_eng_ti];
    host_preset(t, g_eng_e, 0);
    t->p[P_LEVEL] = 80;
    clear_matrix(t);
    if (g_eng_set)
        t->p[g_eng_id] = g_eng_val;
    laws_hold_on(t);
}

static int law_engines(void)
{
    uint32_t i, k;
    char what[160], label[96];

    for (i = 0; i < NENG_SHOWN; i++) {
        uint32_t e = ENGINE_ORDER[i], ti = i % NTRK;
        laws_run_t dry, wet;
        int32_t lim;
        track_t *t;

        g_eng_e = e;
        g_eng_ti = ti;
        g_eng_set = 0;
        dry = laws_measure(laws_setup_eng, 0);
        lim = laws_click_lim(&dry);
        if (lim < 20000)
            lim = 20000;                           /* corner knobs click more than the dry default */
        printf("laws: engine %u (%s) dry click=%d lim=%d\n", e, ENGINES[e]->name, (int)dry.click,
               (int)lim);

        t = &trk[ti];
        for (k = 0; k < 8u; k++) {
            uint32_t id = P_E0 + k;
            const param_desc_t *d = param_desc_of(e, id);
            int16_t cs[5];
            int nc, ci;

            if (!param_live(d))
                continue;
            nc = corners(d, cs);
            for (ci = 0; ci < nc; ci++) {
                int32_t fit = param_fit(d, cs[ci]);
                laws_setup_eng();
                t->p[id] = (int16_t)fit;
                snprintf(what, sizeof what, "eng %u E%u corner %d fit=%d in range", e, k, (int)cs[ci],
                         (int)fit);
                check(what, t->p[id] == (int16_t)fit && fit >= d->min && fit <= d->max);
            }
            /* audio only at min and max */
            for (ci = 0; ci < nc; ci++) {
                if (cs[ci] != d->min && cs[ci] != d->max)
                    continue;
                g_eng_set = 1;
                g_eng_id = id;
                g_eng_val = (int16_t)param_fit(d, cs[ci]);
                wet = laws_measure(laws_setup_eng, 0);
                snprintf(label, sizeof label, "eng %u E%u=%d", e, k, (int)g_eng_val);
                laws_assert_audio(label, &wet, lim);
            }
            g_eng_set = 0;
        }
    }
    return bad;
}

/* ---------------------------------------------------------------- FX stages axis --- */
enum { FXK_TR = 0, FXK_G = 1 };
typedef struct {
    const char *name;
    uint8_t st;
    uint8_t n;
    uint8_t skip_click;
    struct { uint16_t id; uint8_t kind; } knob[4];
} fx_law_stage_t;

static const fx_law_stage_t FX_LAW[] = {
    {"DISTORT", INS_DIST, 4, 0,
     {{P_IDMODE, FXK_TR}, {P_DIST, FXK_TR}, {P_ED_FX, FXK_TR}, {P_IDMIX, FXK_TR}}},
    {"FILTER", INS_FILTER, 4, 0,
     {{P_IFTYPE, FXK_TR}, {P_IFCUT, FXK_TR}, {P_IFRES, FXK_TR}, {P_IFMIX, FXK_TR}}},
    {"CRUSH", INS_CRUSH, 4, 1,
     {{P_ICBITS, FXK_TR}, {P_ICRATE, FXK_TR}, {P_ICLPF, FXK_TR}, {P_ICMIX, FXK_TR}}},
    {"PHASER", INS_PHASR, 4, 0,
     {{P_IPRATE, FXK_TR}, {P_IPDEP, FXK_TR}, {P_IPFBK, FXK_TR}, {P_IPMIX, FXK_TR}}},
    {"FLANGER", INS_FLANG, 3, 0,
     {{P_ILRATE, FXK_TR}, {P_ILDEP, FXK_TR}, {P_ILMIX, FXK_TR}}},
    {"CHORUS", INS_CHOR, 3, 0,
     {{P_IHRATE, FXK_TR}, {P_IHDEP, FXK_TR}, {P_IHMIX, FXK_TR}}},
    {"DELAY", INS_DLY, 4, 0,
     {{G_DTIME, FXK_G}, {G_DFDBK, FXK_G}, {G_DCOLOR, FXK_G}, {P_DLY, FXK_TR}}},
    {"REVERB", INS_REV, 4, 0,
     {{G_RTYPE, FXK_G}, {G_RSIZE, FXK_G}, {G_RDAMP, FXK_G}, {P_REV, FXK_TR}}},
    {"SLICER", INS_SLCR, 4, 0,
     {{P_SLCR, FXK_TR}, {P_SLPAT, FXK_TR}, {P_SLRATE, FXK_TR}, {P_SLDEPTH, FXK_TR}}},
    {"TAPE", INS_TAPE, 4, 0,
     {{G_TSPEED, FXK_G}, {G_TWOW, FXK_G}, {G_TDROP, FXK_G}, {G_TTEMPO, FXK_G}}},
};

static const uint8_t FX_MIXID[INS_N] = {P_IDMIX, P_IFMIX, P_ICMIX, P_IPMIX, P_ILMIX, P_IHMIX};
static uint32_t g_fx_si;

static void laws_fx_stage_apply(const fx_law_stage_t *S)
{
    track_t *t = &trk[0];
    uint32_t s;

    laws_light_mix();
    clear_matrix(t);
    for (s = 0; s < INS_N; s++) {
        ins_send_set(t, s, 0);
        t->p[FX_MIXID[s]] = 0;
    }
    t->p[P_DLY] = 0;
    t->p[P_REV] = 0;
    t->p[P_SLDEPTH] = 0;
    song.g[G_TMIX] = 0;
    song.g[G_TAPEMODE] = TAPE_SEND;
    bus_own_set(0, -1);
    bus_own_set(1, -1);
    bus_own_set(2, -1);

    if (S->st < INS_N)
        t->p[FX_MIXID[S->st]] = 64;
    else if (S->st == INS_DLY) {
        bus_own_set(0, 0);
        t->p[P_DLY] = 64;
    } else if (S->st == INS_REV) {
        bus_own_set(1, 0);
        t->p[P_REV] = 64;
    } else if (S->st == INS_SLCR) {
        ins_send_set(t, INS_SLCR, 0);
        bus_own_set(2, 0);
        t->p[P_SLDEPTH] = 64;
        t->p[P_SLCR] = SL_GATE;
        slicer_start();
    } else if (S->st == INS_TAPE) {
        song.g[G_TAPEMODE] = TAPE_CH1;
        song.g[G_TMIX] = 64;
        song.g[G_TTEMPO] = 0;
    }
    laws_hold_notes();
}

static void laws_setup_fx(void)
{
    laws_fx_stage_apply(&FX_LAW[g_fx_si]);
    if (FX_LAW[g_fx_si].st < INS_N)
        trk[0].p[FX_MIXID[FX_LAW[g_fx_si].st]] = 96;
    else if (FX_LAW[g_fx_si].st == INS_DLY)
        trk[0].p[P_DLY] = 96;
    else if (FX_LAW[g_fx_si].st == INS_REV)
        trk[0].p[P_REV] = 96;
    else if (FX_LAW[g_fx_si].st == INS_SLCR)
        trk[0].p[P_SLDEPTH] = 96;
    else if (FX_LAW[g_fx_si].st == INS_TAPE)
        song.g[G_TMIX] = 96;
}

static int law_fx(void)
{
    laws_run_t dry, wet;
    int32_t lim;
    uint32_t si, ki;
    char what[160], label[96];

    dry = laws_measure(laws_setup_dry, 0);
    lim = laws_click_lim(&dry);

    for (si = 0; si < NELEM(FX_LAW); si++) {
        const fx_law_stage_t *S = &FX_LAW[si];
        laws_fx_stage_apply(S);
        for (ki = 0; ki < S->n; ki++) {
            uint16_t id = S->knob[ki].id;
            const param_desc_t *d = S->knob[ki].kind == FXK_G ? &GP[id] : &TP[id];
            int16_t cs[5], *slot;
            int nc, ci;

            if (!param_live(d))
                continue;
            slot = S->knob[ki].kind == FXK_G ? &song.g[id] : &trk[0].p[id];
            nc = corners(d, cs);
            for (ci = 0; ci < nc; ci++) {
                int32_t fit = param_fit(d, cs[ci]);
                *slot = (int16_t)fit;
                snprintf(what, sizeof what, "fx %s knob %u corner %d fit=%d", S->name, ki, (int)cs[ci],
                         (int)fit);
                check(what, *slot == (int16_t)fit && fit >= d->min && fit <= d->max);
            }
        }
        g_fx_si = si;
        wet = laws_measure(laws_setup_fx, 0);
        snprintf(label, sizeof label, "fx %s", S->name);
        laws_assert_audio(label, &wet, S->skip_click ? 0 : lim);
    }
    return bad;
}

/* ---------------------------------------------------------------- LFO DEST axis --- */
static uint32_t g_ld_id;
static int16_t g_ld_val;

static void laws_setup_lfo_dest(void)
{
    track_t *t = &trk[0];
    laws_light_mix();
    clear_matrix(t);
    t->p[P_LD_PIT] = 0;
    t->p[P_LD_FLT] = 0;
    t->p[P_LD_SHP] = 0;
    t->p[P_LD_AMP] = 0;
    t->p[g_ld_id] = g_ld_val;
    laws_hold_notes();
    lfo_at(t, 32767);
}

static int32_t laws_block_peak(void)
{
    int32_t o[2 * CTL], pk = 0;
    uint32_t i;
    mix_block(o, CTL);
    for (i = 0; i < 2u * CTL; i++) {
        int32_t a = o[i] < 0 ? -o[i] : o[i];
        if (a > pk)
            pk = a;
    }
    return pk;
}

static int law_lfo_dest(void)
{
    static const uint32_t IDS[] = {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP};
    laws_run_t dry, wet;
    int32_t lim;
    uint32_t n;
    char what[160], label[96];

    dry = laws_measure(laws_setup_dry, 0);
    lim = laws_click_lim(&dry);
    if (lim < 20000)
        lim = 20000;

    for (n = 0; n < NELEM(IDS); n++) {
        uint32_t id = IDS[n];
        const param_desc_t *d = &TP[id];
        int16_t cs[5];
        int nc, ci;

        nc = corners(d, cs);
        for (ci = 0; ci < nc; ci++) {
            int32_t fit = param_fit(d, cs[ci]);
            track_t *t = &trk[0];
            laws_light_mix();
            clear_matrix(t);
            t->p[P_LD_PIT] = t->p[P_LD_FLT] = t->p[P_LD_SHP] = t->p[P_LD_AMP] = 0;
            t->p[id] = (int16_t)fit;
            laws_hold_notes();
            lfo_at(t, 32767);
            snprintf(what, sizeof what, "lfo dest %u corner %d stored", id, (int)cs[ci]);
            check(what, t->p[id] == (int16_t)fit && fit >= d->min && fit <= d->max);
            if (id != P_LD_AMP && fit != 0) {
                int32_t pk = laws_block_peak();
                snprintf(what, sizeof what, "lfo dest %u corner %d moves audio", id, (int)cs[ci]);
                check(what, pk > 0);
            }
        }
        for (ci = 0; ci < nc; ci++) {
            if (cs[ci] != d->min && cs[ci] != d->max)
                continue;
            g_ld_id = id;
            g_ld_val = (int16_t)param_fit(d, cs[ci]);
            wet = laws_measure(laws_setup_lfo_dest, 0);
            snprintf(label, sizeof label, "lfo dest %u=%d", id, (int)g_ld_val);
            laws_assert_audio(label, &wet, lim);
        }
    }
    return bad;
}

/* ---------------------------------------------------------------- param locks axis --- */
static void laws_motion_wipe(void)
{
    memset(&motion, 0, sizeof motion);
    memset(motion_active, 0, sizeof motion_active);
    memset(motion_locked, 0, sizeof motion_locked);
}

static int16_t g_lock_p[8];
static const uint16_t LOCK_AUDIO_IDS[8] = {
    P_ATK, P_PAN, P_DIST, P_DLY, P_REV, P_IDMIX, P_IFCUT, P_ED_FX
};

static void laws_setup_locks_audio(void)
{
    track_t *t = &trk[0];
    uint32_t i;
    laws_light_mix();
    clear_matrix(t);
    for (i = 0; i < NELEM(LOCK_AUDIO_IDS); i++)
        if (LOCK_AUDIO_IDS[i] < P_COUNT)
            t->p[LOCK_AUDIO_IDS[i]] = g_lock_p[i];
    laws_hold_notes();
}

static int law_locks(void)
{
    track_t *t;
    uint32_t id;
    int16_t v;
    char what[160], label[96];
    laws_run_t dry, wet;
    int32_t lim;
    static const uint32_t MFX_IDS[] = {
        MFX_TIME, MFX_FB, MFX_COLOR, MFX_SIZE, MFX_DAMP, MFX_TSPEED, MFX_TWOW
    };

    laws_light_mix();
    t = &trk[0];
    clear_matrix(t);
    laws_motion_wipe();

    for (id = 0; id < P_COUNT; id++) {
        const param_desc_t *d;
        int16_t cs[5];
        int nc, ci;

        if (!motion_param(id))
            continue;
        d = motion_target_desc(t, id);
        if (!d)
            continue;
        nc = corners(d, cs);
        for (ci = 0; ci < nc; ci++) {
            int32_t fit;
            if (cs[ci] != d->min && cs[ci] != d->max)
                continue;
            fit = param_fit(d, cs[ci]);
            if (!MFX_ALL(id) && (fit < -64 || fit > 127))
                continue;
            laws_motion_wipe();
            snprintf(what, sizeof what, "lock id %u corner %d set", id, (int)fit);
            check(what, motion_set_lock(t, 1, id, (int16_t)fit) == 0);
            snprintf(what, sizeof what, "lock id %u corner %d get", id, (int)fit);
            check(what, motion_lock_get(t, 1, id, &v) && v == (int16_t)fit);
        }
        laws_motion_wipe();
    }

    laws_motion_wipe();
    check("lock PITCH on ANALOG", motion_set_lock(t, 1, MSEQ_PITCH, 5) == 0 &&
          motion_lock_get(t, 1, MSEQ_PITCH, &v) && v == 5);
    laws_motion_wipe();
    host_drums(&trk[1]);
    check("lock PITCH refused on kit", motion_set_lock(&trk[1], 1, MSEQ_PITCH, 5) != 0 &&
          !motion_lock_get(&trk[1], 1, MSEQ_PITCH, &v));
    laws_motion_wipe();
    host_preset(&trk[1], ENGI_ANALOG, 0);              /* restore companion track */

    for (id = 0; id < NELEM(MFX_IDS); id++) {
        uint32_t mid = MFX_IDS[id];
        const param_desc_t *d;
        int16_t fit_lo, fit_hi;

        if (!motion_param(mid))
            continue;
        d = motion_target_desc(t, mid);
        if (!d)
            continue;
        fit_lo = (int16_t)param_fit(d, d->min);
        fit_hi = (int16_t)param_fit(d, d->max);
        laws_motion_wipe();
        snprintf(what, sizeof what, "MFX %u min lock", mid);
        check(what, motion_set_lock(t, 1, mid, fit_lo) == 0 && motion_lock_get(t, 1, mid, &v) &&
              v == fit_lo);
        laws_motion_wipe();
        snprintf(what, sizeof what, "MFX %u max lock", mid);
        check(what, motion_set_lock(t, 1, mid, fit_hi) == 0 && motion_lock_get(t, 1, mid, &v) &&
              v == fit_hi);
        laws_motion_wipe();
    }

    {
        static const int16_t PICK[8] = {10, 20, 40, 50, 60, 70, 80, 30};
        uint32_t i;
        laws_light_mix();
        clear_matrix(t);
        laws_motion_wipe();
        for (i = 0; i < NELEM(LOCK_AUDIO_IDS); i++) {
            uint32_t lid = LOCK_AUDIO_IDS[i];
            const param_desc_t *d = motion_target_desc(t, lid);
            int16_t fit = d ? (int16_t)param_fit(d, PICK[i]) : PICK[i];
            g_lock_p[i] = fit;
            check("audio lock install", motion_set_lock(t, 1, lid, fit) == 0);
        }
        dry = laws_measure(laws_setup_dry, 0);
        lim = laws_click_lim(&dry);
        if (lim < 20000)
            lim = 20000;
        wet = laws_measure(laws_setup_locks_audio, 0);
        snprintf(label, sizeof label, "locks audio (~8)");
        laws_assert_audio(label, &wet, lim);
        laws_motion_wipe();
    }
    return bad;
}

/* ---------------------------------------------------------------- punch-in axis --- */
static void laws_perf_clear_all(void)
{
    uint32_t k;
    for (k = 0; k < PF_N; k++)
        perf_press(k, 0);
    perf_latched = 0;
    perf_latch_on = 0;
    perf_kill = 0;
    perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
}

static void laws_punch_start(void)
{
    laws_perf_clear_all();
    memset(&pf, 0, sizeof pf);
    pf.src = pf.next = PF_N;
    pf.lc = PF_TOP;
    pf.mg[0] = pf.mg[1] = pf.mg[2] = pf.mg[3] = 32768;
    sl_lent = 0;
    transport_req = 1;
    song.playing = 1;
    perf_start();
}

static int laws_pf_engaged(void)
{
    return perf_act || pf.mode != BM_NONE || pf.w || pf.la || pf.ha || pf.cw || pf.fw || pf.pw ||
           pf.thr || pf.mute || perf_latched;
}

static uint32_t g_punch_e;

static void laws_setup_punch(void)
{
    laws_light_mix();
    clear_matrix(&trk[0]);
    laws_hold_notes();
    laws_punch_start();
}

static void laws_engage_punch(void)
{
    perf_press(g_punch_e, 1);
}

static int laws_punch_crush(uint32_t e)
{
    return e == PF_GLT;                                /* GLT segments can crush; skip click */
}

/* CLR + every key up, then ~0.5 s of mix so buffer / THROW tails finish (punch_crackle CHURN_END). */
static void laws_punch_release(void)
{
    int32_t o[2 * CTL];
    uint32_t e, k, nblk = FS / (2u * CTL);

    perf_latch_on = 1;
    perf_press(PF_CLR, 1);
    perf_press(PF_CLR, 0);
    perf_latch_on = 0;
    for (e = 0; e < PF_N; e++)
        perf_press(e, 0);
    perf_kill = 0;
    for (k = 0; k < nblk; k++)
        mix_block(o, CTL);
}

static int laws_punch_idle(void)
{
    return !laws_pf_engaged() && !pf.td && !perf_held && !perf_kill && !pfx_any && !pfx_qon;
}

static int law_punch(void)
{
    uint8_t seen[PF_N];
    uint32_t ids[PF_N], n = 0, i;
    laws_run_t dry, wet;
    int32_t lim;
    char what[160], label[96];

    memset(seen, 0, sizeof seen);
    for (i = 0; i < PF_KEYS; i++) {
        uint32_t e = PF_DEF[i];
        if (e < PF_N && !seen[e]) {
            seen[e] = 1;
            ids[n++] = e;
        }
        e = PF2_DEF[i];
        if (e < PF_N && !seen[e]) {
            seen[e] = 1;
            ids[n++] = e;
        }
    }

    dry = laws_measure(laws_setup_dry, 0);
    lim = laws_click_lim(&dry);
    if (lim < 20000)
        lim = 20000;

    for (i = 0; i < n; i++) {
        uint32_t e = ids[i];

        g_punch_e = e;
        laws_perf_clear_all();
        wet = laws_measure(laws_setup_punch, laws_engage_punch);
        laws_punch_release();
        snprintf(what, sizeof what, "punch PF %u stuck clear", e);
        check(what, laws_punch_idle());
        snprintf(label, sizeof label, "punch PF %u", e);
        laws_assert_audio(label, &wet, laws_punch_crush(e) ? 0 : lim);
        laws_perf_clear_all();
        transport_req = 2;
        song.playing = 0;
    }
    return bad;
}

/* ---------------------------------------------------------------- stacked stress --- */
static void laws_synth_heavy(void)
{
    static const uint8_t PARTS[NPART][3] = {{12, 0, 8}, {2, 0, 8}, {5, 0, 4}, {10, 0, 0}};
    static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    static const int MIX[] = {P_IDMIX, P_IFMIX, P_ICMIX, P_IPMIX, P_ILMIX, P_IHMIX};
    uint32_t p, i;

    memset(dly_buf, 0, sizeof dly_buf);
    memset(rev_tank, 0, sizeof rev_tank);
    R = &rev_tank[0];
    memset(&fx, 0, sizeof fx);
    lim_env = LIM_T;
    memset(trk, 0, sizeof trk);
    MOD_W_CLEAR();
    host_tracks_init();
    for (p = 0; p < NPART; p++) {
        host_preset(&trk[p], PARTS[p][0], PARTS[p][1]);
        trk[p].p[P_VOICE] = V_POLY;
        trk[p].p[P_SUS] = 127;
        trk[p].p[P_AMODE] = 0;
        for (i = 0; i < PARTS[p][2]; i++)
            trk_note_on(&trk[p], NOTES[i] + 12u * p, 100);
    }
    for (p = 0; p < NPART; p++) {
        for (i = 0; i < sizeof MIX / sizeof MIX[0]; i++)
            trk[p].p[MIX[i]] = 100;
        trk[p].p[P_DLY] = 100;
        trk[p].p[P_REV] = 100;
    }
    song.g[G_BPM] = 120;
    song.sel = 0;
}

static void laws_setup_heavy_matrix(void)
{
    static const int DESTS[NMSLOT] = {
        MD_PITCH, MD_CUT, MD_SHP, MD_AMP, MD_PAN, MD_DIST, MD_DLY, MD_REV
    };
    track_t *t = &trk[0];
    uint32_t i;

    laws_synth_heavy();
    clear_matrix(t);
    for (i = 0; i < NMSLOT; i++)
        slot(t, i, MS_LFO, DESTS[i], 40);
    lfo_at(t, 32767);
}

static void laws_setup_heavy_punch(void)
{
    laws_synth_heavy();
    clear_matrix(&trk[0]);
    laws_punch_start();
}

static void laws_engage_worst_punch(void)
{
    static const uint32_t WORST[] = {PF_R16, PF_LPF, PF_FLG};
    uint32_t i;

    for (i = 0; i < NELEM(WORST); i++)
        perf_press(WORST[i], 1);
}

static void laws_setup_heavy_both(void)
{
    laws_setup_heavy_matrix();
    laws_punch_start();
}

static void laws_engage_one_punch(void)
{
    perf_press(PF_R16, 1);
}

static int32_t laws_stress_lim(const laws_run_t *dry)
{
    int32_t lim = 8 * dry->click;
    if (lim < 50000)
        lim = 50000;
    return lim;
}

static int law_stress(void)
{
    laws_run_t dry, wet;
    int32_t lim;

    dry = laws_measure(laws_synth_heavy, 0);
    lim = laws_stress_lim(&dry);

    wet = laws_measure(laws_setup_heavy_matrix, laws_engage_lfo);
    laws_assert_audio("stress heavy_matrix", &wet, lim);

    wet = laws_measure(laws_setup_heavy_punch, laws_engage_worst_punch);
    laws_punch_release();
    check("stress heavy_punch stuck clear", laws_punch_idle());
    laws_assert_audio("stress heavy_punch", &wet, lim);
    laws_perf_clear_all();
    transport_req = 2;
    song.playing = 0;

    wet = laws_measure(laws_setup_heavy_both, laws_engage_one_punch);
    laws_punch_release();
    check("stress heavy_both stuck clear", laws_punch_idle());
    laws_assert_audio("stress heavy_both", &wet, lim);
    laws_perf_clear_all();
    transport_req = 2;
    song.playing = 0;
    return bad;
}

int main(void)
{
    printf("laws: matrix + engines + FX + LFO DEST + locks + punch + stress\n");
    printf("laws: HALF_US=%u SETTLE=%u MEAS=%u\n", HALF_US, SETTLE_BLK, MEAS_BLK);
    (void)instr_now();
    printf("laws: counter=%s cost_meaningful=%d\n",
           ipc_kind == 1 ? "instructions" : ipc_kind == 2 ? "rdtsc-cycles" : "none", LAWS_COST_MEANINGFUL);
    law_matrix();
    law_engines();
    law_fx();
    law_lfo_dest();
    law_locks();
    law_punch();
    law_stress();
    printf("%s\n", bad ? "LAWS TEST FAILED" : "laws test passed");
    return bad ? 1 : 0;
}
