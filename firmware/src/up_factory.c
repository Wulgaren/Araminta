/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Built-in factory preset bank at flash 0xF4000 (OBJ_FACBANK): 16 up_rec_t slots, single-copy, read-only
 * from the UI. The picks are indices into ENGINES[] / presets[] (built into the image); the bank is
 * materialised into flash on first boot (or when the sector is invalid) so Return-to-V15 / stock never
 * needs the selection table. Users cannot overwrite these slots (up_fac_put refuses). */
#define FAC_SLOTS UP_PER_BANK
/* FAC_E (NENGINES + 1) is defined in ui.c for the PRESETS list sentinel. */

/* Proposed 16 (Natan to review): a spread of bass / lead / pad / keys / perc character across engines. */
static const uint8_t FAC_SRC[FAC_SLOTS][2] = {
    {0, 0},                                    /* ANALOG SAW LEAD */
    {0, 7},                                    /* ANALOG SUB BASS */
    {0, 12},                                   /* ANALOG SUPER SAW */
    {12, 0},                                   /* FM6 TINE EP */
    {12, 2},                                   /* FM6 FM BASS */
    {12, 1},                                   /* FM6 BELL */
    {5, 0},                                    /* VOICE CHOIR AAH */
    {9, 1},                                    /* PHYS MARIMBA */
    {9, 2},                                    /* PHYS PLUCK */
    {7, 0},                                    /* WHEEL FULL ORGAN */
    {14, 3},                                   /* WT WT PAD */
    {8, 0},                                    /* GRAIN CLOUD PAD */
    {3, 2},                                    /* LOFI ARP 8BIT */
    {2, 0},                                    /* PHASE BRASS */
    {6, 0},                                    /* TRIO FAT BASS */
    {11, 0},                                   /* NOISE WIND */
};

static up_bank_t up_fac __attribute__((section(".pool")));   /* (main loop only) */

static up_rec_t *up_fac_rec(uint32_t k) { return &up_fac.r[k % FAC_SLOTS]; }

static void fac_fill_rec(up_rec_t *r, uint32_t eng, uint32_t pi)
{
    const engine_t *en;
    const preset_t *pr;
    uint32_t i, n;
    char nm[12];
    memset(r, 0, sizeof *r);
    if (eng >= NENGINES || !eng_ok(eng))
        eng = 0;
    en = ENGINES[eng];
    n = en->npresets ? en->npresets : 1u;
    pr = &en->presets[pi % n];
    r->used = UP_USED;
    r->ver = UP_VER;
    r->engine = (uint8_t)eng;
    r->np = (uint8_t)P_COUNT;
    memset(nm, 0, sizeof nm);
    for (i = 0; i < sizeof nm && pr->name[i]; i++)
        nm[i] = pr->name[i];
    up_set_name(r, 0, nm);
    for (i = 0; i < P_COUNT; i++) {
        const param_desc_t *d = param_desc_of(eng, i);
        int16_t v = d ? d->def : 0;
        if (i >= P_E0 && i < P_E0 + 8u)
            v = pr->e[i - P_E0];
        else if (i == P_ATK) v = pr->env[0];
        else if (i == P_DEC) v = pr->env[1];
        else if (i == P_SUS) v = pr->env[2];
        else if (i == P_REL) v = pr->env[3];
        else if (i == P_ED_FLT) v = pr->fenv;
        else if (i == P_VOICE) v = pr->mono ? V_LEGATO : V_POLY;
        else if (i == P_DIST && pr->fx[0]) v = (int16_t)(pr->fx[0] - 1);
        else if (i == P_CHOR && pr->fx[1]) v = (int16_t)(pr->fx[1] - 1);
        else if (i == P_DLY && pr->fx[2]) v = (int16_t)(pr->fx[2] - 1);
        else if (i == P_REV && pr->fx[3]) v = (int16_t)(pr->fx[3] - 1);
        up_set_value(r, i, v);
    }
    up_set_cat(r, cat_of_engine(eng));
    up_mark_osc2(r);
    up_mark_vib(r);
}

static void fac_build_bank(void)
{
    uint32_t i;
    up_fac.magic = UP_BANK_MAGIC;
    up_fac.rsize = sizeof(up_rec_t);
    up_fac.nslot = FAC_SLOTS;
    for (i = 0; i < FAC_SLOTS; i++)
        fac_fill_rec(up_fac_rec(i), FAC_SRC[i][0], FAC_SRC[i][1]);
}

static int up_fac_used(uint32_t k) { return k < FAC_SLOTS && up_valid(up_fac_rec(k)); }

static uint32_t up_fac_count(void)
{
    uint32_t k, n = 0;
    for (k = 0; k < FAC_SLOTS; k++)
        if (up_fac_used(k))
            n++;
    return n;
}

static uint32_t up_fac_nth(uint32_t n)
{
    uint32_t k;
    for (k = 0; k < FAC_SLOTS; k++)
        if (up_fac_used(k) && !n--)
            return k;
    return FAC_SLOTS;
}

static uint32_t up_fac_rank(uint32_t k)
{
    uint32_t i, n = 0;
    for (i = 0; i < k && i < FAC_SLOTS; i++)
        if (up_fac_used(i))
            n++;
    return n;
}

static uint32_t up_fac_cat(uint32_t k) { return up_cat_of(up_fac_rec(k % FAC_SLOTS)); }

static const char *up_fac_name(uint32_t k)
{
    static char nm[13];
    const up_rec_t *r;
    uint32_t i;
    if (!up_fac_used(k))
        return "-";
    r = up_fac_rec(k);
    for (i = 0; i < 12u && r->name[i]; i++)
        nm[i] = r->name[i];
    nm[i] = 0;
    return nm;
}

/* 0 ok, 1 empty / bad; never writes flash (read-only bank) */
static int up_fac_load(uint32_t k)
{
    const up_rec_t *r;
    int16_t v[P_COUNT];
    uint32_t i;
    track_t *t = TSEL;
    if (!up_fac_used(k))
        return 1;
    r = up_fac_rec(k);
    up_values(r, v);
    if (v[P_QUANT] == QN_CURR)
        v[P_QUANT] = QN_ALL;
    if (r->engine == ENGI_DIGITAL || eng_retired(r->engine))
        return 1;
    load_begin(t, UNDO_SOUND);
    panic_req |= (uint8_t)(1u << song.sel);
    fm1_irq_off();
    t->eng_req = r->engine;
    for (i = 0; i < P_COUNT; i++)
        if (!param_kept(i))
            t->p[i] = v[i];
    t->preset = 0;
    t->user = 0;                                       /* not a writable user slot */
    fm1_irq_on();
    if (r->engine == ENGI_FM6)
        fm6_adopt((uint32_t)(t - trk));                /* OWN: no separate FM6 pack in the factory bank yet */
    load_end(t);
    sync_reload = 1;
    ui.force = 1;
    return 0;
}

static void up_fac_boot(void)
{
#if FELUCCA_FLASH
    int n = flash_ok ? st_load(OBJ_FACBANK, &up_fac, sizeof up_fac) : -1;
    if (n != (int)sizeof up_fac || up_fac.magic != UP_BANK_MAGIC || up_fac.rsize != sizeof(up_rec_t) ||
        up_fac.nslot != FAC_SLOTS) {
        fac_build_bank();
        if (flash_ok)
            (void)st_save(OBJ_FACBANK, &up_fac, sizeof up_fac);
    } else {
        uint32_t i;
        for (i = 0; i < FAC_SLOTS; i++)
            up_migrate(up_fac_rec(i));
    }
#else
    fac_build_bank();
#endif
}
