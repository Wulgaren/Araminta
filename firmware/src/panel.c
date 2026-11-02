/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Physical panel: which matrix button / encoder carries which printed label.
 * The matrix ids are known, the printed labels are not, so
 * the mapping is a table with a best guess, overridable by HARDWARE CALIBRATION
 * (hold OCT- and OCT+ while powering on): FELUCCA asks for each label in turn.
 * The learned table lives in .noinit and, with FELUCCA_FLASH, in flash with
 * the settings (project.c); read it back with `fm1t memr` to bake it in. */
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
static const char *const B_NAME[NB] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
                                        "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
static const char *const E_NAME[NE] = {"SELECT", "ALGORITHM", "PRESETS", "KNOB 1", "KNOB 2",
                                        "KNOB 3", "KNOB 4"};
#define PANEL_MAGIC 0x50414E35u          /* "PAN5": bump when PANEL_DEFAULT changes */

typedef struct {
    uint32_t magic;
    uint8_t btn[NB];             /* matrix button id (0..13) per label */
    uint8_t enc[NE];             /* matrix encoder (0..6) per role */
    int8_t dir[NE];              /* +1 / -1 so that clockwise is + */
} panel_t;
panel_t panel __attribute__((section(".noinit")));

static const panel_t PANEL_DEFAULT = {
    PANEL_MAGIC,
    {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1},   /* PLAY = 12, REC = 13 (measured on hardware) */
    {0, 1, 6, 2, 3, 4, 5},                           /* SELECT = enc 0, ALGORITHM = enc 1, PRESETS = enc 6 (printed labels) */
    {1, 1, 1, 1, 1, 1, 1},
};

static void panel_init(void)                     /* also after a flash load: ids are used as array indexes and shifts */
{
    uint32_t i, buttons = 0, encoders = 0, ok = panel.magic == PANEL_MAGIC;
    for (i = 0; ok && i < NB; i++) {
        ok = panel.btn[i] < NB && !(buttons & (1u << panel.btn[i]));
        if (ok) buttons |= 1u << panel.btn[i];
    }
    for (i = 0; ok && i < NE; i++) {
        ok = panel.enc[i] < NE && !(encoders & (1u << panel.enc[i])) &&
             (panel.dir[i] == 1 || panel.dir[i] == -1);
        if (ok) encoders |= 1u << panel.enc[i];
    }
    if (!ok)
        panel = PANEL_DEFAULT;
    ft_btn_mask = 1u << panel.btn[B_REC];           /* free take: REC closes, PLAY drops (seq.c) */
    ft_drop_mask = 1u << panel.btn[B_PLAY];
}

static uint32_t panel_btn_of(uint32_t matrix_id)        /* label of a matrix button, NB if none */
{
    uint32_t b;
    for (b = 0; b < NB; b++)
        if (panel.btn[b] == matrix_id)
            return b;
    return NB;
}

/* steps of a role, + = clockwise */
static uint8_t panel_moved;                     /* a knob turned since ui.c scr_input last looked (SCREEN OFF) */
/* KNOB 1..4 also leave as relative CC for a DAW on the host: track 1 = CC 16..19, track 2 = 20..23, ...
 * (4 tracks x 4 knobs, song.sel picks the bank), value 64 + detents (Bin Offset), on the selected track's
 * MIDI channel (the one its keys use). PRESETS is pitch bend and SELECT the mod wheel (CC 1), persistent, on
 * the same channel, while both keep browsing / stepping pages. A fast turn carries the panel's acceleration
 * (ui_input.c accel) at a DAW parameter's range, 0..127, so it multiplies by up to 16; its own timing, so a
 * CC turn never disturbs the page knob's. The knobs' own job below is unchanged.
 *
 * The wheels are one pair for the selected track's channel: turning the track (ALGORITHM) puts the old
 * channel's bend back to centre and its mod to 0, so a bent note is never left behind. */
static uint32_t panel_cc_enc[6];     /* per knob: last read (ms) | direction << 24 | ms per detent << 25 (4,5: wheels) */
static uint16_t panel_bend = 8192;   /* pitch bend, 14-bit, centre */
static uint8_t panel_mod;            /* mod wheel, 0..127 */
static uint32_t panel_wheel_ch = 16u;   /* the channel the wheels are on (16: not chosen yet) */
static int32_t panel_cc_accel(uint32_t k, int32_t s)
{
    uint32_t now = fm1_ms & 0xFFFFFFu, st = panel_cc_enc[k], up = s > 0, pi = st >> 25, i, a, m = 1;
    a = (uint32_t)(s < 0 ? -s : s);
    i = ((now - st) & 0xFFFFFFu) / a;
    if (!st || ((st >> 24) & 1u) != up || i >= ACC_GAP)
        i = 127u;
    else if (pi < ACC_GAP) {
        uint32_t interval = i > pi ? i : pi;
        m = ACC_RATE / (interval ? interval : 1u);
        if (m > 16u)
            m = 16u;                            /* (0..127: the panel's cap for its wide values) */
        if (m < 1u)
            m = 1u;
    }
    panel_cc_enc[k] = now | up << 24 | (i ? i : 1u) << 25;
    return s * (int32_t)m;
}
/* the wheels follow the selected track's channel: leaving one puts its bend and mod back */
static void panel_wheels_route(void)
{
    uint32_t ch = trk_midi_ch(song.sel);
    if (ch == panel_wheel_ch)
        return;
    if (panel_wheel_ch < 16u) {
        if (panel_bend != 8192)
            midi_out_event(0x0Eu | (0xE0u | panel_wheel_ch) << 8 | 64u << 24);   /* LSB 0, MSB 64: centre */
        if (panel_mod)
            midi_out_event(0x0Bu | (0xB0u | panel_wheel_ch) << 8 | 1u << 16);    /* mod 0 */
    }
    panel_wheel_ch = ch;
    panel_bend = 8192;
    panel_mod = 0;
}
static void panel_cc(uint32_t role, int32_t s)
{
    uint32_t ch = trk_midi_ch(song.sel), cc;
    if (role >= EN_K1 && role <= EN_K4) {
        cc = 16u + 4u * song.sel + (role - EN_K1);
        s = panel_cc_accel(role - EN_K1, s);
        while (s) {
            int32_t d = s > 63 ? 63 : s < -63 ? -63 : s;
            midi_out_event(0x0Bu | (0xB0u | ch) << 8 | cc << 16 | (uint32_t)(64 + d) << 24);
            s -= d;
        }
    } else if (role == EN_PRESET) {             /* pitch bend: 1/64 of the range a detent */
        int32_t b = (int32_t)panel_bend + panel_cc_accel(4u, s) * 256;
        if (b < 0)
            b = 0;
        if (b > 16383)
            b = 16383;
        if (b == panel_bend)
            return;
        panel_bend = (uint16_t)b;
        midi_out_event(0x0Eu | (0xE0u | ch) << 8 | (uint32_t)(b & 127) << 16 | (uint32_t)(b >> 7) << 24);
    } else if (role == EN_SELECT) {             /* mod wheel: 0..127 */
        int32_t m = panel_mod + panel_cc_accel(5u, s);
        if (m < 0)
            m = 0;
        if (m > 127)
            m = 127;
        if (m == panel_mod)
            return;
        panel_mod = (uint8_t)m;
        midi_out_event(0x0Bu | (0xB0u | ch) << 8 | 1u << 16 | (uint32_t)m << 24);
    }
}
static int32_t panel_enc(uint32_t role)
{
    int32_t s;
    panel_wheels_route();
    s = fm1_enc_take(panel.enc[role]) * panel.dir[role];
    if (s) {
        panel_moved = 1;
        panel_cc(role, s);
    }
    return s;
}

/* Keep the retained settings layout fixed. palette: the index into UI_PALETTES (SET3 held an index
 * into the 20 palettes of earlier firmware) */
#define SETTINGS_MAGIC 0x53455434u              /* "SET4" */
#define SETTINGS_MAGIC_OLD 0x53455433u          /* "SET3" */
struct { uint32_t magic, palette, lowcut, zoom; } settings __attribute__((section(".noinit")));

static void settings_save(void);              /* project.c: flash copy (FELUCCA_FLASH) */

/* HOLD (menu): how long a button is held before its layer opens (ui_input.c). Saved in the settings record's
 * retired bold field as HOLD_TAG | index; any other value there (0 or 1 from older firmware) is the default */
#define HOLD_TAG 0x484C4400u
#define HOLD_DEF 1u
static const uint16_t HOLD_MS[4] = {300, 400, 500, 600};
static uint8_t settings_hold = HOLD_DEF;
static uint32_t hold_from_stored(uint32_t v) { return (v & ~3u) == HOLD_TAG ? v & 3u : HOLD_DEF; }
static uint32_t hold_to_stored(uint32_t old, uint32_t i)
{
    return i % 4u != HOLD_DEF ? HOLD_TAG | (i & 3u) : old > 1u ? 0u : old;
}
static int hold_stored_ok(uint32_t v) { return v <= 1u || (v & ~3u) == HOLD_TAG; }

/* LEDS (menu): OFF, DIM LO, DIM HI, INV. DIM HI (the default), the idle buttons and keys glow dim (~1/30) and the
 * active ones are lit (#35); DIM LO the same, darker (~1/60, hal/fm1_input.h FM1_LED_DIM_LO_NS); OFF no glow,
 * the active ones lit (as 1.0); INV the idle ones lit and the active ones dark, as the stock firmware (ui_input.c
 * ui_leds). Saved in the settings record's retired zoom field as LEDS_TAG | mode, the modes append-only: DIM 0
 * (now DIM HI) and INV 1 keep their values, OFF 2 and DIM LO 3 are new; any other value there (0 or 1 from older
 * firmware) is DIM HI. LEDS_MENU: the menu's order, darkest first */
#define LEDS_TAG 0x4C454400u                    /* "LED" */
enum { LEDS_DIM, LEDS_INV, LEDS_OFF, LEDS_DIM_LO, LEDS_COUNT };
static const uint8_t LEDS_MENU[LEDS_COUNT] = {LEDS_OFF, LEDS_DIM_LO, LEDS_DIM, LEDS_INV};
static const char *const LEDS_NAME[LEDS_COUNT] = {"DIM HI", "INV", "OFF", "DIM LO"};
static uint8_t settings_leds = LEDS_DIM;
static uint32_t leds_from_stored(uint32_t v) { return (v & ~3u) == LEDS_TAG && (v & 3u) < LEDS_COUNT ? v & 3u : LEDS_DIM; }
static uint32_t leds_to_stored(uint32_t old, uint32_t m)
{
    return m != LEDS_DIM && m < LEDS_COUNT ? LEDS_TAG | m : old > 1u ? 0u : old;
}
static int leds_stored_ok(uint32_t v) { return v <= 1u || ((v & ~3u) == LEDS_TAG && (v & 3u) < LEDS_COUNT); }
/* the menu's next / previous mode (KNOB 1 stops at the ends, OCT+ (s 0) cycles) */
static uint32_t leds_step(uint32_t m, int32_t s)
{
    uint32_t i = 0;
    while (i + 1u < LEDS_COUNT && LEDS_MENU[i] != m)
        i++;
    i = s > 0 ? (i + 1u < LEDS_COUNT ? i + 1u : i) : s < 0 ? (i ? i - 1u : 0u) : (i + 1u) % LEDS_COUNT;
    return LEDS_MENU[i];
}

static void settings_init(void)
{
    if (settings.magic == SETTINGS_MAGIC_OLD && settings.palette < 20u) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = palette_from_stored(settings.palette);
    }
    if (settings.magic != SETTINGS_MAGIC || settings.palette >= NPALETTES) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = UI_GREY_INDEX;      /* GREY (default; pitch black) */
        settings.lowcut = 0;
        settings.zoom = 0;                     /* (retired: the LEDS setting, settings_persist.c) */
    }
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)(settings.lowcut % 3u);
}
