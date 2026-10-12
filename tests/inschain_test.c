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
    }

    /* Exclusive DLY CHAIN: owner mixes in track_insert; others' sends ignored; SEND restores bus. */
    {
        uint64_t send_h, chain_h, used_h;
        fresh();
        t = &trk[0];
        song.g[G_DLYOWN] = 0;
        song.g[G_DTIME] = 1;
        song.g[G_DFDBK] = 80;
        song.g[G_DMIX] = 127;
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
        song.g[G_DMIX] = 127;
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
        song.g[G_DMIX] = 127;
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
        song.g[G_DMIX] = 127;
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
               G_COUNT == 30u && GP[G_CRTYPE].names == GP[G_RTYPE].names && GP[G_CRSIZE].max == 127);
        }
        bus_own_set(1, -1);
        bus_own_set(0, -1);
    }

    /* FX pages: EFFECTS, REVERB, DLY, DIST, …; SLICER; Punch-in. MASTER on GLO.
     * Stage pages have no mix knob: the percent is EFFECTS K4. No bus CHORUS page. */
    {
        uint32_t i, pr = NPAGES, prev = NPAGES, ps = NPAGES, pd = NPAGES, pdl = NPAGES, pm = NPAGES;
        uint32_t mix = 0, chor = 0;
        static const uint8_t MIXK[] = {P_IDMIX, P_IFMIX, P_ICMIX, P_IPMIX, P_ILMIX, P_IHMIX};
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
        ck("FX pages: EFFECTS, REVERB, DELAY, DISTORT…; SLICER own section; MASTER on GLO",
           pr < prev && prev < pdl && pdl < pd && pd < ps && PAGES[pr].graph == GR_IORD &&
           PAGES[pr].sec == FX_SEC_INS && PAGES[pdl].sec == FX_SEC_INS &&
           PAGES[ps].sec == FX_SEC_SLCR && PAGES[pm].fam == FAM_GLO && PAGES[pm].sec == GLO_SEC_MASTER &&
           PAGES[pd].id[2] == P_ED_FX && PAGES[pd].id[3] == 0xFFu && !mix &&
           PAGES[pdl].id[3] == 0xFFu && PAGES[pdl].id[0] == G_DTIME && !chor);
        {
            uint8_t list[FX_LIST_N];
            fx_list_get(&trk[0], list);
            ck("EFFECTS list is 8: inserts + DLY + REV",
               FX_LIST_N == 8u && list[6] == INS_DLY && list[7] == INS_REV);
        }
    }

    ck("ANALOG FILTER TYPE includes COMB", NELEM(N_FTYPE) == 4u && str_eq(N_FTYPE[3], "COMB"));

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
        song.g[G_DFDBK] = 90; song.g[G_DMIX] = 127;
        t->p[P_DLY] = 40; h0 = note_hash(delay_samples() / CTL + 16u, 64);
        fresh(); t = &trk[0]; clear_stages(t);
        song.g[G_DLYOWN] = 0; song.g[G_DTIME] = (int16_t)(DTIME_FREE + 50);
        song.g[G_DFDBK] = 90; song.g[G_DMIX] = 127;
        t->p[P_DLY] = 120; h1 = note_hash(delay_samples() / CTL + 16u, 64);
        ck("DLY send amount moves the render (after the first echo)", h0 != h1);

        fresh(); t = &trk[0]; clear_stages(t);
        song.g[G_REVOWN] = 0; song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 90; song.g[G_RDAMP] = 60;
        t->p[P_REV] = 40; h0 = note_hash(24, 64);
        fresh(); t = &trk[0]; clear_stages(t);
        song.g[G_REVOWN] = 0; song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 90; song.g[G_RDAMP] = 60;
        t->p[P_REV] = 120; h1 = note_hash(24, 64);
        ck("REV send amount moves the render", h0 != h1);
    }

    printf("inschain: %d failure(s)\n", bad);
    return bad ? 1 : 0;
}
