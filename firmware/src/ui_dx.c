/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * JIANT (FELUCCA TONIC): EDIT > SOUND / TONE / MOVEMENT, a sound of the DRUM-X kit. */
/* Shown in the EDIT family on a DRUM track (ui.c page_visible). They edit the section's kit
 * (drumx_voice.c dx_kit, saved with the section: project.c FUN11 / FUNB):
 *   SOUND     LANE (BD A .. CB B: the sound and its patch; a key struck picks the lane), ENGI (its MODEL),
 *             and the source: WAVE / RATIO / KIND / CLAPS, NOIS / AIR
 *   TONE      FILT, COLR / CUT / MIX, CHAR / Q / TAIL, OSC2 (the FM's second oscillator)
 *   MOVEMENT  PTCH / RATE, PMOD / SIZZ / GAP / LEN, PENV / HPF / FALL, DCAY / LEN
 * Each card keeps its knob across the five models (CLASSIC shown as FM, SHELL, METAL, TEETH, CLICK): a card the
 * model does not use is drawn empty and its knob does nothing. The amounts read as percentages, as every other
 * page's 0..127 values do; PTCH reads as the note and its cents; OSC2 as its amount, 0..100 %. The values are the
 * side's; the MORPH plays between the two (EDIT MRPH). The panel shows the kit's lanes with the edited lane lit
 * (ui_graph.c graph_colony). */
static uint8_t dx_ui_seen = 0xFFu;                   /* the last key note followed */
static const char *const DX_LANE_ABBR[8] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"};
static const char *const DX_ENGINE_NAME[DXV_N] = {"FM", "SHELL", "METAL", "TEETH", "CLICK"};
static const char *const DX_WAVE_NAME[4] = {"SINE", "HARM", "METAL", "CLANG"};
static const char *const DX_SHELL_R_NAME[4] = {"OFF", "1.42", "1.94", "1.60"};
static const char *const DX_METAL_KIND_NAME[8] = {"HATS", "CYMBAL", "4 SQR", "CLUSTER", "BELL", "TRIADS", "2 SQR", "NOISE"};
static const char *const DX_CLAPS_NAME[4] = {"3", "4", "5", "6"};
static const char *const DX_CLICK_R_NAME[8] = {"3.00", "3.67", "4.81", "2.50", "0.74", "1.35", "3.25", "1.48"};
static const char *const DX_FILT_NAME[4] = {"LP", "BP", "HP", "GR"};
static const char *const DX_FM_DRV_NAME[4] = {"OFF", "DRIVE", "BURST", "DR+BU"};
static const char *const DX_SHELL_DRV_NAME[4] = {"OFF", "DRIVE", "SHORT", "DR+SH"};
static const char *const DX_Q_NAME[2] = {"WIDE", "NARROW"};
static const char *const DX_TEETH_TAIL_NAME[4] = {"7%", "20%", "27%", "57%"};
static const char *const DX_CLICK_DRV_NAME[8] = {"X1", "X2", "X6", "X8", "X1+", "X2+", "X6+", "X8+"};
static const char *const DX_PENV_NAME[4] = {"DECAY", "LONG", "NOISE", "SINE"};
static const char *const DX_HPF_NAME[4] = {"OFF", "6.9K", "8.8K", "11.7K"};
static const char *const DX_FALL_NAME[4] = {"3.5MS", "5MS", "7MS", "10MS"};

/* a key struck on the track (seq.c key_on: last_note) picks the sound it plays */
static void dx_follow(void)
{
    if (last_note != dx_ui_seen) {
        if (dx_ui_seen != 0xFFu)                     /* (the first look only takes note of it) */
            dx_ui_lane = (uint8_t)drum_lane(last_note);
        dx_ui_seen = last_note;
    }
}

/* the lane's mode fields: bits 0..1 WAVE, 2..3 FILTER, 4 SNAP, 5..6 pitch modulation, 7 DRIVE */
static uint32_t dx_mode_get(const dx_lane_t *L, uint32_t sh, uint32_t mask)
{
    return ((uint32_t)L->mode >> sh) & mask;
}
static void dx_mode_put(dx_lane_t *L, uint32_t sh, uint32_t mask, uint32_t v)
{
    L->mode = (uint8_t)((L->mode & ~(mask << sh)) | ((v & mask) << sh));
}
/* an index stepped one detent, clamped to 0..max */
static uint32_t dx_step(uint32_t ix, int32_t s, uint32_t max)
{
    return (uint32_t)clamp((int32_t)ix + (s > 0 ? 1 : -1), 0, (int32_t)max);
}
static void dx_mode_step(dx_lane_t *L, uint32_t sh, uint32_t mask, uint32_t max, int32_t s)
{
    dx_mode_put(L, sh, mask, dx_step(dx_mode_get(L, sh, mask), s, max));
}
/* the cards that read two fields as one index (written back by dx_knob) */
static uint32_t dx_metal_kind(const dx_lane_t *L) { return dx_mode_get(L, 0, 3u) | dx_mode_get(L, 7, 1u) << 2; }
static uint32_t dx_click_ratio(const dx_lane_t *L) { return dx_mode_get(L, 0, 3u) | dx_mode_get(L, 2, 1u) << 2; }
static uint32_t dx_drv_snap(const dx_lane_t *L) { return dx_mode_get(L, 7, 1u) | dx_mode_get(L, 4, 1u) << 1; }
static uint32_t dx_teeth_tail(const dx_lane_t *L) { return dx_mode_get(L, 4, 1u) | dx_mode_get(L, 7, 1u) << 1; }
static uint32_t dx_click_drv(const dx_lane_t *L) { return dx_mode_get(L, 5, 3u) | dx_mode_get(L, 7, 1u) << 2; }
/* the FILTER engine e plays: GR is FM's alone (METAL plays it as BP, SHELL and TEETH as HP) */
static uint32_t dx_filt_heard(const dx_lane_t *L, uint32_t e)
{
    uint32_t f = dx_mode_get(L, 2, 3u);
    return f != DXF_GR || e == DXV_CLASSIC ? f : e == DXV_METAL ? DXF_BP : DXF_HP;
}
static int dx_metal_q(const dx_lane_t *L) { return dx_filt_heard(L, DXV_METAL) == DXF_BP; }   /* SNAP: the BP's Q */
static int dx_fm_gr(const dx_lane_t *L) { return dx_mode_get(L, 2, 3u) == DXF_GR; }
static void dx_num_step(uint8_t *p, uint32_t k, int32_t s, int32_t max)
{
    *p = (uint8_t)clamp((int32_t)*p + accel(EN_K1 + k, s, max), 0, max);
}

static void dx_lane_name(char *b, uint32_t lane, uint32_t side)   /* "BD A" */
{
    str_cpy(b, DX_LANE_ABBR[lane & 7u], 8);
    str_cpy(b + str_len(b), side ? " B" : " A", 8 - str_len(b));
}

/* KNOB k (0..3) turned s on the page */
static void dx_knob(const page_t *pg, uint32_t k, int32_t s)
{
    dx_lane_t *L = &dx_kit[dx_ui_lane & 7u];
    uint8_t *v = dx_ui_side ? L->b : L->a;
    uint32_t l = dx_ui_lane & 7u, e = dx_mdl[l] % DXV_N, p = dx_pagen(pg), ix;

    if (p == 1u) {                                      /* SOUND */
        if (k == 0u) {                                  /* LANE: BD A .. CB B */
            ix = dx_step(l * 2u + (dx_ui_side ? 1u : 0u), s, 15u);
            dx_ui_lane = (uint8_t)(ix >> 1);
            dx_ui_side = (uint8_t)(ix & 1u);
        } else if (k == 1u) {                           /* ENGI: the lane's MODEL */
            dx_mdl[l] = (uint8_t)dx_step(dx_mdl[l], s, DXV_N - 1u);
        } else if (k == 2u) {
            if (e == DXV_METAL) {                       /* KIND: WAVE + DRIVE */
                ix = dx_step(dx_metal_kind(L), s, 7u);
                dx_mode_put(L, 0, 3u, ix);
                dx_mode_put(L, 7, 1u, ix >> 2);
            } else if (e == DXV_CLICK) {                /* RATIO: WAVE + FILTER's low bit */
                ix = dx_step(dx_click_ratio(L), s, 7u);
                dx_mode_put(L, 0, 3u, ix);
                dx_mode_put(L, 2, 1u, ix >> 2);
            } else {
                dx_mode_step(L, 0, 3u, 3u, s);          /* WAVE / RATIO / CLAPS */
            }
        } else if (e != DXV_CLICK) {
            dx_num_step(&v[DXP_NOISE], k, s, 127);
        }
        return;
    }
    if (p == 2u) {                                      /* TONE */
        if (k == 0u) {
            if (e != DXV_CLICK)                         /* FILT: from the filter heard */
                dx_mode_put(L, 2, 3u, dx_step(dx_filt_heard(L, e), s, e == DXV_CLASSIC ? 3u : 2u));
        } else if (k == 1u) {
            dx_num_step(&v[DXP_COLOR], k, s, 127);
        } else if (k == 2u) {
            switch (e) {
            case DXV_CLASSIC: case DXV_SHELL:           /* DRV: DRIVE + SNAP */
                ix = dx_step(dx_drv_snap(L), s, 3u);
                dx_mode_put(L, 7, 1u, ix);
                dx_mode_put(L, 4, 1u, ix >> 1);
                break;
            case DXV_METAL:                             /* Q: SNAP */
                if (dx_metal_q(L))
                    dx_mode_step(L, 4, 1u, 1u, s);
                break;
            case DXV_TEETH:                             /* TAIL: SNAP + DRIVE */
                ix = dx_step(dx_teeth_tail(L), s, 3u);
                dx_mode_put(L, 4, 1u, ix);
                dx_mode_put(L, 7, 1u, ix >> 1);
                break;
            default:                                    /* CLICK DRV: pitch-mod field + DRIVE */
                ix = dx_step(dx_click_drv(L), s, 7u);
                dx_mode_put(L, 5, 3u, ix);
                dx_mode_put(L, 7, 1u, ix >> 2);
                break;
            }
        } else if (e == DXV_CLASSIC) {
            dx_num_step(&dx_lay[l], k, s, 15);
        }
        return;
    }
    if (k == 0u) {                                      /* MOVEMENT */
        if (e != DXV_TEETH)
            dx_num_step(&v[DXP_PITCH], k, s, 127);
    } else if (k == 1u) {
        dx_num_step(&v[DXP_PMOD], k, s, 127);
    } else if (k == 2u) {
        if (e != DXV_CLICK && !(e == DXV_CLASSIC && dx_fm_gr(L)))
            dx_mode_step(L, 5, 3u, 3u, s);              /* PENV / HPF / FALL */
    } else {
        dx_num_step(&v[DXP_DECAY], k, s, 127);
    }
}

static void dx_empty_card(uint32_t c)
{
    draw_column(c, "", "", "", T_THEME, -1, ICON_NONE);
}
/* a card showing one of the sound's 0..127 amounts, as a share of 127 like the other pages' F_PCT */
static void dx_val_card(uint32_t c, const char *label, uint32_t val, uint32_t icon)
{
    char b[12];
    fmt_int(b, (val * 100u + 63u) / 127u);
    draw_column(c, label, b, "%", VAL(c), (int32_t)val * 1000 / 127, icon);
}
/* OSC2's LAYER (0..15) as the amount of it the sound carries, 0 .. 100 % at its top (the mix dx_layer applies to
 * the triangle tops out at 70 %: 100 % is the knob's end, not a sound with no first oscillator left) */
static void dx_layer_card(uint32_t c, const char *label, uint32_t val, uint32_t icon)
{
    char b[12];
    uint32_t pct = (val * 100u + 7u) / 15u;
    fmt_int(b, pct);
    draw_column(c, label, b, "%", VAL(c), (int32_t)pct * 10, icon);
}
/* PTCH: the note the oscillator starts on and its cents above it (3/4 semitone a step from C1, drumx_voice.c) */
static void dx_pitch_card(uint32_t c, const char *label, uint32_t val, uint32_t icon)
{
    char b[12], u[8];
    uint32_t q = val * 3u;                              /* 25-cent steps above MIDI 24 */
    note_name(b, 24u + q / 4u);
    u[0] = 0;
    if (q % 4u) {
        u[0] = '+';
        fmt_int(u + 1, (int32_t)(q % 4u) * 25);
        str_cpy(u + str_len(u), "c", 3);
    }
    draw_column(c, label, b, u, VAL(c), (int32_t)val * 1000 / 127, icon);
}
static void dx_ix_card(uint32_t c, const char *label, const char *val, uint32_t ix, uint32_t steps, uint32_t icon)
{
    draw_column(c, label, val, "", VAL(c), (int32_t)(ix * 1000u / (steps - 1u)), icon);
}

/* the four cards of SOUND */
static void dx_sound_cards(const dx_lane_t *L, const uint8_t *v, uint32_t e)
{
    const uint32_t l = dx_ui_lane & 7u, w = dx_mode_get(L, 0, 3u);
    char b[12];
    dx_lane_name(b, l, dx_ui_side);
    draw_column(0, "LANE", b, "", VAL(0u),
                (int32_t)(l * 2u + (dx_ui_side ? 1u : 0u)) * 1000 / 15, ICON_X_PATTERN);
    draw_column(1, "ENGI", DX_ENGINE_NAME[e], "", VAL(1u), (int32_t)e * 1000 / (DXV_N - 1), ICON_WAVE);
    switch (e) {
    case DXV_CLASSIC: dx_ix_card(2, "WAVE", DX_WAVE_NAME[w], w, 4, ICON_WAVE); break;
    case DXV_SHELL: dx_ix_card(2, "RATIO", DX_SHELL_R_NAME[w], w, 4, ICON_RATIO); break;
    case DXV_METAL: dx_ix_card(2, "KIND", DX_METAL_KIND_NAME[dx_metal_kind(L)], dx_metal_kind(L), 8, ICON_WAVE); break;
    case DXV_TEETH: dx_ix_card(2, "CLAPS", DX_CLAPS_NAME[w], w, 4, ICON_X_PATTERN); break;
    default: dx_ix_card(2, "RATIO", DX_CLICK_R_NAME[dx_click_ratio(L)], dx_click_ratio(L), 8, ICON_RATIO); break;
    }
    if (e == DXV_CLICK)
        dx_empty_card(3);
    else
        dx_val_card(3, e == DXV_TEETH ? "AIR" : "NOIS", v[DXP_NOISE], ICON_NOISE);
}

/* the four cards of TONE */
static void dx_tone_cards(const dx_lane_t *L, const uint8_t *v, uint32_t e)
{
    uint32_t f = dx_filt_heard(L, e), ix;
    if (e == DXV_CLICK)
        dx_empty_card(0);
    else
        dx_ix_card(0, "FILT", DX_FILT_NAME[f], f, e == DXV_CLASSIC ? 4u : 3u, ICON_CUTOFF);
    if (e == DXV_CLASSIC)
        dx_val_card(1, "COLR", v[DXP_COLOR], ICON_TONE);
    else if (e == DXV_CLICK)
        dx_val_card(1, "MIX", v[DXP_COLOR], ICON_MIX);
    else
        dx_val_card(1, "CUT", v[DXP_COLOR], ICON_CUTOFF);
    switch (e) {
    case DXV_CLASSIC: case DXV_SHELL:
        ix = dx_drv_snap(L);
        dx_ix_card(2, "CHAR", e == DXV_SHELL ? DX_SHELL_DRV_NAME[ix] : DX_FM_DRV_NAME[ix], ix, 4, ICON_DIST);
        break;
    case DXV_METAL:
        if (dx_metal_q(L))
            dx_ix_card(2, "Q", DX_Q_NAME[dx_mode_get(L, 4, 1u)], dx_mode_get(L, 4, 1u), 2, ICON_RESO);
        else
            dx_empty_card(2);
        break;
    case DXV_TEETH:
        ix = dx_teeth_tail(L);
        dx_ix_card(2, "TAIL", DX_TEETH_TAIL_NAME[ix], ix, 4, ICON_DECAY);
        break;
    default:
        ix = dx_click_drv(L);
        dx_ix_card(2, "CHAR", DX_CLICK_DRV_NAME[ix], ix, 8, ICON_DIST);
        break;
    }
    if (e == DXV_CLASSIC)
        dx_layer_card(3, "OSC2", dx_lay[dx_ui_lane & 7u], ICON_TONE);
    else
        dx_empty_card(3);
}

/* the four cards of MOVEMENT */
static void dx_move_cards(const dx_lane_t *L, const uint8_t *v, uint32_t e)
{
    int gr = e == DXV_CLASSIC && dx_fm_gr(L);
    const char *l0 = gr ? "RATE" : "PTCH", *l1 = gr ? "LEN" : "PMOD";
    uint32_t ix = dx_mode_get(L, 5, 3u);

    if (e == DXV_TEETH) {
        dx_empty_card(0);
    } else if (gr) {
        dx_val_card(0, l0, v[DXP_PITCH], ICON_RATE);
    } else {
        dx_pitch_card(0, l0, v[DXP_PITCH], ICON_PITCH);
    }
    if (e == DXV_METAL)
        l1 = "SIZZ";
    else if (e == DXV_TEETH)
        l1 = "GAP";
    dx_val_card(1, l1, v[DXP_PMOD], ICON_ENV);
    if (e == DXV_CLASSIC && !gr) {
        dx_ix_card(2, "PENV", DX_PENV_NAME[ix], ix, 4, ICON_ENV);
    } else if (e == DXV_SHELL) {
        dx_ix_card(2, "PENV", DX_PENV_NAME[ix], ix, 4, ICON_ENV);
    } else if (e == DXV_METAL) {
        dx_ix_card(2, "HPF", DX_HPF_NAME[ix], ix, 4, ICON_ENV);
    } else if (e == DXV_TEETH) {
        dx_ix_card(2, "FALL", DX_FALL_NAME[ix], ix, 4, ICON_ENV);
    } else {
        dx_empty_card(2);
    }
    dx_val_card(3, e == DXV_CLICK ? "LEN" : "DCAY", v[DXP_DECAY], ICON_DECAY);
}

/* the four cards */
static void dx_cards(const page_t *pg)
{
    const dx_lane_t *L;
    const uint8_t *v;
    uint32_t e, p;
    dx_follow();
    L = &dx_kit[dx_ui_lane & 7u];
    v = dx_ui_side ? L->b : L->a;
    e = dx_mdl[dx_ui_lane & 7u] % DXV_N;
    p = dx_pagen(pg);
    if (p == 1u)
        dx_sound_cards(L, v, e);
    else if (p == 2u)
        dx_tone_cards(L, v, e);
    else
        dx_move_cards(L, v, e);
}
/* what the cards and the panel show changed */
static uint32_t dx_sig(void)
{
    const dx_lane_t *L = &dx_kit[dx_ui_lane & 7u];
    uint32_t h = (uint32_t)dx_ui_lane * 31u + dx_ui_side * 977u + L->mode * 131u + dx_mdl[dx_ui_lane & 7u] * 7919u +
                 dx_lay[dx_ui_lane & 7u] * 104729u, i;
    for (i = 0; i < DXP_N; i++)
        h = (h ^ (uint32_t)L->a[i] << 8 ^ L->b[i]) * 16777619u;
    return h;
}
