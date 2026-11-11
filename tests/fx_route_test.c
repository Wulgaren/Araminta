/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host regression for every EFFECTS stage in every route it supports (CHAIN, SEND, MASTER):
 *   KNOBS   each of the stage's knobs moves the render
 *   FLAT    MIX 0 (TAPE: MIX 0) is bit-identical to the same mix with no EFFECTS at all
 *   HOT     a hot eight-voice chord through the stage at its extremes stays inside the mix's int32 headroom
 *   WIRE    the stage's settings, and its route, survive a project save and a load
 * The SEND knobs of an insert (and of the SLICER) are the shared bus ones (fx_bus_trk), as the EFFECTS page
 * edits them; the stage's MIX stays on the track. Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int bad;
static void ck(const char *what, int ok)
{
    printf("fxroute: %-74s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ---------------------------------------------------------------- harness --- */
#define FXR_BLOCKS 160u
static int32_t out_buf[2 * CTL];

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

/* Power-on (ui_power_on) plus a clean FX side: the pools, buses and tape the previous case left. */
static void fxr_fresh(void)
{
    uint32_t k;
    ui_power_on();
    memset(dly_buf, 0, sizeof dly_buf);
    memset(rev_tank, 0, sizeof rev_tank);
    R = &rev_tank[0];
    memset(&fx, 0, sizeof fx);
    dly_q8 = 0;
    dly_free = 0;
    tape_clear();
    memset(ins_st, 0, sizeof ins_st);
    memset(ins_ctx, 0, sizeof ins_ctx);
    memset(ins_lines, 0, sizeof ins_lines);
    memset(ins_master_st, 0, sizeof ins_master_st);
    memset(ins_master_lines, 0, sizeof ins_master_lines);
    memset(ins_bus_ran, 0, sizeof ins_bus_ran);
    memset(bus_knobs_on, 0, sizeof bus_knobs_on);
    memset(fx_order_tbl, 0, sizeof fx_order_tbl);
    memset(fx_master_tr, 0, sizeof fx_master_tr);
    fx_master_order_tr = 0;
    fx_bus_init();
    for (k = 0; k < NTRK; k++)
        pf.mg[k] = 32768;
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = 0;
    memset(lce, 0, sizeof lce);
    vage = 0;
    rng_state = 0x1234567u;
    song.playing = 0;
    song.sel = 0;
    song.master_q12 = 4096;                         /* full master level (ui_power_on leaves it 0) */
    for (k = 0; k < NTRK; k++)
        host_preset(&trk[k], 0, 0);                 /* ANALOG SAW LEAD: each stage has material */
    slicer_start();
}

/* no EFFECTS at all: every amount 0, every route back to its neutral state, TAPE off */
static const uint8_t MIXID[INS_N] = {P_IDMIX, P_IFMIX, P_ICMIX, P_IPMIX, P_ILMIX, P_IHMIX};
static void fx_off(void)
{
    uint32_t t, s;
    for (t = 0; t < NTRK; t++) {
        for (s = 0; s < INS_N; s++) {
            ins_send_set(&trk[t], s, 0);
            trk[t].p[MIXID[s]] = 0;
        }
        trk[t].p[P_DLY] = 0;
        trk[t].p[P_REV] = 0;
        trk[t].p[P_SLDEPTH] = 0;
    }
    for (s = 0; s < FX_LIST_N; s++)
        fx_master_clear(s);
    song.g[G_DLYOWN] = 0;
    song.g[G_REVOWN] = 0;
    song.g[G_SLOWN] = (int16_t)((1u << NTRK) - 1u);   /* all SEND */
    song.g[G_TAPEMODE] = TAPE_SEND;
    song.g[G_TMIX] = 0;
    song.g[G_TSPEED] = 100;
    song.g[G_TWOW] = 0;
    song.g[G_TDROP] = 0;
    song.g[G_TTEMPO] = 1;
}

/* a held, steady, bright voice */
static void set_voice(void)
{
    trk[0].p[P_ATK] = 0;
    trk[0].p[P_DEC] = 127;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_REL] = 24;
    trk[0].p[P_LEVEL] = 112;
}

/* --------------------------------------------------------------- the matrix --- */
enum { RT_CHAIN, RT_SEND, RT_MASTER, RT_N };
static const char *const RT_NAME[RT_N] = {"CHAIN", "SEND", "MASTER"};

/* where a knob lives: the track, a global (song.g) or the reverb (CHAIN uses its own tank globals) */
enum { K_TRACK, K_GLOBAL, K_REV };
typedef struct {
    uint16_t id;
    int16_t base, alt;
    uint8_t kind;
} knob_t;
typedef struct {
    uint8_t st;
    knob_t knob[4];
    uint8_t nknob;
} stage_t;
/* base: the stage at a setting where every one of its knobs matters; alt: the probe value */
static const stage_t STAGES[] = {
    {INS_DIST,   {{P_IDMODE, IDM_HARD, IDM_FOLD, K_TRACK},
                  {P_DIST, 60, 110, K_TRACK}, {P_ED_FX, 0, 40, K_TRACK}}, 3},
    {INS_FILTER, {{P_IFTYPE, IFT_LP, IFT_HP, K_TRACK},
                  {P_IFCUT, 96, 40, K_TRACK}, {P_IFRES, 40, 120, K_TRACK}}, 3},
    {INS_CRUSH,  {{P_ICBITS, 48, 100, K_TRACK}, {P_ICRATE, 96, 16, K_TRACK},
                  {P_ICLPF, 96, 20, K_TRACK}}, 3},
    {INS_PHASR,  {{P_IPRATE, 40, 100, K_TRACK}, {P_IPDEP, 80, 127, K_TRACK},
                  {P_IPFBK, 96, 10, K_TRACK}}, 3},
    {INS_FLANG,  {{P_ILRATE, 40, 100, K_TRACK}, {P_ILDEP, 80, 127, K_TRACK}}, 2},
    {INS_CHOR,   {{P_IHRATE, 40, 100, K_TRACK}, {P_IHDEP, 80, 127, K_TRACK}}, 2},
    {INS_DLY,    {{G_DTIME, DTIME_FREE + 30, DTIME_FREE + 80, K_GLOBAL},
                  {G_DFDBK, 90, 10, K_GLOBAL}, {G_DCOLOR, 70, 120, K_GLOBAL}}, 3},
    {INS_REV,    {{G_RTYPE, 2, 0, K_REV}, {G_RSIZE, 90, 20, K_REV}, {G_RDAMP, 60, 10, K_REV}}, 3},
    {INS_SLCR,   {{P_SLCR, SL_GATE, SL_STUT, K_TRACK}, {P_SLPAT, 1, 4, K_TRACK},
                  {P_SLRATE, 5, 4, K_TRACK}}, 3},
    {INS_TAPE,   {{G_TSPEED, 50, 75, K_GLOBAL}, {G_TWOW, 0, 100, K_GLOBAL}}, 2},
};

/* configure the stage's route: CHAIN, SEND or MASTER on track 0 */
static void route_set(uint32_t st, uint32_t rt)
{
    const uint32_t tr = 0;
    if (st == INS_TAPE) {
        song.g[G_TAPEMODE] = (int16_t)(rt == RT_CHAIN ? TAPE_CH1 : rt == RT_SEND ? TAPE_SEND : TAPE_MASTER);
        if (rt == RT_MASTER)
            fx_master_set(INS_TAPE, tr);
        else
            fx_master_clear(INS_TAPE);
        return;
    }
    if (st == INS_SLCR) {
        if (rt == RT_MASTER) {
            fx_master_set(INS_SLCR, tr);
            song.g[G_SLOWN] = (int16_t)((1u << NTRK) - 1u);
        } else {
            fx_master_clear(INS_SLCR);
            bus_own_set(2, rt == RT_CHAIN ? (int32_t)tr : -1);
        }
        return;
    }
    if (st == INS_DLY || st == INS_REV) {
        uint32_t which = st == INS_REV ? 1u : 0u;
        fx_master_clear(st);
        bus_own_set(which, rt == RT_CHAIN ? (int32_t)tr : -1);
        if (rt == RT_MASTER)
            fx_master_set(st, tr);
        return;
    }
    ins_send_set(&trk[tr], st, rt == RT_SEND);
    if (rt == RT_MASTER)
        fx_master_set(st, tr);
    else
        fx_master_clear(st);
}

/* the knob's storage for this stage and route: SEND inserts/SLICER edit the shared bus, as the page does */
static int16_t *knob_slot(uint32_t st, uint32_t rt, const knob_t *k)
{
    uint16_t id = k->id;
    if (k->kind == K_GLOBAL)
        return &song.g[id];
    if (k->kind == K_REV) {
        if (rt == RT_CHAIN)
            id = (uint16_t)(id == G_RTYPE ? G_CRTYPE : id == G_RSIZE ? G_CRSIZE : G_CRDAMP);
        return &song.g[id];
    }
    if ((st < INS_N || st == INS_SLCR) && rt == RT_SEND)
        return &fx_bus_trk.p[id];
    return &trk[0].p[id];
}

static const char *knob_label(const knob_t *k)
{
    return k->kind == K_TRACK ? TP[k->id].label : GP[k->id].label;
}

static void stage_mix(uint32_t st, int32_t v)
{
    if (st == INS_DLY)
        trk[0].p[P_DLY] = (int16_t)v;
    else if (st == INS_REV)
        trk[0].p[P_REV] = (int16_t)v;
    else if (st == INS_SLCR)
        trk[0].p[P_SLDEPTH] = (int16_t)v;
    else if (st == INS_TAPE)
        song.g[G_TMIX] = (int16_t)v;
    else
        trk[0].p[MIXID[st]] = (int16_t)v;
}

static int16_t stage_mix_get(uint32_t st)
{
    if (st == INS_DLY)
        return trk[0].p[P_DLY];
    if (st == INS_REV)
        return trk[0].p[P_REV];
    if (st == INS_SLCR)
        return trk[0].p[P_SLDEPTH];
    if (st == INS_TAPE)
        return song.g[G_TMIX];
    return trk[0].p[MIXID[st]];
}

/* arm the stage: route, base knobs (one replaced by its probe value when alt >= 0), and an amount */
static void stage_arm(const stage_t *s, uint32_t rt, int alt, int32_t mix)
{
    uint32_t j;
    fxr_fresh();
    set_voice();
    fx_off();
    route_set(s->st, rt);
    for (j = 0; j < s->nknob; j++)
        *knob_slot(s->st, rt, &s->knob[j]) = (int16_t)((int)j == alt ? s->knob[j].alt : s->knob[j].base);
    stage_mix(s->st, mix);
}

/* the stage's extremes, for the hot-input check */
static void stage_hot(const stage_t *s, uint32_t rt)
{
    switch (s->st) {
    case INS_DIST:
        *knob_slot(s->st, rt, &s->knob[0]) = IDM_FUZZ;
        *knob_slot(s->st, rt, &s->knob[1]) = 127;
        *knob_slot(s->st, rt, &s->knob[2]) = 63;
        break;
    case INS_FILTER:
        *knob_slot(s->st, rt, &s->knob[0]) = IFT_COMB;
        *knob_slot(s->st, rt, &s->knob[1]) = 0;
        *knob_slot(s->st, rt, &s->knob[2]) = 127;
        break;
    case INS_CRUSH:
        *knob_slot(s->st, rt, &s->knob[0]) = 0;
        *knob_slot(s->st, rt, &s->knob[1]) = 0;
        *knob_slot(s->st, rt, &s->knob[2]) = 0;
        break;
    case INS_PHASR:
        *knob_slot(s->st, rt, &s->knob[0]) = 1;
        *knob_slot(s->st, rt, &s->knob[1]) = 127;
        *knob_slot(s->st, rt, &s->knob[2]) = 127;
        break;
    case INS_FLANG:
    case INS_CHOR:
        *knob_slot(s->st, rt, &s->knob[0]) = 1;
        *knob_slot(s->st, rt, &s->knob[1]) = 127;
        break;
    case INS_DLY:
        *knob_slot(s->st, rt, &s->knob[0]) = DTIME_FREE + 20;
        *knob_slot(s->st, rt, &s->knob[1]) = 120;
        *knob_slot(s->st, rt, &s->knob[2]) = 127;
        break;
    case INS_REV:
        *knob_slot(s->st, rt, &s->knob[0]) = 2;
        *knob_slot(s->st, rt, &s->knob[1]) = 127;
        *knob_slot(s->st, rt, &s->knob[2]) = 0;
        break;
    case INS_SLCR:
        *knob_slot(s->st, rt, &s->knob[0]) = SL_STUT;
        *knob_slot(s->st, rt, &s->knob[1]) = 1;
        *knob_slot(s->st, rt, &s->knob[2]) = 5;
        break;
    case INS_TAPE:
        song.g[G_TSPEED] = 25;
        song.g[G_TWOW] = 127;
        song.g[G_TTEMPO] = 0;
        break;
    }
}

static int64_t iabs64(int64_t x) { return x < 0 ? -x : x; }

static uint64_t render_hash(void)
{
    trk_note_on(&trk[0], 60, 110);
    return hash_out(FXR_BLOCKS);
}

/* --------------------------------------------------------------------- tests --- */
static void test_knobs(void)
{
    uint32_t s, rt, j;
    for (s = 0; s < NELEM(STAGES); s++) {
        const stage_t *g = &STAGES[s];
        for (rt = 0; rt < RT_N; rt++) {
            uint64_t h0;
            stage_arm(g, rt, -1, 127);
            h0 = render_hash();
            for (j = 0; j < g->nknob; j++) {
                uint64_t h1;
                char w[96];
                stage_arm(g, rt, (int)j, 127);
                h1 = render_hash();
                snprintf(w, sizeof w, "%s %s: %s moves the render", N_ISTAGE[g->st], RT_NAME[rt],
                         knob_label(&g->knob[j]));
                ck(w, h0 != h1);
            }
        }
    }
}

static void test_flat(void)
{
    uint32_t s, rt;
    uint64_t dry;
    fxr_fresh();
    set_voice();
    fx_off();
    dry = render_hash();
    for (s = 0; s < NELEM(STAGES); s++) {
        const stage_t *g = &STAGES[s];
        for (rt = 0; rt < RT_N; rt++) {
            uint64_t h;
            char w[96];
            stage_arm(g, rt, -1, 0);
            h = render_hash();
            snprintf(w, sizeof w, "%s %s: MIX 0 is bit-identical to dry", N_ISTAGE[g->st], RT_NAME[rt]);
            ck(w, h == dry);
        }
    }
}

static void test_hot(void)
{
    static const int64_t PRE_MAX = 1 << 26;
    static const uint8_t HOT[8] = {36, 40, 43, 48, 52, 55, 60, 64};
    uint32_t s, rt, k, i, b;
    uint64_t dry_hot;
    int32_t dry_out_max = 0;
    int64_t dry_pre_max = 0;
    /* the reference: the same hot chord, no EFFECTS */
    fxr_fresh();
    trk[0].p[P_ATK] = 0;
    trk[0].p[P_DEC] = 127;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_REL] = 0;
    trk[0].p[P_LEVEL] = 127;
    for (k = 0; k < 8u; k++)
        trk_note_on(&trk[0], HOT[k], 127);
    for (b = 0; b < 64u; b++) {
        mix_block(out_buf, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = out_buf[i] < 0 ? -out_buf[i] : out_buf[i];
            if (a > dry_out_max) dry_out_max = a;
        }
        for (i = 0; i < CTL; i++) {
            int64_t a = iabs64(mix_l[i]) > iabs64(mix_r[i]) ? iabs64(mix_l[i]) : iabs64(mix_r[i]);
            if (a > dry_pre_max) dry_pre_max = a;
        }
    }
    dry_hot = hash_out(64);
    for (s = 0; s < NELEM(STAGES); s++) {
        const stage_t *g = &STAGES[s];
        for (rt = 0; rt < RT_N; rt++) {
            int ok = 1;
            int32_t out_max = 0;
            int64_t pre_max = 0;
            uint64_t h;
            fxr_fresh();
            trk[0].p[P_ATK] = 0;
            trk[0].p[P_DEC] = 127;
            trk[0].p[P_SUS] = 127;
            trk[0].p[P_REL] = 0;
            trk[0].p[P_LEVEL] = 127;
            fx_off();
            route_set(g->st, rt);
            stage_hot(g, rt);
            stage_mix(g->st, 127);
            song.g[G_CLIP] = 100;
            for (k = 0; k < 8u; k++)
                trk_note_on(&trk[0], HOT[k], 127);
            for (b = 0; b < 64u && ok; b++) {
                mix_block(out_buf, CTL);
                for (i = 0; i < 2u * CTL; i++) {
                    int32_t a = out_buf[i] < 0 ? -out_buf[i] : out_buf[i];
                    if (a > 32767)
                        ok = 0;
                    if (a > out_max)
                        out_max = a;
                }
                for (i = 0; i < CTL; i++) {
                    int64_t a = iabs64(mix_l[i]) > iabs64(mix_r[i]) ? iabs64(mix_l[i]) : iabs64(mix_r[i]);
                    if (a > pre_max)
                        pre_max = a;
                }
            }
            h = hash_out(64);
            {
                char w[120];
                snprintf(w, sizeof w, "%s %s: hot input stays bounded (peak %lld/pre %lld, dry %lld/%lld)",
                         N_ISTAGE[g->st], RT_NAME[rt], (long long)out_max, (long long)pre_max,
                         (long long)dry_out_max, (long long)dry_pre_max);
                ck(w, ok && out_max > 0 && pre_max <= PRE_MAX && h != dry_hot);
            }
            song.g[G_CLIP] = 0;
        }
    }
}

static void test_wire(void)
{
    uint32_t s, rt, j;
    for (s = 0; s < NELEM(STAGES); s++) {
        const stage_t *g = &STAGES[s];
        for (rt = 0; rt < RT_N; rt++) {
            project_t saved, loaded;
            project_store_t wire;
            int16_t mix_before, mix_after;
            int16_t knob_before[4], knob_after[4];
            int ok, same = 1;
            stage_arm(g, rt, 0, 127);              /* base knobs, the first one at its probe value */
            mix_before = stage_mix_get(g->st);
            for (j = 0; j < g->nknob; j++)
                knob_before[j] = *knob_slot(g->st, rt, &g->knob[j]);
            project_capture(&saved);
            ok = proj_pack(&wire, &saved) && proj_import(&loaded, &wire, sizeof wire) &&
                 !project_restore_runtime(&loaded);
            mix_after = stage_mix_get(g->st);
            for (j = 0; j < g->nknob; j++)
                knob_after[j] = *knob_slot(g->st, rt, &g->knob[j]);
            for (j = 0; j < g->nknob; j++)
                same &= knob_before[j] == knob_after[j];
            same &= mix_before == mix_after;
            same &= str_eq(fx_list_mode(&trk[0], g->st), RT_NAME[rt]);
            {
                char w[112];
                snprintf(w, sizeof w, "%s %s: knobs, MIX and route survive save / load", N_ISTAGE[g->st],
                         RT_NAME[rt]);
                ck(w, ok && same);
            }
        }
    }
}

static void test_tape_default(void)
{
    uint64_t dry, def, slow;
    int ok;
    /* the reference: the same power-on sound with only TAPE off (the preset's own sends stay) */
    fxr_fresh();
    set_voice();
    song.g[G_TAPEMODE] = TAPE_SEND;
    song.g[G_TMIX] = 0;
    dry = render_hash();
    /* power-on: TAPE MASTER at 100 %, and at SPEED 100 the head is idle */
    fxr_fresh();
    set_voice();
    ok = GP[G_TMIX].def == 127 && GP[G_TAPEMODE].def == TAPE_MASTER &&
         song.g[G_TMIX] == 127 && song.g[G_TAPEMODE] == TAPE_MASTER;
    def = render_hash();
    ck("power-on: TAPE MIX 100 % MASTER is bit-identical to TAPE off", ok && def == dry);
    fxr_fresh();
    set_voice();
    song.g[G_TSPEED] = 50;
    slow = render_hash();
    ck("power-on TAPE MASTER: SPEED moves the whole mix (no MIX raised first)", slow != dry);
    fx_master_sanitize();                            /* as fx_boot: one owner, the selected track */
    ck("power-on TAPE row: MASTER on the owner, USED on the others",
       str_eq(fx_list_mode(&trk[0], INS_TAPE), "MASTER") &&
       str_eq(fx_list_mode(&trk[1], INS_TAPE), "USED"));
}

int main(void)
{
    printf("fxroute: stages %u routes %u\n", (unsigned)NELEM(STAGES), (unsigned)RT_N);
    test_knobs();
    test_flat();
    test_hot();
    test_wire();
    test_tape_default();
    printf("fxroute: %d failure(s)\n", bad);
    return bad ? 1 : 0;
}
