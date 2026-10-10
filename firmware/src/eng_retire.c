/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SAMPLE (4), GRAIN (8), NOISE (11), SLICE (13): retired like DIGITAL (fm4_convert.c ENG_FM4_GONE).
 * Indices stay reserved (stores are append-only). Stubs are never selectable (engines.c eng_ok).
 * Load policy (project.c proj_retire, upreset / ui load paths):
 *   SAMPLE PERC (SET 4)         -> DRUM kit (unchanged: core.h drum_from_perc)
 *   SAMPLE / GRAIN / NOISE else -> ANALOG (0), E0..E7 ANALOG defaults; shared params kept
 *   SLICE                       -> DRUM (10), default kit
 * User-preset records keep the old engine byte (UP_PUT still takes it); every load converts,
 * as DIGITAL keeps engine 1. */

/* wide ranges so a packed store keeps its E0..E7 until proj_retire / up_load remaps (like DIGITAL's GONE keeps
 * its real ranges for fm4_convert). Labels stay "-". */
#define RETIRE_EDIT(l0, l1, l2, l3, l4, l5, l6, l7) { \
        {l0, F_INT, -64, 127, 0, 0, 0}, {l1, F_INT, -64, 127, 0, 0, 0}, {l2, F_INT, -64, 127, 0, 0, 0}, \
        {l3, F_INT, -64, 127, 0, 0, 0}, {l4, F_INT, -64, 127, 0, 0, 0}, {l5, F_INT, -64, 127, 0, 0, 0}, \
        {l6, F_INT, -64, 127, 0, 0, 0}, {l7, F_INT, -64, 127, 0, 0, 0}}

static void retire_gone_note_on(struct track *t, voice_t *v) { (void)t; (void)v; }
static void retire_gone_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)out; (void)n; (void)m;
}
static const engine_t ENG_SAMPLE_GONE = {
    .name = "-", .page_title = {"-", "-"},
    .edit = RETIRE_EDIT("-", "-", "-", "-", "-", "-", "-", "-"),
    .note_on = retire_gone_note_on, .render = retire_gone_render,
    .knob = {P_LEVEL, P_ATK, P_DEC, P_REL},
};
static const engine_t ENG_GRAIN_GONE = {
    .name = "-", .page_title = {"-", "-"},
    .edit = RETIRE_EDIT("-", "-", "-", "-", "-", "-", "-", "-"),
    .note_on = retire_gone_note_on, .render = retire_gone_render,
    .knob = {P_LEVEL, P_ATK, P_DEC, P_REL},
};
static const engine_t ENG_NOISE_GONE = {
    .name = "-", .page_title = {"-", "-"},
    .edit = RETIRE_EDIT("-", "-", "-", "-", "-", "-", "-", "-"),
    .note_on = retire_gone_note_on, .render = retire_gone_render,
    .knob = {P_LEVEL, P_ATK, P_DEC, P_REL},
};
static const engine_t ENG_SLICE_GONE = {
    .name = "-", .page_title = {"-", "-"},
    .edit = RETIRE_EDIT("-", "-", "-", "-", "-", "-", "-", "-"),
    .note_on = retire_gone_note_on, .render = retire_gone_render,
    .knob = {P_LEVEL, P_ATK, P_DEC, P_REL},
};

/* 1 = e is SAMPLE / GRAIN / NOISE / SLICE (DIGITAL stays FELUCCA_FM4-gated in eng_ok) */
static int eng_retired(uint32_t e)
{
    return e == ENGI_SAMPLE || e == ENGI_GRAIN || e == ENGI_NOISE || e == ENGI_SLICE;
}

/* the engine a retired (or DIGITAL-without-FM4) index plays as when listed / ranked */
static uint32_t eng_play_as(uint32_t e)
{
    if (e == ENGI_DIGITAL && !FELUCCA_FM4)
        return ENGI_FM6;
    if (e == ENGI_SLICE)
        return ENGI_DRUM;
    if (e == ENGI_SAMPLE || e == ENGI_GRAIN || e == ENGI_NOISE)
        return 0u;                                   /* ANALOG */
    return e < NENGINES ? e : 0u;
}
