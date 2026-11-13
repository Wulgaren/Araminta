/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the project formats (firmware/src/project.c, -DPROJ_HOST part): FUN7..FUN19 as the firmware
 * stored them, imported at today's parameters (the parameters added since at their defaults), steps,
 * globals, names, FM6 patches and the chain; a tensor of damaged / wrong-size records refused. Formats
 * FUN1..FUN6 (Felucca 0.x .. 1.0 development) are no longer read: such a magic imports as empty (0).
 * Run by tests/run_tests.sh (needs build/gen from one firmware build). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1                             /* (trk_def_engine: engines.c, through hostsim.c) */
#include "../firmware/src/project.c"
#include "old_pack.h"                         /* (FUN9 / FUN8 / FUN7 as firmware before 1.2 wrote them) */

static int check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* a store packed as FUN16, rewritten to the FUN15 layout (5-byte chain rows, 72 motion records 48 bytes later),
 * for the tests that check what an older reader sees */
/* a stored kit is the factory one, packed as a project keeps it (each sound's LAYER in its spare bits) */
static int dx_factory(const dx_lane_t *k)
{
    dx_lane_t d[8];
    dx_kit_pack(d, DX_KIT_DEF, DX_LAY_DEF, DX_MDL_DEF);
    return !memcmp(k, d, sizeof d);
}
static void store_fun15_layout(project_store_t *st, uint32_t magic)
{
    uint32_t at = 68u + NTRK * (P_COUNT + 1u + 2u + NSTEP * 9u), i, count = st->raw[at];
    uint8_t chain[CHAIN_WIRE15_SIZE], mot[MOTION_FILE_SIZE_V15];
    memset(chain, 0, sizeof chain);
    for (i = 0; i < count && i < CHAIN_ROWS; i++) {
        uint32_t w = (uint32_t)st->raw[at + 4u + i * 2u] | (uint32_t)st->raw[at + 5u + i * 2u] << 8;
        chain[4u + i * 5u] = (uint8_t)(w & 7u);
        chain[4u + i * 5u + 1u] = (uint8_t)((w >> 3) & 7u);
        chain[4u + i * 5u + 2u] = (uint8_t)((w >> 6) & 7u);
        chain[4u + i * 5u + 3u] = (uint8_t)((w >> 9) & 7u);
        chain[4u + i * 5u + 4u] = (uint8_t)(((w >> 12) & 15u) + 1u);
    }
    chain[0] = st->raw[at];
    chain[1] = chain[2] = chain[3] = 0;
    memcpy(mot, st->raw + at + CHAIN_WIRE16_SIZE, MOTION_FILE_SIZE_V15);
    if (mot[0] > MOTION_FILE_MAX_V15)
        mot[0] = MOTION_FILE_MAX_V15;                   /* FUN16 holds 88, FUN15 72 */
    memcpy(st->raw + at, chain, sizeof chain);
    memcpy(st->raw + at + CHAIN_WIRE15_SIZE, mot, sizeof mot);
    memcpy(st->raw, &magic, 4);
    {   /* (the hash last, over everything above) */
        uint32_t sum = proj_hash(st->raw, PROJ_STORE_SIZE - 4u);
        memcpy(st->raw + PROJ_STORE_SIZE - 4u, &sum, 4);
    }
}

/* track t of the converted project has the old values where they belong; drum: track 4 was the drum part,
 * lvl / rev: its level and reverb send (the old globals) */
static int fm6_wire_same(const uint8_t *w, const uint8_t *p)
{
    uint32_t i;
    for (i = 0; i < FM6_PACKED; i++)
        if ((w[i] & 0x7Fu) != p[i])
            return 0;
    return 1;
}

static int fx_motion_wire(void)
{
    int bad = 0, ok = 1;
    project_t q, loaded; project_store_t st;
    memset(&q, 0, sizeof q);
    q.magic = PROJ_MAGIC; q.size = sizeof q; q.parts = NPART; q.phys = PROJ_PHYS;
    chain_defaults(&q.chain); memcpy(q.dx, DX_KIT_DEF, sizeof q.dx);
    q.g[G_DTIME] = 65; q.g[G_RSIZE] = 21;
    q.g[G_CRSIZE] = 22; q.g[G_CRDAMP] = 23;
    q.motion.count = 1; q.motion.on = 15;
    for (uint32_t ms = 1; ms <= 1486; ms++) {
        q.motion.event[0] = (motion_event_t){(uint8_t)(ms & 255u), (uint8_t)(MFX_TIME | (ms & 1u ? MOTION_LOCK : 0u)), (int16_t)(64 + ms)};
        ok &= proj_pack(&st, &q) && proj_import(&loaded, &st, sizeof st) &&
            loaded.motion.event[0].place == (ms & 255u) && loaded.motion.event[0].param == q.motion.event[0].param &&
            loaded.motion.event[0].value == 64 + (int32_t)ms && loaded.g[G_DTIME] == 65 &&
            loaded.g[G_RSIZE] == 21 && loaded.g[G_CRSIZE] == 22 && loaded.g[G_CRDAMP] == 23;
    }
    bad += check("FUN15 stores every exact TIME value without shifting globals", ok);
    q.motion.event[0] = (motion_event_t){17, P_REV | MOTION_LOCK, 109};
    q.motion.count = 1; q.motion.on = 1;
    ok = proj_pack(&st, &q);
    store_fun15_layout(&st, 0x46554E45u);                /* what a FUN14 reader sees */
    bad += check("FUN14 projects retain their three-byte locks and global IDs", ok && proj_import(&loaded, &st, sizeof st) &&
        loaded.motion.count == 1 && loaded.motion.event[0].param == (P_REV | MOTION_LOCK) &&
        loaded.motion.event[0].value == 109 && loaded.g[G_CRSIZE] == 22);
    {
        static const int16_t speed[] = {25, 50, 100, 128, 195, 200, 399, 400};
        ok = 1;
        for (uint32_t k = 0; k < NELEM(speed); k++) {
            int16_t first;
            q.motion.event[0] = (motion_event_t){17, MFX_TSPEED | MOTION_LOCK, speed[k]};
            ok &= proj_pack(&st, &q) && proj_import(&loaded, &st, sizeof st);
            first = loaded.motion.event[0].value;
            ok &= abs(first - speed[k]) <= (speed[k] / 50 ? speed[k] / 50 : 1);
            for (uint32_t cycle = 0; cycle < 8u; cycle++) {
                q.motion = loaded.motion;
                ok &= proj_pack(&st, &q) && proj_import(&loaded, &st, sizeof st) &&
                      loaded.motion.event[0].value == first;
            }
        }
        bad += check("TAPE SPEED locks quantize once and stay stable across save/load cycles", ok);
    }
    return bad;
}
static int chord_mode_wire(void)
{
    project_t saved, loaded;
    project_store_t wire;
    memset(&saved, 0, sizeof saved);
    saved.magic = PROJ_MAGIC; saved.size = sizeof saved; saved.parts = NPART; saved.phys = PROJ_PHYS;
    chain_defaults(&saved.chain); memcpy(saved.dx, DX_KIT_DEF, sizeof saved.dx);
    saved.t[0].p[P_CHRD] = CH_MELODY;
    int ok = proj_pack(&wire, &saved) && proj_import(&loaded, &wire, sizeof wire) &&
             loaded.t[0].p[P_CHRD] == CH_MELODY;
    ok &= param_fit(param_desc_of(0, P_CHRD), CH_MELODY + 1) == CH_MELODY;
    return check("CHRD MELO round trips; a value above it clamps to MELO", ok);
}

static int generation_wire(void)
{
    project_t saved, loaded;
    project_store_t wire;
    int ok = 1;
    memset(&saved, 0, sizeof saved);
    saved.magic = PROJ_MAGIC; saved.size = sizeof saved; saved.parts = NPART; saved.phys = PROJ_PHYS;
    chain_defaults(&saved.chain); memcpy(saved.dx, DX_KIT_DEF, sizeof saved.dx);
    for (uint32_t t = 0; t < NTRK; t++) {
        saved.t[t].generation = (generation_t){{(uint8_t)(10u + t), 67, 100}, (uint8_t)(31u + t), 255, 43};
        memcpy(saved.fm6[t], FM6_FACTORY[t * 2u], FM6_PACKED);
    }
    ok = proj_pack(&wire, &saved) && proj_import(&loaded, &wire, sizeof wire);
    for (uint32_t t = 0; t < NTRK; t++)
        ok &= !memcmp(&loaded.t[t].generation, &saved.t[t].generation, sizeof(generation_t)) &&
              !memcmp(loaded.fm6[t], saved.fm6[t], FM6_PACKED);
    return check("GENERATION controls round trip per track with FM6 patches preserved", ok);
}

static int generation_legacy_defaults(void)
{
    project_t saved, loaded;
    project_store_t wire;
    int ok = 1;
    memset(&saved, 0, sizeof saved);
    saved.magic = PROJ_MAGIC; saved.size = sizeof saved; saved.parts = NPART; saved.phys = PROJ_PHYS;
    chain_defaults(&saved.chain); memcpy(saved.dx, DX_KIT_DEF, sizeof saved.dx);
    for (uint32_t t = 0; t < NTRK; t++)
        memcpy(saved.fm6[t], FM6_FACTORY[t], FM6_PACKED);
    ok = proj_pack(&wire, &saved);
    uint32_t motion_off = 68u + NTRK * (P_COUNT + 1u + 2u + NSTEP * 9u) + CHAIN_WIRE16_SIZE;
    wire.raw[motion_off + 2u] = 0;
    uint32_t sum = proj_hash(wire.raw, sizeof wire - 4u);
    memcpy(wire.raw + sizeof wire - 4u, &sum, 4);
    ok &= proj_import(&loaded, &wire, sizeof wire);
    for (uint32_t t = 0; t < NTRK; t++) {
        const generation_t *g = &loaded.t[t].generation;
        ok &= g->density[0] == 50u && g->density[1] == 50u && g->density[2] == 50u &&
              g->x == 128u && g->y == 128u && g->chaos == 0u &&
              !memcmp(loaded.fm6[t], saved.fm6[t], FM6_PACKED);
    }
    return check("Projects saved before GENERATION receive defaults with FM6 patches preserved", ok);
}

/* every step feature on one step (a chord, accent + slide, x4 ratchet, nudge -5, the whole grid, a silent
 * chance): one FUN15 record, there and back, byte for byte */
static int step_wire_freeze(void)
{
    static const uint8_t NOTE[4] = {60, 64, 67, 71};
    project_t saved, loaded;
    project_store_t a, b, c;
    const step_t *s;
    int ok;
    memset(&saved, 0, sizeof saved);
    saved.magic = PROJ_MAGIC; saved.size = sizeof saved; saved.parts = NPART; saved.phys = PROJ_PHYS;
    chain_defaults(&saved.chain); memcpy(saved.dx, DX_KIT_DEF, sizeof saved.dx);
    for (uint32_t j = 0; j < 4u; j++) step_set_note(&saved.t[0].step[0], j, NOTE[j]);
    saved.t[0].step[0].n = 4; saved.t[0].step[0].time = ST_NOTE;
    step_set_fl(&saved.t[0].step[0], SF_ACCENT | SF_SLIDE);
    step_set_ratchet(&saved.t[0].step[0], 4);
    step_set_nudge(&saved.t[0].step[0], -5);
    saved.t[0].step[0].vel = 127;
    saved.t[0].step[0].hit = saved.t[0].step[0].acc = 0xFF;
    saved.t[0].step[0].probability = 101;
    saved.sum = proj_sum(&saved);
    ok = proj_pack(&a, &saved) && proj_pack(&b, &saved) && !memcmp(a.raw, b.raw, sizeof a.raw) &&
         proj_import(&loaded, &a, sizeof a) && proj_pack(&c, &loaded) && !memcmp(a.raw, c.raw, sizeof a.raw);
    s = &loaded.t[0].step[0];
    for (uint32_t j = 0; j < 4u; j++) ok &= step_note(s, j) == NOTE[j];
    ok &= s->n == 4u && s->time == ST_NOTE && step_fl(s) == (SF_ACCENT | SF_SLIDE) &&
          step_ratchet(s) == 4u && step_nudge(s) == -5 && s->vel == 127u &&
          s->hit == 0xFFu && s->acc == 0xFFu && s->probability == 101u;
    return check("FUN15: a step's chord, flags, ratchet, nudge, chance and grid round trip byte for byte", ok);
}

static int tape_wire(void)
{
    static project_t a, c;
    static project_store_t st, st16;
    static const uint8_t ORD[FX_LIST_N] = {INS_TAPE, INS_FILTER, INS_DIST, INS_SLCR, INS_CHOR,
                                           INS_REV, INS_CRUSH, INS_DLY, INS_FLANG, INS_PHASR};
    uint32_t i, t;
    int ok;
    memset(&a, 0, sizeof a);
    a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
    chain_defaults(&a.chain); memcpy(a.dx, DX_KIT_DEF, sizeof a.dx);
    for (i = 0; i < G_COUNT; i++)
        a.g[i] = GP[i].def;
    a.g[G_TSPEED] = 88; a.g[G_TWOW] = 44; a.g[G_TDROP] = 1; a.g[G_TMIX] = 55; a.g[G_TAPEMODE] = TAPE_MASTER;
    a.g[G_TTEMPO] = 0;
    for (t = 0; t < NTRK; t++) {
        a.t[t].engine = 0;
        a.t[t].p[P_IORD] = (int16_t)IORD_DEF;
        a.t[t].p[P_CHOR] = (int16_t)FX_POS_DEF;
        a.t[t].fx_sl_pos = 8u;
        memcpy(a.t[t].fx_order, ORD, FX_LIST_N);
    }
    a.t[0].p[P_TAPEMIX] = 77;
    a.fx_master[INS_DIST] = 1;                        /* track 1 owns DIST, track 2 DLY, track 4 SLCR */
    a.fx_master[INS_DLY] = 2;
    a.fx_master[INS_SLCR] = 4;
    a.fx_master[INS_TAPE] = 3;                        /* TAPE is MASTER, track 3 */
    ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
    for (i = 0; i < FX_LIST_N; i++)
        ok &= c.t[0].fx_order[i] == ORD[i];
    ok &= c.g[G_TSPEED] == 88 && c.g[G_TWOW] == 44 && c.g[G_TDROP] == 1 && c.g[G_TMIX] == 55 &&
          c.g[G_TAPEMODE] == TAPE_MASTER && c.g[G_TTEMPO] == 0 && c.t[0].p[P_TAPEMIX] == 77 &&
          c.fx_master[INS_DIST] == 1 && c.fx_master[INS_DLY] == 2 && c.fx_master[INS_SLCR] == 4 &&
          c.fx_master[INS_TAPE] == 3 && c.fx_master[INS_REV] == 0;
    {   /* the same bytes stamped FUN18 (before TEMPO): it loads ON, the current default */
        uint32_t sum;
        ((uint32_t *)st.raw)[0] = PROJ_MAGIC_V18;
        sum = proj_hash(st.raw, PROJ_STORE_SIZE - 4u);
        memcpy(st.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        ok &= proj_import(&c, &st, sizeof st) && c.g[G_TTEMPO] == 1 && c.g[G_TSPEED] == 88;
    }
    {   /* the same bytes as a FUN16: the order is not there, the globals are the old inert ids,
         * and no MASTER owner (FUN16/17 wrote zeros in those spare bytes) */
        uint32_t sum;
        memset(a.fx_master, 0, sizeof a.fx_master);
        ok &= proj_pack(&st16, &a);
        ((uint32_t *)st16.raw)[0] = PROJ_MAGIC_V16;
        sum = proj_hash(st16.raw, PROJ_STORE_SIZE - 4u);
        memcpy(st16.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        ok &= proj_import(&c, &st16, sizeof st16);
        {   /* factory order: DIST, CRUSH, FILTER, PHASR, FLANG, CHOR, DLY, REV, SLCR, TAPE */
            static const uint8_t DEF[FX_LIST_N] = {INS_DIST, INS_CRUSH, INS_FILTER, INS_PHASR, INS_FLANG,
                                                   INS_CHOR, INS_DLY, INS_REV, INS_SLCR, INS_TAPE};
            for (i = 0; i < FX_LIST_N; i++)
                ok &= c.t[0].fx_order[i] == DEF[i];
        }
        ok &= c.g[G_TSPEED] == GP[G_TSPEED].def && c.g[G_TWOW] == 0 && c.g[G_TDROP] == 0 &&
              c.g[G_TMIX] == 0 && c.g[G_TAPEMODE] == TAPE_SEND && c.g[G_TTEMPO] == 1 &&
              c.t[0].p[P_TAPEMIX] == 0;
        for (i = 0; i < FX_LIST_N; i++)
            ok &= c.fx_master[i] == 0;
    }
    return check("FUN19: the 10-stage order, TAPE globals/TEMPO and MASTER owners round trip; FUN18/16 get defaults", ok);
}

/* 3.5: ANALOG's E8 is INT (was KTR) and the FM3 slots are MORPH / DRIFT; header byte 67 bit 1 marks a record
 * that means it. Without the bit, ANALOG loads the three as 0 and drops their motion. */
static int osc2_migration(void)
{
    project_t q, loaded; project_store_t st;
    uint32_t sum = 0;
    int bad = 0, ok;
    memset(&q, 0, sizeof q);
    q.magic = PROJ_MAGIC; q.size = sizeof q; q.parts = NPART; q.phys = PROJ_PHYS;
    chain_defaults(&q.chain); memcpy(q.dx, DX_KIT_DEF, sizeof q.dx);
    q.t[1].engine = ENGI_ANALOG;
    q.t[1].p[P_E0] = 3; q.t[1].p[P_E7] = 12; q.t[1].p[P_MORPH] = 30; q.t[1].p[P_DRIFT] = 40;
    q.t[1].p[P_WAVE2] = 0;                             /* SAME */
    q.motion.count = 4; q.motion.on = 1;
    q.motion.event[0] = (motion_event_t){(uint8_t)(1u << 6), P_E7, 12};
    q.motion.event[1] = (motion_event_t){(uint8_t)(1u << 6), P_MORPH, 30};
    q.motion.event[2] = (motion_event_t){(uint8_t)(1u << 6), P_DRIFT, 40};
    q.motion.event[3] = (motion_event_t){(uint8_t)(1u << 6), P_WAVE2, 1};
    ok = proj_pack(&st, &q) && (st.raw[67] & 2u) && proj_import(&loaded, &st, sizeof st);
    bad += check("OSC A/B: a new record keeps INT / MORPH / DRIFT / WAVE2 and their motion",
                 ok && loaded.t[1].p[P_E7] == 12 && loaded.t[1].p[P_MORPH] == 30 &&
                 loaded.t[1].p[P_DRIFT] == 40 && loaded.t[1].p[P_WAVE2] == 0 &&
                 loaded.motion.count == 4);
    q.t[1].p[P_WAVE2] = 2;                             /* an explicit SQR */
    ok = proj_pack(&st, &q) && proj_import(&loaded, &st, sizeof st);
    bad += check("OSC B: an explicit WAVE survives a round trip", ok && loaded.t[1].p[P_WAVE2] == 2);
    q.t[1].p[P_WAVE2] = 4;                             /* stale FM2 ATK from a track that was FM6 */
    ok = proj_pack(&st, &q);
    st.raw[67] &= (uint8_t)~2u;                          /* what firmware before 3.5 wrote */
    sum = proj_hash(st.raw, PROJ_STORE_SIZE - 4u);
    memcpy(st.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
    ok = ok && proj_import(&loaded, &st, sizeof st);
    bad += check("OSC A/B: an older record loads them 0, WAVE2 SAME, and drops their motion",
                 ok && loaded.t[1].p[P_E7] == 0 && loaded.t[1].p[P_MORPH] == 0 &&
                 loaded.t[1].p[P_DRIFT] == 0 && loaded.t[1].p[P_WAVE2] == 0 &&
                 loaded.motion.count == 0);
    return bad;
}

/* 3.6: P_E5 changed from VMOD to VIB. An old project (header bit 2 clear) moves VMOD into the patch's
 * modulator velocity sensitivity and drops E5 automation; a new one keeps VIB. */
static int vib_wire(void)
{
    static project_store_t st;
    project_t a, c;
    uint8_t v[FP_SIZE + 1u], out[FP_SIZE + 1u];
    int16_t def[P_COUNT];
    uint32_t k, sum, ok;
    int bad = 0;
    memset(&a, 0, sizeof a);
    a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
    chain_defaults(&a.chain);
    for (k = 0; k < P_COUNT; k++)
        def[k] = param_desc_of(ENGI_FM6, k)->def;
    for (k = 0; k < NTRK; k++) {
        a.t[k].engine = ENGI_FM6;
        memcpy(a.t[k].p, def, sizeof def);
        memcpy(a.fm6[k], FM6_INIT, FM6_PACKED);
    }
    fm6_unpack(FM6_INIT, v);
    v[FP_ALG] = 0;                                    /* two carriers (bits 3, 5), four modulators */
    v[0 * FP_OP + FP_KVS] = 6;                        /* clamped to 7 */
    fm6_pack(v, a.fm6[0]);
    a.t[0].p[P_E5] = 5;                               /* the old VMOD */
    a.motion.count = 2;
    a.motion.event[0] = (motion_event_t){4u, P_E5, 3};      /* track 1: VMOD automation goes */
    a.motion.event[1] = (motion_event_t){4u, P_E4, 9};      /* track 1: MRAT stays */
    a.sum = proj_sum(&a);
    ok = proj_pack(&st, &a);
    st.raw[67] &= (uint8_t)~4u;                       /* what firmware before 3.6 wrote */
    sum = proj_hash(st.raw, PROJ_STORE_SIZE - 4u);
    memcpy(st.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
    ok = ok && proj_import(&c, &st, sizeof st);
    fm6_unpack(c.fm6[0], out);
    bad += check("VIB: an old project's VMOD moves into the modulators' velocity sensitivity",
                 ok && c.t[0].p[P_E5] == 0 && out[0 * FP_OP + FP_KVS] == 7 && out[1 * FP_OP + FP_KVS] == 5 &&
                 out[2 * FP_OP + FP_KVS] == 5 && out[4 * FP_OP + FP_KVS] == 5 &&
                 out[3 * FP_OP + FP_KVS] == 0 && out[5 * FP_OP + FP_KVS] == 0);
    bad += check("VIB: its E5 automation goes, the other event stays",
                 c.motion.count == 1u && c.motion.event[0].param == P_E4 && c.motion.event[0].value == 9);
    a.fm6_vib = 1;
    a.t[0].p[P_E5] = 40;                              /* new semantics: VIB, kept as it is */
    a.motion.count = 0;
    a.sum = proj_sum(&a);
    ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
    fm6_unpack(c.fm6[0], out);
    bad += check("VIB: a new project keeps VIB and the patch",
                 ok && c.t[0].p[P_E5] == 40 && out[0 * FP_OP + FP_KVS] == 6 && out[1 * FP_OP + FP_KVS] == 0);
    return bad;
}

/* FUN10 (FUNA) allowed 110 motion records in the body. Build one from a packed FUN20 image: demote the magic,
 * expand the motion block into the FUN10 size (the bytes FUN11 later took for the kit), and check import. */
static int store_fun10_motion110(project_store_t *st, const project_t *src)
{
    project_store_t cur;
    uint32_t at, i, sum, n = 110u;
    uint8_t chain[CHAIN_WIRE15_SIZE], mot[MOTION_FILE_SIZE_V10];
    if (!proj_pack(&cur, src))                          /* 4096: body+append hold 110; then demote to FUN10 wire */
        return 0;
    at = 68u + NTRK * (P_COUNT + 1u + 2u + NSTEP * 9u);
    memset(chain, 0, sizeof chain);
    chain[0] = cur.raw[at];
    for (i = 0; i < chain[0] && i < CHAIN_ROWS; i++) {
        uint32_t w = (uint32_t)cur.raw[at + 4u + i * 2u] | (uint32_t)cur.raw[at + 5u + i * 2u] << 8;
        chain[4u + i * 5u] = (uint8_t)(w & 7u);
        chain[4u + i * 5u + 1u] = (uint8_t)((w >> 3) & 7u);
        chain[4u + i * 5u + 2u] = (uint8_t)((w >> 6) & 7u);
        chain[4u + i * 5u + 3u] = (uint8_t)((w >> 9) & 7u);
        chain[4u + i * 5u + 4u] = (uint8_t)(((w >> 12) & 15u) + 1u);
    }
    memset(mot, 0, sizeof mot);
    mot[0] = (uint8_t)n;
    mot[1] = src->motion.on;
    for (i = 0; i < n; i++) {
        const motion_event_t *e = &src->motion.event[i];
        mot[4u + 3u * i] = e->place;
        mot[4u + 3u * i + 1u] = e->param;
        mot[4u + 3u * i + 2u] = (uint8_t)e->value;
    }
    memset(st, 0, sizeof *st);
    memcpy(st->raw, cur.raw, PROJ_STORE_OLD);
    {
        uint32_t magic = PROJ_MAGIC_V10, size = PROJ_STORE_OLD;
        memcpy(st->raw, &magic, 4);
        memcpy(st->raw + 4, &size, 4);
    }
    st->raw[67] = 0;                                    /* FUN10: no OSC2 / VIB / TAPE TEMPO bits */
    memcpy(st->raw + at, chain, sizeof chain);
    memcpy(st->raw + at + CHAIN_WIRE15_SIZE, mot, sizeof mot);
    /* name + hash at the 3840-byte end (FUN10 field size) */
    memcpy(st->raw + PROJ_STORE_OLD - 4u - PROJ_NAME_LEN, cur.raw + PROJ_STORE_SIZE - 4u - PROJ_NAME_LEN,
           PROJ_NAME_LEN);
    sum = proj_hash(st->raw, PROJ_STORE_OLD - 4u);
    memcpy(st->raw + PROJ_STORE_OLD - 4u, &sum, 4);
    return 1;
}

static int fun10_motion110(void)
{
    project_t saved, loaded;
    project_store_t st;
    uint32_t i;
    int ok;
    memset(&saved, 0, sizeof saved);
    saved.magic = PROJ_MAGIC; saved.size = sizeof saved; saved.parts = NPART; saved.phys = PROJ_PHYS;
    chain_defaults(&saved.chain); memcpy(saved.dx, DX_KIT_DEF, sizeof saved.dx);
    saved.motion.count = MOTION_FILE_MAX_V10;
    saved.motion.on = 15;
    for (i = 0; i < MOTION_FILE_MAX_V10; i++) {
        saved.motion.event[i].place = (uint8_t)i;       /* unique places (duplicates fail motion_valid) */
        saved.motion.event[i].param = P_REV;            /* not an MFX_* id: FUN10 refuses those */
        saved.motion.event[i].value = (int16_t)(i % 100u);
    }
    saved.sum = proj_sum(&saved);
    ok = store_fun10_motion110(&st, &saved) &&
         proj_import(&loaded, st.raw, (int)PROJ_STORE_OLD) &&
         loaded.motion.count == MOTION_FILE_MAX_V10;
    for (i = 0; ok && i < MOTION_FILE_MAX_V10; i++)
        ok &= loaded.motion.event[i].place == saved.motion.event[i].place &&
              loaded.motion.event[i].param == P_REV &&
              loaded.motion.event[i].value == saved.motion.event[i].value;
    return check("FUN10 with 110 motion records (above the FUN16 body of 88) loads", ok);
}

/* FUN20: 128 motion records (88 in the body + 40 in the append), packed, read back and packed again. */
static int fun20_full_motion(void)
{
    project_t saved, loaded;
    project_store_t a, b;
    uint32_t i;
    int ok;
    memset(&saved, 0, sizeof saved);
    saved.magic = PROJ_MAGIC; saved.size = sizeof saved; saved.parts = NPART; saved.phys = PROJ_PHYS;
    chain_defaults(&saved.chain); memcpy(saved.dx, DX_KIT_DEF, sizeof saved.dx);
    saved.motion.count = MOTION_FILE_MAX;
    saved.motion.on = 15;
    for (i = 0; i < MOTION_FILE_MAX; i++) {
        saved.motion.event[i].place = (uint8_t)i;
        saved.motion.event[i].param = (uint8_t)(MFX_FB | (i & 1u ? MOTION_LOCK : 0u));
        saved.motion.event[i].value = (int16_t)(i % 100u);   /* MFX_FB: 0..120 */
    }
    saved.lane[0] = 0x1F; saved.lane[39] = 0xA5; saved.lane_tgt = 2;
    saved.sum = proj_sum(&saved);
    ok = proj_pack(&a, &saved) && a.raw[0] == 0x46 && a.raw[1] == 0x55 && a.raw[2] == 0x4E && a.raw[3] == 0x4B
         && a.raw[PROJ_APPEND_MARK_OFF] == PROJ_APPEND_MARK
         && proj_import(&loaded, &a, sizeof a) && loaded.motion.count == MOTION_FILE_MAX
         && loaded.lane[0] == 0x1F && loaded.lane[39] == 0xA5 && loaded.lane_tgt == 2
         && proj_pack(&b, &loaded) && !memcmp(a.raw, b.raw, sizeof a.raw);
    for (i = 0; ok && i < MOTION_FILE_MAX; i++)
        ok &= loaded.motion.event[i].place == (uint8_t)i &&
              loaded.motion.event[i].param == saved.motion.event[i].param &&
              loaded.motion.event[i].value == saved.motion.event[i].value;
    return check("FUN20 with all 148 motion records and the punch-in lane round trips byte for byte", ok);
}

/* matrix slots 5..8 (proj_trk_t.mx) in the FUN20 store's tail (PROJ_MX_OFF): every track's round trip, extreme values,
 * the motion cap they cost (148 records), and a FUN20 file of more records (written before them) loading whole */
static int mx_same(const project_t *x, const project_t *y)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        if (memcmp(x->t[t].mx, y->t[t].mx, sizeof x->t[t].mx))
            return 0;
    return 1;
}
static int mx_wire(void)
{
    static project_t saved, loaded;
    static project_store_t a, b;
    uint32_t i, t, at = 68u + NTRK * (P_COUNT + 1u + 2u + NSTEP * 9u) + CHAIN_WIRE16_SIZE, sum;
    int ok;
    memset(&saved, 0, sizeof saved);
    saved.magic = PROJ_MAGIC; saved.size = sizeof saved; saved.parts = NPART; saved.phys = PROJ_PHYS;
    chain_defaults(&saved.chain); memcpy(saved.dx, DX_KIT_DEF, sizeof saved.dx);
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < 4u; i++) {
            saved.t[t].mx[i][MF_SRC] = (int16_t)(t == 3 && i == 3 ? MS_N - 1 : (t + i) % MS_N);
            saved.t[t].mx[i][MF_DST] = (int16_t)(t == 3 && i == 3 ? MD_N - 1 : (7u * t + 3u * i) % MD_N);
            saved.t[t].mx[i][MF_AMT] = (int16_t)(t == 0 && i == 0 ? -64 : t == 0 && i == 1 ? 63 : (int)(t * 4u + i) - 8);
        }
    saved.sum = proj_sum(&saved);
    ok = proj_pack(&a, &saved) && proj_import(&loaded, &a, sizeof a) && mx_same(&loaded, &saved) &&
         proj_pack(&b, &loaded) && !memcmp(a.raw, b.raw, sizeof a.raw);
    int bad = check("FUN20: matrix slots 5..8 of every track round trip (AMT -64 / 63, SLEW, VEL)", ok);
    saved.motion.count = MOTION_FILE_MAX + 5u;          /* more than a save keeps: clipped at 148, the slots kept */
    for (i = 0; i < saved.motion.count; i++)
        saved.motion.event[i] = (motion_event_t){(uint8_t)i, MFX_FB, (int16_t)(i % 100u)};
    saved.sum = proj_sum(&saved);
    ok = MOTION_FILE_MAX == 148u && proj_pack(&a, &saved) && proj_import(&loaded, &a, sizeof a) &&
         loaded.motion.count == 148u && mx_same(&loaded, &saved);
    bad += check("FUN20: a save keeps 148 motion records and the slots", ok);
    /* a FUN20 file of 155 records (before slots 5..8): records 149..155 where the slots go now */
    for (t = 0; t < NTRK; t++)
        memset(saved.t[t].mx, 0, sizeof saved.t[t].mx);
    saved.motion.count = 148u;
    saved.sum = proj_sum(&saved);
    ok = proj_pack(&a, &saved);
    a.raw[at] = 155u;
    for (i = 148u; i < 155u; i++) {
        uint8_t *w = a.raw + PROJ_MOTION_SPARE_OFF + 3u * (i - MOTION_FILE_MAX_BODY - MOTION_FILE_APPEND_MAX);
        w[0] = (uint8_t)i; w[1] = MFX_FB; w[2] = (uint8_t)(i % 100u);
    }
    sum = proj_hash(a.raw, PROJ_STORE_SIZE - 4u); memcpy(a.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
    ok &= proj_import(&loaded, &a, sizeof a) && loaded.motion.count == 155u &&
          loaded.motion.event[154].place == 154u && loaded.motion.event[154].value == 54;
    for (t = 0; ok && t < NTRK; t++)
        ok &= !memcmp(loaded.t[t].mx, (int16_t[4][3]){{0}}, sizeof loaded.t[t].mx);
    bad += check("FUN20 of 155 motion records (written before): every record, slots 5..8 OFF", ok);
    return bad;
}

/* DRUM-X kits: each sound's LAYER and MODEL (in the kit's spare bits) and the filter GR through a save and a load */
static int dx_layer_wire(void)
{
    static project_t a, c;
    static project_store_t st;
    dx_lane_t k[8];
    uint8_t l[8], m[8] = {0, DXV_SHELL, DXV_TEETH, DXV_METAL, DXV_METAL, 0, DXV_CLICK, DXV_METAL}, mo[8];
    int ok;
    memset(&a, 0, sizeof a);
    a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
    chain_defaults(&a.chain);
    memcpy(l, DX_KITS[DXK_LATIN].lay, sizeof l);     /* LATIN PERC: GR on HAT CL, a LAYER on SNARE */
    l[DV_RIM] = 15;                                 /* every LAYER bit */
    dx_kit_pack(a.dx, DX_KITS[DXK_LATIN].lane, l, m);
    a.sum = proj_sum(&a);
    ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st) && dx_kit_ok(c.dx);
    dx_kit_unpack(k, dx_lay, mo, c.dx);
    ok = ok && !memcmp(k, DX_KITS[DXK_LATIN].lane, sizeof k) && !memcmp(dx_lay, l, sizeof l) &&
         ((k[DV_HATC].mode >> 2) & 3u) == DXF_GR && dx_lay[DV_SNARE] == 4u && !memcmp(mo, m, sizeof m);
    c.dx[0].a[DXP_COLOR] |= 0x80u;                  /* (no LAYER bit there: a broken kit) */
    ok = ok && !dx_kit_ok(c.dx);
    c.dx[0].a[DXP_COLOR] &= 0x7Fu;
    c.dx[0].b[0] |= 0x80u, c.dx[0].b[1] |= 0x80u, c.dx[0].b[2] |= 0x80u;   /* (MODEL 7: none such) */
    ok = ok && !dx_kit_ok(c.dx);
    return check("DRUM-X kits: each sound's LAYER and MODEL and the filter GR round trip", ok);
}

/* the DRUM-X preset called name: its index (the kit it loads, DX_KITS), -1 none */
static int drumx_preset(const char *name)
{
    uint32_t i;
    for (i = 0; i < ENG_DRUM.npresets; i++)
        if (str_eq(ENG_DRUM.presets[i].name, name))
            return (int)i;
    return -1;
}
/* a project with Felucca's DRUMS engine (retired) on track 1 at KIT kit, saved and loaded again (as an old file is) */
static int drums_project(project_t *c, uint32_t kit, int drumx_on_t2)
{
    static project_t a;
    static project_store_t st;
    memset(&a, 0, sizeof a);
    a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
    chain_defaults(&a.chain);
    dx_kit_pack(a.dx, DX_KITS[0].lane, DX_KITS[0].lay, DX_MDL_DEF);
    a.t[0].engine = ENGI_DRUMS;
    a.t[0].p[P_E0] = (int16_t)kit;
    for (uint32_t i = 0; i < 8u; i++)
        a.t[0].p[P_LN0 + i] = 127;                 /* the original engine's default lane levels */
    if (drumx_on_t2) {                                  /* (another track on DRUM-X, its own kit: LATIN PERC's) */
        a.t[1].engine = ENGI_DRUM;
        dx_kit_pack(a.dx, DX_KITS[DXK_LATIN].lane, DX_KITS[DXK_LATIN].lay, DX_MDL_DEF);
    }
    a.sum = proj_sum(&a);
    return proj_pack(&st, &a) && proj_import(c, &st, sizeof st);
}
/* DRUMS (retired) -> DRUM-X: the kit its KIT played as a DRUM-X preset, the mix kits assembled, a DRUM-X track's kit
 * kept */
static int drums_retired(void)
{
    static project_t c;
    dx_lane_t want[8], k[8];
    uint8_t lay[8], mdl[8];
    int p80 = drumx_preset("80"), bad = 0, ok, i;
    const int8_t *m;
    /* (1) KIT 80: the 80 preset's kit and lane levels */
    ok = p80 >= 0 && drums_project(&c, 1, 0) && c.t[0].engine == ENGI_DRUM;
    if (ok) {
        dx_kit_load((uint32_t)p80);
        dx_kit_pack(want, dx_kit, dx_lay, dx_mdl);
        ok = !memcmp(c.dx, want, sizeof want);
        for (m = preset_mod(&ENG_DRUM, (uint32_t)p80); ok && m && m[0] >= 0; m += 2)
            if (m[0] >= P_LN0 && m[0] <= P_LN7)
                ok = c.t[0].p[m[0]] == m[1];
    }
    bad += check("DRUMS track on KIT 80 loads as DRUM-X with the 80 preset's kit and lane levels", ok);
    /* (2) KIT DEEP (6): 77's kick 2 semitones down, 80's snare, 77's clap, 80's hats, 77's tom, 80's rim, 10's bell */
    ok = drums_project(&c, 6, 0) && c.t[0].engine == ENGI_DRUM;
    if (ok) {
        static const char *const FROM[8] = {"77", "80", "77", "80", "80", "77", "80", "10"};
        dx_kit_unpack(k, lay, mdl, c.dx);
        for (i = 0; ok && i < 8; i++) {
            int pi = drumx_preset(FROM[i]);
            ok = pi >= 0;
            if (!ok) break;
            dx_kit_load((uint32_t)pi);
            want[i] = dx_kit[i];
            if (i == DV_KICK) {                         /* (2 semitones: 3 steps of 3/4, as near as it goes) */
                want[i].a[DXP_PITCH] = (uint8_t)(want[i].a[DXP_PITCH] - 3);
                want[i].b[DXP_PITCH] = (uint8_t)(want[i].b[DXP_PITCH] - 3);
            }
            ok = !memcmp(&k[i], &want[i], sizeof want[i]) && mdl[i] == dx_mdl[i] && lay[i] == dx_lay[i];
        }
    }
    bad += check("DRUMS track on KIT DEEP loads with DEEP's kit assembled from 77, 80 and 10's sounds", ok);
    /* (3) a DRUM-X track already there: the project's kit stays (one kit a project) */
    ok = drums_project(&c, 1, 1) && c.t[0].engine == ENGI_DRUM;
    if (ok) {
        dx_kit_pack(want, DX_KITS[DXK_LATIN].lane, DX_KITS[DXK_LATIN].lay, DX_MDL_DEF);
        ok = !memcmp(c.dx, want, sizeof want);
    }
    bad += check("DRUMS track beside a DRUM-X track: DRUM-X, the project's kit kept", ok);
    return bad;
}

/* A saved DRUMS lane balance survives the wire migration, including a lane made silent with LEVEL 0. */
static int drums_lane_levels(void)
{
    static project_t a, c;
    static project_store_t st;
    uint32_t i;
    int ok;
    memset(&a, 0, sizeof a);
    a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
    chain_defaults(&a.chain);
    dx_kit_pack(a.dx, DX_KIT_DEF, DX_LAY_DEF, DX_MDL_DEF);
    a.t[0].engine = ENGI_DRUMS;
    a.t[0].p[P_E0] = 1;
    for (i = 0; i < 8u; i++)
        a.t[0].p[P_LN0 + i] = 127;
    a.t[0].p[P_LN0] = 0;
    a.t[0].p[P_LN1] = 64;
    a.sum = proj_sum(&a);
    ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
    return check("DRUMS project migration keeps a silent kick and scales a half-volume snare",
                 ok && c.t[0].engine == ENGI_DRUM && c.t[0].p[P_LN0] == 0 &&
                 c.t[0].p[P_LN1] == 51 && c.t[0].p[P_LN2] == 107);
}

int main(void)
{
    static project_t q, q2, buf;
    uint32_t i, t;
    int bad = fx_motion_wire() + tape_wire() + osc2_migration(), ok;

    bad += check("layout: SLICER after DETUNE, then the matrix just before P_E0",
                 P_SLCR == P_DETUNE + 1 && P_SLDEPTH + 1 == P_M1SRC && P_M4AMT + 1 == P_FM1_ATK && P_FM4_LEVEL + 1 == P_CHRD && P_VOIC + 1 == P_LN0 &&
                 P_LN7 + 1 == P_LSYNC && P_LSYNC + 3 == P_SQNT && P_SQNT + 1 == P_SPRD && P_SPRD + 1 == P_IORD &&
                 P_IHMIX + 1 == P_FTYPE && P_FTYPE + 1 == P_ESYNC && P_ESYNC + 1 == P_E0 && P_E0 == 119 &&
                 P_COUNT == 127u);
    /* 3.10: the object spans two sectors of one storage object, and the four-slot wire cache is gone (a small
     * summary replaces it, so NOINIT no longer bounds the project size) */
    bad += check("3.10: the object is two sectors and the NOINIT cache is a 4-slot summary",
                 sizeof(project_store_t) == 4096u && sizeof(project_store_t) <= 2u * 4096u - 256u &&
                 sizeof(proj_slot_sum) == 4u * 44u && sizeof(proj_slot_sum) <= 256u);

    /* today's format: a matrix slot set, WHEEL engine 7, a DRUM track with notes and hits: stored as is */
    memset(&q, 0, sizeof q);
    q.magic = PROJ_MAGIC; q.size = sizeof q; q.sel = 1; q.parts = NPART; q.phys = PROJ_PHYS; q.fm6_vib = 1;
    chain_defaults(&q.chain); memcpy(q.dx, DX_KIT_DEF, sizeof q.dx);
    for (t = 0; t < NTRK; t++) {
        q.t[t].engine = trk_def_engine(t);
        q.t[t].preset = PROJ_DEF_SOUND;
        for (i = 0; i < P_COUNT; i++) q.t[t].p[i] = TP[i].def;
        memcpy(q.fm6[t], FM6_INIT, FM6_PACKED);
    }
    q.t[1].engine = 7;
    q.t[2].p[P_M2SRC] = 6;
    q.t[2].p[P_M2DST] = 11;
    q.t[2].p[P_M2AMT] = -17;
    q.t[0].engine = ENGI_DRUM;
    q.t[0].step[0] = (step_t){{36, 42, 0, 0}, 2, ST_NOTE, 0, 96, 1u << DV_SNARE, 1u << DV_SNARE};
    q.sum = proj_sum(&q);
    memcpy(&buf, &q, sizeof q);
    bad += check("today's format: as stored (a matrix slot, engine 7, a DRUM step with notes and hits)",
                 proj_import(&q2, &buf, (int)sizeof q) && !memcmp(&q, &q2, sizeof q) && q2.t[1].engine == 7 &&
                 q2.t[2].p[P_M2AMT] == -17 && q2.t[0].step[0].n == 2u && q2.t[0].step[0].hit == 1u << DV_SNARE);
    q.chain.count = 2;
    q.chain.row[0] = chain_row_of(0, 4);
    q.chain.row[1] = chain_row_of(3, 16);
    q.sum = proj_sum(&q);
    bad += check("chain round trip", proj_import(&q2, &q, sizeof q) &&
        !memcmp(&q.chain, &q2.chain, sizeof q.chain));
    q.chain.row[0].repeat = 0;
    q.sum = proj_sum(&q);
    bad += check("an invalid chain row is refused even with a correct checksum", !proj_import(&q2, &q, sizeof q));
    chain_defaults(&q.chain);
    q.sum = proj_sum(&q);

    {   /* FUN1..FUN6 are no longer read: such a magic imports as empty (0) */
        static project_store_t st;
        uint32_t magic = 0x364E5546u, sum;             /* "FUN6" */
        ok = proj_pack(&st, &q);
        memcpy(st.raw, &magic, 4);
        sum = proj_hash(st.raw, PROJ_STORE_SIZE - 4u);
        memcpy(st.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        bad += check("a FUN6 magic is refused (empty)", ok && !proj_import(&q2, &st, sizeof st));
    }

    {   /* the FUN7 name: the reserved tail's last 12 bytes, covered by the hash; zero = no name */
        project_t a, c;
        project_store_t st, st2;
        uint32_t i, zero = 1;
        bad += check("3.10 layout: frozen offsets, 256 B append area between FM6 and name",
                     PROJ_NAME_OFF == 4080u && PROJ_FM6_OFF == 3312u && PROJ_DX_OFF == 3216u &&
                     PROJ_DX_OFF + PROJ_DX_SIZE == PROJ_FM6_OFF &&
                     PROJ_NAME_OFF - (PROJ_FM6_OFF + NTRK * FM6_PACKED) == 256u &&
                     PROJ_SCL_OFF + PROJ_SCL_SIZE + 2u == PROJ_CRV_OFF &&
                     68u + NTRK * (P_COUNT + 1u + 2u + NSTEP * 9u) + CHAIN_WIRE16_SIZE + MOTION_FILE_SIZE ==
                     PROJ_SCL_OFF);
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        a.t[1].p[P_LEVEL] = 99;
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a);
        for (i = 0; i < PROJ_NAME_LEN; i++) zero &= st.raw[PROJ_NAME_OFF + i] == 0u;
        bad += check("FUN7 no name: the tail all zero (as firmware before wrote it)", ok && zero);
        bad += check("FUN7 of before names (zero tail): loads, no name", proj_import(&c, &st, sizeof st) && !c.name[0] &&
                     c.t[1].p[P_LEVEL] == 99);
        memcpy(a.name, "lofi jam", 8);
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN7 name round trip (upper case, 0-padded)", ok && !memcmp(c.name, "LOFI JAM\0\0\0\0", 12) &&
                     !memcmp(st.raw + PROJ_NAME_OFF, "LOFI JAM\0\0\0\0", 12));
        memcpy(a.name, "ABCDEFGHIJKL", 12);         /* 12: no 0 */
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN7 name of 12 characters (no terminator)", ok && !memcmp(c.name, "ABCDEFGHIJKL", 12));
        st2 = st;
        st2.raw[PROJ_NAME_OFF + 3] ^= 1u;
        bad += check("FUN7 name byte changed: the hash refuses the project", !proj_import(&c, &st2, sizeof st2));
        st2 = st;
        st2.raw[PROJ_NAME_OFF + 3] = 7u;            /* not printable, the hash right */
        {
            uint32_t sum = proj_hash(st2.raw, PROJ_STORE_SIZE - 4u);
            memcpy(st2.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        }
        bad += check("FUN7 name with a byte outside 32..126: the project loads, no name",
                     proj_import(&c, &st2, sizeof st2) && !c.name[0] && c.t[1].p[P_LEVEL] == 99);
    }

    {   /* FUN8: each track's FM6 patch travels with the project; FUN7 and older get the init patch */
        project_t a, c;
        static project_store_t st, slot;
        static uint8_t v7[PROJ_STORE_V7];
        uint32_t i, k, init = 1, sum;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        for (k = 0; k < NTRK; k++) {
            memcpy(a.fm6[k], FM6_FACTORY[k * 2u], FM6_PACKED);
            a.t[k].engine = ENGI_FM6;
            a.t[k].p[P_E7] = (int16_t)(k * 2u);
        }
        memcpy(a.name, "FM SONG", 7);
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && ((uint32_t *)st.raw)[0] == PROJ_MAGIC && proj_import(&c, &st, sizeof st);
        for (k = 0; k < NTRK; k++)
            ok &= !memcmp(c.fm6[k], FM6_FACTORY[k * 2u], FM6_PACKED) && c.t[k].engine == ENGI_FM6 &&
                  fm6_wire_same(st.raw + PROJ_FM6_OFF + k * FM6_PACKED, FM6_FACTORY[k * 2u]);
        bad += check("FUN14: the four FM6 patches round trip (bytes at PROJ_FM6_OFF)", ok && !memcmp(c.name, "FM SONG", 7));
        st.raw[PROJ_FM6_OFF + 5] ^= 1u;
        bad += check("FUN14: a patch byte changed: the hash refuses it", !proj_import(&c, &st, sizeof st));
        st.raw[PROJ_FM6_OFF + 5] ^= 1u;
        {   /* FUN8 as 1.0.x wrote it (3584 bytes, 91 parameters: E0..E7 at 83..90, the patches at 3056): the DRUM
             * lane levels 100 %, E0..E7 and their motion at today's P_E0.., the patches and the name kept */
            static uint8_t v8[3584];
            uint32_t j;
            project_t e = a;
            e.t[3].engine = ENGI_DRUM; e.t[3].p[P_E3] = 30;
            e.motion.count = 2; e.motion.on = 8;
            e.motion.event[0] = (motion_event_t){3u << 6 | 2u, P_E3, 99};
            e.motion.event[1] = (motion_event_t){3u << 6 | 4u, P_REV, 50};
            {   /* FUN10 holds parameter locks (bit 7 of the id): on a DRUM lane level and on an engine value */
                project_t f = e, g;
                f.motion.count = 4;
                f.motion.event[2] = (motion_event_t){3u << 6 | 5u, (uint8_t)(P_LN2 | MOTION_LOCK), 40};
                f.motion.event[3] = (motion_event_t){3u << 6 | 6u, (uint8_t)(P_E5 | MOTION_LOCK), -3};
                f.sum = proj_sum(&f);
                ok = proj_pack(&st, &f) && proj_import(&g, &st, sizeof st) && !memcmp(&f.motion, &g.motion, sizeof f.motion);
                bad += check("FUN10 round trip: locks on a lane level (P_LN2) and an engine value (P_E5) kept", ok &&
                             st.raw[68u + NTRK * (P_COUNT + 1u + 2u + NSTEP * 9u) + CHAIN_WIRE16_SIZE + 4u + 6u + 1u] ==
                             (P_LN2 | MOTION_LOCK) && g.motion.event[3].param == (P_E5 | MOTION_LOCK));
            }
            e.motion.count = 3;   /* a FUN8 of a 1.1 development build (91 parameters) could hold a lock: its E5 then */
            e.motion.event[2] = (motion_event_t){3u << 6 | 6u, (uint8_t)(P_E5 | MOTION_LOCK), -3};
            e.sum = proj_sum(&e);
            ok = old_pack(v8, &e, OLD_FUN8, 91u) == sizeof v8;   /* (its E3 at 86, its E5 lock at 88) */
            ok = ok && proj_import(&c, v8, sizeof v8);
            for (k = 0; ok && k < NTRK; k++) {
                for (j = P_LN0; j <= P_LN7; j++) ok &= c.t[k].p[j] == 127;
                ok &= !memcmp(c.fm6[k], FM6_FACTORY[k * 2u], FM6_PACKED);
            }
            bad += check("FUN8 of 1.0.x (91 parameters): lane levels 100 %, E0..E7 at P_E0, patches and name kept", ok &&
                         c.t[3].engine == ENGI_DRUM && c.t[3].p[P_E3] == 30 && c.t[2].p[P_E7] == 4 &&
                         !memcmp(c.name, "FM SONG", 7));
            bad += check("  its motion: E3 (86 then) at P_E3, REV kept", c.motion.count == 3u &&
                         c.motion.event[0].param == P_E3 && c.motion.event[0].value == 99 && c.motion.event[1].param == P_REV);
            bad += check("  a lock on its E5 (88 then): at P_E5, still a lock", c.motion.event[2].param == (P_E5 | MOTION_LOCK) &&
                         c.motion.event[2].value == -3);
            bad += check("  one byte short: refused", !proj_import(&c, v8, sizeof v8 - 1u));
            memset(&slot, 0xA5, sizeof slot);
            memcpy(slot.raw, v8, sizeof v8);
            bad += check("  in a retained (longer) slot: loads", proj_import(&c, &slot, sizeof slot) && c.t[3].p[P_E3] == 30);
        }
        /* the same music as FUN7 (3388 bytes, 91 parameters, no patches): the name at 3372 */
        ok = old_pack(v7, &a, OLD_FUN7, 91u) == sizeof v7 && proj_import(&c, v7, sizeof v7);
        for (k = 0; k < NTRK; k++)
            init &= !memcmp(c.fm6[k], FM6_INIT, FM6_PACKED);
        bad += check("FUN7 -> FUN10: the music and name kept, every track the init patch",
                     ok && init && c.t[2].engine == ENGI_FM6 && c.t[2].p[P_E7] == 4 && !memcmp(c.name, "FM SONG", 7));
        {   /* REVERB TYPE (id 24) of a FUN7 holding the old drum channel (10): ROOM; SPRING (1) kept */
            int16_t g24 = 10;
            memcpy(v7 + 8u + 2u * G_RTYPE, &g24, 2);
            sum = proj_hash(v7, PROJ_STORE_V7 - 4u);
            memcpy(v7 + PROJ_STORE_V7 - 4u, &sum, 4);
            ok = proj_import(&c, v7, sizeof v7) && c.g[G_RTYPE] == 0 && proj_ok(&c);
            g24 = 1;
            memcpy(v7 + 8u + 2u * G_RTYPE, &g24, 2);
            sum = proj_hash(v7, PROJ_STORE_V7 - 4u);
            memcpy(v7 + PROJ_STORE_V7 - 4u, &sum, 4);
            ok &= proj_import(&c, v7, sizeof v7) && c.g[G_RTYPE] == 1;
            bad += check("FUN7 REVERB TYPE above SPRING (an old drum channel): ROOM", ok);
        }
        ok = proj_import(&c, v7, sizeof v7 - 1u);
        bad += check("FUN7 one byte short: refused", !ok);
        /* the retained cache after an update: slot 1's FUN7 record, longer slot, garbage after it */
        memset(&slot, 0xA5, sizeof slot);
        memcpy(slot.raw, v7, sizeof v7);
        bad += check("a retained 3840-byte slot holding a FUN7 record loads it", proj_import(&c, &slot, sizeof slot) &&
                     c.t[2].p[P_E7] == 4 && !memcmp(c.fm6[0], FM6_INIT, FM6_PACKED));
        memset(&slot, 0xA5, sizeof slot);
        bad += check("a retained slot of garbage is empty", !proj_import(&c, &slot, sizeof slot));
        for (i = 0; i < FM6_PACKED; i++) a.fm6[1][i] = (uint8_t)(i | 0x80u);
        a.sum = proj_sum(&a);
        bad += check("FUN8: a patch byte above 127 cannot be packed", !proj_pack(&st, &a));
        memcpy(a.fm6[1], FM6_INIT, FM6_PACKED);
        a.t[0].step[3].n = 1;
        step_set_note(&a.t[0].step[3], 0, 200); step_set_note(&a.t[0].step[3], 2, 128);
        a.t[0].step[3].vel = 255; a.t[0].step[3].hit = 0x05; a.t[0].step[3].acc = 0xF7;
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN8: a step out of range packs inside it (note, velocity 127; accents on hits): it loads",
                     ok && step_note(&c.t[0].step[3], 0) == 127u && step_note(&c.t[0].step[3], 2) == 127u && c.t[0].step[3].vel == 127u &&
                     c.t[0].step[3].hit == 0x05u && c.t[0].step[3].acc == 0x05u);
    }

    {   /* SAMPLE tracks of SET 4 (PERC, the GM kit, retired after 1.0.2) in FUN8 projects load as DRUM with its
         * default kit: the E values the kit's, the rest of the sound, the steps and the preset byte's meaning kept; their
         * motion on the EDIT values goes, the rest stays; another SAMPLE set stays SAMPLE. Idempotent */
        project_t a, c, d;
        static project_store_t st;
        static const int16_t KIT[8] = DRUM_KIT_E;
        uint32_t i;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        for (t = 0; t < NTRK; t++) {
            for (i = 0; i < P_COUNT; i++)
                a.t[t].p[i] = param_desc_of(ENGI_SAMPLE, i)->def;
            a.t[t].engine = ENGI_SAMPLE;
            memcpy(a.fm6[t], FM6_INIT, FM6_PACKED);
        }
        a.t[2].p[P_E0] = SMP_SET_PERC;                  /* track 3: a saved PERC sound (SET 4, no loop) */
        a.t[2].p[P_E3] = 0;
        a.t[2].p[P_LEVEL] = 77; a.t[2].p[P_REV] = 41; a.t[2].p[P_PAN] = -9;
        a.t[2].preset = 4;
        a.t[2].step[0] = (step_t){{36, 42, 0, 0}, 2, ST_NOTE, SF_ACCENT, 100, 0, 0};
        a.t[2].step[4] = (step_t){{38, 49, 0, 0}, 2, ST_NOTE, 0, 90, 0, 0};
        a.t[0].p[P_E0] = 2;                             /* track 1: FLUTE stays */
        a.motion.count = 3;
        a.motion.event[0] = (motion_event_t){2u << 6 | 3u, P_E4, 20};          /* track 3: CUT: goes */
        a.motion.event[1] = (motion_event_t){2u << 6 | 5u, P_REV, 60};         /* track 3: a send: stays */
        a.motion.event[2] = (motion_event_t){0u << 6 | 3u, P_E4, 70};          /* track 1: stays */
        a.sum = proj_sum(&a);
#define PERC_OK(c) ((c).t[2].engine == ENGI_DRUM && (c).t[2].preset == 0u && !memcmp(&(c).t[2].p[P_E0], KIT, sizeof KIT) && \
                    !memcmp((c).t[2].p, a.t[2].p, P_E0 * sizeof(int16_t)) && \
                    !memcmp((c).t[2].step, a.t[2].step, sizeof a.t[2].step) && \
                    (c).t[0].engine == ENGI_SAMPLE && (c).t[0].p[P_E0] == 2 && (c).t[1].engine == ENGI_SAMPLE)
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN8 with a SAMPLE PERC track: DRUM's kit, the rest of the sound and the steps kept", ok && PERC_OK(c) &&
                     str_eq(ENGINES[ENGI_DRUM]->presets[0].name, "FM DRUMS") &&
                     !memcmp(ENGINES[ENGI_DRUM]->presets[0].e, (int8_t[8])DRUM_KIT_E, 8));
        bad += check("  its motion on CUT goes; a send's and the other track's stay",
                     c.motion.count == 2u && c.motion.event[0].param == P_REV && c.motion.event[1].place == 3u &&
                     motion_valid(&c.motion) && proj_ok(&c));
        bad += check("  imported again: as it is (DRUM now)", proj_import(&d, &c, sizeof c) && !memcmp(&d, &c, sizeof d));
#undef PERC_OK
    }
    {   /* GRAIN / NOISE stay; SLICE -> DRUM (eng_retire.c). Idempotent. */
        project_t a, c;
        static project_store_t st;
        uint32_t i;
        static const int16_t KIT[8] = DRUM_KIT_E;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        for (t = 0; t < NTRK; t++) {
            for (i = 0; i < P_COUNT; i++)
                a.t[t].p[i] = TP[i].def;
            memcpy(a.fm6[t], FM6_INIT, FM6_PACKED);
        }
        a.t[0].engine = ENGI_GRAIN; a.t[0].p[P_LEVEL] = 88;
        a.t[1].engine = ENGI_NOISE; a.t[1].p[P_LEVEL] = 77;
        a.t[2].engine = ENGI_SLICE; a.t[2].p[P_LEVEL] = 66; a.t[2].preset = 1;
        a.t[3].engine = 0;
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN11 GRAIN/NOISE stay, SLICE->DRUM kit; shared LEVEL kept",
                     ok && c.t[0].engine == ENGI_GRAIN && c.t[0].p[P_LEVEL] == 88 &&
                     c.t[1].engine == ENGI_NOISE && c.t[1].p[P_LEVEL] == 77 &&
                     c.t[2].engine == ENGI_DRUM && c.t[2].preset == 0u &&
                     !memcmp(&c.t[2].p[P_E0], KIT, sizeof KIT) && c.t[2].p[P_LEVEL] == 66 &&
                     c.t[3].engine == 0u && proj_ok(&c));
        bad += check("  imported again: as it is",
                     proj_import(&a, &c, sizeof c) && !memcmp(&a, &c, sizeof c));
    }
    {   /* P_IORD bits 10..15: the stage is a send. The order in the low 10 bits still round-trips.
         * An order above 719 with no room in those bits is refused. */
        project_t a, c;
        static project_store_t st;
        uint16_t bits;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        bits = (uint16_t)(12u | (1u << 10) | (1u << 15));
        a.t[0].p[P_IORD] = (int16_t)bits;
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN10 P_IORD send flags round-trip with the order",
                     ok && (uint16_t)c.t[0].p[P_IORD] == bits && ((uint16_t)c.t[0].p[P_IORD] & 1023u) == 12u);
        a.t[0].p[P_IORD] = 800;
        a.sum = proj_sum(&a);
        bad += check("FUN10 P_IORD order above 719 is refused", !proj_pack(&st, &a));
    }
    {   /* FUN11 kit/mute still on disk; X-MOD (dx_mot) is RAM-only — those 4 bytes hold SEND bus knobs. */
        project_t a, c;
        static project_store_t st, funa;
        uint32_t sum, i;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        memcpy(a.dx, DX_KIT_DEF, sizeof a.dx);
        a.dx[0].a[0] = 11; a.dx[0].b[2] = 99; a.dx[3].a[4] = 55;
        a.dx_mute = (uint16_t)(DXG_KICK | (1u << 8) | (4u << 8));
        a.dx_mot[0] = 3; a.dx_mot[1] = 40; a.dx_mot[2] = 2; a.dx_mot[3] = 17;
        for (i = 0; i < FX_BUS_N; i++)
            a.fx_bus[i] = (uint8_t)(10u + i);
        memcpy(a.name, "DX KIT", 6);
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && ((uint32_t *)st.raw)[0] == PROJ_MAGIC &&
             !memcmp(st.raw + PROJ_DX_OFF, a.dx, sizeof a.dx) &&
             st.raw[PROJ_DX_OFF + sizeof a.dx] == (uint8_t)(a.dx_mute & DXG_ALL) &&
             st.raw[PROJ_DX_OFF + sizeof a.dx + 1u] == (uint8_t)(a.dx_mute >> 8) &&
             st.raw[PROJ_DX_OFF + sizeof a.dx + 2u] == 1u &&
             proj_import(&c, &st, sizeof st) &&
             !memcmp(c.dx, a.dx, sizeof a.dx) && c.dx_mute == a.dx_mute &&
             !c.dx_mot[0] && !c.dx_mot[1] && !c.dx_mot[2] && !c.dx_mot[3] &&
             !memcmp(c.fx_bus, a.fx_bus, FX_BUS_N) && !memcmp(c.name, "DX KIT", 6);
        bad += check("FUN14: DRUM-X kit/mute round trip; X-MOD not on disk; SEND bus knobs round trip", ok);
        funa = st;
        store_fun15_layout(&funa, 0x46554E41u);       /* FUNA / FUN10: the FUN15 chain and motion, no kit block */
        memset(funa.raw + PROJ_DX_OFF, 0, PROJ_DX_SIZE);
        sum = proj_hash(funa.raw, PROJ_STORE_SIZE - 4u);
        memcpy(funa.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        ok = proj_import(&c, &funa, sizeof funa) && dx_factory(c.dx) &&
             !c.dx_mute && !c.dx_mot[0] && !c.dx_mot[1] && !c.dx_mot[2] && !c.dx_mot[3] &&
             !memcmp(c.name, "DX KIT", 6);
        bad += check("FUN10 (FUNA): loads with the factory DRUM-X kit (custom kit not on disk)", ok);
    }
    {   /* FUN14: MAP = CURR + SLCR slots on disk; stash RAM-only; SEND bus in reclaimed bytes. */
        project_t a, c;
        static project_store_t st;
        uint32_t i;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        a.t[0].p[P_QUANT] = QN_CURR;
        a.t[1].p[P_QUANT] = QN_ALL;                        /* plain ALL: no lock bit */
        a.t[1].scl_stash_ok = 1;
        a.t[1].scl_stash[0] = 5; a.t[1].scl_stash[1] = 2; a.t[1].scl_stash[2] = 1;
        a.t[2].p[P_QUANT] = QN_CURR;
        a.t[3].p[P_QUANT] = QN_CURR;
        a.t[0].fx_sl_pos = 3; a.t[1].fx_sl_pos = 5; a.t[2].fx_sl_pos = 7; a.t[3].fx_sl_pos = 8;
        a.g[G_SLOWN] = 2;                                 /* legacy: track 1 CHAIN → mask 0xD */
        for (i = 0; i < FX_BUS_N; i++)
            a.fx_bus[i] = (uint8_t)(20u + i * 3u);
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) &&
             (st.raw[PROJ_SCL_OFF] & 0x8Fu) == 0x8Du &&   /* mark + locks t0/t2/t3 */
             ((st.raw[PROJ_SCL_OFF] >> 4) & 7u) == 2u &&  /* legacy owner for old readers */
             (st.raw[PROJ_SL_SEND_OFF] & 0x1Fu) == 0x1Du && /* mask 0xD | mark */
             st.raw[PROJ_SLPOS_OFF] == (uint8_t)(3u | (5u << 4)) &&
             st.raw[PROJ_SLPOS_OFF + 1u] == (uint8_t)(7u | (8u << 4)) &&
             proj_import(&c, &st, sizeof st) &&
             c.t[0].p[P_QUANT] == QN_CURR && c.t[1].p[P_QUANT] == QN_ALL &&
             c.t[2].p[P_QUANT] == QN_CURR && c.t[3].p[P_QUANT] == QN_CURR &&
             !c.t[1].scl_stash_ok && !c.t[1].scl_stash[0] && !c.t[1].scl_stash[1] &&
             !c.t[1].scl_stash[2] && !memcmp(c.fx_bus, a.fx_bus, FX_BUS_N) &&
             c.t[0].fx_sl_pos == 3 && c.t[1].fx_sl_pos == 5 && c.t[2].fx_sl_pos == 7 &&
             c.t[3].fx_sl_pos == 8 && c.g[G_SLOWN] == 0xD;
        bad += check("FUN14: MAP CURR + SLCR slots; stash not on disk; SEND bus knobs round trip", ok);
    }
    {   /* Felucca 1.5 FUNA (np 111): INSERT + FTYPE/ESYNC through proj_import end-to-end */
        project_t c;
        static project_store_t funa;
        int16_t p111[111];
        uint32_t i, t, pos, magic = PROJ_MAGIC_V10, size = PROJ_STORE_SIZE, sum;
        chain_config_t ch;
        memset(&funa, 0, sizeof funa);
        memcpy(funa.raw, &magic, 4);
        memcpy(funa.raw + 4, &size, 4);
        for (i = 0; i < PROJ_NG_WIRE; i++)
            ((int16_t *)(funa.raw + 8))[i] = GP[i].def;
        ((int16_t *)(funa.raw + 8))[G_RTYPE] = 2;      /* HALL */
        ((int16_t *)(funa.raw + 8))[G_RSIZE] = 90;
        ((int16_t *)(funa.raw + 8))[G_RDAMP] = 40;
        funa.raw[62] = 0; funa.raw[63] = NPART; funa.raw[64] = PROJ_PHYS; funa.raw[66] = 111;
        for (i = 0; i < 111u; i++)
            p111[i] = 0;
        p111[P_LEVEL] = 100;
        p111[P_DIST] = 70;
        p111[96] = 5;                                  /* IT_CRUSH */
        p111[97] = 40; p111[98] = 50; p111[99] = 60; p111[100] = 110;
        p111[101] = 1;                                 /* BP */
        p111[102] = 1;                                 /* ESYNC */
        for (i = 0; i < 8u; i++)
            p111[103 + i] = (int16_t)(20 + i);
        pos = 68u;
        for (t = 0; t < NTRK; t++) {
            for (i = 0; i < 111u; i++)
                funa.raw[pos++] = (uint8_t)(p111[i] + 64);
            funa.raw[pos++] = (uint8_t)(t == 0u ? 0u : ENGI_FM6);   /* ANALOG, then FM6 */
            funa.raw[pos++] = 0;
            for (i = 0; i < NSTEP; i++) {
                funa.raw[pos++] = 0; funa.raw[pos++] = 0; funa.raw[pos++] = 0; funa.raw[pos++] = 0;
                funa.raw[pos++] = (uint8_t)(ST_REST << 3);
                funa.raw[pos++] = 96; funa.raw[pos++] = 0; funa.raw[pos++] = 0; funa.raw[pos++] = 100;
            }
        }
        chain_defaults(&ch);
        memcpy(funa.raw + pos, &ch, sizeof ch);        /* then 388 B zero motion (1.5 full block) */
        for (t = 0; t < NTRK; t++)
            memcpy(funa.raw + PROJ_FM6_OFF + t * FM6_PACKED, FM6_INIT, FM6_PACKED);
        memcpy(funa.raw + PROJ_NAME_OFF, "V15 CRUSH", 9);
        sum = proj_hash(funa.raw, PROJ_STORE_SIZE - 4u);
        memcpy(funa.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        ok = proj_import(&c, &funa, sizeof funa);
        bad += check("FUNA np111: imports (Felucca 1.5 INSERT project)", ok);
        bad += check("FUNA np111: CRUSH INSERT -> chain crush params + MIX",
                     ok && c.t[0].p[P_ICBITS] == 40 && c.t[0].p[P_ICRATE] == 50 &&
                     c.t[0].p[P_ICLPF] == 60 && c.t[0].p[P_ICMIX] == 110 &&
                     c.t[0].p[P_DIST] == 70 && c.t[0].p[P_IDMIX] == 127);
        bad += check("FUNA np111: FTYPE/ESYNC and engine values kept (KTR -> INT 0)",
                     ok && c.t[0].p[P_FTYPE] == 1 && c.t[0].p[P_ESYNC] == 1 &&
                     c.t[0].p[P_E0] == 20 && c.t[0].p[P_E7] == 0 && c.t[0].p[P_MORPH] == 0 &&
                     c.t[0].p[P_DRIFT] == 0 && c.t[0].p[P_LEVEL] == 100);
        bad += check("FUNA np111: factory DRUM-X kit, CHAIN reverb = SEND, name kept",
                     ok && dx_factory(c.dx) && !c.dx_mute &&
                     c.g[G_CRTYPE] == c.g[G_RTYPE] && c.g[G_CRSIZE] == c.g[G_RSIZE] &&
                     c.g[G_CRDAMP] == c.g[G_RDAMP] && c.g[G_RTYPE] == 2 &&
                     !memcmp(c.name, "V15 CRUSH", 9) && c.t[0].engine == 0u && c.t[1].engine == ENGI_FM6);
        /* TYPE OFF: dry chain defaults, FTYPE/ESYNC still apply */
        p111[96] = 0; p111[100] = 64;
        pos = 68u;
        for (t = 0; t < NTRK; t++) {
            for (i = 0; i < 111u; i++)
                funa.raw[pos++] = (uint8_t)(p111[i] + 64);
            funa.raw[pos++] = 0; funa.raw[pos++] = 0;
            pos += NSTEP * 9u;
        }
        sum = proj_hash(funa.raw, PROJ_STORE_SIZE - 4u);
        memcpy(funa.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        ok = proj_import(&c, &funa, sizeof funa);
        bad += check("FUNA np111: INSERT TYPE OFF stays dry (ICMIX 0), ESYNC kept",
                     ok && c.t[0].p[P_ICMIX] == 0 && c.t[0].p[P_IPMIX] == 0 &&
                     c.t[0].p[P_ILMIX] == 0 && c.t[0].p[P_IHMIX] == 0 && c.t[0].p[P_ESYNC] == 1);
    }
    bad += chord_mode_wire();
    bad += vib_wire();
    bad += generation_wire();
    bad += generation_legacy_defaults();
    bad += step_wire_freeze();
    bad += fun10_motion110();
    bad += fun20_full_motion();
    bad += mx_wire();
    bad += dx_layer_wire();
    bad += drums_retired();
    bad += drums_lane_levels();
    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}
