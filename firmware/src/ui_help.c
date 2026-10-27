/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* HELP (1.5, Discussion #156; MENU > SYSTEM > HELP, OFF by default): a short, visual hint per page and per layer, key
 * caps and two or three words, never a screen of text.
 *   a page   entered (a page button, HOME): its hint in the footer's first row for HELP_FRAMES (~2 s; in place of the
 *            steps or the OCT+ / OCT- hint, which come back after), ui_draw.c draw_foot
 *   a layer  while its map shows: a second footer row under the layer's own (ui_layer.c draw_layer)
 * A hint is "[KEYCAP]WORDS" up to three times (gfx.c kc_tag names the keycaps); help_parse splits it for cv_key_row.
 * Pages by their title (a page added later has none until it is written here); HOME, HOME LEVELS and the DRUM grid by
 * what they are. Every hint fits its row with every word (tests/ui_test.c test_help: no word dropped). The setting and
 * the timer: ui.c help_on, help_start (PREF_HELP, ui.help_t) */

static const char H_EDIT[] = "[K1-4]SOUND[EDIT]HOLD: ENGINES";
static const char H_LANES[] = "[K1-4]LANES[EDIT]MUTES";
static const char H_FMOP[] = "[K1]OPERATOR[SAVE]HOLD: UNDO";
static const struct { const char *title, *hint; } HELP_PAGE[] = {
    {"ENV", "[K1-4]A D S R[ENV]ENV DEST"},
    {"ENV DEST", "[K1-4]ENV AMOUNTS[ENV]ENV"},
    {"LFO", "[K1-4]SHAPE[LFO]HOLD: MOMENTARY"},
    {"LFO 2", "[K1]SYNC TO BPM[SELECT]LFO DEST"},   /* SELECT walks the LFO section; the LFO button opens MOD */
    {"LFO DEST", "[K1-4]LFO AMOUNTS[LFO]MOD"},
    {"MOD", "[K1]SLOT[K2]SOURCE[K3]DEST"},
    {"EFFECTS", "[K1]CHOOSE[K2]ORDER[K3]USE AS"},
    {"DISTORT", "[K1]MODE[K2]DRIVE[K3]TONE"},
    {"CRUSH", "[K1]BITS[K2]RATE[K3]LPF"},
    {"PHASER", "[K1]RATE[K2]DEPTH[K3]FDBK"},
    {"FLANGER", "[K1]RATE[K2]DEPTH"},
    {"CHORUS", "[K1]RATE[K2]DEPTH"},
    {"SLICER", "[K1]ON[K2]PATTERN[FX]PUNCH"},
    {"TAPE", "[K1]SPEED[K2]WOW[K3]DROP[K4]MIX"},
    {"DELAY", "[K1]DIV OR MS[K4]L TIME, R RATE"},
    {"REVERB", "[K1]TYPE[K2]SIZE[K3]DAMP"},
    {"MASTER", "[K1-4]CLIP PNCH DUCK[GLO]SONG"},
    {"SCL", "[K2]SCALE[K3]MAP[SCL]CHORDS"},
    {"CHORD", "[K3]STRUM[K4]STROKE"},
    {"EDIT 1", H_EDIT},
    {"EDIT 2", H_EDIT},
    {"FILTER", "[K1]TYPE[K2]CUTOFF[K3]RES[K4]KTR"},
    {"FILTER DRIVE", "[K1]DRIVE[EDIT]FILTER"},
    {"SOUND", "[K1]LANE[K2]PITCH[K3]PMOD[K4]PENV"},
    {"SOUND 2", "[K1]DECAY[K2]NOISE[K3]COLOR[K4]MODE"},
    {"LANES", H_LANES},
    {"LANES 2", H_LANES},
    {"SLICES", "[K1]SLICE[K2]MOVE[OCT+]SPLIT, JOIN"},
    {"OPERATOR", H_FMOP},
    {"OP ENV", H_FMOP},
    {"VOICE", "[K1]VOICE[K2]GLIDE[K3]UNI DTN[K4]SPREAD"}, /* UNI DTN / SPRD blank off UNI / off POLY+UNI */

    {"SONG", "[K1]SECTION[KEYS]A-D[PLAY]SONG"},
    {"PRESETS", "[K2]ENGINE[K3]FAV[K4]LIST"},
    {"USER", "[K1]SLOT[OCT+]SAVE[SELECT]LIST"},
    {"PHRASES", "[K1]PHRASE[OCT+]LOAD[SAVE]PROJ"},
    {"PROJECT", "[K1]SLOT[K4]SAVE[EDIT]NAME"},
    {"CLEAR", "[KEYS]CLEAR[SAVE]PROJECT"},
    {"ARP", "[K1]MODE[KEYS]CHORD[ARP]ARP 2"},
    {"ARP 2", "[K3]LATCH[K4]ORDER[ARP]ARP"},
    {"PATTERN", "[K1]LEN[K2]DIV[SEQ]TOOLS"},
    {"SEQ TOOLS", "[KEYS]TOOLS[K1]LEN[K2]DIV"},
    {"STEP", "[KEYS]WRITE[K1]STEP[SELECT]PATTERN"},
    {"DETAIL", "[PRESETS]STEP[K1]CHANCE[K2]RATCH"},
    {"GENERATION", "[K1]LEN[K2]KICK[SELECT]MAP"},
    {"GENERATION 2", "[K2]MAP X[K3]MAP Y[K4]CHAOS"},
    {"AUTOMATION", "[K1]ROW[OCT+]DO[EDIT]DELETE"},
    {"PUNCH-IN", "[K1-4]MACROS[SELECT]PAGES"},
    {"MIXER", "[K4]PLAY[ALGO]TRACK"},
    {"CLOCK", "[OCT+]SELECT: TEMPO[GLO]MASTER"},
};
#define H_HOME "[OCT+]PRESETS[HOME]MENU"          /* Terminus: shorter than Inter Tight so the row fits */
#define H_LEVELS "[K1-4]T1-T4 LEVEL[HOME]MIXER"
#define H_GRID "[KEYS]STEPS[K2]LANE[SEQ]RATCH"
/* a layer's (LAYER_*): what its footer row (LAYERS[].foot) does not say */
static const char *const HELP_LAYER[LAYER_N] = {
    "", "[KEYS]MUTES[FX]LOCK", "[KEYS]SOLO MUTE TAP[K1-4]LEVELS", "[KEYS]ROOT[K1-4]ROOT SCL CHORD",
    "[K1]ENGINE[K2]SOUND[K3]FAV", "[K1]LENGTH[K3]SWING[K4]GATE", "[KEYS]CLEAR[OCT+]YES",
};

/* the hint of the page shown, 0 = none */
static const char *help_page(void)
{
    uint32_t i;
    const char *t;
    if (ui.home)
        return home_levels() ? H_LEVELS : H_HOME;
    if (grid_on() && !detail_on())
        return H_GRID;
    t = cur_page()->title;
    for (i = 0; i < NELEM(HELP_PAGE); i++)
        if (str_eq(HELP_PAGE[i].title, t))
            return HELP_PAGE[i].hint;
    return 0;
}
/* "[K1]SLOT[OCT+]SAVE" -> kh[] (at most 3), their words in b (bn bytes); the number of hints */
static uint32_t help_parse(const char *s, khint_t *kh, char *b, uint32_t bn)
{
    uint32_t n = 0, i = 0, len;
    int32_t id;
    while (s && n < 3u && (id = kc_tag(s, &len)) >= 0) {
        s += len;
        kh[n].key = (uint8_t)id;
        kh[n++].act = b + i;
        while (*s && *s != '[' && i + 2u < bn)
            b[i++] = *s++;
        b[i++] = 0;
    }
    return n;
}
/* a hint as a row of key hints from x 8 to 232 at y (the footer's rows: 2, 21); 0 = nothing to draw */
static int help_row(const char *s, int32_t y)
{
    khint_t kh[3];
    char b[48];
    uint32_t n = help_parse(s, kh, b, sizeof b);
    if (n)
        cv_key_row(8, 232, y, kh, n, 7u, T_BG);
    return n != 0u;
}
