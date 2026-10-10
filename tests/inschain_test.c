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

static void fresh(void)
{
    uint32_t k;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(cho_buf, 0, sizeof cho_buf);
    memset(rev_comb, 0, sizeof rev_comb);
    memset(rev_ap, 0, sizeof rev_ap);
    memset(&fx, 0, sizeof fx);
    memset(&pf, 0, sizeof pf);
    memset(ins_st, 0, sizeof ins_st);
    memset(ins_lines, 0, sizeof ins_lines);
    for (k = 0; k < NTRK; k++)
        pf.mg[k] = 32768;
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    vage = 0;
    rng_state = 0x1234567u;
    song.playing = 0;
    memset(trk, 0, sizeof trk);
    memset(&mod, 0, sizeof mod);
    memset(sl, 0, sizeof sl);
    memset(&motion, 0, sizeof motion);
    host_tracks_init();
    host_preset(&trk[0], 0, 0);                     /* ANALOG first preset */
    song.sel = 0;
}

int main(void)
{
    track_t *t;
    uint8_t ord[INS_N];
    uint64_t dry, wet, bypass, reordered;
    uint32_t i;

    printf("inschain_test: P_COUNT %u P_E0 %u INS_N %u\n", (unsigned)P_COUNT, (unsigned)P_E0, (unsigned)INS_N);
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
    t->p[P_IORD] = 0;                               /* DIST, FILTER, CRUSH, ... */
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
        ck("migrate: FTYPE/ESYNC and engine values moved",
           got[P_FTYPE] == 2 && got[P_ESYNC] == 1 && got[P_E0] == 10 && got[P_E7] == 17);
    }

    /* FX pages: REORDER first, SLICER after stages, FX sends after */
    {
        uint32_t i, pr = NPAGES, ps = NPAGES, pd = NPAGES, pfx = NPAGES;
        for (i = 0; i < NPAGES; i++) {
            if (PAGES[i].fam != FAM_FX) continue;
            if (str_eq(PAGES[i].title, "REORDER") && pr == NPAGES) pr = i;
            if (str_eq(PAGES[i].title, "SLICER") && ps == NPAGES) ps = i;
            if (str_eq(PAGES[i].title, "DIST") && pd == NPAGES) pd = i;
            if (str_eq(PAGES[i].title, "FX") && pfx == NPAGES) pfx = i;
        }
        ck("FX pages: REORDER then DIST.. then SLICER then FX sends",
           pr < pd && pd < ps && ps < pfx && PAGES[pr].graph == GR_IORD);
        ck("FX sends page has CHO DLY REV only",
           PAGES[pfx].id[0] == P_CHOR && PAGES[pfx].id[1] == P_DLY && PAGES[pfx].id[2] == P_REV);
    }

    ck("ANALOG FILTER TYPE includes COMB", NELEM(N_FTYPE) == 4u && str_eq(N_FTYPE[3], "COMB"));

    printf("inschain: %d failure(s)\n", bad);
    return bad ? 1 : 0;
}
