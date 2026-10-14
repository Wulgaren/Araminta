/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the SLICER (firmware/src/slicer.c), same sources as the firmware (through hostsim.c).
 *   build/host/slicer_test [DEMO_DIR]          (run_tests.sh: build/slicer_demo)
 * 1. no clicks: a 110 Hz sine through GATE / STUT at several rates, tempi, swings, and with the mode,
 *    pattern and depth turned while it runs: the largest sample step of the output stays within a few
 *    times the sine's own (a hard gate edge would be ~60 x).
 * 2. timing: a constant signal through GATE at 97 BPM with swing: every step's middle is open or
 *    closed as the pattern says, every closing ramp ends on the step boundary (sample exact, from the
 *    swung step lengths of seq.c).
 * 3. sync: the 4-track song with the transport: at every block the SLICER's step is the sequencer's.
 * 4. OFF is transparent (the signal untouched), STUT repeats what the live step played.
 * 5. cost: instructions per sample of the mix, 4 SLICERs on against off (proc_pid_rusage).
 * Demos (WAV, 44.1 kHz) into DEMO_DIR: dry / gated / stuttered versions of a pad, the acid line, the
 * drums (track 4: DRUM), and the song. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int check(const char *what, int ok)
{
    printf("slicer: %-74s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void set_slicer(track_t *t, int mode, int pat, int rate, int depth)
{
    t->p[P_SLCR] = (int16_t)mode;
    t->p[P_SLPAT] = (int16_t)pat;
    t->p[P_SLRATE] = (int16_t)rate;
    t->p[P_SLDEPTH] = (int16_t)depth;
    /* SEND reads MODE/PAT/RATE from the shared bus; keep it in sync for host setups. */
    fx_bus_trk.p[P_SLCR] = (int16_t)mode;
    fx_bus_trk.p[P_SLPAT] = (int16_t)pat;
    fx_bus_trk.p[P_SLRATE] = (int16_t)rate;
}

/* ---------------------------------------------------------- 1. clicks --- */
/* n seconds of a 110 Hz sine (amplitude 20000) through track 1's SLICER; turn: change the settings
 * while it runs. Returns the largest |step| of the output; *in_step: the sine's own */
static int32_t sine_run(double secs, int turn, int32_t *in_step, int64_t *closed_energy)
{
    uint32_t f, i, frames = (uint32_t)(secs * FS) / CTL * CTL;
    int32_t prev = 0, pin = 0, mx = 0, mi = 0;
    double ph = 0, dph = 2 * M_PI * 110.0 / FS;
    track_t *t = &trk[0];
    *closed_energy = 0;
    slicer_start();
    for (f = 0; f < frames; f += CTL) {
        int32_t b[CTL];
        if (turn && f % 3008u == 0u) {          /* modes, patterns, depths, rates while it runs */
            uint32_t k = f / 3008u;
            set_slicer(t, (int)(k % 3u), (int)(1u + k * 7u % 16u), (int)(k % 6u), (int)(40u + k * 37u % 88u));
        }
        for (i = 0; i < CTL; i++) {
            b[i] = (int32_t)lrint(20000.0 * sin(ph));
            ph += dph;
            if (f + i) {
                int32_t d = abs(b[i] - pin);
                mi = d > mi ? d : mi;
            }
            pin = b[i];
        }
        slicer_track(t, b, CTL);
        for (i = 0; i < CTL; i++) {
            if (f + i) {
                int32_t d = abs(b[i] - prev);
                mx = d > mx ? d : mx;
            }
            prev = b[i];
            if (!sl[0].bit && sl[0].pos > SL_RAMP + CTL)
                *closed_energy += (int64_t)b[i] * b[i];
        }
    }
    *in_step = mi;
    return mx;
}

static int test_clicks(void)
{
    static const struct { const char *name; int mode, pat, rate, depth, bpm, swing; double secs; } C[] = {
        {"GATE pattern 1, 1/16, 120 BPM, 100 %", SL_GATE, 1, 1, 127, 120, 0, 4},
        {"GATE pattern 12, 1/32, 240 BPM, 100 %", SL_GATE, 12, 2, 127, 240, 0, 3},
        {"GATE pattern 2, 32T, 240 BPM, swing 50 %", SL_GATE, 2, 5, 127, 240, 50, 3},
        {"GATE pattern 5, 8T, 90 BPM, 60 %", SL_GATE, 5, 3, 76, 90, 10, 4},
        {"STUT pattern 7, 1/16, 120 BPM, 100 %", SL_STUT, 7, 1, 127, 120, 0, 4},
        {"STUT pattern 3, 32T, 200 BPM, 70 %, swing 30 %", SL_STUT, 3, 5, 90, 200, 30, 3},
        {"STUT pattern 9, 1/8, 40 BPM (loop shorter than the step)", SL_STUT, 9, 0, 127, 40, 0, 8},
        {"STUT pattern 13, 16T, 133 BPM", SL_STUT, 13, 4, 127, 133, 0, 4},
    };
    uint32_t c;
    int bad = 0;
    char what[160];
    for (c = 0; c < sizeof C / sizeof C[0]; c++) {
        int32_t in, out;
        int64_t ce;
        host_tracks_init();
        memset(sl, 0, sizeof sl);
        song.g[G_BPM] = (int16_t)C[c].bpm;
        trk[0].p[P_SSWING] = (int16_t)C[c].swing;
        set_slicer(&trk[0], C[c].mode, C[c].pat, C[c].rate, C[c].depth);
        out = sine_run(C[c].secs, 0, &in, &ce);
        snprintf(what, sizeof what, "no clicks: %s: largest step %d (sine %d, %.1f x)", C[c].name, out, in,
                 (double)out / in);
        bad += check(what, out <= 4 * in);
        if (C[c].mode == SL_GATE && C[c].depth == 127) {
            snprintf(what, sizeof what, "  closed steps silent (rms %.2f)", sqrt((double)ce / (FS * C[c].secs)));
            bad += check(what, ce < (int64_t)(FS * C[c].secs));
        }
    }
    {
        int32_t in, out;
        int64_t ce;
        host_tracks_init();
        memset(sl, 0, sizeof sl);
        out = sine_run(10, 1, &in, &ce);
        snprintf(what, sizeof what, "no clicks: mode / pattern / depth / rate turned while it runs: %d (%.1f x)", out,
                 (double)out / in);
        bad += check(what, out <= 4 * in);
    }
    return bad;
}

/* ---------------------------------------------------------- 2. timing --- */
static int test_timing(void)
{
    const uint32_t steps = 64u, pat = 8u;
    uint32_t f = 0, k, b0 = 0, bad_mid = 0, bad_edge = 0, n = 0;
    static int32_t out[16u * 44100u];
    int bad = 0;
    char what[160];
    track_t *t = &trk[0];
    host_tracks_init();
    memset(sl, 0, sizeof sl);
    song.g[G_BPM] = 97;
    song.g[G_SWING] = 10;
    song.g[G_SLOWN] = 1;                          /* CHAIN: track swing shapes the private clock */
    t->p[P_SSWING] = 20;
    set_slicer(t, SL_GATE, (int)pat, 1, 127);
    slicer_start();
    while (f + CTL <= sizeof out / sizeof out[0]) {
        uint32_t i;
        for (i = 0; i < CTL; i++)
            out[f + i] = 16384;
        slicer_track(t, out + f, CTL);
        f += CTL;
    }
    for (k = 0; k < steps; k++) {                 /* the step lengths as seq.c step_samples has them */
        uint32_t base = (uint32_t)FS * 60u / 97u / 4u, len, idx = k & 15u, on = (SL_PAT[pat - 1u] >> idx) & 1u;
        uint32_t non = (SL_PAT[pat - 1u] >> ((idx + 1u) & 15u)) & 1u;
        int32_t sw = (20 + 10) * (int32_t)base / 250;
        len = base + (uint32_t)((idx & 1u) ? -sw : sw);
        if (b0 + len >= f)
            break;
        n++;
        {
            int32_t open = mulq15(16384, 23170);  /* active GATE sits ~-3 dB under dry */
            if (on ? abs(out[b0 + len / 2u] - open) > 2 : abs(out[b0 + len / 2u]) > 2)
                bad_mid++;
            if (on && !non && (abs(out[b0 + len - 1u]) > 300 || abs(out[b0 + len - 1u - SL_RAMP] - open) > 2))
                bad_edge++;                       /* closed by the boundary, not earlier */
            if (!on && non && (abs(out[b0 + len]) > 300 || abs(out[b0 + len + SL_RAMP] - open) > 2))
                bad_edge++;                       /* opens from the boundary */
        }
        b0 += len;
    }
    snprintf(what, sizeof what, "timing: GATE at 97 BPM, swing 20 + 10 %%: %u steps open / closed as the pattern says",
             n);
    bad += check(what, !bad_mid && n > 32u);
    bad += check("timing: every ramp ends / starts on the swung step boundary (sample exact)", !bad_edge);
    return bad;
}

/* ------------------------------------------------------------ the song --- */
static void song_setup(void)
{
    static const uint8_t ACID[16] = {45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50};
    static const uint8_t ACIDF[16] = {1, 0, 2, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 2, 0, 1};
    static const uint8_t AM[4] = {57, 60, 64, 67}, FMI[4] = {53, 57, 60, 64};
    static const uint8_t LEAD[12] = {76, 0, 0, 79, 0, 0, 81, 0, 79, 0, 76, 0};
    track_t *t1 = &trk[0], *t2 = &trk[1], *t3 = &trk[2], *td = &trk[3];
    uint32_t i;
    host_tracks_init();
    memset(sl, 0, sizeof sl);
    song.g[G_BPM] = 120;
    host_preset(t1, 0, 4);
    host_preset(t2, 1, 5);
    host_preset(t3, 3, 0);
    host_drums(td);                                /* DRUM KIT (SAMPLE PERC until 1.0.2) */
    for (i = 0; i < 16u; i++) {
        uint8_t n = ACID[i];
        put_step(t1, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, ACIDF[i]);
    }
    t2->p[P_SLEN] = 32;
    t2->p[P_SGATE] = 120;
    for (i = 0; i < 32u; i++)
        put_step(t2, i, i % 16u == 0u ? 4u : 0u, i < 16u ? AM : FMI, i % 16u == 0u ? ST_NOTE : i % 16u < 14u ? ST_TIE : ST_REST, 0);
    t3->p[P_SLEN] = 12;
    for (i = 0; i < 12u; i++) {
        uint8_t n = LEAD[i];
        put_step(t3, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, i == 0u ? SF_ACCENT : 0u);
    }
    for (i = 0; i < 16u; i++) {
        uint8_t n[4];
        uint32_t k = 0;
        if (i % 4u == 0u)
            n[k++] = 36;
        if (i == 4u || i == 12u)
            n[k++] = 38;
        if (i % 2u == 0u)
            n[k++] = i == 14u ? 46 : 42;
        put_step(td, i, k, n, k ? ST_NOTE : ST_REST, i % 4u == 0u ? SF_ACCENT : 0u);
    }
}

/* ------------------------------------------------------------ 3. sync --- */
/* SEND bus: one shared clock (MODE/PAT/RATE from fx_bus_trk). CHAIN: that track's private clock.
 * Older per-track SEND clocks are gone — a shared bus cannot swing differently per track. */
static int test_sync(void)
{
    uint32_t f, k, blocks = 0, miss = 0, miss_ch = 0;
    char what[160];
    int bad = 0;
    song_setup();
    song.g[G_SWING] = 0;
    song.g[G_SLOWN] = 0;                          /* SEND for all */
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_SSWING] = 0;
        trk[k].p[P_SLEN] = 16;                    /* 16 steps of 1/16: the SLICER's bar */
        set_slicer(&trk[k], SL_GATE, 1, 1, 127);
    }
    song.g[G_BPM] = 131;
    transport_req = 1;
    for (f = 0; f < 12u * FS; f += CTL) {
        int32_t o[2 * CTL];
        const sl_t *s;
        uint32_t idx;
        mix_block(o, CTL);
        blocks++;
        s = sl_state(&trk[0]);
        idx = s && s->pos >= CTL ? s->idx : s ? (s->idx + 15u) & 15u : 0u;
        miss += !s || idx != trk[0].seq_idx % 16u;
        for (k = 1; k < NTRK; k++)
            miss += sl_state(&trk[k]) != s;       /* every SEND track shares the bus */
    }
    transport_req = 2;
    snprintf(what, sizeof what, "sync: SEND bus follows track 0 sequencer at %u blocks", blocks);
    bad += check(what, !miss);
    /* CHAIN on track 1: private clock; others USED */
    song_setup();
    song.g[G_SLOWN] = 2;                          /* track 1 CHAIN */
    song.g[G_BPM] = 120;
    set_slicer(&trk[1], SL_GATE, 1, 1, 127);
    slicer_start();
    transport_req = 1;
    for (f = 0; f < 4u * FS; f += CTL) {
        int32_t o[2 * CTL];
        const sl_t *s;
        uint32_t idx;
        mix_block(o, CTL);
        s = sl_state(&trk[1]);
        idx = s && s->pos >= CTL ? s->idx : s ? (s->idx + 15u) & 15u : 0u;
        miss_ch += !s || s != &sl[1] || idx != trk[1].seq_idx % 16u;
        miss_ch += sl_state(&trk[0]) != 0;        /* USED */
    }
    transport_req = 2;
    song.g[G_SLOWN] = 0;
    bad += check("sync: CHAIN track private clock; others USED", !miss_ch);
    return bad;
}

/* ------------------------------------------------------- 4. OFF / STUT --- */
static int test_off_stut(void)
{
    uint32_t f, i, same = 1, rep_ok = 0, rep_n = 0;
    track_t *t = &trk[0];
    int bad = 0;
    host_tracks_init();
    memset(sl, 0, sizeof sl);
    slicer_start();
    for (f = 0; f < FS; f += CTL) {               /* OFF: untouched */
        int32_t b[CTL], c[CTL];
        for (i = 0; i < CTL; i++)
            b[i] = c[i] = (int32_t)((f + i) * 2654435761u >> 17) - 16384;
        slicer_track(t, b, CTL);
        same &= !memcmp(b, c, sizeof b);
    }
    bad += check("OFF: the signal is not touched", same);
    /* STUT, pattern 7 (x...), 1/16 at 120 BPM, 100 %: the repeat steps play the live step again */
    set_slicer(t, SL_STUT, 7, 1, 127);
    slicer_start();
    {
        static int32_t in[4u * 44100u], out[4u * 44100u];
        uint32_t len = (uint32_t)FS * 60u / 120u / 4u;
        for (f = 0; f < sizeof in / sizeof in[0]; f++)
            in[f] = out[f] = (int32_t)(12000.0 * sin(2 * M_PI * 150.0 * f / FS));   /* 18.75 periods a step */
        for (f = 0; f + CTL <= sizeof in / sizeof in[0]; f += CTL)
            slicer_track(t, out + f, CTL);
        for (f = 0; f + 4u * len < sizeof in / sizeof in[0]; f += 4u * len)   /* steps 1..3 of every 4 = step 0 */
            for (i = 1; i < 4u; i++) {
                uint32_t a = f + i * len + len / 2u, b = f + len / 2u;
                int32_t want = mulq15(in[b], 23170);   /* full MIX: repeat at -3 dB vs recorded */
                rep_n++;
                rep_ok += abs(out[a] - want) < 500;    /* (22 kHz recording, read between samples) */
            }
    }
    {
        char what[160];
        snprintf(what, sizeof what, "STUT: the repeat steps play the live step (%u of %u checked)", rep_ok, rep_n);
        bad += check(what, rep_ok == rep_n && rep_n > 8u);
    }
    return bad;
}

/* ------------------------------------------------------------ 5. cost --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}

static double cost_run(int on)
{
    uint32_t f, k;
    uint64_t i0;
    int32_t o[2 * CTL];
    song_setup();
    for (k = 0; k < NTRK; k++)
        set_slicer(&trk[k], on ? (k & 1u ? SL_GATE : SL_STUT) : 0, 4, 1, 100);
    transport_req = 1;
    for (f = 0; f < FS; f += CTL)
        mix_block(o, CTL);
    i0 = instr_now();
    for (f = 0; f < 2u * FS; f += CTL)
        mix_block(o, CTL);
    transport_req = 2;
    return i0 ? (double)(instr_now() - i0) / (2.0 * FS) : 0;
}

static int test_cost(void)
{
    double off = cost_run(0), on = cost_run(1);
    char what[160];
    if (!off) {
        printf("slicer: cost: no instruction counter on this host\n");
        return 0;
    }
    snprintf(what, sizeof what, "cost: the song, 4 SLICERs on: %.0f instructions / sample (off %.0f): +%.0f, %.0f a track",
             on, off, on - off, (on - off) / 4);
    return check(what, (on - off) / 4 < 80);   /* pad envelope (~-3 dB) sits in this budget */
}

/* ------------------------------------------------------------ demos --- */
static void demo_song(const char *dir, const char *name, const int16_t (*s)[4], uint32_t solo)
{
    char path[512];
    FILE *w;
    uint32_t f, k, frames = 8u * FS;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(w = fopen(path, "wb")))
        return;
    song_setup();
    for (k = 0; k < NTRK; k++) {
        set_slicer(&trk[k], s[k][0], s[k][1], s[k][2], s[k][3]);
        if (solo && k + 1u != solo)
            trk[k].p[P_LEVEL] = 0;
    }
    transport_req = 1;
    wav_hdr(w, frames);
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        uint32_t i;
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            wav_put(w, o[2 * i], o[2 * i + 1]);
    }
    transport_req = 2;
    fclose(w);
}

static void demos(const char *dir)
{
    static const int16_t DRY[4][4] = {{0, 1, 1, 127}, {0, 1, 1, 127}, {0, 1, 1, 127}, {0, 1, 1, 127}};
    static const int16_t PAD_GATE[4][4] = {{0, 1, 1, 127}, {SL_GATE, 1, 1, 127}, {0, 1, 1, 127}, {0, 1, 1, 127}};
    static const int16_t PAD_GATE2[4][4] = {{0, 1, 1, 127}, {SL_GATE, 15, 2, 110}, {0, 1, 1, 127}, {0, 1, 1, 127}};
    static const int16_t ACID_STUT[4][4] = {{SL_STUT, 7, 1, 127}, {0, 1, 1, 127}, {0, 1, 1, 127}, {0, 1, 1, 127}};
    static const int16_t DRUM_STUT[4][4] = {{0, 1, 1, 127}, {0, 1, 1, 127}, {0, 1, 1, 127}, {SL_STUT, 12, 2, 127}};
    static const int16_t DRUM_GATE[4][4] = {{0, 1, 1, 127}, {0, 1, 1, 127}, {0, 1, 1, 127}, {SL_GATE, 3, 1, 127}};
    static const int16_t ALL[4][4] = {{SL_STUT, 9, 1, 127}, {SL_GATE, 1, 1, 127}, {SL_GATE, 14, 2, 90},
                                      {SL_STUT, 12, 2, 110}};
    demo_song(dir, "song_dry.wav", DRY, 0);
    demo_song(dir, "song_slicers.wav", ALL, 0);
    demo_song(dir, "pad_dry.wav", DRY, 2);
    demo_song(dir, "pad_gate_p1_16th.wav", PAD_GATE, 2);
    demo_song(dir, "pad_gate_p15_32nd.wav", PAD_GATE2, 2);
    demo_song(dir, "acid_dry.wav", DRY, 1);
    demo_song(dir, "acid_stut_p7.wav", ACID_STUT, 1);
    demo_song(dir, "drums_dry.wav", DRY, 4);
    demo_song(dir, "drums_gate_p3.wav", DRUM_GATE, 4);
    demo_song(dir, "drums_stut_p12_32nd.wav", DRUM_STUT, 4);
    printf("slicer: demos in %s (song_dry / song_slicers, pad, acid, drums: dry and sliced)\n", dir);
}

/* -------------------------------------------------- 6. MIX + SEND bus --- */
/* MIX (P_SLDEPTH) is the amount: 0 is transparent even when MODE is GATE.
 * SEND: MODE/PAT/RATE from fx_bus_trk (set_slicer keeps the bus in sync for host setups). */
static int test_mix_send(void)
{
    uint32_t f, i, same = 1, gated = 0;
    track_t *t0 = &trk[0], *t1 = &trk[1];
    int bad = 0;
    host_tracks_init();
    memset(sl, 0, sizeof sl);
    song.g[G_SLOWN] = 0;                                /* SEND for all */
    set_slicer(t0, SL_GATE, 1, 1, 0);                   /* MIX 0; bus MODE via set_slicer */
    set_slicer(t1, SL_GATE, 1, 1, 0);
    slicer_start();
    for (f = 0; f < FS / 2u; f += CTL) {
        int32_t b[CTL], c[CTL];
        slicer_block();
        for (i = 0; i < CTL; i++)
            b[i] = c[i] = (int32_t)((f + i) * 2654435761u >> 17) - 16384;
        slicer_track(t0, b, CTL);
        same &= !memcmp(b, c, sizeof b);
    }
    bad += check("MIX 0: GATE mode is transparent (bypass)", same);
    t0->p[P_SLDEPTH] = 127;                            /* MIX only; leave bus MODE/PAT/RATE */
    slicer_start();
    for (f = 0; f < FS / 2u; f += CTL) {
        int32_t b[CTL], c[CTL];
        slicer_block();
        for (i = 0; i < CTL; i++)
            b[i] = c[i] = (int32_t)((f + i) * 2654435761u >> 17) - 16384;
        slicer_track(t0, b, CTL);
        if (memcmp(b, c, sizeof b))
            gated = 1;
    }
    bad += check("MIX 127: SEND bus GATE closes some steps", gated);
    /* track MODE ignored on SEND: bus stays GATE while track MODE is OFF */
    t0->p[P_SLCR] = SL_OFF;
    slicer_start();
    gated = 0;
    for (f = 0; f < FS / 4u; f += CTL) {
        int32_t b[CTL], c[CTL];
        slicer_block();
        for (i = 0; i < CTL; i++)
            b[i] = c[i] = (int32_t)((f + i) * 2654435761u >> 17) - 16384;
        slicer_track(t0, b, CTL);
        if (memcmp(b, c, sizeof b))
            gated = 1;
    }
    bad += check("SEND: track MODE OFF ignored (bus GATE still runs)", gated);
    /* two SEND tracks share one DSP state pointer */
    host_tracks_init();
    memset(sl, 0, sizeof sl);
    song.g[G_SLOWN] = 0;
    set_slicer(t0, SL_GATE, 1, 1, 127);
    set_slicer(t1, SL_GATE, 8, 2, 127);
    bad += check("SEND: two tracks share one slicer bus DSP",
                 sl_state(t0) == sl_state(t1) && sl_state(t0) == &sl_bus);
    song.g[G_SLOWN] = 1;                                /* track 0 CHAIN; others USED (like DLY) */
    bad += check("CHAIN track keeps private slicer; other tracks USED",
                 sl_state(t0) == &sl[0] && sl_state(t1) == 0);
    song.g[G_SLOWN] = 0;
    return bad;
}

/* Active GATE/STUT sit ~-3 dB under dry. MIX 0 is bypass. On a decayed live signal, lower STUT
 * MIX clearly quiets the stutter. */
static int test_stut_mix_level(void)
{
    track_t *t = &trk[0];
    uint32_t f, i, len;
    int64_t e127 = 0, e64 = 0, e0 = 0;
    uint32_t n127 = 0, n64 = 0;
    double r127, r64;
    int bad = 0, same = 1;
    char what[160];

    host_tracks_init();
    memset(sl, 0, sizeof sl);
    song.g[G_BPM] = 120;
    song.g[G_SLOWN] = 1;
    len = (uint32_t)FS * 60u / 120u / 4u;                 /* 1/16 step */
    set_slicer(t, SL_STUT, 1, 1, 0);
    slicer_start();
    for (f = 0; f < 2u * FS; f += CTL) {
        int32_t b[CTL], c[CTL];
        for (i = 0; i < CTL; i++)
            b[i] = c[i] = (f / len) & 1u ? 2000 : 16000; /* loud on 'x', quiet on '.' */
        slicer_track(t, b, CTL);
        same &= !memcmp(b, c, sizeof b);
    }
    bad += check("STUT MIX 0: decayed tone stays dry (bypass)", same);

    /* GATE open steps are padded too (not only STUT repeats). */
    {
        int64_t e_open = 0;
        uint32_t n_open = 0;
        double ro;
        set_slicer(t, SL_GATE, 1, 1, 127);
        slicer_start();
        for (f = 0; f < 2u * FS; f += CTL) {
            int32_t b[CTL];
            for (i = 0; i < CTL; i++)
                b[i] = 16000;
            slicer_track(t, b, CTL);
            if (f < FS / 2u)
                continue;
            for (i = 0; i < CTL; i++)
                if (sl[0].bit && sl[0].pos > SL_RAMP) {
                    e_open += (int64_t)b[i] * b[i];
                    n_open++;
                }
        }
        ro = n_open ? sqrt((double)e_open / n_open) / 16000.0 : 0;
        snprintf(what, sizeof what, "GATE MIX 127: open steps ~-3 dB under dry (got %.2f)", ro);
        bad += check(what, ro > 0.60 && ro < 0.85);
    }

    set_slicer(t, SL_STUT, 1, 1, 127);
    slicer_start();
    for (f = 0; f < 4u * FS; f += CTL) {
        int32_t b[CTL];
        for (i = 0; i < CTL; i++)
            b[i] = (f / len) & 1u ? 2000 : 16000;
        slicer_track(t, b, CTL);
        if (f < FS / 2u)
            continue;
        for (i = 0; i < CTL; i++)
            if (!sl[0].bit) {
                e127 += (int64_t)b[i] * b[i];
                n127++;
            }
    }
    r127 = n127 ? sqrt((double)e127 / n127) / 16000.0 : 0;
    snprintf(what, sizeof what, "STUT MIX 127: '.' ~-3 dB vs recorded hit (got %.2f)", r127);
    bad += check(what, r127 > 0.58 && r127 < 0.82);

    set_slicer(t, SL_STUT, 1, 1, 64);
    slicer_start();
    for (f = 0; f < 4u * FS; f += CTL) {
        int32_t b[CTL];
        for (i = 0; i < CTL; i++)
            b[i] = (f / len) & 1u ? 2000 : 16000;
        slicer_track(t, b, CTL);
        if (f < FS / 2u)
            continue;
        for (i = 0; i < CTL; i++)
            if (!sl[0].bit) {
                e64 += (int64_t)b[i] * b[i];
                n64++;
            }
    }
    r64 = n64 ? sqrt((double)e64 / n64) / 16000.0 : 0;
    snprintf(what, sizeof what, "STUT MIX 64: '.' quieter than full MIX (%.2f < %.2f)", r64, r127);
    bad += check(what, r64 < r127 * 0.85 && r64 > 0.15);

    set_slicer(t, SL_STUT, 1, 1, 127);
    slicer_start();
    e0 = 0;
    n127 = 0;
    for (f = 0; f < 2u * FS; f += CTL) {
        int32_t b[CTL];
        for (i = 0; i < CTL; i++)
            b[i] = 16000;
        slicer_track(t, b, CTL);
        if (f < FS / 2u)
            continue;
        for (i = 0; i < CTL; i++)
            if (!sl[0].bit) {
                e0 += (int64_t)b[i] * b[i];
                n127++;
            }
    }
    r127 = n127 ? sqrt((double)e0 / n127) / 16000.0 : 0;
    snprintf(what, sizeof what, "STUT MIX 127 constant: under unity from -3 dB pad (got %.2f)", r127);
    bad += check(what, r127 > 0.60 && r127 < 0.85);
    return bad;
}

/* An active gate/repeat must finish its ramp, then leave both stereo signals untouched. */
static int test_mix_zero_after_ramp(void)
{
    int bad = 0;
    uint32_t chain, mode, f, i;
    for (chain = 0; chain < 2; chain++) {
        for (mode = SL_GATE; mode <= SL_STUT; mode++) {
            track_t *t = &trk[0];
            int active = 0, same = 1;
            host_tracks_init();
            memset(sl, 0, sizeof sl);
            memset(&sl_bus, 0, sizeof sl_bus);
            song.g[G_SLOWN] = chain ? 1 : 0;
            set_slicer(t, (int)mode, 1, 1, 127);
            slicer_start();
            for (f = 0; f < FS / 5u; f += CTL) {
                int32_t b[CTL], sd[CTL];
                for (i = 0; i < CTL; i++) b[i] = 12000, sd[i] = 6000;
                slicer_block();
                slicer_track_sd(t, b, sd, CTL);
                active |= mode == SL_GATE ? (chain ? sl[0].gc : sl_send_gc[0]) != 0
                                         : (chain ? sl[0].w : sl_send_w[0]) != 0;
            }
            bad += check("MIX 0 setup reaches an active gate/repeat", active);
            t->p[P_SLDEPTH] = 0;
            for (f = 0; f < SL_RAMP + 2u * CTL; f += CTL) {
                int32_t b[CTL], sd[CTL], dry[CTL], side[CTL];
                for (i = 0; i < CTL; i++) {
                    b[i] = dry[i] = 17000 + (int32_t)i;
                    sd[i] = side[i] = -3000 - (int32_t)i;
                }
                slicer_block();
                slicer_track_sd(t, b, sd, CTL);
                if (f >= SL_RAMP)
                    same &= !memcmp(b, dry, sizeof b) && !memcmp(sd, side, sizeof sd);
            }
            bad += check("MIX 0 settles to stereo bypass and releases the repeat", same && !slicer_busy(t));
        }
    }
    return bad;
}

int main(int argc, char **argv)
{
    int bad = 0;
    bad += test_clicks();
    bad += test_timing();
    bad += test_sync();
    bad += test_off_stut();
    bad += test_cost();
    bad += test_mix_send();
    bad += test_stut_mix_level();
    bad += test_mix_zero_after_ramp();
    if (argc > 1)
        demos(argv[1]);
    printf("%s\n", bad ? "SLICER TEST FAILED" : "slicer test passed");
    return bad != 0;
}
