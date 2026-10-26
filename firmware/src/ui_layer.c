/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca quick layers: while a page button is held, the keys and KNOB 1..4
 * are its shortcuts and its map shows over the page. One table (LAYERS) drives the gesture, the keys, the knobs,
 * the LEDs and the overlay:
 *   FX   HOLD  the performance effects (perform.c) and the track mutes while held; KNOB 1..4 its macros. The white
 *              keys hold the punch page's fixed map (1/2: PF_DEF, 2/2: PF2_DEF; SELECT walks the two on the
 *              sticky map; the 1.2 reassignment with PRESETS is retired)
 *   GLO  SET   black keys 1..4 (F#3 G#3 A#3 C#4) T1..T4 MUTE (latched; lit = sounding), F3..B3 SOLO T1..T4 while
 *              held (HOLD cells, a corner triangle), C4 UNMUTE ALL, D4 MIDI LEARN on / off (1.5, midi_learn.c), F4 TAP
 *              tempo; KNOB 1..4 T1..T4 LEVEL;
 *              GLO + PLAY: from the top without stopping
 *   SCL  SET   any key: its note name is ROOT; KNOB 1..4 ROOT SCL CHRD VOIC (LY_SCL: the SCL page's first two,
 *              the CHORD page's two; QNT TRN stay on SCL); the LEDs show the root lit and the scale's notes breathing
 *   EDIT SET   the white keys from F3: the engines in PRESETS order (one key each, the NENG_SHOWN one can pick:
 *              engines.c eng_vis), the next white key INIT (LY_INIT: E5)
 *              (the dialog); KNOB 1 ENG, 2 No. (the engine's sounds), 3 FAV. Sound loads as on PRESETS: the steps
 *              stay, SAVE held undoes, the editor gets RELOAD; they apply while playing too. 1.4.1: the black keys
 *              the selected track's engine's quick actions (EQA below: DRUM 1..8 its lanes' MUTE; other engines
 *              none yet, the keys dark)
 *   SEQ  SET   SEQ > SEQ TOOLS (the last SEQ section; the first tap is PATTERN): the white keys from F3 the sequence
 *              tools (ui_tools.c: CLEAR, REVERSE, SHIFT < >, RANDOM, COOK; on a DRUM track BEAT, MUTE and the lane's
 *              CLEAR REVERSE FILL RANDOM), black keys 1..8 the lane (DRUM); KNOB 1..4 LEN DIV SWING GATE (PATTERN's).
 *              E4 / F4 are OCT- / OCT+ on a synth (BEAT / MUTE on a kit); D5..G5 HALF, *2, HUMAN, DUP.
 *              Each action one undo (SAVE held), OCT- the track as the layer opened (tl_open)
 * REC taps arm / disarm (ui_input.c); CLICK / COUNT-IN / LEVEL live in MENU > AUDIO; CLEAR sequence on SAVE > CLEAR
 * and SEQ TOOLS.
 * The gesture: let go before HOLD (the menu: 0.3 .. 0.6 s) with nothing else touched: a tap, the button's page.
 * Held past HOLD alone: the map (a peek), letting go does nothing. A key, a knob or a button meanwhile: a combo,
 * the map at once, no tap. Keys pressed with the button down are the layer's (seq.c keyboard_block): silent, no
 * MIDI, never recorded; keys held before stay notes. Only one layer at a time: a second layer button is ignored.
 * PLAY and REC work in every layer (GLO held + PLAY: RESTART; GLO's lock: PLAY as always, #183); SAVE, HOME, SEQ and
 * the other page buttons are swallowed. In SET layers OCT± do not shift the octave: OCT- (on release, not with OCT+)
 * puts back what the layer changed since it opened. A HOLD key's effect lasts until the key is let go, the map with
 * it. No layer in the menu, a dialog, NAME or the UPDATE MODE countdown. After a tap, until the layer has been opened
 * once: "HOLD [GLO] QUICK" (the seen bits are kept with the settings, favorites.c spare byte).
 * The lock (Discussion #83): a double tap (the second press within LY_DTAP_MS of the first tap, both let go before
 * HOLD, nothing else touched) opens the map and keeps it open with no button held (ui.lock): ly_down() counts the lock
 * as the button held, so the keys (seq.c keyboard_block: kb_lock), KNOB 1..4, OCT- act exactly as held (PLAY starts
 * and stops: GLO's RESTART needs GLO held, layer_play). A tap of its button closes it (the press hands it back to the
 * button: held on, a peek; let go, closed; never a tap), so do another page button, HOME, SAVE, SEQ (each then acts as
 * always), the menu, a dialog, NAME. The first tap is not deferred (a single tap opens its page at once, no lag): it
 * opens the page and remembers the page it left (lys.nv); the second tap locks the layer over that page, put back, so
 * a double tap leaves the page as it was. A first tap that acted instead of opening a page (EDIT on STEP with a
 * step held: its locks; on USER / PROJECT: rename) arms no double tap: two quick taps there still act twice. */
#include "ui_tools.c"                                   /* SEQ TOOLS' actions */
enum { LK_HOLD, LK_SET };
typedef struct {
    uint8_t btn, kind, fam;            /* the button, HOLD / SET, the family whose first page KNOB 1..4 edit */
    const char *head;                  /* (FAM_HOME: the layer's own knobs) the header over the map */
    khint_t foot[3];                   /* the footer's key hints (a third with no word: none) */
} layer_t;
static const layer_t LAYERS[LAYER_N] = {
    {0, 0, 0, "", {{0, 0}, {0, 0}, {0, 0}}},
    {B_FX, LK_HOLD, FAM_HOME, "[FX]", {{0, 0}, {0, 0}, {0, 0}}},   /* (the page's own footer: the punch map does not claim
                                                      * it; keys, the mutes and G5 CLR show in the map itself) */
    {B_GLO, LK_SET, FAM_HOME, "[GLO]", {{KC_PLAY, "RESTART"}, {KC_OCTDN, "UNDO"}, {KC_GLO, "DONE"}}},
    {B_SCL, LK_SET, FAM_SCL, "[SCL]", {{KC_KEYS, "ROOT"}, {KC_OCTDN, "UNDO"}, {KC_SCL, "DONE"}}},
    {B_EDIT, LK_SET, FAM_HOME, "[EDIT]", {{KC_KEYS, "ENGINE"}, {KC_OCTDN, "UNDO"}, {KC_EDIT, "DONE"}}},
    {B_SEQ, LK_SET, FAM_SEQ, "[SEQ]", {{KC_KEYS, "TOOLS"}, {KC_OCTDN, "UNDO"}, {KC_SEQ, "DONE"}}},
    /* sticky-only (section_apply): not armed by SAVE; SAVE still opens the family via btn_hold */
    {B_SAVE, LK_HOLD, FAM_SAVE, "[CLEAR]", {{KC_KEYS, "ACTION"}, {KC_SAVE, "DONE"}, {0, 0}}},
};
static const uint8_t LY_KC[LAYER_N] = {0, KC_FX, KC_GLO, KC_SCL, KC_EDIT, KC_SEQ, KC_SAVE};
/* SCL's knobs: the key and its chord (cur_page() while the layer edits or draws them: page_over) */
static const page_t LY_SCL = {"SCL", FAM_SCL, SC_TRACK, GR_SCALE, 0, {P_ROOT, P_SCALE, P_CHRD, P_VOIC}};
/* SEQ TOOLS' knobs: LEN DIV SWING GATE (also the PATTERN page's) */
static const page_t LY_SEQ = {"PATTERN", FAM_SEQ, SC_TRACK, GR_STEPS, SEQ_SEC_STEP, {P_SLEN, P_SDIV, P_SSWING, P_SGATE}};
static const page_t *ly_page(uint32_t l) { return l == LAYER_SCL ? &LY_SCL : l == LAYER_SEQ ? &LY_SEQ : 0; }
#define LY_OPEN 2u                     /* ui.ly_t0: the map opened (no tap any more) */
#define LY_COMBO 4u                    /* .. by a combo */
#define LY_DEAD 8u                     /* .. pressed where there is no layer: does nothing */
#define LY_INIT ((uint32_t)NENG_SHOWN) /* EDIT: the white key of INIT, the one after the engines (13: E5), its map cell */
typedef char ly_init_fits[LY_INIT < 16u ? 1 : -1];   /* (a white key: F3 .. G5) */
#define layer_seen (favorites.factory[15][31])   /* bit l: layer l opened once (a byte no engine uses) */
/* (the 1.2 key map (favorites.factory[15][14..23]) is retired: the pages are fixed maps (perform.c
 * PF_DEF / PF2_DEF); the 10 bytes are kept as saved but unused) */
static const khint_t GLO_LOCK_FOOT[3] = {{KC_OCTDN, "UNDO"}, {KC_GLO, "DONE"}, {0, 0}};   /* (locked: PLAY plays, #183) */

static struct {
    uint8_t l, trk, loaded, oct;       /* SET: the layer and the track of the snapshot; EDIT: a sound loaded since
                                        * it opened (SEQ: a tool used); OCT- / OCT+ pressed in a SET layer (bits) */
    uint8_t cook;                      /* SEQ: COOK pressed since it opened */
    uint8_t fxk;                       /* FX: the white key pressed last in the layer (+ 1; 0 none) */
    uint8_t mute[NPART];               /* EDIT: the DRUM lanes' mutes when it opened (eng_drum.c drum_mute; OCT-) */
    uint32_t eqh;                      /* EDIT: the keys (bits 0..26) of the HOLD quick actions held */
    int16_t v[9];                      /* the values when it opened: GLO mutes, levels, BPM; SCL ROOT..TRN, CHRD VOIC;
                                        * SEQ LEN DIV SWING GATE */
    uint32_t solo;                     /* GLO: the keys held that solo */
    uint32_t tap[4];                   /* GLO TAP: the last taps (fm1_ms) */
    uint8_t ntap;
    uint8_t quiet;                     /* #39: a layer closed after it opened: KNOB 1..4 do nothing until quiet_t + */
    uint32_t quiet_t;                  /* LY_QUIET_MS (fm1_ms) */
    uint8_t dt_l, dt_hint, dtap;       /* #83: the layer of the last tap that opened a page (0 none), its hint said; the
                                        * press now armed is that tap's second (a double tap) */
    uint32_t dt_ms;                    /* .. when that tap was let go (fm1_ms) */
    struct { uint8_t home, page, act, seq, fam[FAM_COUNT]; } nv;   /* .. the page it left (put back by the lock) */
} lys;
#define LY_QUIET_MS 250u
#define LY_DTAP_MS 300u                /* #83: the second press of a double tap at most this long after the first tap */

static int layer_allowed(void) { return !ui.menu && !ui.confirm && !ui.uboot && !name_on(); }
static uint32_t ly_bit(uint32_t l) { return 1u << panel.btn[LAYERS[l].btn]; }
static uint32_t ly_down(uint32_t l) { return l && ((fm1_in.buttons & ly_bit(l)) != 0u || ui.lock == l); }   /* (locked: held) */
static uint32_t layer_bits(void)
{
    uint32_t l, m = 0;
    for (l = LAYER_FX; l < LAYER_N; l++)
        if (l != LAYER_CLR)                             /* CLEAR: sticky section only (SAVE is not a hold layer) */
            m |= ly_bit(l);                             /* (1.2: every layer on every page, SEQ's too) */
    return m;
}
static uint32_t layer_btn(void) { return LAYERS[ui.layer % LAYER_N].btn; }
/* button only; FX LATCH still names the mode (HOLD / SET / TOOLS were redundant once the map is up) */
static const char *layer_head(void) { return ui.layer == LAYER_FX && perf_latch_on ? "[FX] LATCH" : LAYERS[ui.layer % LAYER_N].head; }
/* map open: sticky section lock (Punch-in / SEQ tools), or a button held past open (retired for maps) */
static uint32_t layer_open(void)
{
    uint32_t l = ui.lock ? ui.lock : (ui.ly && (ui.ly_t0 & LY_OPEN) ? ui.ly : 0u);
    return l && ly_down(l) && layer_allowed() ? l : 0u;
}
static int layer_set_open(void)
{
    return layer_open() && LAYERS[layer_open()].kind == LK_SET;
}
static uint32_t layer_held(void) { return ui.ly && ly_down(ui.ly) && layer_allowed(); }

/* each pass, before the keys: what the ISR (seq.c keyboard_block) gives to a layer. Armed: its button; none
 * armed: any layer button not already down (one pressed meanwhile is armed below; one held over from before
 * is ignored, its keys stay notes); armed but dead (pressed under a dialog, a menu or NAME that has closed
 * since): nothing, its keys stay notes and FX starts no effect with no map shown; FX's effects only with FX */
static void layer_masks(void)
{
    uint32_t m = !layer_allowed() || (ui.ly && (ui.ly_t0 & LY_DEAD)) ? 0u
               : ui.ly ? ly_bit(ui.ly) : layer_bits() & ~fm1_in.buttons;
    kb_mask = m;
    perf_mask = m & ly_bit(LAYER_FX);
    kb_lock = (uint8_t)(m && ui.lock ? 1u | (ui.lock == LAYER_FX ? 2u : 0u) : 0u);   /* (no button held: the ISR's) */
}

/* a layer button pressed (its edge) while none is armed: it is now, a dead one where no layer opens */
static void layer_arm(uint32_t pressed, uint32_t now)
{
    uint32_t l;
    if (ui.ly)
        return;
    for (l = LAYER_FX; l < LAYER_N; l++)
        if (l != LAYER_CLR && (pressed & ly_bit(l))) {
            ui.ly = (uint8_t)l;
            ui.ly_t0 = (now & ~15u) | 1u | (layer_allowed() ? 0u : LY_DEAD);
            lys.dtap = lys.dt_l == l && fm1_ms - lys.dt_ms <= LY_DTAP_MS;   /* (#83: the second tap of a double tap?) */
            lys.dt_l = 0;
            return;
        }
}

/* the armed layer lets go (its button up, or its lock closed): FX's macros snap back (FX LATCH: they stay); one that
 * opened, or a combo: KNOB 1..4 quiet for a while (#39: the knob still turning is not the page's) */
static void layer_let_go(uint32_t quiet)
{
    if (ui.ly == LAYER_FX && !perf_latch_on)
        perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
    if (quiet) {
        lys.quiet = 1;
        lys.quiet_t = fm1_ms;
    }
    ui.ly_t0 = 0;
    ui.ly = 0;
    ui.lock = 0;
}

/* #83, each pass before the layer's buttons are read: a locked layer closes. Its own button pressed: the lock goes,
 * the button held keeps it open (a peek: let go, it closes, no tap). Another button but PLAY, REC and OCT- / OCT+
 * (another page button, another layer's, HOME, SAVE, SEQ): closed now, and that button acts as always. Any other
 * button pressed between the taps of a double tap: no double tap */
static void layer_lock_input(uint32_t pressed)
{
    uint32_t keep = 1u << panel.btn[B_PLAY] | 1u << panel.btn[B_REC] | 1u << panel.btn[B_OCTDN] | 1u << panel.btn[B_OCTUP];
    if (lys.dt_l && (pressed & ~ly_bit(lys.dt_l)))
        lys.dt_l = 0;
    if (!ui.lock)
        return;
    if (pressed & ly_bit(ui.lock))
        ui.lock = 0;
    else if (pressed & ~keep)
        layer_let_go(1);
}
static int layer_locked(void) { return ui.lock && ui.layer == ui.lock; }   /* (the map's header: the lock's mark) */

static void layer_opened(uint32_t l);
/* #83: the second tap of a double tap: the page the first one left comes back, and the layer locks open over it */
static void layer_lock(uint32_t l, uint32_t now)
{
    ui.home = lys.nv.home;
    ui.page = lys.nv.page;
    memcpy(ui.fam_last, lys.nv.fam, sizeof ui.fam_last);
    ui.act = lys.nv.act;
    song.seq_mode = lys.nv.seq;
    ui.entry_open = 0;
    if (lys.dt_hint)
        ui.msg_t = 0;                                   /* (the first tap's "HOLD [..] QUICK") */
    ui.lock = (uint8_t)l;
    ui.ly_t0 = (now & ~15u) | 1u | LY_OPEN;
    ui.force = 1;
    layer_opened(l);
}

/* the layer opened: once seen, no more hint; SET: what OCT- will put back */
static void layer_opened(uint32_t l)
{
    uint32_t k;
    if (!((layer_seen >> l) & 1u)) {
        layer_seen |= (uint8_t)(1u << l);
        settings_save();                                /* (deferred while playing) */
    }
    lys.l = (uint8_t)l;
    lys.trk = song.sel;
    lys.loaded = 0;
    lys.cook = 0;
    if (l == LAYER_SEQ) {                               /* the track as it is now (OCT-) */
        memcpy(tl_open.step, TSEL->step, sizeof tl_open.step);
        motion_snapshot_track(TSEL, &tl_open.mo);
        tl_open.sqnt = TSEL->p[P_SQNT];                  /* (HUMAN switches QUANTIZE off) */
    }
    for (k = 0; k < 4u; k++)
        if (l == LAYER_SEQ) {
            lys.v[k] = TSEL->p[P_SLEN + k];
        } else if (l == LAYER_GLO) {
            lys.v[k] = trk[k].p[P_MUTE];
            lys.v[4u + k] = trk[k].p[P_LEVEL];
        } else {
            lys.v[k] = TSEL->p[P_ROOT + k];
            lys.v[4u + k] = k < 2u ? TSEL->p[P_CHRD + k] : 0;
        }
    lys.v[8] = song.g[G_BPM];
    memcpy(lys.mute, drum_mute, sizeof lys.mute);
}

/* the armed button: 0, or the layer whose button was tapped (let go). Maps are section-sticky (section_apply),
 * not hold or double-tap. Holding the button still counts as a combo modifier. */
static uint32_t layer_gesture(uint32_t now, uint32_t combo)
{
    uint32_t *t0 = &ui.ly_t0, l = ui.ly;
    (void)now;
    if (!l)
        return 0;
    if (!layer_allowed()) {
        *t0 |= LY_DEAD;
        if (!section_map_layer())
            ui.lock = 0;                                /* (menu / dialog / NAME: drop lock unless a section needs it) */
    }
    if (ly_down(l) && (fm1_in.buttons & ly_bit(l)))
        return 0;                                       /* button still down: wait for release (no hold-open) */
    l = *t0 & LY_DEAD || combo ? 0u : l;               /* combo on release: no page tap */
    {
        uint32_t keep = ui.lock;                        /* section sticky lock survives the button release */
        layer_let_go(0);
        ui.lock = keep;
    }
    return l;
}

/* #39: KNOB 1..4 belong to no page while a layer lets go: the frame its button is let go (the turns read then
 * were made with it held), while its map still shows (its keys held after it), and LY_QUIET_MS after it closed.
 * Their turns are dropped there (ui_input); before, they edited the page under the layer (an ARP page: ARP on) */
static int layer_knobs_quiet(void)
{
    if (lys.quiet && fm1_ms - lys.quiet_t >= LY_QUIET_MS)
        lys.quiet = 0;
    /* sticky section maps (ui.lock): knobs are the map's; quiet only for a held button, post-release, or LY_QUIET */
    return layer_allowed() && (ui.ly || lys.quiet || (ui.layer && !ui.lock));
}

/* the map shows while the button is held open, and after it while its keys are still held */
static void layer_show(void)
{
    uint32_t show = layer_allowed() ? (layer_open() ? layer_open() : kb_layer ? ui.layer : 0u) : 0u;
    if (show != ui.layer) {
        ui.layer = (uint8_t)show;
        ui.force = 1;
    }
}

/* a tap: the button's section / page. No hold hint, no double-tap lock. */
static void layer_tap(uint32_t l)
{
    page_tap(LAYERS[l].btn);
    lys.dt_l = 0;
    lys.dtap = 0;
}

/* EDIT: a sound load in the layer. The first one takes the undo copy as the track is now (when the layer
 * opened, unless KNOB 2 / 3 had loaded already), so OCT- and SAVE held go back to it */
static uint32_t snd_id(void) { return TSEL->eng_req | (uint32_t)TSEL->preset << 8 | (uint32_t)TSEL->user << 16; }
static void edit_load(uint32_t e, int32_t step)
{
    uint32_t id = snd_id();
    if (!lys.loaded)
        undo.keep = 0;
    if (e < NENGINES) {
        if (e == TSEL->eng_req % NENGINES)
            return;                                     /* its engine already: the sound stays */
        select_engine(e);
        preset_hinted();
    } else {
        eng_list_step(step);
    }
    lys.loaded |= snd_id() != id;
}

/* GLO TAP: from the third tap the tempo of the taps (the last 4); 2 s without one starts over. INT clock only */
static void glo_tap(void)
{
    uint32_t n = lys.ntap, i;
    if (song.g[G_CLOCK]) {
        ui_message("TAP: CLK IS EXT");
        return;
    }
    if (n && fm1_ms - lys.tap[n - 1u] > 2000u)
        n = 0;
    if (n == 4u) {
        for (i = 0; i < 3u; i++)
            lys.tap[i] = lys.tap[i + 1u];
        n = 3;
    }
    lys.tap[n++] = fm1_ms;
    lys.ntap = (uint8_t)n;
    if (n >= 3u && lys.tap[n - 1u] != lys.tap[0]) {
        song.g[G_BPM] = (int16_t)clamp((int32_t)(60000u * (n - 1u) / (lys.tap[n - 1u] - lys.tap[0])),
                                       GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;
    }
}

/* the selected lane's channel mute (eng_drum.c drum_mute): on while a song plays, no pattern undo */
static void lane_mute_toggle(track_t *t, uint32_t l)
{
    uint32_t k = trk_index(t) % NPART;
    l %= NLANE;
    drum_mute[k] ^= (uint8_t)(1u << l);
    ui_say(drum_lane_name(t, l), (drum_mute[k] >> l) & 1u ? " MUTED" : " ON");
    ui.force = 1;
}
/* SEQ TOOLS: white key place p's action on the selected track (ui_tools.c), "STOP TO EDIT" while a song plays.
 * F4 MUTE toggles the selected drum lane and is not a pattern edit */
static void tools_key(uint32_t p)
{
    const char *m;
    char n[4];
    int a = tl2_at(TSEL, p);
    if (a >= 0) {                                       /* stored-note edits */
        if (chain_busy()) {
            ui_message("STOP TO EDIT");
            return;
        }
        if (!(m = tl2_do(TSEL, (uint32_t)a)))
            return;
        ui_message(m);
    } else {
        if (!tl_cell(TSEL, p))
            return;
        if (p == TL_MUTE) {
            lane_mute_toggle(TSEL, ui.lane);
            return;
        }
        if (chain_busy()) {
            ui_message("STOP TO EDIT");
            return;
        }
        if (!(m = tl_do(TSEL, p, ui.lane)))
            return;
        if (p == TL_COOK && lys.cook < 99u) {           /* "COOKED 3": the third since the layer opened */
            lys.cook++;
            fmt_int(n, lys.cook);
            ui_say("COOKED ", n);
        } else {
            ui_message(m);
        }
    }
    if (!lys.loaded)                                    /* (the first one: how to take it back) */
        str_cpy(ui.msg2, "[SAVE] HOLD TO UNDO", sizeof ui.msg2);
    lys.loaded = 1;
    ui.force = 1;
}

/* the FX layer's cells' names (NONE: PF_N) */
static const char *const PF_CELL[PF_NFX] = {"1/8", "1/16", "1/32", "REV", "LPF", "HPF", "STOP", "FRZ", "PITCH+", "PITCH-",
    "FLNG", "PHSR", "VERB", "ECHO", "OCT-", "OCT+", "/2", "DEC-", "DEC+", "ST8", "ST16", "ATK+", "ARP", "RND", "*2", "GLITCH",
    "1/12"};
static const char W_NOTE[16] = {'F', 'G', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'A', 'B', 'C', 'D', 'E', 'F', 'G'};
/* (fx_map_sync retired with the reassignment: the key maps are the pages' fixed PF_DEF / PF2_DEF) */

static uint32_t fx_key_held(void)
{
    return lys.fxk && ly_down(LAYER_FX) && ((kb_layer >> (lys.fxk - 1u)) & 1u) ? lys.fxk : 0u;
}
/* 1.4.1 ENGINE QUICK ACTIONS: the EDIT layer's black keys (1..11: F#3 .. F#5) do what the selected track's engine
 * offers there: EQA, per engine up to 11, black key n the n-th; past them (and on an engine with none) a key is dark
 * and does nothing. An action: its cell's name, HOLD (on while its key is held: act down, then up; a corner triangle,
 * breathes) or SET (act on each press), its handler and its LED (on: lit). They are performance state: no sound load,
 * no undo copy, not recorded; OCT- puts back what they changed since the layer opened (eqa_put_back). A new engine's
 * actions: a table and its row in EQA, and what OCT- puts back in eqa_put_back.
 *   DRUM  1..8 MUTE of the lanes KICK .. BELL (eng_drum.c drum_mute; the cells the kit's names), latched: lit =
 *         sounding, dark = muted (as GLO's track mutes); the cell of a muted lane as a muted track's (KEY) */
enum { EQ_SHOW_SEL, EQ_SHOW_MUTE };                /* the cell: on = the selection's fill; on = idle, off = KEY */
typedef struct {
    uint8_t kind, show, arg;                       /* LK_HOLD / LK_SET, EQ_SHOW_*, the handler's argument */
    const char *(*name)(const track_t *t, uint32_t arg);   /* the cell's (2 .. 3 characters) */
    void (*act)(track_t *t, uint32_t arg, uint32_t down);  /* SET: pressed (down 1); HOLD: pressed, let go (0) */
    uint32_t (*on)(const track_t *t, uint32_t arg);        /* the LED lit, the cell's state */
} eqa_t;
typedef struct { uint8_t eng, n; const eqa_t *a; } eqa_set_t;

static void eqa_drum_mute(track_t *t, uint32_t l, uint32_t down)
{
    if (!down)
        return;
    lane_mute_toggle(t, l);
}
static uint32_t eqa_drum_on(const track_t *t, uint32_t l) { return !((drum_mute[trk_index(t) % NPART] >> l) & 1u); }
#define EQA_LANE(l) {LK_SET, EQ_SHOW_MUTE, l, drum_lane_abbr, eqa_drum_mute, eqa_drum_on}
static const eqa_t EQA_DRUM[NLANE] = {EQA_LANE(0), EQA_LANE(1), EQA_LANE(2), EQA_LANE(3), EQA_LANE(4), EQA_LANE(5),
    EQA_LANE(6), EQA_LANE(7)};
static const eqa_set_t EQA[] = {
    {ENGI_DRUM, NLANE, EQA_DRUM},
};
typedef char eqa_fits[NLANE <= 11 ? 1 : -1];       /* (11 black keys) */

static uint32_t eqa_count(const track_t *t)        /* the actions of t's engine (black keys 1..n) */
{
    uint32_t i, e = t->eng_req % NENGINES;
    for (i = 0; i < NELEM(EQA); i++)
        if (EQA[i].eng == e)
            return EQA[i].n;
    return 0;
}
static const eqa_t *eqa_of(const track_t *t, uint32_t p)   /* black key p's (0 = F#3), 0 none */
{
    uint32_t i, e = t->eng_req % NENGINES;
    for (i = 0; i < NELEM(EQA); i++)
        if (EQA[i].eng == e)
            return p < EQA[i].n ? &EQA[i].a[p] : 0;
    return 0;
}
/* black key k (its place p) pressed in the EDIT layer */
static void eqa_key(uint32_t k, uint32_t p)
{
    const eqa_t *a = eqa_of(TSEL, p);
    if (!a)
        return;
    if (a->kind == LK_HOLD)
        lys.eqh |= 1u << k;
    a->act(TSEL, a->arg, 1);
    ui.force = 1;
}
/* each pass: the HOLD actions whose keys were let go */
static void eqa_release(void)
{
    uint32_t r = lys.eqh & ~kb_layer, k;
    const eqa_t *a;
    lys.eqh &= ~r;
    for (k = 0; r && k < 27u; k++)
        if (((r >> k) & 1u) && (a = eqa_of(TSEL, key_place(k))) != 0 && a->kind == LK_HOLD) {
            a->act(TSEL, a->arg, 0);
            ui.force = 1;
        }
}
/* OCT- in the EDIT layer: the actions' state as the layer opened (1 = something changed) */
static int eqa_put_back(void)
{
    int d = memcmp(drum_mute, lys.mute, sizeof drum_mute) != 0;
    memcpy(drum_mute, lys.mute, sizeof drum_mute);
    return d;
}
/* the map's signature of the actions: their LEDs and names */
static uint32_t eqa_sig(void)
{
    uint32_t i, n = eqa_count(TSEL), h = n;
    for (i = 0; i < n; i++) {
        const eqa_t *a = eqa_of(TSEL, i);
        h = h * 33u + a->on(TSEL, a->arg) + (uint32_t)(uintptr_t)a->name(TSEL, a->arg) * 7u;
    }
    return h;
}

static void clear_key(uint32_t p);                       /* SAVE > CLEAR sticky map */
static uint32_t clear_kind(uint32_t p);
static int clear_ready(uint32_t kind);

/* a key pressed in layer l (k: 0 = F3 .. 26 = G5) */
static void layer_key(uint32_t l, uint32_t k)
{
    uint32_t p = key_place(k), i;
    if (l == LAYER_FX) {
        if (!key_black(k) && p < PF_KEYS)
            lys.fxk = (uint8_t)(k + 1u);                /* (the held key's cell is marked) */
    } else if (l == LAYER_GLO) {
        if (key_black(k)) {
            if (p < NTRK)
                trk[p].p[P_MUTE] = (int16_t)(trk[p].p[P_MUTE] == PLAY_OFF ? PLAY_ON : PLAY_OFF);
        } else if (p == 4u) {
            for (i = 0; i < NTRK; i++)
                if (trk[i].p[P_MUTE] == PLAY_OFF)
                    trk[i].p[P_MUTE] = PLAY_ON;
        } else if (p == 7u) {
            glo_tap();
        } else if (p == 5u) {
            ml_toggle();                                /* D4: MIDI LEARN on / off (midi_learn.c) */
        }
    } else if (l == LAYER_SCL) {
        TSEL->p[P_ROOT] = (int16_t)((k + 5u) % 12u);    /* the key's note name (F3 = F) */
        scale_lock_after(TSEL, P_ROOT);
    } else if (l == LAYER_EDIT && !key_black(k)) {
        if (p < NENG_SHOWN && p < LY_INIT)
            edit_load(eng_vis(p), 0);
        else if (p == LY_INIT && chain_busy())
            ui_message("STOP TO EDIT");
        else if (p == LY_INIT)
            confirm_open(CF_INIT_SOUND, song.sel);      /* (the dialog closes the layer) */
    } else if (l == LAYER_EDIT) {
        eqa_key(k, p);                                  /* (black keys: the engine's quick actions) */
    } else if (l == LAYER_SEQ) {
        if (!key_black(k))
            tools_key(p);
        else if (p < NLANE && drum_track(TSEL))
            ui.lane = (uint8_t)p;                       /* (black keys 1..8: the lane, as on the grid) */
    } else if (l == LAYER_CLR) {
        if (!key_black(k))
            clear_key(p);
    }
}
/* GLO open: white keys F3..B3 solo that track while held (perform.c perf_solo). LEDs read lys.solo by key. */
static void glo_solo_held(void)
{
    uint32_t i, s = 0, l = layer_open();
    lys.solo = 0;
    if (l != LAYER_GLO) {
        perf_solo = 0;
        return;
    }
    for (i = 0; i < 27u; i++) {
        uint32_t p;
        if (!((kb_layer >> i) & 1u) || key_black(i))
            continue;
        p = key_place(i);
        if (p >= NTRK)
            continue;
        lys.solo |= 1u << i;
        s |= 1u << p;
    }
    perf_solo = (uint8_t)s;
}
/* each pass: the keys the layer got now; then GLO's solo from the white keys still held */
static void layer_keys(uint32_t keys)
{
    uint32_t k, l = layer_open() ? layer_open() : ui.layer;
    /* hold maps removed: GLO + D4 still toggles MIDI LEARN (no mute/solo map on hold) */
    if (!l && ui.ly == LAYER_GLO && layer_held()) {
        for (k = 0; keys && k < 27u; k++)
            if (((keys >> k) & 1u) && !key_black(k) && key_place(k) == 5u)
                ml_toggle();
        glo_solo_held();
        return;
    }
    for (k = 0; keys && k < 27u; k++)
        if ((keys >> k) & 1u)
            layer_key(l, k);
    if (lys.eqh)
        eqa_release();
    glo_solo_held();
}
/* GLO held while MIDI LEARN is on: knobs still pick Tk LEVEL (map itself stays closed) */
static int glo_learn_knobs(void)
{
    return ui.ml.on && ui.ly == LAYER_GLO && layer_held() && !layer_open();
}

/* KNOB 1..4 in the open layer: its own (FX macros, GLO levels, EDIT's sound), else the four of its page */
static void layer_knob(uint32_t k, int32_t s)
{
    uint32_t l = layer_open() ? layer_open() : (glo_learn_knobs() ? LAYER_GLO : 0u);
    if (l == LAYER_FX) {                                /* perform.c perf_k, not recorded */
        if (k == 3u && !perf_harm_on())
            s = -s;                                     /* DEPTH (100 - perf_k[3]) rises to the right (#40: it fell) */
        perf_k[k] = (int8_t)clamp(perf_k[k] + accel(EN_K1 + k, s, k ? 100 : 200), k ? 0 : -100, 100);   /* (#126) */
    } else if (l == LAYER_GLO) {                        /* T1..T4 LEVEL, recorded as on MIXER */
        int16_t *vp = &trk[k].p[P_LEVEL];
        *vp = (int16_t)clamp(*vp + accel(EN_K1 + k, s, TP[P_LEVEL].max - TP[P_LEVEL].min), TP[P_LEVEL].min,
                             TP[P_LEVEL].max);
        motion_capture(&trk[k], P_LEVEL, *vp);
        ml_knob(&trk[k], P_LEVEL);                      /* (MIDI LEARN: Tk LEVEL picked) */
    } else if (l == LAYER_EDIT) {                       /* ENG, No., FAV */
        if (k == 0u)
            edit_load(eng_step(TSEL->eng_req, s), 0);
        else if (k == 1u)
            edit_load(NENGINES, s);
        else if (k == 2u)
            preset_mark(s > 0);
    } else if (l && l != LAYER_CLR) {                   /* the page's knobs, wherever the page is */
        uint8_t h = ui.home, pg = ui.page;
        ui.home = 0;
        ui.page = (uint8_t)page_first(LAYERS[l].fam);
        page_over = ly_page(l);
        edit_param(k, s);
        page_over = 0;
        ui.home = h;
        ui.page = pg;
    }
}

/* OCT+ held + PLAY: RESTART (from the top while playing); stopped: PLAY. 1 = done here. */
static int layer_play(void)
{
    if (!(fm1_in.buttons & (1u << panel.btn[B_OCTUP])) || chain_busy())
        return 0;
    if (song.g[G_CLOCK] && song.playing) {
        ui_message("RESTART: CLK IS EXT");
        return 1;
    }
    transport_req = song.playing ? 3u : 1u;
    return 1;
}

/* OCT- / OCT+ pressed with a SET layer's button down before it opened: a combo, so it opens now,
 * before oct_taps and layer_oct read it (else the press was nobody's: no put back, no octave) */
static void layer_oct_open(uint32_t pressed)
{
    uint32_t l = ui.ly, oct = 1u << panel.btn[B_OCTDN] | 1u << panel.btn[B_OCTUP];
    if ((pressed & oct) && l && !(ui.ly_t0 & (LY_OPEN | LY_DEAD)) && layer_held() &&
        LAYERS[l].kind == LK_SET) {
        ui.ly_t0 |= LY_OPEN | LY_COMBO;
        layer_opened(l);
    }
}

/* OCT- / OCT+ in a SET layer: no octave; OCT- let go puts back what the layer changed. Returns the taps left.
 * FX LATCH Punch-in is not a SET layer: OCT± shift octave; G5 CLR turns latched FX off. */
static uint32_t layer_oct(uint32_t pressed, uint32_t oct)
{
    uint32_t dn = panel.btn[B_OCTDN], up = panel.btn[B_OCTUP], k, mine;
    if (layer_set_open())
        lys.oct |= (uint8_t)(((pressed >> dn) & 1u) | ((pressed >> up) & 1u) << 1);
    mine = oct & lys.oct;
    if (mine & 1u) {                                    /* OCT-: back to when it opened */
        if (lys.l == LAYER_EDIT) {
            uint32_t ld = lys.loaded;
            if (ld)
                undo_swap();                            /* (the copy from its first load) */
            lys.loaded = 0;
            if (eqa_put_back() && !ld)                  /* (the quick actions: the lanes' mutes) */
                ui_message("MUTES PUT BACK");
        } else if (lys.l == LAYER_SEQ) {
            track_t *t = &trk[lys.trk % NTRK];
            if (lys.loaded && chain_busy()) {
                ui_message("STOP TO EDIT");
            } else {
                if (lys.loaded) {                       /* (SAVE held then: the tools again) */
                    tl_undo_take(t);
                    memcpy(t->step, tl_open.step, sizeof t->step);
                    if (motion_replace_track(t, &tl_open.mo))
                        motion_full = 1;
                }
                for (k = 0; k < 4u; k++)
                    t->p[P_SLEN + k] = lys.v[k];
                t->p[P_SQNT] = tl_open.sqnt;            /* (HUMAN switches QUANTIZE off) */
                lys.loaded = 0;
                lys.cook = 0;
                ui_message("SEQUENCE PUT BACK");
            }
        } else if (lys.l == LAYER_GLO) {
            for (k = 0; k < NTRK; k++) {
                trk[k].p[P_MUTE] = lys.v[k];
                trk[k].p[P_LEVEL] = lys.v[4u + k];
            }
            song.g[G_BPM] = lys.v[8];
            ui_message("MIX PUT BACK");
        } else {
            track_t *t = &trk[lys.trk % NTRK];
            for (k = 0; k < 4u; k++)
                t->p[P_ROOT + k] = lys.v[k];
            t->p[P_CHRD] = lys.v[4];
            t->p[P_VOIC] = lys.v[5];
            scale_lock_push(t);
            ui_message("SCALE PUT BACK");
        }
    }
    lys.oct &= (uint8_t)(((fm1_in.buttons >> dn) & 1u) | ((fm1_in.buttons >> up) & 1u) << 1);
    return oct & ~mine;
}

/* --------------------------------------------------------- the LEDs --- */
/* lit = in effect now (held, the value, a track sounding), breathing = can be pressed (*br: dark .. ~60 % and
 * back, ~1.1 s, hal/fm1_input.h fm1_led_breath; was a hard 250 ms blink, #119), dark = nothing there */
static int glo_sounding(uint32_t t) { return trk_heard(t); }
static uint32_t layer_leds(uint32_t *br)
{
    uint32_t k, m = 0, n = 0, l = ui.layer, held = perf_held | perf_latched, ok = perf_avail();
    uint32_t mask = scale_mask(TSEL), root = (uint32_t)TSEL->p[P_ROOT] % 12u;
    const eqa_t *a;
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k), b = (uint32_t)key_black(k), on = 0, can = 0, e;
        if (l == LAYER_FX) {                            /* effects breathe, held lit, a too-long REPEAT dark */
            e = perf_key(k);
            can = e < PF_N && ((ok >> e) & 1u);
            on = can && ((held >> e) & 1u);
        } else if (l == LAYER_GLO) {                    /* sounding lit; SOLO held lit, the others, C4, F4 breathe */
            on = b ? p < NTRK && glo_sounding(p) : (p < NTRK && ((lys.solo >> k) & 1u)) || (p == 5u && ui.ml.on);
            can = !b && (p < NTRK || p == 4u || p == 5u || (p == 7u && !song.g[G_CLOCK]));
        } else if (l == LAYER_SCL) {                    /* the root lit, the scale's notes breathe */
            e = (k + 5u + 12u - root) % 12u;
            on = e == 0u;
            can = (mask >> e) & 1u;
        } else if (l == LAYER_EDIT && !b) {             /* the engine lit, the others and INIT breathe */
            on = p < NENG_SHOWN && p < LY_INIT && eng_vis(p) == TSEL->eng_req % NENGINES;
            can = p < NENG_SHOWN && p < LY_INIT ? 1u : p == LY_INIT && !chain_busy();
        } else if (l == LAYER_EDIT && (a = eqa_of(TSEL, p)) != 0) {   /* the quick actions: on lit (DRUM: sounding), */
            on = a->on(TSEL, a->arg) != 0u;             /* a HOLD one off breathes, none dark */
            can = a->kind == LK_HOLD;
        } else if (l == LAYER_SEQ) {                    /* the tools breathe (not while a song plays); DRUM: the lane */
            int drum = drum_track(TSEL);                /* lit, the other lanes breathe; F4 lit while that lane is muted */
            uint32_t mu = drum ? (drum_mute[trk_index(TSEL) % NPART] >> (ui.lane % NLANE)) & 1u : 0u;
            int a = !b ? tl2_at(TSEL, p) : -1;
            if (a >= 0) {
                can = !chain_busy();
            } else if (!b && p == TL_MUTE) {
                on = drum && mu;
                can = drum && !mu;
            } else {
                on = b && p < NLANE && drum && p == ui.lane;
                can = b ? p < NLANE && drum : tl_cell(TSEL, p) && !chain_busy();
            }
        } else if (l == LAYER_CLR && !b) {              /* white keys with an action breathe when ready */
            uint32_t kind = clear_kind(p);
            can = kind != CF_NONE && clear_ready(kind);
        }
        m |= on << k;
        n |= (can & !on) << k;
    }
    if (br)
        *br = n;
    return m;
}

/* ------------------------------------------------------ the overlay --- */
/* One template: the header names the button ("[GLO]"; FX LATCH: "[FX] LATCH"); the cards are KNOB 1..4; the panel the
 * map of the keys as cells (the key's note name, a Fukiai icon, a name; FX's 16 white keys: the note and the effect's
 * short name, 4 a row); the footer the keycaps. A cell: RAISE
 * (can be pressed), the selection's fill (the value now, SET), the accent (held, HOLD), DIM (cannot now: pressing
 * it says why), KEY (a muted track, as the MUTE badge); HOLD cells in a SET layer: a corner triangle */
static const char B_NOTE[NTRK] = {'F', 'G', 'A', 'C'};  /* black keys 1..4: F# G# A# C# */
#define LC_X(c) (6 + 58 * (int32_t)(c))                 /* cell column c: 54 px wide, 4 px apart */
#define LC_W 54
#define LC_H 42                                          /* the big cells: rows at y 4 and 50 */
#define LF_H 19                                          /* FX: the white keys' rows (F3 .. B3, C4 .. F4, G4 .. C5, D5 .. G5) */
#define LM_Y 96                                          /* the black keys' row (22 px) */
#define LM_H 22
enum { LS_OFF, LS_SEL, LS_HELD, LS_WAIT, LS_DIM, LS_MUTE };
/* 1.5 (#173): | LS_ALT, an idle (OFF, DIM) cell of the FX map's 2nd and 4th columns in a fill of its own, the columns
 * light / dark in turn as the piano roll's keys, so the effects are told apart at a glance: SURF -> TEXT 22 % (the
 * idle RAISE is 12 %); LINE (idle cells unfilled) 10 %. GREY: LINE's 10 % and RAISE's 12 % land on the same gray
 * step (and LINE blanks any fill that equals RAISE), so bump LINE's alt to stay distinct. */
#define LS_ALT 0x10u
static uint16_t lc_alt(void)
{
    int pct = ux.style ? (ux.mono ? 28 : 10) : 22;
    return ux_mix(T_SURF, T_TEXT, pct);
}

static uint16_t lc_fill(uint32_t st, uint16_t *ink)
{
    uint32_t alt = st & LS_ALT;
    st &= ~LS_ALT;
    *ink = st == LS_DIM ? T_DIM : T_THEME;
    if (st == LS_MUTE) {
        *ink = T_INK;
        return T_KEY;
    }
    if (st == LS_HELD) {
        *ink = T_BG;
        return T_ACCENT;
    }
    if (st == LS_SEL || st == LS_WAIT) {
        *ink = T_INK;
        return T_THEME;
    }
    return alt ? lc_alt() : T_RAISE;
}
/* a cell at x, y, h px high: big (h > 30) the icon over the name; a name: compact, the icon at the right (none: no
 * icon); else a black key's: two icons at the right. tri: a HOLD cell in a SET layer */
/* a cell's box: its fill on the panel. LINE: a RAISE (idle) cell is not filled, and 1 px rules divide the
 * cells: one in the gap left of a cell (not the first column) and one in the gap above it (not the first row), each
 * across the gap's corner, so they meet in a grid. Cells are 4 px apart from x 6, y 4. Returns the fill */
static uint16_t lc_box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t fill)
{
    if (ux.style) {
        if (fill == T_RAISE)
            fill = T_SURF;
        if (x > 7)                                       /* (not the first column: x 6) */
            cv_rule(x - 2, y > 4 ? y - 4 : y, 1, h + (y > 4 ? 4 : 0));
        if (y > 4)
            cv_rule(x > 7 ? x - 4 : x, y - 2, w + (x > 7 ? 4 : 0), 1);
    }
    cv_rrect(x, y, w, h, 4, fill, T_SURF);
    return fill;
}
static void lcell(int32_t x, int32_t y, int32_t h, const char *note, uint32_t icon, uint32_t icon2, const char *name,
                  uint32_t st, int tri)
{
    uint16_t ink, fill = lc_fill(st, &ink), idle = fill == T_RAISE || ((st & LS_ALT) && fill == lc_alt());
    /* FX punch-ins (note + short name, no icon): draw note with the name, left-aligned on one band.
     * Mute row (note + icons, no name): note centred with the icons, not pinned to the top. */
    uint32_t punch = note && name && name[0] && h <= 24 && !icon && !icon2;
    fill = lc_box(x, y, LC_W, h, fill);
    if (note && !punch && name)
        cv_text_on(x + 5, y + 3, &AF_S, note, idle ? T_MID : ink, fill);
    if (h > 30) {                                   /* the icon's cell over the name, 6 px between, centred */
        int32_t t = y + HALF_UP(h - (16 + 6 + AF_S_CAP_H));
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V | AL_N(2), "layer cell icon + name centred up/down");
        GFX_HOOK_ALIGN(x, 0, x + LC_W, 0, AL_H | AL_PASS, "layer cell icon / name centred across");
        cv_icon_in(x, t, LC_W, 0, 16, icon, ink, fill);
        GFX_HOOK_ALIGN(x, 0, x + LC_W, 0, AL_H | AL_PASS, "layer cell icon / name centred across");
        cv_text_in(x, t + 22 - AF_S_CAP_Y, LC_W, &AF_S, name, ink, fill);
    } else if (name && h > 24) {                           /* 12 px icon, 2 px to the name's caps (cell bottom → caps) */
        int32_t ib[4], t;
        icon_ink(12, icon, ib);
        /* no note: centre by icon ink (MOUTH &c. sit low in the cell). with a note, keep the cell stack so the
         * name stays clear of the key letter */
        t = note ? y + HALF_UP(h - (12 + 2 + AF_S_CAP_H))
                 : y + HALF_UP(h - (14 + AF_S_CAP_H - ib[1])) - ib[1];
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V | AL_N(2), "layer cell icon + name centred up/down");
        cv_icon_on(note ? x + LC_W - 17 : x + 5, t, 12, icon, ink, fill);
        GFX_HOOK_ALIGN(x, 0, x + LC_W, 0, AL_H | AL_PASS, "layer cell icon / name centred across");
        cv_text_in(x, t + 14 - AF_S_CAP_Y, LC_W, &AF_S, name, ink, fill);
    } else if (punch) {                                    /* FX map: "F 1/8" left after the key letter */
        int32_t ty = y + CAP_IN(S, h);
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V | AL_N(2), "layer punch-in note + name centred up/down");
        cv_text_on(x + 5, ty, &AF_S, note, idle ? T_MID : ink, fill);
        cv_text_on(x + 5 + text_w(&AF_S, note) + 3, ty, &AF_S, name, ink, fill);
    } else if (name && *name) {                           /* a key's row: the icon and the name on its middle */
        if (!note && icon < ICON_COUNT) {                  /* (the EDIT layer: the engine's icon instead of the key) */
            GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "layer key cell icon / name centred up/down");
            cv_icon_in(x + 3, y, 0, h, 12, icon, ink, fill);
            GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "layer key cell icon / name centred up/down");
            cv_text_r(x + LC_W - 3, y + CAP_IN(S, h), &AF_S, name, ink, fill);
        } else {
            GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "layer key cell icon / name centred up/down");
            cv_text_r(x + LC_W - 5, y + CAP_IN(S, h), &AF_S, name, ink, fill);
        }
    } else if (!name) {                                    /* mute row: note + MUTE + track cushion, one band */
        if (note) {
            GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "layer mute cell note centred up/down");
            cv_text_on(x + 5, y + CAP_IN(S, h), &AF_S, note, idle ? T_MID : ink, fill);
        }
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "layer mute icons centred up/down");
        cv_icon_in(x + LC_W - 31, y, 0, h, 12, icon, ink, fill);
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "layer mute icons centred up/down");
        cv_icon_in(x + LC_W - 17, y, 0, h, 12, icon2, ink, fill);
    }
    if (tri) {
        int32_t j;
        for (j = 0; j < 4; j++)
            cv_rect(x + LC_W - 7 + j, y + 2 + j, 4 - j, 1, ink);
    }
}
static void bnote(char *n, uint32_t t) { n[0] = B_NOTE[t]; n[1] = '#'; n[2] = 0; }

static void layer_fx(void)
{
    uint32_t held = perf_kill ? 0u : perf_held | perf_latched, act = perf_act, ok = perf_avail(), p, e, st;
    uint32_t ed = fx_key_held() ? key_place(fx_key_held() - 1u) : PF_KEYS;
    char n[3] = {0, 0, 0};
    for (p = 0; p < PF_KEYS; p++) {                     /* the white keys F3 .. G5, 4 a row: the effect each holds */
        const char *cell;
        e = perf_map_now()[p];
        if (p == 15u && perf_latch_on) {                /* G5 CLR: latched punch-ins + macros off */
            cell = "CLR";
            st = p == ed ? LS_SEL : pfx_clr ? LS_HELD : LS_OFF;
        } else {
            cell = e < PF_NFX ? PF_CELL[e] : "";
            st = p == ed ? LS_SEL : e >= PF_NFX || !((ok >> e) & 1u) ? LS_DIM : !((held >> e) & 1u) ? LS_OFF
               : (act >> e) & 1u ? LS_HELD : LS_WAIT;
        }
        n[0] = W_NOTE[p];
        lcell(LC_X(p % 4u), 4 + (LF_H + 4) * (int32_t)(p / 4u), LF_H, n, 0, 0, cell,
              st | (p & 1u ? LS_ALT : 0u), 0);           /* (#173: the 2nd and 4th columns' idle cells their own fill) */
    }
    for (e = 0; e < NTRK; e++) {                        /* the mutes of the black keys 1..4 */
        bnote(n, e);
        lcell(LC_X(e), LM_Y, LM_H, n, ICON_MUTE, trk_icon(e, 0), 0, (held >> (PF_M1 + e)) & 1u ? LS_MUTE : LS_OFF, 0);
    }
}
static void layer_glo(void)
{
    uint32_t e;
    char n[3] = {0, 0, 0};
    for (e = 0; e < NTRK; e++) {                        /* F3 .. B3: SOLO while held */
        n[0] = W_NOTE[e];
        lcell(LC_X(e), 4, LC_H, n, trk_icon(e, 0), 0, "SOLO", (perf_solo >> e) & 1u ? LS_HELD : LS_OFF, 1);
    }
    n[0] = 'C';
    lcell(LC_X(0), 50, LC_H, n, ICON_MUTE, 0, "ALL", LS_OFF, 0);   /* (unmute all) */
    n[0] = 'D';
    lcell(LC_X(1), 50, LC_H, n, ICON_MIDI, 0, "LEARN", ui.ml.on ? LS_SEL : LS_OFF, 0);   /* (MIDI LEARN, 1.5) */
    n[0] = 'F';
    lcell(LC_X(3), 50, LC_H, n, ICON_TEMPO, 0, "TAP", song.g[G_CLOCK] ? LS_DIM : LS_OFF, 0);
    for (e = 0; e < NTRK; e++) {                        /* the black keys 1..4: MUTE, latched */
        bnote(n, e);
        lcell(LC_X(e), LM_Y, LM_H, n, ICON_MUTE, trk_icon(e, 0), 0, trk[e].p[P_MUTE] == PLAY_OFF ? LS_MUTE : LS_OFF, 0);
    }
}
static void layer_scl(void)                             /* KNOB 2's scales, 4 x 4, the one now selected */
{
    uint32_t i, sc = (uint32_t)TSEL->p[P_SCALE];
    for (i = 0; i < 16u && i <= (uint32_t)TP[P_SCALE].max; i++) {
        int32_t x = LC_X(i % 4u), y = 4 + 29 * (int32_t)(i / 4u);
        uint16_t ink, fill = lc_fill(i == sc ? LS_SEL : LS_OFF, &ink);
        fill = lc_box(x, y, LC_W, 25, fill);
        GFX_HOOK_ALIGN(x, y, x + LC_W, y + 25, AL_HV, "scale cell name centred");
        cv_text_in(x, y + CAP_IN(S, 25), LC_W, &AF_S, TP[P_SCALE].names[i], ink, fill);
    }
}
/* 1.4.1: the engine's quick actions (black keys 1..n) in a row of n cells under the engines, 18 px high, 4 px apart
 * (8: 25 px, as SEQ TOOLS' lanes; 11: 17 px): the name; SET EQ_SHOW_MUTE: off a muted track's KEY; a HOLD one's
 * corner triangle */
static void eqa_row(uint32_t n)
{
    uint32_t i, w = (228u - 4u * (n - 1u)) / n;
    for (i = 0; i < n; i++) {
        const eqa_t *a = eqa_of(TSEL, i);
        uint32_t on = a->on(TSEL, a->arg) != 0u, st = a->show == EQ_SHOW_MUTE ? (on ? LS_OFF : LS_MUTE)
                    : on ? (a->kind == LK_HOLD ? LS_HELD : LS_SEL) : LS_OFF;
        int32_t x = 6 + (int32_t)((w + 4u) * i), j;
        uint16_t ink, fill = lc_box(x, 100, (int32_t)w, 18, lc_fill(st, &ink));
        GFX_HOOK_ALIGN(x, 100, x + (int32_t)w, 118, AL_HV, "edit quick action cell name centred");
        cv_text_in(x, 100 + CAP_IN(S, 18), (int32_t)w, &AF_S, a->name(TSEL, a->arg), ink, fill);
        for (j = 0; a->kind == LK_HOLD && j < 4; j++)
            cv_rect(x + (int32_t)w - 7 + j, 102 + j, 4 - j, 1, ink);
    }
}
static void layer_edit(void)                            /* the engines from F3, INIT next (LY_INIT), the sound under them */
{                                                        /* (cells show the engine's icon, not the key's note) */
    uint32_t n = NENG_SHOWN < LY_INIT ? NENG_SHOWN : LY_INIT, cells = n + 1u, i, h = cells > 12u ? 20u : 28u;   /* 4 rows of 20: 8 px clear above the sound row */
    uint32_t na = eqa_count(TSEL);
    for (i = 0; i < cells; i++) {
        int32_t x = LC_X(i % 4u), y = 4 + (int32_t)(h + 4u) * (int32_t)(i / 4u);
        const engine_t *en = ENGINES[eng_vis(i) % NENGINES];
        if (i < n)
            lcell(x, y, (int32_t)h, 0, engine_icon(en->name), 0, h > 24u ? en->name : eng_abbr(en->name),
                  eng_vis(i) == TSEL->eng_req % NENGINES ? LS_SEL : LS_OFF, 0);
        else
            lcell(x, y, (int32_t)h, 0, ICON_X_WARN, 0, "INIT", chain_busy() ? LS_DIM : LS_OFF, 0);
    }
    if (na)                                             /* the black keys' quick actions in the sound's place (KNOB 2 */
        eqa_row(na);                                    /* still shows its number) */
    else
        engine_sound_row(104);
}

/* SAVE > CLEAR: white keys in columns under the cards (TRACK | ALL | SONG). Key opens confirm. */
enum {
    CL_PAT = 0, CL_SND, CL_MOT, CL_TRACKALL,             /* F3 G3 A3 B3 */
    CL_ALLPAT = 4, CL_ALLSND, CL_ALLMOT, CL_ALL,          /* C4 D4 E4 F4 */
    CL_SEC = 8, CL_SONG                                 /* G4 A4 */
};
static const char *const CL_CELL[16] = {
    "PAT", "SND", "MOT", "ALL", "PATS", "SNDS", "MOTS", "ALL", "SEC", "SONG", "", "", "", "", "", ""
};
static uint32_t clear_kind(uint32_t p)
{
    switch (p) {
    case CL_PAT: return CF_CLEAR_SEQ;
    case CL_SND: return CF_INIT_SOUND;
    case CL_MOT: return CF_CLEAR_MOTION;
    case CL_TRACKALL: return CF_CLEAR_TRACK_ALL;
    case CL_ALLPAT: return CF_CLEAR_ALL_SEQ;
    case CL_ALLSND: return CF_INIT_ALL_SOUND;
    case CL_ALLMOT: return CF_CLEAR_ALL_MOTION;
    case CL_ALL: return CF_CLEAR_EVERY_TRACK_ALL;
    case CL_SEC: return CF_DEL_ROW;
    case CL_SONG: return CF_CLEAR_SONG;
    default: return CF_NONE;
    }
}
static int clear_ready(uint32_t kind)
{
    uint32_t i;
    if (chain_busy())
        return 0;
    switch (kind) {
    case CF_CLEAR_SEQ: return !seq_is_empty(TSEL) || motion_count(TSEL);
    case CF_INIT_SOUND:
    case CF_CLEAR_TRACK_ALL: return 1;
    case CF_CLEAR_MOTION: return motion_count(TSEL) != 0u;
    case CF_CLEAR_ALL_SEQ:
        for (i = 0; i < NTRK; i++)
            if (!seq_is_empty(&trk[i]) || motion_count(&trk[i]))
                return 1;
        return 0;
    case CF_INIT_ALL_SOUND:
    case CF_CLEAR_EVERY_TRACK_ALL: return 1;
    case CF_CLEAR_ALL_MOTION:
        for (i = 0; i < NTRK; i++)
            if (motion_count(&trk[i]))
                return 1;
        return 0;
    case CF_DEL_ROW: return ui.song_row < chain_config.count;
    case CF_CLEAR_SONG: return chain_config.count != 0u;
    default: return 0;
    }
}
static void clear_key(uint32_t p)
{
    uint32_t kind = clear_kind(p);
    if (!kind)
        return;
    if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
    if (!clear_ready(kind)) {
        ui_message(kind == CF_DEL_ROW ? "NOTHING TO DEL" : "NOTHING TO CLEAR");
        return;
    }
    confirm_open(kind, kind == CF_DEL_ROW ? ui.song_row : song.sel);
}
static void layer_clr(void)
{
    uint32_t p, kind, st;
    char n[3] = {0, 0, 0};
    for (p = 0; p < 16u; p++) {
        if (!CL_CELL[p][0])
            continue;
        kind = clear_kind(p);
        st = chain_busy() || !clear_ready(kind) ? LS_DIM : LS_OFF;
        n[0] = W_NOTE[p];
        lcell(LC_X(p / 4u), 4 + (LF_H + 4) * (int32_t)(p % 4u), LF_H, n, 0, 0, CL_CELL[p],
              st | ((p / 4u) & 1u ? LS_ALT : 0u), 0);
    }
}

/* SEQ TOOLS: the white keys' tools, 4 a row from F3 (the whole sequence; RANDOM COOK BEAT MUTE; the lane's), on a
 * DRUM track the lanes of black keys 1..8 under them (the selected one filled, a muted lane as a muted track). A
 * melodic track: the sequence's tools plus the octave edits on E4 / F4 (BEAT / MUTE on DRUM). D5..G5 are the other
 * NOTE edits on every track. On DRUM the four rows are 20 px so the lane row still fits at y 100. */
static const uint8_t TL_ICON[TL_N] = {ICON_CLEAR, ICON_X_REVERSE, ICON_X_LEFT, ICON_X_RIGHT, ICON_PROB, ICON_W_SH,
    ICON_DRUM, ICON_MUTE, ICON_CLEAR, ICON_X_REVERSE, ICON_STEPS, ICON_PROB};
static const char *const TL_NAME[TL_N] = {"CLEAR", "REVERSE", "SHIFT", "SHIFT", "RANDOM", "COOK", "BEAT", "MUTE",
    "CLR", "REV", "FILL", "RND"};
static const char *const TL_NAME_S[TL_N] = {"CLEAR", "REVRS", "SHIFT", "SHIFT", "RANDOM", "COOK", "BEAT", "MUTE",
    "CLR", "REV", "FIL", "RND"};                       /* (20 px rows on DRUM: the cell has no room for the long names) */
static const uint8_t TL2_ICON[TL2_N] = {ICON_OCTAVE, ICON_OCTAVE, ICON_TIME, ICON_DIVISION, ICON_PROB, ICON_STEPS};
static const char *const TL2_NAME[TL2_N] = {"OCT-", "OCT+", "HALF", "*2", "HUMAN", "DUP"};
static void layer_seq_edit(uint32_t p, int32_t h, uint32_t row)
{
    char n[3] = {0, 0, 0};
    int a = tl2_at(TSEL, p);
    if (a < 0)
        return;
    n[0] = W_NOTE[p];
    lcell(LC_X(p % 4u), 4 + (h + 4) * (int32_t)row, h, n, TL2_ICON[a], 0, TL2_NAME[a],
          chain_busy() ? LS_DIM : LS_OFF, 0);
}
static void layer_seq(void)
{
    uint32_t p, drum, mute;
    int32_t h;
    char n[3] = {0, 0, 0}, nm[8];
    drum = (uint32_t)drum_track(TSEL);
    h = drum ? 20 : 28;
    mute = drum ? drum_mute[trk_index(TSEL) % NPART] : 0u;
    for (p = 0; p < TL_N; p++) {
        uint32_t st = chain_busy() && p != TL_MUTE ? LS_DIM : LS_OFF;
        if (!tl_cell(TSEL, p))
            continue;
        n[0] = W_NOTE[p];
        str_cpy(nm, h > 24 ? TL_NAME[p] : TL_NAME_S[p], sizeof nm);
        if (p >= TL_LCLR || p == TL_MUTE) {             /* the lane's: "CLR BD", "M BD" (MUTE shortened: Terminus) */
            if (p == TL_MUTE)
                str_cpy(nm, "M", sizeof nm);
            str_cpy(nm + str_len(nm), " ", 2);
            str_cpy(nm + str_len(nm), drum_lane_abbr(TSEL, ui.lane), sizeof nm - str_len(nm));
        }
        if (p == TL_MUTE && ((mute >> (ui.lane % NLANE)) & 1u))
            st = LS_MUTE;                               /* the selected lane is muted: the key's fill */
        lcell(LC_X(p % 4u), 4 + (h + 4) * (int32_t)(p / 4u), h, n, TL_ICON[p], 0, nm, st, 0);
    }
    if (!drum) {                                        /* E4 / F4: BEAT / MUTE on a kit, the octave edits here */
        layer_seq_edit(6u, h, 1u);
        layer_seq_edit(7u, h, 1u);
    }
    for (p = 12u; p < 16u; p++)                         /* D5..G5 on every track; row 3 on a kit, 2 on a synth */
        layer_seq_edit(p, h, drum ? 3u : 2u);
    for (p = 0; drum && p < NLANE; p++) {               /* the lanes (black keys 1..8): 8 cells of 25 px, 4 px apart.
                                                         * a muted lane: a muted track's fill; the selected one framed */
        int sel = p == ui.lane % NLANE, mu = (mute >> p) & 1u;
        int32_t x = 6 + 29 * (int32_t)p;
        uint16_t ink, fill = lc_box(x, 100, 25, 18, lc_fill(mu ? LS_MUTE : sel ? LS_SEL : LS_OFF, &ink));
        GFX_HOOK_ALIGN(x, 100, x + 25, 118, AL_HV, "tools lane cell name centred");
        cv_text_in(x, 100 + CAP_IN(S, 18), 25, &AF_S, drum_lane_abbr(TSEL, p), ink, fill);
        if (sel && mu)
            cv_frame(x, 100, 25, 18, T_ACCENT);
    }
}

static void layer_cards(uint32_t l)
{
    char val[12];
    const char *unit;
    uint32_t c;
    if (l == LAYER_FX) {                                /* FILTER CRUSH THROW DEPTH */
        int32_t m = perf_k[0];
        fmt_int(val, m < 0 ? -m : m);
        draw_column(0, "FILTER", m ? val : "OFF", m < 0 ? "LP" : m > 0 ? "HP" : "", m ? VAL(0u) : T_DIM, (m + 100) * 5,
                    ICON_CUTOFF);
        fmt_int(val, perf_k[1]);
        draw_column(1, "CRUSH", perf_k[1] ? val : "OFF", perf_k[1] ? "%" : "", perf_k[1] ? VAL(1u) : T_DIM,
                    perf_k[1] * 10, ICON_BITS);
        fmt_int(val, perf_k[2]);
        draw_column(2, "THROW", perf_k[2] ? val : "OFF", perf_k[2] ? "%" : "", perf_k[2] ? VAL(2u) : T_DIM,
                    perf_k[2] * 10, ICON_DELAY);
        if (perf_harm_on()) {                           /* OCT UP / DN playing: KNOB 4 is its shimmer (SHIMR) */
            fmt_int(val, perf_k[3]);
            draw_column(3, "SHIMR", perf_k[3] ? val : "OFF", perf_k[3] ? "%" : "", perf_k[3] ? VAL(3u) : T_DIM,
                        perf_k[3] * 10, ICON_FEEDBACK);
        } else {
            fmt_int(val, 100 - perf_k[3]);
            draw_column(3, "DEPTH", val, "%", VAL(3u), (100 - perf_k[3]) * 10, ICON_MIX);
        }
    } else if (l == LAYER_GLO) {                        /* T1..T4 LEVEL (the track's icon; muted: dim) */
        for (c = 0; c < NTRK; c++) {
            param_format(&TP[P_LEVEL], trk[c].p[P_LEVEL], val, &unit);
            draw_column(c, "LEVEL", val, unit, glo_sounding(c) ? VAL(c) : T_DIM, RATIO(&TP[P_LEVEL], trk[c].p[P_LEVEL]),
                        trk_icon(c, 1));
        }
    } else if (l == LAYER_EDIT) {
        engine_columns();
    } else if (l == LAYER_CLR) {                        /* categories; ALGORITHM still picks the track */
        char tn[3] = {'T', (char)('1' + song.sel % NTRK), 0};
        draw_column(0, "TRACK", tn, "", VAL(0u), -1, ICON_AUTO);
        draw_column(1, "ALL", "TRKS", "", VAL(1u), -1, ICON_AUTO);
        draw_column(2, "SONG", "ORDER", "", VAL(2u), -1, ICON_X_SONG);
        draw_column(3, "", "", "", T_THEME, -1, ICON_NONE);
    } else {                                            /* the page's four (SCL: LY_SCL) */
        uint8_t h = ui.home, pg = ui.page;
        ui.home = 0;
        ui.page = (uint8_t)page_first(LAYERS[l].fam);
        page_over = ly_page(l);
        draw_columns();
        page_over = 0;
        ui.home = h;
        ui.page = pg;
    }
}

static void draw_layer(void)
{
    uint32_t l = ui.layer % LAYER_N, sig = l * 7919u + ux.gen * 977u;
    layer_cards(l);
    if (l == LAYER_FX)
    {
        uint32_t p;
        sig += (perf_kill ? 0u : perf_held | perf_latched) * 31u + perf_latch_on * 11u + perf_act * 131u + perf_avail() * 7u + (uint32_t)perf_harm_on() * 3u +
               fx_key_held() * 4099u + perf_page2 * 17u;
        for (p = 0; p < PF_KEYS; p++)                   /* (the key map: the punch page's) */
            sig = sig * 33u + perf_map_now()[p];
    }
    else if (l == LAYER_GLO)
        sig += perf_solo * 31u + (uint32_t)song.g[G_CLOCK] * 5u + ui.ml.on * 7u +
               (uint32_t)((trk[0].p[P_MUTE] & 3) | (trk[1].p[P_MUTE] & 3) << 2 | (trk[2].p[P_MUTE] & 3) << 4 |
                          (trk[3].p[P_MUTE] & 3) << 6) * 131u;
    else if (l == LAYER_SCL)
        sig += (uint32_t)TSEL->p[P_SCALE] * 31u;
    else if (l == LAYER_SEQ)
        sig += (uint32_t)drum_track(TSEL) * 31u + ui.lane * 5u + (uint32_t)chain_busy() * 3u + (uint32_t)TSEL->p[P_E0] * 131u + song.sel * 977u +
               drum_mute[trk_index(TSEL) % NPART] * 2654435761u;
    else if (l == LAYER_CLR)
        sig += song.sel * 31u + (uint32_t)chain_busy() * 5u + chain_config.count * 7u + ui.song_row * 11u +
               (uint32_t)(!seq_is_empty(TSEL)) * 13u + motion_count(TSEL) * 17u;
    else
        sig += snd_id() * 31u + (uint32_t)preset_favorite() * 5u + (uint32_t)chain_busy() * 3u + up_gen * 101u +
               eqa_sig() * 2654435761u;
    if (ui.force || sig != ui.layer_sig) {
        ui.layer_sig = sig;
        cv_begin(240, H_GRAPH, T_BG);
        cv_rrect(3, 0, 234, H_GRAPH, 5, T_SURF, T_BG);
        cv_bg = T_SURF;
        if (l == LAYER_FX)
            layer_fx();
        else if (l == LAYER_GLO)
            layer_glo();
        else if (l == LAYER_SCL)
            layer_scl();
        else if (l == LAYER_SEQ)
            layer_seq();
        else if (l == LAYER_CLR)
            layer_clr();
        else
            layer_edit();
        cv_blit(0, Y_GRAPH);
    }
    if (ui.force) {                                     /* the footer: what the keys, knobs and buttons do */
        if (l == LAYER_FX) {                            /* the punch map keeps the page's own bottom bar */
            draw_foot();
        } else {
            cv_begin(240, H_FOOT, T_BG);
            const khint_t *ft = l == LAYER_GLO && ui.lock == LAYER_GLO ? GLO_LOCK_FOOT : LAYERS[l].foot;
            int h = help_on() && help_row(HELP_LAYER[l], 21);   /* 1.5, HELP ON: a second row, what this one does not say */
            cv_key_row(8, 232, h ? 2 : 9, ft, ft[2].act ? 3u : 2u, 7u, T_BG);
            cv_blit(0, Y_FOOT);
            ui.foot_sig = 0;
        }
    }
}
