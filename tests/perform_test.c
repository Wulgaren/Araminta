/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the FX hold layer's effects (firmware/src/perform.c), same sources as the firmware (through
 * hostsim.c).   build/host/perform_test [DEMO_DIR]          (run_tests.sh: build/perform_demo)
 * 1. timing: REPEAT and REVERSE start on the next 1/16 of the transport (sample exact) and end when let go
 *    (the live signal back, bit for bit, after the 2.9 ms ramp); the others start at once.
 * 2. stereo: REPEAT, REVERSE, TAPE STOP and FREEZE keep left and right apart (a silent right channel stays
 *    silent; each side follows its own input).
 * 3. a REPEAT too long for the loop (1/8 and REVERSE below 81 BPM, 1/12 below 54) does nothing at all;
 *    1/12 is a third of a beat (the 8T rate).
 * 4. the SLICER: its recordings are not used meanwhile and are dropped afterwards.
 * 5. the keys: a layer key plays nothing and sends no MIDI; a key held before FX stays a note; the white
 *    keys past the first 12 (D5 .. G5, none in the default map) are the layer's too but do nothing.
 * 6. idle: nothing held, nothing ramping, the mix is bit-identical (and the goldens of regress.c too).
 * 7. no clicks, no overflow; the filters, the CRUSH and THROW macros and the mutes do what they say.
 * 8. cost: instructions per sample of the song, idle and with every effect at once (proc_pid_rusage).
 * 9. OCT UP / OCT DN (the harmonizer): the pitch of the shifted part (the output less its 0.56 of the live
 *    input), each side its own: a sine whose period fits 1024 samples (the taps' distance) a whole number of
 *    times (215.33 / 344.53 Hz: both taps in phase, their jumps whole periods: no splice error) comes out at
 *    2x / 0.5x within 3 cents (a 2.5 s DFT, its peak refined); any other sine (220 / 330 Hz) has its energy in
 *    lines fs / 2048 = 21.5 Hz apart round the target (the rotating-delay splice, kept from the original: its "phasey"
 *    sound): its strongest line within one spacing of 2x / 0.5x. A silent side stays silent; no click in or out on a 110 Hz sine and the live signal back bit for bit
 *    after the ramp; the shimmer (KNOB 4 at 100, 0.82) on 4x full-scale square waves stays bounded and dies
 *    away after the input stops; KNOB 4 at 0 is plain (the delay holds only the input); the cost: over the
 *    idle song, and the harmonizer's own over REPEAT 1/16 alone (the layer's stage, which every effect of it
 *    pays): about 2 % at most (device estimate: 1.7 % per 100 instructions per sample).
 * 10. FLANGER, PHASER (1.2): silence stays silence, a silent side silent; no DC (a zero-mean noise keeps its mean, DC
 *    passes at unity) and about the input's level; bounded on 4x full-scale squares with LPF, HPF, REPEAT and CRUSH;
 *    the sweep follows the tempo (a 441 Hz saw comes out the same a bar / half a bar later at 120 and 90 BPM, not
 *    after 120's bar at 90); no click in or out, the live signal back bit for bit; stacked with REPEAT + LPF, let go:
 *    those as alone, bit for bit; L and R apart; KNOB 4 at 0 %: live; the cost.
 * 11. the key map (perform.c PF_DEF / PF2_DEF, seq.c perf_key): two fixed pages, the punch page shown picks theirs:
 *    the pages' contents (REPEAT 1/8 1/12 1/16 1/32 first, /2 *2 GLITCH on 2/2); the keys of a page, a page flip
 *    while nothing held; FX LATCH on with a key held (letting go clears its bit, the latched ones stay sticky);
 *    the pages' maps on the keys, G5 CLR only with LATCH.
 * 12. 1.5.1: the delay's TIME 1/8D 1/16D 1/4D (their lengths at 60 61 90 120 BPM and on the external clock, 1/4D clamped
 *    to the line at 60 BPM and below, the list append-only and longest first on the knob, ARP RATE / SEQ DIV unchanged, an
 *    echo at 1/8D where it belongs, free ms: the FM-1's one-pole roll (1.4391e-4 a sample to the target, tau 157 ms): a
 *    fast turn rides at the turn's own speed, an instant jump sweeps its chirp and lands, up and down alike); REV THROW / ECHO THROW: every track's reverb / delay send as at 127 while held, the
 *    other bus untouched, the glide (11.6 ms, bounded steps) in and out, ECHO THROW's feedback 0 -> 0.7 and one above
 *    left alone, bounded on full-scale squares and dying away, the reverb tail ringing out after the release, idle
 *    again after it, on PUNCH-IN 2/2 (the map's 8th / 9th key), the tracks' values never touched.
 * Demos (WAV) into DEMO_DIR; with a third directory 1.5.1's throws (the loop dry, ECHO THROW at 1/8D, REV THROW); with a second directory, the harmonizer's (a melody with OCT UP, OCT DN, and
 * OCT UP with the shimmer) and FLANGER's and PHASER's (a loop plain, then with each) into it. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int check(const char *what, int ok)
{
    printf("perform: %-73s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* ------------------------------------------------------ a test signal --- */
#define NT (6u * 44100u)
static int32_t in_l[NT], in_r[NT], out_l[NT], out_r[NT];
static void test_signal(double fl, double al, double fr, double ar)
{
    uint32_t t;
    for (t = 0; t < NT; t++) {
        in_l[t] = (int32_t)lrint(al * sin(2 * M_PI * fl * t / FS));
        in_r[t] = (int32_t)lrint(ar * sin(2 * M_PI * fr * t / FS + 1.0));
    }
}
static void perf_reset(void)
{
    memset(&pf, 0, sizeof pf);
    pf.src = pf.next = PF_N;
    pf.lc = PF_TOP;
    pf.mg[0] = pf.mg[1] = pf.mg[2] = pf.mg[3] = 32768;
    perf_held = perf_act = 0;
    perf_latched = 0;
    perf_kill = 0;
    perf_page(0);
    memset((void *)perf_k, 0, sizeof perf_k);
    sl_lent = 0;
    memset(sl, 0, sizeof sl);
    memset(sl_buf, 0, sizeof sl_buf);                  /* GLITCH reads what the ring held before it */
}
/* the stage alone on the test signal: samples [t0, t1); ev: press (+e + 1) / let go (-(e + 1)) at block bt */
typedef struct { uint32_t t; int e; } ev_t;
static int busy_seen;
static int8_t run_crush;                           /* KNOB 2 (CRUSH) during run(), 0 = untouched */
static int8_t run_k4;                              /* KNOB 4 (DEPTH; with OCT UP / DN the shimmer) during run() */
static void run(uint32_t bpm, int playing, const ev_t *ev, uint32_t nev, uint32_t t1)
{
    uint32_t t, k = 0;
    host_tracks_init();
    perf_reset();
    perf_k[1] = run_crush;
    perf_k[3] = run_k4;
    song.g[G_BPM] = (int16_t)bpm;
    song.playing = (uint8_t)playing;
    perf_start();
    busy_seen = 0;
    for (t = 0; t < t1; t += CTL) {
        uint32_t n = t1 - t < CTL ? t1 - t : CTL;    /* (the last block can be short: no write past the arrays) */
        while (k < nev && ev[k].t <= t) {
            perf_press((uint32_t)(ev[k].e > 0 ? ev[k].e - 1 : -ev[k].e - 1), ev[k].e > 0);
            k++;
        }
        memcpy(out_l + t, in_l + t, n * 4);
        memcpy(out_r + t, in_r + t, n * 4);
        if (perf_begin(n)) {
            busy_seen = 1;
            perf_block(out_l + t, out_r + t, n);
        }
    }
}
#define P16 5512u                                  /* a 1/16 at 120 BPM */
static int same(uint32_t a, uint32_t b)            /* out == in on [a, b), both sides */
{
    uint32_t t;
    for (t = a; t < b; t++)
        if (out_l[t] != in_l[t] || out_r[t] != in_r[t])
            return 0;
    return 1;
}
static int32_t err_vs(uint32_t a, uint32_t b, int32_t (*src)(const int32_t *, uint32_t), int side)
{
    uint32_t t;
    int32_t e = 0;
    for (t = a; t < b; t++) {
        int32_t d = abs((side ? out_r : out_l)[t] - src(side ? in_r : in_l, t));
        e = d > e ? d : e;
    }
    return e;
}
static uint32_t shift_d;                           /* the loop: in[t - shift_d] */
static int32_t delayed(const int32_t *x, uint32_t t) { return x[t - shift_d]; }
static uint32_t rev_a, rev_len, rev_t0;            /* REVERSE: in[rev_a + rev_len - 1 - (t - rev_t0) % rev_len] */
static int32_t reversed(const int32_t *x, uint32_t t) { return x[rev_a + rev_len - 1u - (t - rev_t0) % rev_len]; }
static int32_t peak(const int32_t *x, uint32_t a, uint32_t b)
{
    int32_t m = 0;
    for (; a < b; a++)
        m = abs(x[a]) > m ? abs(x[a]) : m;
    return m;
}

/* ------------------------------------------------------------ 1. timing --- */
static int test_timing(void)
{
    int bad = 0;
    char what[160];
    test_signal(440, 12000, 277, 9000);
    {   /* REPEAT 1/16: pressed mid-1/16, records the next 1/16, then loops it */
        ev_t ev[] = {{992, PF_R16 + 1}, {20000, -(PF_R16 + 1)}};
        int32_t el, er, el2;
        run(120, 1, ev, 2, 40000);
        shift_d = P16;
        el = err_vs(2 * P16 + 160, 3 * P16 - 160, delayed, 0);
        er = err_vs(2 * P16 + 160, 3 * P16 - 160, delayed, 1);
        shift_d = 2 * P16;
        el2 = err_vs(3 * P16 + 160, 20000 - 160, delayed, 0);
        snprintf(what, sizeof what, "REPEAT 1/16: live until the 1/16 after the press + one 1/16 (exact)");
        bad += check(what, same(0, 2 * P16));
        snprintf(what, sizeof what, "  then the 1/16 from that 1/16, looped (error L %d R %d, 2nd pass %d of 12000)", el, er, el2);
        bad += check(what, el < 100 && er < 100 && el2 < 100);
        bad += check("  let go: the live signal again after the ramp, bit for bit", same(20000 + 160, 40000));
        bad += check("  the buffer given back, nothing left running", !sl_lent && pf.mode == BM_NONE && !pf.busy);
    }
    {   /* REVERSE: the next 1/8 recorded, then played backwards */
        ev_t ev[] = {{992, PF_REV + 1}, {40000, -(PF_REV + 1)}};
        int32_t el, er;
        run(120, 1, ev, 2, 50000);
        rev_a = P16;
        rev_len = 11025;
        rev_t0 = P16 + rev_len;
        el = err_vs(rev_t0 + 160, rev_t0 + rev_len - 160, reversed, 0);
        er = err_vs(rev_t0 + 160, rev_t0 + rev_len - 160, reversed, 1);
        bad += check("REVERSE: live until the 1/16 after the press + a 1/8 (exact)", same(0, rev_t0));
        snprintf(what, sizeof what, "  then that 1/8 backwards (error L %d R %d of 12000)", el, er);
        bad += check(what, el < 100 && er < 100);
        bad += check("  let go: live again", same(40000 + 160, 50000));
    }
    {   /* stopped: at once */
        ev_t ev[] = {{992, PF_R16 + 1}};
        run(120, 0, ev, 1, 12000);
        bad += check("stopped: a 1/16 effect starts at once", (perf_act & PF_BIT(PF_R16)) && !same(992, 12000));
    }
    {   /* the immediate ones: LPF moves at once */
        ev_t ev[] = {{992, PF_LPF + 1}};
        run(120, 1, ev, 1, 3000);
        bad += check("LPF starts at once (not on the 1/16)", same(0, 992) && !same(992, 3000));
    }
    return bad;
}

/* ------------------------------------------------------------ 2. stereo --- */
static int test_stereo(void)
{
    static const struct { const char *name; int e; } C[] = {
        {"REPEAT 1/8", PF_R8}, {"REVERSE", PF_REV}, {"TAPE STOP", PF_TAPE}, {"FREEZE", PF_FRZ}};
    uint32_t c;
    int bad = 0;
    char what[160];
    for (c = 0; c < 4u; c++) {
        ev_t ev[] = {{992, C[c].e + 1}, {60000, -(C[c].e + 1)}};
        int32_t pl, pr;
        double cl = 0, cr = 0, nl = 0, nr = 0;
        uint32_t t;
        test_signal(440, 12000, 0, 0);                    /* the right side silent */
        run(120, 1, ev, 2, 70000);
        pl = C[c].e == PF_TAPE ? peak(out_l, 2000, 12000) : peak(out_l, 30000, 60000);
        pr = peak(out_r, 0, 70000);
        snprintf(what, sizeof what, "%s: a silent right side stays silent (L %d, R %d)", C[c].name, pl, pr);
        bad += check(what, pr == 0 && pl > 3000);
        test_signal(440, 12000, 3 * 440, 12000);          /* each side its own pitch */
        run(120, 1, ev, 2, 70000);
        for (t = C[c].e == PF_TAPE ? 1100u : 30000u; t < (C[c].e == PF_TAPE ? 2600u : 50000u); t++) {
            cl += (double)out_l[t] * out_l[t - 1];
            nl += (double)out_l[t] * out_l[t];
            cr += (double)out_r[t] * out_r[t - 1];
            nr += (double)out_r[t] * out_r[t];
        }
        cl = nl > 0 ? cl / nl : 1;
        cr = nr > 0 ? cr / nr : 1;
        snprintf(what, sizeof what, "  each side keeps its own pitch (lag-1 correlation L %.3f, R %.3f)", cl, cr);
        bad += check(what, cl > 0.99 && cr < 0.99 && cr > 0.9 && cl - cr > 0.008);
    }
    {   /* FREEZE holds: the input stops, the sound goes on */
        ev_t ev[] = {{992, PF_FRZ + 1}, {60000, -(PF_FRZ + 1)}};
        uint32_t t;
        test_signal(440, 12000, 330, 9000);
        for (t = 9000; t < NT; t++)
            in_l[t] = in_r[t] = 0;
        run(120, 1, ev, 2, 70000);
        snprintf(what, sizeof what, "FREEZE: the input gone, it holds what it caught (L %d R %d)", peak(out_l, 20000, 60000),
                 peak(out_r, 20000, 60000));
        bad += check(what, peak(out_l, 20000, 60000) > 6000 && peak(out_r, 20000, 60000) > 4000 && same(60000 + 160, 70000));
    }
    {   /* TAPE STOP: slower, then silent within its beat */
        ev_t ev[] = {{992, PF_TAPE + 1}, {50016, -(PF_TAPE + 1)}};
        test_signal(440, 12000, 330, 9000);
        run(120, 1, ev, 2, 60000);
        snprintf(what, sizeof what, "TAPE STOP: silent a beat after the press (peak %d), live again after", peak(out_l, 992 + 22050 + 64, 50016));
        bad += check(what, peak(out_l, 992 + 22050 + 64, 50016) == 0 && peak(out_r, 992 + 22050 + 64, 50016) == 0 &&
                           same(50016 + 160, 60000));
    }
    return bad;
}

/* -------------------------------------------------------- 3. too long --- */
static int test_too_long(void)
{
    int bad = 0;
    ev_t ev[] = {{992, PF_R8 + 1}, {40000, -(PF_R8 + 1)}};
    test_signal(440, 12000, 277, 9000);
    run(72, 1, ev, 2, 50000);
    bad += check("REPEAT 1/8 at 72 BPM (417 ms, the loop holds 371): nothing happens, bit for bit",
                 same(0, 50000) && !busy_seen && !sl_lent && !(perf_avail() & PF_BIT(PF_R8)));
    run(120, 1, ev, 2, 50000);
    bad += check("REPEAT 1/8 at 120 BPM (250 ms): it plays", !same(0, 40000) && (perf_avail() & PF_BIT(PF_R8)));
    song.g[G_BPM] = 81;
    bad += check("  REPEAT 1/8 and REVERSE (1/8) fit from 81 BPM, 1/16 from 41",
                 (perf_avail() & PF_BIT(PF_R8) && perf_avail() & PF_BIT(PF_REV)) &&
                 (song.g[G_BPM] = 80, !(perf_avail() & PF_BIT(PF_R8)) && !(perf_avail() & PF_BIT(PF_REV))) &&
                 (song.g[G_BPM] = 41, perf_avail() & PF_BIT(PF_R16)) &&
                 (song.g[G_BPM] = 40, !(perf_avail() & PF_BIT(PF_R16))));
    {   /* a REPEAT held while the tempo slows past its fit stops cleanly */
        uint32_t t;
        int32_t mx = 0;
        host_tracks_init();
        perf_reset();
        song.g[G_BPM] = 120;
        song.playing = 1;
        perf_start();
        test_signal(110, 12000, 110, 12000);
        perf_press(PF_R8, 1);
        for (t = 0; t < 120000u; t += CTL) {
            uint32_t i;
            if (t == 60000u)
                song.g[G_BPM] = 72;
            memcpy(out_l + t % NT, in_l + t % NT, CTL * 4);
            memcpy(out_r + t % NT, in_r + t % NT, CTL * 4);
            if (perf_begin(CTL))
                perf_block(out_l + t % NT, out_r + t % NT, CTL);
            for (i = 1; i < CTL && t > 1000u; i++) {
                int32_t d = abs(out_l[t % NT + i] - out_l[t % NT + i - 1]);
                mx = d > mx ? d : mx;
            }
        }
        perf_press(PF_R8, 0);
        bad += check("  a REPEAT 1/8 held while the tempo drops below its fit: it ends, no click",
                     pf.mode == BM_NONE && !sl_lent && mx < 4 * 190);
    }
    {   /* 3.2: REPEAT 1/12, a third of a beat (the 8T rate) */
        song.g[G_BPM] = 120;
        bad += check("REPEAT 1/12: a third of a beat (7350 samples at 120 BPM, the 8T rate)",
                     perf_len(PF_R12) == beat_samples() / 3u && perf_len(PF_R12) == 7350u &&
                     (perf_avail() & PF_BIT(PF_R12)));
        bad += check("  1/12 is a buffer REPEAT (not MIDI); the loop holds it from 54 BPM",
                     (PF_BUF & PF_BIT(PF_R12)) && (PF_REPEAT & PF_BIT(PF_R12)) && !(PF_MIDI & PF_BIT(PF_R12)) &&
                     (song.g[G_BPM] = 54, perf_avail() & PF_BIT(PF_R12)) &&
                     (song.g[G_BPM] = 53, !(perf_avail() & PF_BIT(PF_R12))));
    }
    return bad;
}

/* ------------------------------------------------------------ the song --- */
static void song_setup(void)
{
    static const uint8_t ACID[16] = {45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50};
    static const uint8_t AM[4] = {57, 60, 64, 67};
    track_t *t1 = &trk[0], *t2 = &trk[1], *td = &trk[3];
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    perf_reset();
    song.g[G_BPM] = 120;
    host_preset(t1, 0, 4);
    host_preset(t2, 1, 5);
    host_drums(td);                                /* DRUM KIT (SAMPLE PERC until 1.0.2) */
    for (i = 0; i < 16u; i++) {
        uint8_t n = ACID[i], d[4], k = 0;
        put_step(t1, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, 0);
        put_step(t2, i, i % 8u == 0u ? 4u : 0u, AM, i % 8u == 0u ? ST_NOTE : ST_TIE, 0);
        if (i % 4u == 0u) d[k++] = 36;
        if (i % 2u == 0u) d[k++] = 42;
        if (i == 4u || i == 12u) d[k++] = 38;
        put_step(td, i, k, d, k ? ST_NOTE : ST_REST, 0);
    }
    trk[0].p[P_PAN] = -40;                         /* the mix itself stereo */
    trk[1].p[P_PAN] = 40;
}
static int32_t song_l[10u * 44100u + CTL], song_r[10u * 44100u + CTL];   /* the longest render (harm_demos: 9.7 s), and
                                                         * the last block may run past the frames asked */
static void song_render(uint32_t frames, void (*at)(uint32_t t))
{
    uint32_t t, i;
    int32_t o[2 * CTL];
    transport_req = 1;
    for (t = 0; t < frames; t += CTL) {
        if (at)
            at(t);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            song_l[t + i] = o[2 * i];
            song_r[t + i] = o[2 * i + 1];
        }
    }
    transport_req = 2;
    mix_block(o, CTL);
}

/* ------------------------------------------------------------ 4. SLICER --- */
static int slicer_ok;
static void stut_at(uint32_t t)
{
    uint32_t k;
    if (t == 44032u)
        perf_press(PF_R16, 1);
    if (t > 50000u && t < 80000u)
        for (k = 0; k < NTRK; k++)
            if (sl[k].rec_on || sl[k].loop)
                slicer_ok = 0;
    if (t > 50000u && t < 80000u && !sl_lent)
        slicer_ok = 0;
    if (t == 80000u)
        perf_press(PF_R16, 0);
    if (t == 80000u + 320u)
        for (k = 0; k < NTRK; k++)
            if (sl_lent || sl[k].rec || sl[k].loop)
                slicer_ok = 0;
}
static int test_slicer(void)
{
    uint32_t k;
    song_setup();
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_SLCR] = SL_STUT;
        trk[k].p[P_SLPAT] = 1;
        trk[k].p[P_SLRATE] = 1;
        trk[k].p[P_SLDEPTH] = 127;
    }
    slicer_ok = 1;
    song_render(120000, stut_at);
    return check("SLICER STUT: live (no recording, no repeat) while REPEAT has the buffer, dropped after", slicer_ok);
}

/* -------------------------------------------------------------- 5. keys --- */
static int test_keys(void)
{
    int bad = 0;
    uint32_t mo0, fx = 1u << 3, k = 9;              /* (a button bit for FX; key 9: a white key, D4) */
    song_setup();
    usb.config = 1;
    mo_r = mo_w = 0;
    kb_mask = perf_mask = fx;
    fm1_in.buttons = fx;
    fm1_in.notes = 1u << k;
    keyboard_block();
    mo0 = mo_w;
    events_block(CTL);
    bad += check("a key pressed while FX is held: no voice, no MIDI, the layer's, its effect held",
                 !busy_now() && mo0 == 0u && (kb_layer >> k) & 1u && (perf_held & PF_BIT(perf_key(k))));
    fm1_in.buttons = 0;                             /* FX let go first: the effect stays with the key */
    keyboard_block();
    bad += check("  FX let go first: the key still holds its effect", (perf_held & PF_BIT(perf_key(k))) != 0u);
    fm1_in.notes = 0;
    keyboard_block();
    bad += check("  let go: no note-off, the effect off", mo_w == 0u && !kb_layer && !perf_held);
    fm1_in.notes = 1u << 7;                         /* a key, then FX: a note, and its note-off later */
    keyboard_block();
    fm1_in.buttons = fx;
    keyboard_block();
    fm1_in.notes = 0;
    keyboard_block();
    bad += check("a key held before FX stays a note (note-on and note-off sent)", mo_w == 2u && !kb_layer);
    kb_mask = perf_mask = 0;                                /* no layer now (a menu): keys are notes */
    fm1_in.notes = 1u << 7;
    keyboard_block();
    bad += check("no layer (kb_mask 0): FX held, keys are notes", mo_w == 3u && !kb_layer);
    fm1_in.notes = 0;
    fm1_in.buttons = 0;
    keyboard_block();
    {   /* the white keys past the first 10 (B4 on), and black keys past the mutes: the layer's, nothing */
        uint32_t q, ok = 1, assigned = 0;
        for (q = 0; q < 27u; q++)
            if (perf_key(q) < PF_N)
                assigned |= 1u << q;
        kb_mask = perf_mask = fx;
        mo0 = mo_w;
        for (q = 0; q < 27u; q++) {
            if ((assigned >> q) & 1u)
                continue;
            fm1_in.buttons = fx;
            fm1_in.notes = 1u << q;
            keyboard_block();
            ok &= (kb_layer >> q) & 1u && kb_note[q] == KB_SILENT && !perf_held && mo_w == mo0;
            fm1_in.notes = 0;
            keyboard_block();
            ok &= !kb_layer && !perf_held && mo_w == mo0;
        }
        fm1_in.buttons = 0;
        bad += check("the keys without an effect (G5 unassigned when LATCH off, A#4 target, black past mutes): silent, nothing held",
                     ok && assigned == 0x1AD5BFFu);  /* F3..F5 + mute blacks; G5/A#4 are PF_N/PF_TGT (>= PF_N) */
        kb_mask = perf_mask = 0;
    }
    usb.config = 0;
    return bad;
}

/* -------------------------------------------------------------- 6. idle --- */
static int test_idle(void)
{
    static int32_t a_l[NT], a_r[NT];
    uint32_t t, diff = 0;
    int bad = 0, fd[2];
    pid_t pid;
    song_setup();
    if (pipe(fd) || (pid = fork()) < 0)
        return check("idle: fork", 0);
    if (!pid) {                                     /* the same state, FX held the whole time, no key */
        kb_mask = perf_mask = 1u << 3;
        fm1_in.buttons = 1u << 3;
        song_render(3u * FS, 0);
        close(fd[0]);
        if (write(fd[1], song_l, 3u * FS * 4u) < 0 || write(fd[1], song_r, 3u * FS * 4u) < 0)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    song_render(3u * FS, 0);
    {
        FILE *f = fdopen(fd[0], "rb");
        size_t got = fread(a_l, 4, 3u * FS, f) + fread(a_r, 4, 3u * FS, f);
        fclose(f);
        waitpid(pid, 0, 0);
        diff = got != 6u * FS;
    }
    for (t = 0; t < 3u * FS; t++)
        diff += a_l[t] != song_l[t] || a_r[t] != song_r[t];
    bad += check("idle (FX held, no key): the song bit for bit as without the layer", !diff);
    {   /* after an effect: once its ramps are over, the stage is skipped again */
        uint32_t f;
        int32_t o[2 * CTL];
        perf_press(PF_R16, 1);
        for (f = 0; f < FS; f += CTL)
            mix_block(o, CTL);
        perf_press(PF_R16, 0);
        for (f = 0; f < 512u; f += CTL)
            mix_block(o, CTL);
        bad += check("  after an effect is let go and its ramp is over: skipped again", !pf.busy && !perf_begin(CTL));
    }
    return bad;
}

/* ----------------------------------------------- 7. clicks, the rest --- */
static int test_misc(void)
{
    int bad = 0;
    char what[160];
    static const struct { const char *name; int e; uint32_t off; } C[] = {
        {"REPEAT 1/8", PF_R8, 30000}, {"REPEAT 1/16", PF_R16, 30000}, {"REPEAT 1/32", PF_R32, 30000},
        {"REVERSE", PF_REV, 50000}, {"TAPE STOP", PF_TAPE, 30016}, {"FREEZE", PF_FRZ, 30016}, {"LPF", PF_LPF, 60000},
        {"HPF", PF_HPF, 60000}};
    uint32_t c, t;
    int32_t own;
    test_signal(110, 16000, 110, 16000);
    for (own = 0, t = 1; t < NT; t++)
        own = abs(in_l[t] - in_l[t - 1]) > own ? abs(in_l[t] - in_l[t - 1]) : own;
    for (c = 0; c < sizeof C / sizeof C[0]; c++) {
        ev_t ev[] = {{992, C[c].e + 1}, {C[c].off, -(C[c].e + 1)}};
        int32_t mx = 0;
        run(133, 1, ev, 2, C[c].off + 9000);
        for (t = 1; t < C[c].off + 9000; t++) {
            int32_t d = abs(out_l[t] - out_l[t - 1]);
            mx = d > mx ? d : mx;
        }
        snprintf(what, sizeof what, "no clicks: %s on a 110 Hz sine, in and out: largest step %d (sine %d)", C[c].name, mx, own);
        bad += check(what, mx <= 4 * own);
        bad += check("  back to the live signal, bit for bit", same(C[c].off + 3000, C[c].off + 9000));
    }
    {   /* the filters at their ends; full scale in, no overflow */
        ev_t ev[] = {{0, PF_LPF + 1}};
        test_signal(5000, 30000, 5000, 30000);
        run(120, 1, ev, 1, 100000);
        snprintf(what, sizeof what, "LPF held a bar: a 5 kHz sine down to %d of 30000", peak(out_l, 92000, 100000));
        bad += check(what, peak(out_l, 92000, 100000) < 3000 && peak(out_r, 92000, 100000) < 3000);
        ev[0].e = PF_HPF + 1;
        test_signal(80, 30000, 80, 30000);
        run(120, 1, ev, 1, 100000);
        snprintf(what, sizeof what, "HPF held a bar: an 80 Hz sine down to %d of 30000", peak(out_l, 92000, 100000));
        bad += check(what, peak(out_l, 92000, 100000) < 3000);
        for (t = 0; t < NT; t++) {                 /* full scale square waves, past Q15 */
            in_l[t] = (t / 37u) & 1u ? 120000 : -120000;
            in_r[t] = (t / 53u) & 1u ? 120000 : -120000;
        }
        {
            ev_t e2[] = {{0, PF_LPF + 1}, {0, PF_HPF + 1}, {0, PF_R16 + 1}};
            run_crush = 100;
            run(120, 1, e2, 3, 100000);
            run_crush = 0;
            snprintf(what, sizeof what, "4x full scale through LPF + HPF + KNOB 2 CRUSH + REPEAT: peak %d (no wrap)",
                     peak(out_l, 0, 100000) > peak(out_r, 0, 100000) ? peak(out_l, 0, 100000) : peak(out_r, 0, 100000));
            bad += check(what, peak(out_l, 0, 100000) < 300000 && peak(out_r, 0, 100000) < 300000);
        }
    }
    {   /* KNOB 1 left: a low-pass at once; KNOB 4: the REPEAT's level */
        test_signal(5000, 30000, 5000, 30000);
        host_tracks_init();
        perf_reset();
        song.g[G_BPM] = 120;
        song.playing = 1;
        perf_start();
        perf_k[0] = -100;
        for (t = 0; t < 20000u; t += CTL) {
            memcpy(out_l + t, in_l + t, CTL * 4);
            memcpy(out_r + t, in_r + t, CTL * 4);
            if (perf_begin(CTL))
                perf_block(out_l + t, out_r + t, CTL);
        }
        perf_k[0] = 0;
        snprintf(what, sizeof what, "KNOB 1 FILTER all the way left: a 5 kHz sine down to %d", peak(out_l, 10000, 20000));
        bad += check(what, peak(out_l, 10000, 20000) < 3000);
    }
    {   /* THROW (KNOB 3): the dry mix into the delay and reverb sends; mutes: a track ramps out */
        static int32_t ml[CTL], mr[CTL], sd[CTL], sr[CTL];
        uint32_t i;
        host_tracks_init();
        perf_reset();
        song.playing = 1;
        perf_k[2] = 100;
        for (t = 0; t < 1024u; t += CTL) {
            for (i = 0; i < CTL; i++)
                ml[i] = mr[i] = 8000, sd[i] = sr[i] = 0;
            if (perf_begin(CTL))
                perf_pre(ml, mr, sd, sr, CTL);
        }
        bad += check("KNOB 3 THROW at 100: the dry mix into both sends, full, the dry mix untouched",
                     sd[CTL - 1] > 7900 && sr[CTL - 1] > 7900 && ml[CTL - 1] == 8000 && mr[CTL - 1] == 8000);
        perf_k[2] = 0;
    }
    {
        int32_t o[2 * CTL];
        uint32_t f;
        int32_t before = 0, during = 0;
        song_setup();
        fx_boot();                                      /* no previous song's bus tails in the dry mute check */
        trk[1].p[P_SLEN] = 1; trk[3].p[P_SLEN] = 1;
        trk[1].step[0].n = 0; trk[3].step[0].n = 0;          /* only track 1 plays, dry */
        trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
        transport_req = 1;
        for (f = 0; f < 2u * FS; f += CTL) {
            uint32_t i;
            if (f == 44096u)
                perf_press(PF_M1, 1);
            mix_block(o, CTL);
            for (i = 0; i < CTL; i++) {
                if (f > FS / 2u && f < 44096u) before = abs(o[2 * i]) > before ? abs(o[2 * i]) : before;
                if (f > FS + 512u) during = abs(o[2 * i]) > during ? abs(o[2 * i]) : during;
            }
        }
        perf_press(PF_M1, 0);
        transport_req = 2;
        mix_block(o, CTL);
        snprintf(what, sizeof what, "black key 1: track 1 muted while held (peak %d -> %d), P_MUTE untouched", before, during);
        bad += check(what, before > 1000 && during < 200 && !trk[0].p[P_MUTE]);
    }
    {   /* PLAY SOLO on a silent track: the one that was playing ramps out, its own PLAY stays ON */
        int32_t o[2 * CTL];
        uint32_t f;
        int32_t before = 0, during = 0;
        song_setup();
        fx_boot();                                      /* isolate this dry solo check from earlier bus tails */
        trk[1].p[P_SLEN] = 1; trk[3].p[P_SLEN] = 1;
        trk[1].step[0].n = 0; trk[3].step[0].n = 0;
        trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
        transport_req = 1;
        for (f = 0; f < 2u * FS; f += CTL) {
            uint32_t i;
            if (f == 44096u)
                trk[1].p[P_MUTE] = PLAY_SOLO;
            mix_block(o, CTL);
            for (i = 0; i < CTL; i++) {
                if (f > FS / 2u && f < 44096u) before = abs(o[2 * i]) > before ? abs(o[2 * i]) : before;
                if (f > FS + 512u) during = abs(o[2 * i]) > during ? abs(o[2 * i]) : during;
            }
        }
        transport_req = 2;
        mix_block(o, CTL);
        snprintf(what, sizeof what, "PLAY SOLO on track 2: track 1 silent (peak %d -> %d), its PLAY stays ON", before, during);
        bad += check(what, before > 1000 && during < 200 && trk[0].p[P_MUTE] == PLAY_ON && trk[1].p[P_MUTE] == PLAY_SOLO);
        trk[1].p[P_MUTE] = PLAY_ON;
    }
    return bad;
}

/* -------------------------------------------------------- 9. harmonizer --- */
/* the strongest frequency of x[a .. b) near f0 (+-15 %): a coarse scan, then golden-section on the DFT power */
static double dft_pow(const int32_t *x, uint32_t a, uint32_t b, double f)
{
    double w = 2 * M_PI * f / FS, re = 0, im = 0, c = cos(w), sn = sin(w), cr = 1, ci = 0;
    uint32_t t;
    for (t = a; t < b; t++) {                      /* (a Hann window: leakage of the splices kept local) */
        double h = 0.5 - 0.5 * cos(2 * M_PI * (t - a) / (b - a)), v = x[t] * h, nr;
        re += v * cr;
        im -= v * ci;
        nr = cr * c - ci * sn;
        ci = cr * sn + ci * c;
        cr = nr;
    }
    return re * re + im * im;
}
static double peak_freq(const int32_t *x, uint32_t a, uint32_t b, double f0)
{
    double lo = f0 * 0.85, hi = f0 * 1.15, best = f0, bp = -1, f, g = 0.6180339887;
    int k;
    for (f = lo; f <= hi; f += 0.25) {
        double p = dft_pow(x, a, b, f);
        if (p > bp) {
            bp = p;
            best = f;
        }
    }
    lo = best - 0.25;
    hi = best + 0.25;
    for (k = 0; k < 40; k++) {
        double m1 = hi - g * (hi - lo), m2 = lo + g * (hi - lo);
        if (dft_pow(x, a, b, m1) > dft_pow(x, a, b, m2))
            hi = m2;
        else
            lo = m1;
    }
    return (lo + hi) / 2;
}
static int32_t res_l[NT], res_r[NT];
static int test_harm(void)
{
    int bad = 0;
    char what[200];
    uint32_t t, k;
    for (k = 0; k < 4u; k++) {   /* the pitch, each side its own: on the sweep's grid (k 0, 1), off it (2, 3) */
        int e = k & 1u ? PF_ODN : PF_OUP;
        double r = k & 1u ? 0.5 : 2.0, f0l = k < 2u ? 5.0 * FS / 1024 : 220, f0r = k < 2u ? 8.0 * FS / 1024 : 330;
        double fl, fr, cl, cr;
        ev_t ev[] = {{992, e + 1}};
        test_signal(f0l, 12000, f0r, 9000);
        run(120, 1, ev, 1, 3u * FS);
        for (t = 0; t < 3u * FS; t++) {                /* the shifted part: out less 0.56 of the input */
            res_l[t] = out_l[t] - ((in_l[t] >> 1) + ((in_l[t] >> 1) >> 3));
            res_r[t] = out_r[t] - ((in_r[t] >> 1) + ((in_r[t] >> 1) >> 3));
        }
        fl = peak_freq(res_l, FS / 2u, 3u * FS, f0l * r);
        fr = peak_freq(res_r, FS / 2u, 3u * FS, f0r * r);
        cl = 1200 * log2(fl / (f0l * r));
        cr = 1200 * log2(fr / (f0r * r));
        if (k < 2u) {
            snprintf(what, sizeof what, "%s: %.2f / %.2f Hz in, out at %.2f / %.2f Hz (%+.2f / %+.2f cents)",
                     k ? "OCT DN" : "OCT UP", f0l, f0r, fl, fr, cl, cr);
            bad += check(what, fabs(cl) <= 3 && fabs(cr) <= 3);
        } else {
            snprintf(what, sizeof what, "  220 / 330 Hz: the strongest splice line %+.1f / %+.1f Hz from %.0f / %.0f (within 21.5)",
                     fl - f0l * r, fr - f0r * r, f0l * r, f0r * r);
            bad += check(what, fabs(fl - f0l * r) <= FS / 2048.0 && fabs(fr - f0r * r) <= FS / 2048.0);
        }
    }
    {   /* a silent side stays silent */
        ev_t ev[] = {{992, PF_OUP + 1}, {60000, -(PF_OUP + 1)}};
        test_signal(440, 12000, 0, 0);
        run(120, 1, ev, 2, 70000);
        snprintf(what, sizeof what, "OCT UP: a silent right side stays silent (L %d, R %d)", peak(out_l, 20000, 60000),
                 peak(out_r, 0, 70000));
        bad += check(what, peak(out_r, 0, 70000) == 0 && peak(out_l, 20000, 60000) > 3000);
    }
    for (k = 0; k < 2u; k++) {   /* no clicks in or out; live again after the ramp */
        int e = k ? PF_ODN : PF_OUP;
        ev_t ev[] = {{992, e + 1}, {40000, -(e + 1)}};
        int32_t mx = 0, own = 0;
        test_signal(110, 16000, 110, 16000);
        for (t = 1; t < NT; t++)
            own = abs(in_l[t] - in_l[t - 1]) > own ? abs(in_l[t] - in_l[t - 1]) : own;
        run(133, 1, ev, 2, 49000);
        for (t = 1; t < 49000u; t++) {
            int32_t d = abs(out_l[t] - out_l[t - 1]);
            mx = d > mx ? d : mx;
        }
        snprintf(what, sizeof what, "no clicks: %s on a 110 Hz sine, in and out: largest step %d (sine %d)",
                 k ? "OCT DN" : "OCT UP", mx, own);
        bad += check(what, mx <= 4 * own);
        bad += check("  back to the live signal, bit for bit; the buffer given back",
                     same(40000 + 3000, 49000) && !sl_lent && pf.mode == BM_NONE);
    }
    {   /* the shimmer at its most: bounded, and it dies away */
        ev_t ev[] = {{0, PF_OUP + 1}};
        int32_t pk, tail;
        uint32_t e1 = NT / CTL * CTL, e0 = e1 - FS / 2u;   /* (whole blocks) the last half second */
        for (t = 0; t < NT; t++) {
            in_l[t] = t < 3u * FS ? ((t / 37u) & 1u ? 120000 : -120000) : 0;
            in_r[t] = t < 3u * FS ? ((t / 53u) & 1u ? 120000 : -120000) : 0;
        }
        run_k4 = 100;
        run(120, 1, ev, 1, e1);
        run_k4 = 0;
        pk = peak(out_l, 0, e1) > peak(out_r, 0, e1) ? peak(out_l, 0, e1) : peak(out_r, 0, e1);
        tail = peak(out_l, e0, e1) > peak(out_r, e0, e1) ? peak(out_l, e0, e1) : peak(out_r, e0, e1);
        snprintf(what, sizeof what, "OCT UP shimmer 0.82 on 4x full-scale squares: peak %d, 2.5 s after they stop %d", pk, tail);
        bad += check(what, pk < 300000 && tail <= 16 && pf.hfb == 100 * HB_FB);
    }
    {   /* KNOB 4 at 0: no feedback, the delay holds the input only (half level) */
        ev_t ev[] = {{0, PF_OUP + 1}};
        uint32_t f, diff = 0;
        test_signal(440, 12000, 330, 9000);
        run(120, 1, ev, 1, 20000);
        for (f = 1; f <= 2048u; f++) {
            uint32_t s = 20000u - f;
            const int16_t *q = &sl_buf[0][0] + 2u * ((pf.wr - f) & HB_MASK);
            diff += q[0] != (int16_t)(in_l[s] >> 1) || q[1] != (int16_t)(in_r[s] >> 1);
        }
        bad += check("OCT UP, KNOB 4 at 0: no shimmer (the delay holds the input alone)", !diff && !pf.hfb);
    }
    return bad;
}

/* ------------------------------------------------- 10. FLANGER, PHASER --- */
static double mean_of(const int32_t *x, uint32_t a, uint32_t b)
{
    double m = 0;
    uint32_t t;
    for (t = a; t < b; t++)
        m += x[t];
    return m / (b - a);
}
static double rms_of(const int32_t *x, uint32_t a, uint32_t b)
{
    double m = 0;
    uint32_t t;
    for (t = a; t < b; t++)
        m += (double)x[t] * x[t];
    return sqrt(m / (b - a));
}
/* the largest |out[t] - out[t + d]| over [a, b), both sides: the output repeats after d samples */
static int32_t period_err(uint32_t a, uint32_t b, uint32_t d)
{
    uint32_t t;
    int32_t e = 0;
    for (t = a; t < b; t++) {
        int32_t x = abs(out_l[t] - out_l[t + d]), y = abs(out_r[t] - out_r[t + d]);
        e = x > e ? x : e;
        e = y > e ? y : e;
    }
    return e;
}
static int test_mod(void)
{
    static const struct { const char *name; int e; uint32_t s16; double dc; } M[] = {{"FLANGER", PF_FLG, 16, 1.275},
                                                                                     {"PHASER", PF_PHS, 8, 1.2}};
    static int32_t ref_l[NT], ref_r[NT];
    int bad = 0;
    char what[200];
    uint32_t m, t;
    int32_t own;
    for (m = 0; m < 2u; m++) {
        int e = M[m].e;
        {   /* silence in: silence out; a silent side stays silent */
            ev_t ev[] = {{992, e + 1}, {60000, -(e + 1)}};
            test_signal(440, 0, 0, 0);
            run(120, 1, ev, 2, 70000);
            snprintf(what, sizeof what, "%s: silence in, silence out (exact)", M[m].name);
            bad += check(what,
                         !peak(out_l, 0, 70000) && !peak(out_r, 0, 70000) && busy_seen);
            test_signal(440, 12000, 0, 0);
            run(120, 1, ev, 2, 70000);
            snprintf(what, sizeof what, "  a silent right side stays silent (L %d, R %d)", peak(out_l, 20000, 60000),
                     peak(out_r, 0, 70000));
            bad += check(what, !peak(out_r, 0, 70000) && peak(out_l, 20000, 60000) > 6000 && !same(2000, 60000));
        }
        {   /* no DC: a zero-mean mix stays zero-mean; its level about the input's (noise: the comb / notches average out) */
            ev_t ev[] = {{0, e + 1}};
            double dm, lv;
            uint32_t r = 12345;
            for (t = 0; t < NT; t++) {
                r = r * 1103515245u + 12345u;
                in_l[t] = (int32_t)((r >> 16) & 0x3FFF) - 8192;
                r = r * 1103515245u + 12345u;
                in_r[t] = (int32_t)((r >> 16) & 0x3FFF) - 8192;
            }
            {   /* (less its own mean over the part measured: |mean| < 0.5) */
                int32_t ml = (int32_t)lrint(mean_of(in_l, FS, 4u * FS)), mr = (int32_t)lrint(mean_of(in_r, FS, 4u * FS));
                for (t = 0; t < 4u * FS; t++) {
                    in_l[t] -= ml;
                    in_r[t] -= mr;
                }
            }
            run(120, 1, ev, 1, 4u * FS);
            dm = fmax(fabs(mean_of(out_l, FS, 4u * FS)), fabs(mean_of(out_r, FS, 4u * FS)));
            lv = 20 * log10(rms_of(out_l, FS, 4u * FS) / rms_of(in_l, FS, 4u * FS));
            snprintf(what, sizeof what, "  no DC: zero-mean noise (%.0f rms) in, the mean out %.2f; its level %+.1f dB",
                     rms_of(in_l, FS, 4u * FS), dm, lv);
            bad += check(what, dm < 4.0 && lv > -6 && lv < 1.5);
            for (t = 0; t < NT; t++)
                in_l[t] = in_r[t] = 8000;
            run(120, 1, ev, 1, 2u * FS);
            {
                int32_t ml = (int32_t)lrint(mean_of(out_l, FS, 2u * FS)), mr = (int32_t)lrint(mean_of(out_r, FS, 2u * FS));
                snprintf(what, sizeof what, "  DC through, steady, at the peaks' gain (%.3f): 8000 in, L %d R %d out", M[m].dc,
                         ml, mr);
                bad += check(what, fabs(ml - 8000 * M[m].dc) <= 80 && fabs(mr - 8000 * M[m].dc) <= 80 &&
                                   peak(out_l, FS, 2u * FS) - abs(ml) <= 8);
            }
        }
        {   /* bounded: 4x full-scale squares through it with LPF, HPF, REPEAT and CRUSH */
            ev_t ev[] = {{0, PF_LPF + 1}, {0, PF_HPF + 1}, {0, PF_R16 + 1}, {0, e + 1}};
            int32_t pk;
            for (t = 0; t < NT; t++) {
                in_l[t] = (t / 37u) & 1u ? 120000 : -120000;
                in_r[t] = (t / 53u) & 1u ? 120000 : -120000;
            }
            run_crush = 100;
            run(120, 1, ev, 4, 100000);
            run_crush = 0;
            pk = peak(out_l, 0, 100000) > peak(out_r, 0, 100000) ? peak(out_l, 0, 100000) : peak(out_r, 0, 100000);
            snprintf(what, sizeof what, "  4x full scale with LPF + HPF + REPEAT + CRUSH: peak %d (no wrap)", pk);
            bad += check(what, pk < 300000);
        }
        {   /* the sweep in time: a 16-sample pattern (a period that fits the sweep at both tempi) comes out the same a sweep
             * later, at 120 and at 90 BPM; at 90 BPM not after 120's sweep, at 120 not after half of it */
            static const uint32_t BPM[2] = {120, 90};
            static const int16_t PAT[16] = {9000, -3000, 4000, 12000, -8000, 2000, -11000, 6000, 0, 7000, -5000, -12000,
                                            3000, 10000, -6000, -8000};
            uint32_t b;
            ev_t ev[] = {{0, e + 1}};
            for (t = 0; t < NT; t++)
                in_l[t] = in_r[t] = PAT[t % 16u];
            for (b = 0; b < 2u; b++) {
                uint32_t per = M[m].s16 * (FS * 60u / BPM[b] / 4u), wrong = M[m].s16 * (FS * 60u / 120u / 4u);
                int32_t ok_e, bad_e;
                run(BPM[b], 1, ev, 1, 8192u + 2u * per);   /* (a sweep from 8192 on, against the next: within NT) */
                ok_e = period_err(8192u, 8192u + per, per);
                bad_e = period_err(8192u, 8192u + per, b ? wrong : per / 2u);
                snprintf(what, sizeof what, "  %u BPM: the output repeats after %s (%u samples): error %d; after %s %d",
                         BPM[b], m ? "1/2 bar" : "a bar", per, ok_e, b ? "120 BPM's" : "half of it", bad_e);
                bad += check(what, ok_e <= 160 && bad_e > 1000);   /* (PHASER: its coefficients a block at a time) */
            }
        }
        {   /* no click in or out (a 110 Hz sine); live again after the ramp, bit for bit */
            ev_t ev[] = {{992, e + 1}, {40000, -(e + 1)}};
            int32_t mx = 0;
            test_signal(110, 16000, 110, 16000);
            for (own = 0, t = 1; t < NT; t++)
                own = abs(in_l[t] - in_l[t - 1]) > own ? abs(in_l[t] - in_l[t - 1]) : own;
            run(133, 1, ev, 2, 49000);
            for (t = 1; t < 49000u; t++) {
                int32_t d = abs(out_l[t] - out_l[t - 1]);
                d = abs(out_r[t] - out_r[t - 1]) > d ? abs(out_r[t] - out_r[t - 1]) : d;
                mx = d > mx ? d : mx;
            }
            snprintf(what, sizeof what, "  no clicks on a 110 Hz sine, in and out: largest step %d (sine %d)", mx, own);
            bad += check(what, mx <= 4 * own);
            bad += check("  let go: the live signal again after the ramp, bit for bit; the stage off",
                         same(40000 + 640, 49000) && !pf.busy && !pf.fw && !pf.pw && !pf.fn);
        }
        {   /* stacking: with REPEAT 1/16 and LPF held, let go of it: the rest as without it, bit for bit (after the ramp) */
            ev_t ev[] = {{0, PF_R16 + 1}, {0, PF_LPF + 1}, {992, e + 1}, {30000, -(e + 1)}};
            uint32_t diff = 0, during = 0;
            test_signal(330, 12000, 220, 12000);
            run(120, 1, ev, 2, 50000);                 /* REPEAT and LPF alone */
            memcpy(ref_l, out_l, 50000u * 4u);
            memcpy(ref_r, out_r, 50000u * 4u);
            run(120, 1, ev, 4, 50000);
            for (t = 0; t < 50000u; t++) {
                if (t >= 30000u + 640u)
                    diff += out_l[t] != ref_l[t] || out_r[t] != ref_r[t];
                if (t >= 12000u && t < 30000u)
                    during += out_l[t] != ref_l[t];
            }
            snprintf(what, sizeof what, "  with REPEAT 1/16 + LPF: it changes them (%u samples), let go they are as alone",
                     during);
            bad += check(what, during > 10000u && !diff && (perf_act & PF_BIT(PF_R16)));
        }
    }
    {   /* both at once with HPF: bounded and both running; the right side a quarter sweep behind (L != R) */
        ev_t ev[] = {{0, PF_FLG + 1}, {0, PF_PHS + 1}, {0, PF_HPF + 1}};
        uint32_t lr = 0;
        test_signal(330, 12000, 330, 12000);
        for (t = 0; t < NT; t++)
            in_r[t] = in_l[t];
        run(120, 1, ev, 3, 80000);
        for (t = 20000; t < 80000u; t++)
            lr += out_l[t] != out_r[t];
        snprintf(what, sizeof what, "FLANGER + PHASER + HPF: the same input both sides, L and R apart (%u samples), peak %d", lr,
                 peak(out_l, 0, 80000));
        bad += check(what, lr > 50000u && peak(out_l, 0, 80000) < 40000 && pf.fw == 32768 && pf.pw == 32768);
    }
    {   /* KNOB 4 DEPTH: their level (0 % = the live signal) */
        ev_t ev[] = {{0, PF_FLG + 1}, {0, PF_PHS + 1}};
        test_signal(330, 12000, 220, 12000);
        run_k4 = 100;
        run(120, 1, ev, 2, 20000);
        run_k4 = 0;
        {
            int32_t d = 0;
            for (t = 0; t < 20000u; t++)
                d = abs(out_l[t] - in_l[t]) > d ? abs(out_l[t] - in_l[t]) : d;
            snprintf(what, sizeof what, "KNOB 4 DEPTH 0 %%: FLANGER and PHASER held, the live signal (within %d of 12000)", d);
            bad += check(what, d <= 120);                /* (as the buffer effects: 0 % is 68 / 32768 of the effect) */
        }
    }
    return bad;
}

/* ------------------------------------------------- 11. the key map --- */
static int test_map(void)
{
    int bad = 0, ok = 1;
    uint32_t p, k;
    char what[160];
    bad += check("map: the pages are fixed (PUNCH-IN 1/2: REPEAT 1/8 1/12 1/16 1/32, LPF/HPF/OCT, DEC, stutter, ATK ARP RANDOM; 2/2: the audio effects, /2 *2 GLITCH)",
                 PF_DEF[0] == PF_R8 && PF_DEF[1] == PF_R12 && PF_DEF[2] == PF_R16 && PF_DEF[3] == PF_R32 &&
                 PF_DEF[4] == PF_LPF && PF_DEF[5] == PF_HPF && PF_DEF[6] == PF_OCTD && PF_DEF[7] == PF_OCTU &&
                 PF_DEF[8] == PF_DSHT && PF_DEF[9] == PF_DLNG && PF_DEF[10] == PF_S8 && PF_DEF[11] == PF_S16 &&
                 PF_DEF[12] == PF_ATK && PF_DEF[13] == PF_ARP && PF_DEF[14] == PF_RND && PF_DEF[15] == PF_N &&
                 PF2_DEF[0] == PF_REV && PF2_DEF[1] == PF_TAPE && PF2_DEF[2] == PF_FRZ && PF2_DEF[3] == PF_OUP &&
                 PF2_DEF[4] == PF_ODN && PF2_DEF[5] == PF_FLG && PF2_DEF[6] == PF_PHS && PF2_DEF[7] == PF_RTHR &&
                 PF2_DEF[8] == PF_ETHR && PF2_DEF[9] == PF_HALF && PF2_DEF[10] == PF_X2 && PF2_DEF[11] == PF_GLT &&
                 PF2_DEF[12] == PF_N && PF2_DEF[15] == PF_N);
    {   /* the keys: the shown page's fixed map (G5 none, LATCH off); a page flip (ui.c page_entered) follows */
        uint32_t fx = 1u << 3, white = 0, ck = 0;
        song_setup();
        while (key_place(ck) != 4u)
            ck++;                                       /* C4 */
        kb_mask = perf_mask = fx;
        ok = 1;
        for (k = 0; k < 27u; k++) {
            uint32_t e;
            if (key_black(k))
                continue;
            p = key_place(k);
            e = perf_key(k);
            fm1_in.buttons = fx;
            fm1_in.notes = 1u << k;
            keyboard_block();
            ok &= p == 15u ? !perf_held : perf_held == PF_BIT(e);
            fm1_in.notes = 0;
            keyboard_block();
            ok &= !perf_held && !kb_layer;
            white++;
        }
        bad += check("keys: the punch page's map under the notes (G5 unassigned, LATCH off), all 16 white keys",
                     ok && white == 16u);
        ok = 1;
        perf_page(1);                                   /* PUNCH-IN 2/2 (SELECT on the sticky map) */
        fm1_in.notes = 1u << ck;                        /* C4 on PUNCH-IN 2/2: PITCH DN */
        keyboard_block();
        ok &= perf_held == PF_BIT(PF_ODN) && perf_latched == 0u;
        fm1_in.notes = 0;
        keyboard_block();
        ok &= !perf_held && !kb_layer;
        bad += check("  the page flipped: the keys hold PUNCH-IN 2/2's effects (C4: PITCH DN)", ok);
        /* the latch coming on with a key still held (a settings load): letting the key go clears its bit, the
         * latched effects stay sticky */
        perf_page(0);
        perf_latch_on = 0;
        fm1_in.notes = 1u << ck;                        /* C4: LPF on page 1 */
        keyboard_block();
        ok = perf_held == PF_BIT(PF_LPF);
        perf_latch_on = 1;                              /* MENU > FX LATCH ON with the key down */
        fm1_in.notes = 0;
        keyboard_block();
        ok &= !perf_held;                               /* the release with the latch on: no stuck effect */
        fm1_in.notes = 1u << ck;
        keyboard_block();
        ok &= perf_latched == PF_BIT(PF_LPF) && !perf_held;   /* a press still latches it */
        fm1_in.notes = 0;
        keyboard_block();
        ok &= perf_latched == PF_BIT(PF_LPF) && !perf_held;   /* a latched effect stays until toggled off */
        fm1_in.notes = 1u << ck;
        keyboard_block();
        ok &= !perf_latched && !perf_held;
        fm1_in.notes = 0;
        keyboard_block();
        perf_latch_on = 0;
        bad += check("  FX LATCH on with a key held: letting go clears its bit, the latched ones stay sticky", ok);
        perf_page(0);                                   /* (page 1's map back, ui.c page_entered) */
        fm1_in.buttons = 0;
        kb_mask = perf_mask = 0;
    }
    snprintf(what, sizeof what, "map: the default map on the keys: F3..B3 REPEAT 1/8 1/12 1/16 1/32, C4..F4 LPF/HPF/OCT, G4..F5 the rest; G5 CLR only with LATCH");
    {
        ok = 1;
        for (k = 0; k < 27u; k++)
            if (!key_black(k)) {
                p = key_place(k);
                ok &= perf_key(k) == (p < PF_KEYS ? PF_DEF[p] : PF_N);   /* LATCH off: G5 is PF_N */
            }
        ok &= perf_key(0) == PF_R8 && perf_key(2) == PF_R12 && perf_key(4) == PF_R16 && perf_key(6) == PF_R32 &&
              perf_key(7) == PF_LPF && perf_key(9) == PF_HPF && perf_key(11) == PF_OCTD &&
              perf_key(12) == PF_OCTU && perf_key(21) == PF_ATK && perf_key(23) == PF_ARP && perf_key(24) == PF_RND;
        perf_latch_on = 1;
        for (k = 0; k < 27u; k++)
            if (!key_black(k) && key_place(k) == 15u)
                ok &= perf_key(k) == PF_CLR;
        perf_latch_on = 0;
        perf_page(1);
        for (k = 0; k < 27u; k++)
            if (!key_black(k)) {
                p = key_place(k);
                ok &= perf_key(k) == (p < PF_KEYS ? PF2_DEF[p] : PF_N);
            }
        ok &= perf_key(16) == PF_HALF && perf_key(18) == PF_X2 && perf_key(19) == PF_GLT;   /* A4 /2, B4 *2, C5 GLITCH */
        perf_page(0);                                   /* (page 1's map back) */
    }
    bad += check(what, ok);
    return bad;
}

/* ------------------------------- 12. the delay's dotted TIME, REV / ECHO THROW (1.5.1) --- */
/* the delay bus alone (fx_buses with the chorus and reverb inputs silent and their lines cleared): dly_in -> wet */
static void dly_clear(void)
{
    memset(dly_buf, 0, sizeof dly_buf);
    memset(rev_tank, 0, sizeof rev_tank);
    R = &rev_tank[0];
    memset(&fx, 0, sizeof fx);
    dly_q8 = 0;
    dly_free = 0;
    song.g[G_RTYPE] = 0;
}
static void dly_run(const int32_t *in, int32_t *wet_out, uint32_t frames)
{
    static int32_t z[CTL], w[CTL];
    uint32_t t, i;
    for (t = 0; t < frames; t += CTL) {
        uint32_t n = frames - t < CTL ? frames - t : CTL;   /* (the last block can be short) */
        fx_buses(in + t, z, w, n);
        for (i = 0; i < n; i++)
            wet_out[t + i] = w[i];
    }
}
/* the free-ms knob turned from a to b ms over turn samples: samples from the turn's start until the read
 * position lands, and the largest Q8 step a block takes on the way. That step is the tape pitch, and the
 * FM-1's own pole (DLY_K_NUM) sizes it: it is the speed the turn moves the target at, so the same move
 * turned slowly bends far less, and the read lands when the pole's creep catches the rest. */
static uint32_t knob_turn(uint32_t a_ms, uint32_t b_ms, uint32_t turn, uint32_t *settle)
{
    int32_t z[2u * CTL], from = (int32_t)(DTIME_FREE + (int32_t)a_ms), to = (int32_t)(DTIME_FREE + (int32_t)b_ms);
    uint32_t t, prev, max_st = 0, step;
    memset(z, 0, sizeof z);
    dly_clear();
    song.g[G_DTIME] = (int16_t)from;
    dly_run(z, z + CTL, CTL);                          /* snap onto a_ms */
    prev = dly_q8;
    for (t = 0; t < 8u * FS; t += CTL) {
        song.g[G_DTIME] = (int16_t)(from + (to - from) * (int32_t)(t < turn ? t : turn) / (int32_t)turn);
        dly_run(z, z + CTL, CTL);
        step = dly_q8 > prev ? dly_q8 - prev : prev - dly_q8;
        if (step > max_st)
            max_st = step;
        prev = dly_q8;
        if (t >= turn && dly_q8 == dtime_q8())
            break;
    }
    *settle = t;
    return max_st;
}
/* fn run in a child from this state (the noise / RAND seeds as they are here: renders compared bit for bit start
 * alike), its n bytes at buf back */
static void in_child(void (*fn)(void), void *buf, size_t n)
{
    int fd[2];
    pid_t pid;
    FILE *f;
    if (pipe(fd) || (pid = fork()) < 0)
        return;
    if (!pid) {
        close(fd[0]);
        fn();
        if (write(fd[1], buf, n) < 0)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    f = fdopen(fd[0], "rb");
    if (fread(buf, 1, n, f) != n)
        memset(buf, 0x55, n);
    fclose(f);
    waitpid(pid, 0, 0);
}
/* the song with every track's REV / DLY (-1: 0 40 127 90 / 0 20 127 64), effect e pressed at sample `press`: each
 * sample's send_r / send_d (sr_a, sd_a), rendered in a child */
static int32_t sr_a[2u * (FS + CTL)];
#define sd_a (sr_a + FS + CTL)
static uint32_t sr_frames, sr_press, sr_e;
static int32_t sr_rev, sr_dly;
static void sends_child(void);
static void sends_render(uint32_t frames, int32_t rev, int32_t dly, uint32_t press, uint32_t e)
{
    sr_frames = frames;
    sr_rev = rev;
    sr_dly = dly;
    sr_press = press;
    sr_e = e;
    in_child(sends_child, sr_a, sizeof sr_a);
}
static void sends_child(void)
{
    static const int32_t REVS[4] = {0, 40, 127, 90}, DLYS[4] = {0, 20, 127, 64};
    uint32_t t, i, k, frames = sr_frames, press = sr_press, e = sr_e;
    int32_t o[2 * CTL], rev = sr_rev, dly = sr_dly;
    song_setup();
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_REV] = (int16_t)(rev < 0 ? REVS[k] : rev);
        trk[k].p[P_DLY] = (int16_t)(dly < 0 ? DLYS[k] : dly);
    }
    transport_req = 1;
    for (t = 0; t < frames; t += CTL) {
        if (t == press)
            perf_press(e, 1);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            sr_a[t + i] = send_r[i];
            sd_a[t + i] = send_d[i];
        }
    }
    transport_req = 2;
    mix_block(o, CTL);
}
static int test_throw(void)
{
    static const uint8_t MUL[13][2] = {{1, 1}, {1, 2}, {1, 4}, {1, 8}, {1, 3}, {1, 6}, {2, 1}, {4, 1}, {8, 1}, {16, 1},
                                       {3, 4}, {3, 8}, {3, 2}};   /* N_DTIME: a quarter x a / b */
    static const uint32_t BPM[4] = {60, 61, 90, 120};
    static int32_t in[8u * FS], w0[8u * FS];
    char what[200];
    int bad = 0, ok = 1;
    uint32_t b, v, t, i;
    /* the TIME values' lengths */
    song.g[G_CLOCK] = 0;
    for (b = 0; b < 4u; b++) {
        song.g[G_BPM] = (int16_t)BPM[b];
        for (v = 0; v < 13u; v++) {
            uint32_t q = FS * 60u / BPM[b], want = v < 10u ? (MUL[v][0] * q) / MUL[v][1] : (MUL[v][0] * q) >> (v == 10u ? 2 : v == 11u ? 3 : 1);
            song.g[G_DTIME] = (int16_t)v;
            want = want >= DLY_LEN ? DLY_LEN - 1u : want;
            ok &= delay_samples() == want;
        }
    }
    song.g[G_BPM] = 120;
    song.g[G_DTIME] = 10;
    ok &= delay_samples() == 16537u;                   /* 1/8D at 120: 375 ms */
    song.g[G_DTIME] = 11;
    ok &= delay_samples() == 8268u;                    /* 1/16D: 187.5 ms */
    song.g[G_DTIME] = 12;
    ok &= delay_samples() == 33075u;                   /* 1/4D: 750 ms */
    bad += check("TIME: the ten as before, 1/8D 1/16D 1/4D 3/4 3/8 3/2 of a quarter at 60 61 90 120 BPM", ok);
    {   /* free milliseconds (DLY K4 TIME): 1 ms steps, not the division, inside the line */
        uint32_t ms, prev = 0;
        ok = 1;
        for (ms = 1; ms <= DTIME_MS_MAX; ms += ms < 20u || ms > DTIME_MS_MAX - 5u ? 1u : 50u) {
            uint32_t s;
            song.g[G_DTIME] = (int16_t)(DTIME_FREE + (int32_t)ms);
            s = delay_samples();
            ok &= dtime_free() && dtime_ms() == ms && s == (dtime_q8() + 128u) >> 8 && s >= 16u && s < DLY_LEN && s >= prev;
            prev = s;
        }
        song.g[G_BPM] = 120;
        song.g[G_DTIME] = 1;
        ok &= !dtime_free() && delay_samples() == FS * 60u / 120u / 2u;
        song.g[G_DTIME] = 12;                          /* the 1/4D checks below keep this value */
        bad += check("TIME free ms: one millisecond a step, longer as the number grows, divisions unchanged", ok);
    }
    {   /* free-ms TIME: the read position follows the knob with the FM-1's pole. 455 -> 25 ms in half a second
           * moves the target 0.86 samples a sample; the read rides at the turn's own speed (0.82, about +11
           * semitones) and lands 1.59 s in. The same move turned over four seconds bends at a seventh of that:
           * the bend is how fast the knob turns, not how far it moves. */
        uint32_t settle, fast, slow;
        host_tracks_init();
        song.g[G_TSPEED] = 100;
        song.g[G_DFDBK] = 0;
        fast = knob_turn(455, 25, FS / 2u, &settle);  /* a quick turn: 430 ms in half a second */
        ok = settle >= 7u * FS / 5u && settle < 9u * FS / 5u &&
              fast / CTL >= 190u && fast / CTL <= 230u && dly_q8 == dtime_q8();
        snprintf(what, sizeof what,
                 "TIME free ms: 455 -> 25 in half a second lands in %u samples, max step %u Q8/block (tape pitch)",
                 settle, fast);
        bad += check(what, ok);
        slow = knob_turn(455, 25, 4u * FS, &settle); /* the same move, turned slowly */
        ok = slow * 6u < fast && slow / CTL >= 20u && slow / CTL <= 35u && dly_q8 == dtime_q8();
        snprintf(what, sizeof what,
                 "TIME free ms: the same move over four seconds: max step %u Q8/block (the bend is the turn speed)",
                 slow);
        bad += check(what, ok);
        song.g[G_DTIME] = 12;
    }
    song.g[G_BPM] = 61;
    ok = delay_samples() == 65065u;                   /* 1/4D at 61: 1.475 s, fits */
    song.g[G_BPM] = 60;
    ok &= delay_samples() == DLY_LEN - 1u;             /* at 60 (66150): the line's length, 1.486 s */
    song.g[G_BPM] = 40;
    ok &= delay_samples() == DLY_LEN - 1u;
    song.g[G_CLOCK] = 1;
    midi_beat_samples = 30000u;                         /* an external clock's quarter */
    ok &= delay_samples() == 45000u;
    midi_beat_samples = 0;
    song.g[G_CLOCK] = 0;
    song.g[G_BPM] = 120;
    bad += check("TIME 1/4D: fits from 61 BPM, clamps to 65535 samples at 60 and below; the external clock's quarter", ok);
    {   /* the list: append-only names; knob skips 1/1 2BAR 4BAR (they never fit the line). ARP/SEQ keep ten. */
        const param_desc_t *d = &GP[G_DTIME];
        static const uint8_t SHOWN[10] = {6, 12, 0, 10, 1, 11, 4, 2, 5, 3};
        int32_t x = 6, seen = 0;
        ok = d->max == 12 && d->names[9][0] == '4' &&   /* stored 4BAR id remains for old projects */
             TP[P_ARATE].max == 9 && TP[P_SDIV].max == 9 && TP[P_ARATE].names == TP[P_SDIV].names;
        for (v = 0; v < 10u; v++) {
            ok &= x == SHOWN[v] && x != 7 && x != 8 && x != 9;
            seen |= 1 << x;
            x = param_turn(d, x, 1);
        }
        ok &= x == 3 && param_turn(d, 0, -1) == 12 && param_turn(d, 6, -1) == 6 &&
              param_turn(d, 9, 1) != 9 && (seen & 0x380) == 0;   /* never visits 7..9; legacy 4BAR steps to the list */
        bad += check("TIME list: knob 1/2 .. 1/32 (no 1/1 2BAR 4BAR); dotted 10..12 stay; max 12 for loads", ok);
    }
    {   /* the delay itself at 1/8D: a burst comes back 16537 samples later */
        uint32_t first = 0;
        host_tracks_init();
        dly_clear();
        song.g[G_BPM] = 120;
        song.g[G_DTIME] = 10;
        song.g[G_DFDBK] = 0;
        song.g[G_TSPEED] = 100;
        memset(in, 0, sizeof in);
        for (t = 0; t < 64u; t++)
            in[t] = 20000;
        dly_run(in, w0, FS);
        for (t = 0; t < FS && !first; t++)
            if (abs(w0[t]) > 5000)
                first = t;
        snprintf(what, sizeof what, "the delay at 1/8D, 120 BPM: the echo %u samples after the input (16537)", first);
        bad += check(what, first == 16537u);
    }
    /* ECHO THROW's feedback: FDBK 0 lifted to 0.7 while held; one above 0.7 left alone; bounded and dying away */
    {
        double r_off, r_on;
        uint32_t p = 5512u;                             /* 1/16 at 120 */
        int32_t pk1, pk4, tail, drive;
        host_tracks_init();
        song.g[G_BPM] = 120;
        song.g[G_DTIME] = 2;
        song.g[G_DFDBK] = 0;
        song.g[G_DCOLOR] = 127;
        song.g[G_TSPEED] = 100;
        memset(in, 0, sizeof in);
        for (t = 0; t < 1000u; t++)
            in[t] = 20000;
        perf_reset();
        dly_clear();
        dly_run(in, w0, 3u * p + CTL);
        r_off = (double)w0[2u * p + 500u] / (double)w0[p + 500u];
        pf.ew1 = 32768;                                 /* held (perf_begin's share at the block's end) */
        dly_clear();
        dly_run(in, w0, 3u * p + CTL);
        r_on = (double)w0[2u * p + 500u] / (double)w0[p + 500u];
        song.g[G_DFDBK] = 120;                          /* 0.84: above 0.7, as it is */
        {
            int32_t fb = song.g[G_DFDBK] * 230, f2 = fb;
            if (pf.ew1 && f2 < THR_FB)
                f2 += mulq16(THR_FB - f2, (uint32_t)pf.ew1 << 1);
            ok = f2 == fb;
        }
        snprintf(what, sizeof what, "ECHO THROW: FDBK 0 -> repeat ratio %.3f, held %.3f (0.7); FDBK 120 left as it is", r_off, r_on);
        bad += check(what, fabs(r_off) < 0.01 && r_on > 0.66 && r_on < 0.71 && ok);
        /* full-scale squares into the delay for 4 s at FDBK 120 and 90 with ECHO THROW held, then silence */
        for (v = 0; v < 2u; v++) {
            song.g[G_DFDBK] = v ? 90 : 120;
            for (t = 0; t < 8u * FS; t++)
                in[t] = t < 4u * FS ? ((t / 50u) & 1u ? 1 << 22 : -(1 << 22)) : 0;
            dly_clear();
            pf.ew1 = 32768;
            dly_run(in, w0, 4u * FS);
            pf.ew1 = 0;                                 /* let go */
            dly_run(in + 4u * FS, w0 + 4u * FS, 4u * FS);
            pk1 = peak(w0, 0, FS);
            pk4 = peak(w0, 3u * FS, 4u * FS);
            drive = peak(w0, FS, 4u * FS);
            tail = peak(w0, 7u * FS + FS / 2u, 8u * FS);
            snprintf(what, sizeof what, "  FDBK %d + ECHO THROW on 4x full-scale squares: peak %d .. %d (no growth), 3.5 s after: %d",
                     v ? 90 : 120, pk1, pk4, tail);
            /* wet is mulq15(x<<1, …): int16 min (-32768) yields peak 65536 */
            bad += check(what, drive <= 2 * 32768 && pk4 <= pk1 + pk1 / 50 && tail < drive / 100);
        }
        perf_reset();
    }
    /* REV THROW / ECHO THROW on the song: the sends as at 127, the glide, back bit for bit, the tail */
    {
        static int32_t sr_ref[FS + CTL], sd_ref[FS + CTL], sr_off[FS + CTL], sd_off[FS + CTL];
        uint32_t frames = FS, press = 8192u, diff_r = 0, diff_d = 0, off_d = 0;
        int32_t mx = 0;
        sends_render(frames, 127, 127, ~0u, 0);         /* every track's sends at 127 */
        memcpy(sr_ref, sr_a, sizeof sr_ref);
        memcpy(sd_ref, sd_a, sizeof sd_ref);
        sends_render(frames, -1, -1, ~0u, 0);           /* their own (0 40 127 90 / 0 20 127 64) */
        memcpy(sr_off, sr_a, sizeof sr_off);
        memcpy(sd_off, sd_a, sizeof sd_off);
        sends_render(frames, -1, -1, press, PF_RTHR);   /* REV THROW from 186 ms */
        for (t = 0; t < frames; t++) {
            if (t < press)
                diff_r += sr_a[t] != sr_off[t];
            else if (t >= press + 512u + CTL)
                mx = abs(sr_a[t] - sr_ref[t]) > mx ? abs(sr_a[t] - sr_ref[t]) : mx;
            off_d += sd_a[t] != sd_off[t];
        }
        snprintf(what, sizeof what, "REV THROW: the reverb send as with every REV at 127 (within %d), the delay's untouched", mx);
        bad += check(what, !diff_r && mx <= 3 && !off_d);
        mx = 0;
        sends_render(frames, -1, -1, press, PF_ETHR);
        for (t = press + 512u + CTL; t < frames; t++)
            mx = abs(sd_a[t] - sd_ref[t]) > mx ? abs(sd_a[t] - sd_ref[t]) : mx;
        for (t = 0; t < frames; t++)
            diff_d += sr_a[t] != sr_off[t];
        snprintf(what, sizeof what, "ECHO THROW: the delay send as with every DLY at 127 (within %d), the reverb's untouched", mx);
        bad += check(what, mx <= 3 && !diff_d);
    }
    {   /* the glide: a steady signal on one track, its sends at 0: up to full and back, THR_SLOPE a sample */
        static int32_t b1[CTL], sd[CTL], sr[CTL];
        int32_t prev = 0, full, maxstep = 0, last = 0;
        uint32_t up = 0, down = 0, k;
        track_t *tk = &trk[0];
        host_tracks_init();
        perf_reset();
        tk->p[P_REV] = tk->p[P_DLY] = 0;
        for (i = 0; i < CTL; i++)
            b1[i] = 40000;
        full = mulq15(((40000 >> 2) * LEVEL_Q12[tk->p[P_LEVEL] & 127]) >> 10, 32766);
        ok = 1;
        for (t = 0; t < 4096u; t += CTL) {
            if (t == 0)
                perf_press(PF_RTHR, 1);
            if (t == 2048u)
                perf_press(PF_RTHR, 0);
            perf_begin(CTL);
            memset(sd, 0, sizeof sd);
            memset(sr, 0, sizeof sr);
            if (pf.thr)
                perf_throw(tk, b1, sd, sr, CTL);
            for (i = 0; i < CTL; i++) {
                int32_t s = abs(sr[i] - prev);
                maxstep = s > maxstep ? s : maxstep;
                ok &= !sd[i];
                if (!up && sr[i] >= full)
                    up = t + i + 1u;
                if (t >= 2048u && !down && sr[i] == 0)
                    down = t + i + 1u - 2048u;
                prev = last = sr[i];
            }
            perf_block(sd, sr, CTL);                    /* (the stage, for pf.busy) */
        }
        k = (uint32_t)full * THR_SLOPE / 32768u + 2u;
        snprintf(what, sizeof what, "  the glide: full (%d) after %u samples, 0 again %u after the release, steps <= %d (bound %u)",
                 full, up, down, maxstep, k);
        bad += check(what, ok && up >= 500u && up <= 520u && down && down <= 520u && (uint32_t)maxstep <= k && !last &&
                           !pf.thr && !pf.busy && !perf_begin(CTL) && !tk->p[P_REV] && !tk->p[P_DLY]);
    }
    for (v = 0; v < 2u; v++) {   /* let go as the track falls silent: the bus rings out (REV / DLY 0: without the
                                  * throw nothing at all); ROOM SIZE 110, the delay 1/8D FDBK 60 */
        static int32_t b1[CTL], sd[CTL], sr[CTL], z[CTL], wb[CTL];
        uint32_t rel = FS - FS % CTL, e = v ? PF_ETHR : PF_RTHR, run;
        double e0 = 0, e1 = 0, e2 = 0, ref = 0;
        track_t *tk = &trk[0];
        for (run = 0; run < 2u; run++) {
            host_tracks_init();
            perf_reset();
            dly_clear();
            song.g[G_RSIZE] = 110;
            song.g[G_DTIME] = 10;
            tk->p[P_REV] = tk->p[P_DLY] = 0;
            for (t = 0; t < 3u * FS; t += CTL) {
                if (run && t == FS / 2u - (FS / 2u) % CTL)
                    perf_press(e, 1);
                if (run && t == rel)
                    perf_press(e, 0);
                for (i = 0; i < CTL; i++)
                    b1[i] = t < rel ? (int32_t)lrint(60000.0 * sin(2 * M_PI * 220.0 * (t + i) / FS)) : 0;
                perf_begin(CTL);
                memset(sd, 0, sizeof sd);
                memset(sr, 0, sizeof sr);
                if (pf.thr)
                    perf_throw(tk, b1, sd, sr, CTL);
                fx_buses(sd, sr, wb, CTL);
                perf_block(z, z, CTL);
                for (i = 0; i < CTL; i++) {
                    double a = fabs((double)wb[i]);
                    if (!run)
                        ref += a;
                    else if (t + i >= rel - FS / 4u && t + i < rel)
                        e0 += a;
                    else if (t + i >= rel + FS / 10u && t + i < rel + FS / 2u)
                        e1 += a;
                    else if (t + i >= rel + 13u * FS / 10u && t + i < rel + 17u * FS / 10u)
                        e2 += a;
                }
            }
        }
        e0 /= 0.25 * FS;
        e1 /= 0.4 * FS;
        e2 /= 0.4 * FS;
        snprintf(what, sizeof what, "%s let go: the %s rings out (|wet| %.0f held, %.0f 0.1 .. 0.5 s after, %.1f 1.3 .. 1.7 s; without: %.0f)",
                 v ? "ECHO THROW" : "REV THROW", v ? "delay" : "reverb", e0, e1, e2, ref);
        bad += check(what, ref == 0.0 && e0 > 500.0 && e1 > e0 / 20.0 && e2 < e1 / 8.0 && !pf.thr && !pf.busy);
    }
    {   /* PUNCH 2 (1.5.1): REV / ECHO THROW are the effects 12 / 13 and the page's keys 8 / 9, no settings */
        ok = PF_PHS == 11 && PF_RTHR == 12 && PF_ETHR == 13 && PF_HARM == (PF_BIT(PF_OUP) | PF_BIT(PF_ODN));
        ok &= PF2_DEF[7] == PF_RTHR && PF2_DEF[8] == PF_ETHR && PF_PHS == 11;
        bad += check("PUNCH-IN 2/2: REV / ECHO THROW are the 8th / 9th key (the effects 12 / 13); no settings", ok);
    }
    return bad;
}
/* The FM-1-style tape roll: the read position walks toward the TIME target with one pole per sample, K =
 * 1.4391e-4 (tau 6949 samples, 157 ms), no cap and no direction. An instant jump down (505 -> 174 ms) sizes
 * its first step to the error (2.10 samples a sample), the error decays by e every tau, and the creep lands
 * the read (54623 samples in, 1.24 s). The same move up is the mirror, a step for a step, and nothing else
 * runs: one pole, then the creep to land. */
static int test_dly_move(void)
{
    static int32_t z[2u * CTL], w[2u * CTL];
    char what[200];
    uint32_t t, prev, max_st, landed, first, e_tau, e0, st_last;
    int bad = 0, ok;
    memset(z, 0, sizeof z);
    host_tracks_init();
    /* an instant jump down: 505 -> 174 ms at one step */
    dly_clear();
    song.g[G_DTIME] = (int16_t)(DTIME_FREE + 505);
    dly_process(z, w, 1, 32768);                       /* the first block snaps onto the time */
    bad += check("tape roll: a fresh free TIME snaps onto the time", dly_q8 == dtime_q8());
    song.g[G_DTIME] = (int16_t)(DTIME_FREE + 174);
    e0 = dly_q8 - dtime_q8();                          /* the whole jump: 331 ms of error, Q8 */
    prev = dly_q8;
    max_st = landed = first = e_tau = st_last = 0;
    ok = 1;
    for (t = 0; t < 2u * FS; t++) {
        uint32_t st;
        dly_process(z, w, 1, 32768);
        st = dly_q8 > prev ? dly_q8 - prev : prev - dly_q8;
        if (st > max_st)
            max_st = st;
        if (t && st > st_last)                         /* the pole only decays: no step ever grows */
            ok = 0;
        st_last = st;
        prev = dly_q8;
        if (!t)
            first = st;
        if (t == 6948u)                                /* one tau in: e^-1 of the jump is left */
            e_tau = dly_q8 - dtime_q8();
        if (!landed && dly_q8 == dtime_q8())
            landed = t + 1u;
        else if (landed && dly_q8 != dtime_q8())       /* and it stays: nothing wired after it */
            ok = 0;
    }
    ok &= first == (((e0 >> 4) * (uint32_t)DLY_K_NUM) >> 16);   /* the pole's first step, straight from K */
    ok &= max_st == first;                             /* .. and it is the largest of the glide */
    ok &= e_tau > e0 * 35u / 100u && e_tau < e0 * 39u / 100u;   /* the pole's time constant */
    ok &= landed == 54623u && dly_q8 == dtime_q8();
    snprintf(what, sizeof what,
             "tape roll: 505 -> 174 ms: first step %u Q8, %u left one tau in, lands in %u samples (%.2f s)",
             first, e_tau, landed, (double)landed / FS);
    bad += check(what, ok);
    /* the same 171 ms up and down: the pole is symmetric, a step for a step */
    {
        uint32_t down, up, first_down, first_up;
        dly_clear();
        song.g[G_DTIME] = (int16_t)(DTIME_FREE + 345);
        dly_process(z, w, 1, 32768);
        song.g[G_DTIME] = (int16_t)(DTIME_FREE + 174);
        prev = dly_q8; down = first_down = 0;
        for (t = 0; t < 2u * FS; t++) {
            dly_process(z, w, 1, 32768);
            if (!t)
                first_down = prev - dly_q8;
            if (dly_q8 == dtime_q8()) {
                down = t + 1u;
                break;
            }
        }
        dly_clear();
        song.g[G_DTIME] = (int16_t)(DTIME_FREE + 174);
        dly_process(z, w, 1, 32768);
        song.g[G_DTIME] = (int16_t)(DTIME_FREE + 345);
        prev = dly_q8; up = first_up = 0;
        for (t = 0; t < 2u * FS; t++) {
            dly_process(z, w, 1, 32768);
            if (!t)
                first_up = dly_q8 - prev;
            if (dly_q8 == dtime_q8()) {
                up = t + 1u;
                break;
            }
        }
        ok = down == up && down == 50031u && first_down == first_up;
        snprintf(what, sizeof what,
                 "tape roll: 171 ms up and down are the mirror (first step %u Q8, lands in %u samples both ways)",
                 first_down, down);
        bad += check(what, ok);
    }
    song.g[G_DTIME] = 12;
    return bad;
}

/* -------------------------------------------------------------- 8. cost --- */
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
    static const int ALL[] = {PF_R16, PF_LPF, PF_HPF, PF_M1 + 2};
    static const int HARM[] = {PF_OUP};
    uint32_t f, k;
    uint64_t i0;
    int32_t o[2 * CTL];
    song_setup();
    transport_req = 1;
    for (f = 0; f < FS; f += CTL)
        mix_block(o, CTL);
    if (on == 1) {
        for (k = 0; k < sizeof ALL / sizeof ALL[0]; k++)
            perf_press((uint32_t)ALL[k], 1);
        perf_k[1] = perf_k[2] = 100;                /* the CRUSH and THROW macros */
    } else if (on == 2 || on == 4) {                /* OCT UP, with the shimmer (2) or plain (4) */
        perf_press((uint32_t)HARM[0], 1);
        perf_k[3] = on == 2 ? 60 : 0;
    } else if (on == 3) {                           /* REPEAT 1/16 alone: the layer's own cost, for scale */
        perf_press(PF_R16, 1);
    } else if (on == 5 || on == 6) {                /* REPEAT 1/16 and FLANGER (5) / PHASER (6) */
        perf_press(PF_R16, 1);
        perf_press(on == 5 ? PF_FLG : PF_PHS, 1);
    } else if (on == 7) {                           /* REV THROW and ECHO THROW together (1.5.1) */
        perf_press(PF_RTHR, 1);
        perf_press(PF_ETHR, 1);
    }
    i0 = instr_now();
    for (f = 0; f < 2u * FS; f += CTL)
        mix_block(o, CTL);
    for (k = 0; k < PF_N; k++)
        perf_press(k, 0);
    perf_k[1] = perf_k[2] = perf_k[3] = 0;
    transport_req = 2;
    return i0 ? (double)(instr_now() - i0) / (2.0 * FS) : 0;
}
/* each beat four times, the smallest: the host's retired-instruction counter reads a few percent
 * high now and then when the machine is busy (the suite runs ten tests at once: one run measured
 * the shimmer +147 against the +129 bound, the very next the same binary +116, while the other
 * beats stayed byte-exact). The true count is the smallest read; a real regression raises every
 * read. Two reads were not enough: the PHASER's true +142 sits only five below its +147 bound, so
 * one noisy read out of two still tipped it over under load. */
static double cost_min(int on)
{
    double a = cost_run(on), b = cost_run(on), c = cost_run(on), d = cost_run(on);
    double m = a < b ? a : b;
    if (c < m) m = c;
    if (d < m) m = d;
    return m;
}
static int test_cost(void)
{
    double idle = cost_min(0), on = cost_min(1), harm = cost_min(2), rep = cost_min(3), plain = cost_min(4);
    double flg = cost_min(5), phs = cost_min(6), thr = cost_min(7);
    char what[200];
    int bad;
    if (!idle) {
        printf("perform: cost: no instruction counter on this host\n");
        return 0;
    }
    snprintf(what, sizeof what, "cost: the song %.0f instructions / sample idle, %.0f with REPEAT+LPF+HPF+MUTE, K2 CRUSH, K3 THROW: +%.0f",
             idle, on, on - idle);
    bad = check(what, on - idle < 400);
    printf("perform: cost: over the idle song: REPEAT 1/16 alone +%.0f (the layer's own stage), OCT UP +%.0f, with the shimmer +%.0f\n",
           rep - idle, plain - idle, harm - idle);
    snprintf(what, sizeof what, "  OCT UP over REPEAT 1/16 (the harmonizer itself): +%.0f, with the shimmer +%.0f (device ~%.1f / %.1f %%, about 2 %%)",
             plain - rep, harm - rep, (plain - rep) * 0.017, (harm - rep) * 0.017);
    bad += check(what, (harm - rep) * 0.017 <= 2.2);
    snprintf(what, sizeof what, "  FLANGER over REPEAT 1/16: +%.0f, PHASER +%.0f (device ~%.1f / %.1f %%, at most 2.5 %% each)",
             flg - rep, phs - rep, (flg - rep) * 0.017, (phs - rep) * 0.017);
    bad += check(what, (flg - rep) * 0.017 <= 2.5 && (phs - rep) * 0.017 <= 2.5);
    snprintf(what, sizeof what, "  REV THROW + ECHO THROW over idle: +%.0f, over REPEAT 1/16 +%.0f (device ~%.1f %%, at most 2 %%)",
             thr - idle, thr - rep, (thr - rep) * 0.017);
    return bad + check(what, (thr - rep) * 0.017 <= 2.0);
}

/* ------------------------------------------------------------ demos --- */
static void demo_at(uint32_t t)
{
    static const struct { uint32_t t; int e; } D[] = {     /* press +(e + 1), let go -(e + 1) */
        {88192, PF_R8 + 1}, {110080, -(PF_R8 + 1)}, {110080, PF_R16 + 1}, {121088, PF_R32 + 1},
        {126592, -(PF_R32 + 1)}, {132096, -(PF_R16 + 1)}, {176384, PF_LPF + 1}, {264576, -(PF_LPF + 1)},
        {264576, PF_HPF + 1}, {308672, -(PF_HPF + 1)}, {308672, PF_REV + 1}, {352768, -(PF_REV + 1)},
        {352768, PF_TAPE + 1}, {385024, -(PF_TAPE + 1)}, {396800, PF_FRZ + 1}, {440896, -(PF_FRZ + 1)},
        {440896, PF_M1 + 1}, {462848, -(PF_M1 + 1)}};
    uint32_t i;
    if (t == 462848u)
        perf_k[2] = 100;                            /* KNOB 3 THROW for a moment */
    if (t == 474880u)
        perf_k[2] = 0;
    for (i = 0; i < sizeof D / sizeof D[0]; i++)
        if (D[i].t == t)
            perf_press((uint32_t)abs(D[i].e) - 1u, D[i].e > 0);
}
static void demos(const char *dir)
{
    char path[512];
    FILE *f;
    uint32_t t;
    song_setup();
    song_render(NT - 4096u, demo_at);
    snprintf(path, sizeof path, "%s/perform_tour.wav", dir);
    if (!(f = fopen(path, "wb")))
        return;
    wav_hdr(f, NT - 4096u);
    for (t = 0; t < NT - 4096u; t++)
        wav_put(f, song_l[t], song_r[t]);
    fclose(f);
    printf("perform: demo %s (REPEAT 1/8, a 1/16 -> 1/32 roll, LPF, HPF, REVERSE, TAPE STOP, FREEZE, MUTE 1, KNOB 3 THROW)\n", path);
}

/* the harmonizer on a melody: plain, OCT UP (a bar), OCT DN (a bar), OCT UP with the shimmer (a bar) */
static uint32_t hd_mode;
static void harm_at(uint32_t t)
{
    uint32_t bar = 4u * FS * 60u / 110u, e = hd_mode == 1u ? PF_ODN : PF_OUP;
    if (!hd_mode)
        return;
    if (t == ((bar + CTL - 1u) / CTL) * CTL) {
        perf_k[3] = hd_mode == 2u ? 70 : 0;
        perf_press(e, 1);
    }
    if (t == ((3u * bar + CTL - 1u) / CTL) * CTL)
        perf_press(e, 0);
}
static void harm_demos(const char *dir)
{
    static const uint8_t MEL[16] = {69, 0, 72, 76, 0, 74, 72, 0, 71, 0, 67, 69, 0, 0, 64, 0};
    static const char *const NAME[3] = {"harmonizer_up", "harmonizer_down", "harmonizer_shimmer"};
    char path[512];
    FILE *f;
    uint32_t t, i, m, frames = 4u * 4u * FS * 60u / 110u + FS;
    for (m = 0; m < 3u; m++) {
        memset(trk, 0, sizeof trk);
        host_tracks_init();
        perf_reset();
        song.g[G_BPM] = 110;
        host_preset(&trk[0], 0, 0);                       /* ANALOG SAW LEAD */
        for (i = 0; i < 16u; i++)
            put_step(&trk[0], i, MEL[i] ? 1u : 0u, &MEL[i], MEL[i] ? ST_NOTE : ST_REST, 0);
        hd_mode = m + 1u;
        song_render(frames, harm_at);
        hd_mode = 0;
        perf_k[3] = 0;
        snprintf(path, sizeof path, "%s/%s.wav", dir, NAME[m]);
        if (!(f = fopen(path, "wb")))
            return;
        wav_hdr(f, frames);
        for (t = 0; t < frames; t++)
            wav_put(f, song_l[t], song_r[t]);
        fclose(f);
        printf("perform: demo %s (a bar plain, two with %s, one plain)\n", path,
               m == 0u ? "OCT UP" : m == 1u ? "OCT DN" : "OCT UP and KNOB 4 SHIMMER 70");
    }
}

/* FLANGER and PHASER on the song: a bar plain, two with the effect, a bar plain (120 BPM) */
static uint32_t md_e;
static void mod_at(uint32_t t)
{
    uint32_t bar = 4u * FS * 60u / 120u;
    if (t == ((bar + CTL - 1u) / CTL) * CTL)
        perf_press(md_e, 1);
    if (t == ((3u * bar + CTL - 1u) / CTL) * CTL)
        perf_press(md_e, 0);
}
static void mod_demos(const char *dir)
{
    static const char *const NAME[2] = {"flanger", "phaser"};
    char path[512];
    FILE *f;
    uint32_t t, m, frames = 4u * 4u * FS * 60u / 120u + FS / 2u;
    for (m = 0; m < 2u; m++) {
        song_setup();
        md_e = m ? PF_PHS : PF_FLG;
        song_render(frames, mod_at);
        snprintf(path, sizeof path, "%s/%s.wav", dir, NAME[m]);
        if (!(f = fopen(path, "wb")))
            return;
        wav_hdr(f, frames);
        for (t = 0; t < frames; t++)
            wav_put(f, song_l[t], song_r[t]);
        fclose(f);
        printf("perform: demo %s (the loop: a bar plain, two with %s, one plain)\n", path, m ? "PHASER" : "FLANGER");
    }
}

/* 1.5.1's throws on the song (120 BPM, every track's REV / DLY 0, so all of it is the throw's): the loop dry; ECHO
 * THROW at TIME 1/8D held the 2nd bar's second half; REV THROW (HALL) likewise; each 4 bars, the song stopping after
 * the 3rd so the tail is heard */
static uint32_t td_e;
static void throw_at(uint32_t t)
{
    uint32_t bar = 4u * FS * 60u / 120u;
    if (td_e < PF_N && t == ((3u * bar / 2u + CTL - 1u) / CTL) * CTL)
        perf_press(td_e, 1);
    if (td_e < PF_N && t == ((2u * bar + CTL - 1u) / CTL) * CTL)
        perf_press(td_e, 0);
    if (t == ((3u * bar + CTL - 1u) / CTL) * CTL)
        transport_req = 2;
}
static void throw_demos(const char *dir)
{
    static const char *const NAME[3] = {"loop_dry", "echo_throw_1-8D", "rev_throw"};
    char path[512];
    FILE *f, *tour;
    uint32_t t, m, k, frames = 4u * 4u * FS * 60u / 120u;
    snprintf(path, sizeof path, "%s/throw_tour.wav", dir);   /* the three one after the other */
    if (!(tour = fopen(path, "wb")))
        return;
    wav_hdr(tour, 3u * frames);
    for (m = 0; m < 3u; m++) {
        song_setup();
        for (k = 0; k < NTRK; k++)
            trk[k].p[P_REV] = trk[k].p[P_DLY] = trk[k].p[P_CHOR] = 0;
        song.g[G_DTIME] = 10;                           /* 1/8D */
        song.g[G_RTYPE] = 2;                            /* HALL, the default */
        td_e = m == 0u ? PF_N : m == 1u ? PF_ETHR : PF_RTHR;
        song_render(frames, throw_at);
        snprintf(path, sizeof path, "%s/%s.wav", dir, NAME[m]);
        if (!(f = fopen(path, "wb")))
            return;
        wav_hdr(f, frames);
        for (t = 0; t < frames; t++) {
            wav_put(f, song_l[t], song_r[t]);
            wav_put(tour, song_l[t], song_r[t]);
        }
        fclose(f);
        printf("perform: demo %s (%s)\n", path, m == 0u ? "the loop dry, no sends" : m == 1u ?
               "ECHO THROW at 1/8D held 1.5 .. 2 bars, the song stops after 3" : "REV THROW held 1.5 .. 2 bars, the song stops after 3");
    }
    fclose(tour);
    printf("perform: demo %s/throw_tour.wav (the three in a row)\n", dir);
}

/* ------------------------------------------------- punch-in MIDI restore --- */
/* DEC± / OCT± change params / pitch only while held; outside the ISR (after mix_block) they are as stored. */
static int test_pfx_restore(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    int32_t o[2 * CTL];
    int16_t d0, r0, s0, a0;
    uint32_t i;

    host_tracks_init();
    perf_reset();
    pfx_tgt = 0;
    pfx_any = 0;
    memset(pfx_lane, 0, sizeof pfx_lane);
    song.playing = 0;
    song.g[G_BPM] = 120;
    host_preset(t, 0, 0);
    d0 = t->p[P_DEC];
    r0 = t->p[P_REL];
    s0 = t->p[P_SUS] = 100;
    a0 = t->p[P_ATK];

    /* OCT+: pitch offset while held, cleared on release */
    perf_press(PF_OCTU, 1);
    mix_block(o, CTL);
    bad += check("OCT+: pfx_pit +12 semitones while held", t->pfx_pit == 192);
    perf_press(PF_OCTU, 0);
    mix_block(o, CTL);
    bad += check("OCT+: pfx_pit restored to 0 on release", t->pfx_pit == 0 && !(t->pfx & PFX_DEC));

    /* OCT-: same the other way */
    perf_press(PF_OCTD, 1);
    mix_block(o, CTL);
    bad += check("OCT-: pfx_pit -12 semitones while held", t->pfx_pit == -192);
    perf_press(PF_OCTD, 0);
    mix_block(o, CTL);
    bad += check("OCT-: pfx_pit restored to 0 on release", t->pfx_pit == 0);

    /* DEC-: shortens DEC/REL, SUS 0 while held; stored values back after the block */
    {
        int16_t during_dec, during_sus;
        perf_press(PF_DSHT, 1);
        mix_block(o, CTL);
        during_dec = t->p[P_DEC];
        during_sus = t->p[P_SUS];
        /* pfx_end already ran: outside the ISR the stored values are back */
        bad += check("DEC-: DEC shortened while the ISR ran; stored DEC/SUS back after the block",
                     during_dec == d0 && during_sus == s0 && t->p[P_DEC] == d0 && t->p[P_REL] == r0 &&
                     t->p[P_SUS] == s0);
        /* prove the ISR actually changed them: sample mid-block via a second press with a probe */
        {
            int16_t mid_dec = -1, mid_sus = -1;
            /* hold DEC- across a note so pfx_block runs with PFX_DEC; catch values before pfx_end by
             * reading keep through the effect of a synth pluck (SUS forced 0 while sounding) */
            t->p[P_SUS] = s0;
            trk_note_on(t, 60, 100);
            for (i = 0; i < 8u; i++)
                mix_block(o, CTL);
            mid_sus = t->p[P_SUS];                      /* after pfx_end: back */
            mid_dec = t->p[P_DEC];
            bad += check("DEC- held: synth SUS/DEC as stored outside the ISR (restored each block end)",
                         mid_sus == s0 && mid_dec == d0 && (t->pfx & PFX_DEC));
            /* while held, a new block must temporarily apply the short decay (env dies with SUS 0) */
            {
                int32_t env = 0;
                uint32_t v;
                for (i = 0; i < 22016u / CTL; i++)
                    mix_block(o, CTL);
                for (v = 0; v < NVOICE; v++)
                    if (t->v[v].active && t->v[v].env > env)
                        env = t->v[v].env;
                bad += check("DEC- held: a synth note decays away (SUS forced 0 inside the ISR)",
                             env < (1 << 24) / 20);
            }
            trk_note_off(t, 60);
        }
        perf_press(PF_DSHT, 0);
        mix_block(o, CTL);
        bad += check("DEC-: PFX_DEC cleared and DEC/REL/SUS restored on release",
                     !(t->pfx & PFX_DEC) && t->p[P_DEC] == d0 && t->p[P_REL] == r0 && t->p[P_SUS] == s0);
    }

    /* DEC+: lengthens DEC/REL while held; back after */
    {
        int16_t want_d = (int16_t)(d0 + (127 - d0) * 2 / 3), want_r = (int16_t)(r0 + (127 - r0) * 2 / 3);
        (void)want_d;
        (void)want_r;
        t->p[P_DEC] = d0;
        t->p[P_REL] = r0;
        perf_press(PF_DLNG, 1);
        mix_block(o, CTL);
        bad += check("DEC+: DEC/REL as stored outside the ISR while held", t->p[P_DEC] == d0 && t->p[P_REL] == r0 &&
                     (t->pfx & PFX_DEC));
        perf_press(PF_DLNG, 0);
        mix_block(o, CTL);
        bad += check("DEC+: restored on release", !(t->pfx & PFX_DEC) && t->p[P_DEC] == d0 && t->p[P_REL] == r0 &&
                     t->p[P_ATK] == a0);
    }
    return bad;
}

/* ---------------------------------------------------- x2 TEMPO (3.2) --- */
static uint32_t seq_steps_run(track_t *t, uint32_t samples)
{
    uint32_t k, n = 0, prev = t->seq_idx;
    for (k = 0; k < samples; k += CTL) {
        seq_tick(t, CTL);
        if (t->seq_idx != prev) {
            n++;
            prev = t->seq_idx;
        }
    }
    return n;
}
static void seq_fresh(track_t *t)
{
    host_tracks_init();
    song.g[G_BPM] = 120;
    song.playing = 1;
    song.rec = 0;
    t->p[P_SDIV] = 2;
    t->p[P_SLEN] = 16;
    t->seq_idx = 0;
    t->seq_pos = 0;
    t->seq_n = 0;
    t->seq_hold = 0;
    t->pfx = 0;
    t->pfx_rs = 0;
}
static int test_x2(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    int32_t o[2 * CTL];
    uint32_t normal, doubled;

    seq_fresh(t);
    normal = seq_steps_run(t, 88200u);                 /* one bar of 1/16 at 120 BPM */
    seq_fresh(t);
    perf_reset();
    pfx_lph = 0;
    clk_pos = CLK_START;
    perf_press(PF_X2, 1);
    pfx_block(CTL, PF_BIT(PF_X2));                     /* (pfx_block alone: events_block would consume pfx_rs) */
    bad += check("x2: the MIDI bit and its origin while held", (t->pfx & PFX_X2) && t->pfx_idx0 == 0 && t->pfx_pos0 == 0);
    doubled = seq_steps_run(t, 88200u);
    bad += check("x2: one bar plays twice the steps", doubled >= 2u * normal - 2u && doubled <= 2u * normal + 2u);
    perf_press(PF_X2, 0);
    pfx_block(CTL, 0);
    bad += check("x2: let go arms the resync", !(t->pfx & PFX_X2) && t->pfx_rs);
    seq_tick(t, CTL);
    bad += check("x2: the next tick resyncs once", !t->pfx_rs);
    pfx_lane_put(0, 11);
    bad += check("x2: lane nibble 11", pfx_lane_at(0) == 11u);
    memset(pfx_lane, 0, sizeof pfx_lane);
    return bad;
}

/* --------------------------------------------------------- GLITCH (3.2) --- */
static int test_glitch(void)
{
    int bad = 0;
    ev_t e = {0, PF_GLT + 1};
    static int32_t a[NT], b[NT];
    uint32_t seen = 0, t;

    perf_reset();
    for (t = 0; t < PB_FRAMES; t++)
        pb_put(t, t < PB_FRAMES / 2u ? 100 : 200, t < PB_FRAMES / 2u ? 300 : 400);
    pf.wr = 5000;
    pf.P = 5512;
    pf.gl.a = GA_RPT;
    pf.gl.g0 = 10000;
    pf.gl.l = 1000;
    perf_glt_seg();
    pf.gl.u = SL_RAMP;
    perf_glt(0, 0);
    bad += check("GLITCH repeats the recent stereo samples in the ring's second half",
                 pf.gl_yl == 400 && pf.gl_yr == 800);

    test_signal(220.0, 0.5, 330.0, 0.5);
    clk_pos = CLK_START;
    run(120, 1, &e, 1, 4u * 44100u);
    bad += check("GLITCH: a held key plays the master through the slice buffer", busy_seen && !same(0, NT));
    memcpy(a, out_l, sizeof a);
    memcpy(b, out_r, sizeof b);
    clk_pos = CLK_START;
    run(120, 1, &e, 1, 4u * 44100u);
    bad += check("GLITCH: the same slice actions every pass (deterministic)",
                 !memcmp(a, out_l, sizeof a) && !memcmp(b, out_r, sizeof b));

    host_tracks_init();
    perf_reset();
    song.g[G_BPM] = 70;                                /* below ~82 BPM: only GATE / CRUSH / live */
    song.playing = 1;
    clk_pos = CLK_START;
    perf_start();
    perf_press(PF_GLT, 1);
    for (t = 0; t < NT; t += CTL) {
        uint32_t n = NT - t < CTL ? NT - t : CTL;    /* (the last block can be short) */
        memcpy(out_l + t, in_l + t, n * 4);
        memcpy(out_r + t, in_r + t, n * 4);
        if (perf_begin(n)) {
            perf_block(out_l + t, out_r + t, n);
            seen |= 1u << pf.gl.a;
        }
    }
    bad += check("GLITCH: below 82 BPM only GATE / CRUSH / live",
                 !(seen & ((1u << GA_RPT) | (1u << GA_REV) | (1u << GA_UP) | (1u << GA_DN) | (1u << GA_SKIP))));
    return bad;
}

int main(int argc, char **argv)
{
    int bad = 0;
    bad += test_timing();
    bad += test_stereo();
    bad += test_too_long();
    bad += test_slicer();
    bad += test_keys();
    bad += test_idle();
    bad += test_misc();
    bad += test_harm();
    bad += test_mod();
    bad += test_map();
    bad += test_throw();
    bad += test_dly_move();
    bad += test_pfx_restore();
    bad += test_x2();
    bad += test_glitch();
    bad += test_cost();
    if (argc > 1)
        demos(argv[1]);
    if (argc > 2) {
        harm_demos(argv[2]);
        mod_demos(argv[2]);
    }
    if (argc > 3)
        throw_demos(argv[3]);
    printf("%s\n", bad ? "PERFORM TEST FAILED" : "perform test passed");
    return bad != 0;
}
