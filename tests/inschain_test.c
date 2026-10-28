/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test: 6-stage insert chain order + MIX 0 bypass (P1-C). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int bad;
static void ck(const char *what, int ok)
{
    printf("inschain: %-76s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t out_buf[2 * CTL];
static void render_blocks(uint32_t n)
{
    while (n--)
        mix_block(out_buf, CTL);
}

static uint64_t hash_out(uint32_t blocks)
{
    uint64_t h = 0xCBF29CE484222325ull;
    uint32_t i, k, b;
    for (b = 0; b < blocks; b++) {
        mix_block(out_buf, CTL);
        for (i = 0; i < 2u * CTL; i++)
            for (k = 0; k < 4u; k++) {
                h ^= ((uint32_t)out_buf[i] >> (8u * k)) & 0xFFu;
                h *= 0x100000001B3ull;
            }
    }
    return h;
}

/* sum of |left| over blocks (after a short settle): insert FILTER CUT / RES must move this */
static int64_t energy_out(uint32_t settle, uint32_t blocks)
{
    int64_t e = 0;
    uint32_t i, b;
    render_blocks(settle);
    for (b = 0; b < blocks; b++) {
        mix_block(out_buf, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t x = out_buf[2u * i];
            e += x < 0 ? -x : x;
        }
    }
    return e;
}

/* bright ANALOG saw, synth filter open: insert stages alone colour the sound */
static void clear_stages(track_t *t)
{
    uint32_t i;
    for (i = P_IORD; i < P_FTYPE; i++)
        t->p[i] = TP[i].def;
    t->p[P_IORD] = 0;
    t->p[P_IDMIX] = t->p[P_IFMIX] = t->p[P_ICMIX] = t->p[P_IPMIX] = t->p[P_ILMIX] = t->p[P_IHMIX] = 0;
    t->p[P_DIST] = 0;
    t->p[P_DLY] = t->p[P_REV] = 0;
    t->p[P_E4] = 127;
    t->p[P_E5] = 0;
    t->p[P_ED_FLT] = 0;
}
static void filter_setup(track_t *t, int32_t type, int32_t cut, int32_t res, int32_t mix)
{
    clear_stages(t);
    t->p[P_IFTYPE] = (int16_t)type;
    t->p[P_IFCUT] = (int16_t)cut;
    t->p[P_IFRES] = (int16_t)res;
    t->p[P_IFMIX] = (int16_t)mix;
}
/* hold a note, settle, hash: used to prove a stage's knobs move the render */
static uint64_t note_hash(uint32_t settle, uint32_t blocks)
{
    trk_note_on(&trk[0], 60, 110);
    render_blocks(settle);
    return hash_out(blocks);
}

static void fresh(void)
{
    uint32_t k;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(rev_tank, 0, sizeof rev_tank);
    R = &rev_tank[0];
    memset(&fx, 0, sizeof fx);
    tape_clear();
    memset(&pf, 0, sizeof pf);
    memset(ins_st, 0, sizeof ins_st);
    memset(ins_lines, 0, sizeof ins_lines);
    memset(ins_bus_ran, 0, sizeof ins_bus_ran);
    memset(bus_knobs_on, 0, sizeof bus_knobs_on);
    fx_bus_init();
    dly_q8 = 0;                                       /* (as power-on: the first block snaps onto the time) */
    dly_free = 0;
    for (k = 0; k < NTRK; k++)
        pf.mg[k] = 32768;
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    vage = 0;
    rng_state = 0x1234567u;
    song.playing = 0;
    memset(trk, 0, sizeof trk);
    memset(fx_order_tbl, 0, sizeof fx_order_tbl);
    MOD_W_CLEAR();
    memset(sl, 0, sizeof sl);
    memset(&motion, 0, sizeof motion);
    host_tracks_init();
    host_preset(&trk[0], 0, 0);                     /* ANALOG first preset */
    host_preset(&trk[1], 0, 0);
    host_preset(&trk[2], 0, 0);
    host_preset(&trk[3], 0, 0);
    song.sel = 0;
}

/* TAPE (FUN17): a held SINE KEY, one head. MASTER/CHAIN drop the zero-crossing rate at SPEED 50;
 * SEND adds a wet copy; SPEED 100 with MIX 0 stays bit-identical. */
static uint32_t zero_cross_blocks(uint32_t blocks)
{
    uint32_t n = 0, b, i;
    int prev = 0, any = 0;
    for (b = 0; b < blocks; b++) {
        mix_block(out_buf, CTL);
        for (i = 0; i < CTL; i++) {
            int v = out_buf[2u * i];
            if (any && (v < 0) != (prev < 0))
                n++;
            prev = v;
            any = 1;
        }
    }
    return n;
}
/* the biggest sample-to-sample step: a head-wrap click shows up here even when the ZC rate is right */
static uint32_t max_delta_blocks(uint32_t blocks)
{
    uint32_t d = 0, b, i;
    int32_t prev = 0;
    int any = 0;
    for (b = 0; b < blocks; b++) {
        mix_block(out_buf, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t v = out_buf[2u * i], a = v - prev;
            if (a < 0)
                a = -a;
            if (any && (uint32_t)a > d)
                d = (uint32_t)a;
            prev = v;
            any = 1;
        }
    }
    return d;
}
static void tape_note(void)
{
    host_preset(&trk[0], 0, 5);                       /* ANALOG SINE KEY */
    trk[0].p[P_SUS] = 110;
    trk_note_on(&trk[0], 60, 100);
    render_blocks(128);                               /* the speed slew settles */
}
static void test_tape(void)
{
    uint64_t dry, bypass, master, chain, send;
    uint32_t zc_dry, zc_master, zc_chain, md_dry, md_master, md_chain;
    fresh();
    tape_note();
    dry = hash_out(64);
    zc_dry = zero_cross_blocks(64);
    md_dry = max_delta_blocks(64);

    fresh();                                          /* SEND, MIX 0: the head is out of the path */
    song.g[G_TAPEMODE] = TAPE_SEND;
    song.g[G_TSPEED] = 100;
    tape_note();
    bypass = hash_out(64);
    ck("TAPE SEND MIX 0 at 100 %: bit-identical to dry", dry == bypass);

    fresh();
    song.g[G_TAPEMODE] = TAPE_MASTER;
    song.g[G_TSPEED] = 50;
    song.g[G_TMIX] = 127;
    tape_note();
    master = hash_out(64);
    zc_master = zero_cross_blocks(64);
    md_master = max_delta_blocks(64);
    ck("TAPE MASTER 50 %: the render changes, the pitch drops, no head-wrap clicks",
       master != dry && zc_master * 4u < zc_dry * 3u && md_master < md_dry * 2u + 128u);

    fresh();
    song.g[G_TAPEMODE] = TAPE_CH1;
    song.g[G_TSPEED] = 50;
    song.g[G_TMIX] = 127;
    tape_note();
    chain = hash_out(64);
    zc_chain = zero_cross_blocks(64);
    md_chain = max_delta_blocks(64);
    ck("TAPE CHAIN 50 % on track 1: the render changes, the pitch drops, no clicks",
       chain != dry && zc_chain * 4u < zc_dry * 3u && md_chain < md_dry * 2u + 128u);

    fresh();
    song.g[G_TAPEMODE] = TAPE_SEND;
    song.g[G_TSPEED] = 50;
    song.g[G_TMIX] = 127;
    tape_note();
    send = hash_out(64);
    ck("TAPE SEND 50 %: the wet copy changes the render", send != dry);

    {   /* DROPOUT: 10..40 ms dips, 0.25..1 s apart: a long render must hear them */
        uint64_t off, on;
        fresh();
        song.g[G_TAPEMODE] = TAPE_MASTER;
        song.g[G_TSPEED] = 100;
        song.g[G_TMIX] = 127;
        tape_note();
        render_blocks(64);
        off = hash_out(2048);
        fresh();
        song.g[G_TAPEMODE] = TAPE_MASTER;
        song.g[G_TSPEED] = 100;
        song.g[G_TMIX] = 127;
        song.g[G_TDROP] = 1;
        tape_note();
        render_blocks(64);
        on = hash_out(2048);
        ck("TAPE DROPOUT on: the long render carries the tape's dips", on != off);
    }

    fresh();
    ck("TAPE mode labels: SEND / CHAIN / MASTER / USED",
       str_eq(fx_list_mode(&trk[0], INS_TAPE), "SEND") &&
       (song.g[G_TAPEMODE] = TAPE_CH1, str_eq(fx_list_mode(&trk[0], INS_TAPE), "CHAIN")) &&
       str_eq(fx_list_mode(&trk[1], INS_TAPE), "USED") &&
       (song.g[G_TAPEMODE] = TAPE_MASTER, str_eq(fx_list_mode(&trk[0], INS_TAPE), "MASTER")));
}
/* A MASTER insert runs on the whole mix (here one track held): it is not bypassed and does not need the
 * owner's per-track chain. Its MIX 0 is the dry path. */
/* A shared bus in MASTER takes the whole mix: its per-track route is suppressed and the owner's MIX
 * sets the amount. */
static void test_master_shared(void)
{
    uint64_t dry;
    fresh();
    tape_note();
    dry = hash_out(24);
    fresh();
    tape_note();
    trk[0].p[P_DLY] = 127;
    fx_master_set(INS_DLY, 0);
    ck("MASTER DELAY: the whole mix carries the repeats", hash_out(24) != dry);
    fx_master_clear(INS_DLY);
    fresh();
    tape_note();
    trk[0].p[P_REV] = 127;
    fx_master_set(INS_REV, 0);
    ck("MASTER REVERB: the whole mix carries the tail", hash_out(24) != dry);
    fx_master_clear(INS_REV);
    fresh();
    tape_note();
    trk[0].p[P_SLCR] = SL_GATE;
    trk[0].p[P_SLPAT] = 1;
    trk[0].p[P_SLDEPTH] = 127;
    fx_master_set(INS_SLCR, 0);
    ck("MASTER SLICER: the whole mix is gated", hash_out(24) != dry);
    fx_master_clear(INS_SLCR);
    song.g[G_DLYOWN] = song.g[G_REVOWN] = 0;
    song.g[G_SLOWN] = 0;
}

static void test_master_insert(void)
{
    uint64_t dry, master;
    fresh();
    tape_note();
    dry = hash_out(24);
    fresh();
    tape_note();
    trk[0].p[P_DIST] = 100;
    trk[0].p[P_IDMODE] = IDM_HARD;
    trk[0].p[P_IDMIX] = 127;
    fx_master_set(INS_DIST, 0);
    master = hash_out(24);
    ck("MASTER insert DIST 127: the whole mix changes", master != dry);
    fx_master_clear(INS_DIST);
    fresh();
    tape_note();
    trk[0].p[P_DIST] = 100;
    trk[0].p[P_IDMODE] = IDM_HARD;
    trk[0].p[P_IDMIX] = 0;
    fx_master_set(INS_DIST, 0);
    ck("MASTER insert DIST MIX 0: bit-identical to dry", hash_out(24) == dry);
    fx_master_clear(INS_DIST);
    song.g[G_TAPEMODE] = TAPE_SEND;
    song.g[G_TMIX] = 0;
    song.g[G_TDROP] = song.g[G_TWOW] = 0;
    song.g[G_TSPEED] = 100;
}

/* A pulse at a block boundary must reach both MASTER delay lines on later blocks. */
static void test_master_swept_tail(void)
{
    uint32_t st, b, i;
    for (st = INS_FLANG; st <= INS_CHOR; st++) {
        int64_t left = 0, right = 0;
        fresh();
        ins_send_set(&trk[0], st, 0);
        trk[0].p[st == INS_FLANG ? P_ILMIX : P_IHMIX] = 127;
        trk[0].p[st == INS_FLANG ? P_ILDEP : P_IHDEP] = 0;
        fx_master_set(st, 0);
        memset(mix_l, 0, sizeof mix_l);
        memset(mix_r, 0, sizeof mix_r);
        mix_l[CTL - 1u] = 16000;
        mix_r[CTL - 1u] = -12000;
        fx_master_run(CTL);
        for (b = 0; b < 8u; b++) {
            memset(mix_l, 0, sizeof mix_l);
            memset(mix_r, 0, sizeof mix_r);
            fx_master_run(CTL);
            for (i = 0; i < CTL; i++) {
                left += mix_l[i] < 0 ? -mix_l[i] : mix_l[i];
                right += mix_r[i] < 0 ? -mix_r[i] : mix_r[i];
            }
        }
        ck(st == INS_FLANG ? "MASTER FLANG: a boundary pulse rings in both channels" :
                                 "MASTER CHOR: a boundary pulse rings in both channels",
           left > 0 && right > 0);
        fx_master_clear(st);
    }
}

/* A fresh fully wet delay/reverb has no return yet, even for a loud summed mix. */
static void test_master_bus_headroom(void)
{
    uint32_t st, sign;
    for (st = INS_DLY; st <= INS_REV; st++) {
        int ok = 1;
        for (sign = 0; sign < 2u; sign++) {
            fresh();
            trk[0].p[st == INS_DLY ? P_DLY : P_REV] = 127;
            fx_master_set(st, 0);
            mix_l[0] = mix_r[0] = sign ? -100000 : 100000;
            fx_master_run(1);
            ok &= mix_l[0] == 0 && mix_r[0] == 0;
            fx_master_clear(st);
        }
        ck(st == INS_DLY ? "MASTER DELAY: full wet removes a loud dry mix without overflow" :
                              "MASTER REVERB: full wet removes a loud dry mix without overflow", ok);
    }
}

/* Transport restart reopens step zero of the MASTER gate on both channels. */
static void test_master_slicer_restart(void)
{
    uint32_t b, i;
    int32_t open_l, open_r, closed_l, closed_r;
    fresh();
    song.g[G_BPM] = 300;
    song.g[G_SWING] = 0;
    trk[0].p[P_SSWING] = 0;
    trk[0].p[P_SLCR] = SL_GATE;
    trk[0].p[P_SLPAT] = 1;
    trk[0].p[P_SLRATE] = 2;
    trk[0].p[P_SLDEPTH] = 127;
    fx_master_set(INS_SLCR, 0);
    seq_start();
    for (b = 0; b < 1536u / CTL; b++) {
        for (i = 0; i < CTL; i++) {
            mix_l[i] = 20000;
            mix_r[i] = -16000;
        }
        fx_master_run(CTL);
        if (b == 256u / CTL - 1u) {
            open_l = mix_l[CTL - 1u];
            open_r = mix_r[CTL - 1u];
        }
    }
    closed_l = mix_l[CTL - 1u];
    closed_r = mix_r[CTL - 1u];
    seq_stop();
    seq_start();
    for (b = 0; b < 256u / CTL; b++) {
        for (i = 0; i < CTL; i++) {
            mix_l[i] = 20000;
            mix_r[i] = -16000;
        }
        fx_master_run(CTL);
    }
    ck("MASTER SLICER: restarting transport reopens step zero in both channels",
       open_l > 10000 && open_r < -8000 && closed_l < 100 && closed_r > -100 &&
       mix_l[CTL - 1u] == open_l && mix_r[CTL - 1u] == open_r);
    seq_stop();
    fx_master_clear(INS_SLCR);
}

static int pick_has(uint32_t id, const char *name)
{
    uint32_t i;
    for (i = 0; i < pk_n; i++)
        if (pk_rows[i].id == id && (!name || str_eq(pk_rows[i].name, name))) return 1;
    return 0;
}

/* The four DIST MODEs on a 344 Hz sine (128 samples a period, one voice's full scale): at high DRIVE
 * they must not converge into one shape (the bug: SOFT, HARD and FUZZ all squared off and were
 * indistinguishable maxed out), a MODE switch must not jump in level, and none of them may leave DC. */
#define DM_N 8192u                                     /* 64 whole periods: settled, then measured */
static int32_t dm_wet[IDM_N][DM_N];
static double dm_corr(const int32_t *a, const int32_t *b)
{
    double sa = 0, sb = 0, sab = 0, va = 0, vb = 0;
    uint32_t i;
    for (i = 0; i < DM_N; i++) { sa += a[i]; sb += b[i]; }
    sa /= DM_N; sb /= DM_N;
    for (i = 0; i < DM_N; i++) {
        double x = a[i] - sa, y = b[i] - sb;
        va += x * x; vb += y * y; sab += x * y;
    }
    return va > 0 && vb > 0 ? sab / sqrt(va * vb) : 0.0;
}
static void dm_render(uint32_t mode, int32_t drive)
{
    track_t tt;
    ins_st_t st;
    int32_t buf[DM_N];
    uint32_t i;
    memset(&tt, 0, sizeof tt);
    memset(&st, 0, sizeof st);
    tt.p[P_IDMODE] = (int16_t)mode;
    tt.p[P_DIST] = (int16_t)drive;
    tt.p[P_ED_FX] = 0;                                 /* TONE at its default: the drive-linked cutoff alone */
    for (i = 0; i < DM_N; i++)
        buf[i] = (int32_t)(24000.0 * sin(2.0 * M_PI * (double)i / 128.0));
    ins_run_dist(&st, &tt, buf, DM_N, 32768);          /* MIX 100 % (ins_mixq(127)), from rest */
    for (i = 0; i < DM_N; i++)
        buf[i] = (int32_t)(24000.0 * sin(2.0 * M_PI * (double)(i + DM_N) / 128.0));
    ins_run_dist(&st, &tt, buf, DM_N, 32768);
    for (i = 0; i < DM_N; i++)
        dm_wet[mode][i] = buf[i];
}
static void test_dist_modes(void)
{
    static const char *const NM[IDM_N] = {"SOFT", "HARD", "FOLD", "FUZZ"};
    static const int32_t DR[3] = {64, 96, 127};
    double dm_rms[IDM_N];
    uint32_t d, m, k;
    for (d = 0; d < 3u; d++) {
        double worst = 0, hi = 0, lo = 1e30, dc = 0;
        for (m = 0; m < IDM_N; m++) {
            double e = 0, s = 0;
            dm_render(m, DR[d]);
            for (k = 0; k < DM_N; k++) { e += (double)dm_wet[m][k] * dm_wet[m][k]; s += dm_wet[m][k]; }
            dm_rms[m] = sqrt(e / DM_N);
            dc = fabs(s / DM_N) > dc ? fabs(s / DM_N) : dc;
            hi = dm_rms[m] > hi ? dm_rms[m] : hi;
            lo = dm_rms[m] < lo ? dm_rms[m] : lo;
        }
        printf("inschain:   DIST MODEs at DRIVE %3d, RMS", DR[d]);
        for (m = 0; m < IDM_N; m++)
            printf(" %s %6.0f", NM[m], dm_rms[m]);
        printf(" (worst DC %.0f)\n", dc);
        for (m = 0; m < IDM_N; m++)
            for (k = m + 1u; k < IDM_N; k++) {
                double c = dm_corr(dm_wet[m], dm_wet[k]);
                worst = c > worst ? c : worst;
                printf("inschain:     %s/%s correlation %.5f\n", NM[m], NM[k], c);
            }
        {
            /* the bar tightens with the drive: at DRIVE 64 every shape still shares the input's
             * fundamental (a rounded sine vs a square correlate ~0.95); the bug to catch is the
             * convergence at the top of the range, where they used to be sample-identical */
            static const double LIM[3] = {0.97, 0.95, 0.90};
            char w[96];
            snprintf(w, sizeof w, "DIST MODE %d: every pair distinct (correlation < %.2f)", DR[d], LIM[d]);
            ck(w, worst < LIM[d]);
        }
        if (DR[d] == 127) {
            char w[96];
            snprintf(w, sizeof w, "DIST MODE maxed: no level jump (RMS within 6 dB), no DC (< 2 %% of RMS)");
            ck(w, hi < lo * 2.0 && dc < hi * 0.02);
        }
    }
}

static void test_fx_reservations(void)
{
    uint32_t sel;
    fresh();
    pk_build_mdst(&trk[0], MD_OFF, &sel);
    ck("FX list has continuous delay/reverb controls and explicit MIX", pick_has(MD_DTIME, "TIME") &&
       pick_has(MD_DFB, "FDBK") && pick_has(MD_DCOLOR, "COLOR") && pick_has(MD_RSIZE, "SIZE") &&
       pick_has(MD_RDAMP, "DAMP") && pick_has(MD_DLY, "MIX") && pick_has(MD_REV, "MIX"));
    trk[0].p[P_M1DST] = MD_RSIZE;
    pk_build_mdst(&trk[1], MD_OFF, &sel);
    ck("a stored SEND assignment hides its control on other tracks", !pick_has(MD_RSIZE, 0));
    pk_build_auto(&trk[1], MFX_SIZE, NSTEP, &sel);
    ck("the SEND assignment also reserves the lock destination", !pick_has(MFX_SIZE, 0));
    trk[0].p[P_M1DST] = MD_OFF;
    ck("shared reverb SIZE accepts a parameter lock", !motion_set_lock(&trk[0], 0, MFX_SIZE, 90));
    pk_build_mdst(&trk[1], MD_OFF, &sel);
    ck("a SEND lock reserves the LFO destination", !pick_has(MD_RSIZE, 0));
    motion_clear_locks(&trk[0], NSTEP);
    pk_build_mdst(&trk[1], MD_OFF, &sel);
    ck("removing the lock releases the shared control", pick_has(MD_RSIZE, 0));
    song.g[G_REVOWN] = 1;
    trk[0].p[P_M1DST] = MD_RSIZE;
    pk_build_mdst(&trk[1], MD_OFF, &sel);
    ck("private CHAIN reverb does not reserve the SEND bank", pick_has(MD_RSIZE, 0));
}

static void test_retired_delay_mix(void)
{
    uint32_t legacy, block, i;
    for (legacy = 0; legacy < 2; legacy++) {
        int32_t peak = 0;
        fresh();
        song.g[G_DTIME] = DTIME_FREE + 100;
        song.g[G_DFDBK] = 0;
        song.g[G_TSPEED] = legacy ? 127 : 100;
        for (block = 0; block < FS / CTL / 5u; block++) {
            int32_t d[CTL] = {0}, r[CTL] = {0}, w[CTL];
            if (!block) d[0] = 24000;
            fx_buses(d, r, w, CTL);
            for (i = 0; i < CTL; i++) if (w[i] > peak) peak = w[i];
        }
        ck("delay SEND impulse returns at full level regardless of retired MIX", peak == 24000);
    }
}

static void test_fx_bank_switch(void)
{
    fresh();
    song.g[G_RSIZE] = 30;
    song.g[G_CRSIZE] = 90;
    trk[1].p[P_PAN] = -10;
    ck("bank switch setup creates a SEND size lock", !motion_set_lock(&trk[0], 0, MFX_SIZE, 120));
    ck("bank switch setup creates independent pan automation", !motion_set_event(&trk[1], 0, P_PAN, 40));
    motion_begin();
    motion_step(&trk[0], 0, &motion, 1);
    motion_step(&trk[1], 0, &motion, 1);
    bus_own_set(1, 0);
    ck("SEND to CHAIN restores the old bank and preserves the new base",
       song.g[G_RSIZE] == 30 && song.g[G_CRSIZE] == 90 && trk[1].p[P_PAN] == 40);
    motion_step(&trk[0], 0, &motion, 1);
    ck("the size lock follows the selected CHAIN bank", song.g[G_CRSIZE] == 120 && song.g[G_RSIZE] == 30);
    motion_step(&trk[0], 1, &motion, 1);
    ck("the CHAIN lock restores its own bank's base", song.g[G_CRSIZE] == 90);
    motion_end();
}

static void test_fx_edit_paths(void)
{
    uint32_t i;
    int16_t value;
    fresh();
    ui_power_on();
    song.g[G_DTIME] = 1;
    ck("edit setup creates a TIME lock", !motion_set_lock(TSEL, 0, MFX_TIME, DTIME_FREE + 100));
    motion_begin();
    motion_step(TSEL, 0, &motion, 1);
    for (i = 0; i < NPAGES; i++) if (PAGES[i].graph == GR_MOD) break;
    ui.home = 0; ui.page = i; mod_ui_slot = 0;
    edit_param(1, 1);
    motion_end();
    ck("editing a MOD source preserves the delay RATE base", song.g[G_DTIME] == 1);

    fresh();
    ui_power_on();
    ins_send_set(TSEL, INS_FILTER, 1);
    ck("edit setup creates a shared FILTER cut lock", !motion_set_lock(TSEL, 0, P_IFCUT, 100));
    motion_begin();
    motion_step(TSEL, 0, &motion, 1);
    go_title("FILTER");
    edit_param(1, -1);
    value = fx_bus_trk.p[P_IFCUT];
    motion_end();
    ck("editing a shared SEND knob updates its motion base", fx_bus_trk.p[P_IFCUT] == value);

    fresh();
    ui_power_on();
    fx_bus_trk.p[P_SLRATE] = 3;
    TSEL->p[P_SLRATE] = 8;
    ck("edit setup creates a shared SLICER rate lock", !motion_set_lock(TSEL, 0, P_SLRATE, 5));
    motion_begin();
    motion_step(TSEL, 0, &motion, 1);
    {
        uint8_t order[FX_LIST_N];
        fx_list_get(TSEL, order);
        for (i = 0; i < FX_LIST_N && order[i] != INS_SLCR; i++) ;
        ui.ior_sel = i;
    }
    ior_knob(2, -1);                                /* left: this track takes the SLICER chain */
    ck("SLICER mode switch restores the shared rate before selecting CHAIN",
       sl_trk_chain(TSEL) && fx_bus_trk.p[P_SLRATE] == 3 && TSEL->p[P_SLRATE] == 8);
    motion_step(TSEL, 0, &motion, 1);
    motion_end();
    ck("SLICER lock restores its private rate after switching", TSEL->p[P_SLRATE] == 8);
}

/* Current dev (d035bf7): deep SEND chorus consumers read the preceding consumer's line writes. */
static void test_shared_chorus(void)
{
    uint32_t k;
    fresh();
    for (k = 0; k < NPART; k++) {
        clear_stages(&trk[k]);
        trk[k].p[P_CHOR] = 0;
    }
    for (k = 0; k < 2u; k++) {
        ins_send_set(&trk[k], INS_CHOR, 1);
        trk[k].p[P_IHMIX] = 127;
        trk_note_on(&trk[k], 60 + 7 * k, 110);
    }
    fx_bus_trk.p[P_IHDEP] = 127;
    fx_bus_trk.p[P_IHRATE] = 0;
    ins_st[ins_bus_tr(INS_CHOR)][INS_CHOR].w = 32768;
    ins_st[ins_bus_tr(INS_CHOR)][INS_CHOR].ph = 0x40000000u;
    ck("deep shared chorus preserves dev's sequential render", hash_out(1) == 0x03a45cf4814f7ccdull);
}

int main(void)
{
    track_t *t;
    uint8_t ord[INS_N];
    uint64_t dry, wet, bypass, reordered;
    uint32_t i;

    printf("inschain_test: P_COUNT %u P_E0 %u INS_N %u\n", (unsigned)P_COUNT, (unsigned)P_E0, (unsigned)INS_N);
    test_shared_chorus();
    test_tape();
    test_master_insert();
    test_master_shared();
    test_master_swept_tail();
    test_master_bus_headroom();
    test_master_slicer_restart();
    ck("param layout: insert chain before FTYPE/ESYNC/E0",
       P_SPRD + 1 == P_IORD && P_IHMIX + 1 == P_FTYPE && P_ESYNC + 1 == P_E0 && P_COUNT == 127u && P_E0 == 119u);

    fresh();
    t = &trk[0];
    for (i = P_IORD; i < P_FTYPE; i++)
        t->p[i] = TP[i].def;
    t->p[P_IORD] = 0;
    t->p[P_IDMIX] = t->p[P_IFMIX] = t->p[P_ICMIX] = t->p[P_IPMIX] = t->p[P_ILMIX] = t->p[P_IHMIX] = 0;
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;

    /* hold a note so the chain has audio to process */
    trk_note_on(t, 60, 100);
    render_blocks(8);
    dry = hash_out(32);

    /* MIX 0 on every stage: same as dry (bypass / skip DSP), even with drive/cut set */
    fresh();
    t = &trk[0];
    for (i = P_IORD; i < P_FTYPE; i++)
        t->p[i] = TP[i].def;
    t->p[P_IORD] = 0;
    t->p[P_IDMODE] = IDM_SOFT;
    t->p[P_DIST] = 100;
    t->p[P_IFTYPE] = IFT_LP;
    t->p[P_IFCUT] = 40;
    t->p[P_IDMIX] = t->p[P_IFMIX] = t->p[P_ICMIX] = t->p[P_IPMIX] = t->p[P_ILMIX] = t->p[P_IHMIX] = 0;
    t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    trk_note_on(t, 60, 100);
    render_blocks(8);
    bypass = hash_out(32);
    ck("MIX 0 on all stages: bit-identical to dry (stages skipped)", dry == bypass);

    /* DIST wet changes the hash */
    fresh();
    t = &trk[0];
    t->p[P_IDMODE] = IDM_HARD;
    t->p[P_DIST] = 100;
    t->p[P_IDMIX] = 127;
    trk_note_on(t, 60, 100);
    render_blocks(8);
    wet = hash_out(32);
    ck("DIST at MIX 127 changes the render vs dry", wet != dry);
    fresh();
    t = &trk[0];
    t->p[P_IDMODE] = IDM_HARD;
    t->p[P_DIST] = 100;
    t->p[P_IDMIX] = 127;
    ins_send_set(t, INS_DIST, 1);
    {
        uint8_t o[INS_N] = {INS_FILTER, INS_DIST, INS_CRUSH, INS_PHASR, INS_FLANG, INS_CHOR};
        ins_order_set(t, o);
    }
    ins_order_get(t, ord);
    ck("DIST as a send stays a send when the chain is reordered",
       ins_is_send(t, INS_DIST) && !ins_is_send(t, INS_FILTER) && ord[0] == INS_FILTER && ord[1] == INS_DIST);
    trk_note_on(t, 60, 100);
    render_blocks(8);
    ck("DIST as a send sounds different from DIST in the chain", hash_out(32) != wet);
    fresh();
    t = &trk[0];
    t->p[P_IDMODE] = IDM_HARD;
    t->p[P_DIST] = 100;
    t->p[P_IDMIX] = t->p[P_IFMIX] = t->p[P_ICMIX] = t->p[P_IPMIX] = t->p[P_ILMIX] = t->p[P_IHMIX] = 0;
    t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    ins_send_set(t, INS_DIST, 1);
    trk_note_on(t, 60, 100);
    render_blocks(8);
    ck("a send at MIX 0 is silent, the same as bypass", hash_out(32) == bypass);
    fresh();
    t = &trk[0];
    t->p[P_IDMODE] = IDM_HARD;
    t->p[P_DIST] = 100;
    t->p[P_IDMIX] = 127;
    t->p[P_ED_FX] = -64;
    trk_note_on(t, 60, 100);
    render_blocks(8);
    ck("DIST TONE moves the render (offset on the drive-linked cutoff)", hash_out(32) != wet);

    /* order: put CRUSH before DIST; with both wet, order matters */
    fresh();
    t = &trk[0];
    t->p[P_IDMODE] = IDM_SOFT;
    t->p[P_DIST] = 90;
    t->p[P_IDMIX] = 127;
    t->p[P_ICBITS] = 40;
    t->p[P_ICRATE] = 20;
    t->p[P_ICLPF] = 80;
    t->p[P_ICMIX] = 127;
    t->p[P_IORD] = 0;                               /* identity: DIST, FILTER, CRUSH, ... */
    trk_note_on(t, 60, 100);
    render_blocks(8);
    wet = hash_out(32);

    fresh();
    t = &trk[0];
    t->p[P_IDMODE] = IDM_SOFT;
    t->p[P_DIST] = 90;
    t->p[P_IDMIX] = 127;
    t->p[P_ICBITS] = 40;
    t->p[P_ICRATE] = 20;
    t->p[P_ICLPF] = 80;
    t->p[P_ICMIX] = 127;
    {
        uint8_t o[INS_N] = {INS_CRUSH, INS_DIST, INS_FILTER, INS_PHASR, INS_FLANG, INS_CHOR};
        ins_order_set(t, o);
    }
    ins_order_get(t, ord);
    ck("ins_order_set/get: CRUSH first in chain", ord[0] == INS_CRUSH && ord[1] == INS_DIST);
    {   /* Lehmer round-trip for every adjacent swap (the old 5!..0! table only survived DIST↔FILTER) */
        uint8_t base[INS_N] = {0, 1, 2, 3, 4, 5}, got[INS_N];
        uint32_t h, ok_all = 1;
        for (h = 0; h + 1u < INS_N; h++) {
            uint8_t o[INS_N];
            uint32_t k;
            for (k = 0; k < INS_N; k++)
                o[k] = base[k];
            { uint8_t tmp = o[h]; o[h] = o[h + 1u]; o[h + 1u] = tmp; }
            ins_order_set(t, o);
            ins_order_get(t, got);
            for (k = 0; k < INS_N; k++)
                ok_all &= got[k] == o[k];
        }
        ck("ins_order_set/get: every adjacent swap round-trips (Lehmer 0!..5!)", ok_all);
    }
    {
        uint8_t o[INS_N] = {INS_CRUSH, INS_DIST, INS_FILTER, INS_PHASR, INS_FLANG, INS_CHOR};
        ins_order_set(t, o);                            /* restore after the round-trip probe */
    }
    trk_note_on(t, 60, 100);
    render_blocks(8);
    reordered = hash_out(32);
    ck("CRUSH-before-DIST order changes the render vs DIST-before-CRUSH", reordered != wet);

    /* migration from 1.5 INSERT */
    {
        int16_t old[111], got[P_COUNT], def[P_COUNT];
        for (i = 0; i < P_COUNT; i++)
            def[i] = TP[i].def;
        for (i = 0; i < 111u; i++)
            old[i] = 0;
        old[P_DIST] = 80;
        old[96] = 5;                                    /* IT_CRUSH */
        old[97] = 50; old[98] = 60; old[99] = 70; old[100] = 100;
        old[101] = 2;                                   /* HP */
        old[102] = 1;                                   /* ESYNC on */
        for (i = 0; i < 8u; i++)
            old[103 + i] = (int16_t)(10 + i);
        params_by_count(got, old, 111u, def);
        ins_migrate(got, 111u);
        ck("migrate: CRUSH params + MIX from 1.5 INSERT",
           got[P_ICBITS] == 50 && got[P_ICRATE] == 60 && got[P_ICLPF] == 70 && got[P_ICMIX] == 100);
        ck("migrate: old P_DIST kept as drive; DIST MIX on",
           got[P_DIST] == 80 && got[P_IDMIX] == 127);
        old[96] = 2;                                    /* HARD */
        old[97] = 80;                                   /* drive */
        old[98] = 40;                                   /* TONE cutoff */
        old[100] = 90;
        params_by_count(got, old, 111u, def);
        ins_migrate(got, 111u);
        ck("migrate: 1.5 DIST TONE comes back as an offset on the drive-linked cutoff",
           got[P_IDMODE] == IDM_HARD && got[P_DIST] == 80 && got[P_IDMIX] == 90 && got[P_ED_FX] == 40 - (96 - (80 >> 2)));
        ck("migrate: FTYPE/ESYNC and engine values moved",
           got[P_FTYPE] == 2 && got[P_ESYNC] == 1 && got[P_E0] == 10 && got[P_E7] == 17);
        old[96] = 0;                                    /* TYPE OFF */
        old[100] = 64;
        params_by_count(got, old, 111u, def);
        ins_migrate(got, 111u);
        ck("migrate: TYPE OFF leaves stage MIX at 0 (dry)",
           got[P_ICMIX] == 0 && got[P_IPMIX] == 0 && got[P_ILMIX] == 0 && got[P_IHMIX] == 0 &&
           (got[P_DIST] ? got[P_IDMIX] == 127 : got[P_IDMIX] == 0));
        ck("migrate: factory order is IORD_DEF (CRUSH before FILTER)",
           got[P_IORD] == (int16_t)IORD_DEF);
        old[96] = 6;                                    /* PHASR */
        old[97] = 30; old[98] = 40; old[99] = 50; old[100] = 80;
        params_by_count(got, old, 111u, def);
        ins_migrate(got, 111u);
        ck("migrate: PHASR RATE/DEP/FBK/MIX",
           got[P_IPRATE] == 30 && got[P_IPDEP] == 40 && got[P_IPFBK] == 50 && got[P_IPMIX] == 80);
        old[96] = 7;                                    /* FLANG */
        old[97] = 33; old[98] = 44; old[100] = 55;
        params_by_count(got, old, 111u, def);
        ins_migrate(got, 111u);
        ck("migrate: FLANG RATE/DEP/MIX",
           got[P_ILRATE] == 33 && got[P_ILDEP] == 44 && got[P_ILMIX] == 55);
        old[96] = 8;                                    /* CHOR */
        old[97] = 22; old[98] = 33; old[100] = 44;
        params_by_count(got, old, 111u, def);
        ins_migrate(got, 111u);
        ck("migrate: CHOR RATE/DEP/MIX",
           got[P_IHRATE] == 22 && got[P_IHDEP] == 33 && got[P_IHMIX] == 44);
    }

    /* Exclusive DLY CHAIN: owner mixes in track_insert; others' sends ignored; SEND restores bus. */
    {
        uint64_t send_h, chain_h, used_h;
        fresh();
        t = &trk[0];
        song.g[G_DLYOWN] = 0;
        song.g[G_DTIME] = 1;
        song.g[G_DFDBK] = 80;
        song.g[G_TSPEED] = 100;
        t->p[P_DLY] = 100;
        t->p[P_REV] = 0;
        trk[1].p[P_DLY] = 100;
        trk[1].p[P_REV] = 0;
        trk_note_on(t, 60, 100);
        render_blocks(16);
        send_h = hash_out(48);
        fresh();
        t = &trk[0];
        song.g[G_DLYOWN] = 1;                           /* track 0 owns delay as CHAIN */
        song.g[G_DTIME] = 1;
        song.g[G_DFDBK] = 80;
        song.g[G_TSPEED] = 100;
        t->p[P_DLY] = 100;
        t->p[P_REV] = 0;
        trk_note_on(t, 60, 100);
        render_blocks(16);
        chain_h = hash_out(48);
        ck("delay CHAIN on owner changes the render vs SEND bus", chain_h != send_h);
        fresh();
        t = &trk[0];
        song.g[G_DLYOWN] = 2;                           /* track 1 owns; track 0 is USED */
        song.g[G_DTIME] = 1;
        song.g[G_DFDBK] = 80;
        song.g[G_TSPEED] = 100;
        t->p[P_DLY] = 100;
        t->p[P_REV] = 0;
        trk_note_on(t, 60, 100);
        render_blocks(16);
        used_h = hash_out(48);
        fresh();
        t = &trk[0];
        song.g[G_DLYOWN] = 2;
        song.g[G_DTIME] = 1;
        song.g[G_DFDBK] = 80;
        song.g[G_TSPEED] = 100;
        t->p[P_DLY] = 0;                                /* USED: send amount ignored anyway */
        t->p[P_REV] = 0;
        trk_note_on(t, 60, 100);
        render_blocks(16);
        ck("USED track: delay send ignored (same as mix 0 while another owns CHAIN)",
           used_h == hash_out(48));
        ck("modes: SEND / CHAIN / USED strings",
           fx_list_mode(&trk[0], INS_DLY)[0] == 'U' && song.g[G_DLYOWN] == 2);
        bus_own_set(0, -1);
        ck("release ownership restores SEND for all",
           song.g[G_DLYOWN] == 0 && fx_list_mode(&trk[0], INS_DLY)[0] == 'S' &&
           fx_list_mode(&trk[1], INS_DLY)[0] == 'S');
        /* REV CHAIN: owner CHAIN, others SEND (not USED); DLY stays USED when chained. */
        song.g[G_REVOWN] = 1;
        song.g[G_DLYOWN] = 2;
        ck("REV chained: owner CHAIN, others SEND; DLY non-owner USED",
           fx_list_mode(&trk[0], INS_REV)[0] == 'C' && fx_list_mode(&trk[1], INS_REV)[0] == 'S' &&
           fx_list_mode(&trk[0], INS_DLY)[0] == 'U' && fx_list_mode(&trk[1], INS_DLY)[0] == 'C');
        /* CHAIN tank has its own TYPE/SIZE/DAMP; SEND keeps G_R*. */
        {
            uint64_t h_send, h_chain;
            song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 90; song.g[G_RDAMP] = 60;
            song.g[G_CRTYPE] = 0; song.g[G_CRSIZE] = 40; song.g[G_CRDAMP] = 100;
            song.g[G_REVOWN] = 1;
            t = &trk[0];
            t->p[P_REV] = 100; t->p[P_DLY] = 0;
            trk[1].p[P_REV] = 0; trk[1].p[P_DLY] = 0;
            trk_note_on(t, 60, 100);
            render_blocks(24);
            h_chain = hash_out(48);
            song.g[G_CRTYPE] = 2; song.g[G_CRSIZE] = 90; song.g[G_CRDAMP] = 60; /* match SEND */
            fresh();
            t = &trk[0];
            song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 90; song.g[G_RDAMP] = 60;
            song.g[G_CRTYPE] = 2; song.g[G_CRSIZE] = 90; song.g[G_CRDAMP] = 60;
            song.g[G_REVOWN] = 1;
            t->p[P_REV] = 100; t->p[P_DLY] = 0;
            trk_note_on(t, 60, 100);
            render_blocks(24);
            h_send = hash_out(48);
            ck("CHAIN reverb params diverge from SEND (ROOM/40 vs HALL/90 changes render)",
               h_chain != h_send);
            ck("GP has CHAIN TYPE/SIZE/DAMP",
               G_COUNT == 31u && GP[G_CRTYPE].names == GP[G_RTYPE].names && GP[G_CRSIZE].max == 127);
        }
        bus_own_set(1, -1);
        bus_own_set(0, -1);
    }

    /* FX pages: EFFECTS (incl. SLICER), REVERB, DLY, DIST, …; Punch-in. MASTER on GLO.
     * Stage pages have no mix knob: the percent is EFFECTS K4. No bus CHORUS page.
     * SLICER is folded into EFFECTS (FX_SEC_INS); MIX is P_SLDEPTH (no DEPTH knob on the page). */
    {
        uint32_t i, pr = NPAGES, prev = NPAGES, ps = NPAGES, pd = NPAGES, pdl = NPAGES, pm = NPAGES;
        uint32_t mix = 0, chor = 0;
        static const uint8_t MIXK[] = {P_IDMIX, P_IFMIX, P_ICMIX, P_IPMIX, P_ILMIX, P_IHMIX, P_SLDEPTH};
        for (i = 0; i < NPAGES; i++) {
            uint32_t k, m;
            if (str_eq(PAGES[i].title, "EFFECTS") && PAGES[i].fam == FAM_FX && pr == NPAGES) pr = i;
            if (str_eq(PAGES[i].title, "REVERB") && PAGES[i].fam == FAM_FX && prev == NPAGES) prev = i;
            if (str_eq(PAGES[i].title, "SLICER") && PAGES[i].fam == FAM_FX && ps == NPAGES) ps = i;
            if (str_eq(PAGES[i].title, "DISTORT") && PAGES[i].fam == FAM_FX && pd == NPAGES) pd = i;
            if (str_eq(PAGES[i].title, "DELAY") && PAGES[i].fam == FAM_FX && pdl == NPAGES) pdl = i;
            if (str_eq(PAGES[i].title, "MASTER") && pm == NPAGES) pm = i;
            if (str_eq(PAGES[i].title, "CHORUS") && PAGES[i].scope == SC_GLOBAL) chor = 1;
            if (PAGES[i].fam == FAM_FX && PAGES[i].graph == GR_INS)
                for (k = 0; k < 4u; k++)
                    for (m = 0; m < sizeof MIXK; m++)
                        mix |= PAGES[i].id[k] == MIXK[m];
        }
        ck("FX pages: EFFECTS, REVERB, DELAY, DISTORT…; SLICER in EFFECTS; MASTER on GLO",
           pr < prev && prev < pdl && pdl < pd && pd < ps && PAGES[pr].graph == GR_IORD &&
           PAGES[pr].sec == FX_SEC_INS && PAGES[pdl].sec == FX_SEC_INS &&
           PAGES[ps].sec == FX_SEC_INS && PAGES[ps].id[3] == 0xFFu &&
           str_eq(TP[P_SLDEPTH].label, "MIX") && FX_NSEC == 2u &&
           PAGES[pm].fam == FAM_GLO && PAGES[pm].sec == GLO_SEC_MASTER &&
           PAGES[pd].id[2] == P_ED_FX && PAGES[pd].id[3] == 0xFFu && !mix &&
           PAGES[pdl].id[3] == 0xFFu && PAGES[pdl].id[0] == G_DTIME && !chor);
        {
            uint32_t pc = NPAGES, pf = NPAGES;
            for (i = 0; i < NPAGES; i++) {
                if (str_eq(PAGES[i].title, "CRUSH") && PAGES[i].fam == FAM_FX && pc == NPAGES) pc = i;
                if (str_eq(PAGES[i].title, "FILTER") && PAGES[i].fam == FAM_FX &&
                    PAGES[i].graph == GR_INS && pf == NPAGES)
                    pf = i;
            }
            ck("FX pages: CRUSH before FILTER (after DISTORT)", pd < pc && pc < pf && pf < ps);
        }
        {
            uint8_t list[FX_LIST_N], defo[INS_N];
            fx_list_get(&trk[0], list);
            ck("EFFECTS list is 10: inserts + DLY + REV + SLCR + TAPE",
               FX_LIST_N == 10u && list[6] == INS_DLY && list[7] == INS_REV && list[8] == INS_SLCR &&
               list[9] == INS_TAPE && str_eq(N_ISTAGE[INS_SLCR], "SLICER") && str_eq(N_ISTAGE[INS_TAPE], "TAPE"));
            fresh();
            t = &trk[0];
            ck("factory P_IORD is IORD_DEF (DIST→CRUSH→FILTER→…)",
               (uint16_t)t->p[P_IORD] == IORD_DEF && TP[P_IORD].def == (int16_t)IORD_DEF);
            ins_order_get(t, defo);
            ck("factory chain order: DIST, CRUSH, FILTER, PHASR, FLANG, CHOR",
               defo[0] == INS_DIST && defo[1] == INS_CRUSH && defo[2] == INS_FILTER &&
               defo[3] == INS_PHASR && defo[4] == INS_FLANG && defo[5] == INS_CHOR);
            fx_list_get(t, list);
            ck("factory EFFECTS list: DIST…CHOR, DLY, REV, SLCR, TAPE; SLCR default SEND",
               list[0] == INS_DIST && list[1] == INS_CRUSH && list[2] == INS_FILTER &&
               list[5] == INS_CHOR && list[6] == INS_DLY && list[7] == INS_REV && list[8] == INS_SLCR &&
               list[9] == INS_TAPE && str_eq(fx_list_mode(t, INS_SLCR), "SEND") &&
               str_eq(fx_list_mode(t, INS_TAPE), "SEND") && ins_mix_of(t, INS_SLCR) == t->p[P_SLDEPTH]);
            {
                uint8_t before[FX_LIST_N], after[FX_LIST_N];
                uint32_t sl = 8;
                fx_list_get(t, before);
                ck("SLICER SEND→CHAIN→SEND per track",
                   sl_trk_send(t) && fx_list_mode(t, INS_SLCR)[0] == 'S');
                bus_own_set(2, 0);                       /* track 0 CHAIN, others SEND */
                ck("SLICER CHAIN on track 0; track 1 still SEND",
                   sl_trk_chain(t) && fx_list_mode(t, INS_SLCR)[0] == 'C' &&
                   sl_trk_send(&trk[1]) && fx_list_mode(&trk[1], INS_SLCR)[0] == 'S');
                bus_own_set(2, -1);
                ck("SLICER all-SEND restores SEND on every track",
                   sl_trk_send(t) && sl_trk_send(&trk[1]) && fx_list_mode(&trk[1], INS_SLCR)[0] == 'S');
                fx_list_get(t, before);
                fx_list_move(t, sl, -1);                /* SLCR past REV */
                fx_list_get(t, after);
                ck("SLICER reorderable in EFFECTS list (swap with REV)",
                   before[8] == INS_SLCR && after[7] == INS_SLCR && after[8] == INS_REV);
            }
        }
    }

    ck("ANALOG FILTER TYPE includes COMB", NELEM(N_FTYPE) == 4u && str_eq(N_FTYPE[3], "COMB"));

    /* Test A: two tracks SEND FILTER share one CUT; a CHAIN track keeps a private CUT. */
    {
        uint64_t h_lo, h_hi, h_a, h_b, h_chain_lo, h_chain_hi;
        fresh();
        filter_setup(&trk[0], IFT_LP, 64, 40, 127);
        filter_setup(&trk[1], IFT_LP, 64, 40, 127);
        ins_send_set(&trk[0], INS_FILTER, 1);
        ins_send_set(&trk[1], INS_FILTER, 1);
        fx_bus_trk.p[P_IFTYPE] = IFT_LP;
        fx_bus_trk.p[P_IFRES] = 40;
        fx_bus_trk.p[P_IFCUT] = 32;
        trk_note_on(&trk[0], 60, 100);
        trk_note_on(&trk[1], 60, 100);
        render_blocks(8);
        h_lo = hash_out(48);
        fresh();
        filter_setup(&trk[0], IFT_LP, 64, 40, 127);
        filter_setup(&trk[1], IFT_LP, 64, 40, 127);
        ins_send_set(&trk[0], INS_FILTER, 1);
        ins_send_set(&trk[1], INS_FILTER, 1);
        fx_bus_trk.p[P_IFTYPE] = IFT_LP;
        fx_bus_trk.p[P_IFRES] = 40;
        fx_bus_trk.p[P_IFCUT] = 110;
        trk_note_on(&trk[0], 60, 100);
        trk_note_on(&trk[1], 60, 100);
        render_blocks(8);
        h_hi = hash_out(48);
        ck("SEND FILTER: shared bus CUT moves both tracks' render", h_lo != h_hi);
        /* track-local P_IFCUT must not drive the SEND bus */
        fresh();
        filter_setup(&trk[0], IFT_LP, 110, 40, 127);
        filter_setup(&trk[1], IFT_LP, 110, 40, 127);
        ins_send_set(&trk[0], INS_FILTER, 1);
        ins_send_set(&trk[1], INS_FILTER, 1);
        fx_bus_trk.p[P_IFTYPE] = IFT_LP;
        fx_bus_trk.p[P_IFRES] = 40;
        fx_bus_trk.p[P_IFCUT] = 32;
        trk_note_on(&trk[0], 60, 100);
        trk_note_on(&trk[1], 60, 100);
        render_blocks(8);
        h_a = hash_out(48);
        fresh();
        filter_setup(&trk[0], IFT_LP, 32, 40, 127);
        filter_setup(&trk[1], IFT_LP, 32, 40, 127);
        ins_send_set(&trk[0], INS_FILTER, 1);
        ins_send_set(&trk[1], INS_FILTER, 1);
        fx_bus_trk.p[P_IFTYPE] = IFT_LP;
        fx_bus_trk.p[P_IFRES] = 40;
        fx_bus_trk.p[P_IFCUT] = 32;
        trk_note_on(&trk[0], 60, 100);
        trk_note_on(&trk[1], 60, 100);
        render_blocks(8);
        h_b = hash_out(48);
        ck("SEND FILTER: track P_IFCUT ignored (bus CUT is the source)", h_a == h_b);
        /* CHAIN track: private CUT from P_IFCUT; bus CUT ignored */
        fresh();
        filter_setup(&trk[0], IFT_LP, 32, 40, 127);
        ins_send_set(&trk[0], INS_FILTER, 0);
        fx_bus_trk.p[P_IFCUT] = 110;
        trk_note_on(&trk[0], 60, 100);
        render_blocks(8);
        h_chain_lo = hash_out(48);
        fresh();
        filter_setup(&trk[0], IFT_LP, 110, 40, 127);
        ins_send_set(&trk[0], INS_FILTER, 0);
        fx_bus_trk.p[P_IFCUT] = 32;
        trk_note_on(&trk[0], 60, 100);
        render_blocks(8);
        h_chain_hi = hash_out(48);
        ck("CHAIN FILTER: private CUT moves; bus CUT ignored", h_chain_lo != h_chain_hi);
    }

    /* UI: FILTER page knobs edit the SEND bus when the stage is SEND; track P_* when CHAIN. MIX stays track. */
    {
        const page_t *pg = 0;
        int16_t *vp = 0;
        uint32_t i;
        for (i = 0; i < NPAGES; i++)
            if (str_eq(PAGES[i].title, "FILTER") && PAGES[i].fam == FAM_FX && PAGES[i].graph == GR_INS) {
                pg = &PAGES[i];
                break;
            }
        fresh();
        ins_send_set(&trk[0], INS_FILTER, 1);
        song.sel = 0;
        fx_bus_trk.p[P_IFCUT] = 40;
        trk[0].p[P_IFCUT] = 90;
        trk[0].p[P_IFMIX] = 55;
        ck("FILTER page found for SEND/CHAIN knob binding", pg != 0);
        if (pg) {
            page_desc(pg, 1, &vp);
            ck("SEND FILTER: CUT knob binds the bus", vp == &fx_bus_trk.p[P_IFCUT]);
            ins_send_set(&trk[0], INS_FILTER, 0);
            page_desc(pg, 1, &vp);
            ck("CHAIN FILTER: CUT knob binds the track", vp == &trk[0].p[P_IFCUT]);
            /* MIX is on EFFECTS, not this page; IDMIX binding would stay track if present */
            ck("FILTER MIX stays on the track even when SEND", trk[0].p[P_IFMIX] == 55 &&
               fx_bus_trk.p[P_IFMIX] != 55);
        }
    }

    /* insert FILTER: CUT is Q8 into tsvf_coef (same as ANALOG). A low CUT must muffle a bright saw;
     * RES must raise a band-pass peak. Regression for the raw 0..127 cutoff bug (wet ≈ silence). */
    {
        int64_t e_lo, e_hi, e_res0, e_res;
        fresh();
        t = &trk[0];
        filter_setup(t, IFT_LP, 32, 40, 114);           /* ~90 % mix, dark CUT */
        trk_note_on(t, 60, 100);
        e_lo = energy_out(16, 48);
        fresh();
        t = &trk[0];
        filter_setup(t, IFT_LP, 110, 40, 114);          /* open CUT */
        trk_note_on(t, 60, 100);
        e_hi = energy_out(16, 48);
        ck("FILTER LP: low CUT muffles vs open CUT (energy)",
           e_lo > 0 && e_hi > e_lo * 2);
        printf("inschain:   LP energy CUT 32 / 110: %lld / %lld\n",
               (long long)e_lo, (long long)e_hi);

        fresh();
        t = &trk[0];
        filter_setup(t, IFT_BP, 64, 0, 127);
        trk_note_on(t, 72, 100);
        e_res0 = energy_out(16, 48);
        fresh();
        t = &trk[0];
        filter_setup(t, IFT_BP, 64, 110, 127);
        trk_note_on(t, 72, 100);
        e_res = energy_out(16, 48);
        ck("FILTER BP: RES raises energy at the cutoff",
           e_res0 > 0 && e_res > e_res0 + e_res0 / 4);
        printf("inschain:   BP energy RES 0 / 110: %lld / %lld\n", (long long)e_res0, (long long)e_res);

        fresh();
        t = &trk[0];
        filter_setup(t, IFT_COMB, 20, 90, 127);
        wet = note_hash(16, 48);
        fresh();
        t = &trk[0];
        filter_setup(t, IFT_COMB, 110, 90, 127);
        ck("FILTER COMB: CUT changes the comb delay (and the render)",
           wet != note_hash(16, 48));

        /* COMB must colour the dry signal (not near-transparent). RES 0 ≈ bypass;
         * high RES changes the render and rings after the note (feedback). */
        {
            int64_t e_res0, e_res;
            uint64_t h0, h1;
            fresh();
            t = &trk[0];
            clear_stages(t);
            wet = note_hash(16, 48);
            fresh();
            t = &trk[0];
            filter_setup(t, IFT_COMB, 40, 100, 127);
            ck("FILTER COMB: full MIX changes the render vs dry", wet != note_hash(16, 48));

            fresh();
            t = &trk[0];
            filter_setup(t, IFT_COMB, 48, 0, 127);
            h0 = note_hash(16, 48);
            fresh();
            t = &trk[0];
            filter_setup(t, IFT_COMB, 48, 110, 127);
            h1 = note_hash(16, 48);
            ck("FILTER COMB: RES changes the render (feedback)", h0 != h1);

            /* short pluck then measure the tail: high RES keeps the comb ringing */
            fresh();
            t = &trk[0];
            filter_setup(t, IFT_COMB, 32, 0, 127);
            t->p[P_ATK] = 0;
            t->p[P_DEC] = 20;
            t->p[P_SUS] = 0;
            t->p[P_REL] = 10;
            trk_note_on(t, 48, 120);
            render_blocks(8);
            trk_note_off(t, 48);
            e_res0 = energy_out(16, 48);
            fresh();
            t = &trk[0];
            filter_setup(t, IFT_COMB, 32, 110, 127);
            t->p[P_ATK] = 0;
            t->p[P_DEC] = 20;
            t->p[P_SUS] = 0;
            t->p[P_REL] = 10;
            trk_note_on(t, 48, 120);
            render_blocks(8);
            trk_note_off(t, 48);
            e_res = energy_out(16, 48);
            ck("FILTER COMB: high RES rings longer after the note",
               e_res > e_res0 + e_res0 / 4 || (e_res0 < 1000 && e_res > 5000));
            printf("inschain:   COMB tail energy RES 0 / 110: %lld / %lld\n",
                   (long long)e_res0, (long long)e_res);
        }
    }

    /* Every insert stage: MIX engages, and each exposed knob moves the render. */
    {
        uint64_t h0, h1;

        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IDMODE] = IDM_SOFT; t->p[P_DIST] = 20; t->p[P_IDMIX] = 127; h0 = note_hash(16, 48);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IDMODE] = IDM_SOFT; t->p[P_DIST] = 100; t->p[P_IDMIX] = 127; h1 = note_hash(16, 48);
        ck("DIST DRIVE moves the render", h0 != h1);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IDMODE] = IDM_SOFT; t->p[P_DIST] = 80; t->p[P_ED_FX] = -40; t->p[P_IDMIX] = 127; h0 = note_hash(16, 48);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IDMODE] = IDM_SOFT; t->p[P_DIST] = 80; t->p[P_ED_FX] = 40; t->p[P_IDMIX] = 127; h1 = note_hash(16, 48);
        ck("DIST TONE moves the render", h0 != h1);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IDMODE] = IDM_HARD; t->p[P_DIST] = 80; t->p[P_IDMIX] = 127; h0 = note_hash(16, 48);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IDMODE] = IDM_FOLD; t->p[P_DIST] = 80; t->p[P_IDMIX] = 127; h1 = note_hash(16, 48);
        ck("DIST MODE moves the render", h0 != h1);

        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_ICBITS] = 20; t->p[P_ICRATE] = 96; t->p[P_ICLPF] = 96; t->p[P_ICMIX] = 127; h0 = note_hash(16, 48);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_ICBITS] = 100; t->p[P_ICRATE] = 96; t->p[P_ICLPF] = 96; t->p[P_ICMIX] = 127; h1 = note_hash(16, 48);
        ck("CRUSH BITS moves the render", h0 != h1);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_ICBITS] = 40; t->p[P_ICRATE] = 10; t->p[P_ICLPF] = 96; t->p[P_ICMIX] = 127; h0 = note_hash(16, 48);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_ICBITS] = 40; t->p[P_ICRATE] = 110; t->p[P_ICLPF] = 96; t->p[P_ICMIX] = 127; h1 = note_hash(16, 48);
        ck("CRUSH RATE moves the render", h0 != h1);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_ICBITS] = 40; t->p[P_ICRATE] = 40; t->p[P_ICLPF] = 20; t->p[P_ICMIX] = 127; h0 = note_hash(16, 48);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_ICBITS] = 40; t->p[P_ICRATE] = 40; t->p[P_ICLPF] = 110; t->p[P_ICMIX] = 127; h1 = note_hash(16, 48);
        ck("CRUSH LPF moves the render", h0 != h1);

        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IPRATE] = 40; t->p[P_IPDEP] = 20; t->p[P_IPFBK] = 96; t->p[P_IPMIX] = 127; h0 = note_hash(16, 48);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IPRATE] = 40; t->p[P_IPDEP] = 120; t->p[P_IPFBK] = 96; t->p[P_IPMIX] = 127; h1 = note_hash(16, 48);
        ck("PHASR DEPTH moves the render", h0 != h1);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IPRATE] = 10; t->p[P_IPDEP] = 100; t->p[P_IPFBK] = 10; t->p[P_IPMIX] = 127; h0 = note_hash(16, 48);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IPRATE] = 10; t->p[P_IPDEP] = 100; t->p[P_IPFBK] = 120; t->p[P_IPMIX] = 127; h1 = note_hash(16, 48);
        ck("PHASR FDBK moves the render", h0 != h1);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IPRATE] = 10; t->p[P_IPDEP] = 100; t->p[P_IPFBK] = 96; t->p[P_IPMIX] = 127; h0 = note_hash(16, 48);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IPRATE] = 100; t->p[P_IPDEP] = 100; t->p[P_IPFBK] = 96; t->p[P_IPMIX] = 127; h1 = note_hash(16, 48);
        ck("PHASR RATE moves the render", h0 != h1);

        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_ILRATE] = 40; t->p[P_ILDEP] = 20; t->p[P_ILMIX] = 127; h0 = note_hash(16, 48);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_ILRATE] = 40; t->p[P_ILDEP] = 120; t->p[P_ILMIX] = 127; h1 = note_hash(16, 48);
        ck("FLANG DEPTH moves the render", h0 != h1);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_ILRATE] = 10; t->p[P_ILDEP] = 100; t->p[P_ILMIX] = 127; h0 = note_hash(16, 48);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_ILRATE] = 100; t->p[P_ILDEP] = 100; t->p[P_ILMIX] = 127; h1 = note_hash(16, 48);
        ck("FLANG RATE moves the render", h0 != h1);

        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IHRATE] = 40; t->p[P_IHDEP] = 20; t->p[P_IHMIX] = 127; h0 = note_hash(16, 48);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IHRATE] = 40; t->p[P_IHDEP] = 120; t->p[P_IHMIX] = 127; h1 = note_hash(16, 48);
        ck("CHOR DEPTH moves the render", h0 != h1);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IHRATE] = 10; t->p[P_IHDEP] = 100; t->p[P_IHMIX] = 127; h0 = note_hash(16, 48);
        fresh(); t = &trk[0]; clear_stages(t);
        t->p[P_IHRATE] = 100; t->p[P_IHDEP] = 100; t->p[P_IHMIX] = 127; h1 = note_hash(16, 48);
        ck("CHOR RATE moves the render", h0 != h1);

        /* DLY send: wait past the first echo (50 ms free time) so the amount is audible */
        fresh(); t = &trk[0]; clear_stages(t);
        song.g[G_DLYOWN] = 0; song.g[G_DTIME] = (int16_t)(DTIME_FREE + 50);
        song.g[G_DFDBK] = 90; song.g[G_TSPEED] = 100;
        t->p[P_DLY] = 40; h0 = note_hash(delay_samples() / CTL + 16u, 64);
        fresh(); t = &trk[0]; clear_stages(t);
        song.g[G_DLYOWN] = 0; song.g[G_DTIME] = (int16_t)(DTIME_FREE + 50);
        song.g[G_DFDBK] = 90; song.g[G_TSPEED] = 100;
        t->p[P_DLY] = 120; h1 = note_hash(delay_samples() / CTL + 16u, 64);
        ck("DLY send amount moves the render (after the first echo)", h0 != h1);

        /* Short tap: CHAIN delay/reverb must keep ringing after the voice dies (SEND already does).
         * Tap ~2 blocks, REL 0, then measure past the insert tail / first echo. */
        {
            int64_t send_e, chain_e;
            uint32_t echo_b;
            fresh(); t = &trk[0]; clear_stages(t);
            t->p[P_ATK] = 0; t->p[P_DEC] = 0; t->p[P_SUS] = 127; t->p[P_REL] = 0;
            song.g[G_DLYOWN] = 0;
            song.g[G_DTIME] = (int16_t)(DTIME_FREE + 80);
            song.g[G_DFDBK] = 100; song.g[G_TSPEED] = 100; song.g[G_DCOLOR] = 127;
            t->p[P_DLY] = 127;
            echo_b = delay_samples() / CTL + 8u;
            trk_note_on(t, 60, 110);
            render_blocks(2);
            trk_note_off(t, 60);
            render_blocks(echo_b);                      /* dry + release + first echo arrival */
            send_e = energy_out(0, 48);
            fresh(); t = &trk[0]; clear_stages(t);
            t->p[P_ATK] = 0; t->p[P_DEC] = 0; t->p[P_SUS] = 127; t->p[P_REL] = 0;
            song.g[G_DLYOWN] = 1;                        /* track 0 owns delay as CHAIN */
            song.g[G_DTIME] = (int16_t)(DTIME_FREE + 80);
            song.g[G_DFDBK] = 100; song.g[G_TSPEED] = 100; song.g[G_DCOLOR] = 127;
            t->p[P_DLY] = 127;
            trk_note_on(t, 60, 110);
            render_blocks(2);
            trk_note_off(t, 60);
            render_blocks(echo_b);
            chain_e = energy_out(0, 48);
            printf("inschain:   short-tap delay energy SEND %lld / CHAIN %lld\n",
                   (long long)send_e, (long long)chain_e);
            ck("delay CHAIN: short tap still rings after the voice (like SEND)",
               send_e > 10000 && chain_e > send_e / 4);

            fresh(); t = &trk[0]; clear_stages(t);
            t->p[P_ATK] = 0; t->p[P_DEC] = 0; t->p[P_SUS] = 127; t->p[P_REL] = 0;
            song.g[G_REVOWN] = 0;
            song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 100; song.g[G_RDAMP] = 20;
            t->p[P_REV] = 127;
            trk_note_on(t, 60, 110);
            render_blocks(2);
            trk_note_off(t, 60);
            render_blocks(64);                          /* past voice + insert tail */
            send_e = energy_out(0, 48);
            fresh(); t = &trk[0]; clear_stages(t);
            t->p[P_ATK] = 0; t->p[P_DEC] = 0; t->p[P_SUS] = 127; t->p[P_REL] = 0;
            song.g[G_REVOWN] = 1;
            song.g[G_CRTYPE] = 2; song.g[G_CRSIZE] = 100; song.g[G_CRDAMP] = 20;
            t->p[P_REV] = 127;
            trk_note_on(t, 60, 110);
            render_blocks(2);
            trk_note_off(t, 60);
            render_blocks(64);
            chain_e = energy_out(0, 48);
            printf("inschain:   short-tap reverb energy SEND %lld / CHAIN %lld\n",
                   (long long)send_e, (long long)chain_e);
            ck("reverb CHAIN: short tap still rings after the voice (like SEND)",
               send_e > 10000 && chain_e > send_e / 4);
        }

        fresh(); t = &trk[0]; clear_stages(t);
        song.g[G_REVOWN] = 0; song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 90; song.g[G_RDAMP] = 60;
        t->p[P_REV] = 40; h0 = note_hash(24, 64);
        fresh(); t = &trk[0]; clear_stages(t);
        song.g[G_REVOWN] = 0; song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 90; song.g[G_RDAMP] = 60;
        t->p[P_REV] = 120; h1 = note_hash(24, 64);
        ck("REV send amount moves the render", h0 != h1);
    }

    test_dist_modes();
    test_fx_reservations();
    test_retired_delay_mix();
    test_fx_bank_switch();
    test_fx_edit_paths();
    printf("inschain: %d failure(s)\n", bad);
    return bad ? 1 : 0;
}
