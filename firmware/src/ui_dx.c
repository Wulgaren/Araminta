/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * JIANT (FELUCCA TONIC): EDIT > SOUND / SOUND 2, a sound of the DRUM-X kit. */
/* Shown in the EDIT family on a DRUM track (ui.c page_visible). They edit the section's kit
 * (drumx_voice.c dx_kit, saved with the section: project.c FUN11 / FUNB):
 *   SOUND    LANE (BD A .. CB B: the sound and its patch; a key struck picks the lane), PTCH, PMOD (amount),
 *            PENV (pitch-mod kind: DECAY LONG NOISE SINE)
 *   SOUND 2  DCAY, NOIS, COLR, MODE (wave x noise filter x SNAP x DRIVE: 48 combinations)
 * The values are the side's; the MORPH plays between the two (EDIT MRPH). The panel shows the kit's lanes
 * with the edited lane lit (ui_graph.c graph_colony). */
static uint8_t dx_ui_seen = 0xFFu;                   /* the last key note followed */
static const char *const DX_LANE_ABBR[8] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"};

/* a key struck on the track (seq.c key_on: last_note) picks the sound it plays */
static void dx_follow(void)
{
    if (last_note != dx_ui_seen) {
        if (dx_ui_seen != 0xFFu)                     /* (the first look only takes note of it) */
            dx_ui_lane = (uint8_t)drum_lane(last_note);
        dx_ui_seen = last_note;
    }
}

/* MODE 0..47: wave x 12 + filter x 4 + SNAP x 2 + DRIVE (PMOD-kind bits 5..6 kept aside) */
static uint32_t dx_mode_ix(uint32_t m)
{
    return (m & 3u) * 12u + (((m >> 2) & 3u) % 3u) * 4u + ((m >> 4) & 1u) * 2u + ((m & DX_DRIVE) ? 1u : 0u);
}
static uint8_t dx_mode_of(uint32_t ix, uint32_t keep)
{
    uint32_t wave = ix / 12u, flt = ix % 12u / 4u, snap = ix % 4u / 2u, drv = ix & 1u;
    return (uint8_t)((keep & 0x60u) | wave | flt << 2 | snap << 4 | (drv ? DX_DRIVE : 0u));
}
static void dx_mode_name(char *b, uint32_t m)        /* "FM BP+D" (b: 12 bytes) */
{
    static const char *const W[4] = {"SIN", "FM", "MTL", "BEL"}, *const F[3] = {"LP", "BP", "HP"};
    str_cpy(b, W[m & 3u], 12);
    str_cpy(b + str_len(b), " ", 12 - str_len(b));
    str_cpy(b + str_len(b), F[((m >> 2) & 3u) % 3u], 12 - str_len(b));
    if ((m >> 4) & 1u)
        str_cpy(b + str_len(b), "+", 12 - str_len(b));
    if (m & DX_DRIVE)
        str_cpy(b + str_len(b), "D", 12 - str_len(b));
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
    if (!dx_page2(pg)) {
        if (k == 0u) {                                  /* LANE: BD A .. CB B */
            int32_t ix = (int32_t)((dx_ui_lane & 7u) * 2u + (dx_ui_side ? 1u : 0u)) + (s > 0 ? 1 : -1);
            ix = clamp(ix, 0, 15);
            dx_ui_lane = (uint8_t)((uint32_t)ix >> 1);
            dx_ui_side = (uint8_t)((uint32_t)ix & 1u);
        } else if (k == 1u) {
            v[DXP_PITCH] = (uint8_t)clamp(v[DXP_PITCH] + accel(EN_K1 + k, s, 127), 0, 127);
        } else if (k == 2u) {
            v[DXP_PMOD] = (uint8_t)clamp(v[DXP_PMOD] + accel(EN_K1 + k, s, 127), 0, 127);
        } else {                                        /* PENV: pitch-mod kind */
            L->mode = (uint8_t)((L->mode & ~0x60u) |
                                (uint32_t)clamp((int32_t)((L->mode >> 5) & 3u) + (s > 0 ? 1 : -1), 0, 3) << 5);
        }
        return;
    }
    if (k < 3u) {
        uint32_t i = k == 0u ? DXP_DECAY : k == 1u ? DXP_NOISE : DXP_COLOR;
        v[i] = (uint8_t)clamp(v[i] + accel(EN_K1 + k, s, 127), 0, 127);
    } else {
        L->mode = dx_mode_of((uint32_t)clamp((int32_t)dx_mode_ix(L->mode) + (s > 0 ? 1 : -1), 0, 47), L->mode);
    }
}

/* the four cards */
static void dx_cards(const page_t *pg)
{
    const dx_lane_t *L = &dx_kit[dx_ui_lane & 7u];
    const uint8_t *v = dx_ui_side ? L->b : L->a;
    char val[12];
    dx_follow();
    if (!dx_page2(pg)) {
        static const char *const PM[4] = {"DECAY", "LONG", "NOISE", "SINE"};
        dx_lane_name(val, dx_ui_lane, dx_ui_side);
        draw_column(0, "LANE", val, "", VAL(0u),
                    (int32_t)((dx_ui_lane & 7u) * 2u + (dx_ui_side ? 1u : 0u)) * 1000 / 15, ICON_X_PATTERN);
        fmt_int(val, v[DXP_PITCH]);
        draw_column(1, "PTCH", val, "", VAL(1u), v[DXP_PITCH] * 1000 / 127, ICON_PITCH);
        fmt_int(val, v[DXP_PMOD]);
        draw_column(2, "PMOD", val, "", VAL(2u), v[DXP_PMOD] * 1000 / 127, ICON_ENV);
        draw_column(3, "PENV", PM[(L->mode >> 5) & 3u], "", VAL(3u), (int32_t)((L->mode >> 5) & 3u) * 333, ICON_ENV);
        return;
    }
    fmt_int(val, v[DXP_DECAY]);
    draw_column(0, "DCAY", val, "", VAL(0u), v[DXP_DECAY] * 1000 / 127, ICON_DECAY);
    fmt_int(val, v[DXP_NOISE]);
    draw_column(1, "NOIS", val, "", VAL(1u), v[DXP_NOISE] * 1000 / 127, ICON_NOISE);
    fmt_int(val, v[DXP_COLOR]);
    draw_column(2, "COLR", val, "", VAL(2u), v[DXP_COLOR] * 1000 / 127, ICON_TONE);
    dx_mode_name(val, L->mode);
    draw_column(3, "MODE", val, "", VAL(3u), (int32_t)dx_mode_ix(L->mode) * 1000 / 47, ICON_WAVE);
}
/* what the cards and the panel show changed */
static uint32_t dx_sig(void)
{
    const dx_lane_t *L = &dx_kit[dx_ui_lane & 7u];
    uint32_t h = (uint32_t)dx_ui_lane * 31u + dx_ui_side * 977u + L->mode * 131u, i;
    h ^= (uint32_t)dx_mot[0] << 24 ^ (uint32_t)dx_mot[1] << 16 ^ (uint32_t)dx_mot[2] << 8 ^ dx_mot[3];   /* (FUN11 X-MOD bytes) */
    for (i = 0; i < DXP_N; i++)
        h = (h ^ (uint32_t)L->a[i] << 8 ^ L->b[i]) * 16777619u;
    return h;
}
