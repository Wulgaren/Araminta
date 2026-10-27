/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Real UI/motion/project sources: the ids outside a track's p[] and their contract.
 *   PITCH      motion-only, lock-only, 127, never p[]
 *   MFX_*      FX targets aliasing matrix ids, motion locks through fx_target_ptr
 *   EV_*       AUTOMATION's own step rows, not motion ids
 *   PG_*       CHORD's sweep cards, not motion ids
 *   P_ALLOC    the packed sweep (rate / stroke), legacy OFF
 *   MOTION_ID  bit 7 is MOTION_LOCK, never part of the id */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int id_classification(void)
{
    int bad = 0, ok;
    uint32_t id;
    ui_power_on();
    ok = 1;
    for (id = 0; id < P_COUNT; id++)
        ok &= param_real(id) && param_id_known(id) && !param_lock_only(id);
    bad += check("every P_* id is real and known, none lock-only", ok);
    bad += check("PITCH (127) is lock-only, not real, and known", !param_real(MSEQ_PITCH) &&
        param_lock_only(MSEQ_PITCH) && param_id_known(MSEQ_PITCH));
    bad += check("an id past PITCH is unknown", !param_real(MSEQ_PITCH + 1u) &&
        !param_lock_only(MSEQ_PITCH + 1u) && !param_id_known(MSEQ_PITCH + 1u));
    return bad;
}

static int pitch_contract(void)
{
    int bad = 0, ok;
    track_t *t;
    motion_store_t m = {0};
    project_t before, after;
    project_store_t st;
    uint32_t i, nr, row = 0, found = 0, sel = 0, prow = 0;
    uint16_t rw[EV_ROWS];

    ui_power_on();
    t = &trk[0];
    bad += check("PITCH is a motion target", motion_param(MSEQ_PITCH));
    bad += check("PITCH refuses an automation event", motion_set_event(t, 2, MSEQ_PITCH, 7) == 1 && !motion.count);
    bad += check("PITCH accepts a lock, stored with MOTION_LOCK", !motion_set_lock(t, 2, MSEQ_PITCH, 7) &&
        motion.count == 1u && motion.event[0].param == (MSEQ_PITCH | MOTION_LOCK) && motion.event[0].value == 7);
    m.count = 1; m.on = 1;
    m.event[0] = (motion_event_t){0, MSEQ_PITCH, 0};
    ok = !motion_valid(&m);
    m.event[0].param = (uint8_t)(MSEQ_PITCH | MOTION_LOCK);
    m.event[0].value = 25;
    ok &= !motion_valid(&m);
    m.event[0].value = 7;
    ok &= motion_valid(&m);
    bad += check("a stored PITCH record must be a lock in -24..24", ok);
    project_capture(&before);
    ok = proj_pack(&st, &before) && proj_import(&after, &st, sizeof st) && after.motion.count == 1u &&
         after.motion.event[0].param == (MSEQ_PITCH | MOTION_LOCK) && after.motion.event[0].value == 7;
    bad += check("a PITCH lock round-trips through a project (127 + bit 7)", ok);

    i = pk_build_auto(t, MSEQ_PITCH, NSTEP, &sel);
    for (nr = 0, row = 0; nr < i; nr++)
        if (pk_rows[nr].id == MSEQ_PITCH)
            row = nr;
    bad += check("+ ADD lists PITCH and selects it for a PITCH id",
        i && row < i && pk_rows[row].id == MSEQ_PITCH && sel == row);

    motion_clear(t);
    motion_set_lock(t, 0, P_REV, 50);
    i = pk_build_auto(t, P_REV, 0, &sel);
    for (nr = 0, found = 0; nr < i; nr++)
        if (pk_rows[nr].id == MSEQ_PITCH)
            found = 1;
    bad += check("a record's list still offers PITCH and selects the record's id",
        found && sel < i && pk_rows[sel].id == P_REV);

    go_auto_top();
    frame();
    nr = ev_list(rw);
    for (i = 0, found = 0; i < nr; i++)
        if ((rw[i] >> 8) == EVK_REC && MOTION_ID(&motion.event[rw[i] & 0xFFu]) == P_REV) {
            row = i;
            found = 1;
        }
    if (found) {
        ui.ev_row = (uint16_t)row;
        pk_open(PK_AUTO);
        for (i = 0; i < pk_n; i++)
            if (pk_rows[i].id == MSEQ_PITCH)
                prow = i;
        pk_cur = (uint8_t)prow;
        pk_apply();
    }
    bad += check("K3 PARAM: selecting PITCH really changes the record",
        found && motion_find(t, 0, MSEQ_PITCH) >= 0 && motion_find(t, 0, P_REV) < 0 &&
        MOTION_ID(&motion.event[0]) == MSEQ_PITCH && (motion.event[0].param & MOTION_LOCK));

    i = pk_build_auto(t, MSEQ_PITCH, 0, &sel);
    bad += check("a PITCH record's list selects its own row",
        sel < i && pk_rows[sel].id == MSEQ_PITCH);
    return bad;
}

static int mfx_contract(void)
{
    int bad = 0, ok;
    track_t *t;
    project_t before, after;
    project_store_t st;
    int16_t value[MFX_DAMP - MFX_TIME + 1u];
    uint32_t id, i;

    ui_power_on();
    t = &trk[0];
    ok = 1;
    for (id = MFX_TIME; id <= MFX_DAMP; id++) {
        const param_desc_t *d = motion_target_desc(t, id);
        ok &= motion_param(id) && d && d->label && d->label[0] && d->max > d->min;
        value[id - MFX_TIME] = d ? d->min : 0;
        ok &= !motion_set_lock(t, id - MFX_TIME, id, value[id - MFX_TIME]);
    }
    bad += check("every MFX target is a motion parameter with a descriptor and range", ok);
    bad += check("all five MFX locks validate", motion_valid(&motion) && motion.count == 5u);
    project_capture(&before);
    ok = proj_pack(&st, &before) && proj_import(&after, &st, sizeof st) && after.motion.count == 5u;
    for (i = 0; ok && i < 5u; i++)
        ok &= after.motion.event[i].param == (uint8_t)((MFX_TIME + i) | MOTION_LOCK) &&
              after.motion.event[i].value == value[i];
    bad += check("MFX locks round-trip through a project", ok);
    return bad;
}

static int ev_contract(void)
{
    static const uint32_t EV[] = {EV_VEL, EV_NUDGE, EV_CHANCE, EV_RATCH};
    int bad = 0, ok;
    char nm[16];
    uint32_t i, j;
    track_t *t;

    ui_power_on();
    t = &trk[0];
    ok = 1;
    for (i = 0; i < NELEM(EV); i++) {
        ev_name(t, EV[i], nm);
        ok &= ev_id_ok(t, EV[i]) && nm[0] && ev_id_at(ev_id_pos(EV[i])) == EV[i] && !param_id_known(EV[i]);
        for (j = 0; j < i; j++)
            ok &= ev_id_pos(EV[i]) != ev_id_pos(EV[j]);
    }
    bad += check("AUTOMATION's four step rows are named UI ids, not motion ids", ok);
    ok = 1;
    for (i = 0; i < NELEM(EV); i++)
        ok &= ev_id_ok(t, ev_id_step(t, 0, EV[i], 1)) && ev_id_ok(t, ev_id_step(t, 0, EV[i], -1));
    bad += check("ev_id_step from every step row lands on a valid id", ok);
    bad += check("ev_id_step from VEL goes on to the first real parameter", ev_id_step(t, 0, EV_VEL, 1) == 0u);
    bad += check("PITCH's AUTOMATION position maps back to PITCH", ev_id_at(ev_id_pos(MSEQ_PITCH)) == MSEQ_PITCH);
    bad += check("the parameter walk runs last real id -> PITCH -> last real id",
        ev_id_step(t, NSTEP, P_COUNT - 1u, 1) == MSEQ_PITCH &&
        ev_id_step(t, NSTEP, MSEQ_PITCH, -1) == P_COUNT - 1u);
    return bad;
}

static int pg_contract(void)
{
    int bad = 0, ok;
    const page_t *pg = 0;
    const param_desc_t *d;
    int16_t *vp;
    uint32_t i;

    ui_power_on();
    for (i = 0; i < NPAGES; i++)
        if (str_eq(PAGES[i].title, "CHORD")) {
            pg = &PAGES[i];
            break;
        }
    bad += check("CHORD's K3/K4 cards are the sweep's STRUM / STROKE",
        pg && pg->id[2] == PG_STRUM && pg->id[3] == PG_STROKE);
    ok = 0;
    if (pg) {
        d = page_desc(pg, 2, &vp);
        ok = d && vp && str_eq(d->label, "STRUM") && d->max > d->min;
        d = page_desc(pg, 3, &vp);
        ok &= d && vp && str_eq(d->label, "STROKE") && d->max > d->min;
    }
    bad += check("page_desc returns the STRUM / STROKE descriptors", ok);
    bad += check("the CHORD cards are UI-only, not motion ids", !motion_param(PG_STRUM) &&
        !motion_param(PG_STROKE) && !param_id_known(PG_STRUM) && !param_id_known(PG_STROKE));
    return bad;
}

static int sweep_contract(void)
{
    int bad = 0, ok;
    track_t *t;
    uint32_t rate, stroke;

    ui_power_on();
    t = &trk[0];
    ok = 1;
    for (rate = 1; rate <= 7u; rate++)
        for (stroke = 0; stroke <= 2u; stroke++) {
            t->p[P_ALLOC] = sweep_pack(rate, stroke);
            ok &= sweep_rate(t) == rate && sweep_stroke(t) == stroke;
        }
    bad += check("P_ALLOC round-trips every rate 1..7 and stroke 0..2", ok);
    ok = 1;
    for (stroke = 0; stroke <= 2u; stroke++)
        ok &= sweep_pack(0, stroke) == 0;
    t->p[P_ALLOC] = 0;
    ok &= !sweep_rate(t) && !sweep_stroke(t);
    t->p[P_ALLOC] = 1;
    ok &= !sweep_rate(t) && !sweep_stroke(t);
    bad += check("sweep OFF and the legacy ROT / REUSE aliases read as OFF", ok);
    return bad;
}

static int motion_id_contract(void)
{
    int bad = 0, ok;
    motion_event_t e;
    uint32_t i;

    ui_power_on();
    e = (motion_event_t){0, (uint8_t)(MSEQ_PITCH | MOTION_LOCK), 3};
    bad += check("MOTION_ID strips bit 7 from a PITCH record", MOTION_ID(&e) == MSEQ_PITCH &&
        (e.param & MOTION_LOCK));
    e = (motion_event_t){1, (uint8_t)(P_REV | MOTION_LOCK), 40};
    bad += check("MOTION_ID strips bit 7 from an ordinary lock", MOTION_ID(&e) == P_REV &&
        (e.param & MOTION_LOCK));
    motion_set_lock(&trk[0], 0, MSEQ_PITCH, 1);
    motion_set_lock(&trk[0], 1, P_REV, 2);
    ok = motion.count == 2u;
    for (i = 0; i < motion.count; i++)
        ok &= MOTION_ID(&motion.event[i]) != MSEQ_PITCH || (motion.event[i].param & MOTION_LOCK);
    bad += check("every stored PITCH record carries MOTION_LOCK", ok);
    return bad;
}

int main(void)
{
    int bad;
    ui_power_on();
    bad = id_classification() + pitch_contract() + mfx_contract() + ev_contract() + pg_contract() +
          sweep_contract() + motion_id_contract();
    printf("%s\n", bad ? "PARAM CONTRACT TEST FAILED" : "PITCH / special-id parameter contract tests passed");
    return bad;
}
