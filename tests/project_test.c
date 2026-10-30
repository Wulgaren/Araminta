/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the project formats (firmware/src/project.c, -DPROJ_HOST part): a format 4 ("FUN4", 8-byte
 * steps: before the drum grid), a format 3 ("FUN3", 57 parameters per track: before the modulation matrix), a
 * format 2 ("FUN2", 53: before the SLICER) and a format 1 ("FUN1") project, built byte for byte as the
 * firmware stored them, convert to format 6 ("FUN6"): every old value at its parameter, the parameters
 * added since at their defaults (SLICER OFF,
 * every matrix slot OFF), steps, globals, selection, the engine bytes (0..7 kept: the engines added since
 * were appended); damaged ones are refused. Track 4 of a project written before 1.0 (formats 2 and 3,
 * `parts` 0) was the GM drum part: it becomes a DRUM part 4 (PROJ_DEF_KEEP: project_load gives it DRUM's kit;
 * SAMPLE PERC until 1.0.2), its steps (its lanes' notes as hits) and parameters kept, the drum level / reverb send (G_DRLVL /
 * G_DRREV) as its LEVEL / REV; global id 24 (the drum part's MIDI channel then, REVERB TYPE now) loads as ROOM
 * from every format before FUN7. A FUN4 written while the drums were PHYS's MODEL DRUM (`phys` 1) loads such
 * a track as the DRUM engine with the same sound (its E values moved), other PHYS tracks as they were; one
 * of before 1.0 (`phys` 0) its DUST as MODAL bowed. The steps of a DRUM track of before FUN5 (DRUM, or
 * PHYS DRUM become DRUM) get their lanes' notes as hits, the other notes (a low tom, a crash) kept, the
 * step accent as the hits' accents; other tracks' steps stay as they were (no hits). A SAMPLE track of the retired
 * PERC set (SET 4) loads as DRUM with its default kit, in any format (proj_perc).
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

/* the value parameter k (old id) of track t had in the old project */
static int16_t oldv(uint32_t t, uint32_t k) { return (int16_t)(t * 100u + k * 3u + 1u); }

static const uint8_t OLD_ENG[NTRK] = {7, 0, 6, 0};   /* WHEEL, ANALOG, TRIO; the drum track: 0 (no engine) */

/* FUN13 dropped CHR: old wire SCL 0 was CHR -> MAJ; 1..15 -> 0..14. Tests writing pre-FUN13 images use scl_to_v12. */
static int16_t test_scl_from_v12(int16_t sc)
{
    if (sc <= 0) return 0;
    if (sc > 15) return 0;
    return (int16_t)(sc - 1);
}
static int16_t test_scl_to_v12(int16_t sc)
{
    if (sc <= 0) return 0;                              /* MAJ stored as old CHR slot; loads back as MAJ */
    if (sc > 14) return 15;
    return (int16_t)(sc + 1);
}
/* a store packed as FUN16, rewritten to the FUN15 layout (5-byte chain rows, 72 motion records 48 bytes later),
 * for the tests that check what an older reader sees */
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

static void fill_v2_track(proj_trk_v2_t *d, uint32_t t)
{
    uint32_t k;
    for (k = 0; k < PROJ_NP_V2; k++)
        d->p[k] = oldv(t, k);
    d->engine = OLD_ENG[t];
    d->preset = (uint8_t)(t + 5u);
    for (k = 0; k < NSTEP; k++) {
        step8_t *s = &d->step[k];
        s->note[0] = (uint8_t)(36u + (k + t) % 40u);
        s->n = (uint8_t)(k % 3u);
        s->time = (uint8_t)(k % 3u);
        s->flags = (uint8_t)(k & 3u);
        s->vel = (uint8_t)(64u + t);
    }
}

/* today's steps n are the old steps o (8 bytes each), with no hits */
static int steps_same(const step_t *n, const step8_t *o)
{
    uint32_t k, j;
    for (k = 0; k < NSTEP; k++) {
        for (j = 0; j < 4u; j++)
            if (step_note(&n[k], j) != o[k].note[j])
                return 0;
        if (n[k].n != o[k].n || n[k].time != o[k].time || step_fl(&n[k]) != o[k].flags ||
            step_ratchet(&n[k]) != 1u || step_nudge(&n[k]) != 0 ||
            n[k].vel != o[k].vel || n[k].hit || n[k].acc)
            return 0;
    }
    return 1;
}

/* .. as a DRUM track of before the grid gets them (proj_grid: its lanes' notes as hits) */
static int steps_grid_same(const step_t *n, const step8_t *o)
{
    uint32_t k, j;
    for (k = 0; k < NSTEP; k++) {
        step_t s;
        memset(&s, 0, sizeof s);
        for (j = 0; j < 4u; j++) step_set_note(&s, j, o[k].note[j]);
        s.n = o[k].n; s.time = o[k].time; step_set_fl(&s, o[k].flags); s.vel = o[k].vel;
        step_to_grid(&s);
        for (j = 0; j < 4u; j++)
            if (step_note(&n[k], j) != step_note(&s, j))
                return 0;
        if (n[k].n != s.n || n[k].time != s.time || step_fl(&n[k]) != step_fl(&s) ||
            step_ratchet(&n[k]) != step_ratchet(&s) || step_nudge(&n[k]) != step_nudge(&s) ||
            n[k].vel != s.vel || n[k].hit != s.hit || n[k].acc != s.acc)
            return 0;
    }
    return 1;
}

/* today's project q as format 4 stored it (8-byte steps; the hits dropped) */
static void to_v4(project_v4_t *v, const project_t *q)
{
    uint32_t t, k;
    memset(v, 0, sizeof *v);
    v->magic = PROJ_MAGIC_V4;
    v->size = sizeof *v;
    memcpy(v->g, q->g, sizeof v->g);
    v->sel = q->sel;
    v->parts = q->parts;
    v->phys = q->phys;
    for (t = 0; t < NTRK; t++) {
        for (k = 0; k < 61u; k++)
            v->t[t].p[k] = k == (uint32_t)P_SCALE ? test_scl_to_v12(q->t[t].p[k]) : q->t[t].p[k];
        for (k = 0; k < 8u; k++) v->t[t].p[61u + k] = q->t[t].p[P_E0 + k];
        v->t[t].engine = q->t[t].engine;
        v->t[t].preset = q->t[t].preset;
        for (k = 0; k < NSTEP; k++) {
            for (uint32_t j = 0; j < 4u; j++)
                v->t[t].step[k].note[j] = (uint8_t)step_note(&q->t[t].step[k], j);
            v->t[t].step[k].n = q->t[t].step[k].n;
            v->t[t].step[k].time = q->t[t].step[k].time;
            v->t[t].step[k].flags = (uint8_t)step_fl(&q->t[t].step[k]);
            v->t[t].step[k].vel = q->t[t].step[k].vel;
        }
    }
    v->sum = proj_hash(v, sizeof *v - 4u);
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
static int track_ok(const proj_trk_t *n, const proj_trk_v2_t *o, uint32_t t, int drum, int16_t lvl, int16_t rev)
{
    uint32_t k;
    int ok = (drum ? n->engine == ENGI_DRUM && n->preset == PROJ_DEF_KEEP && steps_grid_same(n->step, o->step)
                   : n->engine == o->engine && n->preset == o->preset && steps_same(n->step, o->step));
    for (k = 0; k <= P_DETUNE; k++)
        ok &= n->p[k] == (drum && k == P_LEVEL ? lvl : drum && k == P_REV ? rev
                         : k == P_TAPEMIX ? 0               /* the old GLMOD id: EFFECTS TAPE MIX now */
                         : k == P_CHOR ? TP[P_CHOR].def   /* bus CHO send retired: FXPOS default */
                         : k == P_SCALE ? test_scl_from_v12(oldv(t, k))
                         : oldv(t, k));
    ok &= n->p[P_SLCR] == 0 && n->p[P_SLPAT] == TP[P_SLPAT].def && n->p[P_SLRATE] == TP[P_SLRATE].def &&
          n->p[P_SLDEPTH] == TP[P_SLDEPTH].def;
    for (k = P_M1SRC; k <= P_M4AMT; k++)
        ok &= n->p[k] == 0;                     /* every matrix slot OFF */
    for (k = 0; k < 8u; k++)
        ok &= n->p[P_E0 + k] == (k == 7u && o->engine == ENGI_ANALOG ? 0 : oldv(t, 45u + k));
    return ok;
}

/* a format 3 track (57 parameters: the SLICER at 45..48, P_E0 49) */
static int16_t oldv3(uint32_t t, uint32_t k) { return (int16_t)(t * 200u + k * 5u + 3u); }
static void fill_v3_track(proj_trk_v3_t *d, uint32_t t)
{
    uint32_t k;
    for (k = 0; k < PROJ_NP_V3; k++)
        d->p[k] = oldv3(t, k);
    d->engine = (uint8_t)(t * 3u);
    d->preset = (uint8_t)(t + 1u);
    for (k = 0; k < NSTEP; k++) {
        step8_t *s = &d->step[k];
        s->note[0] = (uint8_t)(40u + (k * 7u + t) % 40u);
        s->n = (uint8_t)((k + t) % 3u);
        s->time = (uint8_t)(k % 3u);
        s->flags = (uint8_t)((k + 1u) & 3u);
        s->vel = (uint8_t)(70u + t);
    }
}
static int track_v3_ok(const proj_trk_t *n, const proj_trk_v3_t *o, uint32_t t)
{
    uint32_t k;
    int ok = n->engine == o->engine && n->preset == o->preset && steps_same(n->step, o->step);
    for (k = 0; k <= P_SLDEPTH; k++)
        ok &= n->p[k] == (k == P_TAPEMIX ? 0
                         : k == P_CHOR ? TP[P_CHOR].def
                         : k == P_SCALE ? test_scl_from_v12(oldv3(t, k))
                         : oldv3(t, k));  /* CHO send -> FXPOS; GLMOD -> TAPE MIX 0 */
    for (k = P_M1SRC; k <= P_M4AMT; k++)
        ok &= n->p[k] == TP[k].def && TP[k].def == 0;
    for (k = 0; k < 8u; k++)
        ok &= n->p[P_E0 + k] == (k == 7u && o->engine == ENGI_ANALOG ? 0 : oldv3(t, 49u + k));
    return ok;
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
    q.t[1].p[P_E7] = 12; q.t[1].p[P_MORPH] = 30; q.t[1].p[P_DRIFT] = 40;
    q.motion.count = 3; q.motion.on = 1;
    q.motion.event[0] = (motion_event_t){(uint8_t)(1u << 6), P_E7, 12};
    q.motion.event[1] = (motion_event_t){(uint8_t)(1u << 6), P_MORPH, 30};
    q.motion.event[2] = (motion_event_t){(uint8_t)(1u << 6), P_DRIFT, 40};
    ok = proj_pack(&st, &q) && (st.raw[67] & 2u) && proj_import(&loaded, &st, sizeof st);
    bad += check("OSC 2: a new record keeps INT / MORPH / DRIFT and their motion",
                 ok && loaded.t[1].p[P_E7] == 12 && loaded.t[1].p[P_MORPH] == 30 &&
                 loaded.t[1].p[P_DRIFT] == 40 && loaded.motion.count == 3);
    ok = proj_pack(&st, &q);
    st.raw[67] &= (uint8_t)~2u;                          /* what firmware before 3.5 wrote */
    sum = proj_hash(st.raw, PROJ_STORE_SIZE - 4u);
    memcpy(st.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
    ok = ok && proj_import(&loaded, &st, sizeof st);
    bad += check("OSC 2: an older record loads the three 0 and drops their motion",
                 ok && loaded.t[1].p[P_E7] == 0 && loaded.t[1].p[P_MORPH] == 0 &&
                 loaded.t[1].p[P_DRIFT] == 0 && loaded.motion.count == 0);
    return bad;
}

int main(void)
{
    static project_v4_t v4;
    static project_v3_t v3;
    static project_v2_t v2;
    static project_v1_t v1;
    static project_t q, q2;
    static union {
        project_t v5;
        project_v4_t v4;
        project_v3_t v3;
        project_v2_t v2;
        project_v1_t v1;
    } buf;
    uint32_t i, t;
    int bad = fx_motion_wire() + tape_wire() + osc2_migration(), ok;

    bad += check("layout: SLICER after DETUNE, then the matrix just before P_E0",
                 P_SLCR == P_DETUNE + 1 && P_SLDEPTH + 1 == P_M1SRC && P_M4AMT + 1 == P_FM1_ATK && P_FM4_LEVEL + 1 == P_CHRD && P_VOIC + 1 == P_LN0 &&
                 P_LN7 + 1 == P_LSYNC && P_LSYNC + 3 == P_SQNT && P_SQNT + 1 == P_SPRD && P_SPRD + 1 == P_IORD &&
                 P_IHMIX + 1 == P_FTYPE && P_FTYPE + 1 == P_ESYNC && P_ESYNC + 1 == P_E0 && P_E0 == 119 &&
                 P_COUNT == 127u && PROJ_NP_V3 == PROJ_NP_V2 + 4u);
    bad += check("FUN10 fits one flash object, the retained cache in NOINIT", sizeof(project_store_t) <= 4096u - 256u &&
                 sizeof(project_store_t) == 3840u && 0xC8u + 4u * sizeof(project_store_t) <= 0x3D50u);

    /* format 2, as written before the SLICER */
    memset(&v2, 0, sizeof v2);
    v2.magic = PROJ_MAGIC_V2;
    v2.size = sizeof v2;
    for (i = 0; i < PROJ_NG_V2; i++)
        v2.g[i] = (int16_t)(500 + i);
    v2.sel = 2;
    for (t = 0; t < NTRK; t++)
        fill_v2_track(&v2.t[t], t);
    v2.sum = proj_hash(&v2, sizeof v2 - 4u);
    bad += check("FUN2 image is 2552 bytes (as stored)", sizeof v2 == 2552u);
    memcpy(&buf, &v2, sizeof v2);
    ok = proj_import(&q, &buf, (int)sizeof v2);
    bad += check("FUN2 -> FUN6: converted, valid format 5 slot", ok && proj_ok(&q) && q.magic == PROJ_MAGIC);
    ok = q.sel == 2;
    for (i = 0; i < PROJ_NG_WIRE; i++)
        ok &= q.g[i] == (i == G_RTYPE || i == G_DRLVL || i == G_DRREV ? 0
                         : i == G_TSPEED || i == G_TWOW || i == G_TDROP || i == G_TMIX || i == G_TAPEMODE ? GP[i].def
                         : (int16_t)(500 + i));
    ok &= q.g[G_CRTYPE] == q.g[G_RTYPE] && q.g[G_CRSIZE] == q.g[G_RSIZE] && q.g[G_CRDAMP] == q.g[G_RDAMP];
    bad += check("FUN2 -> FUN6: globals (id 24, the old drum channel: ROOM) and selected track", ok);
    ok = 1;
    for (t = 0; t < NTRK; t++)
        ok &= track_ok(&q.t[t], &v2.t[t], t, t == 3u, 127, 127);   /* (G_DRLVL / G_DRREV 525 / 526: 127) */
    bad += check("FUN2 -> FUN6: every parameter mapped, SLICER and matrix OFF (4 tracks)", ok);

    bad += check("FUN2 -> FUN6: engine bytes kept (WHEEL 7, ANALOG 0, TRIO 6)",
                 q.t[0].engine == 7 && q.t[1].engine == 0 && q.t[2].engine == 6 &&
                 str_eq(ENGINES[7]->name, "WHEEL") && str_eq(ENGINES[6]->name, "TRIO") && NENGINES > 8);
    bad += check("FUN2 -> FUN6: the drum track -> part 4, DRUM (its kit on load), steps kept (lanes as hits)",
                 q.parts == NPART && q.t[3].engine == ENGI_DRUM && str_eq(ENGINES[ENGI_DRUM]->name, "DRUM-X") &&
                 TRK_DEF[3][0] == ENGI_DRUM && q.t[3].preset == PROJ_DEF_KEEP &&
                 steps_grid_same(q.t[3].step, v2.t[3].step));

    /* format 3 (1.0, four parts), as written before the modulation matrix */
    memset(&v3, 0, sizeof v3);
    v3.magic = PROJ_MAGIC_V3;
    v3.size = sizeof v3;
    for (i = 0; i < PROJ_NG_WIRE; i++)
        v3.g[i] = (int16_t)(600 + i);
    v3.sel = 3;
    v3.parts = NPART;
    for (t = 0; t < NTRK; t++)
        fill_v3_track(&v3.t[t], t);
    v3.sum = proj_hash(&v3, sizeof v3 - 4u);
    bad += check("FUN3 image is 2584 bytes (as stored)", sizeof v3 == 2584u);
    memcpy(&buf, &v3, sizeof v3);
    ok = proj_import(&q, &buf, (int)sizeof v3) && proj_ok(&q) && q.magic == PROJ_MAGIC && q.sel == 3 &&
         q.parts == NPART;
    for (i = 0; i < PROJ_NG_WIRE; i++)
        ok &= q.g[i] == (i == G_RTYPE ? 0
                         : i == G_TSPEED || i == G_TWOW || i == G_TDROP || i == G_TMIX || i == G_TAPEMODE ? GP[i].def
                         : (int16_t)(600 + i));
    ok &= q.g[G_CRTYPE] == q.g[G_RTYPE] && q.g[G_CRSIZE] == q.g[G_RSIZE] && q.g[G_CRDAMP] == q.g[G_RDAMP];
    for (t = 0; t < NTRK; t++)
        ok &= track_v3_ok(&q.t[t], &v3.t[t], t);
    bad += check("FUN3 -> FUN6: every parameter kept, the matrix OFF, steps, globals (24: ROOM)", ok);

    /* a FUN3 written before 1.0 (parts 0: track 4 the drum part, engine byte 0) */
    v3.parts = 0;
    v3.t[3] = v3.t[0];
    v3.t[3].engine = 0;
    v3.t[3].preset = 0;
    v3.g[G_DRLVL] = 90;
    v3.g[G_DRREV] = 20;
    v3.sum = proj_hash(&v3, sizeof v3 - 4u);
    memcpy(&buf, &v3, sizeof v3);
    ok = proj_import(&q2, &buf, (int)sizeof v3) && proj_ok(&q2) && q2.parts == NPART && q2.t[3].engine == ENGI_DRUM &&
         q2.t[3].preset == PROJ_DEF_KEEP && q2.t[3].p[P_LEVEL] == 90 && q2.t[3].p[P_REV] == 20 &&
         q2.t[3].p[P_PAN] == oldv3(0, P_PAN) && q2.t[3].p[P_SLEN] == oldv3(0, P_SLEN) &&
         q2.t[3].p[P_SLCR] == oldv3(0, P_SLCR) && q2.t[3].p[P_M1SRC] == 0 &&
         steps_grid_same(q2.t[3].step, v3.t[0].step) && !memcmp(&q2.t[0], &q.t[0], sizeof q.t[0]);
    bad += check("FUN3 before 1.0: the drum track -> part 4 (LEVEL / REV from G_DRLVL / G_DRREV)", ok);

    /* a FUN6 round trip: stored as is (a matrix slot set; WHEEL engine 7; a DRUM track with
     * notes on its lanes stays so: only an older format is converted) */
    q.t[1].engine = 7;
    q.t[2].p[P_M2SRC] = 6;
    q.t[2].p[P_M2DST] = 11;
    q.t[2].p[P_M2AMT] = -17;
    q.t[0].engine = ENGI_DRUM;
    q.t[0].step[0] = (step_t){{36, 42, 0, 0}, 2, ST_NOTE, 0, 96, 1u << DV_SNARE, 1u << DV_SNARE};
    q.sum = proj_sum(&q);
    memcpy(&buf, &q, sizeof q);
    bad += check("FUN6 -> FUN6: as stored (a matrix slot, engine 7, a DRUM step with notes and hits)",
                 proj_import(&q2, &buf, (int)sizeof q) && !memcmp(&q, &q2, sizeof q) && q2.t[1].engine == 7 &&
                 q2.t[2].p[P_M2AMT] == -17 && q2.t[0].step[0].n == 2u && q2.t[0].step[0].hit == 1u << DV_SNARE);
    q.t[0].engine = 3;
    q.t[0].step[0] = q.t[0].step[1];
    for (t = 0; t < NTRK; t++) {                      /* an older format has no OSC 2: the engine's defaults */
        int an = q.t[t].engine == ENGI_ANALOG;
        if (an)
            q.t[t].p[P_E7] = 0;
        q.t[t].p[P_MORPH] = an ? 0 : TP[P_MORPH].def;
        q.t[t].p[P_DRIFT] = an ? 0 : TP[P_DRIFT].def;
    }
    q.sum = proj_sum(&q);

    {
        project_v5_t old;
        memset(&old, 0, sizeof old);
        memcpy(old.g, q.g, sizeof old.g); old.sel = q.sel; old.parts = q.parts; old.phys = q.phys;
        for (uint32_t k = 0; k < NTRK; k++) {
            for (uint32_t j = 0; j < 61u; j++) old.t[k].p[j] = j == (uint32_t)P_SCALE ? test_scl_to_v12(q.t[k].p[j]) : q.t[k].p[j];
            for (uint32_t j = 0; j < 8u; j++) old.t[k].p[61u + j] = q.t[k].p[P_E0 + j];
            old.t[k].engine = q.t[k].engine; old.t[k].preset = q.t[k].preset;
            for (uint32_t j = 0; j < NSTEP; j++) {
                const step_t *s = &q.t[k].step[j];
                step10_t *d = &old.t[k].step[j];
                for (uint32_t b = 0; b < 4u; b++) d->note[b] = (uint8_t)step_note(s, b);
                d->n = s->n; d->time = s->time; d->flags = (uint8_t)step_fl(s);
                d->vel = s->vel; d->hit = s->hit; d->acc = s->acc;
            }
        }
        old.magic = PROJ_MAGIC_V5;
        old.size = sizeof old;
        old.sum = proj_hash(&old, sizeof old - 4u);
        bad += check("FUN5 -> FUN6: sounds, grid and reserved lane bytes kept, no chain",
            proj_import(&q2, &old, sizeof old) && !memcmp(q2.t, q.t, sizeof q.t) &&
            !q2.chain.count && chain_valid(&q2.chain));
        old.sum ^= 1u;
        bad += check("FUN5: damaged checksum refused", !proj_import(&q2, &old, sizeof old));
        q.chain.count = 2;
        q.chain.row[0] = chain_row_of(0, 4);
        q.chain.row[1] = chain_row_of(3, 16);
        q.sum = proj_sum(&q);
        bad += check("FUN6: chain round trip", proj_import(&q2, &q, sizeof q) &&
            !memcmp(&q.chain, &q2.chain, sizeof q.chain));
        q.chain.row[0].repeat = 0;
        q.sum = proj_sum(&q);
        bad += check("FUN6: invalid row refused even with a correct checksum", !proj_import(&q2, &q, sizeof q));
        chain_defaults(&q.chain);
        q.sum = proj_sum(&q);
    }

    /* format 4 (1.0 development builds): 8-byte steps; a DRUM track's lane notes become its grid */
    for (t = 0; t < NTRK; t++) {
        int an = q.t[t].engine == ENGI_ANALOG;
        if (an)
            q.t[t].p[P_E7] = 0;
        q.t[t].p[P_MORPH] = an ? 0 : TP[P_MORPH].def;
        q.t[t].p[P_DRIFT] = an ? 0 : TP[P_DRIFT].def;
    }
    to_v4(&v4, &q);
    bad += check("FUN4 image is 2680 bytes (as stored)", sizeof v4 == 2680u);
    memcpy(&buf, &v4, sizeof v4);
    ok = proj_import(&q2, &buf, (int)sizeof v4) && proj_ok(&q2) && q2.magic == PROJ_MAGIC && q2.sel == q.sel &&
         !memcmp(q2.g, q.g, sizeof q.g);
    for (t = 0; t < NTRK; t++)
        ok &= !memcmp(q2.t[t].p, q.t[t].p, sizeof q.t[t].p) && q2.t[t].engine == q.t[t].engine &&
              steps_same(q2.t[t].step, v4.t[t].step);
    bad += check("FUN4 -> FUN6: parameters, engines, globals; steps as they were, no hits", ok);
    {   /* track 2 a DRUM track: BEAT-like steps, a chord, a low tom 41, a crash 49, a step accent, a TIE */
        static const step8_t D[6] = {
            {{36, 0, 0, 0}, 1, ST_NOTE, SF_ACCENT, 96}, {{42, 38, 0, 0}, 2, ST_NOTE, 0, 100},
            {{41, 36, 0, 0}, 2, ST_NOTE, SF_ACCENT, 96}, {{49, 0, 0, 0}, 1, ST_NOTE, 0, 96},
            {{0, 0, 0, 0}, 0, ST_TIE, 0, 0}, {{38, 46, 0, 0}, 2, ST_REST, 0, 96},
        };
        const step_t *s = q2.t[1].step;
        v4.t[1].engine = ENGI_DRUM;
        for (i = 0; i < 6u; i++)
            v4.t[1].step[i] = D[i];
        v4.sum = proj_hash(&v4, sizeof v4 - 4u);
        memcpy(&buf, &v4, sizeof v4);
        ok = proj_import(&q2, &buf, (int)sizeof v4) && q2.t[1].engine == ENGI_DRUM;
        ok &= s[0].n == 0 && s[0].hit == 1u << DV_KICK && s[0].acc == 1u << DV_KICK && !(step_fl(&s[0]) & SF_ACCENT) &&
              s[0].vel == 96 && s[0].time == ST_NOTE;
        ok &= s[1].n == 0 && s[1].hit == ((1u << DV_HATC) | (1u << DV_SNARE)) && !s[1].acc && s[1].vel == 100;
        ok &= s[2].n == 1 && step_note(&s[2], 0) == 41 && s[2].hit == 1u << DV_KICK && (step_fl(&s[2]) & SF_ACCENT) &&
              step_lanes(&s[2]) == ((1u << DV_KICK) | (1u << DV_TOM)) && step_accents(&s[2]) == step_lanes(&s[2]);
        ok &= s[3].n == 1 && step_note(&s[3], 0) == 49 && !s[3].hit && step_lanes(&s[3]) == 1u << DV_BELL;
        ok &= s[4].time == ST_TIE && !s[4].hit && s[5].time == ST_REST && s[5].n == 2 && !s[5].hit;
        ok &= steps_same(q2.t[0].step, v4.t[0].step);   /* (not DRUM: as it was) */
        bad += check("FUN4 -> FUN6: a DRUM track's lane notes -> hits (accents too), a low tom / crash kept", ok);
    }
    v3.t[0].p[7]++;
    memcpy(&buf, &v3, sizeof v3);
    bad += check("FUN3 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v3));
    memcpy(&buf, &q, sizeof q);
    buf.v5.magic = PROJ_MAGIC_V3;
    bad += check("FUN6 size with a FUN3 magic: refused", !proj_import(&q2, &buf, (int)sizeof q));

    /* damaged / wrong size */
    v2.t[1].p[3]++;
    memcpy(&buf, &v2, sizeof v2);
    bad += check("FUN2 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v2));
    v2.t[1].p[3]--;
    memcpy(&buf, &v2, sizeof v2);
    bad += check("FUN2 with a wrong length: refused", !proj_import(&q2, &buf, (int)sizeof v2 - 2));
    memcpy(&buf, &q, sizeof q);
    buf.v5.magic = PROJ_MAGIC_V2;
    bad += check("FUN6 size with a FUN2 magic: refused", !proj_import(&q2, &buf, (int)sizeof q));

    /* format 1: one instrument -> track 1, the others their defaults */
    memset(&v1, 0, sizeof v1);
    v1.magic = PROJ_MAGIC_V1;
    v1.size = sizeof v1;
    for (i = 0; i < PROJ_NG_V2; i++)
        v1.g[i] = (int16_t)(700 + i);
    fill_v2_track(&v1.t, 0);
    v1.sum = proj_hash(&v1, sizeof v1 - 4u);
    memcpy(&buf, &v1, sizeof v1);
    ok = proj_import(&q, &buf, (int)sizeof v1) && proj_ok(&q) && track_ok(&q.t[0], &v1.t, 0, 0, 0, 0) && q.g[5] == 705 &&
         q.parts == NPART;
    for (t = 1; t < NTRK; t++)
        ok &= q.t[t].preset == PROJ_DEF_SOUND && q.t[t].engine == trk_def_engine(t) && q.t[t].p[P_SLCR] == 0 && q.t[t].p[P_LEVEL] == TP[P_LEVEL].def &&
              q.t[t].p[P_E0] == ENGINES[trk_def_engine(t)]->edit[0].def && q.t[t].step[0].time == ST_REST;
    bad += check("FUN1 -> FUN6: track 1 mapped, tracks 2..4 defaults", ok);

    /* PHYS MODEL DRUM (a FUN4 with phys 1) -> the DRUM engine; the old DRUM KIT preset becomes DRUM's */
    {
        static const int16_t OLD_KIT[8] = {4, 64, 70, 64, 64, 100, 0, 0};   /* PHYS DRUM KIT before 1.0 */
        static const int16_t E0[8] = {4, 70, 80, 60, 50, 110, 100, 70}, E1[8] = {2, 10, 20, 30, 40, 50, 60, 70};
        for (t = 0; t < NTRK; t++)
            q.t[t].p[P_DIST] = 0;                     /* FUN4 bypass predates the separate DIST MIX. */
        q.t[0].engine = ENGI_PHYS;                      /* MODEL DRUM, PERC 70 (CYM), KICK 100 (ROUND) */
        q.t[0].preset = 9;
        q.t[1].engine = ENGI_PHYS;                      /* MEMB (2: DUST only before phys 1) */
        q.t[2].engine = ENGI_PHYS;
        q.t[3].engine = ENGI_PHYS;
        for (i = 0; i < 8u; i++) {
            q.t[0].p[P_E0 + i] = E0[i];
            q.t[1].p[P_E0 + i] = E1[i];
            q.t[2].p[P_E0 + i] = OLD_KIT[i];
            q.t[3].p[P_E0 + i] = (int16_t)(i ? 30 : PM_SYMP);
        }
        q.phys = 1;
        for (i = 1; i < NSTEP; i++)                     /* (the other steps rests) */
            q.t[0].step[i] = (step_t){{50, 0, 0, 0}, 1, ST_REST, 0, 0, 0, 0};
        q.t[0].step[0] = (step_t){{36, 42, 0, 0}, 2, ST_NOTE, 0, 96, 0, 0};
        q.sum = proj_sum(&q);
        to_v4(&v4, &q);
        memcpy(&buf, &v4, sizeof v4);
        ok = proj_import(&q2, &buf, (int)sizeof v4) && proj_ok(&q2) && q2.phys == PROJ_PHYS &&
             q2.t[0].engine == ENGI_DRUM && q2.t[0].preset == 0 && str_eq(ENGINES[ENGI_DRUM]->name, "DRUM-X") &&
             q2.t[0].p[P_E0] == 2 && q2.t[0].p[P_E6] == 1 && q2.t[0].p[P_E7] == 0;
        for (i = 1; i < 6u; i++)
            ok &= q2.t[0].p[P_E0 + i] == E0[i];         /* TUNE TONE DECY SNAP ACC in place */
        ok &= q2.t[1].engine == ENGI_PHYS && !memcmp(q2.t[1].p, q.t[1].p, sizeof q.t[1].p);
        ok &= q2.t[3].engine == ENGI_PHYS && !memcmp(q2.t[3].p, q.t[3].p, sizeof q.t[3].p);
        ok &= q2.t[2].engine == ENGI_DRUM;              /* PHYS MODEL DRUM -> DRUM-X (E values via drum_from_phys) */
        {
            static const int16_t CONV[8] = {0, 64, 70, 64, 64, 100, 0, 0};   /* OLD_KIT after drum_from_phys */
            for (i = 0; i < 8u; i++)
                ok &= q2.t[2].p[P_E0 + i] == CONV[i];
        }
        ok &= !memcmp(&q2.t[0].step[1], &q.t[0].step[1], sizeof q.t[0].step - sizeof(step_t)) &&
              q2.t[0].p[P_LEVEL] == q.t[0].p[P_LEVEL];
        ok &= q2.t[0].step[0].n == 0 && q2.t[0].step[0].hit == ((1u << DV_KICK) | (1u << DV_HATC));   /* the grid */
        ok &= !memcmp(q2.t[1].step, q.t[1].step, sizeof q.t[1].step);   /* (PHYS: as they were) */
        bad += check("FUN4 phys 1: PHYS MODEL DRUM -> DRUM engine (its lane notes as hits), MEMB kept", ok);
        memcpy(&buf, &q2, sizeof q2);
        bad += check("FUN6 after it: imported again, as it is", proj_import(&q, &buf, (int)sizeof q2) &&
                                                                !memcmp(&q, &q2, sizeof q));
        q.phys = 0;                                     /* before 1.0: MODEL 2 was DUST */
        q.t[1].engine = ENGI_PHYS;
        q.sum = proj_sum(&q);
        to_v4(&v4, &q);
        memcpy(&buf, &v4, sizeof v4);
        ok = proj_import(&q2, &buf, (int)sizeof v4) && q2.phys == PROJ_PHYS && q2.t[1].engine == ENGI_PHYS &&
             q2.t[1].p[P_E0] == PM_MODAL && q2.t[1].p[P_E6] == 64;
        bad += check("FUN4 phys 0: PHYS DUST -> MODAL bowed", ok);
    }

    {   /* the FUN7 name: the reserved tail's last 12 bytes, covered by the hash; zero = no name */
        project_t a, c;
        project_store_t st, st2;
        uint32_t i, zero = 1;
        bad += check("FUN13 name at the end of the reserved tail; kit before FM6; SCALE LOCK before CRV",
                     PROJ_NAME_OFF == 3824u && PROJ_FM6_OFF == 3312u && PROJ_DX_OFF == 3216u &&
                     PROJ_DX_OFF + PROJ_DX_SIZE == PROJ_FM6_OFF &&
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
        bad += check("FUN6..FUN1 imports: no name", !q2.name[0] && !q.name[0]);
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
            uint32_t pos = 68u, pos9 = 68u, j;
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

#if !FELUCCA_FM4
    {   /* DIGITAL tracks (engine 1, retired: src/fm4_convert.c) in FUN8 / FUN7 / FUN4 / FUN1 projects load as FM6 with
         * the converted patch as the track's own; their motion on the EDIT or OP ENV values goes, the rest stays */
        project_t a, c, d;
        static project_store_t st;
        static uint8_t v7[PROJ_STORE_V7];
        static project_v4_t w4;
        static project_v1_t w1;
        uint8_t v[FP_SIZE + 1u], pk[FM6_PACKED];
        int16_t p[P_COUNT];
        uint32_t i, sum;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        for (t = 0; t < NTRK; t++) {
            for (i = 0; i < P_COUNT; i++)
                a.t[t].p[i] = param_desc_of(t == 1u ? ENGI_DIGITAL : 0u, i)->def;
            a.t[t].engine = t == 1u ? ENGI_DIGITAL : 0u;
            a.t[t].preset = t == 1u ? 5u : 0u;
            memcpy(a.fm6[t], FM6_INIT, FM6_PACKED);
        }
        fm4_preset_values(a.t[1].p, 5);                 /* DIGITAL PAD, an OP ENV edit on op 2 */
        a.t[1].p[P_FM2_ATK] = 30;
        a.t[1].p[P_FM2_LEVEL] = 100;
        a.t[1].p[P_LEVEL] = 90;
        memcpy(p, a.t[1].p, sizeof p);
        fm4_convert(p, v);
        fm6_pack(v, pk);
        a.motion.count = 5;
        a.motion.event[0] = (motion_event_t){1u << 6 | 3u, P_E4, 20};          /* track 2: INDEX */
        a.motion.event[1] = (motion_event_t){1u << 6 | 4u, P_FM1_LEVEL, 50};   /* track 2: OP LEVEL */
        a.motion.event[2] = (motion_event_t){1u << 6 | 5u, P_CHOR, 60};        /* track 2: a send: stays */
        a.motion.event[3] = (motion_event_t){0u << 6 | 3u, P_E4, 70};          /* track 1 (ANALOG): stays */
        a.motion.event[4] = (motion_event_t){1u << 6 | 9u, P_E7, 0};           /* track 2: E7 */
        a.sum = proj_sum(&a);
#define FM4_OK(c) ((c).t[1].engine == ENGI_FM6 && (c).t[1].preset == 4u && !memcmp((c).fm6[1], pk, FM6_PACKED) && \
                   !memcmp((c).t[1].p, p, sizeof p) && (c).t[0].engine == 0u && !memcmp((c).fm6[0], FM6_INIT, FM6_PACKED))
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN8 with a DIGITAL track: FM6, the converted patch its own, PTCH / preset FM6 PAD", ok && FM4_OK(c));
        bad += check("  its motion on INDEX / OP LEVEL / E7 goes; a send's and the other track's stay",
                     c.motion.count == 2u && c.motion.event[0].param == P_CHOR && c.motion.event[1].place == 3u &&
                     c.motion.event[1].param == P_E4 && motion_valid(&c.motion) && proj_ok(&c));
        bad += check("  imported again: as it is (FM6 now)", proj_import(&d, &c, sizeof c) && !memcmp(&d, &c, sizeof d));
        (void)sum;                                      /* the same as FUN7 (as the FUN8 block above) */
        {
            int16_t p7[P_COUNT];
            memcpy(p7, p, sizeof p7);
            ins_migrate(p7, 91u);                       /* FUN7 import rebuilds the insert block */
            bad += check("FUN7 with a DIGITAL track: the same", old_pack(v7, &a, OLD_FUN7, 91u) == sizeof v7 &&
                         proj_import(&c, v7, sizeof v7) &&
                         c.t[1].engine == ENGI_FM6 && c.t[1].preset == 4u && !memcmp(c.fm6[1], pk, FM6_PACKED) &&
                         !memcmp(c.t[1].p, p7, sizeof p7) && c.t[0].engine == 0u &&
                         !memcmp(c.fm6[0], FM6_INIT, FM6_PACKED) && c.motion.count == 2u);
        }
        memset(&a.motion, 0, sizeof a.motion);
        a.sum = proj_sum(&a);
        to_v4(&w4, &a);
        memcpy(&buf, &w4, sizeof w4);
        memcpy(p, a.t[1].p, sizeof p);                  /* (FUN4 had no OP ENV values: they load as defaults) */
        for (i = P_FM1_ATK; i <= P_FM4_LEVEL; i++)
            p[i] = TP[i].def;
        fm4_convert(p, v);
        fm6_pack(v, pk);
        ins_migrate(p, PROJ_NP_V4);                     /* FUN4 np: insert defaults + FXPOS */
        ok = proj_import(&c, &buf, (int)sizeof w4) && FM4_OK(c);
        bad += check("FUN4 with a DIGITAL track (no OP ENV then: their defaults): FM6, converted", ok);
        memset(&w1, 0, sizeof w1);
        w1.magic = PROJ_MAGIC_V1;
        w1.size = sizeof w1;
        for (i = 0; i < PROJ_NG_V2; i++)
            w1.g[i] = GP[i].def;
        for (i = 0; i < PROJ_NP_V2 - 8u; i++)
            w1.t.p[i] = TP[i].def;
        for (i = 0; i < 8u; i++)
            w1.t.p[PROJ_NP_V2 - 8u + i] = DIGITAL_PRESETS[1].e[i];   /* BELL */
        w1.t.engine = ENGI_DIGITAL;
        w1.t.preset = 1;
        w1.sum = proj_hash(&w1, sizeof w1 - 4u);
        memcpy(&buf, &w1, sizeof w1);
        ok = proj_import(&c, &buf, (int)sizeof w1) && c.t[0].engine == ENGI_FM6 && c.t[0].preset == 1u &&
             c.t[1].engine == ENGI_FM6 && c.t[1].preset == PROJ_DEF_SOUND;   /* (track 2: the power-on FM6 PAD) */
        bad += check("FUN1 with DIGITAL: track 1 FM6 (BELL), track 2 the power-on sound (FM6 now)", ok);
#undef FM4_OK
    }
#endif
    {   /* SAMPLE tracks of SET 4 (PERC, the GM kit, retired after 1.0.2) in FUN8 / FUN4 projects load as DRUM with its
         * default kit: the E values the kit's, the rest of the sound, the steps and the preset byte's meaning kept; their
         * motion on the EDIT values goes, the rest stays; another SAMPLE set stays SAMPLE. Idempotent */
        project_t a, c, d;
        static project_store_t st;
        static project_v4_t w4;
        static const int16_t KIT[8] = DRUM_KIT_E;
        uint32_t i;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        for (t = 0; t < NTRK; t++) {
            for (i = 0; i < P_COUNT; i++)
                a.t[t].p[i] = param_desc_of(ENGI_SAMPLE, i)->def;
            a.t[t].p[P_DIST] = 0;                     /* This sound is also encoded as FUN4, whose bypass was DRIVE 0. */
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
                     str_eq(ENGINES[ENGI_DRUM]->presets[0].name, "DRUM-X") &&
                     !memcmp(ENGINES[ENGI_DRUM]->presets[0].e, (int8_t[8])DRUM_KIT_E, 8));
        bad += check("  its motion on CUT goes; a send's and the other track's stay",
                     c.motion.count == 2u && c.motion.event[0].param == P_REV && c.motion.event[1].place == 3u &&
                     motion_valid(&c.motion) && proj_ok(&c));
        bad += check("  imported again: as it is (DRUM now)", proj_import(&d, &c, sizeof c) && !memcmp(&d, &c, sizeof d));
        memset(&a.motion, 0, sizeof a.motion);
        a.sum = proj_sum(&a);
        to_v4(&w4, &a);
        memcpy(&buf, &w4, sizeof w4);
        ok = proj_import(&c, &buf, (int)sizeof w4) && PERC_OK(c);
        bad += check("FUN4 with a SAMPLE PERC track: DRUM's kit, the same", ok);
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
        a.dx_mot[XM_RATE] = 3; a.dx_mot[XM_DPTH] = 40; a.dx_mot[XM_SHPE] = 2; a.dx_mot[XM_RAND] = 17;
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
        ok = proj_import(&c, &funa, sizeof funa) && !memcmp(c.dx, DX_KIT_DEF, sizeof c.dx) &&
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
                     ok && !memcmp(c.dx, DX_KIT_DEF, sizeof c.dx) && !c.dx_mute &&
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
    bad += generation_wire();
    bad += generation_legacy_defaults();
    bad += step_wire_freeze();
    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}
