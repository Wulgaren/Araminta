/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLICE (13): retired like DIGITAL (fm4_convert.c ENG_FM4_GONE). Index stays reserved
 * (stores are append-only). Stub is never selectable (engines.c eng_ok).
 * Load policy: SLICE -> DRUM (10), default kit (project.c / upreset / ui).
 * SAMPLE / GRAIN / NOISE are live again; SAMPLE PERC (SET 4) still maps to DRUM via
 * core.h drum_from_perc (unchanged since 1.0.2). */

#define RETIRE_EDIT(l0, l1, l2, l3, l4, l5, l6, l7) { \
        {l0, F_INT, -64, 127, 0, 0, 0}, {l1, F_INT, -64, 127, 0, 0, 0}, {l2, F_INT, -64, 127, 0, 0, 0}, \
        {l3, F_INT, -64, 127, 0, 0, 0}, {l4, F_INT, -64, 127, 0, 0, 0}, {l5, F_INT, -64, 127, 0, 0, 0}, \
        {l6, F_INT, -64, 127, 0, 0, 0}, {l7, F_INT, -64, 127, 0, 0, 0}}

static void retire_gone_note_on(struct track *t, voice_t *v) { (void)t; (void)v; }
static void retire_gone_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)out; (void)n; (void)m;
}
static const engine_t ENG_SLICE_GONE = {
    .name = "-", .page_title = {"-", "-"},
    .edit = RETIRE_EDIT("-", "-", "-", "-", "-", "-", "-", "-"),
    .note_on = retire_gone_note_on, .render = retire_gone_render,
    .knob = {P_LEVEL, P_ATK, P_DEC, P_REL},
};

/* 1 = e is SLICE (DIGITAL stays FELUCCA_FM4-gated in eng_ok) */
static int eng_retired(uint32_t e)
{
    return e == ENGI_SLICE;
}

/* the engine a retired (or DIGITAL-without-FM4) index plays as when listed / ranked */
static uint32_t eng_play_as(uint32_t e)
{
    if (e == ENGI_DIGITAL && !FELUCCA_FM4)
        return ENGI_FM6;
    if (e == ENGI_SLICE)
        return ENGI_DRUM;
    return e < NENGINES ? e : 0u;
}
