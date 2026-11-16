/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 gospodindilly-ops */
/* The second core (fx.c mix_block, hal/fm1_cpu1.h) on the host, after Jangada's tests/dualcore_test.c
 * (github.com/zednaked/jangada, GPL-3.0). Four parts of the same engine at once (every preset of every engine), and
 * mixes of different engines; every part with something that keeps state of its own while it renders: an INSERT, the
 * SLICER (part 1), SPREAD (part 2), the matrix with LFO S&H, which draws from the shared generator (parts 0 and 3),
 * ENV SYNC (part 0); and PHYS's model changed under a sounding note. This fork's chain / send EFFECTS on top:
 * shared SEND stages (the shared DSP and its driver), FLANG / CHOR as SEND (the lines' windows), the SLICER as SEND
 * (one shared clock, a take per track), the matrix moving the shared send knobs of two parts at once (bus_knobs) and
 * the DLY / REV bus settings of two parts (mod_bus), and DLY / REV as CHAIN on two owners (fx_bus_stage) while the
 * matrix moves their knobs. Each scene is rendered on one core, and again with a thread as core 1 (tests/c1_host.h)
 * taking every split of the parts in turn, the firmware's own choice (mix_split) and fixed splits. The outputs must
 * be bit for bit the same; built with -fsanitize=thread (run_tests.sh), nothing two parts share may be touched by
 * both at once.
 *   build/host/dualcore_test          SCENE=text: only the scenes whose name holds it
 *                                     SHARD=i/n: only the scenes whose number % n is i (run_tests.sh's TSAN units)
 *                                     SPLITS=n: only the first n splits of MASKS (TSAN: rot and auto, as each fork()
 *                                     under its runtime costs about half a second; rot puts every split on core 1) */
#include "c1_host.h"
#define main hostsim_main
#include "hostsim.c"
#undef main

#define SECS 3u

/* what a scene puts around the parts (part_setup) */
#define FX_TORTURE 1u               /* an INSERT each, the SLICER (part 1), SPREAD (part 2), matrix S&H (parts 0, 3) */
#define FX_SEND_INS 2u              /* FILTER + CRUSH as shared SEND stages on three parts */
#define FX_SEND_SWEEP 4u            /* FLANG + CHOR as SEND: the shared lines' windows */
#define FX_SEND_SLCR 8u             /* the SLICER as SEND: one shared clock, a take per track */
#define FX_MOD_KNOB 16u             /* the matrix moves the shared send knobs of two parts at once */
#define FX_MOD_BUS 32u              /* the matrix moves the DLY / REV bus settings of two parts (mod_bus) */
#define FX_CHAIN_OWN 64u            /* DLY / REV as CHAIN on two owners (fx_bus_stage), their knobs moved */
#define FX_PUNCH 128u               /* audio punch-ins latched in turn over the render (perform.c, on the second core) */
#define FX_KNOB 256u                /* the FX knob macros: THROW alone up and down, then CRUSH and DEPTH on a FLANGER */

typedef struct { const char *name; uint8_t eng[NPART], pre[NPART]; uint8_t morph; uint16_t fx; } scen_t;

static int32_t peak[NPART];                         /* each part's loudest (track_t.peak), for the check */
static int punch_seen;                              /* a punched render ran the FX layer */

static void part_setup(track_t *t, uint32_t k, uint32_t e, uint32_t fx)
{
    t->p[P_IDMODE] = (int16_t)((k + e) % IDM_N);     /* an INSERT on every part (its own state, ins_st[]) */
    t->p[P_IDMIX] = (int16_t)(50 + 12 * k);
    t->p[P_IFMIX] = 40;
    t->p[P_IFCUT] = (int16_t)(40 + 12 * k);
    t->p[P_REV] = (int16_t)(20 + 10 * k);
    t->p[P_DLY] = (int16_t)(10 * k);
    if (k == 0u) {
        t->p[P_ESYNC] = 1;                          /* ENV SYNC: t->esync_k */
        t->p[P_LWAVE] = 4;                          /* S&H: lfo_rand, the shared generator (parts 0..2) */
        t->p[P_LRATE] = 110;
        t->p[P_LD_FLT] = 20;
    }
    if (k == 1u) {
        t->p[P_SLDEPTH] = 100;                      /* the SLICER: sl[1], the shared clock */
        t->p[P_SLPAT] = 3;
    }
    if (k == 2u)
        t->p[P_SPRD] = 100;                         /* SPREAD: t->spread_on, spread_buf[2] */
    if (k == 3u) {
        t->p[P_LWAVE] = 4;                          /* S&H (part 3: its own generator) into the matrix */
        t->p[P_LRATE] = 100;
        t->p[P_M1SRC] = MS_SH;
        t->p[P_M1DST] = MD_CUT;
        t->p[P_M1AMT] = 40;
        t->p[P_M2SRC] = MS_LFO;
        t->p[P_M2DST] = MD_RATE;                    /* (a per-block destination: t->p while it renders) */
        t->p[P_M2AMT] = 20;
    }
    if (fx & FX_SEND_INS) {                         /* FILTER + CRUSH shared: one DSP, three consumers */
        ins_send_set(t, INS_FILTER, 1);
        ins_send_set(t, INS_CRUSH, 1);
        t->p[P_IFMIX] = (int16_t)(55 + 15 * k);
        t->p[P_IFRES] = 60;
        t->p[P_ICMIX] = (int16_t)(45 + 10 * k);
        t->p[P_ICBITS] = 60;
    }
    if (fx & FX_SEND_SWEEP) {                       /* FLANG + CHOR shared: the lines' windows. The knobs
                                                     * are the bus's; CHOR deep wraps its delay into the
                                                     * window's tail (see ins_run_swept) */
        ins_send_set(t, INS_FLANG, 1);
        ins_send_set(t, INS_CHOR, 1);
        t->p[P_ILMIX] = (int16_t)(50 + 15 * k);
        t->p[P_IHMIX] = (int16_t)(40 + 15 * k);
    }
    if (fx & FX_SEND_SLCR) {
        t->p[P_SLDEPTH] = (int16_t)(60 + 20 * k);    /* MIX per track; MODE / PAT / RATE on the bus */
        t->p[P_SLPAT] = (int16_t)(1 + k);
    }
    if ((fx & FX_MOD_KNOB) && (k == 0u || k == 2u)) { /* two parts move the shared knobs at once */
        t->p[P_M3SRC] = MS_MODW;
        t->p[P_M3DST] = MD_FCUT;
        t->p[P_M3AMT] = (int16_t)(30 + 10 * k);
        t->p[P_M4SRC] = MS_LFO;
        t->p[P_M4DST] = MD_BITS;
        t->p[P_M4AMT] = 25;
        t->mw = 127;
    }
    if ((fx & FX_MOD_BUS) && (k == 0u || k == 2u)) {  /* two parts move the DLY / REV bus settings (mod_bus) */
        t->p[P_M3SRC] = MS_MODW;
        t->p[P_M3DST] = MD_DTIME;
        t->p[P_M3AMT] = 30;
        t->p[P_M4SRC] = MS_LFO;
        t->p[P_M4DST] = MD_RSIZE;
        t->p[P_M4AMT] = 25;
        t->mw = 127;
    }
    if ((fx & FX_CHAIN_OWN) && k == 1u) {           /* the REV owner's own knobs, consumed by its CHAIN run */
        t->p[P_M3SRC] = MS_MODW;
        t->p[P_M3DST] = MD_RSIZE;
        t->p[P_M3AMT] = 35;
        t->mw = 127;
    }
}

static uint64_t render(const scen_t *sc, int dual, const char *mask)
{
    static const uint8_t CH[3] = {0, 7, 12};
    uint64_t h = 0xCBF29CE484222325ull;
    uint32_t f, k, i;
    int32_t o[2 * CTL];
    host_tracks_init();
    c1h_off = !dual;
    if (mask)
        setenv("C1_MASK", mask, 1);
    for (k = 0; k < NPART; k++) {
        host_preset(&trk[k], sc->eng[k], sc->pre[k]);
        part_setup(&trk[k], k, sc->eng[k], sc->fx);
    }
    if (sc->fx & FX_SEND_SLCR) {
        fx_bus_trk.p[P_SLCR] = SL_STUT;
        fx_bus_trk.p[P_SLPAT] = 3;
        fx_bus_trk.p[P_SLRATE] = 1;
    }
    if (sc->fx & FX_SEND_SWEEP) {                   /* the SEND sweeps read the bus knobs; CHOR at 127:
                                                     * its delay wraps into the line windows' tail */
        fx_bus_trk.p[P_ILRATE] = 40;
        fx_bus_trk.p[P_ILDEP] = 80;
        fx_bus_trk.p[P_IHRATE] = 35;
        fx_bus_trk.p[P_IHDEP] = 127;
    }
    if (sc->fx & FX_CHAIN_OWN) {                    /* DLY on part 0, REVERB on part 1 (two owners at once) */
        bus_own_set(0, 0);
        bus_own_set(1, 1);
    }
    if (sc->fx & (FX_PUNCH | FX_KNOB))
        perf_latch_on = 1;                          /* the audio punch-ins: a press latches, the next lets go */
    punch_seen = 0;
    for (f = 0; f < SECS * FS; f += CTL) {
        uint32_t ms = f / (FS / 1000u);
        for (k = 0; k < NPART; k++) {
            uint32_t on = 100u + 330u * k, off = on + 1400u, base = 48u + 5u * k;
            if (ms == on || ms == on + 700u)
                for (i = 0; i < 2u; i++)
                    trk_note_on(&trk[k], base + CH[i + (ms != on)], 90 + 10 * k);
            if (ms == off)
                for (i = 0; i < 3u; i++)
                    trk_note_off(&trk[k], base + CH[i]);
        }
        if (sc->fx & FX_PUNCH) {                    /* the audio ones, on and off again: REPEAT borrows the SLICER's */
            if (ms == 100u) {
                perf_press(PF_R16, 1);
                perf_press(PF_LPF, 1);
            } else if (ms == 250u) {
                perf_press(PF_FLG, 1);
                perf_press(PF_PHS, 1);
            } else if (ms == 400u) {
                perf_press(PF_RTHR, 1);
                perf_press(PF_ETHR, 1);
            } else if (ms == 550u) {
                perf_press(PF_R16, 1);
                perf_press(PF_LPF, 1);
                perf_press(PF_FLG, 1);
                perf_press(PF_PHS, 1);
                perf_press(PF_RTHR, 1);
                perf_press(PF_ETHR, 1);
            } else if (ms == 700u) {
                perf_latch_on = 0;
            }
        }
        if (sc->fx & FX_KNOB) {                     /* THROW's glide down runs on core 0 (perf_pre) while the layer
                                                     * runs on core 1: nothing else held, so only THROW keeps it busy.
                                                     * By block, so each press lands once */
            if (f == 140u * CTL) {
                perf_k[2] = 80;
            } else if (f == 410u * CTL) {
                perf_k[2] = 0;
            } else if (f == 580u * CTL) {
                perf_press(PF_FLG, 1);
                perf_k[1] = 50;
                perf_k[3] = 40;
            } else if (f == 800u * CTL) {
                perf_k[1] = 0;
                perf_k[3] = 0;
                perf_press(PF_FLG, 1);
            }
        }
        if (sc->morph && ms == 600u)                /* PHYS: MODEL changed under a sounding note (phys_render) */
            trk[0].p[P_E0] = (int16_t)((trk[0].p[P_E0] + 1) % PM_COUNT);
        mix_block(o, CTL);
        punch_seen |= perf_layer_busy || perf_act;
        for (k = 0; k < NPART; k++)
            if (trk[k].peak > peak[k])
                peak[k] = trk[k].peak;
        for (i = 0; i < 2u * CTL; i++) {
            uint32_t u = (uint32_t)o[i];
            for (k = 0; k < 4u; k++) {
                h ^= (u >> (8u * k)) & 0xFFu;
                h *= 0x100000001B3ull;
            }
        }
    }
    if ((sc->fx & (FX_PUNCH | FX_KNOB)) && !punch_seen)
        h = 0;                                      /* the layer never ran: the scene proves nothing (FAIL) */
    return h;
}

/* in a child of its own: every render starts from the state at boot */
static uint64_t render_fork(const scen_t *sc, int dual, const char *mask, uint32_t *taken)
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    fflush(stdout);                                 /* (else a child under TSAN prints the parent's lines again) */
    pid = fork();
    if (!pid) {
        h = render(sc, dual, mask);
        if (write(fd[1], &h, sizeof h) != sizeof h || write(fd[1], peak, sizeof peak) != sizeof peak ||
            write(fd[1], &c1h_taken, sizeof c1h_taken) != sizeof c1h_taken)
            _exit(2);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != sizeof h || read(fd[0], peak, sizeof peak) != sizeof peak ||
        read(fd[0], taken, sizeof *taken) != sizeof *taken)
        h = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}

/* mix_split on measured costs (c1_t4_ok): a part whose notes start this block weighs its recent peak, not the
 * last block's quiet cost. Part 1 was quiet (900 ticks) and is struck now (peak 14000): it goes alone to one core,
 * the other three (9200) to the other. On last block's costs alone the split would isolate part 2 (masks 4 / 11). */
static int split_fresh_note(void)
{
    static const uint16_t cost[NPART] = {2700, 900, 5000, 1500};
    uint32_t m, k;
    for (k = 0; k < NPART; k++)
        part_cost[k] = cost[k];
    part_peak[1] = 14000;
    c1_t4_ok = 1;
    trk[1].v[0].active = 1;
    trk[1].v[0].stage = 1;
    trk[1].v[0].age = ++vage;
    m = mix_split();
    c1_t4_ok = 0;
    trk[1].v[0].active = trk[1].v[0].stage = 0;
    memset(part_cost, 0, sizeof part_cost);
    memset(part_peak, 0, sizeof part_peak);
    if (m != 2u && m != 13u) {
        printf("dualcore: a struck part's peak weighs in the split: mask %x, want 2 or d   FAIL\n", m);
        return 1;
    }
    printf("dualcore: a struck part's peak weighs in the split   ok\n");
    return 0;
}

int main(void)
{
    static scen_t sc[256];
    static char names[256][48];
    static const char *const MASKS[] = {"rot", "auto", "5", "10", "3", "12", "1"};
    uint32_t n = 0, e, i, k, g, fail = 0, run = 0;
    uint32_t nmask = sizeof MASKS / sizeof MASKS[0], shard = 0, nshard = 1;
    if (getenv("SPLITS") && atoi(getenv("SPLITS")) > 0 && (uint32_t)atoi(getenv("SPLITS")) < nmask)
        nmask = (uint32_t)atoi(getenv("SPLITS"));
    fail += (uint32_t)split_fresh_note();
    if (getenv("SHARD") && (sscanf(getenv("SHARD"), "%u/%u", &shard, &nshard) != 2 || shard >= nshard)) {
        printf("dualcore: SHARD=%s is not i/n with i < n   FAIL\n", getenv("SHARD"));
        return 1;
    }
    for (e = 0; e < NENGINES; e++) {                    /* every preset: four parts at once */
        if (!eng_ok(e))                                 /* (a retired number: DIGITAL) */
            continue;
        for (g = 0; g * NPART < ENGINES[e]->npresets; g++) {
            snprintf(names[n], sizeof names[n], "4 x %s %u..%u", ENGINES[e]->name, g * NPART, g * NPART + NPART - 1u);
            sc[n].name = names[n];
            for (k = 0; k < NPART; k++) {
                sc[n].eng[k] = (uint8_t)e;
                sc[n].pre[k] = (uint8_t)((g * NPART + k) % ENGINES[e]->npresets);
            }
            sc[n].morph = 0;
            sc[n].fx = FX_TORTURE;
            n++;
        }
    }
    sc[n++] = (scen_t){"PHYS FM6 ANALOG DRUM", {ENGI_PHYS, ENGI_FM6, 0, ENGI_DRUM}, {1, 4, 7, 0}, 0, FX_TORTURE};
    sc[n++] = (scen_t){"GRAIN NOISE VOICE LOFI", {ENGI_GRAIN, ENGI_NOISE, 5, 3}, {1, 4, 7, 10}, 0, FX_TORTURE};
    sc[n++] = (scen_t){"TRIO WHEEL PHASE FM6", {6, 7, 2, ENGI_FM6}, {1, 4, 7, 10}, 0, FX_TORTURE};
    sc[n++] = (scen_t){"PHYS morph + DRUM GRAIN ANALOG", {ENGI_PHYS, ENGI_DRUM, ENGI_GRAIN, 0}, {2, 1, 3, 5}, 1,
                       FX_TORTURE};
    sc[n++] = (scen_t){"SAMPLE FM6 GRAIN PHASE", {ENGI_SAMPLE, ENGI_FM6, ENGI_GRAIN, 2}, {2, 1, 3, 5}, 0, FX_TORTURE};
    /* this fork's chain / send EFFECTS: the machinery two cores share */
    sc[n++] = (scen_t){"send FILTER CRUSH 4 x ANALOG", {0, 0, 0, 0}, {0, 2, 4, 6}, 0,
                       FX_TORTURE | FX_SEND_INS};
    sc[n++] = (scen_t){"send FILTER CRUSH + PHYS morph", {ENGI_PHYS, 0, ENGI_DRUM, 0}, {1, 3, 0, 5}, 1,
                       FX_TORTURE | FX_SEND_INS};
    sc[n++] = (scen_t){"send FLANG CHOR line 4 x FM6", {ENGI_FM6, ENGI_FM6, ENGI_FM6, ENGI_FM6}, {1, 3, 5, 7}, 0,
                       FX_TORTURE | FX_SEND_SWEEP};
    sc[n++] = (scen_t){"send FLANG CHOR + ANALOG PHYS", {0, ENGI_PHYS, 0, ENGI_PHYS}, {2, 1, 4, 3}, 0,
                       FX_TORTURE | FX_SEND_SWEEP};
    sc[n++] = (scen_t){"send SLICER clock DRUM x4", {ENGI_DRUM, ENGI_DRUM, ENGI_DRUM, ENGI_DRUM}, {0, 0, 0, 0}, 0,
                       FX_TORTURE | FX_SEND_SLCR};
    sc[n++] = (scen_t){"send SLICER + FILTER + LOFI", {3, 3, ENGI_DRUM, 3}, {1, 4, 0, 10}, 0,
                       FX_TORTURE | FX_SEND_INS | FX_SEND_SLCR};
    sc[n++] = (scen_t){"matrix on shared send knobs", {0, 0, ENGI_FM6, 0}, {2, 5, 1, 8}, 0,
                       FX_TORTURE | FX_SEND_INS | FX_MOD_KNOB};
    sc[n++] = (scen_t){"matrix on the DLY REV bus (mod_bus)", {ENGI_FM6, ENGI_FM6, 0, 0}, {2, 4, 1, 6}, 0,
                       FX_TORTURE | FX_MOD_BUS};
    sc[n++] = (scen_t){"DLY REV CHAIN owners + bus mod", {ENGI_GRAIN, ENGI_NOISE, 0, ENGI_DRUM}, {1, 2, 3, 0}, 0,
                       FX_TORTURE | FX_MOD_BUS | FX_CHAIN_OWN};
    sc[n++] = (scen_t){"send FLANG CHOR + knobs + bus mod", {0, 0, ENGI_FM6, ENGI_FM6}, {3, 6, 1, 4}, 0,
                       FX_TORTURE | FX_SEND_SWEEP | FX_MOD_KNOB | FX_MOD_BUS};
    /* the FX layer (perform.c perf_block) on the other core, one block late */
    sc[n++] = (scen_t){"punch LPF REPEAT 4 x ANALOG", {0, 0, 0, 0}, {0, 2, 4, 6}, 0, FX_TORTURE | FX_PUNCH};
    sc[n++] = (scen_t){"punch stack + PHYS morph", {ENGI_PHYS, ENGI_FM6, ENGI_DRUM, 0}, {1, 4, 7, 10}, 1,
                       FX_TORTURE | FX_PUNCH};
    sc[n++] = (scen_t){"punch SH 4 x VOICE", {5, 5, ENGI_NOISE, 5}, {1, 3, 0, 5}, 0,
                       FX_TORTURE | FX_PUNCH | FX_SEND_SLCR};
    sc[n++] = (scen_t){"knobs THROW CRUSH DEPTH 4 x ANALOG", {0, 0, 0, 0}, {1, 3, 5, 7}, 0, FX_TORTURE | FX_KNOB};
    for (i = 0; i < n; i++) {
        uint64_t ref;
        uint32_t m, bad = 0, quiet = 0, taken = 0, took = 0;
        if (getenv("SCENE") && !strstr(sc[i].name, getenv("SCENE")))
            continue;
        if (i % nshard != shard)
            continue;
        run++;
        ref = render_fork(&sc[i], 0, 0, &taken);
        for (k = 0; k < NPART; k++)
            quiet += peak[k] < 200;
        if (quiet) {                                    /* a silent part proves nothing */
            printf("dualcore: %-34s %u part(s) silent: peaks %d %d %d %d   FAIL\n", sc[i].name, quiet, peak[0],
                   peak[1], peak[2], peak[3]);
            fail++;
            continue;
        }
        for (m = 0; m < nmask; m++) {
            uint64_t h = render_fork(&sc[i], 1, MASKS[m], &taken);
            took += taken != 0;
            if (h != ref || !h) {
                printf("dualcore: %-34s split %-4s FAIL (%016llx, one core %016llx)\n", sc[i].name, MASKS[m],
                       (unsigned long long)h, (unsigned long long)ref);
                bad++;
            }
        }
        if ((sc[i].fx & FX_SEND_SWEEP) && took) {
            printf("dualcore: %-34s deep shared chorus bypassed its one-core fallback   FAIL\n", sc[i].name);
            bad++;
        } else if (!(sc[i].fx & FX_SEND_SWEEP) && took < 2u) {
            printf("dualcore: %-34s the second core never took parts   FAIL\n", sc[i].name);
            bad++;
        }
        if (!bad)
            printf("dualcore: %-34s %s   ok\n", sc[i].name,
                   sc[i].fx & FX_SEND_SWEEP ? "deep shared chorus stays on one core" : "the same on two cores, every split");
        fail += bad != 0;
    }
    printf("dualcore: %u of %u scenes differ (%u of %u run)\n", fail, run, run, n);
    return fail != 0;
}
