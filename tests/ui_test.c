/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the UI (firmware/src/ui*.c with the sound sources of hostsim.c;
 * the display and the HAL are stubs, the buttons and knobs are driven through them):
 *   SOUNDS     a sound load (PRESETS knob / page, CLEAR INIT, user preset) changes the sound only: the
 *              steps, LEN DIV SWING GATE, ARP, SCL, the SLICER and the mix stay the track's. Power-on:
 *              every sequencer empty.
 *   PATTERNS   SAVE > PHRASES: KNOB 1 PAT (factory patterns, then user presets that hold one), OCT+
 *              LOAD; over the user's steps the REPLACE? dialog (OCT- / OCT+); the PRESETS hint.
 *   UNDO       a load keeps a copy of the track; SAVE held swaps back what the loads changed (again:
 *              redo); browsing keeps the copy from before the first load; power-on and projects: none.
 *   REC        a tap on every screen arms the selected track and starts PLAY, without navigation.
 *              Long REC never clears. On STEP, armed and playing: keys do not write the
 *              cursor step. With the ARP on, the arp's notes are recorded, not the keys.
 *   MIDI IN    MENU > MIDI > MIDI IN (ROUT): CH1-4 (channels 5..16 ignored) / SEL, note-offs follow their note-ons.
 *   SAVE       refused while playing ("STOP TO SAVE"); a used slot asks OVERWRITE? (OCT+ / OCT-).
 *   ACTIONS    PATTERNS, USER, PROJECT: the knobs pick, OCT+ does it, OCT- cancels or goes
 *              HOME; CLEAR: sticky keys open confirm. The OCT LEDs (OCT- lit, OCT+ blinks).
 *   TRACKS     the PRESETS knob does nothing there; KNOB 1 is MUTE.
 *   GRID       SEQ > STEP on a DRUM track: white keys toggle the selected lane's steps of the page, black
 *              keys 1..8 select the lane (and only they play), 9 held = ACC, 10 / 11 the page; KNOB 1..4 STEP
 *              LANE HIT ACC; the key LEDs (hits, the playhead inverted, the lane, ACC, the page keys); a step's
 *              GM notes shown on their lanes and made the lane's own by an edit; live recording into the grid
 *              (lane keys on the grid, GM keys elsewhere; other GM drums as notes; no TIE holds); 12 BEAT from
 *              PATTERNS fills the grid (undo); sound loads keep it, and other engines play it as GM notes.
 *   ROLL       the rolling digits of the card values and the header BPM: strips only, the last frame static,
 *              direction, retarget, the snaps (fast turn, names, shape, page, track, palette, force).
 *   SOAK       #63: 150000 random frames (turns, pages, transport, REC / motion, wrapping clocks): no roll
 *              outlives ROLL_FRAMES, the cards always equal a forced redraw once the rolls are over.
 *   FM6 OPS    (1.2, #76) EDIT > OPERATOR, OP ENV on an FM6 track: KNOB 1 the operator; FREQ (ratio then fixed), FINE, LEVEL, ENV bytes
 *              the pages edit written alone, SLOT OWN, the sound following, one undo per gesture, the macros on top,
 *              projects and user presets keep the patch; FM6 only, not automatable.
 *   USB SERIAL #67: MENU > USB SERIAL ON (default, the descriptors as before) / OFF (audio + MIDI only, class 0):
 *              the menu, the settings, the descriptor at boot (built with the console, as the firmware: FELUCCA_CDC).
 *   SCREEN OFF (1.1.5): MENU > DISPLAY > SCREEN OFF against a simulated clock and the panel's commands (lcd_power):
 *              dark after the time, nothing drawn, any input wakes it (SLPOUT, 120 ms, redraw, DISPON) and is
 *              swallowed; dialogs / count-in / UPDATE MODE keep it on; saved; the sound bit for bit while dark.
 * Built and run by tests/run_tests.sh. */
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 1                                    /* (as src/felucca.c: the console built in, MENU > USB SERIAL) */
#endif
#include <stddef.h>
#include <stdint.h>
static uint32_t host_slots[3u * 0x14000u / 4u];          /* USR1..3 (zero: empty), as the flash at 0xA0000 */
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main

/* ------------------------------------------------------------ HAL stubs --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL];
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_breath[FM1_NCOL], fm1_led_mid[FM1_NCOL];
static uint32_t host_dim_lo;                      /* hal/fm1_input.h: the glow MENU > LEDS asked for (1: DIM LO) */
static void fm1_led_dim_level(uint32_t lo) { host_dim_lo = lo; }
static int host_anim;                             /* hal/fm1_input.h: the power-on sweep running (it has the LEDs) */
static int host_idle, host_idle_glow;             /* .. the idle animation (1.2: MENU > ANIM IDLE) running, its glow */
static int fm1_led_anim_on(void) { return host_anim || host_idle; }
static void fm1_led_idle_start(uint32_t glow) { host_idle = 1; host_idle_glow = (int)glow; }
static int fm1_led_idle_on(void) { return host_idle; }
static void fm1_led_anim_stop(void) { host_anim = host_idle = 0; }
#define FM1_TICKS_PER_US 1u
static uint32_t host_ticks, host_pressed, host_notes;
static int32_t host_enc[7];
static uint32_t fm1_ticks(void) { return host_ticks; }
static uint32_t fm1_input_edges(int x) { uint32_t p = host_pressed; (void)x; host_pressed = 0; return p; }
static uint32_t fm1_input_note_edges(void) { uint32_t n = host_notes; host_notes = 0; return n; }
/* host_enc_late: detents the TIMER5 scan counts just after a read (#102: while ui_input runs); the next read has them */
static int32_t host_enc_late[7];
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = host_enc[e % 7u];
    host_enc[e % 7u] = host_enc_late[e % 7u];
    host_enc_late[e % 7u] = 0;
    return s;
}
static void (*host_wdt_hook)(void);                /* (a test's: time passing in a blocking loop, as panel_setup's) */
static void fm1_wdt_feed(void) { if (host_wdt_hook) host_wdt_hook(); }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static uint16_t host_screen[240 * 240];
/* #181 a blank frame: while host_fl_on, every write of a frame is watched (host_fl_begin .. host_fl_end); a pixel that
 * shows, between two writes, a value that is neither the one before the frame nor the one after (a fill of the
 * background before a canvas lands over it) flashed */
static uint8_t host_fl_on, host_fl_n[240 * 240];
static uint16_t host_fl_was[240 * 240], host_fl_mid[240 * 240];
static void host_put(uint32_t k, uint16_t v)
{
    if (host_fl_on) {
        if (!host_fl_n[k])
            host_fl_was[k] = host_screen[k];
        else if (host_screen[k] != host_fl_was[k] && host_fl_n[k] < 2u)
            host_fl_mid[k] = host_screen[k], host_fl_n[k] = 2;   /* (2: an in-between value kept) */
        if (!host_fl_n[k])
            host_fl_n[k] = 1;
    }
    host_screen[k] = v;
}
static void host_fl_begin(void) { memset(host_fl_n, 0, sizeof host_fl_n); host_fl_on = 1; }
static uint32_t host_fl_end(uint32_t y0, uint32_t y1)   /* the rows y0..y1-1: pixels that flashed */
{
    uint32_t k, n = 0;
    host_fl_on = 0;
    for (k = y0 * 240u; k < y1 * 240u; k++)
        n += host_fl_n[k] == 2u && host_fl_mid[k] != host_screen[k];
    return n;
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
#ifdef UI_FILL_HOOK
    UI_FILL_HOOK(x, y, w, h);                     /* (tests/ui_render.c: what a fill covers is gone) */
#endif
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            host_put((y + j) * 240u + x + i, (uint16_t)((c >> 8) | (c << 8)));
}
static void lcd_sync(void) {}
/* MENU > SCREEN OFF (lcd.c lcd_power / lcd_wake_now): what the panel was told, in order (0 off: DISPOFF, SLPIN;
 * 1 SLPOUT; 2 DISPON; 3 lcd_wake_now), with the time; host_bl the panel showing (the backlight itself stays on: PA2 is
 * the key matrix's /OE too, lcd.c lcd_power) */
static uint32_t host_pw_n, host_pw[256], host_pw_ms[256];
static int host_bl = 1;
static void host_pw_log(uint32_t s)
{
    if (host_pw_n < 256u) { host_pw[host_pw_n] = s; host_pw_ms[host_pw_n] = fm1_ms; }
    host_pw_n++;
    host_bl = s != 0u;
}
static void lcd_power(uint32_t s) { host_pw_log(s); }
static void lcd_wake_now(void) { host_pw_log(3u); }
/* (the HOME oscilloscope's buffer, scope_buf: fx.c, with hostsim.c) */
static uint32_t host_blit_rows;                    /* rows blitted (what a frame draws) */
static uint32_t host_blit_bytes;                   /* bytes the panel took (the strip cache) */
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    host_blit_rows += h;
    host_blit_bytes += w * h * 2u;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++) host_put((y + j) * 240u + x + i, p[j * w + i]);
}
static struct { uint32_t stage; } felucca_dbg;
#define FELUCCA_FLASH 0
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "TEST"
#endif
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/ui_graph.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/ui_layer.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

static int check(const char *what, int ok)
{
    printf("ui: %-74s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* main.c felucca_init */
static void ui_power_on(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(fx_order_tbl, 0, sizeof fx_order_tbl);
    memset(fx_master_tr, 0, sizeof fx_master_tr);
    fx_master_order_tr = 0;
    memset(&song, 0, sizeof song);
    memset(&chain, 0, sizeof chain);
    chain_defaults(&chain_config);
    memset(&ui, 0, sizeof ui);
    page_over = 0;                                /* (no layer's knobs; roll_settle's test page gone) */
    cin_left = cin_total = 0;                     /* (no count-in) */
    memset(&nm, 0, sizeof nm);                    /* NAME closed, no project name */
    memset(&mom, 0, sizeof mom);                  /* MOMENTARY: no value held (the pool at power-on) */
    proj_name[0] = 0;
    proj_cur = PROJ_NO_SLOT;
    memset(&favorites, 0, sizeof favorites);
    ml_off();                                     /* (MIDI LEARN off, nothing learned) */
    memset(&undo, 0, sizeof undo);
    fop_op = fop_stg = 0;                         /* (FM6's operator pages: OP 1, stage 1, as at power-on) */
    memset(pat_last, 0, sizeof pat_last);
    memset(pat_from, 0, sizeof pat_from);                /* (the header's slot letter: none) */
    memset(proj_slot, 0, sizeof proj_slot);
    memset(up_bank, 0, sizeof up_bank);
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    undo_depth++;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, TRK_DEF[i][0]);
        apply_preset_to(t, TRK_DEF[i][1]);
        t->engine = t->eng_req;
        track_defaults_steps(t);
        if (TRK_DEF[i][2])
            load_pat16(t, PATTERNS[TRK_DEF[i][2] - 1u].note, PATTERNS[TRK_DEF[i][2] - 1u].flags);
        pat_sig[i] = steps_sig(t);
        pat_last[i] = TRK_DEF[i][2];
    }
    undo_depth--;
    song.sel = 0;
    ui.home = 1;
    ui.force = 1;
    panel = PANEL_DEFAULT;
    settings.palette = UI_GREY_INDEX;
    palette_set(UI_GREY_INDEX);
    transport_req = 0;
    panic_req = 0;
    mi_r = mi_w = midi_in_overflow = 0;
    memset(midi_sel_on, 0, sizeof midi_sel_on);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_owners, 0, sizeof midi_owners);
    memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target);
    memset(kb_chn, 0, sizeof kb_chn);                   /* chord.c: no key, MIDI chord or last chord */
    memset(mchord, 0, sizeof mchord);
    memset(chord_last, 0, sizeof chord_last);
    perf_held = perf_act = kb_layer = perf_mask = 0;      /* the FX layer: nothing held */
    perf_latched = 0;
    perf_page(0);                                       /* .. its default page (PUNCH) */
    memset(kb_fx, 0, sizeof kb_fx);
    perf_kill = 0;
    perf_mask = 1u << panel.btn[B_FX];                  /* (ui_input sets them every frame) */
    kb_mask = layer_bits();
    perf_solo = 0;
    memset(&lys, 0, sizeof lys);
    perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
    settings_hold = HOLD_DEF;
    fm1_in.buttons = fm1_in.notes = 0;
    memset(&motion, 0, sizeof motion);                  /* (main.c: no motion at power-on) */
    memset(motion_active, 0, sizeof motion_active);
    memset(motion_locked, 0, sizeof motion_locked);
    motion_base_valid = motion_full = motion_save_lim = 0;
    memset(&scrn, 0, sizeof scrn);                        /* MENU > SCREEN OFF: on, idle from now (main.c) */
    scrn.idle = fm1_ms;
    kb_asleep = 0;
    panel_moved = 0;
}
/* card slot drawn with MOTION's mark (ui_draw.c card_mot: its icon, or a long label in the theme colour) */
static int card_motion(uint32_t slot) { return (card_mot >> slot) & 1u; }

/* one UI frame (input, then the drawing on the stub display) */
static void frame(void)
{
    host_ticks += 16000u;
    fm1_ms += 16u;
    ui_input();
    ui_draw();
}
static void press(uint32_t label)                 /* a tap: down, a frame, up, a frame */
{
    fm1_in.buttons |= 1u << panel.btn[label];
    host_pressed |= 1u << panel.btn[label];
    frame();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    frame();
}
static void frames(uint32_t ms) { uint32_t i; for (i = 0; i < ms / 16u; i++) frame(); }
static void btn_down(uint32_t label) { fm1_in.buttons |= 1u << panel.btn[label]; host_pressed |= 1u << panel.btn[label]; }
static void btn_up(uint32_t label) { fm1_in.buttons &= ~(1u << panel.btn[label]); }
static void hold(uint32_t label)                  /* held 0.8 s, then let go */
{
    uint32_t k;
    fm1_in.buttons |= 1u << panel.btn[label];
    host_pressed |= 1u << panel.btn[label];
    for (k = 0; k < 52u; k++)
        frame();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    frame();
}
static void turn(uint32_t role, int32_t s)
{
    host_enc[panel.enc[role]] += s * panel.dir[role];
    frame();
    host_ticks += 200000u;                        /* (no knob acceleration between the turns) */
}
static void stop_transport(void) { transport_req = 0; song.playing = 0; }
static int16_t stored_param(uint32_t slot, uint32_t track, uint32_t id)
{
    project_t p;
    return proj_import(&p, &proj_slot[slot], sizeof proj_slot[slot]) ? p.t[track].p[id] : -32768;
}
static uint8_t stored_note(uint32_t slot, uint32_t track, uint32_t step)
{
    project_t p;
    return proj_import(&p, &proj_slot[slot], sizeof proj_slot[slot]) ? step_note(&p.t[track].step[step], 0) : 0;
}
static int msg_is(const char *s) { return ui.msg_t && str_eq(ui.msg, s); }
static void my_steps(track_t *t)                  /* "recorded" steps */
{
    uint32_t i;
    track_defaults_steps(t);
    for (i = 0; i < 32u; i += 3u) {
        step_set_note(&t->step[i], 0, (uint8_t)(40 + i));
        t->step[i].n = 1;
        t->step[i].time = ST_NOTE;
        t->step[i].vel = 99;
    }
    t->p[P_SLEN] = 32;
}

static void go_page(uint32_t graph)               /* the page that draws graph */
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == graph)
            break;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
}
static void go_title(const char *title);          /* forward: go_seq_step */
static void key_down(uint32_t k);
static void key_up(uint32_t k);
static uint32_t white(uint32_t w);
static void go_seq_step(void) { go_title("STEP"); } /* SEQ opens STEP; grid / roll / step rec */
static void project_not_empty(void)  /* free-take steals step keys when every track is empty */
{
    trk[3].step[0].n = 1; step_set_note(&trk[3].step[0], 0, 60); trk[3].step[0].time = ST_NOTE;
}
/* SEQ > AUTOMATION (1.2): its first row (PLAY, CLEAR); + ADD with id (a parameter, EV_CHANCE, EV_RATCH) on step */
static void go_auto_top(void)
{
    go_page(GR_EVENTS);
    ui.ev_row = 0;
    ui.ev_keep = 0;
}
static void go_auto_add(uint32_t id, uint32_t step)
{
    go_page(GR_EVENTS);
    ui.ev_row = 0xFFFFu;                          /* (ev_row: the last, + ADD) */
    ui.ev_keep = 0;
    ui.ev_id = (uint8_t)id;
    ui.ev_step = (uint8_t)step;
}
/* the row showing c (EVC), selected; 0: not listed */
static int auto_pick(uint32_t c)
{
    uint16_t rw[EV_ROWS];
    uint32_t n = ev_list(rw), r;
    for (r = 0; r < n && rw[r] != c; r++)
        ;
    if (r == n)
        return 0;
    ui.ev_row = (uint16_t)r;
    return 1;
}
static int steps_are(const track_t *t, uint32_t n)  /* steps 1..16 = PATTERNS[n], 17..64 empty, LEN 16 */
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        uint8_t want = i < 16u ? PATTERNS[n].note[i] : 0u;
        if (step_note(&t->step[i], 0) != want || t->step[i].n != (want ? 1u : 0u))
            return 0;
    }
    return t->p[P_SLEN] == 16;
}
/* the sound of a track: engine, preset and the parameters that are not the track's own (param_kept) */
static int same_sound(const track_t *a, const track_t *b)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        if (!param_kept(i) && a->p[i] != b->p[i])
            return 0;
    return a->eng_req == b->eng_req && a->preset == b->preset;
}

/* the track's E values are DRUM's default kit (core.h DRUM_KIT_E: what a SAMPLE PERC sound loads as) */
static int perc_kit(const track_t *t)
{
    static const int16_t KIT[8] = DRUM_KIT_E;
    return !memcmp(&t->p[P_E0], KIT, sizeof KIT);
}
/* the GM drums a DRUM track plays: a kick, a snare and a closed hat each strike their lane and sound */
static int gm_hits_play(track_t *t)
{
    static const uint8_t N[3] = {36, 38, 42}, L[3] = {DV_KICK, DV_SNARE, DV_HATC};
    int32_t o[2u * CTL];
    uint32_t k, i, b, ok = 1, m = t->p[P_MUTE], mq = song.master_q12;
    t->p[P_MUTE] = 0;
    song.master_q12 = 4096;                       /* (the MASTER knob: main.c; 0 in this test) */
    for (b = 0; b < 256u && (t->xf_on || t->eng_req != t->engine); b++)   /* the engine switch done (its fade) */
        mix_block(o, CTL);
    for (k = 0; k < 3u; k++) {
        uint64_t e = 0;
        int lane = 0;
        trk_note_on(t, N[k], 100);
        for (b = 0; b < 16u; b++) {
            memset(o, 0, sizeof o);
            mix_block(o, CTL);
            for (i = 0; i < 2u * CTL; i++)
                e += (uint64_t)(o[i] < 0 ? -o[i] : o[i]);
            for (i = 0; i < NVOICE; i++)
                lane |= t->v[i].active && t->v[i].note == N[k] && (uint32_t)t->v[i].s[0] == L[k];
        }
        trk_note_off(t, N[k]);
        ok &= ENGINES[t->engine] == &ENG_DRUM && drum_lane(N[k]) == L[k] && lane && e > 0;
    }
    t->p[P_MUTE] = (int16_t)m;
    song.master_q12 = mq;
    return (int)ok;
}

/* sounds never touch the steps; the arp, the scale, the SLICER and the pattern parameters are the track's */
static int test_sound_loads(void)
{
    int bad = 0;
    uint32_t i, empty = 1;
    track_t *t = &trk[0], before;
    ui_power_on();
    for (i = 0; i < NTRK; i++)
        empty &= (uint32_t)seq_is_empty(&trk[i]);
    bad += check("power-on: the four sounds, every sequencer empty, no undo copy", empty && undo.trk == 0 && undo_depth == 0 &&
                 trk[3].eng_req == ENGI_DRUM && trk[3].preset == 0u && trk[0].preset == TRK_DEF[0][1]);
    my_steps(t);
    t->p[P_E0 + 1] = 77;                          /* a sound edit */
    t->p[P_AMODE] = 2;                            /* and the track's own settings */
    t->p[P_AOCT] = 3;
    t->p[P_SCALE] = 2;
    t->p[P_TRANS] = 5;
    t->p[P_SLCR] = SL_GATE;
    t->p[P_SLPAT] = 4;
    t->p[P_SDIV] = 1;
    t->p[P_LEVEL] = 90;
    before = *t;
    turn(EN_PRESET, 1);                           /* HOME: the next preset */
    bad += check("PRESETS on HOME loads the sound: steps, LEN, DIV untouched", t->preset == (uint8_t)((before.preset + 1u) % ENGINES[0]->npresets) &&
                 !memcmp(t->step, before.step, sizeof t->step) && t->p[P_SLEN] == 32 && t->p[P_SDIV] == 1);
    bad += check("..ARP, SCL, the SLICER and the mix stay the track's", t->p[P_AMODE] == 2 && t->p[P_AOCT] == 3 && t->p[P_SCALE] == 2 &&
                 t->p[P_TRANS] == 5 && t->p[P_SLCR] == SL_GATE && t->p[P_SLPAT] == 4 && t->p[P_LEVEL] == 90);
    bad += check("..and no warning about the sequence", !msg_is("T1 SEQ REPLACED"));
    for (i = 0; i < ENGINES[0]->npresets; i++)
        if (str_eq(ENGINES[0]->presets[i].name, "RAVE"))
            break;
    t->p[P_AMODE] = 0;
    t->preset = (uint8_t)(i - 1u);
    turn(EN_PRESET, 1);
    bad += check("RAVE (an ARP preset) leaves the arp off; it suggests pattern 13 ARP", t->preset == i && t->p[P_AMODE] == 0 &&
                 preset_pat_hint() == 12 && ui.ppick == 12 && str_eq(PATTERNS[12].name, "ARP"));
    t->p[P_AMODE] = 2;
    before = *t;
    for (i = 0; i < 6u; i++)                      /* several loads, into the next engine */
        turn(EN_PRESET, 1);
    bad += check("browsing on, into another engine: the steps still untouched", t->eng_req != before.eng_req &&
                 !memcmp(t->step, before.step, sizeof t->step) && t->p[P_AMODE] == 2);
    bad += check("browsing keeps the copy from before the first load", undo.trk == 1u && undo.what == UNDO_SOUND &&
                 undo.p[P_E0 + 1] == before.p[P_E0 + 1] && undo.eng == before.eng_req && undo.preset == before.preset);
    step_set_note(&t->step[0], 0, 99);                      /* recorded after the loads */
    hold(B_SAVE);
    bad += check("SAVE held: the sound back (UNDO/REDO T1); steps recorded since stay", same_sound(t, &before) &&
                 step_note(&t->step[0], 0) == 99 && msg_is("UNDO/REDO T1"));
    bad += check("SAVE held does not open the SAVE pages", ui.home);
    hold(B_SAVE);
    bad += check("SAVE held again: the loads again (redo)", t->eng_req != before.eng_req && step_note(&t->step[0], 0) == 99);
    hold(B_SAVE);
    t->p[P_LEVEL] = 50;                           /* a mix change after the undo */
    turn(EN_PRESET, 1);                           /* a load after an undo copies the track as it is now */
    hold(B_SAVE);
    bad += check("a load after the undo: its own copy; the mix (LEVEL) is never swapped", same_sound(t, &before) && t->p[P_LEVEL] == 50);
    press(B_SAVE);
    bad += check("SAVE tap opens the SAVE pages (on release)", !ui.home && cur_page()->fam == FAM_SAVE);
    /* CLEAR INIT / set_engine, user preset load: the sound only, undoable; one copy for all tracks */
    ui_power_on();
    my_steps(&trk[1]);
    trk[1].p[P_SLCR] = SL_STUT;
    before = trk[1];
    track_select(1);
    set_engine(trk[1].eng_req);                   /* same as CLEAR > SND */
    bad += check("INIT sound: the sound only (steps and SLICER kept), a copy of T2", undo.trk == 2u &&
                 !memcmp(trk[1].step, before.step, sizeof before.step) && trk[1].p[P_SLCR] == SL_STUT);
    track_select(0);
    my_steps(&trk[0]);
    trk[0].p[P_SDIV] = 2;
    up_store(3, "MINE");
    bad += check("a user preset keeps its pattern in the record (format unchanged)", up_has_pat(3) && up_rec(3)->note[0] == 40);
    trk[0].p[P_SDIV] = 0;
    track_defaults_steps(&trk[0]);
    step_set_note(&trk[0].step[5], 0, 33);
    trk[0].step[5].n = 1;
    before = trk[0];
    up_load(3);
    bad += check("a user preset load: the sound only (steps, DIV kept); the copy is now T1's", trk[0].user == 4u && undo.trk == 1u &&
                 !memcmp(trk[0].step, before.step, sizeof before.step) && trk[0].p[P_SDIV] == 0);
    undo_swap();
    bad += check("undo of a user preset load", trk[0].user == 0 && !memcmp(trk[0].step, before.step, sizeof before.step));
    /* project load: no copy, and the old one is gone */
    project_save(1);
    up_load(3);
    project_load(1);
    bad += check("a project load drops the copy and takes none", undo.trk == 0 && undo_depth == 0);
    ui_power_on();
    {   /* TRANH (SET 1, preset 1) is gone: both are PIANO aliases, kept for old data, never offered */
        const param_desc_t *sd = &ENGINES[4]->edit[0];
        uint32_t all, pos, k, e, pos0, shown = 0;
        set_engine_of(TSEL, 4);
        pos0 = preset_all_pos(&all);
        TSEL->preset = 1;
        TSEL->p[P_E0] = 1;
        pos = preset_all_pos(&all);
        for (k = 0; k < all; k++)
            if (preset_all_at(k, &e) == 4u)
                shown++;
        e = preset_all_at(pos0 + 1u, &k);
        apply_preset_to(TSEL, 1);
        bad += check("SAMPLE: old SET 1 / preset 1 (TRANH) play PIANO; browsing and knobs skip them",
                     SMP_SETS[1].z0 == SMP_SETS[0].z0 && SMP_SETS[1].nz == SMP_SETS[0].nz && pos == pos0 &&
                     shown == 4u && e == 4u && k == 2u && TSEL->preset == 0u && TSEL->p[P_E0] == 0 &&
                     str_eq(sd->names[1], "PIANO") && enum_step(sd, 0, 1) == 2 && enum_step(sd, 2, 1) == 0 &&
                     enum_step(sd, 1, 2) == 2 && enum_orig(sd, 1) == 0 && enum_orig(sd, 5) == 5 &&
                     enum_orig(&ENGINES[8]->edit[0], 1) == 0);
        bad += check("SAMPLE / GRAIN SET 4 (PERC, retired): a PIANO alias, knobs skip it, USR1..3 stay 5..7",
                     str_eq(sd->names[SMP_SET_PERC], "PIANO") && enum_orig(sd, SMP_SET_PERC) == 0 &&
                     enum_step(sd, 3, 4) == 5 && enum_step(sd, 5, 4) == 3 && str_eq(sd->names[5], "USR1") &&
                     str_eq(sd->names[7], "USR3") && str_eq(sd->names[8], "CR78") && sd->max == 8 &&
                     enum_orig(&ENGINES[8]->edit[0], SMP_SET_PERC) == 0 && SMP_SETS[SMP_SET_PERC].z0 == SMP_SETS[0].z0);
        set_engine_of(TSEL, ENGI_SAMPLE);
        TSEL->p[P_E0] = SMP_CR78_INDEX;
        bad += check("CR78: SET 8 is a factory drum kit (SEQ grid)", drum_track(TSEL) &&
                     str_eq(SMP_ALL_NAMES[SMP_CR78_INDEX], "CR78"));
        apply_preset_to(TSEL, 5);
        bad += check("SAMPLE preset 5 (CR78) selects the factory kit", TSEL->preset == 5u &&
                     TSEL->p[P_E0] == SMP_CR78_INDEX);
        set_engine_of(TSEL, ENGI_SLICE);
        bad += check("SLICE retired: set_engine lands on DRUM", TSEL->eng_req == ENGI_DRUM);
        bad += check("SLICE not eng_ok / SAMPLE GRAIN NOISE live",
                     !eng_ok(ENGI_SLICE) && eng_ok(ENGI_SAMPLE) && eng_ok(ENGI_GRAIN) && eng_ok(ENGI_NOISE) &&
                     str_eq(ENGINES[ENGI_SAMPLE]->name, "SAMPLE") && str_eq(ENGINES[ENGI_SLICE]->name, "-"));
    }
    host_legacy_sample_perc(t);                /* SAMPLE PERC (SET 4, retired after 1.0.2): every load gives DRUM */
    t->preset = 4;                             /* a project written before the factory removal */
    my_steps(t);
    t->p[P_LEVEL] = 71;
    t->p[P_REV] = 33;
    before = *t;
    project_save(0);
    apply_preset_to(t, 0);
    track_defaults_steps(t);
    project_load(0);
    bad += check("saved SAMPLE PERC project: a DRUM track (its kit), the rest of the sound and every step kept",
                 t->eng_req == ENGI_DRUM && t->preset == 0u && perc_kit(t) &&
                 !memcmp(t->p, before.p, P_E0 * sizeof t->p[0]) && !memcmp(t->step, before.step, sizeof t->step));
    bad += check("  its GM hits play as drums (kick, snare, closed hat on their lanes)", gm_hits_play(t));
    {   /* a user preset of SAMPLE PERC (UP_PUT from an older editor, or a bank read): DRUM, its kit */
        up_rec_t r;
        int16_t keep[P_COUNT];
        uint32_t k;
        memset(&r, 0, sizeof r);
        r.used = UP_USED; r.ver = UP_VER; r.engine = ENGI_SAMPLE; r.np = P_COUNT;
        memcpy(r.name, "OLD PERC", 8);
        host_legacy_sample_perc(t);
        for (k = 0; k < P_COUNT; k++)
            up_set_value(&r, k, t->p[k]);
        up_migrate(&r);
        *up_rec(7) = r;
        set_engine_of(t, 0);
        memcpy(keep, t->p, sizeof keep);
        song.sel = (uint8_t)(t - trk);
        up_load(7);
        bad += check("SAMPLE PERC user preset: loads as a DRUM track (its kit)", t->eng_req == ENGI_DRUM && perc_kit(t) &&
                     t->p[P_LEVEL] == keep[P_LEVEL]);
        bad += check("  its GM hits play as drums", gm_hits_play(t));
        memset(up_rec(7), 0, sizeof r);
    }
    set_engine_of(t, 0);
    apply_preset_to(t, SMP_SET_PERC);             /* (engine 0: an ANALOG preset 4, not PERC) */
    bad += check("factory preset 4 of another engine stays that engine's", t->eng_req == 0u && t->preset == 4u);
    t->eng_req = ENGI_SAMPLE;                     /* as a stored SAMPLE PERC would arrive before remap */
    apply_preset_to(t, SMP_SET_PERC);
    bad += check("SAMPLE preset 4 (once PERC): DRUM's kit", t->eng_req == ENGI_DRUM && t->preset == 0u && perc_kit(t) &&
                 gm_hits_play(t));
    ui_power_on();
    host_legacy_sample_perc(&trk[3]);
    my_steps(&trk[3]);
    trk[3].p[P_SDIV] = 3;
    before = trk[3];
    project_save(1);
    project_v6_t legacy = {0};
    project_t decoded;
    proj_import(&decoded, &proj_slot[1], sizeof proj_slot[1]);
    legacy.magic = PROJ_MAGIC_V6; legacy.size = sizeof legacy;
    legacy.parts = 0; legacy.phys = PROJ_PHYS;
    memcpy(legacy.g, decoded.g, sizeof legacy.g);
    for (uint32_t ti = 0; ti < NTRK; ti++) {
        memcpy(legacy.t[ti].p, decoded.t[ti].p, 61u * sizeof(int16_t));
        memcpy(legacy.t[ti].p + 61, decoded.t[ti].p + P_E0, 8u * sizeof(int16_t));
        legacy.t[ti].engine = decoded.t[ti].engine; legacy.t[ti].preset = decoded.t[ti].preset;
        for (uint32_t st = 0; st < NSTEP; st++) {
            const step_t *d = &decoded.t[ti].step[st];
            step10_t *o = &legacy.t[ti].step[st];
            for (uint32_t j = 0; j < 4u; j++) o->note[j] = (uint8_t)step_note(d, j);
            o->n = d->n; o->time = d->time; o->flags = (uint8_t)step_fl(d);
            o->vel = d->vel; o->hit = d->hit; o->acc = d->acc;
        }
    }
    legacy.t[3].engine = 0;
    legacy.g[G_DRLVL] = 71; legacy.g[G_DRREV] = 43;
    legacy.g[G_RTYPE] = 10;                               /* (id 24 was the GM drum part's MIDI channel: 10) */
    legacy.sum = proj_hash(&legacy, offsetof(project_v6_t, sum));
    memcpy(&proj_slot[1], &legacy, sizeof legacy);
    project_load(1);
    bad += check("old GM projects (the drum part) load as DRUM's kit, their mix and steps kept",
                 trk[3].eng_req == ENGI_DRUM && trk[3].preset == 0u && perc_kit(&trk[3]) &&
                 trk[3].p[P_LEVEL] == 71 && trk[3].p[P_REV] == 43 && trk[3].p[P_SDIV] == 3 &&
                 !memcmp(trk[3].step, before.step, sizeof trk[3].step));
    bad += check("  their GM hits play as drums", gm_hits_play(&trk[3]));
    bad += check("  its old drum channel (10, in id 24) loads as REVERB TYPE ROOM", song.g[G_RTYPE] == 0);
    printf("ui: undo copy %u bytes\n", (unsigned)sizeof undo);
    return bad;
}

/* SAVE > PHRASES: K1 PAT, OCT+ LOAD; the dialog over the user's steps; undo */
static int test_patterns(void)
{
    int bad = 0;
    track_t *t = &trk[0], before;
    ui_power_on();
    go_page(GR_PATS);
    bad += check("1.2: PHRASES is a SAVE page (SAVE > PHRASES)", cur_page()->fam == FAM_SAVE &&
                 str_eq(cur_page()->title, "PHRASES") && !song.seq_mode);
    before = *t;
    turn(EN_K2, 1);
    bad += check("KNOB 2 loads nothing (the knobs only pick)", seq_is_empty(t) && ui.confirm == CF_NONE);
    press(B_OCTUP);                               /* LOAD 01 ACID into the empty sequencer */
    bad += check("LOAD into an empty sequencer: at once, LOADED 01 ACID", ui.confirm == CF_NONE && steps_are(t, 0) &&
                 msg_is("LOADED 01 ACID"));
    bad += check("..the sound and the track's settings untouched", same_sound(t, &before) && t->p[P_SDIV] == before.p[P_SDIV] &&
                 t->p[P_AMODE] == before.p[P_AMODE]);
    turn(EN_K1, 2);
    press(B_OCTUP);
    bad += check("a loaded pattern, untouched: the next one without asking (03 MELODY)", ui.confirm == CF_NONE && steps_are(t, 2) &&
                 msg_is("LOADED 03 MELODY"));
    hold(B_SAVE);
    bad += check("SAVE held: back past both loads (empty again), the sound untouched", seq_is_empty(t) && same_sound(t, &before) &&
                 t->p[P_SLEN] == before.p[P_SLEN]);
    hold(B_SAVE);                                 /* redo */
    step_set_note(&t->step[3], 0, 70);                      /* an edit: the user's steps now */
    t->step[3].n = 1;
    t->step[3].time = ST_NOTE;
    press(B_OCTUP);
    bad += check("edited steps: the REPLACE T1 SEQUENCE? dialog", ui.confirm == CF_LOAD_PAT && ui.confirm_trk == 0u &&
                 step_note(&t->step[3], 0) == 70);
    press(B_OCTDN);
    bad += check("OCT- keeps them", ui.confirm == CF_NONE && step_note(&t->step[3], 0) == 70);
    my_steps(t);
    before = *t;
    turn(EN_K1, 9);                               /* 12 BEAT */
    press(B_OCTUP);
    press(B_OCTUP);
    bad += check("OCT+ loads (12 BEAT), then HOLD SAVE: UNDO", ui.confirm == CF_NONE && steps_are(t, 11) &&
                 msg_is("LOADED 12 BEAT") && str_eq(ui.msg2, "[SAVE] HOLD TO UNDO"));
    hold(B_SAVE);
    bad += check("SAVE held: the user's steps and LEN back", !memcmp(t->step, before.step, sizeof t->step) && t->p[P_SLEN] == 32);
    /* a user preset's pattern: listed after the factory ones as U06, with its LEN DIV SWING GATE */
    t->p[P_SDIV] = 2;
    t->p[P_SSWING] = 40;
    up_store(5, "UPAT");
    t->p[P_SDIV] = 0;
    t->p[P_SSWING] = 0;
    up_store(6, "NOPAT");
    memset(up_rec(6)->note, 0, sizeof up_rec(6)->note);   /* .. a record with no pattern: not listed */
    bad += check("user presets with a pattern are listed after the factory ones", pat_count() == NPATTERNS + 1u);
    track_defaults_steps(t);
    turn(EN_K1, 20);                              /* (to the end of the list) */
    press(B_OCTUP);
    bad += check("LOAD U06: its 16 steps, LEN 16 (at most), DIV and SWING", msg_is("LOADED U06 UPAT") && step_note(&t->step[0], 0) == 40 &&
                 step_note(&t->step[3], 0) == 43 && !t->step[30].n && t->p[P_SLEN] == 16 && t->p[P_SDIV] == 2 && t->p[P_SSWING] == 40);
    /* a project's steps count as the user's */
    project_save(2);
    project_load(2);
    press(B_OCTUP);
    bad += check("over a project's steps: the dialog", ui.confirm == CF_LOAD_PAT);
    press(B_OCTDN);
    /* the hint: PRESETS page K3 PAT, and a load moves the pick there */
    go_page(GR_BROWSE);
    turn(EN_K1, 1);
    bad += check("PRESETS: a factory preset's suggested pattern becomes the pick", preset_pat_hint() >= 0 &&
                 ui.ppick == (uint8_t)preset_pat_hint());
    return bad;
}

static int test_rec(void)
{
    static const uint8_t FAMS[3] = {FAM_TRK, FAM_SEQ, FAM_ARP};
    int bad = 0;
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        ui_power_on();
        track_select(2);
        if (k == 1u) go_seq_step(); else open_family(FAMS[k]);
        frame();
        press(B_REC);
        bad += check(k == 0 ? "REC on TRACKS: arms the selected track, PLAY starts" :
                     k == 1 ? "#133 REC on SEQ > STEP: arms the selected track for step recording, PLAY does not start" :
                     "REC on ARP: arms the selected track, PLAY starts",
                     song.rec == 4u && (k == 1u ? transport_req == 0u && msg_is("KEYS WRITE STEPS")
                                               : (transport_req == 1u || msg_is("PLAY FREELY"))));
        song.playing = 1;
        transport_req = 0;
        frames(400);
        press(B_REC);
        bad += check("REC again disarms (the transport runs on)", song.rec == 0u && transport_req == 0u);
        frames(400);
        btn_down(B_REC); frames(600);                  /* hold: no map (arming happens on release) */
        bad += check("REC held: no layer map; arming stays as it was",
                     ui.confirm == CF_NONE && song.rec == 0u && !ui.layer && !ui.lock && !transport_req);
        btn_up(B_REC); frame();
        ui.confirm = 0;
    }
    ui_power_on();
    press(B_REC);
    bad += check("REC on HOME records without changing the screen",
                 ui.home && song.rec == 1u && (transport_req == 1u || msg_is("PLAY FREELY")));
    /* STEP page: armed + playing -> the keys record live only */
    ui_power_on();
    open_family(FAM_SEQ);
    bad += check("SEQ opens STEP", str_eq(cur_page()->title, "STEP") && !ui.lock);
    go_seq_step(); frame();
    press(B_REC);
    song.playing = 1;
    transport_req = 0;
    fm1_in.notes = host_notes = 1u << 7;
    frame();
    fm1_in.notes = 0;
    frame();
    bad += check("STEP, armed and playing: a key does not write the cursor step", !trk[0].step[0].n && ui.cursor == 0);
    song.rec = 0;
    fm1_in.notes = host_notes = 1u << 7;
    frame();
    fm1_in.notes = 0;
    frame();
    bad += check("#133 STEP, not armed: the key only plays (nothing written, the cursor stays)",
                 !trk[0].step[0].n && ui.cursor == 0);
    song.rec = 1;
    song.playing = 0;
    fm1_in.notes = host_notes = 1u << 7;
    frame();
    fm1_in.notes = 0;
    frame();
    bad += check("#133 STEP, armed and stopped: the key writes the cursor step (and moves on)",
                 trk[0].step[0].n == 1 && ui.cursor == 1);
    /* ARP: the arp's notes are recorded, not the key */
    ui_power_on();
    {
        track_t *t = &trk[0];
        uint32_t i, n = 0, notes = 0;
        t->engine = t->eng_req;
        t->p[P_AMODE] = 1;
        t->p[P_AOCT] = 2;
        t->p[P_APROB] = 127;
        song.rec = 1;
        song.playing = 1;
        input_on(t, 60, 100);
        for (i = 0; i < NSTEP; i++)
            n += t->step[i].n;
        bad += check("ARP on: the key held is not recorded", n == 0);
        arp_tick(t, 1);
        for (i = 0; i < NSTEP; i++)
            if (t->step[i].n)
                notes = step_note(&t->step[i], 0);
        bad += check("ARP on: the note the arp plays is recorded", notes == 60u);
        t->p[P_AMODE] = 0;
        input_off(t, 60);
    }
    return bad;
}

static uint32_t gated_notes(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++) n += t->v[i].gate != 0;
    return n;
}

static int test_midi(void)
{
    int bad = 0;
    track_t *a, *b;
    ui_power_on();
    song.sel = 2;
    midi_hint = 0;
    midi_event(0x90, 0, 60, 100); midi_event(0x90, 9, 61, 100);
    bad += check("ROUT CH1-4 (default): channel 1 -> part 1, channel 10 ignored",
                 song.g[G_ROUTE] == 0 && midi_sel_on[0][60] == 1u && !midi_sel_on[9][61] && !gated_notes(&trk[2]));
    bad += check("a note into another track tells the UI", midi_hint == 1u);
    frame(); bad += check("MIDI IN -> T1 shown", msg_is("MIDI IN -> T1"));
    song.g[G_ROUTE] = 1;
    midi_event(0x80, 0, 60, 0); midi_event(0x90, 0, 62, 100);
    bad += check("ROUT changes: note-off releases original owner, new note uses selection",
                 !gated_notes(&trk[0]) && midi_sel_on[0][62] == 3u && midi_track(3) == &trk[2]);
    song.sel = 1; midi_event(0x80, 0, 62, 0);
    bad += check("note-off after selection follows the note-on's track", !midi_sel_on[0][62]);
    midi_event(0x80, 9, 61, 0);
    ui_power_on();
    song.g[G_ROUTE] = 1;
    midi_event(0x90, 0, 60, 100);
    song.sel = 1;
    midi_event(0x90, 0, 60, 100);
    bad += check("same MIDI channel/pitch on a new selected track releases its old owner",
                 !gated_notes(&trk[0]) && gated_notes(&trk[1]) && midi_sel_on[0][60] == 2u);
    midi_event(0x80, 0, 60, 0);
    midi_event(0x80, 0, 60, 0);
    bad += check("both note-offs after a MIDI ownership change leave neither track held",
                 !gated_notes(&trk[0]) && !gated_notes(&trk[1]) && !midi_sel_on[0][60]);
    ui_power_on();
    song.sel = 2; song.g[G_ROUTE] = 1;
    midi_event(0x90, 0, 61, 100);
    song.g[G_ROUTE] = 0;
    midi_event(0x90, 0, 61, 100);
    midi_event(0x80, 0, 61, 0);
    bad += check("same channel/pitch across a ROUT change cannot leave its previous part held",
                 !gated_notes(&trk[2]) && !gated_notes(&trk[0]) && !midi_sel_on[0][61]);
    {
        uint32_t hold;
        for (hold = 0; hold < 2u; hold++) {
            ui_power_on();
            song.g[G_ROUTE] = 1;
            trk[0].p[P_AMODE] = 1; trk[0].p[P_AHOLD] = (int16_t)hold;
            midi_event(0x90, 0, 60, 100);
            song.sel = 1;
            midi_event(0x90, 0, 60, 100);
            bad += check("MIDI ownership transfer releases old ARP keys and respects explicit HOLD",
                         !trk[0].arp_phys && trk[0].nheld == hold && midi_sel_on[0][60] == 2u);
        }
    }
    ui_power_on();
    trk[1].p[P_AMODE] = trk[1].p[P_AHOLD] = 1;
    midi_event(0x90, 0, 60, 100);
    midi_event(0x90, 1, 64, 100);
    song.playing = 1;
    {
        uint32_t i, held = 0;
        for (i = 0; i < MQ; i++) midi_in_event(0x643C9909u);
        midi_in_event(0x003C8908u);                      /* note-off lost to a full ring */
        events_block(CTL);
        for (i = 0; i < NTRK * NVOICE; i++) held += trk[i / NVOICE].v[i % NVOICE].gate;
        bad += check("MIDI overflow releases held notes and latched ARP, keeps transport running",
                     !held && !trk[1].nheld && !trk[1].arp_phys && !midi_sel_on[0][60] &&
                     !midi_sel_on[1][64] && !midi_in_overflow && mi_r == mi_w && song.playing);
        midi_in_event(0x643D9009u);
        events_block(CTL);
        bad += check("MIDI accepts a fresh note after overflow recovery", midi_sel_on[0][61] == 1u && trk[0].v[0].gate);
    }
    {   /* 1.2: MENU > MIDI > MIDI IN (up to 1.1.5 GLO > SYSTEM K3 ROUT): the project's G_ROUTE, its names ROUT's */
        uint32_t k;
        int16_t r0 = song.g[G_ROUTE];
        int ok = MI_TAB[MI_MIDIIN] == MTAB_MIDI && str_eq(MI_NAME[MI_MIDIIN], "MIDI IN") &&
                 menu_n(MI_MIDIIN) == (uint32_t)GP[G_ROUTE].max + 1u;
        for (k = 0; k < menu_n(MI_MIDIIN); k++) {
            menu_put(MI_MIDIIN, k);
            ok &= song.g[G_ROUTE] == (int16_t)k && menu_get(MI_MIDIIN) == k && str_eq(menu_vname(MI_MIDIIN, k), GP[G_ROUTE].names[k]);
        }
        menu_put(MI_MIDIIN, 1u);
        ok &= str_eq(menu_vname(MI_MIDIIN, menu_get(MI_MIDIIN)), "SEL");
        song.g[G_ROUTE] = r0;
        bad += check("MENU > MIDI > MIDI IN: CH1-4 / SEL / CH5-8 .. CH13-16, the project's ROUT (G_ROUTE)", ok);
    }
    {   /* #192: PRESETS loads into the selected track for good (no preview): MIDI IN SEL plays it on any channel;
         * CH1-4 (the default) keeps channel 1 on track 1 whatever is selected, so browsing on T3 is not heard there */
        uint8_t e1 = trk[0].eng_req, p1 = trk[0].preset, e3, p3;
        int ok;
        ui_power_on();
        song.sel = 2;
        go_page(GR_BROWSE);
        e3 = TSEL->eng_req; p3 = TSEL->preset;
        e1 = trk[0].eng_req; p1 = trk[0].preset;
        turn(EN_PRESET, 1);
        turn(EN_PRESET, 1);
        ok = (TSEL->eng_req != e3 || TSEL->preset != p3) && trk[0].eng_req == e1 && trk[0].preset == p1;
        song.g[G_ROUTE] = 1;                             /* SEL */
        midi_event(0x90, 0, 60, 100);
        ok &= gated_notes(&trk[2]) == 1u && !gated_notes(&trk[0]) && midi_sel_on[0][60] == 3u;
        midi_event(0x80, 0, 60, 0);
        song.g[G_ROUTE] = 0;                             /* CH1-4 */
        midi_event(0x90, 0, 60, 100);
        ok &= gated_notes(&trk[0]) == 1u && !gated_notes(&trk[2]) && midi_sel_on[0][60] == 1u;
        midi_event(0x80, 0, 60, 0);
        midi_event(0x90, 2, 62, 100);                    /* (CH1-4: channel 3 is T3's) */
        ok &= gated_notes(&trk[2]) == 1u && midi_sel_on[2][62] == 3u;
        midi_event(0x80, 2, 62, 0);
        bad += check("#192 PRESETS on T3: MIDI IN SEL plays the loaded sound, CH1-4 channel 1 stays T1 (channel 3 T3)", ok);
    }
    return bad;
}

static int test_save(void)
{
    int bad = 0;
    uint32_t i;
    ui_power_on();
    trk[0].p[P_E0] = 5;
    press(B_SAVE);
    bad += check("SAVE opens PRESETS",
                 !ui.home && cur_page()->graph == GR_BROWSE && !up_used(0));
    turn(EN_SELECT, 1);
    bad += check("SELECT: USER (Presets section)", cur_page()->graph == GR_USER && ui.act == 4u);
    press(B_OCTUP);
    bad += check("USER then OCT+ opens NAME (the automatic name), writes nothing yet",
                 name_on() && nm.kind == NK_USER_SAVE && str_eq(nm.s, nm.ph) && nm.cur == nm.len && !up_used(0));
    press(B_OCTUP);
    {
        char nmb[16], au[16];
        up_name(0, nmb);
        up_auto_name(au, trk[0].eng_req, 0);
        bad += check("..OCT+ again stores the edited sound with that name (USER, OCT+, OCT+)",
                     !name_on() && up_used(0) && up_value(up_rec(0), P_E0) == 5 && ui.act == 0u && str_eq(nmb, au));
    }
    turn(EN_SELECT, 1);
    bad += check("SELECT: PHRASES (patterns, still Presets section)", cur_page()->graph == GR_PATS);
    press(B_SAVE);
    bad += check("SAVE button: Presets → Project section", cur_page()->graph == GR_SLOTS);
    turn(EN_SELECT, 1);
    bad += check("SELECT stops on PROJECT (one page in Project)", cur_page()->graph == GR_SLOTS);
    press(B_SAVE);
    bad += check("SAVE button: Project → Clear section", cur_page()->graph == GR_CLEAR && ui.lock == LAYER_CLR);
    go_home();
    press(B_SAVE);
    bad += check("SAVE from HOME returns directly to PRESETS, even after visiting PROJECT",
                 cur_page()->graph == GR_BROWSE);
    turn(EN_SELECT, 1);
    press(B_OCTUP);
    bad += check("USER still asks before overwriting an occupied slot",
                 ui.confirm == CF_OVR_USER && up_value(up_rec(0), P_E0) == 5);
    press(B_OCTDN);
    ui_power_on();
    song.playing = 1;
    project_save(0);
    bad += check("project save while playing: refused, STOP TO SAVE", !project_used(0) && msg_is("STOP TO SAVE"));
    bad += check("user preset save while playing: refused", up_store(0, "X") == 2 && !up_used(0));
    song.playing = 0;
    transport_req = 1;
    project_save(0);
    bad += check("a pending PLAY cannot save a project", !project_used(0) && msg_is("STOP TO SAVE"));
    bad += check("a pending PLAY cannot save a user preset", up_store(0, "X") == 2 && !up_used(0));
    transport_req = 0;
    chain.armed = 1;
    bad += check("a pending SONG cannot save a user preset", up_store(0, "X") == 2 && !up_used(0));
    chain.armed = 0;
    song.playing = 1;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == GR_SLOTS)
            break;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
    ui.msg_t = 0;
    turn(EN_K4, 1);                               /* SAVE picked */
    bad += check("PROJECT KNOB 4 picks SAVE, saves nothing", ui.act == 4u && !project_used(0) && !ui.msg_t);
    press(B_OCTUP);
    bad += check("OCT+ while playing: STOP TO SAVE, no NAME", msg_is("STOP TO SAVE") && !project_used(0) &&
                 song.g[G_SAVE] == 0 && !name_on());
    stop_transport();
    turn(EN_K4, 1);
    press(B_OCTUP);
    bad += check("stopped, an empty slot: OCT+ opens NAME (no name yet: PROJECT A)", name_on() && nm.kind == NK_PROJ_SAVE &&
                 nm.len == 0u && str_eq(nm.ph, "PROJECT A") && !project_used(0));
    press(B_OCTUP);
    bad += check("..OCT+ saves it unnamed, SAVE dropped", !name_on() && project_used(0) && msg_is("SAVED (RAM)") &&
                 ui.act == 0u);
    turn(EN_K4, 1);
    press(B_OCTUP);
    bad += check("a used slot: the OVERWRITE? dialog", ui.confirm == CF_OVR_PROJ && ui.confirm_trk == 0);
    trk[0].p[P_E0] = 5;
    press(B_OCTDN);
    bad += check("OCT- keeps the slot", ui.confirm == CF_NONE && stored_param(0, 0, P_E0) != 5);
    turn(EN_K4, 1);
    press(B_OCTUP);
    press(B_OCTUP);
    bad += check("OCT+ (YES): NAME, the slot not written yet", name_on() && stored_param(0, 0, P_E0) != 5);
    press(B_OCTUP);
    bad += check("OCT+ overwrites it", ui.confirm == CF_NONE && !name_on() && stored_param(0, 0, P_E0) == 5);
    trk[0].p[P_LEVEL] = 11;
    turn(EN_K3, 1);
    press(B_OCTUP);
    bad += check("KNOB 3 LOAD, OCT+: the project back; LOAD stays picked", trk[0].p[P_LEVEL] != 11 &&
                 trk[0].p[P_LEVEL] == stored_param(0, 0, P_LEVEL) && msg_is("LOADED") && ui.act == 3u);
    /* user presets */
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == GR_USER)
            break;
    ui.page = (uint8_t)i;
    page_entered();
    up_store(4, "OLD");
    ui.uslot = 4;
    turn(EN_K4, 1);
    press(B_OCTUP);
    bad += check("USER SAVE over a used slot: the OVERWRITE? dialog", ui.confirm == CF_OVR_USER && ui.confirm_trk == 4u);
    press(B_OCTUP);
    bad += check("YES: NAME with the sound's name (not the old slot's)", name_on() && !str_eq(nm.s, "OLD"));
    press(B_OCTUP);
    {
        char nm[16];
        up_name(4, nm);
        bad += check("OCT+ stores the sound there", ui.confirm == CF_NONE && !str_eq(nm, "OLD"));
    }
    song.playing = 1;
    ui.msg_t = 0;
    turn(EN_K3, 1);                               /* ERASE */
    press(B_OCTUP);
    bad += check("USER ERASE while playing: STOP TO SAVE", msg_is("STOP TO SAVE") && up_used(4));
    stop_transport();
    return bad;
}

static void go_title(const char *title)
{
    uint32_t i;
    for (i = 0; i < NPAGES && !str_eq(PAGES[i].title, title); i++)
        ;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
}
static uint32_t oct_leds_seen(int all)            /* over 1 s: the OCT LED bits lit at some point (all: throughout) */
{
    uint32_t k, any = 0, every = 3u | OCT_BREATH;
    for (k = 0; k < 64u; k++) {
        frame();
        any |= oct_leds();
        every &= oct_leds();
    }
    return all ? every : any;
}

/* the action pages: the knobs pick, OCT+ does it, OCT- cancels / goes HOME; the LEDs; chords */
static int test_actions(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    ui_power_on();
    press(B_OCTUP);
    bad += check("HOME: OCT+ shifts the octave (lit)", song.octave == 1 && oct_leds() == 2u);
    go_page(GR_PATS);
    bad += check("PATTERNS: OCT+ breathes (a pattern to load), OCT- lit", oct_leds_seen(0) == (1u | OCT_BREATH) &&
                 oct_leds_seen(1) == (1u | OCT_BREATH));
    press(B_OCTUP);
    bad += check("..OCT+ loads it, the octave stays", steps_are(t, 0) && song.octave == 1 && !ui.home);
    bad += check("..the same pattern again would change nothing: OCT+ dark", oct_leds_seen(0) == 1u);
    turn(EN_K1, 1);
    bad += check("..another one picked: OCT+ breathes again", oct_leds_seen(0) == (1u | OCT_BREATH));
    {
        char a[16], b[16];
        foot_hint(a, b);
        bad += check("..the footer: OCT+ LOAD / OCT- BACK", str_eq(a, "OCT+ LOAD") && str_eq(b, "OCT- BACK"));
    }
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP];   /* both together (the UPDATE MODE hold): nothing */
    host_pressed |= 1u << panel.btn[B_OCTUP];
    frame();
    fm1_in.buttons |= 1u << panel.btn[B_OCTDN];
    host_pressed |= 1u << panel.btn[B_OCTDN];
    frame();
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]);
    frame();
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTDN]);
    frame();
    bad += check("OCT- + OCT+ together: no load, no HOME, no octave change", steps_are(t, 0) && !ui.home && song.octave == 1);
    go_home();
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP];   /* pressed on HOME, let go on PATTERNS: no load */
    host_pressed |= 1u << panel.btn[B_OCTUP];
    frame();
    go_page(GR_PATS);
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]);
    frame();
    bad += check("a press that began elsewhere does not load", steps_are(t, 0) && song.octave == 2);
    press(B_OCTDN);
    bad += check("PATTERNS: OCT- goes HOME (the octave stays)", ui.home && song.octave == 2);
    /* SAVE > CLEAR: sticky key map (Punch-in style); key opens confirm */
    my_steps(t);
    go_title("CLEAR"); frame();
    bad += check("CLEAR: sticky map, not an action page", ui.lock == LAYER_CLR && ui.layer == LAYER_CLR &&
                 act_cols() == 0u && !seq_is_empty(t));
    key_down(white(0)); frame(); key_up(white(0)); frame();   /* F3 PAT */
    bad += check("CLEAR PAT key first asks confirmation", ui.confirm == CF_CLEAR_SEQ && !seq_is_empty(t));
    press(B_OCTUP);
    bad += check("OCT+ clears the pattern; map stays", seq_is_empty(t) && msg_is("PATTERN CLEARED") &&
                 ui.lock == LAYER_CLR);
    frame();                                          /* kb_lock back after the dialog */
    key_down(white(0)); frame(); key_up(white(0)); frame();
    bad += check("nothing to clear: NOTHING TO CLEAR", !ui.confirm && msg_is("NOTHING TO CLEAR"));
    t->p[P_E0] = (int16_t)(t->p[P_E0] + 1);
    key_down(white(1)); frame(); key_up(white(1)); frame();   /* G3 SND (TRACK col) */
    bad += check("CLEAR SND key first asks confirmation", ui.confirm == CF_INIT_SOUND);
    press(B_OCTUP);
    bad += check("confirmed INIT: SOUND INIT; map stays", msg_is("SOUND INIT") && ui.lock == LAYER_CLR);
    my_steps(&trk[1]);
    frame();
    key_down(white(4)); frame(); key_up(white(4)); frame();   /* C4 PATS (ALL col) */
    bad += check("CLEAR PATS asks for every track", ui.confirm == CF_CLEAR_ALL_SEQ);
    press(B_OCTUP);
    bad += check("confirmed PATS: every track empty", seq_is_empty(&trk[0]) && seq_is_empty(&trk[1]) &&
                 msg_is("ALL PATTERNS CLEARED"));
    frame();
    key_down(white(10)); frame(); key_up(white(10)); frame();   /* B4: unused */
    bad += check("CLEAR B4 does nothing", !ui.confirm && ui.lock == LAYER_CLR);
    press(B_HOME);
    bad += check("HOME leaves Clear", ui.home && !ui.lock);
    /* the dialogs and the menu */
    open_family(FAM_SEQ);
    my_steps(t);
    confirm_open(CF_CLEAR_SEQ, song.sel);
    frame();
    bad += check("a dialog: OCT+ breathes, OCT- lit", ui.confirm == CF_CLEAR_SEQ && oct_leds_seen(0) == (1u | OCT_BREATH) &&
                 oct_leds_seen(1) == (1u | OCT_BREATH));
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP];
    host_pressed |= 1u << panel.btn[B_OCTUP];
    frame();
    bad += check("..it answers on release, not on the press", ui.confirm == CF_CLEAR_SEQ && !seq_is_empty(t));
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]);
    frame();
    bad += check("..OCT+ let go: cleared", ui.confirm == CF_NONE && seq_is_empty(t));
    bad += check("back on SEQ: the octave LEDs again", oct_leds() != 0u || song.octave == 1);
    return bad;
}

static int test_clear_selected_all(void)
{
    int bad = 0;
    char a[24], b[24];
    ui_power_on();
    song.sel = 1;
    track_t *t = TSEL;
    int16_t base_sound = t->p[P_E0];
    my_steps(t);
    t->p[P_E0] = (int16_t)(base_sound + 1);
    motion_set_lock(t, 0, P_REV, 50);
    my_steps(&trk[0]);
    trk[0].p[P_E0]++;
    motion_set_lock(&trk[0], 0, P_REV, 70);
    chain_config.count = 1;
    chain_config.row[0] = chain_row_of(0, 1);
    uint32_t selected_before = track_sig(t), other_before = track_sig(&trk[0]);
    go_title("CLEAR"); frame();
    key_down(white(3)); frame(); key_up(white(3)); frame();   /* B3 ALL for selected track */
    confirm_text(a, b);
    bad += check("CLEAR B3 ALL asks once for the selected track", ui.confirm && str_eq(a, "CLEAR T2 ALL?"));
    press(B_OCTDN);
    bad += check("cancel selected ALL preserves sound, pattern, automation and song", !ui.confirm &&
                 track_sig(t) == selected_before && track_sig(&trk[0]) == other_before && chain_config.count == 1);
    frame();
    key_down(white(3)); frame(); key_up(white(3)); frame();
    press(B_OCTUP);
    bad += check("selected ALL initializes sound and clears pattern and automation only for T2", !ui.confirm &&
                 seq_is_empty(t) && !motion_count(t) && t->p[P_E0] == base_sound &&
                 track_sig(&trk[0]) == other_before && song.sel == 1 && chain_config.count == 1);
    return bad;
}

static int test_clear_column_keys(void)
{
    int bad = 0;
    ui_power_on();
    motion_set_lock(TSEL, 0, P_REV, 50);
    go_title("CLEAR"); frame();
    key_down(white(2)); frame(); key_up(white(2)); frame();   /* A3 MOT */
    bad += check("CLEAR A3 is selected-track automation", ui.confirm == CF_CLEAR_MOTION);
    press(B_OCTDN); frame();
    key_down(white(5)); frame(); key_up(white(5)); frame();   /* D4 SNDS */
    bad += check("CLEAR D4 is every-track sound", ui.confirm == CF_INIT_ALL_SOUND);
    press(B_OCTDN); frame();
    key_down(white(6)); frame(); key_up(white(6)); frame();   /* E4 MOTS */
    bad += check("CLEAR E4 is every-track automation", ui.confirm == CF_CLEAR_ALL_MOTION);
    press(B_OCTDN);
    return bad;
}

static int test_clear_every_track_all(void)
{
    int bad = 0, ok = 1;
    char a[24], b[24];
    int16_t base_sound[NTRK];
    uint32_t before[NTRK], i;
    ui_power_on();
    song.sel = 2;
    for (i = 0; i < NTRK; i++) {
        base_sound[i] = trk[i].p[P_E0];
        my_steps(&trk[i]);
        trk[i].p[P_E0]++;
        motion_set_lock(&trk[i], 0, P_REV, 50);
        before[i] = track_sig(&trk[i]);
    }
    chain_config.count = 1;
    chain_config.row[0] = chain_row_of(0, 1);
    go_title("CLEAR"); frame();
    key_down(white(7)); frame(); key_up(white(7)); frame();   /* F4 ALL for every track */
    confirm_text(a, b);
    bad += check("CLEAR F4 ALL asks once for every track", ui.confirm && str_eq(a, "CLEAR ALL TRACKS?"));
    press(B_OCTDN);
    for (i = 0; i < NTRK; i++)
        ok &= track_sig(&trk[i]) == before[i];
    bad += check("cancel every-track ALL preserves sound, patterns, automation and song", ok && !ui.confirm &&
                 song.sel == 2 && chain_config.count == 1);
    frame();
    key_down(white(7)); frame(); key_up(white(7)); frame();
    press(B_OCTUP);
    for (ok = 1, i = 0; i < NTRK; i++)
        ok &= seq_is_empty(&trk[i]) && !motion_count(&trk[i]) && trk[i].p[P_E0] == base_sound[i];
    bad += check("every-track ALL initializes all sounds and clears all patterns and automation, keeps song", ok &&
                 !ui.confirm && song.sel == 2 && chain_config.count == 1);
    return bad;
}

static int test_tracks(void)
{
    int bad = 0;
    track_t before;
    ui_power_on();
    open_family(FAM_TRK);
    before = trk[0];
    turn(EN_PRESET, 1);
    bad += check("#94 MIXER: the PRESETS knob loads the next sound; the steps and the mix stay",
                 trk[0].preset != before.preset && !memcmp(trk[0].step, before.step, sizeof before.step) &&
                 trk[0].p[P_LEVEL] == before.p[P_LEVEL] && cur_page()->graph == GR_TRK);
    hold(B_SAVE);
    bad += check("#94 MIXER: SAVE held undoes it", trk[0].preset == before.preset && !memcmp(trk[0].p, before.p, sizeof before.p));
    turn(EN_K4, -1);
    bad += check("MIXER KNOB 4 left: PLAY OFF (the selected track stays)", trk[0].p[P_MUTE] == PLAY_OFF && song.sel == 0);
    turn(EN_K1, -3);
    bad += check("KNOB 1 changes LEVEL only (PLAY stays)", trk[0].p[P_MUTE] == PLAY_OFF && trk[0].p[P_LEVEL] == before.p[P_LEVEL] - 3);
    turn(EN_K2, 2);
    bad += check("KNOB 2 changes PAN", trk[0].p[P_PAN] == before.p[P_PAN] + 2);
    turn(EN_K4, 1);
    bad += check("MIXER KNOB 4 right from OFF: PLAY ON", trk[0].p[P_MUTE] == PLAY_ON);
    turn(EN_K4, 1);
    bad += check("MIXER KNOB 4 right: PLAY SOLO", trk[0].p[P_MUTE] == PLAY_SOLO && song.sel == 0);
    turn(EN_K4, 1);
    bad += check("MIXER KNOB 4 stays on SOLO at the end of the list", trk[0].p[P_MUTE] == PLAY_SOLO);
    turn(EN_K4, -1);
    bad += check("MIXER KNOB 4 left from SOLO: PLAY ON", trk[0].p[P_MUTE] == PLAY_ON);
    turn(EN_K4, -1);
    bad += check("MIXER KNOB 4 left again: PLAY OFF", trk[0].p[P_MUTE] == PLAY_OFF);
    turn(EN_ALGO, 1);
    bad += check("ALGORITHM selects the track", song.sel == 1);
    return bad;
}

/* key k of colour black (1) / white (0) at place p (seq.c key_place) */
static uint32_t key_at(int black, uint32_t p)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (key_black(k) == black && key_place(k) == p)
            return k;
    return 0;
}
static void tap_key(uint32_t k)                   /* a key down for a frame, then up */
{
    fm1_in.notes |= 1u << k;
    host_notes |= 1u << k;
    frame();
    fm1_in.notes &= ~(1u << k);
    frame();
}
static uint32_t lane_steps(const track_t *t, uint32_t l)   /* bit i: lane l strikes at step i (0..31) */
{
    uint32_t i, m = 0;
    for (i = 0; i < 32u; i++)
        m |= ((step_lanes(&t->step[i]) >> l) & 1u) << i;
    return m;
}

static int test_grid(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    uint32_t i, k, leds, ok;
    ui_power_on();
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    go_seq_step();
    frame();
    bad += check("SEQ > STEP on a DRUM track is the grid (keys to the UI), GRID in the footer", grid_on() && song.grid == 1u &&
                 cur_page()->scope == SC_STEP);
    bad += check("the lanes: KICK SNARE CLAP HATCL HATOP TOM RIM BELL on GM 36 38 39 42 46 45 37 56",
                 str_eq(drum_lane_name(t, 0), "KICK") && str_eq(drum_lane_name(t, 7), "BELL") && DRUM_LANE_NOTE[3] == 42 &&
                 DRUM_LANE_NOTE[6] == 37 && drum_lane(41) == DV_TOM && drum_lane(49) == DV_BELL && drum_lane(35) == DV_KICK);
    tap_key(key_at(1, 1));
    bad += check("black key 2: lane 2 (SNARE) selected", ui.lane == 1u);
    tap_key(key_at(0, 4));
    bad += check("white key 5: SNARE on step 5, the cursor there", t->step[4].hit == 1u << DV_SNARE && t->step[4].time == ST_NOTE &&
                 !t->step[4].n && ui.cursor == 4u);
    leds = grid_leds();
    bad += check("LEDs: white key 5 lit, black key 2 lit, ACC and the page keys dark (LEN 16)",
                 (leds >> key_at(0, 4) & 1u) && (leds >> key_at(1, 1) & 1u) && !(leds >> key_at(0, 3) & 1u) &&
                 !(leds >> key_at(1, 0) & 1u) && !(leds >> key_at(1, GK_ACC) & 1u) && !(leds >> key_at(1, GK_PGUP) & 1u));
    song.playing = 1;
    t->seq_idx = 4;
    leds = grid_leds();
    ok = !(leds >> key_at(0, 4) & 1u);
    t->seq_idx = 6;
    leds = grid_leds();
    ok &= (leds >> key_at(0, 6) & 1u) && !(leds >> key_at(0, 4) & 1u) == 0u;
    song.playing = 0;
    bad += check("LEDs: the step playing inverted (a hit goes dark, an empty step lights)", ok);
    tap_key(key_at(0, 4));
    bad += check("white key 5 again: off, an empty step (REST)", !t->step[4].hit && t->step[4].time == ST_REST);
    /* ACC held: the white keys set accents, their LEDs show them */
    fm1_in.notes |= 1u << key_at(1, GK_ACC);
    frame();
    tap_key(key_at(0, 0));
    fm1_in.notes |= 1u << key_at(1, GK_ACC);
    leds = grid_leds();
    ok = t->step[0].hit == 1u << DV_SNARE && t->step[0].acc == 1u << DV_SNARE && (leds >> key_at(0, 0) & 1u) &&
         (leds >> key_at(1, GK_ACC) & 1u);
    tap_key(key_at(0, 0));
    fm1_in.notes |= 1u << key_at(1, GK_ACC);
    leds = grid_leds();
    ok &= t->step[0].hit == 1u << DV_SNARE && !t->step[0].acc && !(leds >> key_at(0, 0) & 1u);
    fm1_in.notes = 0;
    frame();
    leds = grid_leds();
    ok &= (leds >> key_at(0, 0) & 1u) != 0u;
    bad += check("ACC held: a tap adds the hit accented, again drops the accent only; the LEDs show accents", ok);
    /* the knobs: STEP LANE HIT ACC */
    turn(EN_K1, 2);
    turn(EN_K2, 1);
    turn(EN_K3, 1);
    ok = ui.cursor == 2u && ui.lane == 2u && t->step[2].hit == 1u << DV_CLAP;
    turn(EN_K4, 1);
    ok &= t->step[2].acc == 1u << DV_CLAP;
    turn(EN_K3, -1);
    ok &= !t->step[2].hit && !t->step[2].acc && t->step[2].time == ST_REST;
    turn(EN_K4, 1);
    ok &= t->step[2].hit == 1u << DV_CLAP && t->step[2].acc == 1u << DV_CLAP;
    bad += check("KNOB 1 STEP, 2 LANE, 3 HIT on / off, 4 ACC (an accent adds the hit)", ok);
    press(B_EDIT);
    bad += check("EDIT on the grid opens EDIT; the hit stays (KNOB 3 turns it off)",
                 t->step[2].hit == (1u << DV_CLAP) && ui.cursor == 2u && cur_page()->fam == FAM_EDIT);
    go_seq_step();
    ui.cursor = 2;
    frame();
    /* pages: LEN 32, the page keys */
    t->p[P_SLEN] = 32;
    leds = grid_leds();
    tap_key(key_at(1, GK_PGUP));
    ok = ui.bank == 1u && ui.cursor == 18u && (leds >> key_at(1, GK_PGUP) & 1u) && (leds >> key_at(1, GK_PGDN) & 1u);
    tap_key(key_at(0, 15));
    ok &= t->step[31].hit == 1u << DV_CLAP;
    tap_key(key_at(1, GK_PGDN));
    ok &= ui.bank == 0u && ui.cursor == 15u;
    t->p[P_SLEN] = 12;
    frame();
    tap_key(key_at(0, 13));
    ok &= !t->step[13].hit && !t->step[13].n;
    bad += check("pages: black keys 11 / 10 up / down (the cursor along); past LEN a key does nothing", ok);
    t->p[P_SLEN] = 16;
    /* a step's GM notes on their lanes */
    t->step[8] = (step_t){{41, 49, 36, 0}, 3, ST_NOTE, SF_ACCENT, 96, 0, 0};
    ok = step_lanes(&t->step[8]) == ((1u << DV_TOM) | (1u << DV_BELL) | (1u << DV_KICK)) &&
         step_accents(&t->step[8]) == step_lanes(&t->step[8]);
    grid_hit(t, 8, DV_TOM, 0);
    ok &= t->step[8].n == 1 && step_note(&t->step[8], 0) == 49 && t->step[8].hit == 1u << DV_KICK && (step_fl(&t->step[8]) & SF_ACCENT);
    grid_acc(t, 8, DV_KICK, 0);
    ok &= !t->step[8].acc && !(step_fl(&t->step[8]) & SF_ACCENT) && t->step[8].n == 1 && (step_lanes(&t->step[8]) >> DV_BELL & 1u);
    grid_acc(t, 8, DV_BELL, 1);
    ok &= !t->step[8].n && t->step[8].hit == ((1u << DV_KICK) | (1u << DV_BELL)) && t->step[8].acc == 1u << DV_BELL;
    bad += check("GM notes on the grid: a low tom / crash on their lanes; an edit makes the lane its hit", ok);
    /* the keys in the audio ISR: on the grid only the lane keys play */
    kb_prev = 0;
    fm1_in.notes = (1u << key_at(0, 3)) | (1u << key_at(1, 3)) | (1u << key_at(1, GK_ACC));
    keyboard_block();
    ok = kb_note[key_at(0, 3)] == KB_SILENT && kb_note[key_at(1, 3)] == 42u && kb_note[key_at(1, GK_ACC)] == KB_SILENT;
    fm1_in.notes = 0;
    keyboard_block();
    bad += check("grid keys in the ISR: white keys and ACC silent, black key 4 plays HAT CL (42)", ok);
    /* live recording on the grid: armed and playing, a lane key records its hit at the play head */
    track_defaults_steps(t);
    song.rec = 1;
    song.playing = 1;
    t->seq_idx = 5;
    t->seq_pos = 0;
    fm1_in.notes = 1u << key_at(1, 4);
    keyboard_block();
    fm1_in.notes = 0;
    keyboard_block();
    ok = t->step[5].hit == 1u << DV_HATO && !t->step[5].n && t->rskip_n == 0u;
    t->seq_pos = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), 5) - 10u;   /* late in step 6: into step 7 */
    fm1_in.notes = 1u << key_at(1, 0);
    keyboard_block();
    ok &= t->step[6].hit == 1u << DV_KICK && t->rskip_n == 1u && t->rskip[0] == 36u;
    rec_hold(t, 7, 16);
    fm1_in.notes = 0;
    keyboard_block();
    ok &= t->step[7].time == ST_REST && !t->rh_n;
    bad += check("REC on the grid: lane keys record hits, quantised (the next step late), no TIE holds", ok);
    /* live recording elsewhere (HOME): the GM keys; a lane's note a hit, another GM drum a note */
    go_home();
    frame();
    t->seq_idx = 9;
    t->seq_pos = 0;
    fm1_in.notes = (1u << 7) | (1u << 13) | (1u << 6);   /* C 36 KICK, F# 42 HAT CL, E 35 (a kick 2 st down) */
    keyboard_block();
    fm1_in.notes = 0;
    keyboard_block();
    ok = !song.grid && t->step[9].hit == ((1u << DV_KICK) | (1u << DV_HATC)) && t->step[9].n == 1 && step_note(&t->step[9], 0) == 35 &&
         step_lanes(&t->step[9]) == ((1u << DV_KICK) | (1u << DV_HATC));
    song.rec = 0;
    song.playing = 0;
    bad += check("REC on HOME: GM keys quantised into the grid (35 a note on the KICK lane)", ok);
    /* SAVE > PHRASES: 12 BEAT into the DRUM track fills the grid */
    track_defaults_steps(t);
    pat_sig[0] = steps_sig(t);
    go_page(GR_PATS);
    ui.ppick = 11;
    press(B_OCTUP);
    ok = msg_is("LOADED 12 BEAT") && t->p[P_SLEN] == 16 && lane_steps(t, DV_KICK) == ((1u << 0) | (1u << 6) | (1u << 8) | (1u << 11)) &&
         lane_steps(t, DV_SNARE) == ((1u << 4) | (1u << 12)) && lane_steps(t, DV_HATO) == 1u << 14 &&
         lane_steps(t, DV_HATC) == 0xA6AEu && t->step[0].acc == 1u << DV_KICK && t->step[4].acc == 1u << DV_SNARE &&
         t->step[1].acc == 0u && t->step[16].time == ST_REST;
    for (i = 0; i < 16u; i++)
        ok &= t->step[i].n == 0u && !(step_fl(&t->step[i]) & SF_ACCENT);
    bad += check("PATTERNS 12 BEAT into a DRUM track: the grid (hits, the accents on 1 5 9 13)", ok);
    go_seq_step();
    ui.lane = DV_KICK;
    cursor_set(0);
    leds = grid_leds();
    ok = 1;
    for (i = 0; i < 16u; i++)
        ok &= (leds >> key_at(0, i) & 1u) == ((0x0941u >> i) & 1u);
    bad += check("..its KICK lane on the white key LEDs (1 7 9 12)", ok);
    {   /* the sound changes, the grid stays and plays as GM notes on another engine */
        step_t keep[NSTEP];
        memcpy(keep, t->step, sizeof keep);
        go_home();
        turn(EN_PRESET, 1);
        set_engine_of(t, 0);
        t->engine = t->eng_req;
        ok = !memcmp(keep, t->step, sizeof keep) && !grid_on();
        t->seq_hold = 0;
        t->seq_n = 0;
        seq_step(t, &t->step[0], 0, div_samples((uint32_t)t->p[P_SDIV]), 0);
        ok &= t->seq_n == 1u && t->seq_notes[0] == 36u;
        seq_release(t);
        bad += check("a sound load keeps the grid; on ANALOG its hits play as their GM notes", ok);
        hold(B_SAVE);                             /* undo the sound loads: DRUM again */
        set_engine_of(t, ENGI_DRUM);
        t->engine = t->eng_req;
    }
    my_steps(t);
    go_page(GR_PATS);
    ui.ppick = 11;
    press(B_OCTUP);
    press(B_OCTUP);
    hold(B_SAVE);
    bad += check("BEAT over the user's steps: the dialog, then SAVE held brings them back", step_note(&t->step[0], 0) == 40 &&
                 !t->step[0].hit && t->p[P_SLEN] == 32);
    /* the menu: the keys play again */
    go_page(GR_ROLL);
    frame();
    k = song.grid;
    hold(B_HOME);
    frame();
    bad += check("HOME held (the menu): the keys are no longer the grid's", k == 1u && ui.menu && !song.grid);
    menu_close();
    return bad;
}

static int test_screen(void)
{
    int bad = 0;
    ui_power_on();
    ui_say("STOP TO SAVE", "");
    ui.force = 1;
    ui_draw();
    bad += check("a message keeps the header's transport and BPM (drawn right of x 80)", ui.msg_t != 0);
    ui.uboot = 3;
    ui.menu = 1;
    ui.force = 1;
    ui_draw();
    bad += check("UPDATE MODE countdown drawn over the menu", ui.force == 0);
    ui.uboot = 0;
    ui.menu = 0;
    return bad;
}

static int test_favorites(void)
{
    int bad = 0;
    uint32_t total, pos;
    track_t before;
    ui_power_on();
    my_steps(TSEL);
    TSEL->p[P_AMODE] = 2;
    go_page(GR_BROWSE);
    before = *TSEL;
    turn(EN_K3, 1);
    turn(EN_K4, 1);
    pos = preset_pos(&total);
    bad += check("FAV and LIST mark/filter the current sound without loading or touching its steps",
                 preset_favorite() && favorites.filter && total == 1 && pos == 0 &&
                 !memcmp(TSEL, &before, sizeof before));
    bad += check("one favorite occupies one display row, without repeated copies",
                 preset_visible(pos, total, 0) == 0 && preset_visible(pos, total, 1) == total);
    select_engine(ENGI_DRUM);
    before = *TSEL;
    turn(EN_K3, 1);
    bad += check("a DRUM sound can be a favorite on any track", preset_favorite() &&
                 favorite_has(ENGI_DRUM, 0) && !memcmp(TSEL, &before, sizeof before));
    turn(EN_PRESET, 1);
    bad += check("filtered browsing crosses DRUM and synth sounds while retaining the track's pattern and ARP",
                 TSEL->eng_req == 0 && preset_favorite() && !memcmp(TSEL->step, before.step, sizeof before.step) &&
                 TSEL->p[P_AMODE] == 2);
    turn(EN_K3, -1);
    pos = preset_pos(&total);
    bad += check("unmarking the current sound leaves it loaded even when outside the filtered list",
                 !preset_favorite() && total == 1 && pos == total && TSEL->eng_req == 0);
    turn(EN_PRESET, -1);
    bad += check("browsing from a nonfavorite selects the last favorite", TSEL->eng_req == ENGI_DRUM);
    turn(EN_K3, -1);
    before = *TSEL;
    turn(EN_PRESET, 1);
    bad += check("empty favorites explains LIST ALL and retains the current sound and steps",
                 msg_is("NO FAVORITES") && !memcmp(TSEL, &before, sizeof before));
    turn(EN_K4, -1);
    preset_pos(&total);
    bad += check("LIST ALL restores the full browser without an implicit load", !favorites.filter && total > 1 &&
                 !memcmp(TSEL, &before, sizeof before));
    up_store(31, "Favorite");
    up_load(31);
    turn(EN_K3, 1);
    bad += check("user slot 32 has its own favorite reference", favorite_has(NENGINES, 31) && preset_favorite());
    up_store(31, "Renamed");
    bad += check("overwriting or renaming a user slot retains its star", favorite_has(NENGINES, 31));
    up_put(31, 0);
    bad += check("successful erasure removes the user slot star and source label",
                 !favorite_has(NENGINES, 31) && !up_used(31) && !TSEL->user);
    up_store(31, "New sound");
    bad += check("reusing the erased slot does not restore its old star", !favorite_has(NENGINES, 31));
    return bad;
}

/* 1.2 (Discussion #90): sound categories. The factory table covers every preset; PRESETS > LIST (KNOB 4) steps ALL FAV
 * BASS .. OTHER without a wrap and browses only that category (factory and user presets), kept with the settings in
 * favorites.factory[15][11] with favorites.filter 0 / 1 as before; the NAME screen's KNOB 3 sets a user preset's
 * category (from the sound's own), written with the name and kept by a rename; a record of before (version 2) gets
 * one by a rename, its sound unchanged; PROJECT's KNOB 3 a random name */
static int test_categories(void)
{
    int bad = 0, ok = 1;
    uint32_t e, k, total, pos, n, i;
    char b[16], s1[16];
    ui_power_on();
    memset(&favorites, 0, sizeof favorites);
    memset(up_bank, 0, sizeof up_bank);
    for (e = 0; e < NENGINES; e++) {
        const char *c = PRESET_CAT[e];
        if (!eng_ok(e) || e == ENGI_DIGITAL) continue;    /* (DIGITAL: FELUCCA_FM4 builds only, its sounds no category) */
        ok &= str_len(c) == ENGINES[e]->npresets;
        for (k = 0; k < ENGINES[e]->npresets; k++)
            ok &= preset_cat(e, k) >= CAT_BASS && preset_cat(e, k) <= CAT_OTHER;
    }
    bad += check("categories: every factory preset of every engine has one (the table's strings == the preset counts)",
                 ok && preset_cat(0, 2) == CAT_BASS && preset_cat(ENGI_DRUM, 0) == CAT_DRUM && preset_cat(ENGI_FM6, 0) == CAT_KEYS &&
                 preset_cat(ENGI_DRUM, 9) == CAT_DRUM && preset_cat(0, 99) == CAT_OTHER);
    go_page(GR_BROWSE);
    select_engine(0);
    apply_preset(0);                                       /* ANALOG SAW LEAD */
    turn(EN_K4, 1);
    turn(EN_K4, 1);
    pos = preset_pos(&total);
    for (n = 0, e = 0; e < NENGINES; e++)
        for (k = 0; eng_ok(e) && k < preset_shown(e); k++)
            n += preset_cat(e, k) == CAT_BASS;
    bad += check("  KNOB 4: ALL, FAV, then BASS: the list holds the factory basses only, the sound not loaded",
                 list_mode() == 2u && str_eq(list_name(list_mode()), "BASS") && !favorites.filter && ui_lcat == CAT_BASS &&
                 total == n && n >= 6u && pos == total && TSEL->eng_req == 0u && TSEL->preset == 0u);
    ok = 1;
    for (i = 0; i < total; i++) {
        turn(EN_PRESET, 1);
        ok &= track_cat(TSEL) == CAT_BASS;
    }
    bad += check("  the PRESETS knob steps through basses only (every engine), and round", ok && preset_pos(&total) == total - 1u);
    turn(EN_K4, 20);
    bad += check("  .. right to OTHER and no further (no wrap)", list_mode() == LIST_N - 1u && str_eq(list_name(list_mode()), "OTHER"));
#if !FELUCCA_FM4                                           /* (DIGITAL's presets: OTHER's) */
    {
        track_t before = *TSEL;
        ui.msg_t = 0;
        turn(EN_PRESET, 1);
        bad += check("  OTHER is empty (no factory sound): NO SOUNDS IN LIST, the sound stays",
                     msg_is("NO SOUNDS IN LIST") && !memcmp(TSEL, &before, sizeof before));
    }
#endif
    {   /* the settings: the category's byte, unknown = none; FAV clears it */
        persist_t p;
        list_set(1u + CAT_PAD);
        settings_export(&p);
        ok = p.favorites.factory[15][11] == CAT_PAD && p.favorites.filter == 0u;
        p.favorites.factory[15][11] = 9;
        ok &= settings_import(&p, sizeof p) == 1 && list_mode() == 0u;
        p.favorites.factory[15][11] = CAT_FX;
        ok &= settings_import(&p, sizeof p) == 1 && list_mode() == 1u + CAT_FX;
        turn(EN_K4, -20);
        turn(EN_K4, 1);
        bad += check("  kept with the settings ([15][11], an unknown value: none); FAV clears it, ALL at the left end",
                     ok && list_mode() == 1u && favorites.filter == 1u && ui_lcat == 0u);
        turn(EN_K4, -1);
    }
    {   /* #197: LIST / FAV not written on every detent (a flash erase stops the LED scan: the whole panel flashed) */
        uint32_t m0 = list_mode();
        int ok2;
        set_later.on = 0;
        frames(200);
        turn(EN_K4, 1);
        ok = set_later.on && list_mode() == m0 + 1u;
        turn(EN_K4, 1);
        turn(EN_K3, 1);
        frames(3000);
        ok &= set_later.on;                                /* (still on PRESETS, touched 3 s ago: not yet) */
        frames(SET_LATER_MS);
        ok &= !set_later.on;                               /* idle: written */
        turn(EN_K4, -2);
        turn(EN_K3, -1);
        ok2 = set_later.on && list_mode() == m0;
        go_page(GR_IORD);                                  /* the page left: written at once */
        frame();
        ok2 &= !set_later.on;
        go_page(GR_BROWSE);
        bad += check("  #197 LIST / FAV (KNOB 4 / 3): saved once idle or the page left, never on a detent", ok && ok2);
    }
    /* NAME: USER SAVE takes the sound's category; KNOB 3 changes it; written with the name */
    select_engine(0);
    apply_preset(2);                                       /* SQR BASS */
    song.playing = 0;
    go_page(GR_USER);
    ui.uslot = 4;
    press(B_OCTUP);
    bad += check("NAME: USER SAVE starts at the sound's category (a factory BASS)", name_on() && nm.cat == CAT_BASS);
    turn(EN_K3, 1);
    ok = nm.cat == CAT_LEAD;
    turn(EN_K3, -5);
    ok &= nm.cat == CAT_BASS;
    turn(EN_K3, 1);
    press(B_OCTUP);
    bad += check("  KNOB 3 steps the category (no wrap below BASS); OCT+ writes it with the name",
                 ok && !name_on() && up_used(4) && up_cat(4) == CAT_LEAD && up_rec(4)->packed[UP_CAT_AT] == CAT_LEAD);
    list_set(1u + CAT_LEAD);
    up_load(4);
    pos = preset_pos(&total);
    bad += check("  the user preset is in the LEAD list (after the factory leads), not in BASS",
                 pos == total - 1u && preset_at(pos, &k) == NENGINES && k == 4u &&
                 (list_set(1u + CAT_BASS), preset_pos(&total) == total));
    list_set(0);
    go_page(GR_USER);
    ui.uslot = 4;
    press(B_EDIT);
    ok = name_on() && nm.kind == NK_USER_RENAME && nm.cat == CAT_LEAD;
    turn(EN_K3, 3);                                        /* LEAD -> KEYS */
    press(B_OCTUP);
    up_name(4, b);
    bad += check("  a rename starts at the slot's own category and writes the new one, the sound as it was",
                 ok && up_cat(4) == CAT_KEYS && up_engine(4) == 0u);
    up_load(4);
    name_open(NK_USER_SAVE, 5);
    ok = name_on() && nm.cat == CAT_KEYS;
    press(B_OCTDN);
    bad += check("  saving a sound loaded from a user preset starts at that preset's category", ok && !name_on() && !up_used(5));
    {   /* a record of before categories (version 2: 16-bit values) */
        up_rec_t *r = up_rec(9);
        int16_t v0[P_COUNT], v1[P_COUNT];
        uint32_t c0;
        memset(r, 0, sizeof *r);
        r->used = UP_USED; r->ver = 2; r->engine = ENGI_DRUM; r->np = 72;
        str_cpy(r->name, "OLD KIT", 12);
        for (i = 0; i < 72u; i++) r->p[i] = (int16_t)(i % 100u);
        up_bank[0].magic = UP_BANK_MAGIC; up_bank[0].rsize = sizeof(up_rec_t); up_bank[0].nslot = UP_PER_BANK;
        c0 = up_cat(9);
        up_values(r, v0);
        ok = up_rename_cat(9, "OLD KIT", CAT_FX) != 1;
        up_values(r, v1);
        bad += check("  a version 2 record: listed by its engine (DRUM); a rename with a category makes it version 4, same sound",
                     c0 == CAT_DRUM && ok && r->ver == UP_VER && up_cat(9) == CAT_FX && !memcmp(v0, v1, sizeof v0));
        r->ver = 1; r->engine = ENGI_PHYS;
        bad += check("  a version 1 PHYS record keeps its version (MODEL 2 = DUST): no category",
                     (up_set_cat(r, CAT_PAD), r->ver == 1u && up_cat(9) == cat_of_engine(ENGI_PHYS)));
        memset(r, 0, sizeof *r);
    }
    /* PROJECT: KNOB 3 a random name */
    go_page(GR_SLOTS);
    name_open(NK_PROJ_SAVE, 1);
    turn(EN_K3, 1);
    str_cpy(s1, nm.s, sizeof s1);
    ok = nm.len >= 8u && nm.len <= 12u && nm.cur == nm.len && str_len(s1) == nm.len;
    for (i = 0, k = 0; i < 16u; i++)
        k |= (uint32_t)(str_len(NM_RA[i]) > 5u || str_len(NM_RB[i]) > 6u);
    turn(EN_K3, 1);
    bad += check("NAME: PROJECT's KNOB 3 types a random name (adjective + noun, at most 12), another one each detent",
                 ok && !k && !str_eq(s1, nm.s) && nm.len <= 12u && strchr(nm.s, ' '));
    name_close();
    {   /* the WAVETABLE names in the lists: the cards are short, the lists name the oscillator */
        char wtn[16];
        set_engine_of(TSEL, (uint32_t)ENGI_WT);
        pk_mdst_name(TSEL, MD_E1 + 1u, wtn);
        ok = str_eq(wtn, "POSITION A");
        pk_mdst_name(TSEL, MD_E1 + 6u, wtn);
        ok &= str_eq(wtn, "B LEVEL");
        ev_name(TSEL, P_E4, wtn);
        ok &= str_eq(wtn, "WAVE B");
        bad += check("WAVETABLE names: MOD picker POSITION A / B LEVEL, AUTOMATION WAVE B", ok);
    }
    memset(up_bank, 0, sizeof up_bank);
    memset(&favorites, 0, sizeof favorites);
    return bad;
}

static int test_display_preferences(void)
{
    int bad = 0, k;
    track_t sounds[NTRK];
    uint16_t before[240 * 240];
    ui_power_on();
    memcpy(sounds, trk, sizeof sounds);
    hold(B_HOME);
    ui.menu_sel = MI_COLOR;
    ui.force = 1; ui_draw();
    memcpy(before, host_screen, sizeof before);
    settings.palette = 5; palette_set(5);
    press(B_OCTUP);
    bad += check("COLOR OCT+ previews the next palette (PAPER, light) without closing the menu",
                 settings.palette == 6 && T_BG == UI_PALETTES[6].bg && ux.light && ui.menu == 1 && !song.octave &&
                 memcmp(before, host_screen, sizeof before) && !memcmp(sounds, trk, sizeof sounds));
    turn(EN_K1, 1);
    bad += check("COLOR KNOB 1 steps on to HI-CON, NIGHT (#50: true black), then wraps to GREY (pitch black)",
                 settings.palette == 7 && (turn(EN_K1, 1), settings.palette == 8u) && T_BG == 0u &&
                 str_eq(UI_PALETTES[8].name, "NIGHT") && (turn(EN_K1, 1), settings.palette == UI_GREY_INDEX) &&
                 str_eq(UI_PALETTES[UI_GREY_INDEX].name, "GREY") && T_BG == 0u);
    settings.lowcut = 2;
    ui.menu_sel = MI_LOWCUT;
    press(B_OCTUP);
    k = settings.lowcut == 2u;                         /* (OCT+ stops at the end, as the knobs) */
    press(B_OCTDN);
    k &= settings.lowcut == 1u && fx_lowcut == 1u;
    press(B_OCTDN);
    bad += check("the expanded menu retains all three SPEAKER modes (OCT+ / OCT- step them, stopping at the ends)",
                 k && settings.lowcut == 0 && !fx_lowcut && ui.menu == 1);
    {   /* #42: SPEAKER EQ FLAT (the old OFF, stored 0) LOWCUT BASS+; KNOB 1 steps them, the audio follows at once */
        int ok = str_eq(MI_NAME[MI_LOWCUT], "SPEAKER EQ") && str_eq(SPK_EQ[0], "FLAT") && str_eq(SPK_EQ[1], "LOWCUT") &&
                 str_eq(SPK_EQ[2], "BASS+");
        turn(EN_K1, 1); ok &= settings.lowcut == 1u && fx_lowcut == 1u;
        turn(EN_K1, 1); ok &= settings.lowcut == 2u && fx_lowcut == 2u;
        turn(EN_K1, 1); ok &= settings.lowcut == 2u;                  /* (KNOB 1 stops at the ends) */
        turn(EN_K1, -1); turn(EN_K1, -1); ok &= settings.lowcut == 0u && !fx_lowcut;
        bad += check("#42 SPEAKER EQ: FLAT (old OFF) / LOWCUT / BASS+ on KNOB 1, fx_lowcut follows", ok);
    }
    press(B_OCTDN);
    k = ui.menu == 1 && settings.lowcut == 0u;         /* (OCT- at the first value: nothing, the menu stays) */
    press(B_HOME);
    bad += check("HOME leaves display preferences without changing musical state (OCT- no longer closes)",
                 k && !ui.menu && settings.palette == UI_GREY_INDEX && !memcmp(sounds, trk, sizeof sounds));
    return bad;
}

/* 1.0.5: the MENU in tabs (ui_menu.c, menu_items.c MI_TAB): every tab 1..MTAB_ROWS rows in a run, each fitting the
 * page in S and LARGE; SELECT between the tabs (wraps, back at a tab's last row), ALGORITHM within one (wraps), PRESETS
 * idle; any of KNOB 1..4 and OCT+ / OCT- change a value, OCT+ opens CALIBRATION and ABOUT, HOME closes (OCT- not);
 * reopened at the same tab and row; STYLE/ANIM retired (MI_TAB = MTAB_COUNT, not shown); tabs always snap (ANIM always OFF) */
static void wdt_ms(void) { fm1_ms++; }
static void wdt_key(void) { fm1_ms++; if (fm1_ms % 50u == 0u) fm1_in.notes = 1u; }   /* a key pressed after a moment */
static int test_menu_tabs(void)
{
    int bad = 0, ok = 1;
    uint32_t t, i, n;
    for (t = 0; t < MTAB_COUNT; t++) {
        uint32_t f = mtab_first(t);
        n = mtab_rows(t);
        ok &= n >= 1u && n <= MTAB_ROWS && f < MI_COUNT;
        for (i = 0; i < MI_COUNT; i++)                  /* its rows in one run */
            ok &= (MI_TAB[i] == t) == (i >= f && i < f + n);
        ui.menu_sel = (uint8_t)f;
        ui_prefs = 0;
        ok &= mr_y(n) - mr_gap() + MP_PAD <= MK_Y - 6;
        ui_prefs = PREF_LARGE;
        ok &= mr_y(n) - mr_gap() + MP_PAD <= MK_Y - 6;
    }
    ok &= MP_Y + MP_PAD + (int32_t)MTAB_ROWS * 19 - 1 + MP_PAD <= MK_Y - 6;   /* (room for MTAB_ROWS: 18 px rows 1 px apart) */
    for (i = 0; i < MI_COUNT; i++)                      /* STYLE / ANIM / ACCEL: MTAB_COUNT (hidden); every other row a real tab */
        ok &= (i == MI_STYLE || i == MI_ANIM || i == MI_ACCEL) ? MI_TAB[i] == MTAB_COUNT : MI_TAB[i] < MTAB_COUNT;
    {   /* the tab bar: MT_GAP between every two tabs at every place of the slide, inside MT_X0..MT_X1 (S and LARGE) */
        int32_t a[MTAB_COUNT], x[MTAB_COUNT], w[MTAB_COUNT], p;
        for (n = 0; n < 2u; n++)
            for (p = 0; p <= (int32_t)(MTAB_COUNT - 1u) * MT_ONE; p++) {
                ui_prefs = n ? PREF_LARGE : 0u;
                mt.pos = (int16_t)p;
                mt_layout(a, x, w);
                ok &= x[0] >= MT_X0 && x[MTAB_COUNT - 1u] + w[MTAB_COUNT - 1u] <= MT_X1;
                for (t = 1; t < MTAB_COUNT; t++) ok &= x[t] - (x[t - 1u] + w[t - 1u]) == MT_GAP;
            }
    }
    ui_prefs = 0;
    bad += check("MENU tabs: DISPLAY CONTROL AUDIO MIDI SYSTEM (6 6 6 2 6 rows), 1..8 each in a run, fit the page; tab gaps "
                 "constant (S, LARGE); STYLE/ANIM/KNOB ACCEL hidden",
                 ok && MTAB_COUNT == 5u && str_eq(MTAB_NAME[0], "DISPLAY") && str_eq(MTAB_NAME[MTAB_MIDI], "MIDI") &&
                 mtab_rows(MTAB_DISPLAY) == 6u && MI_TAB[MI_HOME] == MTAB_DISPLAY && mtab_rows(MTAB_CONTROL) == 6u && mtab_rows(MTAB_AUDIO) == 6u &&
                 mtab_rows(MTAB_MIDI) == 2u && MI_TAB[MI_LCLEAR] == MTAB_MIDI && mtab_rows(MTAB_SYSTEM) == 6u && MI_TAB[MI_TUNE] == MTAB_AUDIO &&   /* (1.5: LEARN CLEAR, HELP) */
                 MI_TAB[MI_MIDIIN] == MTAB_MIDI && MI_TAB[MI_INFO] == MTAB_SYSTEM && MI_INFO >= MI_VALUES &&
                 MI_TAB[MI_STYLE] == MTAB_COUNT && MI_TAB[MI_ANIM] == MTAB_COUNT && MI_TAB[MI_ACCEL] == MTAB_COUNT);

    ui_power_on();
    hold(B_HOME);
    ok = ui.menu == 1 && ui.menu_sel == MI_COLOR && menu_tab() == MTAB_DISPLAY;
    for (i = 0; i < 5u; i++) turn(EN_ALGO, 1);          /* COLOR LARGE LEDS SCREEN OFF SCOPE HOME */
    ok &= ui.menu_sel == MI_HOME;
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_COLOR;    /* (wraps within the tab) */
    turn(EN_ALGO, -1); ok &= ui.menu_sel == MI_HOME;
    turn(EN_ALGO, -1); turn(EN_ALGO, -1); turn(EN_ALGO, -1); ok &= ui.menu_sel == MI_LEDS;
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_LEDS;   /* (PRESETS idle in the menu) */
    bad += check("MENU: ALGORITHM moves within the tab and wraps there (COLOR .. HOME); PRESETS idle", ok);
    turn(EN_SELECT, 1); ok = ui.menu_sel == MI_HOLD && menu_tab() == MTAB_CONTROL;
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_LATCH;     /* (KNOB ACCEL retired: HOLD -> FX LATCH) */
    turn(EN_SELECT, 1); ok &= ui.menu_sel == MI_LOWCUT && menu_tab() == MTAB_AUDIO;
    turn(EN_SELECT, 1); ok &= ui.menu_sel == MI_MIDIIN && menu_tab() == MTAB_MIDI;   /* (1.2) */
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_LCLEAR;    /* (1.5: LEARN CLEAR, then round) */
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_MIDIIN;
    turn(EN_SELECT, 1); ok &= ui.menu_sel == MI_SERIAL && menu_tab() == MTAB_SYSTEM;
    turn(EN_SELECT, -1); turn(EN_SELECT, -1); ok &= ui.menu_sel == MI_LOWCUT;
    turn(EN_SELECT, -1); ok &= ui.menu_sel == MI_LATCH; /* (back at the row left there) */
    turn(EN_SELECT, -1); ok &= ui.menu_sel == MI_LEDS;
    turn(EN_SELECT, -1); ok &= ui.menu_sel == MI_SERIAL && menu_tab() == MTAB_SYSTEM;   /* (wraps) */
    turn(EN_SELECT, 1); ok &= ui.menu_sel == MI_LEDS;
    bad += check("MENU: SELECT steps between the tabs and wraps, each tab back at its last row", ok && ui.menu == 1);
    turn(EN_SELECT, 1);                                 /* CONTROL: FX LATCH */
    ok = ui.menu_sel == MI_LATCH && !(ui_prefs & PREF_LATCH);
    for (i = 0; i < 4u; i++) {                         /* any of KNOB 1..4: right ON, left OFF */
        turn(EN_K1 + i, 1); ok &= (ui_prefs & PREF_LATCH) != 0u;
        turn(EN_K1 + i, -1); ok &= !(ui_prefs & PREF_LATCH);
    }
    turn(EN_SELECT, 1); turn(EN_ALGO, 1);               /* AUDIO: USB LEVEL */
    ok &= ui.menu_sel == MI_USB;
    turn(EN_K4, 1); ok &= (ui_prefs & PREF_USB_FIXED) && fx_usb_fixed;
    turn(EN_K2, -1); ok &= !(ui_prefs & PREF_USB_FIXED) && !fx_usb_fixed;
    bad += check("MENU: any of KNOB 1..4 changes the selected row's value in any tab, the menu stays", ok && ui.menu == 1);

    /* OCT+ / OCT-: the next / previous value on every kind of row (as the knobs: stopping at the ends; COLOR wraps) */
    ok = 1;
    for (i = 0; i < MI_VALUES; i++) {
        uint32_t n = menu_n(i), v, j, want;
        if (!menu_valued(i))                           /* (1.5: LEARN CLEAR, an action) */
            continue;
        uint8_t seen[128] = {0};                       /* (1.2: TUNE has 101 values) */
        ui.menu_sel = (uint8_t)i;
        for (j = 0; j <= n; j++)                       /* down to the first value (COLOR: round) */
            press(B_OCTDN);
        seen[menu_get(i) & 127u] = 1;
        for (j = 0; j <= n; j++) {                     /* up through every value */
            want = menu_step(i, 1);
            press(B_OCTUP);
            ok &= menu_get(i) == want && ui.menu == 1;
            seen[menu_get(i) & 127u] = 1;
        }
        for (j = 0; j < 128u; j++)
            ok &= seen[j] == (j < n);
        for (j = 0; j <= n; j++) {                     /* and down again */
            want = menu_step(i, -1);
            press(B_OCTDN);
            ok &= menu_get(i) == want && ui.menu == 1;
        }
        v = menu_get(i);                               /* the ends: COLOR wraps, the rest stop */
        if (i == MI_COLOR) {
            menu_put(i, n - 1u); press(B_OCTUP); ok &= menu_get(i) == 0u;
            press(B_OCTDN); ok &= menu_get(i) == n - 1u;
        } else {
            press(B_OCTDN); ok &= menu_get(i) == v;
            for (j = 0; j < n; j++) press(B_OCTUP);
            v = menu_get(i);
            press(B_OCTUP); ok &= menu_get(i) == v && v != menu_step(i, -1);
        }
        want = menu_step(i, -1);                       /* a knob steps as OCT does */
        turn(EN_K3, -1); ok &= menu_get(i) == want && ui.menu == 1;
    }
    ui_prefs = 0; settings_hold = HOLD_DEF; settings_leds = LEDS_DIM; ui_style = ST_FLAT;
    settings.lowcut = 0; fx_lowcut = 0; menu_put(MI_COLOR, UI_GREY_INDEX);
    song.g[G_TUNE] = 0; song.g[G_ROUTE] = 0;
    bad += check("MENU: OCT+ / OCT- step every value row up / down (COLOR wraps, the rest stop), the menu stays", ok);
    {   /* the ON / OFF rows: up (a knob right, OCT+) is ON, the switch's knob to the right, whichever bit it is */
        static const uint8_t SW[] = {MI_LARGE, MI_LATCH, MI_BPMLOCK, MI_SERIAL, MI_RESTORE};
        ok = 1;
        for (i = 0; i < NELEM(SW); i++) {
            ui.menu_sel = SW[i];
            press(B_OCTDN); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "OFF");
            press(B_OCTUP); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "ON");
            turn(EN_K2, -1); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "OFF");
            turn(EN_K2, 1); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "ON");
        }
        ui.menu_sel = MI_STYLE;                         /* STYLE / ANIM / ACCEL retired: knob / OCT leave them fixed */
        press(B_OCTUP); ok &= ui_style == ST_FLAT && menu_get(MI_STYLE) == 0u;
        press(B_OCTDN); ok &= ui_style == ST_FLAT && menu_get(MI_STYLE) == 0u;
        ui.menu_sel = MI_ANIM;
        press(B_OCTUP); ok &= (ui_prefs & PREF_ANIM_OFF) && menu_get(MI_ANIM) == 1u;
        press(B_OCTDN); ok &= (ui_prefs & PREF_ANIM_OFF) && menu_get(MI_ANIM) == 1u;
        ui.menu_sel = MI_ACCEL;
        press(B_OCTUP); ok &= menu_get(MI_ACCEL) == 1u;
        press(B_OCTDN); ok &= menu_get(MI_ACCEL) == 1u;
        ui_prefs = PREF_ANIM_OFF;
        bad += check("MENU: on an ON / OFF row OCT+ / a knob right is ON, OCT- / left OFF (USB SERIAL, RESTORE LAST too); STYLE/ANIM/ACCEL fixed", ok);
    }
    ui.menu_sel = MI_USB;
    press(B_HOME);
    ok = !ui.menu;
    hold(B_HOME);
    ok &= ui.menu == 1 && ui.menu_sel == MI_USB && menu_tab() == MTAB_AUDIO;
    frames(500);
    ok &= ui.menu == 1;                                 /* (the hold's release: no tap, the menu stays) */
    hold(B_HOME);
    ok &= !ui.menu;
    frames(1000);
    ok &= !ui.menu;                                     /* (the hold that closed it: no reopen) */
    hold(B_HOME);
    bad += check("MENU: HOME pressed closes it, held too (no reopen); held opens it again at the same tab and row",
                 ok && ui.menu == 1 && ui.menu_sel == MI_USB);
    turn(EN_SELECT, 1); turn(EN_SELECT, 1); turn(EN_ALGO, -1);   /* (MIDI,) SYSTEM: ABOUT */
    ok = ui.menu_sel == MI_ABOUT;
    frame();
    ok &= oct_leds() == OCT_BREATH;                     /* (OCT+ opens, OCT- does nothing: dark) */
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;   /* (no close, no back) */
    turn(EN_K1, 1); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;
    press(B_OCTUP); ok &= ui.menu == 2;
    ok &= oct_leds() == 1u;                             /* (ABOUT: OCT- back, lit) */
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;
    turn(EN_ALGO, -1); ok &= ui.menu_sel == MI_INFO;     /* (1.2) INFO: as ABOUT, no scroll */
    frame();
    ok &= oct_leds() == OCT_BREATH;
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_INFO;
    press(B_OCTUP); ok &= ui.menu == 3 && oct_leds() == 1u;
    turn(EN_PRESET, 3); turn(EN_K1, 1); turn(EN_ALGO, 1); turn(EN_SELECT, 1);
    ok &= ui.menu == 3 && !ui.menu_scroll && ui.menu_sel == MI_INFO;
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_INFO;
    turn(EN_ALGO, -1); ok &= ui.menu_sel == MI_PANEL;
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_PANEL;
    {   /* CALIBRATION: OCT+ starts the setup (nothing pressed here: it times out and keeps the table) */
        panel_t p0 = panel;
        host_wdt_hook = wdt_ms;
        press(B_OCTUP);
        host_wdt_hook = 0;
        ok &= !memcmp(&p0, &panel, sizeof panel) && msg_is("SETUP CANCELLED") && ui.menu == 1;
    }
    {   /* CALIBRATION: a piano key cancels at once (no 30 s wait), the table kept */
        panel_t p0 = panel;
        uint32_t t = fm1_ms;
        host_wdt_hook = wdt_key;
        press(B_OCTUP);
        host_wdt_hook = 0;
        fm1_in.notes = 0;
        ok &= !memcmp(&p0, &panel, sizeof panel) && msg_is("SETUP CANCELLED") && ui.menu == 1 && fm1_ms - t < 1000u;
    }
    bad += check("MENU SYSTEM: OCT+ opens ABOUT, INFO (OCT- back to the tab) and CALIBRATION; OCT- does nothing on them", ok);
    turn(EN_ALGO, 1); turn(EN_ALGO, 1); press(B_OCTUP);     /* (INFO,) ABOUT, HOME: closed, from the document */
    ok = ui.menu == 2;
    press(B_HOME);
    ok &= !ui.menu && ui.home;
    hold(B_HOME); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;   /* (opened at the tabs, ABOUT's row) */
    turn(EN_SELECT, -1); frame();
    {   /* a value row: OCT- and OCT+ both breathe, each while that way has a value to go to; none lit */
        uint32_t r = ui.menu_sel, want = (menu_step(r, -1) != menu_get(r) ? OCT_BREATH_DN : 0u) |
                                         (menu_step(r, 1) != menu_get(r) ? OCT_BREATH : 0u);
        ok &= r < MI_VALUES && oct_leds() == want && want != 0u;
        menu_put(MI_HOLD, 0u); ui.menu_sel = MI_HOLD;   /* HOLD at its first value: OCT- dark, OCT+ breathing */
        ok &= oct_leds() == OCT_BREATH;
        menu_put(MI_HOLD, 1u);                          /* in between: both breathe */
        ok &= oct_leds() == (OCT_BREATH | OCT_BREATH_DN);
    }
    menu_close();
    bad += check("MENU: HOME closes ABOUT too; reopened at the tabs; a value row breathes OCT- / OCT+ where they can go", ok);

    /* ANIM retired (always OFF): tabs snap at once (no slide) */
    ui_power_on();
    hold(B_HOME);
    frame();
    ok = (ui_prefs & PREF_ANIM_OFF) != 0u && mt.pos == 0;
    turn(EN_SELECT, 1);
    ok &= mt.pos == MT_ONE;                             /* CONTROL at once */
    turn(EN_SELECT, 1);
    ok &= mt.pos == 2 * MT_ONE;                         /* AUDIO at once */
    menu_close();
    bad += check("MENU tabs snap (ANIM always OFF: no slide)", ok);
    return bad;
}

/* 1.2: MENU > AUDIO > TUNE and MENU > MIDI > MIDI IN edit the project's G_TUNE and G_ROUTE (up to 1.1.5 GLO > GLOBAL K4
 * and GLO > SYSTEM K3): the same values, stored with the project as before (a PROJECT save / load, the autosave), never
 * in the settings record */
static int test_menu_project_rows(void)
{
    int bad = 0, ok;
    persist_t p0, p1;
    uint32_t sig;
    ui_power_on();
    style_apply();                                      /* PREF_ANIM_OFF before export (same as after menu frames) */
    memset(&p0, 0, sizeof p0); memset(&p1, 0, sizeof p1);   /* (export keeps some fields as they were) */
    settings_export(&p0);
    sig = autosave_sig();
    hold(B_HOME);
    ui.menu_sel = MI_TUNE;
    ok = ui.menu == 1 && menu_tab() == MTAB_AUDIO && song.g[G_TUNE] == 0 && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "0");
    press(B_OCTUP); ok &= song.g[G_TUNE] == 1 && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "+1");
    turn(EN_K2, 11); ok &= song.g[G_TUNE] == 12;         /* (a fast turn: as far as the knob went) */
    turn(EN_K1, -40); ok &= song.g[G_TUNE] == -28 && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "-28");
    turn(EN_K3, -100); ok &= song.g[G_TUNE] == -50;      /* (the ends: -50 .. +50 cents) */
    press(B_OCTDN); ok &= song.g[G_TUNE] == -50;
    turn(EN_K4, 200); ok &= song.g[G_TUNE] == 50 && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "+50");
    turn(EN_K4, -31); ok &= song.g[G_TUNE] == 19;
    bad += check("MENU > AUDIO > TUNE: the project's G_TUNE, -50 .. +50 cents (OCT one, a knob as far as it turned)", ok);
    ui.menu_sel = MI_MIDIIN;
    ok = menu_tab() == MTAB_MIDI && song.g[G_ROUTE] == 0 && str_eq(menu_vname(MI_MIDIIN, menu_get(MI_MIDIIN)), "CH1-4");
    press(B_OCTUP); ok &= song.g[G_ROUTE] == 1;
    turn(EN_K1, 1); turn(EN_K1, 1); ok &= song.g[G_ROUTE] == 3 && str_eq(menu_vname(MI_MIDIIN, menu_get(MI_MIDIIN)), "CH9-12");
    bad += check("MENU > MIDI > MIDI IN: the project's G_ROUTE (ROUT), its names", ok);
    menu_close();
    settings_export(&p1);
    ok = !memcmp(&p0, &p1, sizeof p0) && autosave_sig() != sig;
    bad += check("TUNE / MIDI IN: nothing in the settings record; the autosave sees the change (the music's)", ok);
    project_save(2);                                     /* (PROJECT C) */
    song.g[G_TUNE] = 0; song.g[G_ROUTE] = 0;
    project_load(2);
    ok = song.g[G_TUNE] == 19 && song.g[G_ROUTE] == 3 && menu_get(MI_TUNE) == 69u && menu_get(MI_MIDIIN) == 3u;
    song.g[G_TUNE] = -7; song.g[G_ROUTE] = 4;            /* (another project's values: the rows follow the load) */
    project_save(3);
    project_load(2);
    ok &= song.g[G_TUNE] == 19 && song.g[G_ROUTE] == 3;
    project_load(3);
    ok &= song.g[G_TUNE] == -7 && song.g[G_ROUTE] == 4 && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "-7") &&
          str_eq(menu_vname(MI_MIDIIN, menu_get(MI_MIDIIN)), "CH13-16");
    bad += check("TUNE / MIDI IN: saved with the project and loaded back (round trip; each project its own)", ok);
    ui_power_on();
    return bad;
}

static int test_information(void)
{
    uint16_t header[240 * H_HEAD], footer[240 * 19], battery[240 * H_HEAD];
    track_t sounds[NTRK];
    int bad = 0, quiet = 1;
    uint32_t x, y;
    ui_power_on();
    memcpy(sounds, trk, sizeof sounds);
    hold(B_HOME);
    ui.menu_sel = MI_ABOUT;
    press(B_OCTUP);
    ui_draw();
    bad += check("ABOUT opens one document with scrollable license text and credits", ui.menu == 2 && !ui.menu_scroll && menu_scroll_max() > MENU_DOC_H);
    for (y = 146; y < 220; y++)
        for (x = 158; x < 232; x++)
            if (x < 166 || x >= 224 || y < 154 || y >= 212)
                quiet &= host_screen[y * 240 + x] == swap16(UI_QR_LIGHT);
    bad += check("ABOUT QR keeps its complete white quiet zone above the fixed footer", quiet);
    memcpy(header, host_screen, sizeof header);
    memcpy(footer, host_screen + 221 * 240, sizeof footer);
    turn(EN_ALGO, 3);
    ui_draw();
    bad += check("ALGORITHM scrolls ABOUT with its header and controls fixed",
                 ui.menu == 2 && ui.menu_scroll > 0 && !memcmp(header, host_screen, sizeof header) &&
                 !memcmp(footer, host_screen + 221 * 240, sizeof footer));
    turn(EN_ALGO, 12);
    ui_draw();
    bad += check("ALGORITHM continues from ABOUT into CREDITS without changing pages",
                 ui.menu == 2 && ui.menu_scroll > 216 && !memcmp(header, host_screen, sizeof header) &&
                 !memcmp(footer, host_screen + 221 * 240, sizeof footer));
    uint16_t position = ui.menu_scroll;
    press(B_OCTUP);
    bad += check("OCT+ does not switch pages or alter the octave in the document",
                 ui.menu == 2 && ui.menu_scroll == position && song.octave == 0);
    turn(EN_PRESET, 3);
    bad += check("PRESETS leaves ABOUT scroll alone", ui.menu == 2 && ui.menu_scroll == position);
    turn(EN_ALGO, 10000);
    turn(EN_ALGO, 10000);
    ui_draw();
    bad += check("the continuous document stops at its final credit",
                 ui.menu == 2 && ui.menu_scroll == menu_scroll_max());
    turn(EN_ALGO, -10000);
    bad += check("scrolling back through CREDITS returns to ABOUT's top", ui.menu == 2 && !ui.menu_scroll);
    press(B_OCTDN);
    bad += check("OCT- returns to the menu without changing sounds or patterns",
                 ui.menu == 1 && !memcmp(sounds, trk, sizeof sounds));
    press(B_OCTUP);
    bad += check("reopening ABOUT starts the continuous document at the top", ui.menu == 2 && !ui.menu_scroll);
    menu_close();
    ui.msg_t = 0;
    usb.config = 1; usb.suspended = 0;
    song.batt_raw = 530;
    ui.force = 1; draw_head(); ui.force = 0;
    memcpy(battery, host_screen, sizeof battery);
    uint32_t signature = ui.head_sig;
    for (x = 0; x < 4; x++) { fm1_ms += 600; draw_head(); }
    {   /* GREY bg is pitch black (0): probe the battery cell for ink, not "pixel != 0" alone */
        uint32_t ink = 0, bx, by, bg = host_screen[0];
        for (by = 4u; by < 20u; by++)
            for (bx = HEAD_BAT_X; bx < HEAD_BAT_X + 24u; bx++)
                if (host_screen[by * 240u + bx] != bg) ink++;
        bad += check("USB power shows a steady bolt inside the battery without animation",
                     batt_shown() == 4 && ui.head_sig == signature && !memcmp(battery, host_screen, sizeof battery) &&
                     ink > 40u);
        usb.suspended = 1;
        draw_head();
        ink = 0;
        for (by = 4u; by < 20u; by++)
            for (bx = HEAD_BAT_X; bx < HEAD_BAT_X + 24u; bx++)
                if (host_screen[by * 240u + bx] != bg) ink++;
        bad += check("USB suspension restores the battery outline and measured level",
                     batt_shown() == 0 && ui.head_sig != signature && ink > 20u);
    }
    usb.config = 0; usb.suspended = 0;
    song.batt_raw = 531; int levels = batt_shown() == 1;
    song.batt_raw = 561; levels &= batt_shown() == 2;
    song.batt_raw = 591; levels &= batt_shown() == 3;
    bad += check("battery operation retains all three ADC level thresholds", levels);
    return bad;
}

/* GREY is grayscale on every screen: every page of every engine, HOME, the menu, ABOUT, the dialogs (RGB565 gray:
 * R = B, G = 2 R). */
static int screen_gray(void)
{
    uint32_t i;
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        if ((c >> 11) != (c & 31u) || ((c >> 5) & 63u) != (c >> 11) * 2u)
            return 0;
    }
    return 1;
}
static int mono_screens(uint32_t pal, int (*chk)(void), uint32_t *n)
{
    int ok = 1;
    uint32_t e, i;
    for (e = 0; e < NENGINES; e++) {
        if (!eng_ok(e))
            continue;                               /* (DIGITAL without FELUCCA_FM4: never a track's) */
        for (i = 0; i < NPAGES; i++) {
            ui_power_on(); set_engine_of(TSEL, e); song.playing = 1; song.rec = 1;
            ui.home = 0; ui.page = (uint8_t)i; page_entered();
            if (!page_visible(i)) continue;
            settings.palette = pal; palette_set(pal);
            memset(host_screen, 0, sizeof host_screen);
            ui.force = 1; ui_draw();
            ok &= chk();
            (*n)++;
        }
        ui_power_on(); set_engine_of(TSEL, e); go_home();
        settings.palette = pal; palette_set(pal);
        ui.force = 1; ui_draw(); ok &= chk();
    }
    for (i = 0; i < 4u; i++) {
        ui_power_on(); song.rec = 3;
        settings.palette = pal; palette_set(pal);
        if (i < 2u) { ui.menu = (uint8_t)(i + 1u); ui.menu_scroll = 300; }
        else if (i == 2u) ui.confirm = CF_OVR_PROJ;
        else ui.uboot = 2;
        ui.force = 1; ui_draw(); ok &= chk();
    }
    return ok;
}
static int test_mono_screens(void)
{
    int bad = 0, ok;
    uint32_t e, n = 0;
    ok = mono_screens(UI_GREY_INDEX, screen_gray, &n);
    bad += check("GREY: every pixel of every page, HOME, menu, ABOUT, dialog and countdown is gray", ok && n > 200u);
    ui_power_on();
    for (e = 0; e < NPALETTES; e++) {               /* the other palettes are not gray: the check sees colour */
        ui_power_on(); palette_set(e); ui.force = 1; ui_draw();
        ok = screen_gray();
        if (e == UI_GREY_INDEX ? !ok : ok) bad += check("palette colour shows on HOME", 0);
    }
    palette_set(UI_GREY_INDEX);
    return bad;
}

/* ROLL: the rolling digits of the four cards and the header BPM (ui_draw.c roll_*). The old GLOBAL page's four (1.2: a
 * test page over GLO > CLOCK, whose KNOB 4 is empty since TUNE went to the MENU): BPM (also in the header), SWG, CLK (a
 * name), TUNE (signed). A roll frame differs from the static render only inside the
 * value strip, its last frame is the static render; unchanged digits stay; the direction follows the sign; a
 * change mid-roll retargets; a fast turn, another shape, a page / track / palette change and ui.force snap. */
static uint16_t roll_shot[240 * 240];
static const page_t ROLL_PG = {"CLOCK", FAM_GLO, SC_GLOBAL, GR_NONE, GLO_SEC_CLK, {G_BPM, G_SWING, G_CLOCK, G_TUNE}};
static void roll_settle(void)                     /* CLOCK + TUNE, nothing rolling, no knob hot */
{
    uint32_t k;
    go_title("CLOCK");
    page_over = &ROLL_PG;                         /* (cur_page(): its knobs, its cards; a layer's way, ui_layer.c) */
    ui.hot_t = 0; ui.bpm_t = 0;
    for (k = 0; k < 12u; k++) frame();
}
/* host_screen against roll_shot: the pixels that differ inside the box (in = 1) or outside it */
static uint32_t roll_diff(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int in)
{
    uint32_t n = 0;
    int32_t x, y;
    for (y = 0; y < 240; y++)
        for (x = 0; x < 240; x++) {
            int inside = x >= x0 && x < x1 && y >= y0 && y < y1;
            if (inside == in && host_screen[y * 240 + x] != roll_shot[y * 240 + x]) n++;
        }
    return n;
}
/* the mean row (x16) of the ink (not the card surface) in columns x0..x1-1, rows y0..y1-1 of a screen */
static int32_t roll_ink_row(const uint16_t *s, int32_t x0, int32_t x1, int32_t y0, int32_t y1)
{
    int32_t x, y, n = 0, sum = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            if (s[y * 240 + x] != swap16(T_SURF)) { n++; sum += y * 16; }
    return n ? sum / n : -1;
}
static int roll_static_now(void)                  /* the screen is the static render (a forced redraw changes nothing) */
{
    memcpy(roll_shot, host_screen, sizeof roll_shot);
    ui.force = 1;
    ui_draw();
    return roll_diff(0, 0, 0, 0, 0) == 0;
}
#define STRIP(c) CARD_X(c), Y_LABEL + ROLL_Y, CARD_X(c) + COL_W, Y_LABEL + ROLL_Y + ROLL_H
static int test_roll(void)
{
    int bad = 0, ok;
    uint32_t k, n, e, id;
    static const struct { const char *a, *b; int d; } DIRS[] = {
        {"120", "121", 1}, {"121", "120", -1}, {"129", "130", 1}, {"-12", "-13", -1}, {"-13", "-12", 1}, {"+5", "+6", 1},
        {"1.25", "1.30", 1}, {"0.9", "0.8", -1}, {"9", "10", 0}, {"0", "-1", 0}, {"+5", "-5", 0}, {"C3", "C4", 0},
        {"1/4", "1/8", 0}, {"OFF", "ON", 0}, {"2X", "4X", 0}, {"120", "120", 0}, {"123456", "123457", 1},
        {"1234567", "1234568", 0}, {"12%", "13%", 0}};

    ok = 1;
    for (k = 0; k < sizeof DIRS / sizeof DIRS[0]; k++)
        if (roll_dir(DIRS[k].a, DIRS[k].b) != DIRS[k].d) { printf("  roll_dir %s -> %s\n", DIRS[k].a, DIRS[k].b); ok = 0; }
    bad += check("roll: numbers of one shape roll by the sign of the change; names, length, sign changes snap", ok);
    /* ANIM retired (always OFF): every value snaps; the roll path never starts */
    ok = 1;
    n = 0;
    for (e = 0; e < NENGINES; e++)
        for (id = 0; id < NPAGES && eng_ok(e); id++) {
            uint32_t c;
            ui_power_on(); set_engine_of(TSEL, e);
            ui.home = 0; ui.page = (uint8_t)id; page_entered();
            if (!page_visible(id) || PAGES[id].graph == GR_MOD) continue;
            ui.force = 1; ui_draw();
            for (c = 0; c < 4u; c++) {
                int16_t *vp;
                const param_desc_t *d = PAGES[id].scope == SC_GLOBAL || PAGES[id].scope == SC_TRACK ||
                                        PAGES[id].scope == SC_ENGINE ? page_desc(cur_page(), c, &vp) : 0;
                int32_t v, v0;
                if (!d || !d->label || d->label[0] == '-' || ((act_cols() >> c) & 1u)) continue;
                v0 = *vp;
                for (v = d->min; v < d->max && v - d->min < 40; v++) {
                    *vp = (int16_t)v; ui.frame += ROLL_SNAP; draw_columns();
                    *vp = (int16_t)(v + 1); ui.frame += ROLL_SNAP; draw_columns();
                    if (ui.roll[c].from[0]) {
                        printf("  %s/%s %s: %s rolls (ANIM OFF)\n", ENGINES[e]->name, PAGES[id].title, d->label, ui.roll[c].from);
                        ok = 0;
                    }
                    n++;
                }
                *vp = (int16_t)v0;
            }
            ok &= (ui_prefs & PREF_ANIM_OFF) != 0u;
        }
    ok &= n > 500u;
    bad += check("roll: ANIM always OFF: every page and engine snaps (no rolling digits)", ok);

    ui_power_on(); roll_settle();
    turn(EN_K4, 1);
    ok = song.g[G_TUNE] == 1 && !ui.roll[3].from[0] && roll_static_now() && (ui_prefs & PREF_ANIM_OFF);
    bad += check("roll: a card's value snaps at once (TUNE 0 -> 1), the frame is static", ok);

    ui_power_on(); roll_settle();
    turn(EN_K1, 1);
    ok = song.g[G_BPM] == 121 && !ui.roll[ROLL_BPM].from[0] && !ui.roll[0].from[0] && roll_static_now();
    ui_power_on(); song.g[G_TUNE] = 10; roll_settle();
    turn(EN_K4, 1);
    ok &= song.g[G_TUNE] == 11 && !ui.roll[3].from[0] && roll_static_now();
    bad += check("roll: header BPM and card values snap (no strip animation)", ok);

    ui_power_on(); song.g[G_TUNE] = 1; roll_settle();
    turn(EN_K4, 1); turn(EN_K4, 1);
    ok = song.g[G_TUNE] == 3 && !ui.roll[3].from[0] && roll_static_now();
    bad += check("roll: successive turns stay static (no mid-roll retarget)", ok);

    ui_power_on(); roll_settle();
    turn(EN_K4, -1);
    ok = song.g[G_TUNE] == -1 && !ui.roll[3].from[0] && roll_static_now();
    turn(EN_K3, 1);
    ok &= song.g[G_CLOCK] != 0 && !ui.roll[2].from[0] && roll_static_now();
    bad += check("roll: a sign / length change (0 -> -1) and a name (CLK) snap", ok);

    ok = 1;
    for (k = 0; k < 5u; k++) {
        ui_power_on(); roll_settle();
        turn(EN_K4, 1);
        ok &= !ui.roll[3].from[0];
        if (k == 0) { page_over = 0; go_title("ENV"); }
        else if (k == 1) turn(EN_ALGO, 1);
        else if (k == 2) { settings.palette = 1; palette_set(1); }
        else if (k == 3) set_engine_of(TSEL, eng_step(TSEL->eng_req, 1));
        else ui.force = 1;
        frame();
        ok &= !ui.roll[3].from[0] && !ui.roll[ROLL_BPM].from[0];
    }
    ui_power_on(); roll_settle();
    turn(EN_K1, 1);
    ok &= !ui.roll[ROLL_BPM].from[0];
    ui.force = 1; frame();
    ok &= !ui.roll[ROLL_BPM].from[0];
    bad += check("roll: page / track / palette / engine / force: still no roll (ANIM OFF)", ok);
    palette_set(UI_GREY_INDEX);

    ui_power_on(); roll_settle();
    ok = 1;
    turn(EN_K1, 9);
    turn(EN_K4, -1);
    for (k = 0; k < 3u; k++) { ok &= screen_gray() && !ui.roll[0].from[0] && !ui.roll[3].from[0]; frame(); }
    bad += check("roll: GREY value changes stay gray and static", ok);
    page_over = 0;
    ui_power_on();
    return bad;
}

/* #63 ("parameters moving on their own, fixed by a page change"): a soak of the cards and the rolling digits.
 * ~40 min of UI frames: random turns of KNOB 1..4 and SELECT (bursts back and forth), page / HOME / track
 * changes, the transport and REC (motion recorded and played back), LFO rates, uneven frame times, ui.frame
 * and fm1_ms wrapping. After every frame no roll is older than ROLL_FRAMES; at random quiet moments (no
 * roll, nothing hot) the cards are pixel-identical to a forced redraw (what a page change would show). */
static int test_soak_cards(void)
{
    uint32_t seed = 63u, f, quiet = 0, old = 0, stale = 0, cmp = 0, c;
    static uint16_t shot[240 * CARD_H];
#define SOAK_RND(n) ((seed = seed * 1103515245u + 12345u) >> 8) % (n)
    ui_power_on();
    ui.frame = 0xFFFFFF00u;
    fm1_ms = 0xFFFFFFFFu - 60000u;
    for (f = 0; f < 150000u; f++) {
        uint32_t r = SOAK_RND(1000u);
        if (r < 25u) {                                  /* a knob, 1..3 detents either way */
            uint32_t role = SOAK_RND(5u) ? EN_K1 + SOAK_RND(4u) : EN_SELECT;
            int32_t s = (int32_t)SOAK_RND(3u) + 1;
            host_enc[panel.enc[role]] += (SOAK_RND(2u) ? s : -s) * panel.dir[role];
            quiet = 0;
        } else if (r < 29u) {                           /* a page */
            uint32_t i = SOAK_RND(NPAGES);
            if (page_visible(i)) { ui.home = 0; ui.page = (uint8_t)i; page_entered(); }
            quiet = 0;
        } else if (r < 30u) { ui.home = 1; page_entered(); quiet = 0; }
        else if (r < 31u) transport_req = song.playing ? 2u : 1u;
        else if (r == 31u && SOAK_RND(3u) == 0u) rec_tap();
        else if (r == 32u) { host_enc[panel.enc[EN_ALGO]] += SOAK_RND(2u) ? 1 : -1; quiet = 0; }
        else if (r == 33u) trk[SOAK_RND(NTRK)].p[P_LRATE] = (int16_t)SOAK_RND(128u);
        fm1_ms += SOAK_RND(50u) ? 16u : 1u + SOAK_RND(200u);
        host_ticks += 16000u + SOAK_RND(3u) * 4000u;
        ui_input();
        if (song.playing || transport_req) events_block(FS * 16u / 1000u);
        ui_draw();
        quiet++;
        if (ui.menu || ui.layer || ui.confirm || name_on() || ui.uboot)
            continue;                                   /* (the cards are not drawn) */
        for (c = 0; c < 5u; c++)
            old += ui.roll[c].from[0] && (uint8_t)(ui.frame - ui.roll[c].t0) >= ROLL_FRAMES;
        if (quiet > 45u && !ui.hot_t && !ui.bpm_t && !ui.msg_t && SOAK_RND(8u) == 0u) {
            for (c = 0; c < 5u && !ui.roll[c].from[0]; c++) {}
            if (c < 5u)
                continue;
            memcpy(shot, &host_screen[Y_LABEL * 240], sizeof shot);
            ui.force = 1;
            ui_draw();
            cmp++;
            stale += memcmp(shot, &host_screen[Y_LABEL * 240], sizeof shot) != 0;
        }
    }
#undef SOAK_RND
    if (old || stale || cmp < 500u) printf("  soak: %u old rolls, %u stale cards in %u checks\n", old, stale, cmp);
    stop_transport();
    memset(&motion, 0, sizeof motion);                  /* (what it recorded: the tests below start without) */
    memset(motion_active, 0, sizeof motion_active);
    motion_base_valid = motion_full = motion_save_lim = 0;
    ui_power_on();
    return check("#63 soak: 150000 random frames, wrapping clocks, motion: no roll lasts, cards = a forced redraw",
                 !old && !stale && cmp >= 500u);
}

static void screen_save(const char *dir, const char *name)
{
    char path[512];
    uint32_t i;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    f = fopen(path, "wb");
    if (!f) return;
    ui.msg_t = 0; ui.force = 1;
    ui_draw();
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);           /* canvas stores panel-order RGB565 */
        uint8_t b[3] = {(uint8_t)((c >> 11) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u), (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(b, 1, 3, f);
    }
    fclose(f);
}
static void chain_screens(const char *dir)
{
    uint32_t k;
    ui_power_on();
    go_page(GR_SONG);
    screen_save(dir, "song-empty");
    for (k = 0; k < 2u; k++) { my_steps(&trk[0]); project_save(k); }
    chain_config.count = 3;
    chain_config.row[0] = chain_row_of(0, 2);
    chain_config.row[1] = chain_row_of(1, 4);
    chain_config.row[2] = chain_row_of(0, 1);
    ui.song_row = 1;
    screen_save(dir, "song-ready");
    chain_prepare(); events_block(32);
    screen_save(dir, "song-playing");
    seq_stop();
    /* WAVETABLE's EDIT graph: A's output on page 1, B's wave on page 2 (A while RATIO is hot) */
    ui_power_on();
    set_engine_of(TSEL, (uint32_t)ENGI_WT);
    go_title("EDIT 1");
    ui.force = 1;
    frame();
    screen_save(dir, "wt-edit-a");
    go_title("EDIT 2");
    ui.force = 1;
    frame();
    screen_save(dir, "wt-edit-b");
    ui.hot_t = 40;
    ui.hot_col = 3;
    ui.force = 1;
    frame();
    screen_save(dir, "wt-edit-b-ratio");
}

static int test_chain(void)
{
    uint32_t i, k, period, last, n = 32u;
    int bad = 0, ok;
    step_t before[NTRK][NSTEP];
    int16_t timing[NTRK][4], sounds[NTRK][P_COUNT];
    ui_power_on();
    bad += check("SONG empty: PLAY does not start", chain_prepare() == 1 && !transport_req);
    for (k = 0; k < 2u; k++) {
        for (i = 0; i < NTRK; i++) {
            track_defaults_steps(&trk[i]);
            trk[i].p[P_SLEN] = (int16_t)(2u + k);
            trk[i].step[0] = (step_t){{(uint8_t)(60u + 5u * k), 0, 0, 0}, 1, ST_NOTE, 0, 96, 0, 0};
        }
        project_save(k);
    }
    for (i = 0; i < NTRK; i++) {
        my_steps(&trk[i]);
        trk[i].p[P_SLEN] = 9;
        memcpy(before[i], trk[i].step, sizeof before[i]);
        memcpy(timing[i], &trk[i].p[P_SLEN], sizeof timing[i]);
        memcpy(sounds[i], trk[i].p, sizeof sounds[i]);
    }
    chain_config.count = 2;
    chain_config.row[0] = chain_row_of(0, 2);
    chain_config.row[1] = chain_row_of(3, 1);
    bad += check("SONG missing source: refuses before changing any track", chain_prepare() == 6 && !chain.armed &&
        !memcmp(trk[0].step, before[0], sizeof before[0]));
    chain_config.row[1] = chain_row_of(1, 1);
    project_save(2);
    chain_defaults(&chain_config);
    project_load(2);
    bad += check("SONG rows saved and loaded with their project", chain_config.count == 2 &&
        chain_config.row[0].repeat == 2 && !memcmp(&chain_config.row[1], &(chain_row_t){{1, 1, 1, 1}, 1}, sizeof(chain_row_t)));
    song.rec = 3;
    bad += check("SONG prepares while stopped, no starts over a pending start", chain_prepare() == 0 && chain_prepare() == 2);
    events_block(n);
    bad += check("SONG starts all tracks at source step 0, recording paused", chain.running && song.playing &&
        !song.rec && trk[0].seq_idx == 0 && step_note(&seq_steps(&trk[0])[0], 0) == 60 && trk[0].seq_notes[0] == 60);
    period = div_samples((uint32_t)trk[0].p[P_SDIV]);
    events_block(period);
    events_block(period);
    bad += check("SONG first row repeats without a gap", chain.row == 0 && chain.remaining == 1 && trk[0].seq_idx == 0);
    events_block(period);
    events_block(period);
    ok = chain.row == 1 && chain.remaining == 1;
    for (i = 0; i < NTRK; i++) {
        ok &= trk[i].seq_idx == 0 && trk[i].p[P_SLEN] == 3 && step_note(&seq_steps(&trk[i])[0], 0) == 65;
        for (k = 0; k < P_COUNT; k++)
            if (k < P_SLEN || k > P_SGATE) ok &= trk[i].p[k] == sounds[i][k];
    }
    bad += check("SONG row change: four tracks together, sounds unchanged", ok);
    go_seq_step();
    turn(EN_K2, 1); press(B_EDIT); hold(B_REC); hold(B_SAVE);
    bad += check("SONG playing: step edits, clears, recording and undo are blocked", !memcmp(trk[0].step, before[0], sizeof before[0]) && !ui.confirm);
    events_block(period);
    events_block(period);
    events_block(period);
    ok = !song.playing && !chain.running && song.rec == 3;
    for (i = 0; i < NTRK; i++) ok &= !trk[i].seq_n && !memcmp(trk[i].step, before[i], sizeof before[i]) &&
        !memcmp(&trk[i].p[P_SLEN], timing[i], sizeof timing[i]);
    bad += check("SONG end: stops and restores editable patterns, timing and record arms", ok);
    bad += check("SONG source projects stay unchanged", project_used(0) && project_used(1) && stored_note(0, 0, 0) == 60);
    chain_config.row[0].repeat = 1;
    chain_prepare(); events_block(n);
    trk[0].seq_idx = 1;
    last = step_samples(&trk[0], div_samples((uint32_t)trk[0].p[P_SDIV]), 1);
    trk[0].seq_pos = last - n + 19u;
    events_block(n);
    ok = chain.row == 1;
    for (i = 0; i < NTRK; i++) ok &= trk[i].seq_pos == 19u && trk[i].seq_idx == 0;
    bad += check("SONG transition preserves fractional block time on all tracks", ok);
    transport_req = 2; events_block(n);
    bad += check("SONG manual STOP restores the previous pattern", !chain.running && !song.playing && !memcmp(trk[0].step, before[0], sizeof before[0]));
    chain_prepare(); events_block(n);
    project_load(0);
    bad += check("PROJECT load during SONG stops it before loading new timing", !chain.running && !song.playing &&
        trk[0].p[P_SLEN] == 2 && step_note(&trk[0].step[0], 0) == 60 && !chain_config.count);
    ui_power_on();
    open_family(FAM_GLO);
    for (k = 0; k < NPAGES && cur_page()->graph != GR_SONG; k++) open_family(FAM_GLO);
    press(B_PLAY);
    bad += check("SONG page (GLO's, 1.2) empty PLAY explains how to start", msg_is("ADD A SECTION") && !transport_req &&
                 cur_page()->fam == FAM_GLO);
    turn(EN_K3, 1);
    bad += check("SONG PAT knob (K3) on + ADD adds the first section: every track on A, one repeat",
                 chain_config.count == 1 && !memcmp(&chain_config.row[0], &(chain_row_t){{0, 0, 0, 0}, 1}, sizeof(chain_row_t)));
    turn(EN_K1, 1); turn(EN_K3, 1); turn(EN_K3, 1); turn(EN_K4, 2);
    bad += check("SONG section, pat and repeat knobs build a chain (the new section a copy, T1 B)",
                 chain_config.count == 2 && ui.song_row == 1 && !ui.song_trk &&
                 !memcmp(&chain_config.row[1], &(chain_row_t){{1, 0, 0, 0}, 3}, sizeof(chain_row_t)));
    turn(EN_K2, 2); turn(EN_K3, 9);
    bad += check("SONG K2 picks the lane (T3), K3 runs A..D then - (silent)", ui.song_trk == 2 &&
                 !memcmp(&chain_config.row[1], &(chain_row_t){{1, 0, CHAIN_SILENT, 0}, 3}, sizeof(chain_row_t)));
    turn(EN_K2, 9); turn(EN_K3, -2);
    bad += check("SONG K2 stays in T1..T4, K3 back from - to C", ui.song_trk == 3 && chain_config.row[1].slot[3] == 0 &&
                 chain_config.row[1].slot[2] == CHAIN_SILENT);
    turn(EN_K3, 2);
    bad += check("  .. on T4: C", chain_config.row[1].slot[3] == 2);
    ui.song_trk = 1;
    k = key_at(0, 3);                                /* B3: the white key place 3, slot B; held over an audio block */
    fm1_in.notes |= 1u << k; host_notes |= 1u << k; frame(); events_block(32);
    ok = kb_note[k] == KB_SILENT && song.grid == 3u;
    fm1_in.notes &= ~(1u << k); frame(); events_block(32);
    bad += check("SONG white key B (B3) sets the cell's slot, silently; its LED lit", ok && chain_config.row[1].slot[1] == 1 &&
                 (key_leds(0) >> k & 1u));
    k = key_at(0, 1);                                /* G3: not one of A..D, it plays */
    fm1_in.notes |= 1u << k; host_notes |= 1u << k; frame(); events_block(32);
    ok = kb_note[k] != KB_SILENT;
    fm1_in.notes &= ~(1u << k); frame(); events_block(32);
    bad += check("SONG: the other keys play as anywhere", ok && chain_config.row[1].slot[1] == 1);
    turn(EN_K1, 1); tap_key(key_at(0, 4));           /* (C4 on + ADD: a copy of section 2, its T2 on C) */
    bad += check("SONG white key C (C4) on + ADD adds a section, a copy with the cell on C", chain_config.count == 3 &&
                 !memcmp(&chain_config.row[2], &(chain_row_t){{1, 2, CHAIN_SILENT, 2}, 3}, sizeof(chain_row_t)));
    chain_config.count = 2; ui.song_row = 1; ui.song_trk = 0;
    turn(EN_K4, 30);
    project_save(0);
    chain_config.count = 1;
    chain_config.row[0] = chain_row_of(0, 1);
    fm1_in.buttons |= (1u << panel.btn[B_OCTUP]) | (1u << panel.btn[B_OCTDN]);
    host_pressed |= fm1_in.buttons; frame();
    fm1_in.buttons = 0; frame();
    bad += check("SONG OCT chord does not start playback or leave the page", !transport_req && !ui.home);
    press(B_OCTUP);
    bad += check("SONG OCT+ on release starts, pending start blocks SAVE", chain.armed && transport_req == 1);
    project_save(3);
    bad += check("SONG pending start cannot write a project", msg_is("STOP TO SAVE") && !project_used(3));
    press(B_PLAY); events_block(32);
    bad += check("SONG PLAY cancels a pending start", !chain.running && !chain.armed && !song.playing);
    uint32_t old_count = chain_config.count;
    ui.song_row = 0;
    turn(EN_K4, 30);
    bad += check("SONG K4 on a section: its repeats (at most 16), never the count", chain_config.count == old_count &&
                 chain_config.row[0].repeat == 16);
    for (k = chain_config.count; k < CHAIN_ROWS; k++) { turn(EN_K1, 1); turn(EN_K3, 1); }
    bad += check("SONG append row stays bounded at 16", chain_config.count == CHAIN_ROWS && chain_valid(&chain_config));
    go_title("CLEAR"); frame();
    key_down(white(9)); frame(); key_up(white(9)); frame();   /* A4 SONG (SONG col) */
    bad += check("CLEAR SONG requires explicit confirmation", ui.confirm == CF_CLEAR_SONG && chain_config.count == CHAIN_ROWS);
    press(B_OCTDN);
    bad += check("cancel preserves the full song", chain_config.count == CHAIN_ROWS);
    frame();
    key_down(white(9)); frame(); key_up(white(9)); frame();
    press(B_OCTUP);
    bad += check("confirmed CLEAR SONG keeps musical steps", !chain_config.count && !ui.song_row);
    return bad;
}

/* SONG's timeline (ui_graph.c song_layout / song_follow / graph_song): widths by bars, the least width, + ADD's slot,
 * the view following the cursor and the playing row, the styles of a slot not saved and of + ADD */
static int sg_seen(const song_lay_t *L, uint32_t r)  /* row r's block (r == n: + ADD's) wholly in the view */
{
    int32_t a = L->x[r], b = r < L->n ? L->x[r + 1u] - SG_GAP : L->x[r] + SG_ADDW;
    return a >= sgv.off && b <= sgv.off + SG_W;
}
static uint16_t sg_px(int32_t x, int32_t y) { return swap16(host_screen[(uint32_t)(Y_GRAPH + y) * 240u + (uint32_t)x]); }
static int test_song_timeline(void)
{
    song_lay_t L;
    uint32_t k, r;
    int bad = 0, ok;
    int32_t w0, w1, w2, x;
    ui_power_on();
    trk[0].p[P_SDIV] = 2;                            /* 1/16 */
    trk[0].p[P_SLEN] = 16; project_save(0);          /* A: 1 bar */
    trk[0].p[P_SLEN] = 32; project_save(1);          /* B: 2 bars */
    trk[0].p[P_SLEN] = 1; trk[0].p[P_SDIV] = 3; project_save(2);   /* C: one 1/32, 1/32 bar */
    fm1_ms += 1000u;                                 /* (the slots read again) */
    chain_config.count = 3;
    chain_config.row[0] = chain_row_of(0, 1); chain_config.row[1] = chain_row_of(1, 2); chain_config.row[2] = chain_row_of(0, 1);
    ui.song_row = 0;
    go_page(GR_SONG); frame();
    song_layout(&L);
    w0 = L.x[1] - L.x[0]; w1 = L.x[2] - L.x[1]; w2 = L.x[3] - L.x[2];
    bad += check("SONG timeline: 1 + 4 + 1 bars fitted, the widths by bars, + ADD's slot at the end",
                 L.fit && L.add && L.u0[3] == 6u * SG_BARU && L.x[3] == SG_W - SG_ADDW && L.x[4] == SG_W &&
                 6 * w1 >= 4 * (SG_W - SG_ADDW) - 6 && 6 * w1 <= 4 * (SG_W - SG_ADDW) + 6 && w0 - w2 <= 1 && w2 - w0 <= 1);
    chain_config.row[1] = chain_row_of(2, 1);       /* the 1/32 bar between two of 1 bar x 16 */
    chain_config.row[0].repeat = chain_config.row[2].repeat = 16;
    song_layout(&L);
    w0 = L.x[1] - L.x[0]; w1 = L.x[2] - L.x[1];
    bad += check("SONG timeline: a block far shorter than the rest keeps the least width (its letter)",
                 L.fit && w1 >= SG_MINW && w1 <= SG_MINW + 1 && L.x[3] == SG_W - SG_ADDW && w0 > 80);
    chain_config.count = CHAIN_ROWS;                 /* 16 rows of 2 bars x 16: 512 bars, scrolled */
    for (k = 0; k < CHAIN_ROWS; k++) chain_config.row[k] = chain_row_of(1, 16);
    song_layout(&L);
    bad += check("SONG timeline: a long song scrolls (6 px a bar), no + ADD at 16 rows",
                 !L.fit && !L.add && L.x[1] - L.x[0] == 32 * SG_BAR && L.x[L.n + 1u] == L.x[L.n]);
    ok = 1;
    for (r = 0; r < CHAIN_ROWS; r += 3u) {
        ui.song_row = (uint8_t)r; frame();
        ok &= sg_seen(&L, r);
    }
    ui.song_row = 0; frame();
    ok &= sgv.off == 0 && (ui_prefs & PREF_ANIM_OFF);
    bad += check("SONG timeline: ANIM always OFF, the cursor's block in view at once, every row", ok);
    turn(EN_K1, 15);                                 /* (KNOB 1: the last row) */
    ok = ui.song_row == 15u && sg_seen(&L, 15) && sgv.off == L.x[L.n + 1u] - SG_W;   /* snap, no glide */
    bad += check("SONG timeline: the view snaps to the cursor's block (no glide)", ok);
    chain_config.count = 12;                         /* + ADD's slot followed too */
    song_layout(&L);
    ui.song_row = 12; frames(800);
    bad += check("SONG timeline: the cursor on + ADD (after 12 rows): its slot in view", L.add && sg_seen(&L, 12));
    /* playing: the playing row followed, the cursor after it moves */
    for (k = 0; k < 12u; k++) chain_config.row[k] = chain_row_of(0, 16);  /* 16 bars each (A saved): scrolled */
    song_layout(&L);
    ui.song_row = 0; frames(800);
    chain_prepare(); events_block(32);
    for (k = 0; k < 4000u && chain.running && chain.row < 9u; k++) events_block(div_samples(2));
    frames(800);
    bad += check("SONG timeline: playing row 10 far from the cursor (row 1): the playing block in view",
                 chain.running && chain.row == 9u && ui.song_row == 0u && sg_seen(&L, 9) && !sg_seen(&L, 0) &&
                 song_playx(&L) >= L.x[9] && song_playx(&L) < L.x[10]);
    turn(EN_K1, 1); frames(800);
    bad += check("SONG timeline: the cursor moved while playing: its block in view", ui.song_row == 1u && sg_seen(&L, 1));
    seq_stop(); chain_stop(); frames(100);
    /* styles: a slot not saved outlined (DIM), the selected one filled (THEME), the others RAISE; + ADD dashed */
    chain_config.count = 3;
    chain_config.row[0] = chain_row_of(0, 1); chain_config.row[1] = chain_row_of(3, 1); chain_config.row[2] = chain_row_of(0, 1);
    ui.song_row = 0; fm1_ms += 1000u; go_page(GR_SONG); frames(100);
    song_layout(&L);
    x = SG_X0 + L.x[1] + 4;
    ok = sg_px(SG_X0 + L.x[0] + 4, SG_Y + SG_LH - 2) == T_THEME && sg_px(SG_X0 + L.x[2] + 4, SG_Y + SG_LH - 2) == T_RAISE;
    ok &= sg_px(x, SG_Y) == T_DIM && sg_px(x, SG_Y + SG_LH - 3) == T_SURF && sg_px(SG_X0 + L.x[1], SG_Y + 3) == T_DIM;
    bad += check("SONG timeline: selected THEME, others RAISE, a slot not saved only outlined (DIM)", ok);
    x = SG_X0 + L.x[3];
    bad += check("SONG timeline: + ADD's slot dashed (DIM), THEME when selected",
                 sg_px(x, SG_Y) == T_DIM && sg_px(x + 1, SG_Y) == T_DIM && sg_px(x + 2, SG_Y) == T_SURF &&
                 sg_px(x + 4, SG_Y) == T_DIM && sg_px(x + 4, SG_Y + 3) == T_SURF);
    ui.song_row = 3; frames(100);
    bad += check("SONG timeline: + ADD selected", sg_px(x, SG_Y) == T_THEME && sg_px(x + 2, SG_Y) == T_SURF);
    chain_defaults(&chain_config);
    ui.song_row = 0;
    return bad;
}

/* Product controls are exercised through the actual button/encoder handlers
 * and canvas, including all pages, both fonts and every shipped palette. */
/* Discussions #92 / #94: the PRESETS knob on every page. DETAIL and the DRUM grid: the step cursor. The piano
 * roll: the note under the cursor moves in time (the sound stays). AUTOMATION, USER, PROJECT, PHRASES, SONG: the
 * selection, as KNOB 1; TOOLS: nothing; HOME, PRESETS and every other page: the next sound, the steps untouched, SAVE
 * held undoes it */
static int test_presets_knob(void)
{
    int bad = 0, cur_ok = 1, sel_ok = 1, snd_ok = 1, undo_ok = 1, tools_ok = 1, grid_ok;
    uint32_t i, n_snd = 0, n_cur = 0;
    for (i = 0; i < NPAGES; i++) {
        track_t before;
        uint32_t g = PAGES[i].graph, uslot, ppick;
        int16_t slot;
        ui_power_on();
        song.sel = 0;
        my_steps(TSEL);
        ui.home = 0; ui.page = (uint8_t)i; page_entered();
        if (!page_visible(i))
            continue;
        frame();
        before = *TSEL;
        uslot = ui.uslot; ppick = pat_pick(); slot = song.g[G_SLOT];
        turn(EN_PRESET, 1);
        {
            int same_snd = TSEL->preset == before.preset && TSEL->eng_req == before.eng_req &&
                           !memcmp(TSEL->p, before.p, sizeof before.p);
            int same_steps = !memcmp(TSEL->step, before.step, sizeof before.step);
            if (g == GR_ROLL && str_eq(PAGES[i].title, "DETAIL")) {
                cur_ok &= ui.cursor == 1u && same_snd && same_steps && ui.page == i;
                n_cur++;
            } else if (g == GR_ROLL) {                 /* STEP: the note at the cursor moves one step, the cursor with it */
                cur_ok &= ui.cursor == 1u && same_snd && ui.page == i && step_note(&TSEL->step[1], 0) == step_note(&before.step[0], 0) &&
                          TSEL->step[1].time == ST_NOTE && TSEL->step[0].time == ST_REST && !TSEL->step[0].n;
                n_cur++;
            } else if (g == GR_USER) {
                sel_ok &= ui.uslot == uslot + 1u && same_snd && same_steps;
            } else if (g == GR_SLOTS) {
                sel_ok &= song.g[G_SLOT] == slot + 1 && same_snd && same_steps;
            } else if (g == GR_PATS) {
                sel_ok &= pat_pick() == ppick + 1u && same_snd && same_steps;
            } else if (g == GR_SONG) {
                sel_ok &= same_snd && same_steps && ui.page == i;
            } else if (g == GR_EVENTS) {               /* (AUTOMATION: its row, PLAY's to the first record's) */
                sel_ok &= same_snd && same_steps && ui.page == i && ui.ev_row == 1u;
            } else if (g == GR_CLEAR) {
                tools_ok &= same_snd && same_steps && !ui.confirm;
            } else if (g == GR_PUNCH) {                 /* Punch-in sticky: PRESETS does nothing (no reassign, no load) */
                sel_ok &= same_snd && same_steps && ui.page == i;
            } else {                                 /* EFFECTS included: PRESETS loads a sound (also test_fx_sections) */
                snd_ok &= !same_snd && same_steps && ui.page == i && !ui.home;
                hold(B_SAVE);
                undo_ok &= TSEL->preset == before.preset && TSEL->eng_req == before.eng_req &&
                           !memcmp(TSEL->p, before.p, sizeof before.p) && !memcmp(TSEL->step, before.step, sizeof before.step);
                n_snd++;
            }
        }
    }
    bad += check("#92 PRESETS on STEP moves the note; on DETAIL, the cursor; the sound stays", cur_ok && n_cur == 2u);
    bad += check("#94 PRESETS on USER, PROJECT, PHRASES, SONG, AUTOMATION: the selection (KNOB 1's); nothing loaded", sel_ok);
    bad += check("#94 PRESETS on CLEAR does nothing", tools_ok);
    bad += check("#94 PRESETS elsewhere (EDIT, ENV, LFO, FX, SCL, ARP, MIXER, GLOBAL, ...): the next sound, the steps stay",
                 snd_ok && n_snd >= 15u);
    bad += check("#94   SAVE held undoes each of those loads", undo_ok);
    ui_power_on();
    set_engine_of(TSEL, ENGI_DRUM);
    TSEL->engine = TSEL->eng_req;
    TSEL->p[P_SLEN] = 32;
    go_page(GR_ROLL);
    frame();
    turn(EN_PRESET, 17);
    grid_ok = grid_on() && ui.cursor == 17u && ui.bank == 1u && TSEL->eng_req == ENGI_DRUM;
    turn(EN_PRESET, -18);
    grid_ok &= ui.cursor == 31u;
    bad += check("#92 the DRUM grid: PRESETS moves the cursor (and the page), as KNOB 1; wraps", grid_ok);
    ui_power_on();
    hold(B_HOME);
    i = ui.menu_sel;
    turn(EN_PRESET, 1);
    bad += check("#94 the menu: PRESETS leaves the selection alone", ui.menu == 1 && ui.menu_sel == i);
    turn(EN_ALGO, 1);
    bad += check("#94 the menu: ALGORITHM moves the selection", ui.menu == 1 && ui.menu_sel == i + 1u);
    return bad;
}

/* Pages by button. HOME: Home ↔ Mixer; SEQ: step group → Automation → Tools;
 * GLO: Master → Clock → Song; SAVE: Presets → Project → Clear.
 * AUTOMATION's list edits a step's CHANCE / RATCH (the step's own fields, nothing else), one undo each;
 * re-enter from elsewhere always the family's first page */
static int steps_but(const track_t *t, const step_t *ref, uint32_t at, const step_t *want)   /* the steps = ref, at: want */
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (memcmp(&t->step[i], i == at ? want : &ref[i], sizeof ref[i]))
            return 0;
    return 1;
}
static uint32_t auto_cur(void) { uint16_t rw[EV_ROWS]; return ev_cur(rw, 0); }
static int test_page_cycles(void)
{
    static const char *const PRE_CYC[3] = {"PRESETS", "USER", "PHRASES"};
    int bad = 0, ok = 1;
    uint32_t i, n0;
    step_t ref[NSTEP], want;
    int16_t p0[P_COUNT];
    uint16_t rw[EV_ROWS];
    /* SAVE: SELECT walks inside Presets; button jumps Project, then Clear */
    ui_power_on();
    press(B_SAVE); frames(320);
    ok = !ui.home && str_eq(cur_page()->title, "PRESETS");
    for (i = 1; i < 3u; i++) {
        turn(EN_SELECT, 1); frames(100);
        ok &= !ui.home && cur_page()->fam == FAM_SAVE && str_eq(cur_page()->title, PRE_CYC[i]) &&
              cur_page()->sec == SAVE_SEC_PRE;
    }
    turn(EN_SELECT, 1); frames(100);
    ok &= str_eq(cur_page()->title, "PHRASES");         /* stop at last Presets page */
    press(B_SAVE); frames(100);
    ok &= str_eq(cur_page()->title, "PROJECT") && cur_page()->sec == SAVE_SEC_PROJ;
    turn(EN_SELECT, 1); frames(100);
    ok &= str_eq(cur_page()->title, "PROJECT");         /* one page in Project */
    press(B_SAVE); frames(100);
    ok &= str_eq(cur_page()->title, "CLEAR") && cur_page()->sec == SAVE_SEC_CLEAR && ui.lock == LAYER_CLR;
    bad += check("SAVE: SELECT walks Presets PRESETS>USER>PHRASES; button → Project → Clear", ok);
    ok = 1;
    for (i = 0; i < NPAGES; i++) {
        uint32_t f = PAGES[i].fam;
        ok &= f != FAM_SEQ || PAGES[i].graph == GR_ROLL || PAGES[i].graph == GR_EVENTS ||
              PAGES[i].graph == GR_STEPS || PAGES[i].graph == GR_GENERATION;
        ok &= f != FAM_GLO || PAGES[i].graph == GR_SONG ||
              str_eq(PAGES[i].title, "CLOCK") || str_eq(PAGES[i].title, "MASTER");
        ok &= f != FAM_TRK || PAGES[i].graph == GR_TRK;
        ok &= PAGES[i].graph != GR_TRK || f == FAM_TRK;
        ui_power_on(); ui.home = 0; ui.page = (uint8_t)i; page_entered();
        ok &= FAM_BTN[cur_fam()] == (f == FAM_TRK ? B_HOME : f == FAM_GLO ? B_GLO : f == FAM_SEQ ? B_SEQ :
                                    f == FAM_SAVE ? B_SAVE : FAM_BTN[f]);
    }
    for (i = 0; i < NPAGES; i++)
        ok &= !str_eq(PAGES[i].title, "CHANCE") && !str_eq(PAGES[i].title, "AUTO LIST");
    bad += check("SEQ has PATTERN/STEP/DETAIL/AUTOMATION; GLO has MASTER/SONG/CLOCK; MIXER is HOME's", ok);
    {   /* GLOBAL and SYSTEM are gone; BPM SWG CLK on GLO > CLOCK, TUNE and ROUT on no page (the MENU's) */
        uint32_t k, clock = 0;
        ok = 1;
        for (i = 0; i < NPAGES; i++) {
            ok &= !str_eq(PAGES[i].title, "GLOBAL") && !str_eq(PAGES[i].title, "SYSTEM");
            for (k = 0; k < 4u && PAGES[i].scope == SC_GLOBAL; k++)
                ok &= PAGES[i].id[k] != G_TUNE && PAGES[i].id[k] != G_ROUTE;   /* G_DREL (was G_SYNC) lives on MASTER */
            if (str_eq(PAGES[i].title, "CLOCK"))
                clock = i + 1u;
        }
        ok &= clock && PAGES[clock - 1u].fam == FAM_GLO && PAGES[clock - 1u].scope == SC_GLOBAL &&
              PAGES[clock - 1u].id[0] == G_BPM && PAGES[clock - 1u].id[1] == G_SWING && PAGES[clock - 1u].id[2] == G_CLOCK &&
              PAGES[clock - 1u].id[3] == 0xFFu;
        bad += check("no GLOBAL, no SYSTEM page; GLO > CLOCK has BPM SWG CLK (three knobs), TUNE and ROUT on no page", ok);
    }
    /* re-enter from elsewhere: always the family's first section/page (Punch-in → EFFECTS; ENV DEST → ENV);
     * in-family button still cycles; HOME → MIXER; in-page cursors stay */
    ui_power_on();
    go_title("PUNCH-IN");
    ok = cur_page()->sec == FX_SEC_PUNCH && ui.lock == LAYER_FX;
    open_family(FAM_SEQ);
    ok &= str_eq(cur_page()->title, "STEP") || cur_page()->graph == GR_ROLL;
    open_family(FAM_FX);
    ok &= str_eq(cur_page()->title, "EFFECTS") && cur_page()->sec == FX_SEC_INS && !ui.lock;
    go_title("ENV DEST");
    open_family(FAM_SEQ);
    open_family(FAM_ENV);
    ok &= str_eq(cur_page()->title, "ENV");
    go_title("CLEAR");
    open_family(FAM_GLO);
    open_family(FAM_SAVE);
    ok &= str_eq(cur_page()->title, "PRESETS");
    go_home(); open_family(FAM_GLO); ok &= str_eq(cur_page()->title, "MASTER");
    open_family(FAM_GLO); ok &= str_eq(cur_page()->title, "CLOCK"); /* section cycle */
    open_family(FAM_GLO); ok &= str_eq(cur_page()->title, "SONG");
    open_family(FAM_GLO); ok &= str_eq(cur_page()->title, "MASTER"); /* wrap */
    go_title("EFFECTS");
    ui.ior_sel = 2;
    open_family(FAM_SEQ);
    open_family(FAM_FX);
    ok &= str_eq(cur_page()->title, "EFFECTS") && ui.ior_sel == 2u; /* page resets; list cursor stays */
    for (i = 0; i < 256u; i++) {                         /* fam_last ignored on re-enter */
        go_home(); ui.fam_last[FAM_GLO] = (uint8_t)i; open_family(FAM_GLO);
        ok &= str_eq(cur_page()->title, "MASTER") && ui.fam_last[FAM_GLO] == ui.page;
        go_home(); ui.fam_last[FAM_TRK] = (uint8_t)i; press(B_HOME);
        ok &= cur_page()->graph == GR_TRK && !ui.home;
    }
    bad += check("re-enter family opens its first page (FX Effects, ENV, SAVE PRESETS); GLO cycles "
                 "Master → Clock → Song; HOME always the MIXER first; EFFECTS list cursor kept", ok);

    /* AUTOMATION: + ADD CHANCE / RATCH, the knobs, EDIT: the step's fields only */
    ui_power_on();
    my_steps(TSEL);
    memcpy(ref, TSEL->step, sizeof ref);
    memcpy(p0, TSEL->p, sizeof p0);
    go_auto_top(); frame();
    n0 = ev_list(rw);
    ok = n0 == 3u && rw[0] == EVC(EVK_TOP, 0) && rw[1] == EVC(EVK_QNT, 0) && rw[2] == EVC(EVK_ADD, 0);
    go_auto_add(EV_CHANCE, 3); frame();
    press(B_OCTUP);
    want = ref[3]; step_set_chance(&want, 50);
    ok &= steps_but(TSEL, ref, 3, &want) && auto_cur() == EVC(EVK_CHANCE, 3) && msg_is("CHANCE ADDED") && !motion_count(TSEL);
    bad += check("AUTOMATION + ADD CHANCE: step 4's CHANCE 50 %, nothing else; its row selected", ok);
    turn(EN_K4, 10);
    want = ref[3]; step_set_chance(&want, 60);
    ok = steps_but(TSEL, ref, 3, &want);
    turn(EN_K2, 1);                                     /* to step 5 (at its default): step 4 back to 100 % */
    want = ref[4]; step_set_chance(&want, 60);
    ok &= steps_but(TSEL, ref, 4, &want) && auto_cur() == EVC(EVK_CHANCE, 4);
    bad += check("  KNOB 4 the CHANCE, KNOB 2 moves it to the next step (the old one back to 100 %)", ok);
    hold(B_SAVE);
    want = ref[3]; step_set_chance(&want, 60);
    ok = steps_but(TSEL, ref, 3, &want) && !memcmp(TSEL->p, p0, sizeof p0);
    hold(B_SAVE);
    want = ref[4]; step_set_chance(&want, 60);
    ok &= steps_but(TSEL, ref, 4, &want) && !memcmp(TSEL->p, p0, sizeof p0);
    bad += check("  SAVE held: the move undone (the sound and LEN untouched); held again: redone", ok);
    turn(EN_K4, 1000);                                  /* back at 100 %: the row stays while edited */
    ok = steps_but(TSEL, ref, 0xFFu, 0) && auto_cur() == EVC(EVK_CHANCE, 4);
    turn(EN_K1, -1);                                    /* (another row: it leaves the list) */
    ok &= auto_cur() == EVC(EVK_QNT, 0) && ev_list(rw) == 3u;
    bad += check("  at 100 % it stays listed while edited; another row picked: gone (nothing stored)", ok);
    memcpy(ref, TSEL->step, sizeof ref);
    go_auto_add(EV_RATCH, 6); frame();
    press(B_OCTUP);
    turn(EN_K4, 1); turn(EN_K4, 1); turn(EN_K4, 1);     /* x2 -> x4 (the end holds) */
    want = ref[6]; step_set_ratchet(&want, 4);
    ok = steps_but(TSEL, ref, 6, &want) && auto_cur() == EVC(EVK_RATCH, 6) && step_chance(&TSEL->step[6]) == 100u;
    hold(B_SAVE);                                       /* the turn (one undo): x2 again */
    want = ref[6]; step_set_ratchet(&want, 2);
    ok &= steps_but(TSEL, ref, 6, &want);
    hold(B_SAVE);
    press(B_EDIT);                                      /* EDIT: back to x1 */
    ok &= steps_but(TSEL, ref, 0xFFu, 0) && msg_is("DELETED");
    hold(B_SAVE);
    want = ref[6]; step_set_ratchet(&want, 4);
    ok &= steps_but(TSEL, ref, 6, &want);
    bad += check("AUTOMATION RATCH: + ADD x2, KNOB 4 to x4, EDIT x1; each one undo (SAVE held), the step's flags only", ok);
    {   /* the order: PLAY, QUANTIZE, then by step (CHANCE, RATCH, NUDGE, the records), + ADD; a lock on the same step
         * after them */
        go_auto_add(EV_CHANCE, 6); frame(); press(B_OCTUP);
        go_auto_add(EV_NUDGE, 6); frame(); press(B_OCTUP);
        motion_set_lock(TSEL, 6, P_ATK, 3);
        motion_set_lock(TSEL, 2, P_ATK, 5);
        n0 = ev_list(rw);
        ok = n0 == 8u && rw[0] == EVC(EVK_TOP, 0) && rw[1] == EVC(EVK_QNT, 0) && rw[2] >> 8 == EVK_REC &&
             rw[3] == EVC(EVK_CHANCE, 6) && rw[4] == EVC(EVK_RATCH, 6) && rw[5] == EVC(EVK_NUDGE, 6) &&
             rw[6] >> 8 == EVK_REC && rw[7] == EVC(EVK_ADD, 0);
        go_auto_add(EV_CHANCE, 6); frame(); press(B_OCTUP);
        ok &= msg_is("ALREADY LISTED") && auto_cur() == EVC(EVK_CHANCE, 6);
        bad += check("  the rows: PLAY, QUANTIZE, by step (CHANCE, RATCH, NUDGE, then its locks / events), + ADD; one there: "
                     "ALREADY LISTED", ok);
        auto_pick(EVC(EVK_NUDGE, 6));
        press(B_EDIT);                                  /* (the NUDGE out again: the rest as before) */
        ok = step_nudge(&TSEL->step[6]) == 0;
        bad += check("  EDIT on the NUDGE row: back to 0", ok);
    }
    memcpy(ref, TSEL->step, sizeof ref);
    chain.running = 1;
    auto_pick(EVC(EVK_CHANCE, 6));
    turn(EN_K4, 5); press(B_EDIT);
    ok = steps_but(TSEL, ref, 0xFFu, 0) && msg_is("STOP TO EDIT");
    chain.running = 0;
    bad += check("  while a song plays: STOP TO EDIT, nothing changes", ok);
    return bad;
}

static int led_of(const uint8_t *l, uint32_t id);
static int test_product_ux(void)
{
    int bad = 0, ok = 1;
    uint32_t i, p, b;
    for (i = 0; i < NPAGES; i++) {
        ui_power_on(); project_not_empty(); set_engine_of(TSEL, 1);
        ui.home = 0; ui.page = (uint8_t)i; page_entered();
        if (!page_visible(i))
            continue;
        press(B_REC);
        if (PAGES[i].graph == GR_SONG)
            ok &= song.rec == 0 && !transport_req && msg_is("[SEQ] TO RECORD");
        else if (PAGES[i].graph == GR_ROLL)            /* STEP / DETAIL: step recording when not empty project */
            ok &= song.rec == 1 && !transport_req && ui.page == i && !ui.home;
        else ok &= song.rec == 1 && (transport_req == 1 || msg_is("PLAY FREELY")) && ui.page == i && !ui.home;
    }
    bad += check("REC stays on every page, SONG stopped asks for pattern recording (STEP: step recording, stopped)", ok);
    ui_power_on(); hold(B_HOME); press(B_REC);
    bad += check("REC in the MENU does nothing (the menu stays, no arm, no transport start)",
                 ui.menu == 1 && song.rec == 0 && !transport_req);
    ui.menu_sel = MI_ABOUT; press(B_OCTUP); press(B_REC);
    bad += check("REC in ABOUT does nothing (the document and its scroll stay)",
                 ui.menu == 2 && !ui.menu_scroll && song.rec == 0 && !transport_req);
    ui_power_on(); press(B_HOME);
    bad += check("HOME tapped on HOME opens the four-channel MIXER", cur_page()->graph == GR_TRK && !ui.home &&
                 cur_fam() == FAM_TRK && FAM_BTN[cur_fam()] == B_HOME);
    int16_t len = TSEL->p[P_SLEN], send = TSEL->p[P_REV];
    turn(EN_K3, 1);
    bad += check("MIXER K3 edits reverb, never sequence length", TSEL->p[P_SLEN] == len && TSEL->p[P_REV] == send + 1);
    press(B_HOME);
    ok = ui.home && !ui.menu;
    press(B_HOME);
    ok &= cur_page()->graph == GR_TRK;
    go_title("ENV"); press(B_HOME);
    ok &= ui.home;
    go_title("CLOCK"); press(B_HOME);
    ok &= ui.home;
    bad += check("HOME cycles Home ↔ Mixer; from any other page HOME", ok);
    hold(B_HOME);
    bad += check("HOME held still opens the menu", ui.menu == 1);
    ui_power_on();
    frames(320);                                       /* (taps further apart than a double tap: #83) */
    press(B_GLO); frames(320); ok = cur_page()->fam == FAM_GLO && str_eq(cur_page()->title, "MASTER");
    press(B_GLO); frames(320); ok &= str_eq(cur_page()->title, "CLOCK") && cur_page()->fam == FAM_GLO;
    {   /* CLOCK: GLO's LED lit; HOME's not */
        uint8_t pos0[sizeof led_pos];
        ui_leds();
        memcpy(pos0, led_pos, sizeof pos0);
        memset(led_pos, 0xFF, sizeof led_pos);
        led_pos[panel.btn[B_HOME]] = 0u << 3 | 1u;
        led_pos[panel.btn[B_GLO]] = 0u << 3 | 2u;
        frame(); ui_leds();
        ok &= led_of(fm1_led, panel.btn[B_GLO]) && !led_of(fm1_led, panel.btn[B_HOME]);
        memcpy(led_pos, pos0, sizeof pos0);
    }
    {   /* CLOCK's knobs: BPM SWG CLK; KNOB 4 nothing */
        int16_t bpm = song.g[G_BPM], swg = song.g[G_SWING], clk = song.g[G_CLOCK], tune = song.g[G_TUNE];
        turn(EN_K1, 1); turn(EN_K2, 1); turn(EN_K3, 1); turn(EN_K4, 5);
        ok &= song.g[G_BPM] == bpm + 1 && song.g[G_SWING] == swg + 1 && song.g[G_CLOCK] != clk && song.g[G_TUNE] == tune;
        song.g[G_CLOCK] = clk;
    }
    press(B_GLO); frames(320); ok &= str_eq(cur_page()->title, "SONG") && cur_page()->fam == FAM_GLO;
    press(B_GLO); frames(320); ok &= str_eq(cur_page()->title, "MASTER") && !ui.lock;
    bad += check("GLO cycles Master → Clock → Song → Master", ok);
    go_home(); btn_down(B_SEQ); frames(500);
    ok = !ui.layer && ui.home;                          /* hold alone: no map */
    btn_up(B_SEQ); frame();
    bad += check("SEQ hold on HOME: no peek; release opens STEP",
                 ok && str_eq(cur_page()->title, "STEP") && !ui.lock && !ui.home);
    go_page(GR_SONG);
    song.rec = 1; chain.armed = 1; press(B_REC);
    bad += check("REC cannot write borrowed patterns while SONG is armed", song.rec == 1 && msg_is("STOP TO RECORD"));
    ui_power_on(); open_family(FAM_EDIT);
    ok = 1;
    for (i = 0; i < 8u; i++) { open_family(FAM_EDIT); ok &= page_visible(ui.page) && !(cur_page()->id[0] >= P_FM1_ATK && cur_page()->id[0] <= P_FM4_LEVEL); }
    bad += check("operator envelope pages are hidden on non-DIGITAL instruments", ok);
    {   /* ANALOG: OSC → FILTER → FILTER DRIVE (no EDIT 2 / FLT duplicate) */
        uint32_t saw_flt = 0, saw_drv = 0, saw_e2 = 0;
        set_engine_of(TSEL, 0);
        go_title("EDIT 1");
        ok = str_eq(ENGINES[TSEL->eng_req % NENGINES]->page_title[0], "OSC");
        for (i = 0; i < NPAGES; i++) {
            if (PAGES[i].fam != FAM_EDIT || PAGES[i].sec != EDIT_SEC_SND || !page_visible(i))
                continue;
            if (PAGES[i].scope == SC_ENGINE && PAGES[i].id[0] == P_E4) saw_e2 = 1;
            if (PAGES[i].id[0] == P_FTYPE) saw_flt = 1;
            if (str_eq(PAGES[i].title, "FILTER DRIVE")) saw_drv = 1;
        }
        bad += check("ANALOG EDIT sound: FILTER + FILTER DRIVE, no EDIT 2",
                     ok && saw_flt && saw_drv && !saw_e2);
        set_engine_of(TSEL, 6);                    /* TRIO */
        ok = 1;
        saw_e2 = 0;
        for (i = 0; i < NPAGES; i++) {
            if (PAGES[i].scope == SC_ENGINE && PAGES[i].id[0] == P_E4) {
                ok &= page_visible(i);
                saw_e2 = 1;
            }
        }
        bad += check("other engines still show EDIT 2", ok && saw_e2);
        set_engine_of(TSEL, 0);
    }
#if FELUCCA_FM4
    set_engine_of(TSEL, 1);
    go_title("EDIT 1");                                /* Sound section (EDIT button may have left Voice) */
    ok = 0;
    for (i = 0; i < NPAGES; i++) {                     /* SELECT walks Sound pages */
        if (cur_page()->id[0] == P_FM1_ATK) ok = 1;
        turn(EN_SELECT, 1); frame();
    }
    bad += check("DIGITAL exposes four envelopes and independent operator levels", ok);
    go_title("OP1 ENV"); turn(EN_K1, 4);
    bad += check("operator envelope K1 edits only OP1 attack", TSEL->p[P_FM1_ATK] == 4 && !TSEL->p[P_FM2_ATK]);
    track_select(2); frame();
    bad += check("changing to a non-FM track leaves the stale operator page", page_visible(ui.page) && cur_page()->id[0] == P_E0);
#else
    set_engine_of(TSEL, ENGI_DIGITAL); open_family(FAM_EDIT); ok = TSEL->eng_req == ENGI_FM6;
    for (i = 0; i < 12u; i++) { ok &= !(cur_page()->id[0] >= P_FM1_ATK && cur_page()->id[0] <= P_FM4_LEVEL); open_family(FAM_EDIT); }
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == FAM_EDIT && PAGES[i].id[0] >= P_FM1_ATK && PAGES[i].id[0] <= P_FM4_LEVEL)
            ok &= !page_visible(i);
    bad += check("DIGITAL retired: engine 1 asked for loads FM6; the OP ENV / OP LEVEL pages never show", ok);
#endif
    ui_power_on(); go_auto_add(EV_CHANCE, 0); press(B_OCTUP); turn(EN_K4, 15);
    bad += check("CHANCE is per step and starts at backward-compatible 100 percent", step_chance(&TSEL->step[0]) == 65 && step_chance(&TSEL->step[1]) == 100);
    turn(EN_K4, -1000); ok = step_chance(&TSEL->step[0]) == 0;
    turn(EN_K4, 1000); ok &= step_chance(&TSEL->step[0]) == 100;
    bad += check("CHANCE controls clamp to 0..100 without changing adjacent steps", ok);
    ui_power_on(); song.rec = 1; seq_start(); go_home();
    int16_t baseline = TSEL->p[P_E0]; turn(EN_K1, 1);
    bad += check("REC plus a sound knob records motion through the UI", motion_count(TSEL) == 1 && motion_enabled(TSEL));
    press(B_PLAY); events_block(32);
    bad += check("stopping returns the original sound after a recorded knob gesture", TSEL->p[P_E0] == baseline);
    go_auto_top(); turn(EN_K4, -1);
    bad += check("#93 the page is AUTOMATION (GR_EVENTS inside)", str_eq(cur_page()->title, "AUTOMATION"));
    bad += check("MOTION playback OFF preserves its stored events", !motion_enabled(TSEL) && motion_count(TSEL) == 1);
    press(B_OCTUP);
    bad += check("MOTION clear always asks confirmation", ui.confirm == CF_CLEAR_MOTION && motion_count(TSEL) == 1);
    {
        char qa[24], qb[24];
        confirm_text(qa, qb);
        bad += check("#93   the dialog asks CLEAR T1 AUTOMATION?", str_eq(qa, "CLEAR T1 AUTOMATION?"));
    }
    press(B_OCTDN); press(B_OCTUP); press(B_OCTUP);
    bad += check("confirmed MOTION clear removes events and SAVE hold restores them", motion_count(TSEL) == 0 &&
                 msg_is("AUTOMATION CLEARED"));
    hold(B_SAVE);
    bad += check("motion-clear UNDO restores the recorded events", motion_count(TSEL) == 1);
    motion_full = 1; frame();
    bad += check("#93 the events used up: AUTOMATION FULL", msg_is("AUTOMATION FULL") && !motion_full);
    motion_save_lim = 1; frame();
    bad += check("save-limit flag: AUTOMATION SAVE LIMIT toast",
                 msg_is("AUTOMATION SAVE LIMIT") && !motion_save_lim);
    {   /* past disk motion max: SAVE opens trim confirm (empty or used slot) */
        uint32_t i, page;
        char qa[24], qb[24];
        ui_power_on();
        for (i = 0; i < MOTION_FILE_MAX + 1u; i++)
            motion_set_event(&trk[0], i % NSTEP, i < NSTEP ? P_REV : P_DLY, (int16_t)(i % 60u));
        motion_save_lim = 0;
        for (page = 0; page < NPAGES; page++)
            if (PAGES[page].graph == GR_SLOTS)
                break;
        ui.home = 0;
        ui.page = (uint8_t)page;
        page_entered();
        turn(EN_K4, 1);
        press(B_OCTUP);
        bad += check("empty slot, motion over save max: CF_SAVE_PROJ",
                     ui.confirm == CF_SAVE_PROJ && ui.confirm_trk == 0 && !name_on());
        confirm_text(qa, qb);
        bad += check("CF_SAVE_PROJ empty: SAVE PROJECT + AUTOMATION WON'T SAVE",
                     str_eq(qa, "SAVE PROJECT A?") && str_eq(qb, "AUTOMATION WON'T SAVE"));
        press(B_OCTUP);
        bad += check("CF_SAVE_PROJ OCT+: NAME then save", name_on() && nm.kind == NK_PROJ_SAVE);
        press(B_OCTUP);
        bad += check("trimmed save keeps disk count at MOTION_FILE_MAX",
                     project_used(0) && msg_is("SAVED (RAM)"));
        {
            project_t q;
            bad += check("loaded slot motion clipped to 72",
                         proj_import(&q, &proj_slot[0], sizeof(project_store_t)) &&
                         q.motion.count == MOTION_FILE_MAX);
        }
        turn(EN_K4, 1);
        press(B_OCTUP);
        confirm_text(qa, qb);
        bad += check("used slot, still over max: OVERWRITE + AUTOMATION WON'T SAVE",
                     ui.confirm == CF_SAVE_PROJ && str_eq(qa, "OVERWRITE PROJECT A?") &&
                     str_eq(qb, "AUTOMATION WON'T SAVE"));
        press(B_OCTDN);
        bad += check("CF_SAVE_PROJ OCT- cancels", ui.confirm == CF_NONE);
    }
    {   /* #63: a card shows MOTION's icon while the track's motion changes its parameter */
        uint32_t id = ENGINES[TSEL->eng_req]->knob[0];
        ui_power_on(); go_home(); frame();
        ok = !card_motion(0) && !card_motion(1);
        song.rec = 1; seq_start(); go_home(); turn(EN_K1, 1); frame();
        ok &= card_motion(0) && !card_motion(1) && !card_motion(2) && !card_motion(3);
        bad += check("MOTION: a recorded knob marks its HOME card (and no other)", ok && motion_count(TSEL) == 1);
        song.rec = 0; press(B_PLAY); events_block(32); frame();
        ok = card_motion(0);
        go_auto_top(); turn(EN_K4, -1); go_home(); frame();
        ok &= !card_motion(0) && motion_count(TSEL) == 1;
        bad += check("MOTION: PLAY OFF takes the icon off the card, the events stay", ok);
        go_auto_top(); turn(EN_K4, 1); go_home(); frame();
        ok = card_motion(0);
        track_select(1); frame();
        ok &= !card_motion(0);                           /* (another track: its own motion, none) */
        track_select(0); frame();
        ok &= card_motion(0);
        bad += check("MOTION: PLAY ON again shows it; only on the track that has the motion", ok);
        go_page(GR_TRK); frame();
        ok = !card_motion(0) && !card_motion(1) && !card_motion(2);
        motion_set_event(TSEL, 3, P_PAN, 20); frame();
        ok &= card_motion(1) && !card_motion(0) && !card_motion(2) && !card_motion(3);
        bad += check("MOTION: a mix parameter's event marks its MIXER card (PAN)", ok);
        go_auto_top(); press(B_OCTUP); press(B_OCTUP);
        ok = motion_count(TSEL) == 0;
        go_home(); frame();
        ok &= !card_motion(0) && !card_motion(1);
        go_page(GR_TRK); frame();
        ok &= !card_motion(1);
        bad += check("MOTION: CLEAR takes the icon off every card", ok && id < P_COUNT);
    }
    ui_power_on(); ui_leds();
    memset(led_pos, 0xFF, sizeof led_pos);
    led_pos[panel.btn[B_PLAY]] = 1; led_pos[panel.btn[B_REC]] = 2;
    song.playing = song.rec = 1; ok = 1;
    for (i = 0; i < 120u; i++) { fm1_ms += 17; ui_leds(); ok &= (fm1_led[0] & 6u) == 4u && (fm1_led[8] & 2u); }
    bad += check("PLAY's green and REC LEDs stay lit across time and audio block phase", ok);
    {   /* the power-on sweep (hal/fm1_input.h fm1_led_anim_on) has the LEDs: the UI writes none of them while it runs,
         * all of its own picture on the next frame after */
        uint8_t l0[FM1_NCOL], d0[FM1_NCOL];
        memset(fm1_led, 0x5A, sizeof fm1_led);
        memset(fm1_led_dim, 0xA5, sizeof fm1_led_dim);
        memcpy(l0, fm1_led, sizeof l0);
        memcpy(d0, fm1_led_dim, sizeof d0);
        host_anim = 1;
        ui_leds();
        ok = !memcmp(l0, fm1_led, sizeof l0) && !memcmp(d0, fm1_led_dim, sizeof d0);
        host_anim = 0;
        ui_leds();
        ok &= (fm1_led[0] & 6u) == 4u && (fm1_led[8] & 2u) && fm1_led[1] != 0x5A;
        bad += check("the power-on LED sweep: the UI leaves the LEDs alone while it runs, takes them after", ok);
    }
    led_pos_init();
    ok = 1;
    for (p = 0; p < NPALETTES; p++) for (b = 0; b < 1u; b++) {
        uint16_t graph[240 * H_GRAPH], columns[240 * (Y_SEP_END - Y_LABEL)];
        ui_power_on(); palette_set(p); settings.zoom = 1;
        open_family(FAM_ENV); frame();
        memcpy(graph, host_screen + Y_GRAPH * 240, sizeof graph);
        memcpy(columns, host_screen + Y_LABEL * 240, sizeof columns);
        ui.hot_col = 1; ui.hot_t = 40; ui.force = 1; ui_draw();
        ok &= !memcmp(graph, host_screen + Y_GRAPH * 240, sizeof graph);
        int changed = 0;
        for (uint32_t y = 0; y < Y_SEP_END - Y_LABEL; y++) for (uint32_t x = 0; x < 240; x++) {
            uint16_t old = columns[y * 240 + x], now = host_screen[(Y_LABEL + y) * 240 + x];
            if (x >= 64 && x < 119) changed |= old != now;
            else ok &= old == now;
        }
        ok &= changed;
    }
    bad += check("all palettes: active column is subtle, stable-size, no zoom over graph", ok);
    ui_power_on(); set_engine_of(TSEL, 4); open_family(FAM_EDIT);
    memset(&sample_wave, 0, sizeof sample_wave); last_note = 60;
    voice_t voices[NVOICE]; memcpy(voices, TSEL->v, sizeof voices);
    ok = 1;
    for (i = 0; i < 2000u && !sample_wave.ready; i++) {
        uint32_t oldw = sample_wave.pos; sample_wave_tick(TSEL);
        ok &= sample_wave.pos - oldw <= 512u;
    }
    ok &= sample_wave.ready && !memcmp(voices, TSEL->v, sizeof voices);
    int16_t lo[SAMPLE_WAVE_COLS] = {0}, hi[SAMPLE_WAVE_COLS] = {0};
    voice_t probe = {0}; const smp_zone_t *zone = sample_wave.zone;
    if (zone) for (i = 0; i < zone->n; i++) {
        int32_t v = sample_next(zone, &probe, 0); uint32_t col = i * SAMPLE_WAVE_COLS / zone->n;
        if (v < lo[col]) lo[col] = (int16_t)v;
        if (v > hi[col]) hi[col] = (int16_t)v;
    }
    ok &= !memcmp(lo, sample_wave.lo, sizeof lo) && !memcmp(hi, sample_wave.hi, sizeof hi);
    bad += check("sample waveform is bounded, matches audio IMA decode and leaves voices intact", ok);
    ui_power_on(); return bad;
}

static int test_panel(void)
{
    int bad = 0;
    panel = PANEL_DEFAULT;
    panel.btn[B_PLAY] = panel.btn[B_REC];
    panel_init();
    bad += check("duplicate button mappings recover to the default panel", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
    panel.enc[EN_K1] = panel.enc[EN_K2];
    panel_init();
    bad += check("duplicate knob mappings recover to the default panel", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
    panel.btn[B_PLAY] = 255;
    panel_init();
    bad += check("out-of-range panel data recovers without an invalid shift", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
    panel.dir[EN_K1] = -1;
    panel_init();
    bad += check("a valid reversed knob mapping is retained", panel.dir[EN_K1] == -1);
    return bad;
}

/* ------------------------------------------------- the FX hold layer --- */
static void key_down(uint32_t k) { fm1_in.notes |= 1u << k; host_notes |= 1u << k; keyboard_block(); }
static void key_up(uint32_t k) { fm1_in.notes &= ~(1u << k); keyboard_block(); }
static uint32_t gates(void)                        /* voices of every track with their key down */
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NTRK; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].gate != 0;
    return n;
}
static uint32_t white(uint32_t w) { return key_at(0, w); }

/* a layer's key LEDs as the old 250 ms blink read them: lit, and the breathing ones (#119) "on" at even quarters,
 * so (a ^ b) over ms 0 and 250 = the ones that can be pressed, (a & b) = lit */
static uint32_t leds_at(uint32_t ms)
{
    uint32_t br, m;
    fm1_ms = ms;
    m = layer_leds(&br);
    return m | (((ms / 250u) & 1u) ? 0u : br);
}


static int test_layer(void)
{
    int bad = 0, ok, flash;
    uint32_t i, mo, k;
    /* tap: on release, Inserts; further taps cycle sections (not pages) */
    ui_power_on();
    btn_down(B_FX); frame();
    ok = ui.home;
    frames(64); btn_up(B_FX); frame();
    bad += check("FX tap: nothing on press, the FX page when let go", ok && !ui.home && cur_page()->fam == FAM_FX);
    frames(320);
    press(B_FX);
    bad += check("  a second tap: Punch-in section (SLICER lives under EFFECTS)",
                 cur_page()->sec == FX_SEC_PUNCH && str_eq(cur_page()->title, "PUNCH-IN"));
    ui_power_on();
    btn_down(B_FX);
    for (flash = 0, i = 0; i < 40u; i++) { frame(); flash |= ui.layer; }  /* past old HOLD */
    btn_up(B_FX); frame();
    bad += check("FX hold never opens the punch map (tap on release still enters Inserts)",
                 !flash && !ui.home && !ui.lock);

    /* menu HOLD / LEDS still exist as settings (threshold unused for maps) */
    ui_power_on();
    hold(B_HOME); ui.menu_sel = MI_HOLD; turn(EN_K1, 1); turn(EN_K1, 1); ok = settings_hold == 3u; hold(B_HOME);
    bad += check("menu HOLD setting still saves (unused for layer maps)", ok && !ui.menu);
    {
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        settings_export(&p); settings_hold = HOLD_DEF;
        bad += check("  HOLD is saved with the settings and read back", settings_import(&p, sizeof p) && settings_hold == 3u);
    }
    ui_power_on();
    hold(B_HOME); ui.menu_sel = MI_LEDS;
    ok = settings_leds == LEDS_DIM;
    turn(EN_K1, 1); ok &= settings_leds == LEDS_INV;
    turn(EN_K1, -1); ok &= settings_leds == LEDS_DIM;
    hold(B_HOME);
    bad += check("menu LEDS: DIM HI by default; KNOB 1 steps the mode", ok && !ui.menu);

    /* Punch-in sticky: keys and macros */
    ui_power_on();
    usb.config = 1; mo = mo_w;
    press(B_FX); press(B_FX); frame();                 /* Effects → Punch-in */
    ok = ui.lock == LAYER_FX && ui.layer == LAYER_FX && cur_page()->sec == FX_SEC_PUNCH;
    key_down(white(2)); frame();
    ok &= !gates() && mo_w == mo && (perf_held & PF_BIT(PF_R16));
    key_up(white(2)); frame();
    bad += check("Punch-in sticky: a key's effect, silent, no MIDI", ok && !perf_held);
    key_down(white(3)); frame();
    btn_down(B_ENV); frame();                          /* another family leaves Punch-in */
    btn_up(B_ENV); frame();
    key_up(white(3)); frame();
    bad += check("  leaving Punch-in clears the sticky map", !ui.lock || cur_page()->fam != FAM_FX);

    ui_power_on();
    go_title("REVERB");
    ok = cur_page()->fam == FAM_FX && cur_page()->id[0] == G_RTYPE && song.g[G_RTYPE] == 2 &&
         str_eq(GP[G_RTYPE].names[2], "HALL");
    turn(EN_K1, -2);
    ok &= song.g[G_RTYPE] == 0;
    turn(EN_K1, 1);
    ok &= song.g[G_RTYPE] == 1 && str_eq(GP[G_RTYPE].names[1], "SPRING");
    song.playing = 0;
    project_save(2);
    turn(EN_K1, -1);
    project_load(2);
    bad += check("REVERB page: TYPE HALL (power-on) / ROOM / SPRING; a project keeps SPRING",
                 ok && song.g[G_RTYPE] == 1);
    go_title("CHORUS");
    bad += check("  CHORUS insert page (no bus CHORUS)", cur_page()->fam == FAM_FX && cur_page()->graph == GR_INS &&
                 cur_page()->id[0] == P_IHRATE);

    ui_power_on();
    go_title("PUNCH-IN"); frame();
    ok = ui.lock == LAYER_FX && ui.layer == LAYER_FX;
    turn(EN_K1, -12); turn(EN_K2, 30);
    ok &= perf_k[0] == -12 && perf_k[1] == 30;
    press(B_ENV); frame();
    ok &= !ui.lock;
    bad += check("Punch-in macros on KNOB 1/2; leaving FX clears the sticky lock", ok);

    {
        static const uint8_t PB[6] = {B_ENV, B_LFO, B_SCL, B_ARP, B_GLO, B_EDIT};
        static const uint8_t PF[6] = {FAM_ENV, FAM_LFO, FAM_SCL, FAM_ARP, FAM_GLO, FAM_EDIT};
        ok = 1;
        for (i = 0; i < 6u; i++) {
            ui_power_on();
            btn_down(PB[i]); frame();
            ok &= ui.home;
            btn_up(PB[i]); frame();
            ok &= !ui.home && cur_page()->fam == PF[i];
            ui_power_on();
            hold(PB[i]);
            ok &= !ui.home && cur_page()->fam == PF[i] && !ui.layer; /* hold is tap; no peek map */
        }
        bad += check("ENV LFO SCL ARP GLO EDIT act when let go; hold never opens a peek map", ok);
    }
    ui_power_on();
    go_seq_step(); my_steps(TSEL); ui.cursor = 0; frame();
    btn_down(B_EDIT); frame();
    ok = step_on(&TSEL->step[0]) && ui.cursor == 0 && cur_page()->graph == GR_ROLL;
    btn_up(B_EDIT); frame();
    bad += check("EDIT on STEP opens EDIT when let go; the step stays", ok && step_on(&TSEL->step[0]) &&
                 ui.cursor == 0 && cur_page()->fam == FAM_EDIT);
    ui_power_on(); hold(B_HOME);
    ok = ui.menu == 1; hold(B_HOME); ok &= !ui.menu;
    go_home(); hold(B_SEQ);
    ok &= str_eq(cur_page()->title, "STEP") && !ui.lock; /* release of hold = tap, Step, not the tools map */
    bad += check("HOME hold opens menu; SEQ hold/release opens STEP, no peek-without-page", ok);
    return bad;
}

/* ------------------------------------------------------------ NAME --- */
static void nm_tap(uint32_t k)                    /* a key tapped in NAME: down a frame, up a frame (32 ms) */
{
    fm1_in.notes |= 1u << k; host_notes |= 1u << k; frame();
    fm1_in.notes &= ~(1u << k); frame();
}
static uint32_t nm_black_key(uint32_t f, uint32_t octave)   /* the black key of function f (NB_*: F# G# A# C# D#), octave 0 / 1 */
{
    return key_at(1, f + 5u * octave);
}
static int test_name(void)
{
    int bad = 0, ok;
    uint32_t i, mo;
    char b[16];
    ui_power_on();
    go_page(GR_USER);
    ui.uslot = 2;
    press(B_OCTUP);                               /* (SAVE picked on entry) */
    up_auto_name(b, TSEL->eng_req, 2);
    bad += check("NAME: USER SAVE into an empty slot opens NAME, prefilled with the automatic name",
                 name_on() && nm.kind == NK_USER_SAVE && nm.slot == 2u && str_eq(nm.s, b) && nm.cur == nm.len);
    for (i = 0; i < 12u; i++) nm_tap(nm_black_key(NB_DEL, 0));
    bad += check("  DELETE removes the character before the cursor, down to empty", nm.len == 0u && nm.cur == 0u && !nm.s[0]);
    /* multi-tap: the same key cycles, another key commits, 0.8 s commits */
    nm_tap(white(0));
    ok = nm.len == 1u && nm.s[0] == 'A' && nm.key == 1u && nm.cur == 0u;
    nm_tap(white(0));
    ok &= nm.s[0] == 'B' && nm.len == 1u;
    nm_tap(white(0));
    bad += check("  a white key types its first letter; tapped again within 0.8 s: the next one, cycling (A B A)",
                 ok && nm.s[0] == 'A' && nm.len == 1u && nm.cur == 0u);
    nm_tap(white(1));
    bad += check("  another key keeps the letter and types its own (A, C)", nm.len == 2u && str_eq(nm.s, "AC") && nm.cur == 1u &&
                 nm.key == 2u);
    frames(800);
    bad += check("  0.8 s without a tap keeps it: the cursor moves on", nm.key == 0u && nm.cur == 2u && str_eq(nm.s, "AC"));
    nm_tap(white(1));
    frames(400);
    nm_tap(white(1));
    bad += check("  a second tap within 0.8 s (0.43 s later) still cycles (C -> D)", str_eq(nm.s, "ACD") && nm.key == 2u);
    frames(800);
    nm_tap(white(4)); nm_tap(white(4)); nm_tap(white(4));   /* IJK: K */
    nm_tap(nm_black_key(NB_SPACE, 1));                       /* SPACE (the upper octave's G#) keeps K first */
    bad += check("  a three-letter key (IJK) cycles to K; SPACE keeps it and adds a space", str_eq(nm.s, "ACDK ") && nm.cur == 5u &&
                 !nm.key);
    nm_tap(nm_black_key(NB_DEL, 1));
    nm_tap(nm_black_key(NB_LEFT, 0));
    nm_tap(nm_black_key(NB_LEFT, 0));
    nm_tap(white(2));                             /* E inserted before D */
    frames(800);
    bad += check("  DELETE, cursor left twice, a letter inserted at the cursor", str_eq(nm.s, "ACEDK") && nm.cur == 3u);
    nm_tap(nm_black_key(NB_RIGHT, 1));
    nm_tap(nm_black_key(NB_RIGHT, 0));
    nm_tap(nm_black_key(NB_RIGHT, 0));
    bad += check("  cursor right stops at the end", nm.cur == 5u);
    /* 123 */
    nm_tap(nm_black_key(NB_MODE, 0));
    nm_tap(white(1)); nm_tap(white(9)); nm_tap(white(15));
    bad += check("  D# switches to 123: one tap, one character (2, 0, +), no cycling", nm.num && str_eq(nm.s, "ACEDK20+") &&
                 nm.cur == 8u && !nm.key);
    nm_tap(nm_black_key(NB_MODE, 1));
    nm_tap(white(15)); nm_tap(white(15)); nm_tap(white(15));
    frames(800);
    bad += check("  back to ABC: the last key (0-.) gives 0, then -, then .", !nm.num && str_eq(nm.s, "ACEDK20+.") && nm.cur == 9u);
    /* the knobs */
    turn(EN_K1, -2);
    turn(EN_K2, 1);
    bad += check("  KNOB 1 moves the cursor, KNOB 2 changes the character there (+ -> SPACE, wrapping)",
                 nm.cur == 7u && str_eq(nm.s, "ACEDK20 ."));
    turn(EN_K2, 5);
    bad += check("  KNOB 2 on: SPACE + 5 = E", nm.s[7] == 'E');
    turn(EN_K1, 9);
    turn(EN_K2, 1);
    bad += check("  KNOB 1 to the end, KNOB 2 adds a character there (A)", str_eq(nm.s, "ACEDK20E.A") && nm.cur == 9u && nm.len == 10u);
    turn(EN_K2, -2);
    bad += check("  .. and turns it back past SPACE to the last symbol (+)", nm.s[9] == '+');
    /* full */
    nm_tap(nm_black_key(NB_RIGHT, 0));
    nm_tap(white(7)); frames(800); nm_tap(white(7)); frames(800);
    ui.msg_t = 0;
    nm_tap(white(7));
    bad += check("  12 characters at most: NAME FULL, nothing typed", nm.len == 12u && msg_is("NAME FULL") && str_eq(nm.s, "ACEDK20E.+PP"));
    /* held DELETE repeats */
    fm1_in.notes |= 1u << nm_black_key(NB_DEL, 0); host_notes |= 1u << nm_black_key(NB_DEL, 0);
    frames(16 * 40);
    fm1_in.notes &= ~(1u << nm_black_key(NB_DEL, 0)); frame();
    bad += check("  DELETE held 0.64 s: one, then it repeats after 0.45 s, every 0.09 s (3 in all)", nm.len == 9u);
    /* the keys never sound, record or send MIDI */
    usb.config = 1; mo = mo_w; song.rec = 1;
    {
        step_t before[NSTEP];
        memcpy(before, TSEL->step, sizeof before);
        key_down(white(3)); key_down(nm_black_key(NB_SPACE, 0)); frame();
        for (i = 0; i < 8u; i++) events_block(CTL);
        ok = !gates() && mo_w == mo && !memcmp(before, TSEL->step, sizeof before) && song.grid == 2u;
        key_up(white(3)); key_up(nm_black_key(NB_SPACE, 0)); frame();
        bad += check("  keys in NAME: no voice, no MIDI, nothing recorded (seq.c: song.grid 2)", ok && mo_w == mo);
    }
    song.rec = 0; usb.config = 0;
    {   /* LEDs: all 27 keys do something; the cycling key blinks */
        uint32_t a, c, t0;
        nm_tap(nm_black_key(NB_DEL, 0)); nm_tap(nm_black_key(NB_DEL, 0));
        nm_tap(white(5));
        t0 = (fm1_ms / 500u + 1u) * 500u;
        fm1_ms = t0; a = name_leds(); fm1_ms = t0 + 250u; c = name_leds();
        nm.t = fm1_ms;                            /* (the letter still cycling) */
        bad += check("  LEDs: every key lit, the cycling key (LM) blinks", ((a | c) == 0x7FFFFFFu) && ((a ^ c) == (1u << white(5))));
    }
    /* cancel */
    {
        uint32_t page = ui.page;
        press(B_OCTDN);
        bad += check("  OCT- cancels: nothing written, back on the USER page", !name_on() && !up_used(2) && ui.page == page && !ui.home);
    }
    /* typing a name and saving it */
    ui.act = 4;
    press(B_OCTUP);
    for (i = 0; i < 12u; i++) nm_tap(nm_black_key(NB_DEL, 0));
    nm_tap(white(1)); nm_tap(white(0)); nm_tap(white(0)); nm_tap(nm_black_key(NB_RIGHT, 0));    /* C B */
    nm_tap(nm_black_key(NB_SPACE, 0)); nm_tap(nm_black_key(NB_MODE, 0)); nm_tap(white(1));        /* " 2" */
    nm_tap(nm_black_key(NB_SPACE, 0));                                                           /* a trailing space */
    song.playing = 1;
    press(B_OCTUP);
    bad += check("  OCT+ while playing: STOP TO SAVE, NAME stays", name_on() && msg_is("STOP TO SAVE") && !up_used(2));
    stop_transport();
    press(B_OCTUP);
    up_name(2, b);
    bad += check("  OCT+ stopped: saved as typed, the trailing space dropped (CB 2)", !name_on() && up_used(2) && str_eq(b, "CB 2") &&
                 msg_is("SAVED (RAM)"));
    /* SAVE held does nothing in NAME; the FX layer neither */
    ui.act = 4; press(B_OCTUP); press(B_OCTUP);   /* (U03 used: OVERWRITE?, YES) */
    bad += check("  overwrite: the dialog, then NAME", name_on() && nm.kind == NK_USER_SAVE);
    ui.msg_t = 0;
    hold(B_SAVE);
    btn_down(B_FX); frames(600); ok = !ui.layer; btn_up(B_FX); frame();
    bad += check("  SAVE held, FX held: nothing in NAME (no undo, no map, no page)", name_on() && ok && cur_page()->graph == GR_USER &&
                 !ui.msg_t);
    press(B_OCTDN);
    /* rename: EDIT on USER */
    ui.uslot = 5;
    press(B_EDIT);
    bad += check("  EDIT on an empty slot: EMPTY SLOT", !name_on() && msg_is("EMPTY SLOT") && cur_page()->graph == GR_USER);
    ui.uslot = 2;
    up_load(2);                                   /* (the sound now comes from U03) */
    trk[0].p[P_E0] = 33;                          /* an edit: a rename must not store it */
    press(B_EDIT);
    bad += check("  EDIT on a used slot: NAME (rename) with its name", name_on() && nm.kind == NK_USER_RENAME && str_eq(nm.s, "CB 2"));
    nm_tap(nm_black_key(NB_DEL, 0));
    nm_tap(white(10)); nm_tap(white(10));          /* VW: W */
    press(B_OCTUP);
    up_name(2, b);
    bad += check("  OCT+: renamed (CB W), the sound stored there unchanged", !name_on() && str_eq(b, "CB W") &&
                 up_value(up_rec(2), P_E0) != 33 && msg_is("RENAMED (RAM)"));
    press(B_EDIT);
    for (i = 0; i < 12u; i++) nm_tap(nm_black_key(NB_DEL, 0));
    press(B_OCTUP);
    {
        char au[16];
        up_name(2, b);
        up_auto_name(au, up_rec(2)->engine, 2);
        bad += check("  a name deleted to nothing: the automatic one", str_eq(b, au));
    }
    /* the sound's own name is the prefill */
    trk[0].user = 3;
    ui.uslot = 7; ui.act = 4;
    press(B_OCTUP);
    up_name(2, b);
    bad += check("  SAVE of a sound that came from U03: prefilled with U03's name", name_on() && str_eq(nm.s, b));
    press(B_OCTDN);
    /* projects */
    go_page(GR_SLOTS);
    song.g[G_SLOT] = 2;
    turn(EN_K4, 1);
    press(B_OCTUP);
    for (i = 0; i < 4u; i++) { nm_tap(white(7)); nm_tap(white(6)); frames(800); }   /* P N P N .. */
    press(B_OCTUP);
    {
        char pn[16];
        int named = project_name(1, pn);
        bad += check("PROJECT SAVE: NAME, typed, saved: the slot has the name, the project too", named && str_eq(pn, "PNPNPNPN") &&
                     str_eq(proj_name, "PNPNPNPN"));
    }
    proj_name[0] = 0;
    project_load(1);
    bad += check("  loading it brings its name back (the next save's prefill)", str_eq(proj_name, "PNPNPNPN"));
    trk[0].p[P_LEVEL] = 7;
    press(B_EDIT);
    for (i = 0; i < 4u; i++) nm_tap(nm_black_key(NB_DEL, 1));
    press(B_OCTUP);
    {
        char pn[16];
        project_name(1, pn);
        bad += check("  EDIT renames a project (PNPN); its music unchanged", str_eq(pn, "PNPN") && stored_param(1, 0, P_LEVEL) != 7 &&
                     msg_is("RENAMED (RAM)"));
    }
    song.g[G_SLOT] = 4;
    press(B_EDIT);
    bad += check("  EDIT on an empty project slot: EMPTY SLOT", !name_on() && msg_is("EMPTY SLOT"));
    song.g[G_SLOT] = 2;
    press(B_EDIT);
    hold(B_HOME);
    bad += check("  HOME held in NAME: the menu, NAME closed", ui.menu && !name_on());
    hold(B_HOME);
    return bad;
}

/* the EDIT cycle (no ENGINE page: engines are the EDIT layer's) and its memory */
static int engine_cycle(const char *const *want, uint32_t n) /* EDIT sections + SELECT pages from HOME */
{
    uint32_t i, k;
    int ok = 1;
    frames(320);
    go_home(); frame();
    press(B_EDIT); frames(100);
    for (i = 0; i < n; i++) {
        if (str_eq(want[i], "VOICE")) {

            if (cur_page()->sec != EDIT_SEC_VOX) { press(B_EDIT); frames(100); }
        } else if (cur_page()->sec != EDIT_SEC_SND) {
            press(B_EDIT); frames(100);
        }
        for (k = 0; k < 12u && !str_eq(cur_page()->title, want[i]); k++) {
            turn(EN_SELECT, 1); frames(50);
        }
        ok &= str_eq(cur_page()->title, want[i]);
        if (i + 1u < n && str_eq(want[i + 1u], "EDIT 1") && str_eq(want[i], "VOICE")) {

            press(B_EDIT); frames(100);               /* wrap Sound ← Voice */
        }
    }
    return ok;
}

static int test_px_probe(void) {
    ui_power_on(); go_page(GR_ROLL); frame();
    return 0;
}
static int test_edit_cycle(void)
{
    int bad = 0, ok;
    uint32_t i;
    /* EDIT button cycles sections (Sound ↔ Voice), not every page; SELECT walks pages in the section */
    ui_power_on();
    set_engine_of(TSEL, 0); /* ANALOG */
    press(B_EDIT); frame();
    ok = cur_page()->sec == EDIT_SEC_SND && str_eq(cur_page()->title, "EDIT 1");
    turn(EN_SELECT, 1); frame();
    ok &= cur_page()->sec == EDIT_SEC_SND && str_eq(cur_page()->title, "FILTER");
    turn(EN_SELECT, 1); frame();
    ok &= cur_page()->sec == EDIT_SEC_SND && str_eq(cur_page()->title, "FILTER DRIVE");
    turn(EN_SELECT, 1); frame();
    ok &= cur_page()->sec == EDIT_SEC_SND;              /* next sound page (not EDIT 2) */
    ok &= !str_eq(cur_page()->title, "EDIT 2");
    press(B_EDIT); frame();
    ok &= cur_page()->sec == EDIT_SEC_VOX && str_eq(cur_page()->title, "VOICE");
    ok &= cur_page()->id[0] == P_VOICE && cur_page()->id[1] == P_GLIDE &&
          cur_page()->id[2] == P_DETUNE && cur_page()->id[3] == P_SPRD;   /* one page; no GLMOD / PAN / PLAY */
    turn(EN_SELECT, 1); frame();
    ok &= str_eq(cur_page()->title, "VOICE");                            /* Voice section: one page only */
    press(B_EDIT); frame();
    ok &= cur_page()->sec == EDIT_SEC_SND;
    bad += check("EDIT cycle: ANALOG Sound is OSC→FILTER→FILTER DRIVE; Voice is one page (VCE GLD UNI DTN SPRD)", ok);

    /* UNI DTN / SPRD blank when the voice mode does not use them (page_desc; values stay) */
    {
        int16_t *vp = 0;
        const page_t *vox;
        ui_power_on();
        set_engine_of(TSEL, 0); TSEL->engine = TSEL->eng_req;
        press(B_EDIT); press(B_EDIT); frame();            /* Voice section */
        vox = cur_page();
        ok = str_eq(vox->title, "VOICE");
        TSEL->p[P_VOICE] = (int16_t)V_POLY;
        ok &= page_desc(vox, 2, &vp) == 0 && page_desc(vox, 3, &vp) != 0;   /* no UNI DTN; SPRD on */
        TSEL->p[P_VOICE] = (int16_t)V_MONO;
        ok &= page_desc(vox, 2, &vp) == 0 && page_desc(vox, 3, &vp) == 0;   /* neither */
        TSEL->p[P_VOICE] = (int16_t)V_LEGATO;
        ok &= page_desc(vox, 2, &vp) == 0 && page_desc(vox, 3, &vp) == 0;
        TSEL->p[P_VOICE] = (int16_t)V_UNISON;
        ok &= page_desc(vox, 2, &vp) != 0 && page_desc(vox, 3, &vp) != 0;   /* both */
        bad += check("VOICE: UNI DTN only in UNISON; SPRD only in POLY / UNISON", ok);
    }


    ui_power_on();
    set_engine_of(TSEL, ENGI_DRUM); TSEL->engine = TSEL->eng_req;
    press(B_EDIT); frame();
    ok = cur_page()->fam == FAM_EDIT && cur_page()->sec == EDIT_SEC_SND && str_eq(cur_page()->title, "EDIT 1");
    press(B_EDIT); frame();                           /* Sound → Kit */
    ok &= cur_page()->sec == EDIT_SEC_KIT && str_eq(cur_page()->title, "SOUND");
    turn(EN_SELECT, 1); frame();
    ok &= str_eq(cur_page()->title, "SOUND 2");
    turn(EN_SELECT, 1); frame();                      /* Kit: stop at last page */
    ok &= str_eq(cur_page()->title, "SOUND 2");
    press(B_EDIT); frame();                           /* Kit → Lanes */
    ok &= cur_page()->sec == EDIT_SEC_LAN && str_eq(cur_page()->title, "LANES");
    press(B_EDIT); frame();                           /* Lanes → Sound (Voice hidden on DRUM) */
    ok &= cur_page()->sec == EDIT_SEC_SND;
    bad += check("EDIT on DRUM: Sound → Kit (SOUND..SOUND 2) → Lanes; Voice skipped", ok);
    return bad;
}

/* a missing sample (an empty user slot; a set this build lacks): it plays a sine (eng_sample.c smp_sine) and the UI
 * says NO SAMPLE, led by the no-file icon (ui.c MSG_NO_SAMPLE), once per track and source (ui_input.c
 * sample_notice): when it is chosen, not on its notes; again for another source; after a project load (behind
 * LOADED); the icon and the words fit the header */
static int test_sample_alert(void)
{
    const char *nf = MSG_NO_SAMPLE;
    int bad = 0, ok, seen;
    uint32_t i;
    char b[48];
    ui_power_on();
    frames(100);
    bad += check("missing sample: the power-on sounds have their samples (no alert)", !snd_missing(&trk[0], &i) &&
                 !snd_missing(&trk[1], &i) && !snd_missing(&trk[2], &i) && !snd_missing(&trk[3], &i) &&
                 !str_eq(ui.msg, nf));
    set_engine_of(TSEL, ENGI_SAMPLE);
    TSEL->p[P_E0] = (int16_t)(SMP_NSETS + 1u);      /* SET USR2: empty */
    frame();
    ok = msg_is(nf);
    frames(1500);
    trk_note_on(TSEL, 60, 100);
    frames(100);
    trk_note_off(TSEL, 60);
    frames(100);
    ok &= !ui.msg_t;
    bad += check("SAMPLE on an empty USR2: NO SAMPLE once (not again on a note)", ok);
    TSEL->p[P_E0] = (int16_t)(SMP_NSETS + 2u);      /* USR3: said for it */
    frame();
    ok = msg_is(nf);
    TSEL->p[P_E0] = 0;                              /* PIANO: nothing; then USR3 again: said again */
    frames(1500);
    ok &= !ui.msg_t;
    TSEL->p[P_E0] = (int16_t)(SMP_NSETS + 2u);
    frame();
    ok &= msg_is(nf);
    bad += check("SAMPLE: another empty slot is said; back to it after a sample: said again", ok);
    frames(1500);
    set_engine_of(TSEL, 8u);                         /* GRAIN, SRC USR2 */
    TSEL->p[P_E0] = (int16_t)(SMP_NSETS + 1u);
    frame();
    bad += check("GRAIN on an empty USR2: said", msg_is(nf));
    frames(1500);
    /* a project with an empty slot: said after LOADED */
    set_engine_of(TSEL, ENGI_SAMPLE);
    TSEL->p[P_E0] = (int16_t)(SMP_NSETS + 1u);
    frames(1500);
    project_save(2);
    frames(1500);
    project_load(2);
    frame();
    ok = msg_is("LOADED") && str_eq(ui.msg2, nf);
    for (seen = 0, i = 0; i < 200u && !seen; i++) {
        frame();
        seen = msg_is(nf);
    }
    bad += check("a project load with an empty slot: LOADED, then NO SAMPLE", ok && seen);
    bad += check("missing sample: the icon and NO SAMPLE fit the header",
                 nf[0] == MSG_NOFILE[0] && !text_fit(b, sizeof b, nf + 1, &AF_S, 236 - 106 - 16 - KH_GAP));
    ui_power_on();
    return bad;
}

static int test_slices(void) { return 0; /* SLICE retired */ }



/* ------------------------------------------------ the GLO SCL EDIT layers --- */
static uint32_t black(uint32_t b) { return key_at(1, b); }
static void lay_combo(uint32_t btn, uint32_t k) { btn_down(btn); key_down(k); frame(); }
static void oct_back(void) { btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame(); }


static int test_quick_layers(void)
{
    static const uint8_t LB[3] = {B_GLO, B_SCL, B_EDIT};
    int bad = 0, ok, flash;
    uint32_t i, k;
    /* hold maps removed for GLO / SCL / EDIT */
    ok = 1;
    for (i = 0; i < 3u; i++) {
        ui_power_on();
        btn_down(LB[i]);
        for (flash = 0, k = 0; k < 40u; k++) { frame(); flash |= ui.layer | ui.lock; }
        btn_up(LB[i]); frame();
        ok &= !flash && !ui.home && !ui.layer && !ui.lock;
        ok &= LB[i] == B_GLO ? str_eq(cur_page()->title, "MASTER") :
              LB[i] == B_SCL ? cur_page()->fam == FAM_SCL : str_eq(cur_page()->title, "EDIT 1");
    }
    bad += check("GLO SCL EDIT: tap opens the page; hold never opens a quick map", ok);

    /* SCL still has CHORD via SELECT (one section family) */
    ui_power_on();
    press(B_SCL); frames(100);
    ok = str_eq(cur_page()->title, "SCL");
    turn(EN_SELECT, 1); frames(100);
    ok &= str_eq(cur_page()->title, "CHORD");
    press(B_SCL); frames(100);
    ok &= str_eq(cur_page()->title, "CHORD");           /* button does not advance pages */
    bad += check("SCL: SELECT walks to CHORD; SCL button while in SCL does not advance", ok);

    /* EDIT sections: Sound → Voice */
    ui_power_on();
    press(B_EDIT); frames(100);
    ok = cur_page()->fam == FAM_EDIT && cur_page()->sec == EDIT_SEC_SND;
    press(B_EDIT); frames(100);
    ok &= cur_page()->sec == EDIT_SEC_VOX && str_eq(cur_page()->title, "VOICE");
    bad += check("EDIT button cycles Sound → Voice sections", ok);

    /* OCT++PLAY restart (replaces GLO+PLAY) */
    ui_power_on();
    song.playing = 1; transport_req = 0;
    btn_down(B_OCTUP); frame(); press(B_PLAY);
    ok = transport_req == 3u;
    btn_up(B_OCTUP); frame();
    stop_transport();
    btn_down(B_OCTUP); frame(); press(B_PLAY);
    ok &= transport_req == 1u;
    btn_up(B_OCTUP); frame(); transport_req = 0;
    bad += check("OCT++PLAY: RESTART while playing; stopped: PLAY", ok);

    /* GLO+ black key no longer mutes via hold map */
    ui_power_on();
    trk[1].p[P_MUTE] = 0;
    lay_combo(B_GLO, black(1));
    ok = trk[1].p[P_MUTE] == 0 && !ui.layer;           /* removal */
    key_up(black(1)); btn_up(B_GLO); frame();
    bad += check("GLO hold + mute key does nothing (mute hold map removed)", ok);
    return bad;
}

/* bug fixes (UI): one regression each */
static int test_bughunt_ui(void)
{
    int bad = 0, ok;
    /* 1: REC in NAME, the menu or a dialog: nothing (PLAY cannot stop the transport there) */
    ui_power_on();
    go_page(GR_USER); ui.uslot = 3;
    press(B_OCTUP);                                     /* NAME opens */
    press(B_REC);
    bad += check("REC in NAME does nothing (no arm, no transport start); NAME stays",
                 name_on() && !song.rec && !transport_req && !song.playing);
    ui_power_on(); hold(B_HOME); press(B_REC); press(B_PLAY);
    bad += check("REC in the menu does nothing (then PLAY: nothing either)", ui.menu && !song.rec && !transport_req);
    ui_power_on();
    up_ui(2, 5);
    go_page(GR_USER); ui.uslot = 5; press(B_OCTUP);
    press(B_REC);
    ok = ui.confirm == CF_OVR_USER && !song.rec && !transport_req;
    press(B_OCTUP);
    bad += check("REC in the OVERWRITE? dialog does nothing; OCT+ then opens NAME", ok && name_on());
    {   /* 2: UNDO of a sound load on an FM6 track: the track's own (edited) patch, not PTCH's factory one */
        uint8_t mine[FP_SIZE + 1u], next[FP_SIZE + 1u];
        ui_power_on();
        track_select(1); frame();
        undo_depth++; set_engine_of(TSEL, ENGI_FM6); undo_depth--;   /* (no undo copy of this) */
        frame();
        memcpy(mine, fm6_patch[1], sizeof mine);
        mine[0] ^= 0x15; mine[5] ^= 0x22; mine[40] ^= 0x07;   /* an edited patch (the editor, a project) */
        fm6_set_patch(1, mine);
        memcpy(mine, fm6_patch[1], FP_SIZE);
        turn(EN_PRESET, 1);                             /* browse one sound */
        memcpy(next, fm6_patch[1], FP_SIZE);
        hold(B_SAVE);                                   /* UNDO */
        frame();
        ok = TSEL->eng_req == ENGI_FM6 && !memcmp(mine, fm6_patch[1], FP_SIZE);
        hold(B_SAVE);                                   /* REDO */
        frame();
        bad += check("UNDO of a sound load on FM6: the track's edited patch back; REDO: the load's",
                     ok && memcmp(mine, next, FP_SIZE) && !memcmp(next, fm6_patch[1], FP_SIZE));
        hold(B_SAVE); frame();                          /* (the edited patch again) */
        btn_down(B_EDIT); frames(500);                  /* EDIT hold map removed */
        ok = !ui.layer && TSEL->eng_req == ENGI_FM6;
        btn_up(B_EDIT); frame();
        bad += check("  EDIT hold opens no engine map; FM6 patch stays after the undo",
                     ok && !memcmp(mine, fm6_patch[1], FP_SIZE));
    }
    {   /* 2b: a patch the editor sends between two preset loads is a new starting point: UNDO brings it back */
        uint8_t ed[FP_SIZE + 1u];
        ui_power_on();
        track_select(1); frame();
        undo_depth++; set_engine_of(TSEL, ENGI_FM6); undo_depth--;
        frame();
        turn(EN_PRESET, 1); frame();                    /* load 1 */
        memcpy(ed, fm6_patch[1], sizeof ed);
        ed[0] ^= 0x15; ed[40] ^= 0x07;
        fm6_set_patch(1, ed);                           /* the editor's FM6_PUT */
        memcpy(ed, fm6_patch[1], FP_SIZE);
        turn(EN_PRESET, 1); frame();                    /* load 2 */
        hold(B_SAVE); frame();                          /* UNDO */
        bad += check("UNDO after load, editor patch, load: the editor's patch back",
                     !memcmp(ed, fm6_patch[1], FP_SIZE));
    }
    {   /* the ARP button flashes on the beat while an ARP plays (the bar's first beat longer); dark otherwise */
        uint32_t q, b, r[5];
        ui_power_on(); frame();
        b = beat_samples();
        r[0] = arp_led();                               /* no ARP playing */
        trk[2].p[P_AMODE] = 1; trk[2].nheld = 1; trk[2].held[0] = 60;
        beat_n = 1; beat_pos = 0; r[1] = arp_led();
        beat_pos = b / 3u; r[2] = arp_led();
        beat_n = 0; r[3] = arp_led();
        beat_pos = b * 3u / 4u; r[4] = arp_led();
        bad += check("ARP LED: none without an ARP; flashes on the beat, the bar's first beat longer",
                     r[0] == 2u && r[1] == 1u && r[2] == 0u && r[3] == 1u && r[4] == 0u);
        for (q = 0; q < 4u * b / 128u + 4u; q++) events_block(128);
        bad += check("  the beat counter runs (a bar of blocks wraps the beat of the bar)", beat_n < 4u && beat_pos < b);
        trk[2].p[P_AMODE] = 0; trk[2].nheld = 0;
    }
    /* 3: USER ERASE asks first (ERASE U02?): OCT- keeps the preset, OCT+ erases it */
    ui_power_on();
    go_page(GR_USER); ui.uslot = 1;
    up_ui(2, 1);
    turn(EN_K1 + 2, 1);                                 /* KNOB 3: ERASE picked */
    press(B_OCTUP);
    ok = ui.confirm == CF_ERASE_USER && ui.confirm_trk == 1u && up_used(1);
    {
        char a[24], b[24];
        confirm_text(a, b);
        ok &= str_eq(a, "ERASE U02?") && b[0];
    }
    press(B_OCTDN);
    ok &= !ui.confirm && up_used(1);
    turn(EN_K1 + 2, 1); press(B_OCTUP); press(B_OCTUP);
    bad += check("USER ERASE: the ERASE U02? dialog; OCT- keeps the preset, OCT+ erases it",
                 ok && !ui.confirm && !up_used(1));
    turn(EN_K1 + 2, 1); press(B_OCTUP);
    bad += check("  an empty slot: no dialog, EMPTY SLOT", !ui.confirm && msg_is("EMPTY SLOT"));
    {   /* 4: AUTOMATION (CHANCE's since 1.2) is not STEP: no grid, no key entry, no EDIT clear (its knobs only) */
        uint32_t before;
        step_t s0;
        ui_power_on();
        track_select(3); frame();                       /* T4 DRUM */
        go_page(GR_EVENTS); frame();
        before = lane_steps(TSEL, ui.lane);
        tap_key(white(2));
        bad += check("AUTOMATION on a DRUM track: no grid (title, LEDs), a white key toggles no hit",
                     drum_track(TSEL) && !grid_on() && !keys_mode() && lane_steps(TSEL, ui.lane) == before);
        ui_power_on(); go_auto_top(); frame();
        my_steps(TSEL);
        s0 = TSEL->step[0];
        tap_key(white(5)); frame();
        ok = !memcmp(&s0, &TSEL->step[0], sizeof s0) && ui.cursor == 0u;
        press(B_EDIT);
        ok &= !memcmp(&s0, &TSEL->step[0], sizeof s0) && !msg_is("STEP CLEARED");
        ui_power_on(); go_auto_add(EV_CHANCE, 0); frame();
        s0 = TSEL->step[0];
        press(B_OCTUP);                                 /* (100 % by default: + ADD CHANCE, 50 %) */
        bad += check("AUTOMATION on a melodic track: a key writes no step, EDIT clears none; + ADD CHANCE the chance",
                     ok && step_chance(&TSEL->step[0]) != step_chance(&s0));
        ui_power_on(); project_not_empty(); go_page(GR_ROLL); frame(); song.rec = 1;
        tap_key(white(5)); frame();
        bad += check("  STEP still: armed, a key writes the cursor step", step_on(&TSEL->step[0]) || TSEL->step[0].n);
        song.rec = 0;
    }
    {   /* 5: hold maps removed; ALGO still swallowed while a layer button is armed (ui.ly) */
        ui_power_on();
        btn_down(B_SCL); frames(500);
        ok = !ui.layer && !ui.lock && ui.ly == LAYER_SCL;
        turn(EN_ALGO, 1);
        ok &= song.sel == 0u;                           /* armed button still owns ALGO */
        btn_up(B_SCL); frame(); frames(100);
        turn(EN_ALGO, 1);
        ok &= song.sel == 1u;
        bad += check("ALGORITHM ignored while SCL armed (no map); after release, T2", ok);
    }
    /* 6: CLEAR map: each key its own ready check; nothing to do says so (not STOP TO EDIT) */
    ui_power_on();
    go_title("CLEAR"); frame();
    chain_defaults(&chain_config);                      /* no song rows */
    key_down(white(8)); frame(); key_up(white(8)); frame();   /* G4 SEC */
    ok = !ui.confirm && msg_is("NOTHING TO DEL") && text_w(&AF_S, ui.msg) <= 236 - 106;   /* (fits the header) */
    key_down(white(9)); frame(); key_up(white(9)); frame();   /* A4 SONG */
    ok &= !ui.confirm && msg_is("NOTHING TO CLEAR");
    track_defaults_steps(TSEL);
    chain_config.count = 1; chain_config.row[0] = chain_row_of(0, 1);
    key_down(white(0)); frame(); key_up(white(0)); frame();   /* F3 PAT empty */
    ok &= !ui.confirm && msg_is("NOTHING TO CLEAR");
    key_down(white(9)); frame(); key_up(white(9)); frame();
    bad += check("CLEAR: DELETE SEC / CLEAR SONG / CLEAR PAT with nothing there: NOTHING TO ..; a song: its dialog",
                 ok && ui.confirm == CF_CLEAR_SONG);
    /* 7: REC while a family button is held still arms (combo); hold alone opens no map */
    ok = 1;
    {
        static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
        uint32_t i;
        for (i = 0; i < 4u; i++) {
            ui_power_on();
            btn_down(LB[i]); frame();
            ok &= !ui.layer || ui.lock;                 /* no hold-open map */
            press(B_REC);
            ok &= song.rec == 1u && (transport_req == 1u || msg_is("PLAY FREELY"));
            btn_up(LB[i]); frame();
        }
    }
    bad += check("REC with a family button held still arms; hold alone opens no peek map", ok);
    /* 8: hold maps removed — OCT LEDs stay the octave's while GLO/SCL/EDIT/FX held */
    ok = 1;
    {
        static const uint8_t LB[4] = {B_GLO, B_SCL, B_EDIT, B_FX};
        uint32_t i;
        for (i = 0; i < 4u; i++) {
            ui_power_on();
            song.octave = 1;
            btn_down(LB[i]); frames(500);
            ok &= !layer_set_open() && !ui.lock;
            btn_up(LB[i]); frame();
        }
    }
    bad += check("SET/HOLD peek maps removed: holding GLO/SCL/EDIT/FX opens no layer_set_open map", ok);
    /* 9: EDIT tapped on USER / PROJECT renames; hold-map QUICK hints are gone with the maps */
    ui_power_on();
    layer_seen = 0;
    up_ui(2, 0);
    go_page(GR_USER); ui.uslot = 0; frame();
    ui.msg_t = 0;
    press(B_EDIT);
    ok = name_on() && !msg_is("HOLD [EDIT] QUICK");
    ui_power_on();
    layer_seen = 0;
    press(B_EDIT);
    bad += check("EDIT tap = RENAME on USER: no layer hint over NAME (elsewhere the hint as before)",
                 ok && !name_on() && cur_page()->fam == FAM_EDIT && !msg_is("HOLD [EDIT] QUICK"));
    return bad;
}

/* the chord keys on the device (chord.c): SCL tapped again is the CHORD page, KNOB 1 CHRD, KNOB 2 VOIC, KNOB 3
 * LOCK; the graph names the last chord (MONO gray); a sound load keeps them; the SCL layer's KNOB 3 / 4 are CHRD /
 * VOIC, OCT- puts them back */

static int test_chord_page(void)
{
    int bad = 0, ok;
    ui_power_on();
    press(B_SCL); frames(100);
    ok = str_eq(cur_page()->title, "SCL");
    turn(EN_SELECT, 1); frames(100);
    ok &= str_eq(cur_page()->title, "CHORD");
    turn(EN_K1, 1);
    ok &= TSEL->p[P_CHRD] != 0;
    turn(EN_K2, 1);
    ok &= TSEL->p[P_VOIC] != 0;
    ok &= cur_page()->id[2] == PG_STRUM && cur_page()->id[3] == PG_STROKE;   /* SCALE LOCK is gone */
    turn(EN_K3, 1);
    ok &= sweep_rate(TSEL) != 0;
    turn(EN_K4, 1);
    ok &= sweep_stroke(TSEL) != 0;
    bad += check("SCL: SELECT opens CHORD; KNOB 1 CHRD, KNOB 2 VOIC, K3 STRUM, K4 STROKE", ok);
    press(B_SCL); frames(100);
    ok = str_eq(cur_page()->title, "CHORD");           /* button does not advance */
    bad += check("SCL button while in SCL family does not advance pages", ok);
    /* hold map removed */
    ui_power_on();
    btn_down(B_SCL); frames(600);
    ok = !ui.layer && !ui.lock;
    btn_up(B_SCL); frame();
    bad += check("SCL hold never opens a quick ROOT map", ok && cur_page()->fam == FAM_SCL);
    return bad;
}

/* MAP = ALL (stored QN_CURR): every track follows the current ROOT / SCL while ALL is on, and keeps
 * its own values in scl_stash. Leaving ALL restores all of them. CURR (stored QN_ALL) shares nothing. */
static int test_scale_lock(void)
{
    int bad = 0, ok;
    uint32_t i;

    ui_power_on();
    ok = 1;
    for (i = 0; i < NTRK; i++)
        ok &= trk[i].p[P_QUANT] == QN_OFF && !trk[i].scl_stash_ok;
    bad += check("MAP ALL: every track OFF and unlinked at power-on", ok);

    trk[0].p[P_ROOT] = 0; trk[0].p[P_SCALE] = 0;
    trk[1].p[P_ROOT] = 7; trk[1].p[P_SCALE] = 2;
    trk[2].p[P_ROOT] = 9; trk[2].p[P_SCALE] = 1; trk[2].p[P_QUANT] = QN_KBD;
    trk[3].p[P_ROOT] = 5; trk[3].p[P_SCALE] = 3; trk[3].p[P_QUANT] = QN_SEQ;
    trk[0].p[P_QUANT] = QN_CURR;                       /* stored 4, shown ALL */
    scale_lock_after(&trk[0], P_QUANT);
    ok = 1;
    for (i = 0; i < NTRK; i++)
        ok &= trk[i].p[P_ROOT] == 0 && trk[i].p[P_SCALE] == 0 && trk[i].p[P_QUANT] == QN_CURR;
    ok &= trk[0].scl_stash_ok && trk[1].scl_stash_ok && trk[1].scl_stash[0] == 7 &&
          trk[2].scl_stash[0] == 9 && trk[3].scl_stash[0] == 5 && trk[2].scl_stash[2] == QN_KBD;
    bad += check("ALL: entering sets MAP, ROOT/SCL on every track and stashes their own", ok);

    trk[0].p[P_ROOT] = 3;
    scale_lock_after(&trk[0], P_ROOT);
    ok = 1;
    for (i = 0; i < NTRK; i++)
        ok &= trk[i].p[P_ROOT] == 3;
    bad += check("ALL: a ROOT edit follows every track", ok);

    trk[2].p[P_ROOT] = 4;                              /* a KBD track's edit still leads the shared scale */
    scale_lock_after(&trk[2], P_ROOT);
    ok = 1;
    for (i = 0; i < NTRK; i++)
        ok &= trk[i].p[P_ROOT] == 4;
    bad += check("ALL: an edit on any track follows every track", ok);

    trk[0].p[P_QUANT] = QN_KBD;
    scale_lock_after(&trk[0], P_QUANT);
    ok = trk[0].p[P_ROOT] == 0 && trk[0].p[P_SCALE] == 0 && trk[0].p[P_QUANT] == QN_KBD &&
         trk[1].p[P_ROOT] == 7 && trk[1].p[P_SCALE] == 2 && trk[1].p[P_QUANT] == QN_OFF &&
         trk[2].p[P_ROOT] == 9 && trk[2].p[P_SCALE] == 1 && trk[2].p[P_QUANT] == QN_KBD &&
         trk[3].p[P_ROOT] == 5 && trk[3].p[P_SCALE] == 3 && trk[3].p[P_QUANT] == QN_SEQ;
    for (i = 0; i < NTRK; i++)
        ok &= !trk[i].scl_stash_ok;
    bad += check("ALL -> KBD: every track gets its own MAP and ROOT/SCL back", ok);

    ui_power_on();
    trk[1].p[P_ROOT] = 7;
    song.sel = 0;
    open_family(FAM_SCL);
    frame();
    turn(EN_K3, 1);                                    /* OFF -> ALL */
    ok = trk[0].p[P_QUANT] == QN_CURR && trk[1].p[P_QUANT] == QN_CURR && trk[1].p[P_ROOT] == 0;
    turn(EN_K3, 1);                                    /* ALL -> CURR: restore */
    ok &= trk[0].p[P_QUANT] == QN_ALL && trk[1].p[P_QUANT] == QN_OFF && trk[1].p[P_ROOT] == 7;
    bad += check("ALL from the SCL knob: one step links every track; the next restores", ok);
    return bad;
}

/* the STEP page's piano roll (ui_graph.c graph_roll), read back from the screen: per column the rows whose centre
 * pixel (the bar's middle, x + 6) is a bar colour, against the notes the step sounds (its own; a TIE: the note
 * before's; a REST: none) */
static int pr_is_bar(uint32_t x, uint32_t y)
{
    uint16_t c = swap16(host_screen[y * 240u + x]);
    return c == T_THEME || c == T_ACCENT || c == T_TEXT;
}
static int pr_columns_match(const track_t *t, uint32_t *nbars)
{
    uint32_t i, r, j, len = (uint32_t)t->p[P_SLEN], base = ui.bank * 16u;
    int ok = 1;
    for (i = 0; i < 16u && base + i < len; i++) {
        uint32_t want = 0, got = 0, s = pr_src(t, base + i, len);
        for (j = 0; s < NSTEP && j < 4u + NLANE; j++) {
            int32_t n = pr_note(&t->step[s], j), rr = (int32_t)proll.lo + PR_ROWS - 1 - n;
            if (n >= 0 && rr >= 0 && rr < PR_ROWS) want |= 1u << rr;
        }
        for (r = 0; r < PR_ROWS; r++)
            if (pr_is_bar((uint32_t)(PR_X0 + (int32_t)i * PR_CW + 6), Y_GRAPH + PR_Y0 + r * PR_RH + 2u)) got |= 1u << r;
        if (got != want) {
            printf("ui:   roll column %u: rows %05x drawn, %05x wanted\n", base + i, got, want);
            ok = 0;
        }
        for (r = 0; r < PR_ROWS; r++) *nbars += (got >> r) & 1u;
    }
    return ok;
}
/* 1.2, Discussion #133: step recording on SEQ > STEP. Not armed, the keys only play (being on SEQ writes nothing);
 * REC there arms step recording without starting the transport; armed and stopped a key (keys held together: a
 * chord) writes the cursor step and letting go moves the cursor on one step, wrapping inside LEN; PLAY records live
 * (at the play head, not the cursor). The DRUM grid keeps its own keys (white keys = steps, REC starts PLAY) */
static int test_step_rec(void)
{
    int bad = 0, ok;
    track_t *t;
    step_t before[NSTEP];
    ui_power_on();
    project_not_empty();
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    track_defaults_steps(t);
    go_page(GR_ROLL);
    cursor_set(3);
    frame();
    memcpy(before, t->step, sizeof before);
    key_down(7); frame();
    ok = kb_chn[7] != 0u;                                             /* it sounds */
    key_up(7); frame();
    ok &= !memcmp(before, t->step, sizeof before) && ui.cursor == 3u && !song.rec && !transport_req;
    bad += check("#133 STEP, not armed, stopped: a key sounds, writes nothing, the cursor stays", ok);
    song.playing = 1;
    key_down(7); frame(); key_up(7); frame();
    song.playing = 0;
    bad += check("#133 STEP, not armed, playing: nothing written either", !memcmp(before, t->step, sizeof before));
    cursor_set(3);                                                    /* follow moved the cursor while playing */
    frame();
    song.rec = 2u;                                                    /* T2 armed, T1 selected */
    key_down(7); frame(); key_up(7); frame();
    bad += check("#133 another track armed: the selected track's STEP writes nothing",
                 !memcmp(before, t->step, sizeof before) && ui.cursor == 3u);
    song.rec = 0;
    press(B_REC);
    bad += check("#133 REC on STEP arms step recording: stopped, KEYS WRITE STEPS",
                 song.rec == 1u && !transport_req && !song.playing && !seq_counting() && msg_is("KEYS WRITE STEPS"));
    key_down(7); frame();
    ok = t->step[3].n == 1u && step_note(&t->step[3], 0) == kb_map(t, 7) && t->step[3].time == ST_NOTE && ui.cursor == 3u;
    key_up(7); frame();
    ok &= ui.cursor == 4u && !memcmp(before, t->step, 3u * sizeof before[0]) &&
          !memcmp(before + 4, t->step + 4, (NSTEP - 4u) * sizeof before[0]);
    bad += check("#133 armed, stopped: a key writes its note into the cursor step (only), letting go moves on one", ok);
    key_down(7); frame(); key_down(11); frame(); key_down(14); frame();
    ok = t->step[4].n == 3u && ui.cursor == 4u;
    key_up(7); frame();
    ok &= ui.cursor == 4u;                                            /* (two keys still down: the step stays open) */
    key_up(11); key_up(14); frame();
    ok &= ui.cursor == 5u && step_note(&t->step[4], 0) == kb_map(t, 7) && step_note(&t->step[4], 1) == kb_map(t, 11) &&
          step_note(&t->step[4], 2) == kb_map(t, 14);
    bad += check("#133 keys held together: one chord step (POLY), the cursor moves on once all are up", ok);
    t->p[P_VOICE] = V_MONO;
    key_down(7); frame(); key_down(9); frame(); key_up(7); key_up(9); frame();
    bad += check("#133 MONO: one note, the last key", t->step[5].n == 1u && step_note(&t->step[5], 0) == kb_map(t, 9) &&
                 ui.cursor == 6u);
    t->p[P_SLEN] = 8;
    cursor_set(7);
    frame();
    key_down(9); frame(); key_up(9); frame();
    bad += check("#133 the last step of LEN 8: written, the cursor wraps to step 1", t->step[7].n == 1u && ui.cursor == 0u);
    /* step recording: PRESETS while a key is held sets LEN (TIEs); release moves past the whole note */
    t->p[P_SLEN] = 16;
    t->p[P_VOICE] = V_POLY;
    cursor_set(8);
    step_clear(&t->step[8]);
    step_clear(&t->step[9]);
    step_clear(&t->step[10]);
    step_clear(&t->step[11]);
    frame();
    key_down(7); frame();
    ok = ui.entry_open && t->step[8].n == 1u && t->step[8].time == ST_NOTE && ui.cursor == 8u;
    turn(EN_PRESET, 2);
    ok &= t->step[8].time == ST_NOTE && t->step[9].time == ST_TIE && t->step[10].time == ST_TIE &&
          !t->step[9].n && !t->step[10].n && note_len(t, 8) == 3u && ui.cursor == 8u && ui.entry_open;
    turn(EN_PRESET, -1);
    ok &= note_len(t, 8) == 2u && t->step[10].time == ST_REST && ui.cursor == 8u && ui.entry_open;
    key_up(7); frame();
    ok &= !ui.entry_open && ui.cursor == 10u && t->step[8].n == 1u && t->step[9].time == ST_TIE;
    bad += check("#133 armed, stopped: PRESETS while held sets LEN; let go moves past the note", ok);
    key_down(7); frame();
    turn(EN_PRESET, 1);                                               /* LEN 2 at step 10 */
    key_up(7); frame();
    cursor_set(10);                                                   /* back on that note, nothing held */
    frame();
    turn(EN_PRESET, 1);                                               /* not held: PRESETS still moves the note */
    ok = t->step[11].n == 1u && t->step[11].time == ST_NOTE && t->step[12].time == ST_TIE &&
         t->step[10].time == ST_REST && !t->step[10].n && ui.cursor == 11u;
    bad += check("#133 PRESETS with no key held still moves the note (and its TIEs)", ok);
    cursor_set(4);                                                    /* (an empty step; follow will track the play head) */
    step_clear(&t->step[4]);
    frame();
    press(B_PLAY);
    events_block(CTL);
    ok = song.playing && live_rec_sel() && t->seq_idx < 2u;
    key_down(7); frame(); key_up(7); frame();
    ok &= ui.cursor == t->seq_idx && !t->step[4].n && !t->step[4].hit && (t->step[0].n || t->step[1].n);
    bad += check("#133 PLAY with the track armed: live recording at the play head (cursor follows)", ok);
    stop_transport();
    {
        uint8_t cur = ui.cursor;
        press(B_REC);                                                 /* disarm: the keys only play again */
        memcpy(before, t->step, sizeof before);
        key_down(7); frame(); key_up(7); frame();
        bad += check("#133 REC again disarms: the keys only play, nothing written",
                     !song.rec && !memcmp(before, t->step, sizeof before) && ui.cursor == cur);
    }
    /* the DRUM grid as before: white keys are the steps (armed or not), REC from stop starts PLAY */
    ui_power_on();
    t = TSEL;
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    go_page(GR_ROLL);
    frame();
    tap_key(key_at(0, 4));
    ok = grid_on() && t->step[4].hit != 0u;
    press(B_REC);
    ok &= song.rec == 1u && transport_req == 1u && msg_is("LANE KEYS RECORD");
    transport_req = 0;                                                /* (armed, still stopped) */
    tap_key(key_at(0, 6));
    tap_key(key_at(0, 4));
    ok &= t->step[6].hit != 0u && !t->step[4].hit && ui.cursor == 4u;
    bad += check("#133 the DRUM grid unchanged: white keys toggle steps armed or not, REC from stop starts PLAY", ok);
    song.rec = 0;
    /* PITCH is inert on a kit: SCL K4 on a held grid step creates no lock */
    ui_power_on();
    song.sel = 0;
    t = TSEL;
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    track_defaults_steps(t);
    go_title("SCL");
    go_seq_step();                                                    /* (plk_src stays SCL) */
    frame();
    {
        uint32_t k4 = key_at(0, 4);
        int16_t v;
        ok = lock_id(3) == 0xFFu;
        fm1_in.notes |= 1u << k4;
        host_notes |= 1u << k4;
        frame();
        ok &= lock_held() == (1u << 4);
        turn(EN_K4, 2);
        fm1_in.notes &= ~(1u << k4);
        frame();
        bad += check("SCL K4 on a held DRUM step: no PITCH lock on a kit track",
                     ok && !motion_lock_get(t, 4, MSEQ_PITCH, &v) && !motion_lock_count(t));
    }
    return bad;
}

static int test_tape_locks(void)
{
    int bad = 0, ok;
    track_t *t;

    /* TAPE SPEED / WOW: KNOB 1 / 2 on a held SEQ step are locks (TAPE page is a global page). */
    ui_power_on();
    song.sel = 0;
    t = TSEL;
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    track_defaults_steps(t);
    go_title("TAPE");
    ok = cur_page()->title && str_eq(cur_page()->title, "TAPE") && lock_page_ok(ui.page);
    {
        uint32_t i, found = 0, sel = 0;
        pk_build_mdst(t, MD_OFF, &sel);
        for (i = 0; i < pk_n; i++) {
            if (pk_rows[i].id == MD_TSPEED && str_eq(pk_rows[i].name, "SPEED"))
                found |= 1u;
            if (pk_rows[i].id == MD_TWOW && str_eq(pk_rows[i].name, "WOW"))
                found |= 2u;
        }
        ok &= found == 3u;
    }
    go_seq_step();
    frame();
    {
        uint32_t k4 = key_at(0, 4);
        int16_t v;
        ok &= lock_id(0) == MFX_TSPEED && lock_id(1) == MFX_TWOW && lock_id(2) == 0xFFu;
        fm1_in.notes |= 1u << k4;
        host_notes |= 1u << k4;
        frame();
        ok &= lock_held() == (1u << 4);
        turn(EN_K1, -20);                                  /* SPEED below 100 */
        turn(EN_K2, 5);                                    /* WOW up */
        fm1_in.notes &= ~(1u << k4);
        frame();
        ok &= motion_lock_get(t, 4, MFX_TSPEED, &v) && v < song.g[G_TSPEED] &&
              motion_lock_get(t, 4, MFX_TWOW, &v) && v > 0;
        bad += check("TAPE page: held SEQ step locks SPEED (K1) and WOW (K2); K3 / K4 are not locks", ok);
    }
    ui_power_on();
    t = TSEL;
    t->p[P_LWAVE] = 4;                                  /* S&H makes SMOOTH available on LFO 2 */
    go_title("LFO 2");
    ok = !lock_page_ok(ui.page);
    go_seq_step();
    frame();
    {
        uint32_t k4 = key_at(0, 4);
        int16_t v;
        fm1_in.notes |= 1u << k4;
        host_notes |= 1u << k4;
        frame();
        turn(EN_K4, 5);
        fm1_in.notes &= ~(1u << k4);
        frame();
        bad += check("LFO 2 SMOOTH on a held SEQ step cannot create a TAPE WOW lock",
            ok && !motion_lock_get(t, 4, MFX_TWOW, &v));
    }
    return bad;
}

/* 1.2: MENU > CONTROL > STEP PREVIEW (Discussion #169) and CHORD ENTRY (#155), MENU > DISPLAY > SCOPE (#165; its sound
 * path: click_test.c). Every new setting 0 by default (today's behaviour), a bit of its own byte */
static uint32_t gated(const track_t *t, uint32_t note)   /* voices of t sounding note with their gate on */
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].gate && (note > 127u || t->v[i].note == note);
    return n;
}
static void blocks(uint32_t n) { while (n--) events_block(CTL); }
static void tap_k(uint32_t k) { key_down(k); frame(); key_up(k); frame(); }
static int test_step_preview_chord_entry(void)
{
    int bad = 0, ok;
    track_t *t;
    step_t before[NSTEP];
    uint32_t i, rec0, mc0;
    ui_power_on();
    ok = !ui_prefs2 && !menu_get(MI_SCOPE) && !menu_get(MI_PREVIEW) && !menu_get(MI_CHADD) && !scope_mix &&
         str_eq(menu_vname(MI_SCOPE, 0), "OUT") && str_eq(menu_vname(MI_SCOPE, 1), "MIX") &&
         str_eq(menu_vname(MI_PREVIEW, 1), "ON") && str_eq(menu_vname(MI_CHADD, 0), "HOLD") &&
         str_eq(menu_vname(MI_CHADD, 1), "ADD") && MI_TAB[MI_SCOPE] == MTAB_DISPLAY && MI_TAB[MI_PREVIEW] == MTAB_CONTROL &&
         MI_TAB[MI_CHADD] == MTAB_CONTROL;
    menu_put(MI_SCOPE, 1); ok &= ui_prefs2 == 1u && scope_mix && !ui_prefs && !(ui_rec_prefs & 0x40u);
    menu_put(MI_PREVIEW, 1); menu_put(MI_CHADD, 1); ok &= ui_prefs2 == 7u;
    menu_put(MI_SCOPE, 0); menu_put(MI_PREVIEW, 0); menu_put(MI_CHADD, 0); ok &= !ui_prefs2 && !scope_mix;
    bad += check("1.2 MENU: SCOPE OUT / MIX, STEP PREVIEW OFF / ON, CHORD ENTRY HOLD / ADD: 0 by default, a bit each", ok);

    /* STEP PREVIEW */
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    track_defaults_steps(t);
    for (i = 0; i < NSTEP; i++) step_clear(&t->step[i]);
    t->step[3] = (step_t){{60, 64}, 2, ST_NOTE, 0, 100};
    t->step[4] = (step_t){{67}, 1, ST_NOTE, 0, 100};
    t->step[4].probability = 101;                       /* (chance 0: the preview plays it all the same) */
    go_page(GR_ROLL);
    cursor_set(2);
    frame();
    memcpy(before, t->step, sizeof before);
    rec0 = song.rec; mc0 = motion.count;
    turn(EN_K1, 1);
    blocks(2);
    ok = ui.cursor == 3u && !aud_req && !gated(t, 128u);
    bad += check("STEP PREVIEW OFF (the default): moving the cursor sounds nothing", ok);
    menu_put(MI_PREVIEW, 1);
    turn(EN_K1, 1);
    ok = ui.cursor == 4u && aud_req;
    blocks(1);
    ok &= gated(t, 67) == 1u && gated(t, 128u) == 1u && !aud_req;
    turn(EN_K1, -1);
    blocks(1);
    ok &= ui.cursor == 3u && gated(t, 60) && gated(t, 64) && !gated(t, 67);
    blocks(div_samples(2) / CTL + 2u);
    ok &= !gated(t, 128u);
    bad += check("STEP PREVIEW ON: KNOB 1 to a step sounds it once on the track (a chord whole; chance not rolled), a 1/16 at GATE", ok);
    turn(EN_K1, -1);                                    /* cursor 2, a rest (the chord is at index 3) */
    turn(EN_PRESET, 1);                                 /* onto the chord: sounds it, writes nothing */
    blocks(1);
    ok = ui.cursor == 3u && gated(t, 60) && gated(t, 64);
    blocks(div_samples(2) / CTL + 2u);
    turn(EN_K1, -2);                                    /* (an empty step: nothing) */
    blocks(1);
    ok &= ui.cursor == 1u && !gated(t, 128u);
    bad += check("STEP PREVIEW: PRESETS from a rest moves the cursor and sounds it; an empty step sounds nothing", ok);
    ok = !memcmp(before, t->step, sizeof before) && song.rec == rec0 && motion.count == mc0 && !song.playing;
    bad += check("STEP PREVIEW never writes: the steps, the arming and the motion as they were", ok);
    song.playing = 1;
    turn(EN_K1, 2);
    ok = ui.cursor == 3u && !aud_req;
    aud_req = (uint16_t)(3u << 8 | 1u);                 /* (one asked for just before PLAY: dropped) */
    blocks(1);
    ok &= !aud_req && !gated(t, 60);
    song.playing = 0;
    blocks(div_samples(2) / CTL + 2u);
    song.rec = 1u;                                      /* armed: step recording, stopped: it sounds */
    turn(EN_K1, 1);
    blocks(1);
    ok &= ui.cursor == 4u && gated(t, 67) == 1u && !memcmp(before, t->step, sizeof before);
    blocks(div_samples(2) / CTL + 2u);
    tap_k(7);                                           /* step entry's moving on (HOLD): no preview */
    blocks(1);
    ok &= ui.cursor == 5u && step_note(&t->step[4], 0) == kb_map(t, 7) && !aud_req;
    song.rec = 0;
    bad += check("STEP PREVIEW: not while playing (live recording included); armed and stopped it sounds; step entry's move does not", ok);
    /* the DRUM grid: KNOB 1 STEP sounds the step's hits */
    ui_power_on();
    menu_put(MI_PREVIEW, 1);
    t = TSEL;
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    go_page(GR_ROLL);
    frame();
    tap_key(key_at(0, 2));                              /* a hit at step 3 (the selected lane) */
    blocks(div_samples(2) / CTL + 2u);
    for (i = 0; i < NVOICE; i++) t->v[i].active = 0;
    cursor_set(1);
    memcpy(before, t->step, sizeof before);
    turn(EN_K1, 1);
    ok = grid_on() && ui.cursor == 2u && aud_req && t->step[2].hit;
    blocks(1);
    for (i = 0, mc0 = 0; i < NVOICE; i++) mc0 += t->v[i].active;
    ok &= mc0 >= 1u && !memcmp(before, t->step, sizeof before);
    bad += check("STEP PREVIEW on the DRUM grid: KNOB 1 to a step sounds its hits, writes nothing", ok);
    menu_put(MI_PREVIEW, 0);

    /* CHORD ENTRY ADD */
    ui_power_on();
    project_not_empty();
    menu_put(MI_CHADD, 1);
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    track_defaults_steps(t);
    for (i = 0; i < NSTEP; i++) step_clear(&t->step[i]);
    t->step[5] = (step_t){{50}, 1, ST_NOTE, 0, 100};
    go_page(GR_ROLL);
    cursor_set(3);
    frame();
    press(B_REC);
    ok = song.rec == 1u && !transport_req && !song.playing && msg_is("KEYS ADD TO STEP");
    bad += check("#155 CHORD ENTRY ADD: REC on STEP arms step recording, KEYS ADD TO STEP", ok);
    tap_k(7);
    ok = t->step[3].n == 1u && step_note(&t->step[3], 0) == kb_map(t, 7) && t->step[3].time == ST_NOTE && ui.cursor == 3u;
    tap_k(11); tap_k(14);
    ok &= t->step[3].n == 3u && step_note(&t->step[3], 1) == kb_map(t, 11) && step_note(&t->step[3], 2) == kb_map(t, 14) && ui.cursor == 3u;
    tap_k(16); tap_k(18);
    ok &= t->step[3].n == 4u && step_note(&t->step[3], 3) == kb_map(t, 16) && ui.cursor == 3u;   /* (a 5th: no room) */
    bad += check("#155 ADD: keys one after another build the cursor step's chord (up to 4), the cursor stays", ok);
    tap_k(11);
    ok = t->step[3].n == 3u && step_note(&t->step[3], 0) == kb_map(t, 7) && step_note(&t->step[3], 1) == kb_map(t, 14) &&
         step_note(&t->step[3], 2) == kb_map(t, 16) && ui.cursor == 3u;
    tap_k(18);
    ok &= t->step[3].n == 4u && step_note(&t->step[3], 3) == kb_map(t, 18);
    bad += check("#155 ADD: a key whose note is there takes it out (the others keep their order); then there is room", ok);
    turn(EN_K1, 2);
    tap_k(9);
    ok = ui.cursor == 5u && t->step[5].n == 2u && step_note(&t->step[5], 0) == 50u && step_note(&t->step[5], 1) == kb_map(t, 9) &&
         t->step[3].n == 4u && !t->step[4].n;
    tap_k(9);
    ok &= t->step[5].n == 1u && step_note(&t->step[5], 0) == 50u && t->step[5].time == ST_NOTE;
    turn(EN_K1, -1);
    tap_k(9); tap_k(9);
    ok &= ui.cursor == 4u && !t->step[4].n && t->step[4].time == ST_REST;
    bad += check("#155 ADD: KNOB 1 moves on; a written step gets notes beside its own; the last one out leaves a REST", ok);
    t->step[6].time = ST_TIE;
    turn(EN_K1, 2);
    tap_k(7);
    ok = ui.cursor == 6u && t->step[6].time == ST_NOTE && t->step[6].n == 1u;
    t->p[P_VOICE] = V_MONO;
    turn(EN_K1, 1);
    tap_k(7); tap_k(9);
    ok &= ui.cursor == 7u && t->step[7].n == 1u && step_note(&t->step[7], 0) == kb_map(t, 9);
    tap_k(9);
    ok &= !t->step[7].n;
    bad += check("#155 ADD: a TIE step becomes a note step; MONO keeps one note (another key replaces it, the same takes it out)", ok);
    t->p[P_VOICE] = V_POLY;
    turn(EN_K1, 1);
    key_down(7); frame(); key_down(11); frame();       /* keys held together: each adds */
    key_up(7); key_up(11); frame();
    ok = ui.cursor == 8u && t->step[8].n == 2u;
    press(B_EDIT);
    ok &= t->step[8].n == 2u && ui.cursor == 8u && cur_page()->fam == FAM_EDIT;
    bad += check("#155 ADD: keys held together add each; the chord stays (EDIT opens the sound)", ok);
    song.rec = 0;
    memcpy(before, t->step, sizeof before);
    tap_k(7);
    bad += check("#155 ADD, not armed: the keys only play, nothing written", !memcmp(before, t->step, sizeof before));
    menu_put(MI_CHADD, 0);
    return bad;
}

static int test_piano_roll(void)
{
    int bad = 0, ok;
    uint32_t i, nb = 0, lo0, steps = 0;
    track_t *t;
    ui_power_on();
    t = TSEL;
    load_pat16(t, PATTERNS[0].note, PATTERNS[0].flags);               /* ACID: A2..A3, ties, accents, slides */
    t->p[P_ROOT] = 9; t->p[P_SCALE] = 2;
    go_page(GR_ROLL); ui.cursor = 0; ui.bank = 0;
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    ok = pr_columns_match(t, &nb);
    bad += check("piano roll: ACID's bars drawn at their notes' rows, ties carried, rests empty", ok && nb >= 10u);
    bad += check("  the view holds the page's notes (A2..A3 in 21 rows), MONO gray", proll.lo <= 45 && proll.lo + PR_ROWS - 1 >= 57 &&
                 screen_gray());
    t->p[P_VOICE] = V_POLY;                                           /* chords: up to 4 bars in a column */
    for (i = 0; i < 16u; i += 4u) {
        step_t *s = &t->step[i];
        s->time = ST_NOTE; s->n = 4; step_set_note(s, 0, 57); step_set_note(s, 1, 60); step_set_note(s, 2, 64); step_set_note(s, 3, 67);
        t->step[i + 1u].time = ST_TIE;
    }
    nb = 0;
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    bad += check("  4-note chords stacked, their ties carry all four", pr_columns_match(t, &nb));
    for (i = 0; i < NSTEP; i++) {                                     /* LEN 32, page 2 */
        step_clear(&t->step[i]);
        if (i >= 16u && i % 2u == 0u) { t->step[i].time = ST_NOTE; t->step[i].n = 1; step_set_note(&t->step[i], 0, (uint8_t)(72 + i % 7u)); }
    }
    t->p[P_SLEN] = 32; cursor_set(18);
    nb = 0;
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    bad += check("  LEN 32, the cursor on page 2: its 16 steps drawn", ui.bank == 1u && pr_columns_match(t, &nb) && nb == 8u);
    lo0 = proll.lo;                                                   /* two octaves up: ANIM OFF snaps at once */
    for (i = 16; i < 32u; i++) if (t->step[i].n) step_set_note(&t->step[i], 0, step_note(&t->step[i], 0) + 24u);
    frame(); steps = 1;
    bad += check("  notes moved two octaves: the view snaps at once (ANIM always OFF), then rests",
                 proll.lo == lo0 + 24u && steps == 1u && !proll.moving && (ui_prefs & PREF_ANIM_OFF));
    {
        uint32_t sig = ui.graph_sig;
        frame(); frame();
        bad += check("  nothing changed: the graph is not drawn again", ui.graph_sig == sig);
    }
    nb = 0;
    bad += check("  after the follow the bars match again", pr_columns_match(t, &nb));
    song.rec = 1;                                                     /* (#133: armed, the key writes the step) */
    key_down(7); frames(200);                                         /* a key held: its row lit on the strip */
    {
        int32_t r = (int32_t)proll.lo + PR_ROWS - 1 - (int32_t)kb_chord[7][0];
        uint16_t c = r >= 0 && r < PR_ROWS ? swap16(host_screen[(Y_GRAPH + PR_Y0 + (uint32_t)r * PR_RH + 1u) * 240u + PR_KX + 7]) : 0;
        bad += check("  a key held: its row on the keyboard strip is the accent", kb_chn[7] && c == T_ACCENT);
    }
    key_up(7); frames(100);
    song.rec = 0;
    return bad;
}

/* bug fixes, second round (UI) */
static uint32_t accent_in(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t n = 0;
    int32_t x, y;
    uint16_t a = (uint16_t)((T_ACCENT >> 8) | (T_ACCENT << 8));
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += host_screen[y * 240 + x] == a;
    return n;
}
static int test_bughunt_ui2(void)
{
    int bad = 0;
    uint32_t i;
    {   /* 1. a layer button pressed under a dialog and still held after it closes: dead, its keys stay notes */
        static const uint32_t BTN[2] = {B_FX, B_GLO};
        for (i = 0; i < 2u; i++) {
            uint32_t k = white(0), ok;
            ui_power_on(); TSEL->p[P_VOICE] = V_POLY;
            go_home(); frame();
            confirm_open(CF_CLEAR_SEQ, 0); frame();
            btn_down(BTN[i]); frame(); frames(100);
            btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();   /* the dialog closes, the button still held */
            frames(800);
            ok = !ui.confirm && (ui.ly_t0 & LY_DEAD) && !ui.layer && !kb_mask && !perf_mask;
            key_down(k); frame();
            ok = ok && kb_note[k] != KB_SILENT && gates() == 1u && !((kb_layer >> k) & 1u) && !perf_held && !ui.layer;
            key_up(k); frame();
            btn_up(BTN[i]); frame();
            bad += check(i ? "dead GLO held from a dialog: no map, the key plays a note"
                           : "dead FX held from a dialog: no map, the key plays a note, no effect", ok);
        }
    }
    {   /* 2. STEP entry with CHRD on writes what the key sounds, as live recording does */
        static const uint32_t KEY[2] = {7u, 8u};                      /* C4, C#4 (out of C major) */
        uint32_t v, j, ok = 1;
        for (v = 0; v < 2u; v++)
            for (j = 0; j < 2u; j++) {
                track_t *t;
                step_t e, r;
                ui_power_on(); t = TSEL; t->p[P_VOICE] = v ? V_MONO : V_POLY; t->p[P_CHRD] = CH_DIA3; t->p[P_SCALE] = 0;
project_not_empty();                 track_defaults_steps(t); go_page(GR_ROLL); cursor_set(0); frame(); song.rec = 1;   /* (#133: armed) */
                key_down(KEY[j]); frame();
                e = t->step[0];
                key_up(KEY[j]); frame(); song.rec = 0;
                ok &= e.n == (v ? 1u : 3u) && step_note(&e, 0) == 60u && (v || (step_note(&e, 1) == 64u && step_note(&e, 2) == 67u));
                ui_power_on(); t = TSEL; t->p[P_VOICE] = v ? V_MONO : V_POLY; t->p[P_CHRD] = CH_DIA3; t->p[P_SCALE] = 0;
                track_defaults_steps(t); go_home(); frame();
                song.rec = 1; song.playing = 1; t->seq_idx = 3; t->seq_pos = 0;
                fm1_in.notes = 1u << KEY[j]; keyboard_block();
                r = t->step[3];
                fm1_in.notes = 0; keyboard_block();
                song.rec = 0; song.playing = 0;
                ok &= r.n == e.n;
                for (uint32_t q = 0; q < r.n; q++)
                    ok &= step_note(&r, q) == step_note(&e, q);
            }
        bad += check("STEP entry with CHRD: POLY the chord, MONO its root, as live recording writes", ok);
    }
    {   /* 3. MIXER: the knob just turned is the one drawn in the accent (K1 LEVEL, K2 PAN, K3 REV, K4 the PLAY badge) */
        uint32_t k, a[4][4], ok = 1;
        for (k = 0; k < 4u; k++) {
            int32_t X = CARD_X(0), Y = Y_GRAPH;
            ui_power_on();
            settings.palette = 1; palette_set(1);
            go_page(GR_TRK); frame(); frame();
            turn(EN_K1 + k, 1);                                       /* (K4 right: PLAY SOLO, its badge shown) */
            ui.force = 1; frame();
            a[k][0] = accent_in(X + 4, Y + 37, X + 38, Y + 71);       /* LEVEL */
            a[k][1] = accent_in(X + 3, Y + 84, X + 28, Y + 115);      /* PAN */
            a[k][2] = accent_in(X + 30, Y + 84, X + 55, Y + 115);     /* REV */
            a[k][3] = accent_in(X + 22, Y + 4, X + 56, Y + 20);       /* the PLAY badge */
        }
        for (k = 0; k < 4u; k++) {
            uint32_t j;
            for (j = 0; j < 4u; j++)
                ok &= j == k ? a[k][j] > 0u : a[k][j] == 0u;
        }
        bad += check("MIXER: K1..K4 light LEVEL / PAN / REV / the PLAY badge, nothing else", ok);
    }
    {   /* 4. renaming the slot the music came from renames the music: the next SAVE prefills the new name */
        char pn[16];
        uint32_t ok;
        ui_power_on();
        go_page(GR_SLOTS); song.g[G_SLOT] = 2;
        project_save_as(1, "OLD");
        project_save_as(2, "OTHER");
        project_load(1); frame();
        ok = str_eq(proj_name, "OLD");
        project_rename(2, "ELSE");
        ok &= str_eq(proj_name, "OLD");                               /* another slot: the music's name stays */
        press(B_EDIT);                                                /* rename B on the slot list */
        while (nm.len) { nm_do(NB_DEL); }
        nm_white(13); frames(900);
        str_cpy(nm.s, "NEW", sizeof nm.s); nm.len = nm.cur = 3; nm.key = 0;
        press(B_OCTUP);
        project_name(1, pn);
        ok &= str_eq(pn, "NEW") && str_eq(proj_name, "NEW");
        turn(EN_K4, 1); press(B_OCTUP);                               /* SAVE to B: OVERWRITE? */
        press(B_OCTUP);                                               /* YES: NAME */
        ok &= name_on() && str_eq(nm.s, "NEW");
        press(B_OCTDN);
        bad += check("rename of the loaded slot: the music's name follows, SAVE prefills it", ok);
    }
    {   /* 5. NAME: PLAY stops a transport started meanwhile (so the name can be saved), never starts it; REC ignored */
        uint32_t ok;
        ui_power_on(); go_page(GR_USER); ui.uslot = 3; press(B_OCTUP);
        ok = name_on();
        press(B_PLAY); frame();
        ok &= !transport_req && !song.playing && name_on();          /* stopped: PLAY starts nothing */
        press(B_REC); frame();
        ok &= !song.rec && !transport_req && name_on();
        song.playing = 1;                                             /* an external MIDI Start / the editor */
        press(B_OCTUP);
        ok &= name_on() && msg_is("STOP TO SAVE");
        press(B_REC); frame();
        ok &= !song.rec && !transport_req;
        press(B_PLAY); frame();
        ok &= transport_req == 2u && name_on();                       /* PLAY stops it, NAME stays */
        stop_transport();
        press(B_OCTUP);
        ok &= !name_on() && up_used(3);
        bad += check("NAME: PLAY stops a running transport (never starts), REC ignored, then OCT+ saves", ok);
    }
    {   /* 6. the piano roll's lowest C name ("C-1") stays inside the panel: nothing drawn left of x 3 */
        uint32_t p, x, y, out = 0;
        uint16_t bg;
        for (p = 0; p < NPALETTES; p++) {
            track_t *t;
            uint8_t lo = 0, hi = 127;
            ui_power_on(); t = TSEL; track_defaults_steps(t); t->p[P_SLEN] = 16;
            settings.palette = (uint8_t)p; palette_set(p);
            t->step[0].time = t->step[5].time = ST_NOTE; t->step[0].n = t->step[5].n = 1;
            step_set_note(&t->step[0], 0, lo); step_set_note(&t->step[5], 0, hi);
            go_page(GR_ROLL); cursor_set(0); ui.force = 1; frame(); frame();
            bg = (uint16_t)((T_BG >> 8) | (T_BG << 8));
            for (y = Y_GRAPH; y < Y_GRAPH + H_GRAPH; y++)
                for (x = 0; x < 3u; x++)
                    out += host_screen[y * 240u + x] != bg;
        }
        bad += check("piano roll: the C-1 label stays inside the panel (every palette)", !out);
    }
    {   /* 7. EDIT: INIT is drawn in the cell after the engines, and that cell's white key is the one that inits */
        ui_power_on(); go_home(); frame();
        btn_down(B_EDIT); frames(500);
        key_down(white(LY_INIT)); frame(); key_up(white(LY_INIT)); frame();
        bad += check("EDIT hold map removed: INIT key with EDIT held does not open CF_INIT_SOUND",
                     !ui.layer && ui.confirm != CF_INIT_SOUND);
        btn_up(B_EDIT); frame();
    }
    return bad;
}

#ifndef UI_TEST_NO_MAIN
/* L is a sparse face (tools/gen_aa_font.py L_CHARS): every string drawn in it has all its glyphs */
static int test_large_face(void)
{
    static const char *const FIXED[] = {"ARAMINTA", "0123456789"};  /* ui_menu.c ABOUT; ui_draw.c draw_uboot */
    uint32_t i, missing = 0;
    const char *s;
    for (i = 0; i < NB + NE + 2u; i++)                              /* ui_input.c setup_show: the control names */
        for (s = i < NB ? B_NAME[i] : i < NB + NE ? E_NAME[i - NB] : FIXED[i - NB - NE]; *s; s++)
            missing += glyph_at(&AF_L, (uint8_t)*s) < 0;
    return check("the L face holds every glyph of the strings drawn in it", !missing);
}

#if FELUCCA_FM4
/* The DIGITAL algorithm charts (ui_graph.c FM_CELL / FM_MOD) against the DSP: src/eng_digital.c's switch (alg)
 * in digital_render_legacy and digital_render_custom, written out here as "source>destination" routes and the
 * operators mixed to the output. Op 4's feedback is on every algorithm (drawn always). Also: the carriers sit
 * on the bottom row and only they, a modulator one row above what it modulates, no two operators in a cell,
 * every operator heard, and the eight charts distinct. */
static int test_fm_charts(void)
{
    static const char *const DSP[8][2] = {
        {"4>3 3>2 2>1", "1"},          /* o3 = op(o4), o2 = op(o3), s = op(o2) */
        {"3>2 4>2 2>1", "1"},          /* o2 = op((o3 + o4) / 2), s = op(o2) */
        {"3>2 2>1 4>1", "1"},          /* o2 = op(o3), s = op((o2 + o4) / 2) */
        {"4>3 2>1 3>1", "1"},          /* o3 = op(o4), s = op((o2 + o3) / 2) */
        {"4>3 2>1", "13"},             /* o3 = op(o4), o1 = op(o2), s = (o1 + o3) / 2 */
        {"4>1 4>2 4>3", "123"},        /* o1, o2, o3 = op(o4), s = (o1 + o2 + o3) / 3 */
        {"4>3", "123"},                /* o3 = op(o4), s = (o1 + o2 + o3) / 3 */
        {"", "1234"}};                 /* s = (o1 + o2 + o3 + o4) / 4 */
    uint32_t a, b, k, j, routes = 1, layout = 1, distinct = 1;
    for (a = 0; a < 8u; a++) {
        uint32_t want = 0, car = 0, ok = 1;
        const char *s;
        for (s = DSP[a][0]; *s; s++)
            if (*s == '>') want |= 1u << (4u * (uint32_t)(s[1] - '1') + (uint32_t)(s[-1] - '1'));
        for (s = DSP[a][1]; *s; s++) car |= 1u << (*s - '1');
        if (FM_MOD[a] != want) { printf("ui: algorithm %u: routes %04x, the DSP %04x\n", a + 1u, FM_MOD[a], want); routes = 0; }
        for (k = 0; k < 4u; k++) {
            uint32_t row = FM_CELL[a][k] >> 4, outs = 0;
            if ((row == 0u) != ((car >> k) & 1u)) ok = 0;
            for (j = 0; j < 4u; j++) {
                if (j != k && FM_CELL[a][j] == FM_CELL[a][k]) ok = 0;
                if (FM_MOD[a] >> (4u * j + k) & 1u) { outs++; if (row != (FM_CELL[a][j] >> 4) + 1u) ok = 0; }
            }
            if (!outs && !((car >> k) & 1u)) ok = 0;
        }
        if (!ok) { printf("ui: algorithm %u: the chart's layout is off\n", a + 1u); layout = 0; }
        for (b = 0; b < a; b++)
            if (FM_MOD[a] == FM_MOD[b] && !memcmp(FM_CELL[a], FM_CELL[b], 4)) distinct = 0;
    }
    return check("DIGITAL charts: the 8 algorithms' routes and carriers equal eng_digital.c's", routes) +
           check("DIGITAL charts: carriers on the bottom row, modulators a row up, one per cell, distinct", layout && distinct);
}
#endif

/* The FM6 algorithm charts (ui_graph.c graph_fm6: FM6_CELL, its routes fm6_routes and feedback operator
 * fm6_fb_op read from fm6_core.c FM6_ALG) against the 32 algorithms of 6-operator FM written out here as
 * "source>destination" routes, the carriers and the operator with feedback (fm6_core.c's: on algorithms 4 and
 * 6 the loop is the sixth operator's own, as msfa plays it). Also against the core itself: the carriers
 * fm6_carriers (fm6_car_ops), the feedback flags. The layout: the carriers on the bottom row and only they, a modulator one
 * row above everything it modulates, one operator per cell, columns 0..5, at most 4 rows. (The charts as drawn:
 * tests/ui_render.c's lint, fm6_lint.) */
static int test_fm6_charts(void)
{
    static const struct { const char *r, *car; char fb; } ALGS[32] = {
        {"2>1 4>3 5>4 6>5", "13", '6'}, {"2>1 4>3 5>4 6>5", "13", '2'}, {"2>1 3>2 5>4 6>5", "14", '6'},
        {"2>1 3>2 5>4 6>5", "14", '6'}, {"2>1 4>3 6>5", "135", '6'}, {"2>1 4>3 6>5", "135", '6'},
        {"2>1 4>3 5>3 6>5", "13", '6'}, {"2>1 4>3 5>3 6>5", "13", '4'}, {"2>1 4>3 5>3 6>5", "13", '2'},
        {"2>1 3>2 5>4 6>4", "14", '3'}, {"2>1 3>2 5>4 6>4", "14", '6'}, {"2>1 4>3 5>3 6>3", "13", '2'},
        {"2>1 4>3 5>3 6>3", "13", '6'}, {"2>1 4>3 5>4 6>4", "13", '6'}, {"2>1 4>3 5>4 6>4", "13", '2'},
        {"2>1 3>1 5>1 4>3 6>5", "1", '6'}, {"2>1 3>1 5>1 4>3 6>5", "1", '2'}, {"2>1 3>1 4>1 5>4 6>5", "1", '3'},
        {"2>1 3>2 6>4 6>5", "145", '6'}, {"3>1 3>2 5>4 6>4", "124", '3'}, {"3>1 3>2 6>4 6>5", "1245", '3'},
        {"2>1 6>3 6>4 6>5", "1345", '6'}, {"3>2 6>4 6>5", "1245", '6'}, {"6>3 6>4 6>5", "12345", '6'},
        {"6>4 6>5", "12345", '6'}, {"3>2 5>4 6>4", "124", '6'}, {"3>2 5>4 6>4", "124", '3'},
        {"2>1 4>3 5>4", "136", '5'}, {"4>3 6>5", "1235", '6'}, {"4>3 5>4", "1236", '5'},
        {"6>5", "12345", '6'}, {"", "123456", '6'}};
    uint32_t a, k, j, routes = 1, layout = 1, core = 1;
    for (a = 0; a < 32u; a++) {
        uint8_t m[6], want[6] = {0, 0, 0, 0, 0, 0};
        uint32_t car = 0, ok = 1, fbs = 0;
        const char *s;
        for (s = ALGS[a].r; *s; s++)
            if (*s == '>') want[s[1] - '1'] |= (uint8_t)(1u << (s[-1] - '1'));
        for (s = ALGS[a].car; *s; s++) car |= 1u << (*s - '1');
        fm6_routes(a, m);
        if (memcmp(m, want, 6) || fm6_fb_op(a) != (uint32_t)(ALGS[a].fb - '1')) {
            printf("ui: FM6 algorithm %u: the chart's routes or feedback differ from the written ones\n", a + 1u);
            routes = 0;
        }
        for (k = 0; k < 6u; k++)                                    /* the core: who writes the output, who has FB */
            fbs += (FM6_ALG[a][k] & 0xC0u) == 0xC0u;
        if (fm6_car_ops(a) != car || fbs != 1u) {
            printf("ui: FM6 algorithm %u: fm6_core.c's carriers / feedback differ\n", a + 1u);
            core = 0;
        }
        for (k = 0; k < 6u; k++) {
            uint32_t row = FM6_CELL[a][k] >> 4, outs = 0;
            if ((row == 0u) != ((car >> k) & 1u) || (FM6_CELL[a][k] & 0x0Fu) > 5u || row > 3u) ok = 0;
            for (j = 0; j < 6u; j++) {
                if (j != k && FM6_CELL[a][j] == FM6_CELL[a][k]) ok = 0;
                if (m[j] >> k & 1u) { outs++; if (FM6_CELL[a][k] >> 4 != (FM6_CELL[a][j] >> 4) + 1u) ok = 0; }
            }
            if (!outs && !((car >> k) & 1u)) ok = 0;               /* every operator heard */
        }
        if (!ok) { printf("ui: FM6 algorithm %u: the chart's layout is off\n", a + 1u); layout = 0; }
    }
    return check("FM6 charts: the 32 algorithms' routes, carriers and feedback operator equal fm6_core.c's", routes && core) +
           check("FM6 charts: carriers on the bottom row, modulators a row up, one per cell, at most 4 rows", layout);
}

#if !FELUCCA_FM4
/* DIGITAL retired (src/fm4_convert.c): engine 1 is on no track and in no list; its sounds arrive as FM6 with a patch
 * of their own: a user preset of it, its preset numbers (set_engine_of / apply_preset_to), a track that got engine 1
 * any other way (the main loop's net); the favourites of its presets move to FM6's; the power-on pad is FM6 PAD */
static int test_fm4_retired(void)
{
    int bad = 0, ok = 1;
    uint32_t i, k, e, total, seen = 0, all = 0;
    int16_t p[P_COUNT];
    uint8_t v[FP_SIZE + 1u];
    for (e = 0; e < NENGINES; e++)
        if (eng_ok(e))
            all |= 1u << e;                            /* pickable only (not DIGITAL / SLICE) */
    ui_power_on();
    bad += check("power-on: track 2 is FM6 PAD (TRK_DEF, was DIGITAL PAD)", trk[1].eng_req == ENGI_FM6 &&
                 str_eq(ENGINES[ENGI_FM6]->presets[trk[1].preset].name, "PAD"));
    preset_all_pos(&total);
    for (i = 0; i < total; i++) {
        e = preset_all_at(i, &k);
        if (e < NENGINES)
            seen |= 1u << e;
    }
    bad += check("PRESETS: the list holds every engine's presets but DIGITAL's", seen == all && NENG_SHOWN == 14u + FELUCCA_FM4);
    go_page(GR_BROWSE);
    set_engine_of(TSEL, 0);
    for (i = 0, seen = 0; i < NENG_SHOWN; i++) {
        turn(EN_K2, 1);
        seen |= 1u << TSEL->eng_req;
    }
    bad += check("PRESETS KNOB 2: the engines in order, DIGITAL skipped, back to the first",
                 seen == all && TSEL->eng_req == 0u && eng_step(0, 1) == ENGI_FM6 && eng_step(ENGI_FM6, 1) == 2u &&
                 eng_step(ENGI_FM6, -1) == 0u && eng_step(0, -1) == ENGI_WT && eng_step(ENGI_WT, 1) == 0u);
    {   /* the display order (engines.c ENGINE_ORDER): every engine one can pick once; the PRESETS list follows it */
        static const char *const ORDER[] = {"ANALOG", "FM6", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN",
                                            "PHYS", "NOISE", "DRUM-X", "DRUMS", "WAVETABLE"};
        uint32_t last = 0xFFu, r = 0, n = 0;
        ok = NENG_SHOWN == NELEM(ORDER);
        for (i = 0; ok && i < NENG_SHOWN; i++)
            ok &= str_eq(ENGINES[eng_vis(i)]->name, ORDER[i]) && eng_rank(eng_vis(i)) == i && eng_ok(eng_vis(i));
        preset_all_pos(&total);
        for (i = 0; i < total; i++) {                  /* the list's engines, each once, in that order */
            e = preset_all_at(i, &k);
            if (e >= NENGINES || e == last)
                continue;
            ok &= n < NENG_SHOWN && e == eng_vis(n);
            n++;
            last = e;
            r++;
        }
        bad += check("engines shown ANALOG FM6 PHASE ... PHYS DRUM (ENGINE_ORDER); PRESETS lists them so",
                     ok && r == NENG_SHOWN);
    }
    /* a user preset stored with engine 1: kept as it is, it loads as FM6 with the converted patch */
    for (i = 0; i < P_COUNT; i++)
        p[i] = param_desc_of(ENGI_DIGITAL, i)->def;
    fm4_preset_values(p, 5);                           /* DIGITAL PAD */
    {
        up_rec_t r;
        memset(&r, 0, sizeof r);
        r.used = UP_USED;
        r.ver = UP_VER;
        r.engine = ENGI_DIGITAL;
        r.np = P_COUNT;
        memcpy(r.name, "OLD PAD", 7);
        for (i = 0; i < P_COUNT; i++)
            up_set_value(&r, i, p[i]);
        up_put(7, &r);
    }
    fm4_convert(p, v);
    up_load(7);
    bad += check("a DIGITAL user preset loads as FM6: the converted patch its own (SLOT OWN), preset = FM6 PAD",
                 TSEL->eng_req == ENGI_FM6 && TSEL->preset == 4u && TSEL->p[P_E7] == FM6_OWN && !TSEL->p[P_E0] &&
                 !memcmp(fm6_patch[song.sel], v, FP_SIZE) && fm6_slot[song.sel] == FM6_OWN && TSEL->user == 8u);
    frame();
    bad += check("  .. and the main loop keeps that patch (not a factory one)", !memcmp(fm6_patch[song.sel], v, FP_SIZE));
    eng_list_pos(&total);
    bad += check("  the record stays DIGITAL in the bank, listed with FM6's sounds (EDIT KNOB 2)",
                 up_rec(7)->engine == ENGI_DIGITAL && up_engine(7) == ENGI_FM6 &&
                 total == ENGINES[ENGI_FM6]->npresets + 1u);
    up_store(8, "AGAIN");
    {
        uint8_t pk[FM6_PACKED], w[FP_SIZE + 1u];
        bad += check("  saved again: an FM6 user preset with SLOT OWN and the converted patch beside it",
                     up_rec(8)->engine == ENGI_FM6 && up_value(up_rec(8), P_E7) == FM6_OWN && !upf_get(8, pk) &&
                     (fm6_unpack(pk, w), !memcmp(w, v, FP_SIZE)));
        fm6_load_slot(song.sel, 2);                    /* another patch on the track, then the preset again */
        up_load(8);
        frame();
        bad += check("  .. which loads back: the same patch, SLOT OWN", !memcmp(fm6_patch[song.sel], v, FP_SIZE) &&
                     TSEL->p[P_E7] == FM6_OWN && fm6_slot[song.sel] == FM6_OWN);
    }
    /* preset numbers of engine 1 */
    set_engine_of(TSEL, ENGI_DIGITAL);
    bad += check("engine 1 asked for: DIGITAL E.PIANO converted (FM6, PTCH TINE EP, the patch named E.PIANO)",
                 TSEL->eng_req == ENGI_FM6 && TSEL->preset == 0u && !memcmp(fm6_patch[song.sel] + FP_NAME, "E.PIANO", 7));
    TSEL->eng_req = ENGI_DIGITAL;                      /* (the ISR's view: no other path does this) */
    apply_preset_to(TSEL, 1);
    bad += check("a DIGITAL preset number (apply_preset_to): its sound converted (BELL)",
                 TSEL->eng_req == ENGI_FM6 && TSEL->preset == 1u && !memcmp(fm6_patch[song.sel] + FP_NAME, "BELL", 4));
    for (i = 0; i < P_COUNT; i++)
        trk[2].p[i] = param_desc_of(ENGI_DIGITAL, i)->def;
    fm4_preset_values(trk[2].p, 2);
    trk[2].eng_req = ENGI_DIGITAL;
    frame();
    bad += check("a track left on engine 1 any other way: the main loop converts it (BASS -> FM BASS)",
                 trk[2].eng_req == ENGI_FM6 && trk[2].preset == 2u);
    {   /* favourites of DIGITAL presets (settings PER4) -> the FM6 presets that cover them */
        persist_t pe;
        memset(&pe, 0, sizeof pe);
        pe.magic = PERSIST_MAGIC;
        pe.panel = PANEL_DEFAULT;
        pe.favorites.factory[ENGI_DIGITAL][0] = 1u << 5 | 1u << 7;   /* PAD, FUNK KEY */
        settings_import(&pe, (int)sizeof pe);
        bad += check("favourites: DIGITAL PAD / FUNK KEY -> FM6 PAD / PLUCK, engine 1's cleared",
                     favorite_has(ENGI_FM6, 4) && favorite_has(ENGI_FM6, 7) && !favorites.factory[ENGI_DIGITAL][0]);
        memset(&favorites, 0, sizeof favorites);
    }
    return bad;
}
#endif

/* #38: the keys of the notes the selected track's sequencer and ARP sound light while it plays */
static int test_play_leds(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    uint32_t i;
    ui_power_on();
    song.sel = 0;
    t->step[0] = (step_t){.nb = {60, 64}, .n = 2, .time = ST_NOTE};
    t->step[1] = (step_t){.nb = {100}, .n = 1, .time = ST_NOTE};
    trk[1].step[0] = (step_t){.nb = {67}, .n = 1, .time = ST_NOTE};
    bad += check("stopped, nothing held: no key lit", key_leds(0) == 0u && play_leds() == 0u);
    seq_start();
    events_block(CTL);
    bad += check("playing: C4 and E4 of step 1 light keys 8 and 12 (from F3), track 2's G4 does not",
                 t->seq_n == 2u && trk[1].seq_n == 1u && key_leds(0) == (1u << 7 | 1u << 11));
    song.sel = 1;
    bad += check("the selected track's notes: track 2's G4 on key 15", play_leds() == 1u << 14);
    song.sel = 0;
    fm1_in.notes = 1u << 0;
    bad += check("a key held still lights with them", key_leds(0) == (1u << 0 | 1u << 7 | 1u << 11));
    fm1_in.notes = 0;
    song.octave = -1;
    ok = play_leds() == (1u << 19 | 1u << 23);
    song.octave = 1;
    ok &= play_leds() == 0u;                           /* (C4 and E4 below the keys an octave up) */
    song.octave = 0;
    t->p[P_TRANS] = 2;
    ok &= play_leds() == (1u << 5 | 1u << 9);
    t->p[P_TRANS] = 0;
    bad += check("the keys follow the octave and TRN; notes off the keyboard are not shown", ok);
    t->p[P_QUANT] = 1;                                 /* ALL(=KBD keys) in C major: C#4 -> C4 */
    t->p[P_SCALE] = 0;
    t->p[P_ROOT] = 0;
    ok = kb_map(t, 8) == 60u && play_leds() == (1u << 7 | 1u << 11);
    t->p[P_QUANT] = 0;
    bad += check("MAP ALL(=KBD): only the key of the note itself lights, not the keys rounding onto it", ok);
    t->arp_note = 65;
    bad += check("the ARP's note lights its key too", play_leds() == (1u << 7 | 1u << 11 | 1u << 12));
    t->arp_note = 0;
    events_block(div_samples((uint32_t)t->p[P_SDIV]) + CTL);
    bad += check("a note above the keyboard (G#7) lights nothing", t->seq_n == 1u && t->seq_notes[0] == 100u &&
                 play_leds() == 0u);
    transport_req = 2;
    events_block(CTL);
    bad += check("stopped: the notes end, their keys go dark", !song.playing && key_leds(0) == 0u);
    /* the layer, the grid and NAME keep their keys */
    seq_start();
    events_block(CTL);
    ui.layer = 1;
    ok = key_leds(0) == layer_leds(0);
    ui.layer = 0;
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    t->step[0] = (step_t){.hit = 1u << DV_KICK, .time = ST_NOTE};
    transport_req = 2; events_block(CTL);
    seq_start(); events_block(CTL);
    i = play_leds();
    ok &= t->seq_n == 1u && i != 0u && kb_map(t, (uint32_t)__builtin_ctz(i)) == DRUM_LANE_NOTE[DV_KICK];
    go_seq_step();
    frame();
    ok &= grid_on() && key_leds(0) == grid_leds();
    bad += check("the layer's map and the DRUM grid keep their keys; elsewhere a kit's hits light the keys that play them", ok);
    transport_req = 2; events_block(CTL);
    return bad;
}

/* Discussion #89: on SEQ > STEP (the piano roll) while stopped the keys of the cursor step's notes light, at the
 * octave now (the lowest key that gives each); with the keys held; playing, what plays instead (#38); not on
 * other pages, not for a REST step; the DRUM grid keeps its own map */
static int test_cursor_step_leds(void)
{
    int bad = 0, ok;
    track_t *t;
    ui_power_on();
    song.sel = 0;
    t = TSEL;
    t->step[0] = (step_t){.nb = {60, 64}, .n = 2, .time = ST_NOTE};
    t->step[1] = (step_t){.nb = {100}, .n = 1, .time = ST_NOTE};
    t->step[2] = (step_t){.nb = {62}, .n = 1, .time = ST_REST};
    t->step[3] = (step_t){.nb = {67}, .n = 1, .time = ST_TIE};
    bad += check("#89 HOME, stopped: no step's keys lit", key_leds(0) == 0u);
    go_page(GR_ROLL);
    frame();
    bad += check("#89 STEP, stopped: the cursor step's C4 and E4 light keys 8 and 12",
                 ui.cursor == 0u && key_leds(0) == (1u << 7 | 1u << 11));
    song.octave = -1;
    ok = key_leds(0) == (1u << 19 | 1u << 23);
    song.octave = 1;
    ok &= key_leds(0) == 0u;                            /* (below the keys an octave up) */
    song.octave = 0;
    bad += check("#89 the step's keys follow the octave; notes off the keyboard are not shown", ok);
    fm1_in.notes = 1u << 0;
    bad += check("#89 a key held lights with them", key_leds(0) == (1u << 0 | 1u << 7 | 1u << 11));
    fm1_in.notes = 0;
    turn(EN_K1, 1);
    ok = ui.cursor == 1u && key_leds(0) == 0u;          /* G#7: off the keyboard */
    turn(EN_K1, 1);
    ok &= ui.cursor == 2u && key_leds(0) == 0u;         /* a REST */
    turn(EN_K1, 1);
    ok &= ui.cursor == 3u && key_leds(0) == 1u << 14;   /* a TIE carries its G4 */
    turn(EN_K1, 1);
    ok &= ui.cursor == 4u && key_leds(0) == 0u;         /* empty */
    bad += check("#89 KNOB 1 moves the cursor and the keys follow its step (REST, empty, off the keys: dark)", ok);
    turn(EN_K1, -4);
    seq_start();
    events_block(CTL);
    bad += check("#89 playing: the keys show what plays, not the cursor step", song.playing &&
                 key_leds(0) == play_leds());
    turn(EN_K1, 3);
    bad += check("#89   (the cursor on G4's step changes nothing while playing)", key_leds(0) == play_leds() &&
                 !((key_leds(0) >> 14) & 1u));
    transport_req = 2;
    events_block(CTL);
    frame();
    bad += check("#89 stopped again: the cursor step's keys", !song.playing && key_leds(0) == 1u << 14);
    go_page(GR_EVENTS);
    frame();
    bad += check("#89 AUTOMATION (same cursor): no step keys", key_leds(0) == 0u);
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    t->step[0] = (step_t){.hit = 1u << DV_KICK, .time = ST_NOTE};
    go_page(GR_ROLL);
    frame();
    bad += check("#89 the DRUM grid keeps its map", grid_on() && key_leds(0) == grid_leds());
    return bad;
}

/* Discussion #81: the notes MIDI IN holds on the selected track light their keys as the sequencer's do: USB and TRS,
 * routed by ROUT; at the octave now, the lowest key; off the keyboard nothing; the pedal holds them; the layer's map
 * and the DRUM grid keep their keys */
static void usb_note(uint32_t ch, uint32_t note, uint32_t vel)   /* a USB-MIDI packet, as usb.c queues it */
{
    uint32_t st = (vel ? 0x90u : 0x80u) | ch;
    midi_in_event((st >> 4) | st << 8 | note << 16 | vel << 24);
    events_block(CTL);
}
static void trs_note(uint32_t a, uint32_t b, uint32_t c)        /* TRS MIDI IN: midi_uart.c's parser */
{
    um_byte(a); um_byte(b); um_byte(c);
    events_block(CTL);
}
static int test_midi_leds(void)
{
    int bad = 0, ok;
    ui_power_on();
    memset(&um, 0, sizeof um);
    song.sel = 0;
    usb_note(0, 60, 100);                              /* channel 1 -> T1 (ROUT CH1-4) */
    bad += check("#81 USB MIDI C4 into the selected track lights key 8 (C4)", midi_sel_on[0][60] == 1u &&
                 key_leds(0) == 1u << 7 && play_leds() == 1u << 7);
    usb_note(1, 67, 100);                              /* channel 2 -> T2: not the selected track */
    ok = key_leds(0) == 1u << 7;
    song.sel = 1;
    ok &= key_leds(0) == 1u << 14;
    song.sel = 0;
    bad += check("  another track's MIDI notes light nothing; selected, its G4 lights key 15", ok);
    usb_note(1, 67, 0);
    usb_note(0, 60, 0);
    bad += check("  note-off: dark", key_leds(0) == 0u && !midi_owners[0]);
    trs_note(0x90, 64, 90);                           /* TRS: E4 on channel 1 */
    ok = key_leds(0) == 1u << 11;
    trs_note(0x80, 64, 0);
    bad += check("  TRS MIDI IN: E4 lights key 12 while held", ok && key_leds(0) == 0u);
    song.g[G_ROUTE] = 1;                               /* ROUT SEL: every channel to the selected track */
    song.sel = 2;
    usb_note(9, 62, 100);
    ok = midi_sel_on[9][62] == 3u && key_leds(0) == 1u << 9;
    usb_note(9, 62, 0);
    song.g[G_ROUTE] = 0;
    song.sel = 0;
    bad += check("  ROUT SEL: channel 10 into the selected track lights its key", ok && key_leds(0) == 0u);
    usb_note(0, 60, 100);
    song.octave = -1;
    ok = key_leds(0) == 1u << 19;
    song.octave = 1;
    ok &= key_leds(0) == 0u;                            /* (C4 below the keys an octave up) */
    song.octave = 0;
    usb_note(0, 60, 0);
    usb_note(0, 100, 100);                             /* G#7: above the keyboard */
    usb_note(0, 40, 100);                              /* E2: below it */
    ok &= key_leds(0) == 0u && midi_owners[0] == 2u;
    usb_note(0, 100, 0);
    usb_note(0, 40, 0);
    bad += check("  the keys follow the octave; notes off the keyboard are not shown", ok);
    trk[0].p[P_QUANT] = 1;                             /* ALL(=KBD keys) in C major: C#4 -> C4 */
    trk[0].p[P_SCALE] = 0;
    trk[0].p[P_ROOT] = 0;
    usb_note(0, 60, 100);
    ok = kb_map(&trk[0], 8) == 60u && key_leds(0) == 1u << 7;
    usb_note(0, 60, 0);
    trk[0].p[P_QUANT] = 0;
    bad += check("  MAP ALL(=KBD): only the lowest key giving the note lights", ok && key_leds(0) == 0u);
    midi_event(0xB0, 0, 64, 127);                      /* the pedal down: a released note still sounds */
    usb_note(0, 65, 100);
    usb_note(0, 65, 0);
    ok = key_leds(0) == 1u << 12;
    midi_event(0xB0, 0, 64, 0);
    bad += check("  a note the pedal holds stays lit until the pedal is up", ok && key_leds(0) == 0u);
    fm1_in.notes = 1u << 0;                            /* with a key held and the sequencer */
    usb_note(0, 60, 100);
    ok = key_leds(0) == (1u << 0 | 1u << 7);
    fm1_in.notes = 0;
    trk[0].step[0] = (step_t){.nb = {64}, .n = 1, .time = ST_NOTE};
    seq_start();
    events_block(CTL);
    ok &= key_leds(0) == (1u << 7 | 1u << 11);
    bad += check("  with a key held and the sequencer's notes: all of them lit", ok);
    ui.layer = LAYER_SCL;
    {
        uint32_t b1, b2;
        ok = key_leds(&b1) == layer_leds(&b2) && b1 == b2 && b1;
    }
    ui.layer = 0;
    go_seq_step();                                      /* STEP on a melodic track: the keys as elsewhere */
    frame();
    ok &= !grid_on() && (key_leds(0) & (1u << 7));
    set_engine_of(&trk[0], ENGI_DRUM);
    trk[0].engine = trk[0].eng_req;
    frame();
    ok &= grid_on() && key_leds(0) == grid_leds();
    bad += check("  the layer's map and the DRUM grid keep their keys over the MIDI notes", ok);
    transport_req = 2;
    events_block(CTL);
    usb_note(0, 60, 0);
    return bad;
}

/* #119: what can be pressed breathes (hal/fm1_input.h fm1_led_breath: dark .. ~60 % of lit), never a hard blink: every
 * layer, held and locked (double tap), in every MENU > LEDS mode: its keys that can be pressed and its button in the
 * breath set only (neither lit nor the glow), the ones in effect lit, the picture the same at any time (no 250 ms
 * toggle left); OCT+ in a dialog likewise; closing the layer leaves nothing breathing; the glow's level and
 * the breath's peak follow LEDS (fm1_led_dim_level: DIM LO the darker glow and ~30 %, else DIM HI's and ~60 %) */
static uint32_t breath_keys(void)                     /* the breathing keys, bit k = key k (the real positions) */
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++) {
        uint8_t q = led_pos[14u + k];
        m |= (uint32_t)(q != 0xFF && ((fm1_led_breath[q >> 3] >> (q & 7u)) & 1u)) << k;
    }
    return m;
}
static uint32_t lit_keys(const uint8_t *l)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++) {
        uint8_t q = led_pos[14u + k];
        m |= (uint32_t)(q != 0xFF && ((l[q >> 3] >> (q & 7u)) & 1u)) << k;
    }
    return m;
}
static int led_of(const uint8_t *l, uint32_t id)
{
    uint8_t q = led_pos[id];
    return q != 0xFF && ((l[q >> 3] >> (q & 7u)) & 1u);
}
static int test_breath(void)
{
    static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
    static const uint8_t MODES[4] = {LEDS_OFF, LEDS_DIM_LO, LEDS_DIM, LEDS_INV};
    int bad = 0, ok = 1, oko = 1;
    uint32_t i, m, t;
    ui_power_on();
    for (i = 0; i < 4u; i++) {
        ui_power_on();
        go_title("ENV"); frame();
        btn_down(LB[i]); frames(500);
        ok &= !ui.layer && !ui.lock;                   /* hold peek removed */
        btn_up(LB[i]); frame();
        press(LB[i]); frames(64); press(LB[i]); frames(100);
        ok &= !ui.lock || (LB[i] == B_FX && cur_page()->sec == FX_SEC_PUNCH);
    }
    bad += check("#119 hold/double-tap layer maps removed (no breath map without sticky section)", ok);
    ui_leds();
    for (i = 0; i < 41u; i++)
        led_pos[i] = (uint8_t)((i / 4u) << 3 | (1u + i % 4u));
    for (m = 0; m < 4u; m++) {
        ui_power_on();
        settings_leds = MODES[m];
        go_title("ENV"); frame();
        ui.confirm = CF_CLEAR_SEQ;
        for (t = 0; t < 4u; t++) {
            fm1_ms += 250u;
            ui_leds();
            oko &= led_of(fm1_led_breath, panel.btn[B_OCTUP]) && !led_of(fm1_led, panel.btn[B_OCTUP]);
        }
        ui.confirm = 0;
    }
    settings_leds = LEDS_DIM;
    led_pos_init();
    bad += check("#119 a dialog: OCT+ breathes (not lit), every LEDS mode", oko);
    ui_power_on();
    return bad;
}

static int test_idle_glow(void)
{
    int bad = 0, ok;
    ui_power_on();
    ui_leds();
    memset(led_pos, 0xFF, sizeof led_pos);
    led_pos[panel.btn[B_HOME]] = 0u << 3 | 1u;
    led_pos[panel.btn[B_ENV]] = 0u << 3 | 2u;
    led_pos[panel.btn[B_PLAY]] = 0u << 3 | 3u;
    led_pos[14u + 0u] = 1u << 3 | 1u;              /* keys 1 and 2 */
    led_pos[14u + 1u] = 1u << 3 | 2u;
    go_home();
    ui_leds();
    bad += check("HOME: its button lit, ENV and PLAY (stopped) glow, the keys glow, none lit",
                 fm1_led[0] == 2u && fm1_led_dim[0] == 14u && fm1_led[1] == 0u && fm1_led_dim[1] == 6u);
    fm1_in.notes = 1u;
    song.playing = 1;
    ui_leds();
    ok = fm1_led[0] == 2u && (fm1_led_dim[0] & 8u) == 0u && fm1_led[8] == 2u && fm1_led[1] == 2u && fm1_led_dim[1] == 6u;
    fm1_in.notes = 0;
    song.playing = 0;
    bad += check("a key held lit over the glow; PLAY running: its green, its own LED dark", ok);
    ui.layer = 1;
    ui_leds();
    ok = fm1_led_dim[1] == 0u && fm1_led_dim[0] == 14u;
    ui.layer = 0;
    ui_leds();
    ok &= fm1_led_dim[1] == 6u;
    bad += check("a layer's map: the keys lit or dark (no glow), the buttons still glow", ok);
    /* MENU > LEDS INV: the active ones dark, the idle ones lit (stock), no glow; a breath as in DIM (#119); a map
     * of the keys' own as in DIM */
    settings_leds = LEDS_INV;
    ui_leds();
    ok = fm1_led[0] == 12u && fm1_led_dim[0] == 0u && fm1_led[1] == 6u && fm1_led_dim[1] == 0u;   /* HOME dark */
    fm1_in.notes = 1u;
    song.playing = 1;
    ui_leds();
    ok &= fm1_led[0] == 4u && fm1_led_dim[0] == 0u && fm1_led[1] == 4u && fm1_led_dim[1] == 0u && fm1_led[8] == 2u;   /* PLAY, key 1 dark; green */
    fm1_in.notes = 0;
    song.playing = 0;
    bad += check("LEDS INV: the page's button, PLAY running and a key held go dark, the rest lit, no glow", ok);
    ui.layer = 1;
    led_pos[panel.btn[layer_btn()]] = 0u << 3 | 4u;   /* the layer's button */
    {
        uint32_t seen = 0, t;
        for (t = 0; t < 4u; t++) {                  /* the layer's button breathes: never lit (inverted), never the glow */
            fm1_ms = t * 250u;
            ui_leds();
            seen |= ((fm1_led[0] >> 4) & 1u ? 1u : 2u) | ((fm1_led_dim[0] >> 4) & 1u ? 4u : 0u) |
                    ((fm1_led_breath[0] >> 4) & 1u ? 8u : 0u);
        }
        ok = seen == 10u && fm1_led_dim[1] == 0u && fm1_led[1] == (uint8_t)((layer_leds(0) & 3u) << 1);
    }
    ui.layer = 0;
    fm1_ms = 0;
    bad += check("LEDS INV: the layer's button breathes (not inverted); a layer's map stays lit / dark as in DIM", ok);
    settings_leds = LEDS_DIM;
    led_pos_init();
    return bad;
}

/* "On the step sequencer all the LEDs go out": SEQ > STEP, the piano roll and the DRUM grid, stopped and playing,
 * in every LEDS mode, with the LEDs where the hardware has them (hal/fm1_input.h FM1_KEYMAP). The buttons keep
 * their glow and their active states; on the piano roll the keys glow under the held and playing ones; on the
 * grid its map is lit and its keys that do something glow (DIM LO / HI). Before: keys_own() took the grid's keys
 * out of the glow, so in DIM 26 of 27 keys of an empty 16-step grid went fully dark */
/* the white keys of the grid's steps (bit p = step p of the page shown), bit k = key k */
static uint32_t step_keys(uint32_t steps)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++)
        if (!key_black(k) && key_place(k) < 16u && ((steps >> key_place(k)) & 1u))
            m |= 1u << k;
    return m;
}
static int test_step_leds(void)
{
    static const int8_t MAP[6][FM1_NCOL] = {     /* (hal/fm1_input.h FM1_KEYMAP; the host stub is zero) */
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
        {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
        {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
        { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    };
    static const uint8_t MODES[LEDS_COUNT] = {LEDS_OFF, LEDS_DIM_LO, LEDS_DIM, LEDS_INV};
    int bad = 0;
    uint32_t id, p, r, mi, drum, run, k;
    ui_power_on();
    ui_leds();
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (MAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)(p << 3 | r);
    }
    for (drum = 0; drum < 2u; drum++)
        for (run = 0; run < 2u; run++)
            for (mi = 0; mi < LEDS_COUNT; mi++) {
                uint32_t m = MODES[mi], glow = m == LEDS_DIM || m == LEDS_DIM_LO, keys_lit = 0, keys_dim = 0, btn_bad = 0;
                uint32_t want_dim = 0, kc, keys;
                char what[128];
                ui_power_on();
                song.sel = 0;
                if (drum) {
                    set_engine_of(TSEL, ENGI_DRUM);
                    TSEL->engine = TSEL->eng_req;
                    TSEL->p[P_SLEN] = 16;
                    TSEL->step[2] = (step_t){.hit = 1u << DV_KICK, .time = ST_NOTE};   /* one hit, on step 3 */
                } else {
                    TSEL->step[0] = (step_t){.nb = {60}, .n = 1, .time = ST_NOTE};   /* C4 on step 1 */
                }
                settings_leds = (uint8_t)m;
                go_seq_step();
                frame();
                if (run) {
                    seq_start();
                    events_block(CTL);
                }
                ui_leds();
                for (k = 0; k < NB; k++) {                  /* a button: lit if active, else glow / dark / lit */
                    uint8_t q = led_pos[panel.btn[k]];
                    uint32_t lit = (fm1_led[q >> 3] >> (q & 7u)) & 1u, dim = (fm1_led_dim[q >> 3] >> (q & 7u)) & 1u;
                    int active = k == B_SEQ || (k == B_PLAY && run);
                    btn_bad += m == LEDS_INV ? lit == (uint32_t)active || dim :
                               active ? (k == B_PLAY ? lit || dim : !lit) : lit || dim != glow;
                }
                keys = key_leds(0);
                for (k = 0; k < 27u; k++) {
                    uint8_t q = led_pos[14u + k];
                    keys_lit += (fm1_led[q >> 3] >> (q & 7u)) & 1u;
                    keys_dim += ((fm1_led_dim[q >> 3] & ~fm1_led[q >> 3]) >> (q & 7u)) & 1u;
                }
                kc = (uint32_t)__builtin_popcount(keys);
                if (glow)
                    want_dim = (uint32_t)__builtin_popcount((drum ? grid_glow() : 0x7FFFFFFu) & ~keys);
                else if (drum)                              /* (OFF, INV: the grid's beats alone glow) */
                    want_dim = (uint32_t)__builtin_popcount(grid_beats() & ~keys);
                snprintf(what, sizeof what, "STEP %s, %s, LEDS %s: buttons and keys as the mode says (%u lit, %u glow)",
                         drum ? "DRUM grid" : "piano roll", run ? "playing" : "stopped", LEDS_NAME[m],
                         (unsigned)keys_lit, (unsigned)keys_dim);
                bad += check(what, !btn_bad && host_dim_lo == (m == LEDS_DIM_LO) &&
                                       grid_on() == (int)drum && (!run || kc) &&
                                       keys_dim == want_dim && keys_lit == (m == LEDS_INV && !drum ? 27u - kc : kc) &&
                                       (!glow || keys_lit + keys_dim >= 25u) &&
                                       ((fm1_led[LED_PLAY_GREEN >> 3] >> (LED_PLAY_GREEN & 7u)) & 1u) == run);
                bad += check("  the mid level: the grid's beats in DIM HI / LO only, never a lit key",
                             lit_keys(fm1_led_mid) == (glow && drum ? grid_beats() & ~keys : 0u) &&
                             !(lit_keys(fm1_led_mid) & lit_keys(fm1_led)));
                if (run) {
                    transport_req = 2;
                    events_block(CTL);
                }
            }
    {   /* the DRUM grid's beats (ui_input.c grid_beats): "5111 5111 5111 5111", the first step of each four brighter
         * where no hit shows, the hits lit and the playhead as before; LEN cuts them, a page shows its own; groups of
         * four whatever LEN / DIV; DIM HI / LO: fm1_led_mid over the glow, OFF / INV: the glow alone */
        uint32_t want, beats, keys, kick = 1u << DV_KICK, ok = 1, mi;
        for (mi = 0; mi < LEDS_COUNT; mi++) {
            uint32_t m = MODES[mi], glow = m == LEDS_DIM || m == LEDS_DIM_LO;
            char what[128];
            ui_power_on();
            song.sel = 0;
            set_engine_of(TSEL, ENGI_DRUM);
            TSEL->engine = TSEL->eng_req;
            TSEL->p[P_SLEN] = 16;
            TSEL->step[4] = (step_t){.hit = kick, .time = ST_NOTE};   /* a kick on step 5 (a beat) and on step 3 */
            TSEL->step[2] = (step_t){.hit = kick, .time = ST_NOTE};
            settings_leds = (uint8_t)m;
            go_seq_step();
                frame();
            ui.lane = 0;
            ui_leds();
            keys = key_leds(0);
            beats = grid_beats();
            want = step_keys(1u << 0 | 1u << 8 | 1u << 12);   /* steps 1, 9, 13 (5 holds a hit: lit) */
            ok = beats == want && (keys & step_keys(1u << 4 | 1u << 2)) == step_keys(1u << 4 | 1u << 2) &&
                 lit_keys(fm1_led_mid) == (glow ? want : 0u) && (lit_keys(fm1_led_dim) & want) == want &&
                 (glow || !(lit_keys(fm1_led_dim) & ~want)) && host_dim_lo == (m == LEDS_DIM_LO);
            snprintf(what, sizeof what, "DRUM grid beats, LEDS %s: steps 1 9 13 %s, the hits 3 5 lit", LEDS_NAME[m],
                     glow ? "at the mid level over the glow" : "glow (nothing else does)");
            bad += check(what, ok);
            if (m != LEDS_DIM)
                continue;
            ui.lane = 1;                                /* another lane: none of its hits there, 1 5 9 13 */
            ok = grid_beats() == step_keys(0x1111u);
            ui.lane = 0;
            TSEL->p[P_SLEN] = 12;                       /* LEN 12 (3/4): 1 5 9; 13 is past it */
            ok &= grid_beats() == step_keys(1u << 0 | 1u << 8);
            TSEL->p[P_SLEN] = 16; TSEL->p[P_SDIV] = 4;  /* (DIV changes nothing: steps are counted) */
            ok &= grid_beats() == want;
            song.playing = 1; TSEL->seq_idx = 8;        /* the playhead on step 9: it lights it, no mid there */
            ui_leds();
            ok &= grid_beats() == step_keys(1u << 0 | 1u << 12) && (lit_keys(fm1_led) & step_keys(1u << 8)) &&
                  !(lit_keys(fm1_led_mid) & step_keys(1u << 8));
            song.playing = 0;
            TSEL->p[P_SLEN] = 32;                       /* page 2: its own steps 17 21 25 29 */
            TSEL->step[20] = (step_t){.hit = kick, .time = ST_NOTE};
            ui.bank = 1; ui.cursor = 16;
            ok &= grid_beats() == step_keys(1u << 0 | 1u << 8 | 1u << 12);
            bad += check("  another lane, LEN 12, DIV, the playhead, page 2: the beats as the steps show", ok);
        }
    }
    settings_leds = LEDS_DIM;
    led_pos_init();
    ui_power_on();
    return bad;
}

/* Discussion #127 (1.2): MENU > SCALE LEDS ON, outside the keys' own maps the keys of the selected track's scale glow
 * (the others dark), the root's at the mid level; the keys held / playing lit over it; LEDS OFF / DIM LO / DIM HI
 * alike, INV turned round (root lit, scale mid, the others glowing, the active dark); TRN; MAP KBD: the white
 * keys; a DRUM track, a layer, the default OFF: as before */
/* MENU > DISPLAY > SCREEN OFF (1.1.5, ui.c scr_*): the screen and its backlight off after NEVER / 5 / 15 / 30 (default)
 * / 60 minutes without panel input; the press, key or turn that wakes it does nothing else; the dialogs, the count-in
 * and the UPDATE MODE countdown keep it on and wake it; the sound goes on as if it were on (forked sessions, bit for
 * bit); the setting saved (a spare favorites byte, 0 = NEVER since 1.1.5.1) and offered in the MENU */
static void scr_idle(uint32_t ms)                  /* nobody touches it for ms (a frame a minute, then frames) */
{
    while (ms > 60000u) { fm1_ms += 60000u - 16u; host_ticks += 60000u * 1000u; frame(); ms -= 60000u; }
    frames(ms);
}
static int scr_woke(uint32_t from)                 /* since host_pw_n was `from`: SLPOUT >= 120 ms after the SLPIN,
                                                    * DISPON + backlight >= 120 ms after the SLPOUT, nothing else */
{
    return host_pw_n == from + 2u && host_pw[from] == 1u && host_pw[from + 1u] == 2u && host_bl &&
           host_pw_ms[from] - host_pw_ms[from - 1u] >= 120u && host_pw_ms[from + 1u] - host_pw_ms[from] >= 120u &&
           scrn.st == SCR_ON;
}
static uint64_t scr_session(uint32_t setting, uint32_t *flags)
{
    int32_t o[2u * CTL];
    uint64_t h = 1469598103934665603ull, e = 0;
    uint32_t f, b, i, off = 0;
    ui_power_on();
    scr_put(setting);
    song.master_q12 = 4096;
    my_steps(&trk[0]);
    transport_req = 1;
    for (f = 0; f < 400u; f++) {
        if (f == 50u)
            fm1_ms += 6u * 60000u;                 /* six minutes untouched: 5 MIN goes dark */
        frame();
        off |= !host_bl;
        for (b = 0; b < 4u; b++) {
            mix_block(o, CTL);
            for (i = 0; i < 2u * CTL; i++) {
                h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
                e += (uint64_t)(o[i] < 0 ? -o[i] : o[i]);
            }
        }
    }
    *flags = off | (song.playing ? 2u : 0u) | (e ? 4u : 0u);
    stop_transport();
    return h;
}
static uint64_t scr_session_fork(uint32_t setting, uint32_t *flags)   /* from the same state each time */
{
    int fd[2];
    uint64_t r[2] = {0, 0};
    pid_t p;
    if (pipe(fd))
        return 0;
    p = fork();
    if (!p) {
        uint32_t fl = 0;
        r[0] = scr_session(setting, &fl);
        r[1] = fl;
        if (write(fd[1], r, sizeof r) != sizeof r) _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], r, sizeof r) != sizeof r) r[0] = 0;
    close(fd[0]);
    waitpid(p, 0, 0);
    *flags = (uint32_t)r[1];
    return r[0];
}
/* 1.2 (Discussions #72 / #73): MOMENTARY, LFO held. KNOB 1..4 on HOME and the sound / FX pages change their values as
 * ever and LFO let go puts each back (no LFO page then; LFO alone still opens it); OCT+ with LFO held keeps them (no
 * octave); not recorded as automation; a sound load meanwhile puts the values back first (the undo copy is the sound as
 * it was); the knobs on STEP / action pages / lists act as ever; a layer's knobs are the layer's; up to 8 values */
static int test_momentary(void)
{
    int bad = 0, ok;
    uint32_t id0, id1, k;
    int16_t v0, v1, g0;
    ui_power_on();
    ui.home = 1;
    id0 = ENGINES[TSEL->eng_req]->knob[0];
    id1 = ENGINES[TSEL->eng_req]->knob[1];
    v0 = TSEL->p[id0];
    v1 = TSEL->p[id1];
    btn_down(B_LFO); frame();
    turn(EN_K1, 6);
    turn(EN_K2, -4);
    ok = TSEL->p[id0] != v0 && TSEL->p[id1] != v1 && mom.n == 2u && msg_is("MOMENTARY  [OCT+] KEEP");
 ui_leds();
    btn_up(B_LFO); frame();
    bad += check("MOMENTARY: LFO held, KNOB 1 / 2 on HOME change their values; LFO let go puts them back (no LFO page)",
                 ok && TSEL->p[id0] == v0 && TSEL->p[id1] == v1 && !mom.n && ui.home && msg_is("BACK"));
    press(B_LFO);
    bad += check("  LFO alone still opens its page", !ui.home && cur_page()->fam == FAM_LFO);
    go_home();
    turn(EN_K1, 2);
    bad += check("  without LFO a turn stays", TSEL->p[id0] != v0 && !mom.n);
    v0 = TSEL->p[id0];
    btn_down(B_LFO); frame();
    turn(EN_K1, 5);
    k = (uint32_t)song.octave;
    press(B_OCTUP);
    ok = !mom.n && msg_is("KEPT") && song.octave == (int8_t)k;
    btn_up(B_LFO); frame();
    bad += check("  OCT+ with LFO held keeps them (no octave shift), LFO let go then changes nothing",
                 ok && TSEL->p[id0] != v0 && ui.home);
    /* an FX Sends page (global): SELECT walks pages; button only cycles sections */
    go_title("REVERB"); frame();
    g0 = song.g[G_RSIZE];
    btn_down(B_LFO); frame();
    turn(EN_K2, 9);                                    /* SIZE */
    ok = song.g[G_RSIZE] != g0;
    btn_up(B_LFO); frame();
    bad += check("  a global FX value (REVERB SIZE) too: back on release", ok && song.g[G_RSIZE] == g0 && cur_page()->scope == SC_GLOBAL);
    /* not recorded as automation; recording a plain turn still works */
    go_home();
    song.playing = 1; song.rec = 1u << song.sel;
    k = motion.count;
    btn_down(B_LFO); frame();
    turn(EN_K1, 7);
    ok = motion.count == k;
    btn_up(B_LFO); frame();
    song.playing = 0; song.rec = 0;
    bad += check("  a momentary turn is not recorded as automation (armed and playing)", ok);
    /* a sound load meanwhile: the values back first, then the load (its sound stays) */
    v0 = TSEL->p[id0];
    btn_down(B_LFO); frame();
    turn(EN_K1, 8);
    turn(EN_PRESET, 1);
    ok = !mom.n;
    btn_up(B_LFO); frame();
    {
        int16_t after = TSEL->p[id0];
        undo_swap();
        ok &= TSEL->p[id0] == v0;
        undo_swap();
        ok &= TSEL->p[id0] == after;
    }
    bad += check("  a sound load with LFO held: the values back first (the undo copy is the sound as it was), the load stays", ok);
    /* STEP: the knobs edit the step as ever (no momentary) */
    go_seq_step(); frame();
    btn_down(B_LFO); frame();
    turn(EN_K2, 1);
    ok = !mom.n;
    btn_up(B_LFO); frame();
    bad += check("  on STEP (and the action pages, the lists) the knobs act as ever", ok && cur_page()->graph == GR_ROLL);
    /* #195: MIXER's LEVEL PAN REV MUTE (the selected track's) are momentary too, as on HOME LEVELS and the FX page */
    go_page(GR_TRK); frame();
    {
        int16_t lv = TSEL->p[P_LEVEL], pn = TSEL->p[P_PAN], rv = TSEL->p[P_REV], mu = TSEL->p[P_MUTE];
        song.playing = 1; song.rec = 1u << song.sel;
        k = motion.count;
        btn_down(B_LFO); frame();
        turn(EN_K1, -5);
        turn(EN_K2, 7);
        turn(EN_K3, 9);
        turn(EN_K4, 1);
        ok = TSEL->p[P_LEVEL] != lv && TSEL->p[P_PAN] != pn && TSEL->p[P_REV] != rv && TSEL->p[P_MUTE] != mu &&
             mom.n == 4u && motion.count == k;
        btn_up(B_LFO); frame();
        song.playing = 0; song.rec = 0;
        ok &= TSEL->p[P_LEVEL] == lv && TSEL->p[P_PAN] == pn && TSEL->p[P_REV] == rv && TSEL->p[P_MUTE] == mu &&
              !mom.n && cur_page()->graph == GR_TRK && msg_is("BACK");
        turn(EN_K3, 4);                            /* (without LFO: a turn stays) */
        ok &= TSEL->p[P_REV] != rv && !mom.n;
        TSEL->p[P_REV] = rv;
        bad += check("  #195 MIXER: LEVEL PAN REV PLAY momentary (not recorded), back on release; without LFO they stay", ok);
    }
    /* a layer: its knobs */
    go_home();
    v0 = TSEL->p[id0];
    btn_down(B_LFO); frame();
    btn_down(B_GLO); frame();
    turn(EN_K1, 3);
    ok = !mom.n && TSEL->p[id0] == v0;
    btn_up(B_GLO); frame();
    btn_up(B_LFO); frame();
    bad += check("  with a layer's button held the knobs are the layer's", ok);
    /* at most MOM_N values */
    go_home(); frames(400);                        /* (#39: the layer let go, the knobs quiet a moment) */
    btn_down(B_LFO); frame();
    for (k = 0; k < MOM_N; k++) {
        mom.vp[k] = &trk[k % NTRK].p[P_LEVEL + k / NTRK]; mom.v[k] = *mom.vp[k];
    }
    mom.n = MOM_N;
    v0 = TSEL->p[id0];
    ui.msg_t = 0;
    turn(EN_K1, -3);
    ok = TSEL->p[id0] == v0 && msg_is("MOMENTARY FULL");
    btn_up(B_LFO); frame();
    bad += check("  at most 8 values: MOMENTARY FULL, the turn does nothing", ok && !mom.n);
    return bad;
}
/* ANIM IDLE retired: idle_leds never runs; menu_put(MI_ANIM) is a no-op; ui_idle bit 0 stays clear; PREF_ANIM_OFF always */
static int test_idle_leds(void)
{
    int bad = 0, ok;
    persist_t p;
    ui_power_on();
    host_idle = host_anim = 0;
    frame();
    ok = (ui_prefs & PREF_ANIM_OFF) && menu_get(MI_ANIM) == 1u && !(ui_idle & 1u) &&
         MI_TAB[MI_ANIM] == MTAB_COUNT && str_eq(MI_NAME[MI_ANIM], "ANIM");
    menu_put(MI_ANIM, 2);
    ok &= menu_get(MI_ANIM) == 1u && !(ui_idle & 1u) && (ui_prefs & PREF_ANIM_OFF);
    menu_put(MI_ANIM, 0);
    ok &= menu_get(MI_ANIM) == 1u && (ui_prefs & PREF_ANIM_OFF);
    settings_leds = LEDS_DIM;
    frames(160);
    fm1_ms += IDLE_LEDS_MS + 1000u; frame(); ui_leds();
    bad += check("ANIM IDLE retired: always OFF, not in the menu, idle LEDs never start (a minute without input)",
                 ok && !host_idle && !host_idle_glow);
    settings_export(&p);
    ok = !(p.favorites.factory[15][10] & 1u) && (p.favorites.factory[15][30] & PREF_ANIM_OFF);
    p.favorites.factory[15][10] = 1u;                   /* older IDLE bit: cleared on import */
    p.favorites.factory[15][30] &= (uint8_t)~PREF_ANIM_OFF;
    ok &= settings_import(&p, sizeof p) == 1 && menu_get(MI_ANIM) == 1u && !(ui_idle & 1u) &&
          (ui_prefs & PREF_ANIM_OFF);
    bad += check("  settings import forces ANIM OFF and clears IDLE ([15][10] bit 0)", ok);
    host_idle = 0;
    ui_idle = 0;
    settings_leds = LEDS_DIM;
    return bad;
}
static int test_screen_off(void)
{
    int bad = 0, ok;
    uint32_t n, rows, i;
    int16_t *vp, v0;

    ui_power_on();
    ok = ui_scr == 0u && scr_get() == 0u && SCR_MIN[scr_get()] == 0u && str_eq(MI_NAME[MI_SCROFF], "SCREEN OFF") &&
         MI_TAB[MI_SCROFF] == MTAB_DISPLAY && menu_n(MI_SCROFF) == 5u && str_eq(menu_vname(MI_SCROFF, 0), "NEVER") &&
         str_eq(menu_vname(MI_SCROFF, 1), "5 MIN") && str_eq(menu_vname(MI_SCROFF, 2), "15 MIN") &&
         str_eq(menu_vname(MI_SCROFF, 3), "30 MIN") && str_eq(menu_vname(MI_SCROFF, 4), "60 MIN") &&
         menu_get(MI_SCROFF) == 0u;
    bad += check("SCREEN OFF: MENU > DISPLAY row, NEVER 5 15 30 60 MIN, NEVER by default (stored 0, 1.1.5.1)", ok);
    menu_put(MI_SCROFF, 3u);                         /* 30 MIN for what follows */

    host_pw_n = 0;
    scr_idle(30u * 60000u - 2000u);
    ok = host_pw_n == 0u && host_bl && scrn.st == SCR_ON;
    scr_idle(2000u);
    ok &= host_pw_n == 1u && host_pw[0] == 0u && !host_bl && scrn.st == SCR_OFF && kb_asleep;
    rows = host_blit_rows;
    ui_message("SAVED");
    scr_idle(5000u);
    ok &= host_blit_rows == rows && host_pw_n == 1u && !ui.msg_t;
    bad += check("SCREEN OFF: 30 MIN untouched -> DISPOFF + SLPIN once; dark: nothing drawn", ok);

    n = host_pw_n;
    ok = ui.home && !ui.menu;
    btn_down(B_ENV); frame();                         /* a page button wakes it, does not open its page */
    ok &= scrn.st == SCR_SLPOUT && host_pw_n == n + 1u && host_pw[n] == 1u;   /* (SLPIN long ago: SLPOUT at once) */
    frames(100u);
    ok &= host_pw_n == n + 1u && host_blit_rows == rows;   /* (dark until 120 ms after it) */
    rows = host_blit_rows;
    frames(200u);
    ok &= scr_woke(n) && host_blit_rows - rows >= 200u;   /* (the frame before DISPON redrew it all) */
    btn_up(B_ENV); frame(); frames(100u);
    ok &= ui.home && !ui.menu && !ui.layer && !scrn.eat && !kb_asleep;
    press(B_ENV);                                     /* awake: the button works again */
    ok &= !ui.home && cur_page()->fam == FAM_ENV;
    bad += check("SCREEN OFF: a button wakes it (SLPOUT, 120 ms, the page redrawn, DISPON), swallowed", ok);

    go_home();
    for (i = 0; i < 3u; i++) {                        /* HOME, SAVE, REC held across the wake: no menu, undo, clear */
        static const uint32_t B[3] = {B_HOME, B_SAVE, B_REC};
        scr_idle(31u * 60000u);
        n = host_pw_n;
        ok = !host_bl;
        hold(B[i]); frames(300u);
        ok &= scr_woke(n) && ui.home && !ui.menu && !ui.confirm && !song.rec && !song.playing && !transport_req;
        bad += check(i == 0 ? "SCREEN OFF: HOME held to wake: no menu, no HOME tap after" :
                     i == 1 ? "SCREEN OFF: SAVE held to wake: no undo, no SAVE page" :
                              "SCREEN OFF: REC held to wake: nothing armed, no clear, no transport", ok);
    }

    scr_idle(31u * 60000u);
    n = host_pw_n;
    ok = !host_bl && !gates();
    key_down(white(0)); frame();                      /* a key: wakes it, no note (seq.c kb_asleep) */
    ok &= !gates();
    frames(300u);
    ok &= scr_woke(n) && !gates();
    key_up(white(0)); frame(); frames(50u);
    ok &= !gates() && !kb_asleep;
    key_down(white(0)); frame();
    ok &= gates() == 1u;                              /* awake: the key plays */
    key_up(white(0)); frame();
    bad += check("SCREEN OFF: a key wakes it silently (no note on, none off), the next one plays", ok);

    scr_idle(31u * 60000u);
    n = host_pw_n;
    home_param(0, &vp);
    v0 = *vp;
    for (i = 0; i < 20u; i++) turn(EN_K1, 1);        /* a knob turned to wake it, still turning (on past the wake) */
    ok = scr_woke(n) && *vp == v0;
    turn(EN_PRESET, 1); turn(EN_ALGO, 1); turn(EN_SELECT, 1); turn(EN_K4, -1);
    ok &= *vp == v0 && song.sel == 0u && song.g[G_BPM] == GP[G_BPM].def && scrn.eat;
    frames(400u);                                     /* at rest SCR_EAT_MS: the knobs are the page's again */
    turn(EN_K1, 1);
    ok &= *vp != v0;
    bad += check("SCREEN OFF: a knob wakes it; turns until it rests 0.3 s change nothing (K1..4, PRESETS, ALGO, SELECT)", ok);

    menu_put(MI_SCROFF, 0u);                         /* NEVER */
    n = host_pw_n;
    scr_idle(3u * 3600000u);
    ok = host_pw_n == n && host_bl && ui_scr == 0u;
    menu_put(MI_SCROFF, 1u);                         /* 5 MIN */
    scr_idle(4u * 60000u + 50000u);
    ok &= host_pw_n == n && host_bl;
    scr_idle(15000u);
    ok &= host_pw_n == n + 1u && !host_bl;
    press(B_PLAY); frames(300u);                      /* PLAY wakes it, does not start the transport */
    ok &= scr_woke(n + 1u) && !song.playing && !transport_req;
    bad += check("SCREEN OFF: NEVER stays on for hours; 5 MIN goes dark after 5; PLAY to wake starts nothing", ok);

    n = host_pw_n;
    ui.confirm = CF_CLEAR_SEQ; ui.force = 1;          /* a dialog: never dark */
    scr_idle(20u * 60000u);
    ok = host_pw_n == n && host_bl;
    ui.confirm = 0; ui.force = 1;
    cin_left = 1000u;                                 /* the count-in */
    scr_idle(20u * 60000u);
    ok &= host_pw_n == n && host_bl;
    cin_left = 0;
    scr_idle(6u * 60000u);
    ok &= host_pw_n == n + 1u && !host_bl;
    ui.confirm = CF_CLEAR_SEQ; ui.force = 1;          /* one opening while dark (the editor, MIDI): it wakes */
    frames(300u);
    ok &= scr_woke(n + 1u) && ui.confirm == CF_CLEAR_SEQ;
    ui.confirm = 0; ui.force = 1;
    scr_idle(6u * 60000u);
    n = host_pw_n;
    ui.uboot = 3; ui.force = 1;                       /* the UPDATE MODE countdown */
    frames(300u);
    ok &= scr_woke(n);
    ui.uboot = 0; ui.force = 1;
    scr_idle(6u * 60000u);
    n = host_pw_n;
    scr_wake_now();                                   /* main.c: the crash screen, UPDATE, UBOOT */
    ok &= host_pw_n == n + 1u && host_pw[n] == 3u && host_bl && scrn.st == SCR_ON;
    bad += check("SCREEN OFF: a dialog, the count-in, UPDATE MODE keep it on and wake it; the crash screen at once", ok);

    {   /* the setting with the settings: SCR_CODE in favorites.factory[15][27]; 1.1.5's codes: 0 (its 30 MIN, its
         * default) now NEVER, 3 (its NEVER) NEVER, 2 1 7 as they were; 30 MIN is 4 now; unknown NEVER */
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        menu_put(MI_SCROFF, 4u);
        settings_export(&p); ui_scr = 0;
        ok = p.favorites.factory[15][27] == 7u && settings_import(&p, sizeof p) && scr_get() == 4u;
        p.favorites.factory[15][27] = 0;
        ok &= settings_import(&p, sizeof p) && scr_get() == 0u;
        p.favorites.factory[15][27] = 3;              /* 1.1.5's NEVER */
        ok &= settings_import(&p, sizeof p) && scr_get() == 0u;
        p.favorites.factory[15][27] = 2;              /* 5 MIN, as on 1.1.5 */
        ok &= settings_import(&p, sizeof p) && scr_get() == 1u;
        menu_put(MI_SCROFF, 3u); ok &= ui_scr == 4u && scr_get() == 3u;
        p.favorites.factory[15][27] = 6;              /* unknown: NEVER */
        ok &= settings_import(&p, sizeof p) && scr_get() == 0u;
        ui_scr = 0x55; ok &= scr_get() == 0u;
        bad += check("SCREEN OFF: saved with the settings (0, 1.1.5's 30 MIN, and unknown = NEVER)", ok);
    }

    ui_power_on();                                    /* in the MENU: OCT+ / a knob step it */
    menu_put(MI_SCROFF, 3u);
    hold(B_HOME);
    for (i = 0; i < 3u; i++) turn(EN_ALGO, 1);       /* COLOR LARGE LEDS -> SCREEN OFF (STYLE/ANIM retired) */
    ok = ui.menu == 1 && ui.menu_sel == MI_SCROFF;
    press(B_OCTUP); ok &= scr_get() == 4u;
    press(B_OCTUP); ok &= scr_get() == 4u;           /* (stops at the end) */
    for (i = 0; i < 4u; i++) turn(EN_K2, -1);
    ok &= scr_get() == 0u;
    press(B_OCTDN); ok &= scr_get() == 0u && ui.menu == 1;
    press(B_OCTUP); press(B_OCTUP); press(B_OCTUP); ok &= scr_get() == 3u;
    press(B_HOME); ok &= ui.menu == 0;
    bad += check("SCREEN OFF: the MENU row steps NEVER .. 60 MIN (OCT-/+, any knob), stops at the ends", ok);

    {
        uint32_t f0 = 0, f1 = 0;
        uint64_t a = scr_session_fork(0u, &f0), b = scr_session_fork(1u, &f1);
        bad += check("SCREEN OFF: the sound bit for bit as with NEVER while dark (sequencer playing)",
                     a && a == b && (f0 & 6u) == 6u && (f1 & 6u) == 6u && !(f0 & 1u) && (f1 & 1u));
    }
    ui_power_on();
    return bad;
}

static int test_scale_leds(void)
{
    static const int8_t MAP[6][FM1_NCOL] = {     /* (hal/fm1_input.h FM1_KEYMAP; the host stub is zero) */
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
        {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
        {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
        { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    };
    static const uint8_t MODES[LEDS_COUNT] = {LEDS_OFF, LEDS_DIM_LO, LEDS_DIM, LEDS_INV};
    const uint32_t ALL = 0x7FFFFFFu, C_KEYS = 1u << 7 | 1u << 19;
    int bad = 0, ok;
    uint32_t id, p, r, k, mi, root = 0, sc, white = 0;
    track_t *t;
    ui_power_on();
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (MAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)(p << 3 | r);
    }
    for (k = 0; k < 27u; k++)
        white |= (uint32_t)!key_black(k) << k;
    song.sel = 0;
    t = TSEL;
    t->p[P_SCALE] = 0;                                  /* C major */
    t->p[P_ROOT] = 0;
    settings_leds = LEDS_DIM;
    ui_leds();
    ok = !menu_get(MI_SCLLED) && str_eq(MI_NAME[MI_SCLLED], "SCALE LEDS") && MI_TAB[MI_SCLLED] == MTAB_CONTROL &&
         !scale_leds(&root) && lit_keys(fm1_led_dim) == ALL && !lit_keys(fm1_led_mid);
    bad += check("#127 SCALE LEDS OFF (the default): every key glows as before", ok);
    menu_put(MI_SCLLED, menu_step(MI_SCLLED, 1));
    ok = menu_get(MI_SCLLED) == 1u && (ui_rec_prefs & 0x40u) && !ui_prefs && str_eq(menu_vname(MI_SCLLED, 1), "ON");
    sc = scale_leds(&root);
    bad += check("#127 MENU > CONTROL > SCALE LEDS ON (a bit of its own); C major: the white keys, the root C4 C5",
                 ok && sc == white && root == C_KEYS);
    for (mi = 0; mi < LEDS_COUNT; mi++) {
        uint32_t m = MODES[mi], held = 1u << 1 | 1u << 7;   /* F#3 (out of the scale) and C4 (the root) held */
        char what[128];
        settings_leds = (uint8_t)m;
        fm1_in.notes = 0;
        ui_leds();
        if (m == LEDS_INV)
            ok = lit_keys(fm1_led) == C_KEYS && lit_keys(fm1_led_mid) == (white & ~C_KEYS) &&
                 lit_keys(fm1_led_dim) == (ALL & ~white);
        else
            ok = !lit_keys(fm1_led) && lit_keys(fm1_led_dim) == white && lit_keys(fm1_led_mid) == C_KEYS;
        fm1_in.notes = held;
        ui_leds();
        if (m == LEDS_INV)
            ok &= lit_keys(fm1_led) == (C_KEYS & ~held) && !(lit_keys(fm1_led_mid) & held) &&
                  !(lit_keys(fm1_led_dim) & held) && lit_keys(fm1_led_mid) == (white & ~C_KEYS);
        else
            ok &= lit_keys(fm1_led) == held && lit_keys(fm1_led_dim) == white && lit_keys(fm1_led_mid) == C_KEYS;
        snprintf(what, sizeof what, "#127 LEDS %s: %s; the keys held %s", LEDS_NAME[m],
                 m == LEDS_INV ? "root lit, the scale mid, the others glow" : "the scale glows, the root brighter",
                 m == LEDS_INV ? "dark" : "lit over it");
        bad += check(what, ok);
    }
    fm1_in.notes = 0;
    settings_leds = LEDS_DIM;
    t->p[P_ROOT] = 2;                                   /* D major: F# and C# in, F and C out, D the root */
    sc = scale_leds(&root);
    ok = (sc >> 1 & 1u) && (sc >> 8 & 1u) && !(sc & 1u) && !(sc >> 7 & 1u) && root == (1u << 9 | 1u << 21) &&
         __builtin_popcount(sc) == 16;
    t->p[P_ROOT] = 0;
    t->p[P_TRANS] = 2;                                  /* C major, TRN +2: F3 plays G3 (in), A#3 plays C4 (the root) */
    sc = scale_leds(&root);
    ok &= (sc & 1u) && !(sc >> 1 & 1u) && root == (1u << 5 | 1u << 17);
    t->p[P_TRANS] = 0;
    bad += check("#127 the keys as they play: ROOT D, TRN +2", ok);
    t->p[P_QUANT] = QN_ALL;                             /* ALL: same white-key layout LEDs as KBD */
    sc = scale_leds(&root);
    ok = sc == white && root == C_KEYS;
    t->p[P_QUANT] = QN_KBD;                             /* KBD, PEN: every white key; a root every 5 white keys */
    t->p[P_SCALE] = 4;
    sc = scale_leds(&root);
    ok &= sc == white && root == (1u << 7 | 1u << 16 | 1u << 24);
    ui_leds();
    ok &= lit_keys(fm1_led_dim) == white && lit_keys(fm1_led_mid) == root;
    t->p[P_QUANT] = QN_SEQ;                             /* SEQ keys as OFF: glow by chromatic pitch in MAJ */
    t->p[P_SCALE] = 0;
    sc = scale_leds(&root);
    ok &= sc == white && root == C_KEYS;
    bad += check("#127 MAP ALL/KBD: white-key layout; SEQ as OFF chromatic scale glow", ok);
    t->p[P_SCALE] = 0;
    ui.layer = LAYER_SCL;                               /* a layer: its own map, no scale glow under it */
    ui_leds();
    ok = !lit_keys(fm1_led_mid) && !lit_keys(fm1_led_dim);
    ui.layer = 0;
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    sc = scale_leds(&root);
    ui_leds();
    ok &= !sc && !root && lit_keys(fm1_led_dim) == ALL && !lit_keys(fm1_led_mid);
    go_seq_step();
                frame();
    ui_leds();
    ok &= grid_on() && lit_keys(fm1_led_dim) == grid_glow() &&
          lit_keys(fm1_led_mid) == (grid_beats() & ~key_leds(0));
    bad += check("#127 a layer, a DRUM track (its keys as before) and the DRUM grid keep their own LEDs", ok);
    menu_put(MI_SCLLED, 0);
    ok = !(ui_rec_prefs & 0x40u) && !scale_leds(&root);
    bad += check("#127 SCALE LEDS OFF again: the bit clears", ok);
    ui_rec_prefs = 0;
    settings_leds = LEDS_DIM;
    led_pos_init();
    ui_power_on();
    return bad;
}

/* #37: MAP SEQ on the SCL page: keys chromatic; ALL is KBD keys + seq snap; SEQ saved with the project */
static int test_seq_quant(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    char val[16];
    const char *unit = 0;
    ui_power_on();
    song.sel = 0;
    open_family(FAM_SCL);
    frame();
    ok = cur_page()->id[2] == P_QUANT && t->p[P_QUANT] == 0;
    {   /* the knob's shown order (storage stays append-only: ALL is 4) */
        static const int16_t shown[5] = {QN_OFF, QN_CURR, QN_ALL, QN_KBD, QN_SEQ};
        uint32_t i;
        for (i = 0; i < 5u; i++) {
            ok &= t->p[P_QUANT] == shown[i];
            turn(EN_K3, 1);
        }
        ok &= t->p[P_QUANT] == QN_SEQ;                 /* clamped at the end */
        turn(EN_K3, 10);
        ok &= t->p[P_QUANT] == QN_SEQ;
    }
    param_format(&TP[P_QUANT], t->p[P_QUANT], val, &unit);
    bad += check("SCL KNOB 3 MAP: OFF ALL CURR KBD SEQ, clamps at both ends (default OFF)", ok &&
                 str_eq(val, "SEQ"));
    t->p[P_SCALE] = 0;
    t->p[P_ROOT] = 0;
    t->p[P_QUANT] = QN_CURR;                           /* ALL (stored QN_CURR) quantises the sequence like CURR */
    ok = kb_map(t, 8) == 60u && kb_map(t, 7) == 60u;   /* CURR = ALL: white walk, black -> nearest */
    t->p[P_QUANT] = QN_SEQ;
    ok &= kb_map(t, 8) == 61u && kb_map(t, 7) == 60u;  /* SEQ keys as OFF: C#4 stays C#4 */
    t->step[0] = (step_t){.nb = {61, 66}, .n = 2, .time = ST_NOTE};
    seq_start();
    events_block(CTL);
    ok &= t->seq_n == 2u && t->seq_notes[0] == 60u && t->seq_notes[1] == 65u && step_note(&t->step[0], 0) == 61u &&
          step_note(&t->step[0], 1) == 66u;
    transport_req = 2;
    events_block(CTL);
    bad += check("MAP SEQ: keys chromatic; C#4 F#4 of a step play C4 F4 in C major, the step keeps C#4 F#4", ok);
    project_save(1);
    t->p[P_QUANT] = 0;
    project_load(1);
    bad += check("MAP SEQ saved and loaded with the project", trk[0].p[P_QUANT] == 3);
    return bad;
}

/* #39: KNOB 1..4 with a layer's button held are the layer's only, also as it is let go: a knob turned in the frame
 * the button is let go, while its map still shows (an effect key held after FX), or just after: the page under the
 * layer (ARP: ARP MODE on) or HOME kept its values. GLO / SCL / EDIT change only what their own knobs edit */
static int16_t lk_p[NTRK][P_COUNT], lk_g[G_COUNT];
static void lk_snap(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        memcpy(lk_p[i], trk[i].p, sizeof lk_p[i]);
    memcpy(lk_g, song.g, sizeof lk_g);
}
static int lk_same(uint32_t l)                      /* nothing changed but the layer's own (l: the layer) */
{
    uint32_t i, j;
    for (i = 0; i < NTRK; i++)
        for (j = 0; j < P_COUNT; j++) {
            int own = (l == LAYER_GLO && j == P_LEVEL) ||
                      (l == LAYER_SCL && i == 0u && (j == P_ROOT || j == P_SCALE || j == P_CHRD || j == P_VOIC)) ||
                      (l == LAYER_EDIT && i == 0u && (j < P_AMODE || j > P_AORDER));   /* (a load keeps the ARP) */
            if (lk_p[i][j] != trk[i].p[j] && !own)
                return 0;
        }
    for (j = 0; j < G_COUNT; j++)
        if (lk_g[j] != song.g[j] && l != LAYER_EDIT)
            return 0;
    return 1;
}
static void lk_turn(int32_t s)                       /* KNOB 1..4 together, no frame */
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        host_enc[panel.enc[EN_K1 + k]] += s * panel.dir[EN_K1 + k];
}

static int test_layer_knob_leak(void)
{
    int bad = 0, ok;
    int16_t atk;
    /* Punch-in sticky knobs do not leak into the page under the map after leaving */
    ui_power_on();
    go_title("ENV"); frame();
    atk = TSEL->p[P_ATK];
    go_title("PUNCH-IN"); frame();
    turn(EN_K1, -20);
    ok = perf_k[0] == -20 && TSEL->p[P_ATK] == atk && ui.layer == LAYER_FX;
    go_home(); frame();
    frames(800);
    go_title("ENV"); frame();
    atk = TSEL->p[P_ATK];
    turn(EN_K1, 1);
    ok &= TSEL->p[P_ATK] == atk + 1;
    bad += check("#39 Punch-in macros do not change ENV; after leaving, KNOB 1 is the page's again", ok);
    /* GLO/SCL/EDIT hold maps gone: knobs never stolen by a peek */
    ui_power_on();
    go_title("ENV"); frame();
    atk = TSEL->p[P_ATK];
    btn_down(B_GLO); frames(600); turn(EN_K1, 1); btn_up(B_GLO); frame();
    ok = TSEL->p[P_ATK] == atk + 1 || TSEL->p[P_ATK] == atk; /* may eat the turn as quiet */
    bad += check("GLO hold does not open a level map over ENV", !ui.layer && !ui.lock);
    return bad;
}

/* #102: the TIMER5 scan counts the knobs' detents while ui_input runs; one that came after the layer's read of
 * KNOB 1..4 (host_enc_late) was read again by the page's: the layer's control and the page under it both moved
 * (HOME's KNOB 1 with FX's FILTER). Every layer (FX GLO SCL EDIT) x KNOB 1..4 x held past HOLD / a combo / locked
 * by a double tap, over every page and HOME, with late detents on the knob, PRESETS and ALGORITHM while it is open
 * and on the knob as it closes: the same state as with the detents read in time (the layer gets them all, one a
 * frame later), the layer's own control moved, and nothing under the layer changed (lk_same) */
typedef struct {
    int16_t p[NTRK][P_COUNT], g[G_COUNT];
    int8_t k[4];
    uint32_t sel, snd[NTRK], mot[NTRK];
    uint8_t home, page;
} rc_state_t;
static uint32_t rc_snd(const track_t *t) { return t->eng_req | (uint32_t)t->preset << 8 | (uint32_t)t->user << 16; }
static uint32_t rc_snd0;
static void rc_take(rc_state_t *st)
{
    uint32_t i;
    memset(st, 0, sizeof *st);
    for (i = 0; i < NTRK; i++) {
        memcpy(st->p[i], trk[i].p, sizeof st->p[i]);
        st->snd[i] = rc_snd(&trk[i]);
        st->mot[i] = motion_count(&trk[i]);
    }
    memcpy(st->g, song.g, sizeof st->g);
    memcpy(st->k, perf_k, sizeof st->k);
    st->sel = song.sel;
    st->home = ui.home;
    st->page = ui.page;
}
static void rc_enc(uint32_t role, int32_t s, int late)
{
    if (late)
        host_enc_late[panel.enc[role]] += s * panel.dir[role];
    else
        host_enc[panel.enc[role]] += s * panel.dir[role];
}
/* one run: layer button lb, mode (0 held past HOLD, 1 a combo, 2 locked), knob k, page pg (NPAGES: HOME); late:
 * the detents come after the reads. a: the state with the layer open, b: after it closed */
static void rc_run(uint32_t lb, uint32_t mode, uint32_t k, uint32_t pg, int late, rc_state_t *a, rc_state_t *b)
{
    int32_t d = (lb == B_GLO || (lb == B_FX && k == 3u)) ? -1 : 1;   /* (GLO LEVEL, FX DEPTH: room to move) */
    uint32_t i;
    ui_power_on();
    if (pg < NPAGES) { ui.home = 0; ui.page = (uint8_t)pg; page_entered(); }
    frames(64);
    lk_snap();
    rc_snd0 = rc_snd(TSEL);
    if (mode == 2u) {
        press(lb); frames(64); press(lb); frames(64);
    } else {
        btn_down(lb);
        frame();
        if (mode == 0u)
            frames(560);                                /* (past HOLD: the map, a peek) */
    }
    for (i = 0; i < 3u; i++) {
        rc_enc(EN_K1 + k, d, late && (mode != 1u || i));   /* (a combo: the first in time, it opens the map) */
        if (i == 1u) {
            rc_enc(EN_PRESET, 1, late);                 /* (in time: the layer's read drops them) */
            rc_enc(EN_ALGO, 1, late);
        }
        frame();
    }
    frame();                                            /* (the last late one) */
    rc_take(a);
    if (mode == 2u) {
        press(lb);
    } else {
        btn_up(lb);
        frame();
    }
    for (i = 0; i < 3u; i++) {                          /* turning on as it closes (#39: KNOB 1..4 quiet) */
        rc_enc(EN_K1 + k, d, late);
        frame();
    }
    frames(400);
    rc_take(b);
}

static int test_layer_knob_race(void)
{
    int bad = 0, ok;
    int16_t atk;
    ui_power_on();
    go_title("ENV"); frame();
    atk = TSEL->p[P_ATK];
    go_title("PUNCH-IN"); frame();
    turn(EN_K2, 15);
    ok = perf_k[1] == 15 && TSEL->p[P_ATK] == atk;
    press(B_ENV); frame();
    ok &= cur_page()->fam == FAM_ENV && !ui.lock;
    bad += check("#102 Punch-in knobs stay on the map; leaving for ENV clears the sticky lock", ok);
    return bad;
}

/* Discussion #83: the layer lock. A double tap of FX / GLO / SCL / EDIT (within LY_DTAP_MS, both let go before HOLD)
 * opens the map with no button held, over the page the first tap left (put back); the keys and KNOB 1..4 act as held;
 * a tap closes it (no page), so does another page button (which opens its page), HOME, the menu, a dialog; OCT- puts
 * back as in the layer; a single tap, a peek, a combo and two slow taps are as before; the header shows the lock */
/* ------------------------------------------------------ SEQ TOOLS (1.2) --- */
static void tl_note(step_t *s, uint32_t n) { memset(s, 0, sizeof *s); s->time = ST_NOTE; s->n = 1; step_set_note(s, 0, n); s->vel = 96; }
static void tl_tie(step_t *s) { memset(s, 0, sizeof *s); s->time = ST_TIE; }
/* the selected track: LEN 8, C4 tied, E4, G4 (steps 1 2, 4, 6), a note past LEN (21), a lock on step 4 */
static void tl_melody(void)
{
    uint32_t i;
    track_defaults_steps(TSEL);
    for (i = 0; i < NSTEP; i++)
        step_clear(&TSEL->step[i]);
    tl_note(&TSEL->step[0], 60);
    tl_tie(&TSEL->step[1]);
    tl_note(&TSEL->step[3], 64);
    tl_note(&TSEL->step[5], 67);
    tl_note(&TSEL->step[20], 72);
    TSEL->p[P_SLEN] = 8;
    motion_set_lock(TSEL, 3, P_REV, 50);
}
static int tl_lock_at(uint32_t step) { int16_t v; return motion_lock_get(TSEL, step, P_REV, &v) && v == 50; }
static void tl_key(uint32_t w) { key_down(white(w)); frame(); key_up(white(w)); frame(); }
static uint32_t tl_count(const track_t *t, uint32_t l)
{
    uint32_t i, c = 0;
    for (i = 0; i < (uint32_t)t->p[P_SLEN]; i++)
        c += (step_lanes(&t->step[i]) >> l) & 1u;
    return c;
}
static uint32_t tl_lanes_of(const track_t *t, uint32_t l)   /* lane l over steps 1..16, a bit each */
{
    uint32_t i, m = 0;
    for (i = 0; i < 16u; i++)
        m |= ((step_lanes(&t->step[i]) >> l) & 1u) << i;
    return m;
}
static int tl_in_key(const track_t *t)                       /* every note of steps 1..LEN in the track's scale */
{
    uint32_t i, k;
    for (i = 0; i < (uint32_t)t->p[P_SLEN]; i++)
        for (k = 0; k < t->step[i].n; k++)
            if (t->step[i].time == ST_NOTE && !tl_in_scale(t, step_note(&t->step[i], k)))
                return 0;
    return 1;
}
static int tl_times_are(const track_t *t, const step_t *ref, uint32_t len)   /* the rhythm: the same NOTE / TIE / REST */
{
    uint32_t i;
    for (i = 0; i < len; i++)
        if (t->step[i].time != ref[i].time || (t->step[i].n != 0) != (ref[i].n != 0))
            return 0;
    return 1;
}
static void tl_drum(void)
{
    set_engine_of(TSEL, ENGI_DRUM);
    TSEL->engine = TSEL->eng_req;
    track_defaults_steps(TSEL);
    TSEL->p[P_SLEN] = 16;
}


static int test_seq_tools(void)
{
    int bad = 0, ok, okc;
    uint32_t i, k, a, b2, seed;
    step_t ref[NSTEP], mid[NSTEP];

    /* hold SEQ never peeks; the first tap is STEP. Tools are the last section. */
    ui_power_on();
    go_home(); frame();
    btn_down(B_SEQ); frames(500);
    ok = !ui.layer && ui.lock == 0;                     /* still held: no map yet */
    btn_up(B_SEQ); frame();
    ok &= str_eq(cur_page()->title, "STEP") && !ui.lock && !ui.layer;
    bad += check("SEQ tap opens STEP; hold alone never peeks tools", ok);
    ui_power_on();
    press(B_SEQ); frames(100);
    ok = str_eq(cur_page()->title, "STEP") && !ui.lock;
    turn(EN_SELECT, 1); frames(100);
    ok &= str_eq(cur_page()->title, "PATTERN") && !ui.lock;
    turn(EN_SELECT, 1); frames(100);
    ok &= str_eq(cur_page()->title, "DETAIL");
    press(B_SEQ); frames(100);
    ok &= str_eq(cur_page()->title, "AUTOMATION") && !ui.lock;
    press(B_SEQ); frames(100);
    ok &= str_eq(cur_page()->title, "SEQ TOOLS") && ui.lock == LAYER_SEQ && ui.layer == LAYER_SEQ;
    press(B_SEQ); frames(100);
    ok &= str_eq(cur_page()->title, "STEP") && !ui.lock;
    bad += check("SEQ sections: step group (SELECT STEP>PATTERN>DETAIL) → Automation → SEQ TOOLS → Step", ok);

    /* tools on the SEQ TOOLS section (no button held) */
    ui_power_on();
    tl_melody();
    memcpy(ref, TSEL->step, sizeof ref);
    go_title("SEQ TOOLS"); frame();
    usb.config = 1;
    {
        uint32_t mo = mo_w;
        tl_key(1);                                      /* G3: REVERSE */
        ok = ui.layer == LAYER_SEQ && !gates() && mo_w == mo && msg_is("REVERSED");
    }
    usb.config = 0;
    ok &= TSEL->step[6].time == ST_NOTE && step_note(&TSEL->step[6], 0) == 60 && TSEL->step[7].time == ST_TIE &&
          step_note(&TSEL->step[4], 0) == 64 && step_note(&TSEL->step[2], 0) == 67 && TSEL->step[0].time == ST_REST &&
          step_note(&TSEL->step[20], 0) == 72 && tl_lock_at(4) && !tl_lock_at(3);
    bad += check("  G3 REVERSE on SEQ TOOLS: steps 1..LEN mirrored, lock moves with E4", ok);
    hold(B_SAVE);
    bad += check("  SAVE held: the reverse undone", !memcmp(TSEL->step, ref, sizeof ref) && tl_lock_at(3));
    tl_key(3);
    ok = step_note(&TSEL->step[1], 0) == 60 && TSEL->step[2].time == ST_TIE && step_note(&TSEL->step[4], 0) == 64 && tl_lock_at(4) &&
         msg_is("SHIFTED RIGHT");
    tl_key(3); tl_key(2); tl_key(2);
    ok &= !memcmp(TSEL->step, ref, sizeof ref) && tl_lock_at(3);
    bad += check("  B3 / A3 SHIFT > <: one step round inside LEN; back again: as it was", ok);
    tl_key(0);
    for (ok = msg_is("SEQUENCE CLEARED"), i = 0; i < 8u; i++)
        ok &= TSEL->step[i].time == ST_REST && !TSEL->step[i].n;
    ok &= step_note(&TSEL->step[20], 0) == 72 && !motion_lock_count(TSEL);
    bad += check("  F3 CLEAR: steps 1..LEN empty, past LEN kept", ok);
    hold(B_SAVE);
    ok = !memcmp(TSEL->step, ref, sizeof ref) && tl_lock_at(3);
    go_title("SEQ TOOLS"); frame();
    turn(EN_K1, -2);
    ok &= TSEL->p[P_SLEN] == 6 && ui.layer == LAYER_SEQ;
    bad += check("  SAVE held undoes CLEAR; KNOB 1 on SEQ TOOLS edits LEN", ok);

    ok = okc = 1;
    for (seed = 1; seed <= 40u; seed++) {
        ui_power_on();
        tl_melody();
        TSEL->p[P_ROOT] = 2; TSEL->p[P_SCALE] = 2;
        memcpy(ref, TSEL->step, sizeof ref);
        go_title("SEQ TOOLS"); frame();
        rng_state = 0x9E3779B9u * seed;
        tl_key(4);
        ok &= msg_is("RANDOM NOTES") && tl_times_are(TSEL, ref, NSTEP) && tl_in_key(TSEL) && step_note(&TSEL->step[20], 0) == 72;
        for (k = 0; k < 12u; k++) {
            memcpy(mid, TSEL->step, sizeof mid);
            tl_key(5);
            okc &= tl_times_are(TSEL, ref, NSTEP) && tl_in_key(TSEL) && memcmp(mid, TSEL->step, sizeof mid) != 0;
        }
        okc &= msg_is("COOKED 12");
    }
    bad += check("  C4 RANDOM (melodic): rhythm kept, notes in scale (40 seeds)", ok);
    bad += check("  D4 COOK x12: each press changes it, \"COOKED 12\"", okc);

    /* NOTE EDITS on SEQ TOOLS page 1: stored-note edits, one undo like COOK. */
    /* OCT+ / OCT-: stored notes an octave up / down; ties, rests and the note past LEN stay */
    ui_power_on();
    tl_melody();
    go_title("SEQ TOOLS"); frame();
    tl_key(7);                                          /* F4: OCT+ */
    ok = msg_is("OCT UP") && step_note(&TSEL->step[0], 0) == 72 && TSEL->step[1].time == ST_TIE &&
         step_note(&TSEL->step[3], 0) == 76 && step_note(&TSEL->step[5], 0) == 79 &&
         step_note(&TSEL->step[20], 0) == 72 && tl_lock_at(3);
    hold(B_SAVE);
    ok &= !memcmp(TSEL->step, ref, sizeof ref);
    bad += check("  OCT+ transposes stored notes +12, past LEN / TIE untouched; SAVE held undoes", ok);
    tl_key(6);                                          /* E4: OCT- */
    ok = msg_is("OCT DOWN") && step_note(&TSEL->step[0], 0) == 48 && step_note(&TSEL->step[3], 0) == 52 &&
         step_note(&TSEL->step[5], 0) == 55 && step_note(&TSEL->step[20], 0) == 72;
    hold(B_SAVE);
    ok &= !memcmp(TSEL->step, ref, sizeof ref);
    bad += check("  OCT- transposes stored notes -12; SAVE held undoes", ok);

    /* DUP: A -> A A, LEN x2, steps / locks / automation copied; C5 is the last edit on a melodic track */
    go_title("SEQ TOOLS"); frame();
    tl_key(11);                                         /* C5: DUP */
    ok = msg_is("DUPLICATED") && TSEL->p[P_SLEN] == 16 && step_note(&TSEL->step[8], 0) == 60 &&
         TSEL->step[9].time == ST_TIE && step_note(&TSEL->step[11], 0) == 64 && step_note(&TSEL->step[13], 0) == 67 &&
         step_note(&TSEL->step[20], 0) == 72 && tl_lock_at(3) && tl_lock_at(11);
    hold(B_SAVE);
    ok &= TSEL->p[P_SLEN] == 8 && !memcmp(TSEL->step, ref, sizeof ref) && tl_lock_at(3) && !tl_lock_at(11);
    bad += check("  DUP copies steps 1..LEN after themselves; LEN 8 -> 16; locks copy; SAVE held undoes", ok);

    /* HALF: each step twice as long, LEN x2; TIEs follow their note, short notes stay short */
    go_title("SEQ TOOLS"); frame();
    tl_key(8);                                          /* G4: HALF */
    ok = msg_is("HALF TIME") && TSEL->p[P_SLEN] == 16 && TSEL->step[0].time == ST_NOTE &&
         step_note(&TSEL->step[0], 0) == 60 && TSEL->step[1].time == ST_TIE && TSEL->step[2].time == ST_TIE &&
         TSEL->step[3].time == ST_TIE && step_note(&TSEL->step[6], 0) == 64 &&
         step_note(&TSEL->step[10], 0) == 67 && step_note(&TSEL->step[20], 0) == 72 && tl_lock_at(6);
    hold(B_SAVE);
    ok &= TSEL->p[P_SLEN] == 8 && !memcmp(TSEL->step, ref, sizeof ref);
    bad += check("  HALF stretches steps x2 (LEN 8 -> 16), ties hold; SAVE held undoes", ok);

    /* *2: each pair becomes one, LEN /2; the first playing step wins, a held note becomes a tie */
    go_title("SEQ TOOLS"); frame();
    tl_key(9);                                          /* A4: *2 */
    ok = msg_is("DOUBLE TIME") && TSEL->p[P_SLEN] == 4 && step_note(&TSEL->step[0], 0) == 60 &&
         step_note(&TSEL->step[1], 0) == 64 && step_note(&TSEL->step[2], 0) == 67 && TSEL->step[3].time == ST_REST &&
         step_note(&TSEL->step[20], 0) == 72 && tl_lock_at(1) && !tl_lock_at(3);
    hold(B_SAVE);
    ok &= TSEL->p[P_SLEN] == 8 && !memcmp(TSEL->step, ref, sizeof ref) && tl_lock_at(3);
    bad += check("  *2 squeezes pairs (LEN 8 -> 4), first note wins; SAVE held undoes", ok);

    /* Colliding locks follow the retained note, regardless of record insertion order. */
    ui_power_on();
    tl_melody();
    tl_note(&TSEL->step[1], 62);
    motion_set_lock(TSEL, 1, P_REV, 90);
    motion_set_lock(TSEL, 0, P_REV, 30);
    motion_set_lock(TSEL, 2, P_REV, 70);             /* step 3 is empty: step 4's E wins */
    go_title("SEQ TOOLS"); frame();
    tl_key(9);
    {
        int16_t v;
        project_store_t packed;
        project_t project;
        project_capture(&project);
        ok = motion_lock_get(TSEL, 0, P_REV, &v) && v == 30 &&
             motion_lock_get(TSEL, 1, P_REV, &v) && v == 50 &&
             proj_pack(&packed, &project);
    }
    bad += check("  *2 keeps the retained note's locks and the project remains saveable", ok);
    hold(B_SAVE);
    ui_power_on();
    tl_melody();
    go_title("SEQ TOOLS"); frame();

    /* HUMAN: per step, fresh velocity / nudge each press; QUANTIZE off so the nudge plays */
    go_title("SEQ TOOLS"); frame();
    TSEL->p[P_SQNT] = 1;
    rng_state = 0x2468ACEu;
    tl_key(10);                                         /* B4: HUMAN */
    ok = msg_is("HUMANIZED QNTZ OFF") && TSEL->p[P_SQNT] == 0 && TSEL->step[1].time == ST_TIE &&
         TSEL->step[2].time == ST_REST;
    for (i = 0; i < 8u; i++) {
        int32_t v, n;
        if (TSEL->step[i].time != ST_NOTE || (!TSEL->step[i].n && !TSEL->step[i].hit))
            continue;
        v = TSEL->step[i].vel ? TSEL->step[i].vel : 96;
        n = step_nudge(&TSEL->step[i]);
        ok &= v >= 96 - 16 && v <= 96 + 16 && n >= -2 && n <= 2;
    }
    hold(B_SAVE);
    ok &= TSEL->p[P_SQNT] == 1 && !memcmp(TSEL->step, ref, sizeof ref);
    bad += check("  HUMAN writes velocity / nudge per step, QUANTIZE off; SAVE held undoes both", ok);

    /* OCT- on the page restores the whole visit: steps, LEN and QUANTIZE */
    ui_power_on();
    tl_melody();
    memcpy(ref, TSEL->step, sizeof ref);
    TSEL->p[P_SQNT] = 1;
    go_title("SEQ TOOLS"); frame();
    tl_key(7);
    tl_key(11);
    tl_key(10);
    press(B_OCTDN);
    ok = TSEL->p[P_SLEN] == 8 && TSEL->p[P_SQNT] == 1 && !memcmp(TSEL->step, ref, sizeof ref) &&
         tl_lock_at(3) && !tl_lock_at(11);
    bad += check("  OCT- on SEQ TOOLS puts back every edit of the visit, QUANTIZE too", ok);

    ui_power_on();
    tl_drum();
    go_title("SEQ TOOLS"); frame();
    tl_key(6);
    ok = msg_is("BASIC BEAT") && tl_lanes_of(TSEL, DV_KICK) == 0x1111u && tl_lanes_of(TSEL, DV_SNARE) == 0x1010u;
    key_down(black(2)); frame(); key_up(black(2)); frame();
    ok &= ui.lane == DV_CLAP && !gates();
    bad += check("  E4 BEAT on SEQ TOOLS; black key 3 picks the lane", ok);
    {
        step_t kept[NSTEP];
        uint32_t bit = 1u << DV_CLAP;
        memcpy(kept, TSEL->step, sizeof kept);
        tl_key(7);                                      /* F4: mute the selected lane */
        ok = (drum_mute[song.sel % NPART] & bit) && msg_is("CLAP MUTED") && !memcmp(TSEL->step, kept, sizeof kept);
        key_down(black(0)); frame(); key_up(black(0)); frame();
        tl_key(7);
        ok &= ui.lane == DV_KICK && (drum_mute[song.sel % NPART] & 1u) && (drum_mute[song.sel % NPART] & bit) &&
              msg_is("KICK MUTED");
        tl_key(7);
        ok &= !(drum_mute[song.sel % NPART] & 1u) && msg_is("KICK ON");
        chain.running = 1;
        tl_key(7);
        ok &= (drum_mute[song.sel % NPART] & 1u) && msg_is("KICK MUTED") && !memcmp(TSEL->step, kept, sizeof kept);
        chain.running = 0;
        bad += check("  F4 MUTE toggles the selected lane (each drum on its own); a song playing still mutes", ok);
    }
    {
        step_t kept[NSTEP];
        memcpy(kept, TSEL->step, sizeof kept);
        tl_key(12);                                     /* D5: HALF also fits on a DRUM track's page */
        ok = msg_is("HALF TIME") && TSEL->p[P_SLEN] == 32;
        hold(B_SAVE);
        ok &= TSEL->p[P_SLEN] == 16 && !memcmp(TSEL->step, kept, sizeof kept);
        bad += check("  D5 HALF on a DRUM track: the note edits share page 1; SAVE held undoes", ok);
    }

    ui_power_on();
    go_title("PATTERN"); frame();
    press(B_SEQ); frames(100);                           /* Pattern → Automation: no sticky map */
    ok = !ui.lock && str_eq(cur_page()->title, "AUTOMATION");
    go_seq_step(); frame();
    ok &= !ui.lock && str_eq(cur_page()->title, "STEP");
    bad += check("  no double-tap SEQ lock; Step section has no sticky map", ok);
    chain.running = 1;
    go_title("SEQ TOOLS"); frame();
    memcpy(ref, TSEL->step, sizeof ref);
    tl_key(4);
    ok = msg_is("STOP TO EDIT") && !memcmp(TSEL->step, ref, sizeof ref);
    chain.running = 0;
    bad += check("  while a song plays: STOP TO EDIT", ok);
    return bad;
}


static int test_layer_lock(void)
{
    static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
    int bad = 0, ok = 1;
    uint32_t i, pg;
    for (i = 0; i < 4u; i++) {
        ui_power_on();
        go_title("ENV"); frame();
        pg = ui.page;
        press(LB[i]); frames(64); press(LB[i]); frames(500);
        if (LB[i] == B_FX)
            ok &= ui.lock == LAYER_FX && cur_page()->sec == FX_SEC_PUNCH; /* Effects → Punch-in */
        else
            ok &= !ui.lock && ui.page != pg;           /* second tap: section/page, never a lock */
    }
    bad += check("no layer lock: double-tap GLO/SCL/EDIT; FX second tap is Punch-in", ok);
    ui_power_on();
    go_title("ENV"); frame();
    press(B_SEQ); frames(100);
    ok = !ui.lock && str_eq(cur_page()->title, "STEP"); /* step group lands on STEP */
    turn(EN_SELECT, 1); frames(100);
    ok &= str_eq(cur_page()->title, "PATTERN") && !ui.lock;
    press(B_SEQ); frames(100);
    ok &= str_eq(cur_page()->title, "AUTOMATION") && !ui.lock;
    press(B_SEQ); frames(100);
    ok &= str_eq(cur_page()->title, "SEQ TOOLS") && ui.lock == LAYER_SEQ;
    bad += check("  SEQ taps cycle step group → Automation → SEQ TOOLS; only tools stick", ok);
    ui_power_on();
    press(B_FX); press(B_FX); frame();                 /* Effects → Punch-in */
    ok = ui.lock == LAYER_FX && cur_page()->sec == FX_SEC_PUNCH;
    press(B_FX); frame();
    ok &= ui.lock == 0 && cur_page()->sec == FX_SEC_INS && str_eq(cur_page()->title, "EFFECTS");
    bad += check("  only Punch-in sticky-locks LAYER_FX; leaving clears the lock", ok);
    return bad;
}

/* REC tap arms / disarms; hold never opens a prefs map (CLICK / COUNT-IN / LEVEL: MENU > AUDIO). */
static int test_rec_layer(void)
{
    int bad = 0, ok;
    ui_power_on();
    go_seq_step(); frame();
    press(B_REC);
    ok = song.rec == 1u && !transport_req && msg_is("KEYS WRITE STEPS");
    press(B_REC);
    ok &= song.rec == 0u;
    bad += check("REC tap arms / disarms step recording on STEP (no hold map)", ok);
    ui_power_on();
    go_home(); frame();
    btn_down(B_REC); frames(600);
    ok = !ui.layer && !ui.lock && song.rec == 0u;       /* still down: arming on release */
    btn_up(B_REC); frame();
    ok &= !ui.layer && !ui.lock && song.rec == 1u && ui.home &&
          (transport_req == 1u || msg_is("PLAY FREELY"));
    bad += check("REC hold never opens a layer; release still arms on HOME", ok);
    return bad;
}

/* The FX layer's white keys hold the punch page's fixed map (no reassignment: PUNCH-IN 1/2 = PF_DEF, 2/2 =
 * PF2_DEF, SELECT walks the two on the sticky map); the lock frees a hand */

static int test_fx_map(void)
{
    int bad = 0, ok;
    /* Punch-in sticky: the page's map under the keys; PRESETS does nothing; SELECT walks the two PUNCH-IN pages */
    ui_power_on();
    go_title("PUNCH-IN"); frame();
    ok = ui.lock == LAYER_FX && cur_page()->sec == FX_SEC_PUNCH &&
         perf_key(white(4)) == PF_LPF;                    /* C4: LPF */
    key_down(white(4)); frame();                       /* C4 */
    turn(EN_PRESET, 1);
    ok &= (perf_held & PF_BIT(PF_LPF)) != 0u && perf_key(white(4)) == PF_LPF;   /* no reassignment */
    key_up(white(4)); frame();
    bad += check("Punch-in sticky: map open without holding FX; the keys hold the page's map (PRESETS: nothing)", ok);
    ui_power_on();
    go_title("PUNCH-IN"); frame();
    ok = cur_page()->sec == FX_SEC_PUNCH && ui.lock == LAYER_FX;
    turn(EN_SELECT, 1); frame();
    ok &= !ui.home && cur_page()->sec == FX_SEC_PUNCH && str_eq(cur_page()->title, "PUNCH-IN") &&
          ui.lock == LAYER_FX && perf_key(white(4)) == PF_ODN;   /* 2/2: C4 is PITCH DN */
    turn(EN_SELECT, -1); frame();
    ok &= str_eq(cur_page()->title, "PUNCH-IN") && perf_key(white(4)) == PF_LPF;
    bad += check("  SELECT walks the sticky punch map's PUNCH-IN pages (1/2, 2/2), the map follows", ok && ui.lock == LAYER_FX);
    ui_power_on();
    btn_down(B_FX); frames(600);
    ok = !ui.layer && !ui.lock;
    btn_up(B_FX); frame();
    bad += check("FX hold alone never opens the punch map", ok);
    /* double-tap lock removed */
    ui_power_on();
    go_title("ENV"); frame();
    press(B_FX); frames(64); press(B_FX); frames(100);
    ok = ui.lock == LAYER_FX && cur_page()->sec == FX_SEC_PUNCH;
    bad += check("FX double-tap: second tap is Punch-in (sticky)", ok);
    return bad;
}

/* #40: MENU > FX LATCH ON: a Punch-in key toggles its effect (press again off); macros stay; G5 CLR turns
 * latched effects and FILTER/CRUSH/THROW/DEPTH off; OCT± shift octave; the menu turns them off; OFF (default):
 * punch keys are momentary and G5 is not CLR. Punch-in is the sticky FX map. */

static int test_fx_latch(void)
{
    int bad = 0, ok;
    uint32_t lpf = white(4), m1 = black(0), clr = white(15); /* C4: LPF; A#3 mute T1; G5 CLR when LATCH on */
    ui_power_on();
    ok = !(fx_latch & 1u);
    go_title("PUNCH-IN"); frame();
    ok &= ui.lock == LAYER_FX && cur_page()->sec == FX_SEC_PUNCH;
    ok &= perf_key(clr) != PF_CLR;                      /* LATCH off: no CLR on G5 */
    key_down(lpf); frame();
    ok &= (perf_held & PF_BIT(PF_LPF)) != 0u && !perf_latched;
    key_up(lpf); frame();
    ok &= !(perf_held & PF_BIT(PF_LPF)) && !perf_latched;
    bad += check("#40 FX LATCH OFF: Punch-in keys are momentary; G5 is not CLR", ok);
    /* ON */
    ui_power_on();
    fx_latch = 1;
    frame();                                            /* (ui_input hands the setting to the ISR) */
    go_title("PUNCH-IN"); frame();
    ok = perf_key(clr) == PF_CLR;
    key_down(lpf); frame(); key_up(lpf); frame();
    turn(EN_K2, 30);
    ok &= perf_latched == PF_BIT(PF_LPF) && !perf_held && ui.layer == LAYER_FX &&
         str_eq(layer_head(), "[FX] LATCH") && ((layer_leds(0) >> lpf) & 1u);
    ok &= perf_k[1] == 30;
    ok &= perf_begin(CTL) && (perf_act & PF_BIT(PF_LPF));   /* it runs with nothing held */
    bad += check("  FX LATCH ON: Punch-in C4 latches LPF, CRUSH stays at 30 after the key is let go", ok);
    key_down(m1); frame(); key_up(m1); frame();         /* a mute latches too */
    ok = perf_latched == (PF_BIT(PF_LPF) | PF_BIT(PF_M1));
    key_down(lpf); frame(); key_up(lpf); frame();       /* C4 again: LPF off */
    ok &= perf_latched == PF_BIT(PF_M1);
    bad += check("  a mute key latches; C4 again: LPF off", ok);
    turn(EN_K1, -40);
    key_down(lpf); frame(); key_up(lpf); frame();       /* LPF on again with FILTER -40, CRUSH 30 */
    ok = perf_latched == (PF_BIT(PF_LPF) | PF_BIT(PF_M1)) && perf_k[0] == -40 && perf_k[1] == 30;
    press(B_OCTUP);
    ok &= perf_latched == (PF_BIT(PF_LPF) | PF_BIT(PF_M1)) && perf_k[0] == -40 && perf_k[1] == 30 &&
          song.octave == 1;
    press(B_OCTDN);
    ok &= perf_latched == (PF_BIT(PF_LPF) | PF_BIT(PF_M1)) && perf_k[0] == -40 && perf_k[1] == 30 &&
          song.octave == 0;
    bad += check("  OCT± on Punch-in with LATCH: shift octave; latched effects and knobs stay", ok);
    key_down(clr); frame(); key_up(clr); frame();
    ok = !perf_latched && !perf_k[0] && !perf_k[1] && msg_is("FX ALL OFF");
    bad += check("  G5 CLR: every latched effect and FILTER/CRUSH/THROW/DEPTH off", ok);
    key_down(lpf); frame(); key_up(lpf); frame();
    ok = perf_latched == PF_BIT(PF_LPF);
    hold(B_HOME);
    ok &= ui.menu && !perf_latched;
    ui.menu_sel = MI_LATCH;
    press(B_OCTDN); ok &= !(fx_latch & 1u) && ui.menu;
    press(B_OCTUP); ok &= (fx_latch & 1u) && ui.menu;
    turn(EN_K1, -1); ok &= !(fx_latch & 1u);
    turn(EN_K1, 1); ok &= (fx_latch & 1u) != 0u;
    turn(EN_K1, -1); ok &= !(fx_latch & 1u);
    press(B_HOME);
    bad += check("  the menu turns the latched effects off; MENU > FX LATCH: OCT+ / KNOB 1 right ON, OCT- / left OFF",
                 ok && !ui.menu);
    {   /* kept with the settings, in the favorites record (an older one: OFF) */
        persist_t p;
        fx_latch = 1;
        settings_export(&p);
        fx_latch = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && (fx_latch & 1u);
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;                          /* PER3: no favorites */
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !(fx_latch & 1u);
        bad += check("  FX LATCH saved with the settings; settings from before it: OFF", ok);
    }
    fx_latch = 0;
    return bad;
}

/* #48: the note divisions step by their length, longest first (triplets between their neighbours), on every list
 * that has them (SEQ DIV, ARP RATE, the delay's TIME, the SLICER's RATE); the stored values and their sound stay */
static int test_div_order(void)
{
    int bad = 0, ok = 1;
    uint32_t id, c, k, found = 0;
    static const param_desc_t *const LISTS[] = {&TP[P_SDIV], &TP[P_ARATE], &GP[G_DTIME], &TP[P_SLRATE]};
    ui_power_on();
    for (k = 0; k < NELEM(LISTS); k++) {           /* left end -> right end: each one shorter than the one before */
        const param_desc_t *d = LISTS[k];
        int32_t v = d->def, n, seen = 0, nshow = enum_order_n(d);
        uint32_t prev = 0xFFFFFFFFu, len;
        for (n = 0; n < 40; n++) v = param_turn(d, v, -1);
        for (n = 0; n < nshow; n++) {
            len = d->names == N_SLDIV ? 1000000u / SL_DEN[v] : v >= 10 ? (beat_samples() * 3u) >> DIV_DOT[v - 10] :
                  div_samples((uint32_t)v);                     /* (TIME's dotted ones, 1.5.1: fx.c delay_samples) */
            if (!(len < prev && enum_rank(d, v) == d->min + n && !((seen >> v) & 1))) {
                printf("  %s: %s (value %d, place %d, length %u) after %u\n", d->label, d->names[v], (int)v,
                       (int)enum_rank(d, v), len, prev);
                ok = 0;
            }
            seen |= 1 << v;
            prev = len;
            v = param_turn(d, v, 1);
        }
        ok &= d->names != N_DTIME ? seen == (1 << nshow) - 1
                                 : seen == 0x1C7F && !(seen & 0x380); /* TIME: 0..6,10..12; not 7..9 */
    }
    ok &= str_eq(N_DIV[2], "1/16") && str_eq(N_DIV[4], "8T") && str_eq(N_DIV[9], "4BAR") && TP[P_SDIV].def == 2;
    ok &= param_turn(&TP[P_SDIV], 2, 1) == 5 && param_turn(&TP[P_SDIV], 2, -1) == 4 &&   /* 1/16: right 16T, left 8T */
          param_turn(&TP[P_SDIV], 3, 1) == 3 && param_turn(&TP[P_SDIV], 9, -1) == 9 &&   /* the ends hold */
          param_turn(&TP[P_SDIV], 2, -6) == 8 && param_turn(&TP[P_SDIV], 2, 40) == 3;    /* 6 left: 2BAR; far: clamped */
    bad += check("#48 DIV / RATE / TIME / SLICER RATE step longest -> shortest, every value once, the ends hold", ok);
    ok = 1;                                         /* the real knob on the pages that show them, the gauge follows */
    for (id = 0; id < NPAGES; id++)
        for (c = 0; c < 4u; c++) {
            int16_t *vp;
            const param_desc_t *d;
            ui_power_on();
            ui.home = 0; ui.page = (uint8_t)id; page_entered();
            if (!page_visible(id) || !(d = page_desc(cur_page(), c, &vp)) || !vp ||
                (d->names != N_DIV && d->names != N_SLDIV && d->names != N_DTIME))
                continue;
            found++;
            *vp = d->names != N_SLDIV ? 2 : 1;      /* 1/16 */
            turn(EN_K1 + c, 1);
            ok &= *vp == (d->names != N_SLDIV ? 5 : 4);  /* 16T */
            turn(EN_K1 + c, -2);
            ok &= *vp == (d->names != N_SLDIV ? 4 : 3);  /* 8T */
        }
    ui_power_on();                                  /* SEQ TOOLS: the sticky map, KNOB 2 = DIV */
    go_title("SEQ TOOLS"); frame();
    TSEL->p[P_SDIV] = 2;
    turn(EN_K2, 1);
    ok &= ui.layer == LAYER_SEQ && TSEL->p[P_SDIV] == 5;
    turn(EN_K2, -2);
    ok &= TSEL->p[P_SDIV] == 4;
    bad += check("#48 the pages' knobs step the divisions by length (SEQ TOOLS, ARP, DLY, SLICER)", ok && found >= 3u);
    return bad;
}

/* #52 knob acceleration (always on; KNOB ACCEL menu row retired): a sustained fast turn of a wide value goes
 * x2..x4 (x8 over a range above 64, #126); a slow turn, the start of a turn, a lone quick detent and a reversal are
 * one step per detent; the sign is always the detents'. SELECT's page walk never uses accel; OCT+ tempo does. */
static uint32_t acc_ms(uint32_t role, const uint16_t *gaps, uint32_t n, int32_t dir, int32_t range, int32_t *out)
{
    uint32_t k, sum = 0;
    for (k = 0; k < n; k++) {
        fm1_ms += gaps[k];
        out[k] = accel(role, dir, range);
        sum += (uint32_t)(out[k] < 0 ? -out[k] : out[k]);
    }
    return sum;
}
static int test_knob_accel(void)
{
    int bad = 0, ok;
    uint32_t k, seed = 12345u;
    int32_t o[16];
    static const uint16_t SLOW[8] = {500, 60, 60, 45, 41, 60, 80, 60}, MID[8] = {500, 30, 30, 30, 26, 30, 30, 30},
                          F20[8] = {500, 20, 20, 20, 20, 20, 20, 20}, F10[8] = {500, 10, 10, 10, 10, 10, 10, 10},
                          BOUNCE[8] = {500, 2, 300, 1, 300, 3, 300, 2};
    ui_power_on();
    ok = menu_get(MI_ACCEL) == 1u && MI_TAB[MI_ACCEL] == MTAB_COUNT && str_eq(MI_NAME[MI_ACCEL], "KNOB ACCEL");
    ok &= acc_ms(EN_K1, SLOW, 8, 1, 127, o) == 8u && acc_ms(EN_K2, MID, 8, -1, 127, o) == 8u && o[7] == -1;
    ok &= acc_ms(EN_K3, BOUNCE, 8, 1, 127, o) == 8u;
    bad += check("#52 KNOB ACCEL always on (retired); slow (>= 26 ms a detent) and lone quick detents: 1 step each", ok);
    fm1_ms += 1000;
    acc_ms(EN_K1, F20, 8, 1, 64, o);
    ok = o[0] == 1 && o[1] == 1 && o[2] == 2 && o[7] == 2;          /* 20 ms a detent: x2 from the third */
    acc_ms(EN_K1, F10, 8, 1, 64, o);                               /* (a pause of 500 ms first: a new turn) */
    ok &= o[0] == 1 && o[1] == 1 && o[2] == 4 && o[7] == 4;         /* 10 ms: x4 (the cap of a range <= 64) */
    fm1_ms += 15; ok &= accel(EN_K1, 1, 64) == 3;                  /* 15 ms: x3 */
    fm1_ms += 10; ok &= accel(EN_K1, -1, 64) == -1;                /* reversed: one step, the right sign */
    fm1_ms += 10; ok &= accel(EN_K1, -1, 64) == -1;                /* (the turn starts again) */
    fm1_ms += 10; ok &= accel(EN_K1, -1, 64) == -4;
    fm1_ms += 30; ok &= accel(EN_K1, -3, 64) == -12;               /* 3 detents in one read (10 ms each): x4 */
    bad += check("#52 KNOB ACCEL: 20 ms a detent x2, 15 ms x3, 10 ms x4 from a turn's third detent; reversal x1", ok);
    {   /* #126 a range above 64: 10 ms x5, 8 ms x6, 6 ms and less x8; a flick of 12 detents in ~120 ms, faster in
         * the middle, moves a -100..100 value ~50 (was 38); 12 even ones 52 */
        static const uint16_t FLICK[12] = {500, 16, 12, 10, 8, 7, 7, 8, 10, 12, 16, 20}, F6[8] = {500, 6, 6, 5, 4, 6, 6, 6};
        uint32_t f;
        fm1_ms += 1000;
        f = acc_ms(EN_K2, FLICK, 12, 1, 200, o);
        ok = f == 47u && o[0] == 1 && o[1] == 1;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F10, 8, -1, 200, o) == 2u + 6u * 5u && o[7] == -5;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F6, 8, 1, 127, o) == 2u + 6u * 8u;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F6, 8, 1, 64, o) == 2u + 6u * 4u;      /* (a range <= 64: x4 at most) */
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F6, 8, 1, 0, o) == 8u;                  /* (a list) */
        printf("  #126 a flick of 12 detents over %u ms on -100..100: %u steps\n", 126u, (unsigned)f);
        bad += check("#126 KNOB ACCEL over a range above 64: 10 ms x5 .. 6 ms x8; a 12-detent flick ~120 ms: 47", ok);
    }
    {   /* free-ms TIME (~1486): slow still x1; x8 of the usual from the turn's second fast detent, so a small fast
          * turn registers; a hard flick covers hundreds of ms (6 ms x64, 4 ms x96, 3 ms and less: the x128 cap) */
        static const uint16_t F6[8] = {500, 6, 6, 6, 6, 6, 6, 6}, F4[8] = {500, 4, 4, 4, 4, 4, 4, 4},
                               F3[8] = {500, 3, 3, 3, 3, 3, 3, 3};
        fm1_ms += 1000;
        ok = acc_ms(EN_K3, SLOW, 8, 1, (int32_t)DTIME_MS_MAX, o) == 8u;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K3, F6, 8, 1, (int32_t)DTIME_MS_MAX, o) == 1u + 7u * 64u && o[7] == 64;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K3, F10, 8, 1, (int32_t)DTIME_MS_MAX, o) == 1u + 7u * 40u && o[7] == 40;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K3, F20, 8, 1, (int32_t)DTIME_MS_MAX, o) == 1u + 7u * 16u && o[7] == 16;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K3, F4, 8, 1, (int32_t)DTIME_MS_MAX, o) == 1u + 7u * 96u && o[7] == 96;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K3, F3, 8, 1, (int32_t)DTIME_MS_MAX, o) == 1u + 7u * 128u && o[7] == 128;
        bad += check("KNOB ACCEL free-ms range: slow x1, 20 ms x16, 10 ms x40, 6 ms x64, 4 ms x96, 3 ms x128", ok);
    }
    fm1_ms += 1000; acc_ms(EN_K4, F10, 8, 1, 32, o);
    ok = o[7] == 1;                                                /* a narrow range (<= 32): never */
    fm1_ms += 1000; acc_ms(EN_K4, F10, 8, 1, 0, o);
    ok &= o[7] == 1;                                               /* a list of names (range 0 at the callers) */
    for (k = 0; k < 20000u && ok; k++) {                           /* random turns: the sign is the detents', at */
        int32_t s, r, gap;                                         /* most x8, a slow detent exactly one step */
        seed = seed * 1103515245u + 12345u;
        s = (int32_t)((seed >> 16) % 7u) - 3;
        gap = (int32_t)((seed >> 8) % 90u);
        fm1_ms += (uint32_t)gap;
        r = accel(EN_K1 + (seed & 3u), s, 127);
        ok &= s ? (r > 0) == (s > 0) && r * s <= 8 * s * s && r * s >= s * s : r == 0;
        if (gap >= 40 * (s < 0 ? -s : s) && r != s) ok = 0;
    }
    bad += check("#52 KNOB ACCEL: never on narrow values or lists; 20000 random reads keep the sign, x1..x8, slow = x1", ok);
    {   /* the real knob: LEVEL on MIXER, a detent a frame (16 ms): 1 + 1 + 3 x 6 = 20 for 8 detents */
        int16_t v0;
        ui_power_on();
        go_title("MIXER");
        TSEL->p[P_LEVEL] = 40; v0 = TSEL->p[P_LEVEL];
        fm1_ms += 1000;
        for (k = 0; k < 8u; k++) turn(EN_K1, 1);
        ok = TSEL->p[P_LEVEL] - v0 == 20;
        bad += check("#52 KNOB ACCEL on MIXER LEVEL: a fast turn of 8 detents moves 20", ok);
    }
    {   /* #126 Punch-in sticky FX map KNOB 1 (FILTER -100..100): flick of 12 detents; slow: 12;
         * 6 right then 6 left: 0. (GLO hold map removed: MIXER LEVEL covers accel on a track level above.) */
        static const uint32_t GAP[3] = {10u, 60u, 10u};
        int32_t got[3];
        uint32_t c;
        for (c = 0; c < 3u; c++) {
            ui_power_on();
            go_title("PUNCH-IN"); frames(64);
            for (k = 0; k < 12u; k++) {
                host_enc[panel.enc[EN_K1]] += (c == 2u && k >= 6u ? -1 : 1) * panel.dir[EN_K1];
                fm1_ms += GAP[c] - 16u;                         /* (frame() adds 16) */
                frame();
            }
            got[c] = perf_k[0];
        }
        printf("  #126 FX FILTER, 12 detents: 10 ms %d, 60 ms %d, 6 right 6 left %d\n", (int)got[0],
               (int)got[1], (int)got[2]);
        ok = got[0] >= 35 && got[0] <= 60 && got[1] == 12 && got[2] == 0;
        ui_power_on();
        go_title("MIXER"); frames(64);
        trk[song.sel].p[P_LEVEL] = 0;
        for (k = 0; k < 12u; k++) {
            host_enc[panel.enc[EN_K1]] += panel.dir[EN_K1];
            fm1_ms -= 6u;
            frame();
        }
        ok &= trk[song.sel].p[P_LEVEL] > 12 + 12;
        bad += check("#126 KNOB ACCEL on the FX layer (FILTER: a 12-detent flick >= 35, slow 12) and LEVEL", ok);
    }
    {   /* OCT+ + SELECT tempo accelerates; SELECT page walk does not (one page per detent) */
        int16_t b0;
        uint32_t p0, p1;
        ui_power_on();
        go_title("ENV"); frame();
        p0 = ui.page;
        b0 = song.g[G_BPM];
        btn_down(B_OCTUP); frame();
        fm1_ms += 1000;
        for (k = 0; k < 8u; k++) {
            host_enc[panel.enc[EN_SELECT]] += panel.dir[EN_SELECT];
            fm1_ms -= 6u;
            frame();
        }
        ok = song.g[G_BPM] - b0 > 8 && ui.page == p0;
        btn_up(B_OCTUP); frame();
        go_title("ENV"); frame();
        p0 = ui.page;
        turn(EN_SELECT, 1); frame();
        p1 = ui.page;
        ok &= p1 != p0 && cur_page()->fam == FAM_ENV;
        fm1_ms += 1000;
        for (k = 0; k < 3u; k++) {
            host_enc[panel.enc[EN_SELECT]] += panel.dir[EN_SELECT];
            fm1_ms -= 6u;
            frame();
        }
        /* three fast detents: one page each, not multiplied; stop at last (ENV DEST) */
        ok &= ui.page == p1 && str_eq(cur_page()->title, "ENV DEST");
        bad += check("#52 OCT+SELECT tempo accelerates; SELECT page walk is one page per detent", ok);
    }
    ui_prefs = 0;
    return bad;
}

/* #51 ("a melody appears when I change presets"): browsing every sound in the list (every engine's presets, then user
 * presets, one of them holding a pattern), on HOME and the PRESETS page, stopped and playing, on an empty track: no
 * step is written, the ARP stays off, nothing is held, the transport is not started, no message about a pattern. (In
 * 0.9 a preset carried a 16-step pattern and replaced the steps; since 1.0 only SEQ > PHRASES loads one.) */
static int test_browse_no_pattern(void)
{
    int bad = 0, ok = 1;
    uint32_t pass, n, total, loads = 0, pat_hints = 0;
    track_t *t;
    for (pass = 0; pass < 4u; pass++) {
        ui_power_on();
        t = TSEL;
        my_steps(t); t->p[P_SDIV] = 2;
        up_store(3, "MINE");                          /* a user preset that holds a pattern */
        up_store(5, "PLAIN");
        track_defaults_steps(t);
        ok &= seq_is_empty(t) && up_has_pat(3);
        if (pass & 1u) song.playing = 1;              /* playing: the other tracks run, this one is empty */
        if (pass & 2u) go_page(GR_BROWSE);
        preset_pos(&total);
        for (n = 0; n < total + 2u; n++) {
            turn(EN_PRESET, 1);
            loads++;
            pat_hints += preset_pat_hint() >= 0;
            if (!seq_is_empty(t) || t->p[P_AMODE] || t->nheld || (!(pass & 1u) && (song.playing || transport_req))) {
                printf("  pass %u, sound %u (%s %u): steps %d ARP %d held %u playing %d\n", pass, n,
                       ENGINES[t->eng_req % NENGINES]->name, t->preset, !seq_is_empty(t), t->p[P_AMODE], t->nheld,
                       song.playing);
                ok = 0;
                break;
            }
        }
    }
    if (!ok || loads < 200u || pat_hints <= 10u) printf("  ok %d loads %u hints %u\n", ok, loads, pat_hints);
    bad += check("#51 every sound browsed (HOME, PRESETS; stopped, playing): no step, ARP off, nothing held, no start",
                 ok && loads >= 200u && pat_hints > 10u);
    return bad;
}

/* MENU > USB LEVEL: MASTER (the default, every older setting) / FIXED; the audio follows at once (audio_test: the levels) */
static int test_usb_level(void)
{
    int bad = 0, ok;
    ui_power_on();
    frame();
    ok = !fx_usb_fixed && str_eq(MI_NAME[MI_USB], "USB LEVEL") && str_eq(menu_flag(MI_USB)->name[0], "MASTER");
    hold(B_HOME);
    ui.menu_sel = MI_USB;
    ui.force = 1; ui_draw();
    turn(EN_K1, 1); ok &= (ui_prefs & PREF_USB_FIXED) && fx_usb_fixed;      /* right: FIXED */
    turn(EN_K1, -1); ok &= !(ui_prefs & PREF_USB_FIXED) && !fx_usb_fixed;   /* left: MASTER */
    press(B_OCTUP); ok &= (ui_prefs & PREF_USB_FIXED) && fx_usb_fixed && ui.menu;
    press(B_OCTDN); ok &= !(ui_prefs & PREF_USB_FIXED) && !fx_usb_fixed && ui.menu;
    press(B_OCTUP);
    press(B_HOME);
    ok &= !ui.menu && fx_usb_fixed;
    {
        persist_t p;
        settings_export(&p);
        ui_prefs = 0;
        ok &= settings_import(&p, (int)sizeof p) == 1 &&
              ui_prefs == (uint8_t)(PREF_USB_FIXED | PREF_ANIM_OFF);
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && ui_prefs == PREF_ANIM_OFF;
        frame();
        ok &= !fx_usb_fixed;
    }
    bad += check("MENU > USB LEVEL: MASTER by default, KNOB 1 / OCT+ FIXED, left / OCT- MASTER, saved, the audio follows", ok);
    ui_prefs = 0;
    frame();
    return bad;
}

/* 1.1 (Discussion #131) MENU > AUDIO CLICK (OFF REC ON), CLICK LEVEL (LOW MID HIGH), COUNT-IN (OFF 1 BAR 2 BARS): OFF / MID /
 * OFF by default and in every older setting; a knob applies at once (click.c, seq.c); saved and read back, a stored 3
 * (no value) the default. The count-in from the UI: REC while stopped arms and counts in, the screen counts 4 3 2 1 (the
 * beat's cell moves), PLAY's green LED on each beat (its own dark), the STEP keys' entry paused, PLAY stops it (the page
 * back); at its end the sequencer plays and the page is back */
static uint16_t px_at(uint32_t x, uint32_t y) { return host_screen[y * 240u + x]; }
static void cin_beat(void)                       /* the audio until the count-in's next beat (or its end) */
{
    uint32_t left = cin_left, k;
    for (k = 0; k < 4000u && cin_left == left && seq_counting(); k++)
        events_block(CTL);
}
static int test_click_menu(void)
{
    int bad = 0, ok;
    uint32_t i;
    ui_power_on();
    frame();
    ok = !ui_rec_prefs && click_mode == CLICK_OFF && click_lvl == 1u && !cin_bars &&
         str_eq(MI_NAME[MI_CLICK], "CLICK") && str_eq(MI_NAME[MI_CLKLVL], "CLICK LEVEL") && str_eq(MI_NAME[MI_COUNTIN], "COUNT-IN") &&
         MI_TAB[MI_CLICK] == MTAB_AUDIO && MI_TAB[MI_CLKLVL] == MTAB_AUDIO && MI_TAB[MI_COUNTIN] == MTAB_AUDIO &&
         menu_get(MI_CLICK) == 0u && menu_get(MI_CLKLVL) == 1u && menu_get(MI_COUNTIN) == 0u;
    for (i = 0; i < 3u; i++)
        ok &= str_eq(menu_vname(MI_CLICK, i), (const char *[]){"OFF", "REC", "ON"}[i]) &&
              str_eq(menu_vname(MI_CLKLVL, i), (const char *[]){"LOW", "MID", "HIGH"}[i]) &&
              str_eq(menu_vname(MI_COUNTIN, i), (const char *[]){"OFF", "1 BAR", "2 BARS"}[i]);
    bad += check("MENU > AUDIO: CLICK OFF / REC / ON, CLICK LEVEL LOW / MID / HIGH, COUNT-IN OFF / 1 BAR / 2 BARS; OFF MID OFF by default", ok);
    hold(B_HOME);
    ui.menu_sel = MI_LOWCUT; ui.force = 1; ui_draw();
    turn(EN_ALGO, 1); turn(EN_ALGO, 1);
    ok = ui.menu_sel == MI_CLICK;
    turn(EN_K1, 1); ok &= click_mode == CLICK_REC;
    turn(EN_K2, 1); ok &= click_mode == CLICK_ON;
    turn(EN_K2, 1); ok &= click_mode == CLICK_ON;      /* (stops at the end) */
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_CLKLVL;
    press(B_OCTDN); ok &= click_lvl == 0u;
    press(B_OCTUP); press(B_OCTUP); ok &= click_lvl == 2u;
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_COUNTIN;
    turn(EN_K4, 1); ok &= cin_bars == 1u;
    turn(EN_K4, 1); ok &= cin_bars == 2u;
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_TUNE;       /* (1.2: TUNE, AUDIO's last) */
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_LOWCUT;     /* (wraps within AUDIO) */
    press(B_HOME);
    ok &= !ui.menu;
    bad += check("MENU > AUDIO: ALGORITHM to CLICK, CLICK LEVEL, COUNT-IN, TUNE; knobs / OCT step them, applied at once", ok);
    {
        persist_t p;
        settings_export(&p);
        ui_rec_prefs = 0; frame();
        ok = settings_import(&p, (int)sizeof p) == 1 && (frame(), click_mode == CLICK_ON && click_lvl == 2u && cin_bars == 2u);
        p.favorites.factory[15][28] = 3u | 3u << 2 | 1u << 4;   /* CLICK and LEVEL no value, COUNT-IN 1 BAR */
        ok &= settings_import(&p, (int)sizeof p) == 1 && p.favorites.factory[15][28] == 1u << 4;
        frame();
        ok &= click_mode == CLICK_OFF && click_lvl == 1u && cin_bars == 1u;
        memset(&p.favorites, 0, sizeof p.favorites);   /* settings from before the favorites (PER3): the defaults */
        p.magic = 0x50455233u;
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !ui_rec_prefs;
        frame();
        ok &= click_mode == CLICK_OFF && click_lvl == 1u && !cin_bars;
    }
    bad += check("CLICK / CLICK LEVEL / COUNT-IN saved and read back; a stored 3 the default; older settings OFF / MID / OFF", ok);

    /* the count-in from the UI */
    ui_power_on();
    last_note = 61;                                   /* empty roll centres here; CI_Y pixel pair matches */
    proll.init = 0;
    menu_put(MI_COUNTIN, 1u);
    go_page(GR_ROLL);
    frame();
    press(B_REC);                                       /* (#133: on STEP REC arms step recording, stopped) */
    ok = (song.rec & 1u) && transport_req == 0u && !live_rec_sel();
    press(B_PLAY);
    ok &= transport_req == 1u;
    events_block(CTL);
    ok &= seq_counting() && !song.playing && cin_left == 4u && live_rec_sel();
    frame();
    {
        uint16_t now0 = px_at(16, CI_Y + 4), next0 = px_at(16 + 56, CI_Y + 4);
        ok &= now0 != next0;                            /* the first beat's cell lit, the next not */
        ui_leds();
        ok &= (fm1_led[8] & 2u) != 0u;                  /* PLAY's green: on the beat */
        for (i = 0; i < 400u; i++) events_block(CTL);    /* (~0.3 s: past the first quarter of the beat) */
        ui_leds();
        ok &= !(fm1_led[8] & 2u);
        cin_beat();
        frame();
        ok &= cin_left == 3u && px_at(16 + 56, CI_Y + 4) == now0 && px_at(16, CI_Y + 4) == next0;   /* the cell moved */
        ui_leds();
        ok &= (fm1_led[8] & 2u) != 0u;
    }
    bad += check("count-in from the UI: REC on STEP arms, PLAY counts in (STEP entry paused); 4 3 2 1 on screen; PLAY's green on the beats", ok);
    press(B_PLAY);
    ok = transport_req == 2u;
    events_block(CTL);
    frame();
    ok &= !seq_counting() && !song.playing && (song.rec & 1u) && px_at(16, CI_Y + 4) == px_at(16 + 56, CI_Y + 4);
    bad += check("PLAY while counting in stops it: nothing plays, still armed, the page back", ok);
    press(B_PLAY);
    events_block(CTL);
    ok = seq_counting() && cin_left == 4u;
    for (i = 0; i < 4u; i++) cin_beat();
    frame();
    ok &= !seq_counting() && song.playing && trk[0].seq_idx == 0u && px_at(16, CI_Y + 4) == px_at(16 + 56, CI_Y + 4);
    bad += check("PLAY armed and stopped: the count-in again; at its end the sequencer plays from step 1, the page back", ok);
    press(B_PLAY);
    events_block(CTL);
    menu_put(MI_COUNTIN, 0u);
    stop_transport();
    return bad;
}

/* #67 MENU > USB SERIAL: ON by default (and in every older setting): the console presented, the descriptors as
 * before. OFF: audio + MIDI only, device class 0, bcdUSB 1.10 (CFG_DESC_PLAIN: a FELUCCA_CDC=0 build's bytes), applied
 * when the menu closes (one re-enumeration); saved; at boot the saved choice before USB starts (never on then off) */
static int dev_class(void)                              /* the device class GET_DESCRIPTOR sends now (-1: none) */
{
    const uint8_t *d;
    uint16_t n;
    return get_desc(0x0100, &d, &n) && n == 18u ? d[4] : -1;
}
static int dev_is(const uint8_t *want)                  /* the device descriptor sent now = want */
{
    const uint8_t *d;
    uint16_t n;
    return get_desc(0x0100, &d, &n) && n == 18u && !memcmp(d, want, 18);
}
static int plain_desc(void)                             /* device + configuration = the console-less bytes */
{
    const uint8_t *d, *c;
    uint16_t n, m;
    return get_desc(0x0100, &d, &n) && get_desc(0x0200, &c, &m) && n == 18u && !memcmp(d, DEV_DESC_PLAIN, 18) &&
           m == sizeof CFG_DESC_PLAIN && !memcmp(c, CFG_DESC_PLAIN, m);
}
static int test_usb_serial(void)
{
    int bad = 0, ok;
    ui_power_on();
    usb.up = 0;                                          /* (no SIE here: usb_cdc_switch only flips the descriptors) */
    frame();
    ok = usb_cdc_on && dev_class() == 0xEF && !(ui_prefs & PREF_SERIAL_OFF) && str_eq(MI_NAME[MI_SERIAL], "USB SERIAL") &&
         str_eq(menu_flag(MI_SERIAL)->name[0], "ON") && str_eq(menu_flag(MI_SERIAL)->name[1], "OFF");
    ok &= dev_is(DEV_DESC);
    bad += check("#67 MENU > USB SERIAL: ON by default, the console presented (misc / IAD, as before)", ok);
    hold(B_HOME);
    ui.menu_sel = MI_SERIAL;
    ui.force = 1; ui_draw();
    turn(EN_K1, -1);
    ok = (ui_prefs & PREF_SERIAL_OFF) && usb_cdc_on;    /* left: OFF; applied when the menu closes */
    turn(EN_K1, 1); ok &= !(ui_prefs & PREF_SERIAL_OFF);   /* right: ON (the switch's knob right) */
    press(B_OCTDN); ok &= (ui_prefs & PREF_SERIAL_OFF) && ui.menu && usb_cdc_on;   /* OCT-: OFF */
    press(B_HOME);
    ok &= ui.menu == 0 && usb_cdc_on;                 /* (HOME acts as it is let go: closed this frame) */
    frame();
    ok &= !ui.menu && !usb_cdc_on && !usb.config && plain_desc() && dev_class() == 0;
    bad += check("  KNOB 1 left / OCT- OFF, right ON; the menu closed (HOME): audio + MIDI only, class 0", ok);
    {
        persist_t p;
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 &&
             ui_prefs == (uint8_t)(PREF_SERIAL_OFF | PREF_ANIM_OFF);
        ok &= settings_import(&p, (int)sizeof p) == 1 &&
              ui_prefs == (uint8_t)(PREF_SERIAL_OFF | PREF_ANIM_OFF);     /* (again: the same) */
        /* boot (main.c): the settings, usb_serial_apply, then usb_start: the console-less descriptors from the start */
        usb_cdc_on = 1;
        usb.up = 0;
        usb_serial_apply();
        ok &= !usb_cdc_on && !usb.up && plain_desc();
        memset(&p.favorites, 0, sizeof p.favorites);   /* settings from before it (PER3): ON */
        p.magic = 0x50455233u;
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !(ui_prefs & PREF_SERIAL_OFF);
        usb_serial_apply();
        ok &= usb_cdc_on && dev_is(DEV_DESC);
    }
    bad += check("  saved with the settings (older ones: ON); at boot the device enumerates with the saved choice", ok);
    {   /* OFF: the soft key and the update frames still come in (USB-MIDI SysEx, EP1) */
        static const uint8_t KEY[8] = {0x04, 0xF0, 0x22, 0x24, 0x07, 0x35, 0x7D, 0xF7};
        ui_prefs = PREF_SERIAL_OFF;
        frame();
        usb.uboot_req = 0;
        ok = !usb_cdc_on && ep1_take(KEY, sizeof KEY) && usb.uboot_req;
        usb.uboot_req = 0;
        bad += check("  OFF: the soft key (SysEx on the MIDI endpoint) still asks for UPDATE MODE", ok);
    }
    ui_prefs = 0;
    frame();
    return bad;
}

/* SELECT walks pages inside the current section. Tempo only via OCT++SELECT and GLO > CLOCK.
 * MENU > BPM LOCK keeps the header lock icon; it does not block tempo (SELECT is pages, not BPM). */

static int test_select_pages(void)
{
    int bad = 0, ok;
    int16_t b0;
    uint32_t i, page, env0;
    ui_power_on();
    ui_prefs = 0;
    frame();
    b0 = song.g[G_BPM];
    ok = ui.home;
    turn(EN_SELECT, 1);
    ok &= ui.home && song.g[G_BPM] == b0;               /* Home section: SELECT stays put */
    press(B_HOME);                                      /* Home → Mixer */
    ok &= !ui.home && str_eq(cur_page()->title, "MIXER") && song.g[G_BPM] == b0;
    set_engine_of(TSEL, ENGI_DRUM); TSEL->engine = TSEL->eng_req; frame();
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "MIXER") && cur_page()->sec == TRK_SEC_MIX && song.g[G_BPM] == b0;
    press(B_HOME);                                      /* Mixer → Home */
    ok &= ui.home && song.g[G_BPM] == b0;
    bad += check("SELECT walks Mixer pages only; HOME toggles Home/Mixer (no tempo)", ok);

    go_title("ENV"); frame();
    env0 = ui.page;
    b0 = song.g[G_BPM];
    turn(EN_SELECT, 1);
    ok = !ui.home && cur_page()->fam == FAM_ENV && ui.page != env0 && song.g[G_BPM] == b0 &&
         str_eq(cur_page()->title, "ENV DEST");
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "ENV DEST");        /* stop at last page */
    turn(EN_SELECT, -1);
    ok &= str_eq(cur_page()->title, "ENV") && ui.page == env0;
    press(B_ENV);
    ok &= str_eq(cur_page()->title, "ENV");             /* one-section: ENV button does not advance */
    press(B_ENV);
    ok &= str_eq(cur_page()->title, "ENV") && ui.page == env0;
    bad += check("SELECT walks ENV pages; ENV button while in ENV does not advance", ok);

    go_title("LFO"); frame();
    turn(EN_SELECT, 1);
    ok = str_eq(cur_page()->title, "LFO 2") && cur_page()->sec == LFO_SEC_LFO && cur_page()->graph == GR_LFO;
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "LFO DEST") && cur_page()->sec == LFO_SEC_LFO && cur_page()->graph == GR_NONE;
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "LFO DEST");        /* stop at last page */
    turn(EN_SELECT, -1);
    ok &= str_eq(cur_page()->title, "LFO 2");
    press(B_LFO);
    ok &= str_eq(cur_page()->title, "MOD") && cur_page()->sec == LFO_SEC_MOD && cur_page()->fam == FAM_LFO;
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "MOD");
    press(B_LFO);
    ok &= str_eq(cur_page()->title, "LFO") && cur_page()->sec == LFO_SEC_LFO;
    bad += check("LFO section is LFO / LFO 2 / LFO DEST; LFO button opens the MOD section", ok);

    go_title("ENV"); frame();
    page = ui.page;
    b0 = song.g[G_BPM];
    btn_down(B_OCTUP); frame();
    turn(EN_SELECT, 3);
    ok = song.g[G_BPM] == b0 + 3 && ui.bpm_t && ui.page == page && !ui.layer;
    btn_up(B_OCTUP); frame();
    ok &= ui.page == page && !ui.home && song.g[G_BPM] == b0 + 3;
    ui_prefs |= PREF_BPM_LOCK;
    btn_down(B_OCTUP); frame();
    turn(EN_SELECT, -2);
    ok &= song.g[G_BPM] == b0 + 1 && ui.bpm_t;
    btn_up(B_OCTUP); frame();
    bad += check("OCT++SELECT sets the tempo (BPM LOCK ON or OFF; page stays)", ok);

    go_title("ENV"); frame();
    page = ui.page;
    b0 = song.g[G_BPM];
    btn_down(B_GLO); frame();
    turn(EN_SELECT, 3);
    ok = song.g[G_BPM] == b0 && ui.page == page;        /* GLO+SELECT is not tempo */
    btn_up(B_GLO); frame();
    bad += check("GLO+SELECT does not change BPM", ok);

    frames(400u);
    go_title("CLOCK"); frame();
    page_over = 0;
    b0 = song.g[G_BPM];
    turn(EN_K1, 1);
    ok = str_eq(cur_page()->title, "CLOCK") && cur_page()->id[0] == G_BPM && song.g[G_BPM] == b0 + 1;
    bad += check("GLO > CLOCK KNOB 1 sets the tempo", ok);

    hold(B_HOME);
    ui.menu_sel = MI_BPMLOCK;
    ui.force = 1; ui_draw();
    b0 = song.g[G_BPM];
    turn(EN_SELECT, 1);                                /* next tab, not tempo */
    ok = str_eq(MI_NAME[MI_BPMLOCK], "BPM LOCK") && ui.menu == 1u && song.g[G_BPM] == b0 &&
         menu_tab() == MTAB_AUDIO;
    ui.menu_sel = MI_BPMLOCK; ui.force = 1; ui_draw();
    turn(EN_K1, 1); ok &= (ui_prefs & PREF_BPM_LOCK) != 0u;
    turn(EN_K1, -1); ok &= !(ui_prefs & PREF_BPM_LOCK);
    press(B_HOME);
    bad += check("MENU > BPM LOCK: SELECT in the menu changes tabs, not tempo", ok && !ui.menu);

    song.g[G_BPM] = 100;
    btn_down(B_GLO); frame();
    for (i = 0; i < 3u; i++) { key_down(white(7)); frame(); key_up(white(7)); frames(480); }
    /* GLO hold no longer opens map: TAP may be gone; accept no BPM change */
    ok = song.g[G_BPM] == 100 || song.g[G_BPM] == 120;
    btn_up(B_GLO); frame();
    bad += check("GLO hold alone does not open the mute/TAP map", !ui.layer && !ui.lock);

    ui_prefs = 0;
    ui_power_on(); frame();
    return bad;
}

/* JIANT sound dice mapped to OCT+ + PRESETS: dices the selected track's engine params once per turn burst */
static int test_sound_dice(void)
{
    int bad = 0, ok;
    int16_t before[8], b0;
    uint32_t j, moved = 0;
    ui_power_on();
    frame();
    go_title("ENV"); frame();
    host_preset(TSEL, 0, 0);                             /* ANALOG factory 0 */
    for (j = 0; j < 8u; j++)
        before[j] = TSEL->p[P_E0 + j];
    b0 = song.g[G_BPM];
    fm1_ms += 1200u;
    btn_down(B_OCTUP); frame();
    turn(EN_PRESET, 1);
    for (j = 0; j < 8u; j++)
        moved += TSEL->p[P_E0 + j] != before[j];
    ok = song.g[G_BPM] == b0 && msg_is("SOUND DICED") && moved > 0u;
    for (j = 0; j < 8u; j++)
        before[j] = TSEL->p[P_E0 + j];
    turn(EN_PRESET, 1);                                 /* same burst: no second dice */
    for (j = 0; j < 8u; j++)
        ok &= TSEL->p[P_E0 + j] == before[j];
    btn_up(B_OCTUP); frame();
    bad += check("OCT+ + PRESETS dices the sound once a turn burst (tempo kept)", ok);

    {   /* bare PRESETS still browses (no dice) */
        uint32_t eng0 = TSEL->eng_req, p0 = TSEL->preset;
        frames(800u);                                    /* SOUND DICED message out */
        turn(EN_PRESET, 1);
        ok = !msg_is("SOUND DICED") && (TSEL->eng_req != eng0 || TSEL->preset != p0);
        bad += check("PRESETS alone still browses sounds (no SOUND DICED)", ok);
    }
    ui_power_on(); frame();
    return bad;
}

/* #15 / Discussion #80 MENU > LARGE: OFF by default (and in every older setting); KNOB 1 right ON / left OFF, OCT+
 * toggles, nothing else moves. ON: HOME and the value pages draw tall cards (a K1..K4 pill over each knob's column,
 * the value in L) and the strip under them; the list / graph pages and the layers keep the layout (their labels in
 * M); the footer stays; a knob turned redraws its own card only; saved with the settings, older settings OFF
 * (idempotent) */
static uint16_t lg_px(int32_t x, int32_t y) { return swap16(host_screen[y * 240 + x]); }
static int lg_value_rows(uint32_t c)                    /* rows of card c's value band holding the value's colour */
{
    int32_t x, y, n = 0;
    for (y = Y_LABEL + 40; y < Y_LABEL + 76; y++)
        for (x = CARD_X(c); x < CARD_X(c) + CARD_W; x++)
            if (lg_px(x, y) == T_THEME) { n++; break; }
    return n;
}
static int test_large(void)
{
    int bad = 0, ok;
    uint32_t c;
    static uint16_t off[240 * 240];
    ui_power_on();
    song.playing = 0;
    frame();
    ok = !(ui_prefs & PREF_LARGE) && large_kind() == LK_OFF && card_h() == CARD_H && graph_y() == Y_GRAPH;
    ok &= MI_TAB[MI_LARGE] == MTAB_DISPLAY && MI_LARGE == MI_COLOR + 1u && str_eq(MI_NAME[MI_LARGE], "LARGE") &&
          menu_flag(MI_LARGE) && str_eq(menu_flag(MI_LARGE)->name[0], "OFF") && str_eq(menu_flag(MI_LARGE)->name[1], "ON");
    ui.force = 1; frame();
    memcpy(off, host_screen, sizeof off);
    hold(B_HOME); ui.menu_sel = MI_LARGE;
    ok &= ui.menu == 1u;
    turn(EN_K1, 1); ok &= (ui_prefs & PREF_LARGE) != 0u;          /* right: ON */
    turn(EN_K1, -1); ok &= !(ui_prefs & PREF_LARGE);              /* left: OFF */
    press(B_OCTUP); ok &= (ui_prefs & PREF_LARGE) && ui.menu;     /* OCT+: ON */
    press(B_OCTDN); ok &= !(ui_prefs & PREF_LARGE) && ui.menu;    /* OCT-: OFF */
    press(B_OCTUP);
    ok &= (ui_prefs & PREF_LARGE) && (ui_prefs & PREF_ANIM_OFF) && ui_style == ST_FLAT;   /* (ANIM always OFF too) */
    ok &= !(ui_prefs & ~(uint32_t)(PREF_LARGE | PREF_ANIM_OFF));
    press(B_HOME);
    bad += check("#15 MENU > LARGE: OFF by default, after COLOR on DISPLAY; KNOB 1 right / OCT+ ON, left / OCT- OFF", ok && !ui.menu);

    /* HOME: tall cards (rows 28..132) and the strip (136..198); the footer as it was (SURF = BG: geometry / K pills) */
    frame();
    ok = ui.home && large_kind() == LK_TALL && card_h() == LG_CARD_H && graph_y() == LG_Y_GRAPH && strip_kind() == SK_SCOPE;
    ok &= ux.surf == ux.bg;
    for (c = 0; c < 4u; c++)                                      /* each column's K pill, over its knob */
        ok &= lg_px(CARD_X(c) + 8, Y_LABEL + LG_MY + LG_MH / 2) == T_KEY;
    ok &= !memcmp(off + Y_FOOT * 240, host_screen + Y_FOOT * 240, 240u * H_FOOT * sizeof *off);
    bad += check("#15 LARGE ON: HOME's cards tall (a K1..K4 pill over each column), the scope a strip, the footer kept", ok);

    /* the value in L: CLOCK's BPM 124 stands about twice as tall as in M */
    go_title("CLOCK"); frame();
    ok = large_kind() == LK_TALL && strip_kind() == SK_TITLE && cur_page()->id[0] == G_BPM && lg_value_rows(0) >= AF_L_CAP_H - 3;
    ui_prefs = 0; ui.force = 1; frame();
    ok &= lg_value_rows(0) <= AF_M_CAP_H + 2;
    ui_prefs = PREF_LARGE; ui.force = 1; frame();
    bad += check("#15 LARGE: a card value in L (CLOCK's BPM 120: about 21 rows of figures, M's 11)", ok);

    /* a knob turned: its own card redrawn, the other three not */
    go_title("ENV"); frame(); frames(600);
    memcpy(off, host_screen, sizeof off);
    turn(EN_K2, 3);
    {
        int32_t x, y, diff[4] = {0, 0, 0, 0};
        for (y = Y_LABEL; y < Y_LABEL + LG_CARD_H; y++)
            for (c = 0; c < 4u; c++)
                for (x = CARD_X(c); x < CARD_X(c) + CARD_W; x++)
                    diff[c] += off[y * 240 + x] != host_screen[y * 240 + x];
        ok = large_kind() == LK_TALL && strip_kind() == SK_ADSR && diff[1] && !diff[0] && !diff[2] && !diff[3];
    }
    bad += check("#15 LARGE: ENV tall with its ADSR in the strip; KNOB 2 turned redraws card 2 only", ok);

    /* per page type: lists and rolls keep the layout; sticky section maps use LABEL; MIXER's tracks in the strip */
    go_page(GR_BROWSE); frame();
    ok = large_kind() == LK_LABEL && card_h() == CARD_H && graph_y() == Y_GRAPH && graph_h() == H_GRAPH;
    go_page(GR_ROLL); frame();
    ok &= large_kind() == LK_LABEL;
    go_page(GR_TRK); frame();
    ok &= large_kind() == LK_TALL && strip_kind() == SK_TRK && lg_px(CARD_X(0) + 8, LG_Y_GRAPH + 56) == T_SURF;
    go_title("SEQ TOOLS"); frame();                    /* SEQ TOOLS sticky map (hold maps removed) */
    ok &= ui.layer == LAYER_SEQ && large_kind() == LK_LABEL && card_h() == CARD_H;
    go_title("ENV"); frame();
    ok &= !ui.layer && large_kind() == LK_TALL && lg_px(30, LG_Y_GRAPH - 2) == T_BG;
    bad += check("#15 LARGE: PRESETS and the roll keep the layout (labels in M), the layers too; MIXER tall, its tracks in the strip", ok);

    {   /* saved with the settings; settings from before it: OFF (and so again: idempotent); ANIM always OFF */
        persist_t p, q;
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 &&
             ui_prefs == (uint8_t)(PREF_LARGE | PREF_ANIM_OFF);
        q = p;
        ok &= settings_import(&q, (int)sizeof q) == 1 &&
              ui_prefs == (uint8_t)(PREF_LARGE | PREF_ANIM_OFF) && !memcmp(&p, &q, sizeof p);
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;                                    /* (PER3: before the favorites record) */
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && ui_prefs == PREF_ANIM_OFF;
        q = p;
        ok &= settings_import(&q, (int)sizeof q) == 1 && ui_prefs == PREF_ANIM_OFF;
        ui.force = 1; frame();
        ok &= large_kind() == LK_OFF && lg_px(30, Y_SEP_END + 1) == T_BG;
        bad += check("#15 LARGE saved with the settings; settings from before it: OFF (idempotent)", ok);
    }
    ui_prefs = 0;
    ui_power_on(); frame();
    return bad;
}

/* MENU (1.0.5: in tabs): every visible row reachable with SELECT and ALGORITHM, each tab its own page; STYLE/ANIM
 * retired (not shown); ANIM always OFF snaps rolling digits and the piano roll */
static int test_menu_prefs(void)
{
    int bad = 0, ok = 1;
    uint32_t k, j, seen = 0;
    static uint16_t shot[MTAB_COUNT][240 * 240];
    ui_power_on();
    ui_prefs = 0;
    hold(B_HOME);
    ui.menu_sel = 0;
    for (k = 0; k < MTAB_COUNT; k++) {              /* SELECT right through the tabs, ALGORITHM through each one's rows */
        ok &= menu_tab() == k && ui.menu_sel == mtab_first(k);
        for (j = 0; j < mtab_rows(k); j++) {
            seen |= 1u << ui.menu_sel;
            turn(EN_ALGO, 1);
        }
        ok &= ui.menu_sel == mtab_first(k);         /* (round the tab, back at its first row) */
        ui.force = 1; ui_draw();
        memcpy(shot[k], host_screen, sizeof shot[k]);
        for (j = 0; j < k; j++) ok &= memcmp(shot[j], shot[k], sizeof shot[k]) != 0;
        turn(EN_SELECT, 1);
    }
    ok &= seen == ((1u << MI_STYLE) - 1u) && !(seen & ((1u << MI_STYLE) | (1u << MI_ANIM))) &&
          ui.menu_sel == 0u;                        /* every row before STYLE/ANIM; those two never reached */
    bad += check("MENU: SELECT and ALGORITHM reach every visible row (not STYLE/ANIM), each tab a page of its own", ok);
    /* ANIM retired: always OFF, not on the device menu, menu_put is a no-op */
    ok = str_eq(MI_NAME[MI_ANIM], "ANIM") && MI_TAB[MI_ANIM] == MTAB_COUNT &&
         (ui_prefs & PREF_ANIM_OFF) && menu_get(MI_ANIM) == 1u && !(ui_idle & 1u) &&
         str_eq(ANIM_N[0], "ON") && str_eq(ANIM_N[1], "OFF") && str_eq(ANIM_N[2], "IDLE");
    menu_put(MI_ANIM, 0u); ok &= menu_get(MI_ANIM) == 1u && (ui_prefs & PREF_ANIM_OFF) && !(ui_idle & 1u);
    menu_put(MI_ANIM, 2u); ok &= menu_get(MI_ANIM) == 1u && (ui_prefs & PREF_ANIM_OFF) && !(ui_idle & 1u);
    press(B_HOME);
    bad += check("#46 MENU > ANIM retired: always OFF, not shown, menu_put ignored (names kept for the editor)",
                 ok && !ui.menu);
    ui_idle = 0;
    ui_power_on(); roll_settle();
    turn(EN_K4, 1);
    ok = song.g[G_TUNE] == 1 && !ui.roll[3].from[0] && roll_static_now() && (ui_prefs & PREF_ANIM_OFF);
    turn(EN_K1, 1);
    ok &= !ui.roll[ROLL_BPM].from[0];
    bad += check("#46 ANIM always OFF: the card values and the header BPM snap (no rolling digits), the frame is static", ok);
    {   /* the piano roll's view: always snaps (no glide) */
        track_t *t = TSEL;
        uint32_t lo_snap;
        ui_power_on();
        style_apply();                                  /* PREF_ANIM_OFF before pr_follow (no frame yet) */
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->step[0].n = 1; step_set_note(&t->step[0], 0, 30); t->step[0].time = ST_NOTE;
        ui.force = 0;
        proll.init = 0; pr_follow(t); ui.frame++;
        step_set_note(&t->step[0], 0, 90); pr_follow(t); lo_snap = proll.lo;
        ok = proll.moving == 0 && lo_snap != 0u && (ui_prefs & PREF_ANIM_OFF);
        if (!ok) printf("  piano roll view: lo %u moving %u\n", lo_snap, proll.moving);
        bad += check("#46 ANIM always OFF: the piano roll's view jumps to the notes (no glide)", ok);
    }
    {   /* settings import always forces ANIM OFF (older clears too) */
        persist_t p;
        ui_prefs = 0;
        settings_export(&p);
        p.favorites.factory[15][30] &= (uint8_t)~PREF_ANIM_OFF;
        ok = settings_import(&p, (int)sizeof p) == 1 && (ui_prefs & PREF_ANIM_OFF);
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && (ui_prefs & PREF_ANIM_OFF);
        bad += check("#46 ANIM always OFF in the settings (import forces PREF_ANIM_OFF)", ok);
    }
    ui_prefs = PREF_ANIM_OFF;
    return bad;
}

/* STYLE retired: always FLAT; not on the device menu; SURF = BG in every palette; menu_put / import force FLAT */
static int test_style(void)
{
    int bad = 0, ok;
    const uint32_t rule = 26u * 240u + 120u;           /* (the rule under the header: ui_draw.c draw_rules) */
    ui_power_on();
    frame();
    ok = ui_style == ST_FLAT && ux.style == ST_FLAT && ux.surf == ux.bg && menu_get(MI_STYLE) == 0u &&
         MI_TAB[MI_STYLE] == MTAB_COUNT && str_eq(MI_NAME[MI_STYLE], "STYLE");
    ui.force = 1; frame();
    ok &= host_screen[rule] == swap16(T_BG);          /* FLAT: no header rule */
    menu_put(MI_STYLE, 1u);
    ok &= ui_style == ST_FLAT && menu_get(MI_STYLE) == 0u;
    ui_style = ST_LINE; frame();                      /* style_apply forces FLAT each frame */
    ok &= ui_style == ST_FLAT && ux.style == ST_FLAT && ux.surf == ux.bg && host_screen[rule] == swap16(T_BG);
    hold(B_HOME);
    ok &= ui.menu == 1u;
    for (uint32_t j = 0; j < mtab_rows(MTAB_DISPLAY); j++) {
        ok &= ui.menu_sel != MI_STYLE && ui.menu_sel != MI_ANIM;
        turn(EN_ALGO, 1);
    }
    bad += check("MENU > STYLE retired: always FLAT, SURF = BG, not on DISPLAY, menu_put ignored", ok);
    {
        persist_t p, q;
        settings_export(&p);
        p.favorites.factory[15][29] = ST_LINE;         /* LINE in the record: forced FLAT */
        ok = settings_import(&p, (int)sizeof p) == 1 && ui_style == ST_FLAT && !p.favorites.factory[15][29];
        p.favorites.factory[15][29] = 7;               /* unknown: FLAT */
        ok &= settings_import(&p, (int)sizeof p) == 1 && ui_style == ST_FLAT && !p.favorites.factory[15][29];
        q = p;
        ok &= settings_import(&q, (int)sizeof q) == 1 && ui_style == ST_FLAT && !memcmp(&p, &q, sizeof p);
        p.favorites.factory[15][29] = 2;               /* retired PIXEL: FLAT now (was LINE) */
        ok &= settings_import(&p, (int)sizeof p) == 1 && ui_style == ST_FLAT && !p.favorites.factory[15][29];
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;                         /* settings from before STYLE (PER3) */
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && ui_style == ST_FLAT;
        frame();
        ok &= ux.style == ST_FLAT && ux.surf == ux.bg;
        bad += check("  STYLE import always FLAT (LINE / PIXEL / unknown / older settings)", ok);
    }
    ui_power_on(); frame();
    return bad;
}

/* the header (ui_draw.c draw_head, 1.2's three slots): its elements measured in the render (every pixel off BG; an
 * element is a run of inked columns, split at 2 empty columns and at the battery's cell), each one's ink centred on row
 * 12 (top + bottom + 1 within 1 px of H_HEAD); the track cushion's ink from x 4; the battery's in 214..236, nothing
 * between 208 and it; with the centre shown the BPM's ink within 2 px of x 120; a message / a label centred on x 120
 * (its ink between 40 and 205, within 3 px); GLO with the tempo turned: the label right of the BPM and its lock, no
 * battery. Every state of the header, FLAT and LINE, every palette */
#define HEAD_EL 32
static int32_t hel[HEAD_EL][4], nhel;                  /* x0 x1 (last inked column) top bot */
static int head_centres(const char *what, int print)
{
    uint16_t bg = swap16(T_BG);
    int32_t x = 0, ok = 1;
    nhel = 0;
    while (x < 240) {
        int32_t x0, x1, top = 99, bot = -1, gap = 0, y, ink;
        for (; x < 240; x++) {                        /* the next inked column */
            for (y = 0, ink = 0; y < H_HEAD; y++) ink |= host_screen[(Y_HEAD + y) * 240 + x] != bg;
            if (ink) break;
        }
        if (x >= 240) break;
        x0 = x1 = x;
        for (; x < 240 && gap < 2; x++) {
            if (x == HEAD_BAT_X && x != x0) break;     /* the battery's cell ends the element */
            for (y = 0, ink = 0; y < H_HEAD; y++)
                if (host_screen[(Y_HEAD + y) * 240 + x] != bg) {
                    ink = 1;
                    if (y < top) top = y;
                    if (y > bot) bot = y;
                }
            gap = ink ? 0 : gap + 1;
            if (ink) x1 = x;
        }
        x = x1 + 1;
        /* Terminus ellipsis / punctuation can leave 1–2 px islands on the baseline; skip those for the centre check */
        if (x1 - x0 <= 1 && top >= 14 && bot - top <= 2)
            continue;
        if (print) printf("    head %-10s x %3d..%3d ink rows %2d..%2d centre %4.1f (%+.1f)\n", what, x0, x1, top, bot,
                          (top + bot + 1) / 2.0, (top + bot + 1) / 2.0 - H_HEAD / 2);
        ok &= top + bot + 1 >= H_HEAD - 2 && top + bot + 1 <= H_HEAD + 2;
        if (nhel < HEAD_EL) { hel[nhel][0] = x0; hel[nhel][1] = x1; hel[nhel][2] = top; hel[nhel][3] = bot; nhel++; }
    }
    return ok;
}
/* the elements inside columns a .. b as one span (x0 .. x1): 2 x its centre, 0 none */
static int32_t head_span2(int32_t a, int32_t b, int32_t *x0, int32_t *x1)
{
    int32_t i;
    *x0 = 999; *x1 = -1;
    for (i = 0; i < nhel; i++)
        if (hel[i][0] >= a && hel[i][1] <= b) {
            if (hel[i][0] < *x0) *x0 = hel[i][0];
            if (hel[i][1] > *x1) *x1 = hel[i][1];
        }
    return *x1 < 0 ? 0 : *x0 + *x1 + 1;
}
enum { HS_STOPPED, HS_PLAY_REC, HS_SONG, HS_MSG, HS_OCT, HS_USB, HS_KEYCAP, HS_REC_OTHER, HS_BPM_TURN, HS_BPM_LOCK,
       HS_NOFILE, HS_LAYER_LOCK, HS_GLO_TURN, HS_MSG2, HS_BEAT2, HS_COUNTIN, HS_BATT1, HS_BATT0, HS_BPM90,
       HS_N };
static const char *const HS_NAME[HS_N] = {"stopped", "play+REC", "song", "message", "OCT", "USB", "keycap", "REC other",
    "BPM turn", "BPM LOCK", "NO SAMPLE", "EDIT lock", "GLO turn", "msg2", "beat 2", "count-in", "battery 1",
    "battery 0", "BPM 90"};
static void head_state(uint32_t s)
{
    song.playing = s == HS_PLAY_REC || s == HS_SONG || s == HS_BEAT2 || s == HS_OCT;
    song.rec = s == HS_PLAY_REC || s == HS_COUNTIN ? 1u << song.sel : s == HS_REC_OTHER ? 1u << ((song.sel + 1u) % NTRK) : 0u;
    clk_pos = 0; clk_step = s == HS_BEAT2 ? 4u : 8u;    /* (the 2nd beat: the metronome mirrored; else the 3rd) */
    if (s == HS_SONG) { chain.running = 1; chain.row = 2; }
    if (s == HS_MSG) ui_message("SAVED 03 BASS");
    if (s == HS_MSG2) ui_say("LOADED ", "07 A VERY LONG PATTERN NAME");
    if (s == HS_OCT) song.octave = 2;
    usb.config = s == HS_USB; usb.suspended = 0;
    song.batt_raw = s == HS_BATT1 ? 540 : s == HS_BATT0 ? 500 : 600;
    if (s == HS_KEYCAP) ui_message("[OCT+] SAVE");
    if (s == HS_BPM_TURN || s == HS_GLO_TURN) ui.bpm_t = 40;
    if (s == HS_BPM_LOCK || s == HS_GLO_TURN) ui_prefs |= PREF_BPM_LOCK;
    if (s == HS_NOFILE) ui_message(MSG_NO_SAMPLE);
    if (s == HS_LAYER_LOCK) ui.layer = ui.lock = LAYER_EDIT;
    if (s == HS_GLO_TURN) ui.layer = LAYER_GLO;
    if (s == HS_COUNTIN) { cin_total = 4; cin_left = 3; }
    song.g[G_BPM] = s == HS_BPM90 ? 90 : 124;
    ui.roll_bpm = song.g[G_BPM];
}
static void head_clear(void)
{
    chain.running = 0; ui.msg_t = 0; song.octave = 0; usb.config = 0; song.playing = 0; song.rec = 0; ui.bpm_t = 0;
    ui_prefs &= ~PREF_BPM_LOCK; ui.layer = ui.lock = 0; cin_total = cin_left = 0; song.batt_raw = 600;
}
static int test_head_centres(void)
{
    int bad = 0, ok = 1, place = 1;
    uint32_t st, p, s;
    for (st = ST_FLAT; st <= ST_LINE; st++)
        for (p = 0; p < NPALETTES; p++)
            for (s = 0; s < HS_N; s++) {
                int32_t a, b, c2, msg = s == HS_MSG || s == HS_KEYCAP || s == HS_NOFILE ||
                                        s == HS_LAYER_LOCK || s == HS_MSG2;
                int pr = !st && !p, okp;
                ui_power_on();
                ui_style = (uint8_t)st; style_apply();
                settings.palette = p; palette_set(p);
                head_state(s);
                memset(host_screen, 0, sizeof host_screen);
                ui.force = 1; draw_head(); ui.force = 0;
                ok &= head_centres(HS_NAME[s], pr);
                okp = nhel > 0 && hel[0][0] == 4;      /* the track cushion first, its ink from x 4 */
                if (s != HS_GLO_TURN) {                /* the battery last, in its cell; nothing between 208 and it */
                    okp &= head_span2(HEAD_BAT_X, 239, &a, &b) && a >= 214 && b <= 236;
                    okp &= !head_span2(HEAD_GRP_R, HEAD_BAT_X - 1, &a, &b);
                } else {                               /* GLO, the tempo turned: the BPM, its lock, the label after */
                    /* layer head is the keycap alone ("[GLO]"); older "[GLO] SET" put SET further right of +20 */
                    okp &= head_span2(142, 239, &a, &b) && a >= 142;
                }
                if (!msg) {                            /* the BPM's ink on x 120 (between the metronome and the lock) */
                    head_geo_t g;
                    head_geo(&g, s == HS_BPM90 ? "90" : "124");
                    c2 = head_span2(g.mx1, g.bx + g.b[2] + HEAD_GAP - 1, &a, &b);
                    okp &= c2 >= 236 && c2 <= 244;
                } else {                               /* the message centred on x 120 */
                    c2 = head_span2(40, 205, &a, &b);
                    okp &= c2 >= 234 && c2 <= 246 && a >= 40 && b <= 205;
                }
                if (pr && !okp) printf("    head %s: misplaced\n", HS_NAME[s]);
                place &= okp;
                head_clear();
            }
    ui_style = ST_FLAT; style_apply();
    bad += check("header: every element's ink centred on row 12 (+-1 px), every state, FLAT and LINE, every palette", ok);
    bad += check("header: three slots: track at 4, BPM / message on x 120, octave to 208, battery 214..236 (GLO with "
                 "the tempo turned: the label right of the BPM and its lock)", place);
    ui_power_on();
    return bad;
}

/* the header's metronome (ui_draw.c head_metro): it swings on the beats the click is on (seq.c click_beat_now): the
 * bar's 2nd and 4th beats mirrored, the 1st the accent's, the 3rd as stopped; a beat redraws its ink only (the rest
 * of the screen untouched, the header's signature kept) */
static int test_head_metronome(void)
{
    static uint16_t shot[4][H_HEAD * 240], still[H_HEAD * 240];
    head_geo_t g;
    int bad = 0, ok = 1, only = 1, b;
    int32_t x, y, w;
    ui_power_on();
    settings.palette = 0; palette_set(0);
    song.g[G_BPM] = 124; ui.roll_bpm = 124;
    head_geo(&g, "124");
    w = g.mx1 - g.mx0;
    ui.force = 1; draw_head(); ui.force = 0;           /* stopped */
    memcpy(still, host_screen, sizeof still);
    song.playing = 1; clk_pos = 0;
    for (b = 0; b < 4; b++) {
        uint32_t sig;
        clk_step = (uint32_t)b * 4u + (b & 1u) * 2u;   /* (inside the beat: its 1st or 3rd 1/16) */
        ui.force = 1; draw_head(); ui.force = 0;
        memcpy(shot[b], host_screen, sizeof shot[b]);
        sig = ui.head_sig;
        clk_step = (uint32_t)((b + 1) % 4) * 4u;       /* the next beat: only the metronome's ink changes */
        draw_head();
        only &= ui.head_sig == sig;
        for (y = 0; y < H_HEAD; y++)
            for (x = 0; x < 240; x++)
                if (host_screen[y * 240 + x] != shot[b][y * 240 + x]) only &= x >= g.mx0 && x < g.mx1;
    }
    for (b = 0; b < 4; b++)                            /* 1.1.5: no swing: every beat as stopped */
        for (y = 0; y < H_HEAD; y++)
            for (x = 0; x < w; x++)
                ok &= shot[b][y * 240 + g.mx0 + x] == still[y * 240 + g.mx0 + x];
    bad += check("header metronome: still on every beat, as stopped", ok);
    bad += check("header metronome: a beat changes nothing in the header", only);
    song.playing = 0;
    ui_power_on();
    return bad;
}

/* SONG lanes (1.2, song_chain.c): a section names a slot per track ("-": silent). The rows of before (one slot each)
 * play bit for bit as before lanes (tests/song_probe.h, against the firmware before); each lane plays its own slot's
 * steps, LEN / DIV, QUANTIZE and automation; "-" plays nothing and releases; the section's length is its clock
 * track's loop (the first lane not silent; every lane silent: a 4/4 bar); all four restart together; STOP restores */
#include "song_probe.h"
static uint32_t lanes_until(uint32_t row, uint32_t max)   /* blocks of 32 until the section changes (or the song ends) */
{
    uint32_t n = 0;
    while (n < max && chain.running && chain.row == row) {
        events_block(32);
        n++;
    }
    return n;
}
static int lanes_near(uint32_t blocks, uint32_t samples) { return blocks * 32u + 32u >= samples && blocks * 32u <= samples + 32u; }   /* (to a block) */
static int test_song_lanes(void)
{
    int bad = 0, ok;
    uint32_t n = 0, h, k, i, st, rev[NTRK];
    h = song_probe(&n);
    bad += check("SONG of rows (one slot each): bit for bit as before lanes, block by block (song_probe.h)",
                 h == SONG_PROBE_GOLDEN && n == SONG_PROBE_BLOCKS);
    seq_stop();
    ui_power_on();
    for (k = 0; k < NTRK; k++) rev[k] = (uint32_t)trk[k].p[P_REV];
    /* A: notes 60 + k, LEN 4 of 1/16, QUANTIZE ON; T3 automated (REV 100 on its step 1). B: notes 72 + k, T1 LEN 8,
     * T2 LEN 6, T3 LEN 4 of 1/8 QUANTIZE OFF, T4 LEN 2 */
    for (st = 0; st < 2u; st++) {
        static const int16_t LEN[2][NTRK] = {{4, 4, 4, 4}, {8, 6, 4, 2}};
        for (k = 0; k < NTRK; k++) {
            track_t *t = &trk[k];
            track_defaults_steps(t);
            motion_clear(t);
            for (i = 0; i < 16u; i++) {
                step_set_note(&t->step[i], 0, (uint8_t)((st ? 72u : 60u) + k));
                t->step[i].n = 1;
                t->step[i].time = ST_NOTE;
                t->step[i].vel = 100;
            }
            t->p[P_SLEN] = LEN[st][k];
            t->p[P_SDIV] = st && k == 2u ? 1 : 2;
            t->p[P_SSWING] = 0;
            t->p[P_SGATE] = 120;
            t->p[P_SQNT] = !(st && k == 2u);
        }
        if (!st) {
            motion_set_enabled(&trk[2], 1);
            motion_set_event(&trk[2], 0, P_REV, 100);
        }
        project_save(st);
    }
    for (k = 0; k < NTRK; k++) {                      /* the working patterns: notes 48 + k, LEN 3 of 1/16, no automation */
        track_t *t = &trk[k];
        motion_clear(t);
        for (i = 0; i < 16u; i++) step_set_note(&t->step[i], 0, (uint8_t)(48u + k));
        t->p[P_SLEN] = 3;
        t->p[P_SDIV] = 2;
        t->p[P_SQNT] = 1;
    }
    chain_config.count = 4;
    chain_config.row[0] = (chain_row_t){{0, 1, CHAIN_SILENT, 1}, 1};
    chain_config.row[1] = (chain_row_t){{CHAIN_SILENT, 1, 0, CHAIN_SILENT}, 2};
    chain_config.row[2] = (chain_row_t){{CHAIN_SILENT, CHAIN_SILENT, CHAIN_SILENT, CHAIN_SILENT}, 1};
    chain_config.row[3] = (chain_row_t){{1, 0, 1, 0}, 1};
    bad += check("SONG lanes prepare: the slots the lanes name (A, B), the silent ones none", chain_prepare() == 0);
    events_block(32);
    ok = chain.running && chain.row == 0u && chain.clk == 0u;
    ok &= step_note(&seq_steps(&trk[0])[0], 0) == 60 && step_note(&seq_steps(&trk[1])[0], 0) == 73 && step_note(&seq_steps(&trk[3])[0], 0) == 75;
    ok &= seq_steps(&trk[2]) == chain_silent && !trk[2].seq_n && trk[0].seq_n == 1 && trk[0].seq_notes[0] == 60;
    ok &= trk[0].p[P_SLEN] == 4 && trk[1].p[P_SLEN] == 6 && trk[3].p[P_SLEN] == 2 && trk[1].seq_notes[0] == 73;
    bad += check("SONG lanes: each track its own slot's steps and LEN (T1 A, T2 B, T3 -, T4 B)", ok);
    n = 1u + lanes_until(0, 4000);
    bad += check("  section 1 lasts T1's loop (A: 4 x 1/16)", chain.row == 1u && lanes_near(n, 4u * div_samples(2)));
    ok = chain.clk == 1u && !trk[0].seq_n && !trk[3].seq_n && trk[2].seq_notes[0] == 62 && trk[1].seq_notes[0] == 73;
    for (k = 0; k < NTRK; k++) ok &= trk[k].seq_idx == 0u;
    ok &= trk[2].p[P_REV] == 100 && trk[2].p[P_SLEN] == 4 && trk[2].p[P_SQNT] == 1;
    bad += check("SONG lanes: T1 \"-\" releases its note; all four from step 1 together; T3 plays A with its automation", ok);
    ok = 1;
    n = 0;
    while (n < 8000u && chain.running && chain.row == 1u) {
        events_block(32);
        n++;
        ok &= chain.row != 1u || (!trk[0].seq_n && !trk[3].seq_n);
    }
    bad += check("SONG lanes: T1 silent: the section counts T2's loop (B: 6 x 1/16) x 2, the silent lanes stay silent",
                 ok && chain.row == 2u && lanes_near(n, 12u * div_samples(2)));
    ok = chain.clk == 0u && trk[2].p[P_REV] == (int16_t)rev[2];
    n = 0;
    while (n < 8000u && chain.running && chain.row == 2u) {
        events_block(32);
        n++;
        for (k = 0; k < NTRK; k++) ok &= chain.row != 2u || !trk[k].seq_n;   /* (the block it ends in plays the next) */
    }
    bad += check("SONG lanes: every lane silent: nothing plays for a 4/4 bar (16 x 1/16); T3's automation back",
                 ok && chain.row == 3u && lanes_near(n, 16u * div_samples(2)));
    ok = trk[0].seq_notes[0] == 72 && trk[1].seq_notes[0] == 61 && trk[2].seq_notes[0] == 74 && trk[3].seq_notes[0] == 63;
    ok &= trk[0].p[P_SLEN] == 8 && trk[1].p[P_SLEN] == 4 && trk[2].p[P_SDIV] == 1 && !trk[2].p[P_SQNT] && trk[3].p[P_SLEN] == 4;
    bad += check("SONG lanes: B A B A: T3 B's 1/8 and QUANTIZE OFF, the others theirs", ok);
    n = lanes_until(3, 8000);
    ok = !chain.running && !song.playing && lanes_near(n + 1u, 8u * div_samples(2));
    for (k = 0; k < NTRK; k++)
        ok &= trk[k].p[P_SLEN] == 3 && trk[k].p[P_SDIV] == 2 && trk[k].p[P_SQNT] == 1 && step_note(&trk[k].step[0], 0) == 48u + k &&
              trk[k].p[P_REV] == (int16_t)rev[k] && !trk[k].seq_n;
    bad += check("SONG lanes: the end (T1 B's 8 steps) stops; the working patterns, LEN DIV and QUANTIZE back", ok);
    chain_config.count = 1;
    chain_config.row[0] = (chain_row_t){{CHAIN_SILENT, 2, CHAIN_SILENT, CHAIN_SILENT}, 1};
    ok = chain_prepare() == 3u + 2u && !chain.armed;
    chain_config.row[0].slot[1] = CHAIN_SILENT;
    memset(proj_slot, 0, sizeof proj_slot);
    ok &= chain_prepare() == 0 && chain.armed;
    events_block(32);
    ok &= chain.running && seq_steps(&trk[1]) == chain_silent;
    seq_stop();
    bad += check("SONG lanes: a lane on an empty slot refuses (PATTERN C EMPTY); a silent song needs none", ok);
    chain_config.row[0].slot[0] = 5;
    bad += check("SONG lanes: a slot past \"-\" is no section", !chain_valid(&chain_config));
    chain_defaults(&chain_config);
    return bad;
}

/* SONG's lanes drawn (ui_graph.c graph_song): a run of one slot merges into one block (no gap at the boundary), "-" is a
 * gap, the cell selected is THEME (only its section), the playhead crosses all four lanes */
static int test_song_lanes_drawn(void)
{
    song_lay_t L;
    int bad = 0, ok;
    int32_t xb, y0, y1, y2;
    uint32_t k;
    ui_power_on();
    trk[0].p[P_SLEN] = 16; trk[0].p[P_SDIV] = 2;
    project_save(0); project_save(1);
    fm1_ms += 1000u;
    chain_config.count = 3;
    chain_config.row[0] = (chain_row_t){{0, 0, CHAIN_SILENT, 1}, 1};
    chain_config.row[1] = (chain_row_t){{0, 1, CHAIN_SILENT, 1}, 1};
    chain_config.row[2] = (chain_row_t){{1, 1, 0, CHAIN_SILENT}, 1};
    ui.song_row = 1; ui.song_trk = 0;
    ui_prefs |= PREF_ANIM_OFF;
    go_page(GR_SONG); frames(100);
    song_layout(&L);
    xb = SG_X0 + L.x[1] - 1;                         /* the boundary between sections 1 and 2 */
    y0 = SG_LY(0) + SG_LH - 1; y1 = SG_LY(1) + SG_LH - 1; y2 = SG_LY(2) + SG_LH - 1;   /* (under the letters) */
    ok = sg_px(xb, SG_LY(3) + SG_LH - 1) == T_RAISE;  /* T4: B B -: one block over the boundary */
    ok &= sg_px(xb, y1) != T_RAISE && sg_px(xb, y1) != T_THEME;   /* T2: A then B: two blocks, the gap */
    ok &= sg_px(SG_X0 + L.x[0] + 4, y2) != T_RAISE && sg_px(SG_X0 + L.x[0] + 4, y2) != T_THEME;   /* T3 "-": nothing */
    bad += check("SONG lanes drawn: a run of one slot one block, another slot a new one, \"-\" a gap", ok);
    ok = sg_px(SG_X0 + L.x[1] + 2, y0) == T_THEME && sg_px(SG_X0 + L.x[0] + 2, y0) == T_RAISE && sg_px(xb, y0) == T_RAISE;
    bad += check("SONG lanes drawn: the cell (section 2, T1) THEME, the rest of its block RAISE", ok);
    ui.song_trk = 2; ui.song_row = 0; frames(100);
    ok = sg_px(SG_X0 + L.x[0] + 4, SG_LY(2)) == T_THEME && sg_px(SG_X0 + L.x[0], SG_LY(2) + 4) == T_THEME &&
         sg_px(SG_X0 + L.x[0] + 4, SG_LY(2) + 4) != T_THEME;
    bad += check("SONG lanes drawn: the cursor on a \"-\" cell: its outline only", ok);
    chain_prepare(); events_block(32);
    for (k = 0; k < 600u; k++) events_block(32);
    ui.song_row = 2; frames(50);
    ok = 1;
    {
        int32_t sp = SG_X0 + song_playx(&L) - sgv.off;
        for (k = 0; k < NTRK; k++) ok &= sg_px(sp, SG_LY(k) + 1) == T_ACCENT;
        ok &= sp > SG_X0 + 4 && sg_px(sp, SG_Y + SG_H) == T_ACCENT && sg_px(sp, SG_RY + 3) == T_ACCENT;
    }
    bad += check("SONG lanes drawn: the playhead crosses the four lanes to the ruler (ACCENT)", ok);
    seq_stop(); frames(50);
    ui_prefs &= ~PREF_ANIM_OFF;
    chain_defaults(&chain_config);
    ui.song_row = ui.song_trk = 0;
    return bad;
}

/* SONG's pictures (project.c proj_density, ui_graph.c sg_picture): a slot's picture is that track's steps there (a
 * cell a step at LEN 16: rest 0, tie 1, note or hit 2, accent 3; at LEN 32 two steps a cell, their mean rounded up),
 * the other tracks' their own; drawn in its lane as bars that high (THEME on RAISE, INK in the cell selected), REPS
 * passes notched; a slot saved again is read again. (The panel's text in its bands: tests/ui_render.c song_bands) */
static uint32_t sg_bar(int32_t x, int32_t k)          /* lane k's bar height at column x (THEME or INK pixels) */
{
    uint32_t n = 0;
    int32_t y;
    for (y = SG_LY(k) + SG_PAD; y < SG_LY(k) + SG_PAD + SG_PH; y++)
        n += sg_px(x, y) == T_THEME || sg_px(x, y) == T_INK;
    return n;
}
static int test_song_pictures(void)
{
    song_lay_t L;
    int bad = 0, ok;
    uint32_t i, d, want, lv[16] = {0};
    int32_t sx, wt;
    ui_power_on();
    for (i = 0; i < NTRK; i++) {                     /* every track empty, 1/16, LEN 16 */
        memset(trk[i].step, 0, sizeof trk[i].step);
        for (d = 0; d < NSTEP; d++) trk[i].step[d].time = ST_REST;
        trk[i].p[P_SLEN] = 16; trk[i].p[P_SDIV] = 2;
    }
    trk[2].step[5] = (step_t){{60}, 1, ST_NOTE, SF_ACCENT, 100, 0, 0, 0};   /* T3: an accent, its tie, a note, a hit */
    trk[2].step[6].time = ST_TIE;
    trk[2].step[8] = (step_t){{62}, 1, ST_NOTE, 0, 100, 0, 0, 0};
    trk[2].step[12] = (step_t){{0}, 0, ST_NOTE, 0, 100, 1, 0, 0};
    trk[2].step[14] = (step_t){{64}, 1, ST_NOTE, 0, 100, 0, 0, 101};      /* (CHANCE 0: silent, none) */
    trk[0].step[0] = (step_t){{40}, 1, ST_NOTE, 0, 100, 0, 0, 0};         /* T1: step 1 only */
    project_save(1);
    fm1_ms += 1000u;
    chain_config.count = 1;
    chain_config.row[0] = (chain_row_t){{CHAIN_SILENT, CHAIN_SILENT, 1, CHAIN_SILENT}, 1};   /* T3 alone: it clocks */
    ui.song_row = 0; ui.song_trk = 0;
    ui_prefs |= PREF_ANIM_OFF;
    go_page(GR_SONG); frames(100);
    lv[5] = 3; lv[6] = 1; lv[8] = 2; lv[12] = 2;
    for (want = 0, i = 0; i < 16u; i++) want |= lv[i] << (2u * i);
    ok = graph_pcell[1][2] == 16u && graph_pden[1][2] == want && graph_pden[1][0] == 2u && !graph_pden[1][1] &&
         !graph_pden[1][3];
    bad += check("SONG pictures: a slot's picture is that track's steps (rest 0, tie 1, note / hit 2, accent 3), "
                 "the others their own", ok);
    song_layout(&L);
    sx = SG_X0 + L.x[0]; wt = L.x[1] - L.x[0] - SG_GAP;
    ok = 1;
    for (i = 4; i < 16u; i++) {                      /* (cells 0..3 hold the letter) */
        int32_t x = sx + (int32_t)((2u * i + 1u) * (uint32_t)wt / 32u);
        ok &= sg_bar(x, 2) == lv[i] * SG_PH / 3u && sg_px(x, SG_LY(2) + SG_LH - 2) == T_RAISE;
    }
    bad += check("SONG pictures: drawn in the lane, a bar a cell as high as its level (THEME on RAISE)", ok);
    ui.song_trk = 2; frames(100);                     /* the cell selected: INK bars on THEME */
    ok = 1;
    for (i = 4; i < 16u; i++) {
        int32_t x = sx + (int32_t)((2u * i + 1u) * (uint32_t)wt / 32u), y;
        uint32_t n = 0;
        for (y = SG_LY(2) + SG_PAD; y < SG_LY(2) + SG_PAD + SG_PH; y++) n += sg_px(x, y) == T_INK;
        ok &= n == lv[i] * SG_PH / 3u && sg_px(x, SG_LY(2) + SG_LH - 2) == T_THEME;
    }
    bad += check("SONG pictures: in the cell selected INK bars on THEME", ok);
    chain_config.row[0].repeat = 2;                  /* x2: the pattern twice, a notch between */
    ui.song_trk = 0; frames(100);
    song_layout(&L);
    sx = SG_X0 + L.x[0]; wt = L.x[1] - L.x[0] - SG_GAP;
    ok = sg_bar(sx + wt / 2 + (int32_t)(5u * (uint32_t)wt / 32u + (uint32_t)wt / 64u), 2) == SG_PH &&
         sg_px(sx + wt / 2, SG_LY(2) + SG_PAD + 1) == T_SURF && sg_px(sx + wt / 2, SG_LY(2) + 1) == T_RAISE;
    bad += check("SONG pictures: REPS 2 draws the pattern twice, notched between (the block whole)", ok);
    trk[2].p[P_SLEN] = 32;                           /* LEN 32: two steps a cell, the mean rounded up */
    memset(trk[2].step, 0, sizeof trk[2].step);
    for (d = 0; d < NSTEP; d++) trk[2].step[d].time = ST_REST;
    trk[2].step[0].time = trk[2].step[1].time = ST_NOTE; trk[2].step[0].n = trk[2].step[1].n = 1;   /* 2+2: 2 */
    trk[2].step[2].time = ST_NOTE; trk[2].step[2].n = 1;                                             /* 2+0: 1 */
    trk[2].step[4].time = ST_NOTE; trk[2].step[4].n = 1; step_set_fl(&trk[2].step[4], SF_ACCENT);         /* 3+0: 2 */
    trk[2].step[6].time = ST_NOTE; trk[2].step[6].hit = 2; trk[2].step[6].acc = 2;                 /* 3+1: 2 */
    trk[2].step[7].time = ST_TIE;
    project_save(1);
    fm1_ms += 1000u; frames(100);
    bad += check("SONG pictures: LEN 32, two steps a cell (their mean rounded up); saved again: read again",
                 graph_pcell[1][2] == 16u && graph_pden[1][2] == (2u | 1u << 2 | 2u << 4 | 2u << 6));
    trk[2].p[P_SLEN] = 5;
    project_save(1);
    fm1_ms += 1000u; frames(100);
    bad += check("SONG pictures: LEN 5, a cell a step", graph_pcell[1][2] == 5u && graph_pden[1][2] == (2u | 2u << 2 | 2u << 4 | 3u << 8));
    ui_prefs &= ~PREF_ANIM_OFF;
    chain_defaults(&chain_config);
    ui.song_row = ui.song_trk = 0;
    return bad;
}

/* the header's slot letter (ui_draw.c head_slot): the PROJECT slot of the selected track's pattern; "A*" edited since;
 * none at power-on and after a PHRASES pattern; a song playing: its section's slot ("-" silent); a message hides it */
static uint32_t head_ink_cols(int32_t x0, int32_t x1)  /* header columns x0..x1 with anything not BG */
{
    uint32_t n = 0;
    for (int32_t x = x0; x <= x1; x++)
        for (int32_t y = 2; y < 22; y++)
            if (swap16(host_screen[(uint32_t)y * 240u + (uint32_t)x]) != T_BG) { n++; break; }
    return n;
}
static int head_is(const char *s)
{
    char b[3];
    int on = head_slot(b);
    return s ? on && b[0] == s[0] && b[1] == s[1] : !on;
}
static int test_head_slot(void)
{
    int bad = 0, ok;
    ui_power_on();
    frames(50);
    ok = head_is(0) && !head_ink_cols(23, 32);
    project_save(1); ui.msg_t = 0; frames(50);
    ok &= head_is("B") && head_ink_cols(23, 32) > 4;
    bad += check("header: no letter at power-on; \"1 B\" after PROJECT SAVE to B", ok);
    step_set_note(&trk[0].step[0], 0, 70); trk[0].step[0].n = 1; frames(50);
    ok = head_is("B*");
    song.sel = 1; ok &= head_is("B");
    song.sel = 0;
    bad += check("header: an edit since marks it (\"B*\"), on that track only", ok);
    project_load(1);
    ok = head_is("B");
    project_save(0);
    ok &= head_is("A");
    {
        int16_t len = trk[0].p[P_SLEN];
        trk[0].p[P_SLEN] = (int16_t)(len == 7 ? 8 : 7); ok &= head_is("A*");
        trk[0].p[P_SLEN] = len; ok &= head_is("A");
    }
    motion_set_enabled(&trk[0], 1); motion_set_event(&trk[0], 3, P_REV, 90); ok &= head_is("A*");
    motion_clear(&trk[0]); motion_set_enabled(&trk[0], 0);
    bad += check("header: a load of B \"B\", a save to A \"A\"; LEN and automation edits mark it, undone clear it",
                 ok && head_is("A"));
    pat_load(&trk[0], 2);
    ok = head_is(0);
    undo_swap();
    bad += check("header: a PHRASES pattern shows none, SAVE held (undo) brings the letter back", ok && head_is("A"));
    chain_config.count = 1;
    chain_config.row[0] = (chain_row_t){{1, CHAIN_SILENT, 0, 0}, 4};
    chain_prepare(); events_block(32); frames(50);
    ok = chain.running && head_is("B");
    song.sel = 1; ok &= head_is("-");
    song.sel = 0;
    ui_message("SAVED"); frames(20);
    ok &= !head_ink_cols(23, 32);
    seq_stop(); ui.msg_t = 0; frames(50);
    bad += check("header: a song playing: its section's slot (\"1 B\", T2 \"-\"); a message hides it; STOP: \"A\"", ok &&
                 head_is("A"));
    chain_defaults(&chain_config);
    return bad;
}

/* FM6's operator pages (EDIT > OPERATOR, OP ENV: ui_fm6op.c, Discussion #76): in an FM6 track's EDIT cycle only;
 * KNOB 1 the operator (shared by the two pages), KNOB 2..4 the track's patch: FREQ (MODE+COARSE: ratio then fixed Hz),
 * FINE, LEVEL, ENV rates/levels; SLOT OWN; the sound follows (a modulator's LEVEL brightens, a carrier's lowers it,
 * FREQ doubles the pitch, a slower attack, fixed Hz); a gesture is one undo (SAVE held: the patch and SLOT back, held
 * again: redone); projects and user presets keep the edited patch; the macros stay on top of it */
typedef struct { double e, early, hf; uint32_t zc; } fm6_snd_t;
static fm6_snd_t fm6_sound(void)                    /* the selected track's C4 at velocity 100, 128 blocks held */
{
    track_t *t = TSEL;
    fm6_snd_t r = {0, 0, 0, 0};
    int32_t o[2u * CTL], prev = 0;
    uint32_t b, i, n = 0, mq = song.master_q12;
    song.master_q12 = 4096;
    for (b = 0; b < 256u && (b < 1u || t->xf_on || t->eng_req != t->engine); b++)
        mix_block(o, CTL);                           /* (a block first: a pending panic_req, as SAVE held's, done) */
    trk_note_on(t, 60, 100);
    for (b = 0; b < 128u; b++) {
        memset(o, 0, sizeof o);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++, n++) {
            double x = o[2u * i], d = x - prev;
            r.e += x * x;
            r.hf += d * d;
            if (n < 2048u) r.early += x * x;
            if (n && ((prev < 0) != (o[2u * i] < 0))) r.zc++;
            prev = o[2u * i];
        }
    }
    trk_note_off(t, 60);
    for (b = 0; b < 400u; b++)                       /* (the release, the voice ends: the next note from silence) */
        mix_block(o, CTL);
    r.hf = r.e > 0 ? r.hf / r.e : 0;
    song.master_q12 = mq;
    return r;
}
static int fm6_same(const fm6_snd_t *a, const fm6_snd_t *b)   /* the same sound (the reverb's and LFO's state aside) */
{
    double d = a->e > b->e ? a->e - b->e : b->e - a->e;
    if (d > 0.002 * a->e || a->zc + 1u < b->zc || b->zc + 1u < a->zc)
        printf("    not the same: energy %.6g / %.6g, zero crossings %u / %u\n", a->e, b->e, a->zc, b->zc);
    return d <= 0.002 * a->e && a->zc + 1u >= b->zc && b->zc + 1u >= a->zc;
}
static int patch_diff(const uint8_t *a, const uint8_t *b, uint32_t *at)   /* bytes that differ (the last at *at) */
{
    int n = 0;
    uint32_t i;
    for (i = 0; i < FP_SIZE; i++)
        if (a[i] != b[i]) { n++; *at = i; }
    return n;
}
static void fop_pick(uint32_t opn)                   /* KNOB 1: OP opn (1..6) */
{
    turn(EN_K1, -6);
    turn(EN_K1, (int32_t)opn - 1);
}
static int slot_own(void) { return TSEL->p[P_E7] == FM6_OWN && fm6_slot[song.sel] == FM6_OWN; }
static int test_fm6_ops(void)
{
    static const char *const CYC_F[] = {"EDIT 1", "EDIT 2", "OPERATOR", "OP ENV", "VOICE", "EDIT 1"};

    static const struct { const char *page; uint32_t k, off; } F[] = {
        {"OPERATOR", EN_K3, FP_FF}, {"OPERATOR", EN_K4, FP_OL},
        {"OP ENV", EN_K3, FP_R1}, {"OP ENV", EN_K4, FP_L1}};
    int bad = 0, ok;
    uint32_t opn, f, s, at, n = 0, tr, i;
    uint8_t before[FP_SIZE + 1u], v[FP_SIZE + 1u], mine[FP_SIZE + 1u];
    fm6_snd_t s0, s1;
    ui_power_on();
    song.sel = 1;                                    /* T2: FM6 PAD (SLOT F5) */
    tr = song.sel;
    bad += check("FM6 ops: FM6's EDIT cycle EDIT 1 EDIT 2 OPERATOR OP ENV VOICE EDIT 1",
                 TSEL->eng_req == ENGI_FM6 && engine_cycle(CYC_F, NELEM(CYC_F)));
    ok = TSEL->p[P_E7] == 4 && fm6_slot[tr] == 4u;
    go_title("OPERATOR"); frame();
    fop_pick(3);
    memcpy(before, fm6_patch[tr], FP_SIZE);
    turn(EN_K4, -5);                                 /* OP 3 LEVEL, one gesture of two turns */
    turn(EN_K4, -2);
    ok &= patch_diff(before, fm6_patch[tr], &at) == 1 && at == 3u * FP_OP + FP_OL &&
          fm6_patch[tr][at] == before[at] - 7u && slot_own() && fop_op == 2u;
    bad += check("FM6 ops: OPERATOR KNOB 4 on OP 3: its LEVEL byte only; F5 -> SLOT OWN", ok);
    hold(B_SAVE);
    ok = !memcmp(before, fm6_patch[tr], FP_SIZE) && TSEL->p[P_E7] == 4 && fm6_slot[tr] == 4u && msg_is("UNDO/REDO T2");
    hold(B_SAVE);
    ok &= fm6_patch[tr][3u * FP_OP + FP_OL] == before[3u * FP_OP + FP_OL] - 7u && slot_own();
    frames(1600);
    turn(EN_K3, 4);                                  /* another gesture: its own copy */
    hold(B_SAVE);
    ok &= fm6_patch[tr][3u * FP_OP + FP_OL] == before[3u * FP_OP + FP_OL] - 7u &&
          fm6_patch[tr][3u * FP_OP + FP_FF] == before[3u * FP_OP + FP_FF] && slot_own();
    bad += check("FM6 ops: SAVE held undoes the gesture (patch and SLOT F5 back), again redoes; one undo per gesture", ok);

    ok = 1;                                          /* FINE / LEVEL / ENV: each knob writes its byte only */
    for (opn = 1; opn <= 6u; opn++)
        for (f = 0; f < NELEM(F); f++)
            for (s = 0; s < (F[f].off == FP_R1 || F[f].off == FP_L1 ? 4u : 1u); s++) {
                uint32_t b = (6u - opn) * FP_OP + F[f].off + s, max = fm6_max(b);
                int32_t d;
                go_title(F[f].page); frame();
                fop_pick(opn);
                if (F[f].off == FP_R1 || F[f].off == FP_L1) { turn(EN_K2, -4); turn(EN_K2, (int32_t)s); }
                memcpy(v, fm6_patch[tr], FP_SIZE);
                d = v[b] < max ? 1 : -1;
                frames(1600);
                turn(F[f].k, d);
                if (patch_diff(v, fm6_patch[tr], &at) != 1 || at != b || fm6_patch[tr][b] != (uint8_t)(v[b] + d) || !slot_own()) {
                    printf("ui: FM6 ops: OP %u %s KNOB %u stage %u: byte %u, not %u\n", opn, F[f].page, F[f].k - EN_K1 + 1u,
                           s + 1u, at, b);
                    ok = 0;
                }
                turn(F[f].k, 200);                   /* the range: never past the byte's highest, nor below 0 */
                ok &= fm6_patch[tr][b] == max;
                turn(F[f].k, -200);
                ok &= fm6_patch[tr][b] == 0u;
                n++;
            }
    bad += check("FM6 ops: 6 operators x FINE LEVEL + ENV rates/levels, each knob writes its byte only, SLOT OWN",
                 ok && n == 60u);

    ok = 1;                                          /* FREQ: ratio 0..31 then fixed 1/10/100/1000 Hz (MODE+FC) */
    n = 0;
    for (opn = 1; opn <= 6u; opn++) {
        uint32_t bm = (6u - opn) * FP_OP + FP_MODE, bf = (6u - opn) * FP_OP + FP_FC;
        go_title("OPERATOR"); frame();
        fop_pick(opn);
        frames(50);
        turn(EN_K2, -200);                           /* ratio 0.50 */
        ok &= !fm6_patch[tr][bm] && fm6_patch[tr][bf] == 0u && slot_own();
        for (i = 0; i < 31u; i++) {                  /* one step; frames > ACC_GAP so no accel */
            frames(50);
            turn(EN_K2, 1);
            ok &= !fm6_patch[tr][bm] && fm6_patch[tr][bf] == i + 1u;
        }
        frames(50);
        turn(EN_K2, 1);                              /* into fixed 1 Hz */
        ok &= fm6_patch[tr][bm] == 1u && fm6_patch[tr][bf] == 0u;
        for (i = 1; i < 4u; i++) {
            frames(50);
            turn(EN_K2, 1);
            ok &= fm6_patch[tr][bm] == 1u && fm6_patch[tr][bf] == i;
        }
        turn(EN_K2, 200);
        ok &= fm6_patch[tr][bm] == 1u && fm6_patch[tr][bf] == 3u;   /* 1000 Hz, not past */
        frames(50);
        turn(EN_K2, -1);                             /* back toward ratios: 100 Hz */
        ok &= fm6_patch[tr][bm] == 1u && fm6_patch[tr][bf] == 2u;
        n++;
    }
    bad += check("FM6 ops: FREQ walks ratio 0.50..31 then fixed 1/10/100/1000 Hz (MODE+COARSE), SLOT OWN", ok && n == 6u);

    /* the sound: the init voice (ALG 1: OP 1 the carrier at 99, OP 2 its modulator at 0), the track's other sends off */
    TSEL->p[P_CHOR] = TSEL->p[P_DLY] = TSEL->p[P_REV] = TSEL->p[P_DIST] = 0;
    TSEL->p[P_SLDEPTH] = 0;                     /* the SLICER's MIX too: the SEND bus gates every track's
                                                 * render on its free-running clock, so a round-trip below
                                                 * would compare two different slicer phases */
    memset(dly_buf, 0, sizeof dly_buf);         /* the delay tail of an earlier test is not this sound: the
                                                 * RATE tap holds, then fades after a tempo change, so it
                                                 * must be cleared for s0 and s1 to compare the same tap */
    dly_q8 = 0;
    dly_free = 0;
    TSEL->p[P_LD_PIT] = TSEL->p[P_LD_FLT] = TSEL->p[P_LD_SHP] = TSEL->p[P_LD_AMP] = 0;
    TSEL->p[P_M1SRC] = TSEL->p[P_M2SRC] = TSEL->p[P_M3SRC] = TSEL->p[P_M4SRC] = 0;
    for (f = 0; f < 7u; f++) TSEL->p[P_E0 + f] = 0;
    fm6_unpack(FM6_INIT, v);
    fm6_set_patch(tr, v);
    fm6_adopt(tr);
    s0 = fm6_sound();
    go_title("OPERATOR"); frame();
    fop_pick(2);
    turn(EN_K4, 80);                                 /* OP 2 LEVEL 0 -> 80: FM, brighter */
    s1 = fm6_sound();
    ok = s1.hf > 2.0 * s0.hf && fm6_patch[tr][4u * FP_OP + FP_OL] == 80u && TSEL->p[P_SLDEPTH] == 0;
    printf("    OP 2 LEVEL 80: HF share %.4f -> %.4f\n", s0.hf, s1.hf);
    turn(EN_K4, -80);
    s1 = fm6_sound();
    ok &= fm6_same(&s0, &s1);
    bad += check("FM6 ops: the sound: a modulator's LEVEL brightens it; back to 0 the same sound again", ok);
    fop_pick(1);
    turn(EN_K4, -20);                                /* OP 1 (the carrier) LEVEL 99 -> 79 */
    s1 = fm6_sound();
    ok = s1.e < 0.2 * s0.e && s1.e > 0;
    printf("    OP 1 LEVEL 79: energy x%.3f", s1.e / s0.e);
    turn(EN_K4, 20);
    frames(1600);
    turn(EN_K2, 1);                                  /* OP 1 FREQ 1 -> 2: an octave up */
    s1 = fm6_sound();
    ok &= s1.zc > 1.8 * s0.zc && s1.zc < 2.2 * s0.zc;
    printf("; FREQ 2: zero crossings %u -> %u", s0.zc, s1.zc);
    frames(1600);
    turn(EN_K2, -1);
    go_title("OP ENV"); frame();
    turn(EN_K2, -4);
    turn(EN_K3, -70);                                /* stage 1 (ATK) RATE 99 -> 29: a slow attack */
    s1 = fm6_sound();
    ok &= s1.early < 0.5 * s0.early;
    printf("; ATK RATE 29: the first 2048 samples' energy x%.3f", s1.early / s0.early);
    turn(EN_K3, 70);
    go_title("OPERATOR"); frame();
    fop_pick(1);
    frames(50);
    turn(EN_K2, -200);                               /* FREQ to 0.50 */
    for (i = 0; i < 33u; i++) {                      /* ..31 ratio, then fixed 1 Hz, then 10 Hz */
        frames(50);
        turn(EN_K2, 1);
    }
    ok &= fm6_patch[tr][5u * FP_OP + FP_MODE] == 1u && fm6_patch[tr][5u * FP_OP + FP_FC] == 1u;
    s1 = fm6_sound();
    ok &= s1.zc < s0.zc / 4u;
    printf("; FIXED 10 Hz: zero crossings %u\n", s1.zc);
    for (i = 0; i < 32u; i++) {                      /* back to ratio 1 */
        frames(50);
        turn(EN_K2, -1);
    }
    ok &= !fm6_patch[tr][5u * FP_OP + FP_MODE] && fm6_patch[tr][5u * FP_OP + FP_FC] == 1u;
    s1 = fm6_sound();
    ok &= fm6_same(&s0, &s1);
    bad += check("FM6 ops: a carrier's LEVEL lowers it, FREQ 2 an octave up, ATK RATE 29 slower, FIXED 10 Hz; undone: as before",
                 ok);

    /* the macros on top: MRAT / MLVL move the edited patch as they move any (the patch itself stays) */
    go_title("OPERATOR"); frame();
    fop_pick(2);
    turn(EN_K4, 70);                                 /* OP 2 LEVEL 70, FREQ 1 -> 3 */
    frames(1600);
    turn(EN_K2, 2);
    memcpy(mine, fm6_patch[tr], FP_SIZE);
    TSEL->p[P_E3] = 2;                               /* MRAT +2 */
    s1 = fm6_sound();
    ok = fm6_eff[tr].p[4u * FP_OP + FP_FC] == 5u && !memcmp(mine, fm6_patch[tr], FP_SIZE);
    frames(1600);
    turn(EN_K2, 1);                                  /* an edit under MRAT: the macro still adds to it */
    s1 = fm6_sound();
    ok &= fm6_eff[tr].p[4u * FP_OP + FP_FC] == 6u && fm6_patch[tr][4u * FP_OP + FP_FC] == 4u;
    TSEL->p[P_E3] = 0;
    s0 = fm6_sound();
    TSEL->p[P_E2] = -64;                             /* MLVL -64: the modulators down, darker */
    s1 = fm6_sound();
    ok &= s1.hf < 0.5 * s0.hf && fm6_patch[tr][4u * FP_OP + FP_OL] == 70u;
    TSEL->p[P_E2] = 0;
    bad += check("FM6 ops: the macros apply on top of the edited patch (MRAT adds to COARSE, MLVL darker); the patch stays", ok);

    /* kept: a project and a user preset hold the edited patch (SLOT OWN) */
    memcpy(mine, fm6_patch[tr], FP_SIZE);
    stop_transport();
    project_save(2);
    f = (uint32_t)up_store(9, "OPS");
    ok = f == 0u || f == 3u;                         /* (3: the host has no flash, kept in RAM) */
    TSEL->p[P_E7] = 0;                               /* SLOT F1: another patch */
    fm6_poll();
    ok &= memcmp(mine, fm6_patch[tr], FP_SIZE) != 0;
    project_load(2);
    ok &= !memcmp(mine, fm6_patch[tr], FP_SIZE) && slot_own();
    TSEL->p[P_E7] = 1; fm6_poll();
    up_load(9);
    ok &= !memcmp(mine, fm6_patch[tr], FP_SIZE) && slot_own();
    bad += check("FM6 ops: the edited patch round-trips through a project and a user preset (SLOT OWN)", ok);

    /* nothing else: not on other engines, not automatable */
    ok = 1;
    song.sel = 0;                                    /* T1 ANALOG: no operator page */
    for (f = 0; f < NPAGES; f++)
        if (PAGES[f].graph == GR_FMOP || PAGES[f].graph == GR_FMENV) ok &= !page_visible(f);
    song.sel = 1;
    go_title("OPERATOR"); frame();
    song.rec = 2u; song.playing = 1;
    f = motion.count;
    turn(EN_K3, 3);
    ok &= motion.count == f;
    stop_transport(); song.rec = 0;
    bad += check("FM6 ops: the pages on FM6 only; a turn while recording writes no automation", ok);
    return bad;
}

/* 1.3 (Discussions #112, #134): MENU > DISPLAY > HOME SCOPE / TRACKS (ui.c ui_home_view, settings byte [15][26]) and
 * HOME > TRACKS (ui_graph.c draw_home_tracks): the setting (its default, names, tab, byte, the other settings' bytes
 * untouched, an unknown value as SCOPE, the menu's knob, the settings record); the rows read from the screen: a note's
 * bar, an empty step's stub, a TIE carried on from its note, an accent taller, the step playing on its raised cell,
 * LEN > 16 on the play head's page (stopped: the selected track's cursor page, the others' first) with its pips, DRUM
 * hits as pads (two for two hits), MUTE (its badge, the steps DIM), the selected row THEME, the meter; HOME's knobs
 * still edit the selected track, ALGORITHM moves the selection, HOME's tap still goes Home ↔ Mixer;
 * MENU > LARGE: the cards as on a list page, the rows in the panel */
static uint16_t tr_px(int32_t x, int32_t y) { return swap16(host_screen[(uint32_t)y * 240u + (uint32_t)x]); }
static int32_t tr_y(uint32_t c) { return Y_GRAPH + (int32_t)c * TR_PITCH; }
static int32_t tr_x(uint32_t i) { return 3 + tr_bx(i) + TR_BW / 2; }   /* a step's bar, its middle column */
/* 1.5 (#173): the FX map's columns light / dark in turn (ui_layer.c LS_ALT): an idle cell's fill, read inside its top
 * edge, the same in columns 1 and 3 and in 2 and 4 and different between them, every row, FLAT and LINE (LINE: the
 * 1st / 3rd unfilled, the panel's colour) */
static int test_fx_map_columns(void)
{
    int bad = 0, ok = 1;
    uint32_t st, r, c;
    for (st = ST_FLAT; st <= ST_LINE; st++) {
        ui_power_on();
        settings.palette = UI_GREY_INDEX;
        style_apply();                              /* STYLE retired: forces FLAT / ANIM OFF */
        ux.style = (uint8_t)st;                     /* host: still exercise the LINE draw path */
        palette_set(settings.palette);
        ui.home = 1; ui.layer = LAYER_FX; ui.force = 1;
        draw_layer();
        for (r = 0; r < 4u; r++) {
            uint16_t px[4];
            for (c = 0; c < 4u; c++)
                px[c] = tr_px(LC_X(c) + 27, Y_GRAPH + 4 + (LF_H + 4) * (int32_t)r + 1);
            ok &= px[0] == px[2] && px[1] == px[3] && px[0] != px[1];
            ok &= !st || px[0] == T_SURF;
        }
        ui.layer = 0;
    }
    ui_style = ST_FLAT; style_apply(); settings.palette = UI_GREY_INDEX; palette_set(UI_GREY_INDEX);
    bad += check("1.5 #173: the FX map's columns alternate light / dark (FLAT, LINE)", ok);
    return bad;
}

/* 1.5 (Discussion #156): HELP. MENU > SYSTEM > HELP (OFF by default, bit 3 of ui_prefs2 alone); every page a button
 * reaches (DIGITAL's retired OP pages aside), HOME, HOME LEVELS, the DRUM grid and every layer has a hint of 2..3 key
 * hints that fits its footer row with every word (cv_key_row drops none); ON: a page entered shows its hint ~2 s, then
 * the footer as before; OFF: none */
static int help_fits(const char *s)
{
    khint_t kh[3];
    char b[48];
    uint32_t n = help_parse(s, kh, b, sizeof b), i;
    int32_t w = 6 * ((int32_t)n - 1);
    int ok = n >= 2u && n <= 3u;
    for (i = 0; i < n; i++) {
        ok &= kh[i].key < KC_COUNT && kh[i].act[0] != 0;
        w += kh_w(kh[i].key, kh[i].act);
    }
    if (!ok || w > 224) printf("  help: \"%s\" %u hints, %d px of 224\n", s, n, w);
    return ok && w <= 224;
}
static int test_help(void)
{
    int bad = 0, ok;
    uint8_t before[32];
    uint32_t i, l;
    ui_power_on();
    memcpy(before, favorites.factory[15], 32);
    ok = !help_on() && menu_get(MI_HELP) == 0u && str_eq(MI_NAME[MI_HELP], "HELP") && MI_TAB[MI_HELP] == MTAB_SYSTEM &&
         MI_HELP < MI_VALUES && str_eq(menu_vname(MI_HELP, 1), "ON");
    menu_put(MI_HELP, 1);
    ok &= help_on() && menu_get(MI_HELP) == 1u && favorites.factory[15][24] == (uint8_t)(before[24] | 8u);
    for (i = 0; i < 32u; i++)
        ok &= i == 24u || favorites.factory[15][i] == before[i];
    bad += check("1.5 MENU > SYSTEM > HELP: OFF by default, ON a bit of its own (ui_prefs2 bit 3)", ok);

    ok = 1;
    for (i = 0; i < NPAGES; i++) {                     /* every page but DIGITAL's OP1..4 ENV / OP LEVEL (FM4 only) */
        uint8_t h = ui.home, pg = ui.page;
        if (PAGES[i].fam == FAM_EDIT && PAGES[i].id[0] >= P_FM1_ATK && PAGES[i].id[0] <= P_FM4_LEVEL)
            continue;
        ui.home = 0; ui.page = (uint8_t)i;
        if (!help_page()) { printf("  help: no hint for %s\n", PAGES[i].title); ok = 0; }
        else ok &= help_fits(help_page());
        ui.home = h; ui.page = pg;
    }
    ok &= help_fits(H_HOME) && help_fits(H_LEVELS) && help_fits(H_GRID);
    for (l = LAYER_FX; l < LAYER_N; l++)
        ok &= help_fits(HELP_LAYER[l]);
    bad += check("1.5 HELP: a hint for every page, HOME, LEVELS, the grid and every layer; each fits its row, no word dropped", ok);

    ui_power_on();
    menu_put(MI_HELP, 1);
    press(B_ENV);
    ok = ui.help_t > 0u && str_eq(help_page(), "[K1-4]A D S R[ENV]ENV DEST");
    frames(2200);
    ok &= ui.help_t == 0u;
    press(B_SAVE);
    ok &= ui.help_t > 0u && cur_page()->graph == GR_BROWSE && str_eq(help_page(), "[K2]ENGINE[K3]FAV[K4]LIST");
    press(B_HOME);
    ok &= ui.home && ui.help_t > 0u && str_eq(help_page(), H_HOME);
    menu_put(MI_HOME, HV_LEVELS);
    ok &= str_eq(help_page(), H_LEVELS);
    menu_put(MI_HOME, HV_SCOPE);
    menu_put(MI_HELP, 0);
    press(B_ENV);
    ok &= ui.help_t == 0u;
    bad += check("1.5 HELP ON: a page entered (a button, HOME) shows its hint ~2 s, then not; OFF: never", ok);
    return bad;
}

static int test_home_tracks(void)
{
    int bad = 0, ok;
    uint8_t before[32];
    persist_t p;
    track_t *t;
    uint32_t i, id;
    int16_t v0;
    ui_power_on();
    memcpy(before, favorites.factory[15], 32);
    ok = ui_home_view == 0u && !home_tracks() && menu_get(MI_HOME) == 0u && menu_n(MI_HOME) == 3u &&
         str_eq(MI_NAME[MI_HOME], "HOME") && str_eq(menu_vname(MI_HOME, 0), "SCOPE") &&
         str_eq(menu_vname(MI_HOME, 1), "TRACKS") && str_eq(menu_vname(MI_HOME, 2), "LEVELS") &&   /* (1.5: LEVELS) */
         MI_TAB[MI_HOME] == MTAB_DISPLAY && MI_HOME < MI_VALUES;
    menu_put(MI_HOME, 1);
    ok &= ui_home_view == 1u && home_tracks() && menu_get(MI_HOME) == 1u;
    for (i = 0; i < 32u; i++)                           /* (byte 26 only: SCREEN OFF, the 1.2 flags, the FX keys kept) */
        ok &= i == 26u || favorites.factory[15][i] == before[i];
    memset(&p, 0, sizeof p);
    settings_export(&p);
    ok &= p.favorites.factory[15][26] == 1u;
    menu_put(MI_HOME, 0);
    ok &= settings_import(&p, (int)sizeof p) == 1 && ui_home_view == 1u;   /* (back from the record) */
    ui_home_view = 9;                                   /* a later firmware's view: SCOPE here, kept as saved */
    ok &= !home_tracks() && menu_get(MI_HOME) == 0u;
    settings_export(&p);
    ok &= settings_import(&p, (int)sizeof p) == 1 && ui_home_view == 9u;
    bad += check("1.3 MENU > DISPLAY > HOME: SCOPE (0, the default) / TRACKS, its own settings byte, saved; unknown = SCOPE", ok);

    ui_power_on();
    hold(B_HOME);                                       /* the menu: DISPLAY, down to HOME, K1 right */
    for (i = 0; i < 5u; i++) turn(EN_ALGO, 1);         /* COLOR .. HOME (6 rows; STYLE/ANIM retired) */
    ok = ui.menu == 1 && ui.menu_sel == MI_HOME;
    turn(EN_K1, 1); ok &= home_tracks() && !home_levels();
    turn(EN_K3, 1); ok &= home_levels();               /* (1.5: LEVELS, the last) */
    turn(EN_K3, 1); ok &= home_levels();               /* (stops there) */
    press(B_OCTDN); ok &= home_tracks() && !home_levels();
    press(B_OCTDN); ok &= !home_tracks();
    press(B_OCTUP); ok &= home_tracks();
    press(B_HOME); frame();
    ok &= !ui.menu && ui.home;
    bad += check("1.3 MENU: HOME's row (any knob, OCT+ / OCT-: SCOPE TRACKS LEVELS), the menu closed: HOME", ok);

    /* 1.5 (#134): HOME LEVELS: the rows of TRACKS, KNOB 1..4 the LEVEL of T1..T4 (the selected track's engine untouched),
     * MOMENTARY on any of them. And ANIM no longer sets
     * HOME (up to 1.4.1 its MENU row fell through into HOME's) */
    ui_power_on();
    menu_put(MI_HOME, HV_LEVELS);
    song.sel = 0;
    go_home(); frame();
    {
        int16_t lv[NTRK], e0[NTRK][P_COUNT];
        uint32_t k;
        const param_desc_t *d;
        int16_t *vp;
        for (k = 0; k < NTRK; k++) { lv[k] = trk[k].p[P_LEVEL]; memcpy(e0[k], trk[k].p, sizeof e0[k]); }
        turn(EN_K2, -5);
        turn(EN_K4, 3);
        ok = home_tracks() && home_levels() && trk[1].p[P_LEVEL] < lv[1] && trk[3].p[P_LEVEL] > lv[3] &&
             trk[0].p[P_LEVEL] == lv[0] && trk[2].p[P_LEVEL] == lv[2];
        for (k = 0; k < P_COUNT; k++)                   /* (nothing else of any track) */
            ok &= (k == P_LEVEL || (trk[0].p[k] == e0[0][k] && trk[1].p[k] == e0[1][k] && trk[2].p[k] == e0[2][k] &&
                                    trk[3].p[k] == e0[3][k]));
        d = home_param(2, &vp);
        ok &= vp == &trk[2].p[P_LEVEL] && d == &TP[P_LEVEL] && home_trk(2) == &trk[2];
        bad += check("1.5 HOME LEVELS: KNOB 2 / 4 the LEVEL of T2 / T4, nothing else of any track", ok);
        k = (uint32_t)trk[2].p[P_LEVEL];
        btn_down(B_LFO); frame();
        turn(EN_K3, -6);
        ok = trk[2].p[P_LEVEL] < (int16_t)k && mom.n == 1u;
        btn_up(B_LFO); frame();
        bad += check("1.5 HOME LEVELS: MOMENTARY (LFO held) on T3's LEVEL, back when LFO is let go",
                     ok && trk[2].p[P_LEVEL] == (int16_t)k && !mom.n);
        menu_put(MI_HOME, HV_TRACKS);
        lv[1] = trk[1].p[P_LEVEL];
        turn(EN_K2, 4);
        bad += check("1.5 HOME TRACKS (not LEVELS): KNOB 2 the selected track's engine again, no level",
                     trk[1].p[P_LEVEL] == lv[1] && !home_levels());
        menu_put(MI_HOME, HV_LEVELS);
        menu_put(MI_ANIM, 0u);                          /* ANIM retired: no-op, HOME stays LEVELS */
        ok = home_levels() && menu_get(MI_ANIM) == 1u;
        menu_put(MI_ANIM, 2u);
        ok &= home_levels() && menu_get(MI_ANIM) == 1u;
        menu_put(MI_HOME, HV_SCOPE);
        menu_put(MI_ANIM, 0u);
        ok &= !home_tracks() && menu_get(MI_ANIM) == 1u;
        bad += check("1.5 MENU ANIM retired: leaves HOME as it is (always OFF)", ok);
    }

    /* the rows: T1 (not selected) a line, T2 selected, T3 muted, T4 the DRUM beat */
    ui_power_on();
    ui_home_view = HV_TRACKS;
    song.sel = 1;
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    for (i = 0; i < NSTEP; i++) step_clear(&t->step[i]);
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    t->step[4] = (step_t){{62}, 1, ST_NOTE, 0, 100};
    t->step[5] = (step_t){{62}, 0, ST_TIE, 0, 100};     /* (carried on from step 4's note) */
    t->step[6] = (step_t){{62}, 0, ST_TIE, 0, 100};
    t->step[8] = (step_t){{64}, 1, ST_NOTE, SF_ACCENT, 100};
    t->step[10] = (step_t){{0}, 0, ST_TIE, 0, 100};     /* (a TIE after an empty step: nothing to carry) */
    go_home(); frame();
    {
        int32_t y = tr_y(0);
        ok = tr_px(tr_x(0), y + TR_SB - 3) == T_THEME && tr_px(tr_x(0), y + TR_SB - 9) == T_SURF &&   /* a note */
             tr_px(tr_x(1), y + TR_SB - 1) == T_RAISE && tr_px(tr_x(1), y + TR_SB - 4) == T_SURF &&   /* empty */
             tr_px(tr_x(5), y + TR_SB - 3) == T_THEME && tr_px(tr_x(6), y + TR_SB - 3) == T_THEME &&  /* tied */
             tr_px(3 + tr_bx(5) - 1, y + TR_SB - 3) == T_THEME &&                                    /* .. joined */
             tr_px(3 + tr_bx(4) - 1, y + TR_SB - 3) == T_SURF &&                                     /* (not before its note) */
             tr_px(tr_x(8), y + TR_SB - 9) == T_ACCENT &&                                             /* accented */
             tr_px(tr_x(10), y + TR_SB - 1) == T_MID && tr_px(tr_x(10), y + TR_SB - 3) == T_SURF &&   /* a lone TIE */
             tr_px(3 + 22, y + 1) == T_SURF && tr_px(3 + 22, tr_y(1) + 1) == T_THEME &&               /* T2 selected */
             home_tracks() && large_kind() == LK_OFF;
        bad += check("1.3 HOME > TRACKS: a row's steps: notes, empty stubs, TIEs carried on (joined), accents, a lone TIE", ok);
    }
    song.playing = 1;                                   /* the play head on step 3 (empty), then on the note 4 */
    t->seq_idx = 3;
    frame();
    ok = tr_px(3 + tr_bx(3), tr_y(0) + 20) == T_RAISE && tr_px(tr_x(3), tr_y(0) + TR_SB - 1) == T_MID &&
         tr_px(3 + tr_bx(2), tr_y(0) + 20) == T_SURF;
    t->seq_idx = 4;
    frame();
    ok &= tr_px(tr_x(4), tr_y(0) + TR_SB - 3) == T_TEXT && tr_px(3 + tr_bx(3), tr_y(0) + 20) == T_SURF &&
          tr_px(3 + tr_bx(4) - 1, tr_y(0) + 20) == T_RAISE;
    bad += check("1.3 HOME > TRACKS: the step playing on a raised cell, its bar bright; it moves with the play head", ok);

    t->p[P_SLEN] = 40;                                  /* LEN 40: 3 pages; step 20 a note, step 16 empty */
    t->step[20] = (step_t){{65}, 1, ST_NOTE, 0, 100};
    t->seq_idx = 21;
    frame();
    ok = tr_px(tr_x(4), tr_y(0) + TR_SB - 3) == T_THEME && tr_px(tr_x(0), tr_y(0) + TR_SB - 3) == T_SURF &&
         tr_px(3 + tr_bx(5), tr_y(0) + 20) == T_RAISE &&                                    /* playing 21 = its 6th */
         tr_px(3 + TR_PX + 1, tr_y(0) + TR_SB - 5) == T_THEME &&                            /* the 2nd of 3 pips */
         tr_px(3 + TR_PX + 1, tr_y(0) + TR_SB - 8) == T_RAISE && tr_px(3 + TR_PX + 1, tr_y(0) + TR_SB - 2) == T_RAISE;
    t->seq_idx = 36;                                   /* the last page: 8 steps, the rest blank */
    frame();
    ok &= tr_px(tr_x(7), tr_y(0) + TR_SB - 1) == T_RAISE && tr_px(tr_x(8), tr_y(0) + TR_SB - 1) == T_SURF &&
          tr_px(3 + TR_PX + 1, tr_y(0) + TR_SB - 2) == T_THEME;
    song.playing = 0;                                   /* stopped: T1 not selected, its first page */
    frame();
    ok &= tr_px(tr_x(0), tr_y(0) + TR_SB - 3) == T_THEME && tr_px(tr_x(4), tr_y(0) + TR_SB - 3) == T_THEME &&
          tr_px(3 + TR_PX + 1, tr_y(0) + TR_SB - 8) == T_THEME;
    song.sel = 0;                                       /* .. selected: its cursor's page (INK on THEME) */
    ui.cursor = 20; ui.bank = 1;
    frame();
    ok &= tr_px(tr_x(0), tr_y(0) + TR_SB - 3) == T_THEME && tr_px(tr_x(4), tr_y(0) + TR_SB - 3) == T_INK &&
          tr_px(3 + TR_PX + 1, tr_y(0) + TR_SB - 5) == T_INK;
    bad += check("1.3 HOME > TRACKS: LEN > 16: the play head's page (stopped: the cursor's, selected), pips, past LEN blank", ok);

    song.sel = 1;
    t = &trk[3];                                        /* T4, DRUM: one hit, two hits, an accented hit */
    ok = drum_track(t);
    for (i = 0; i < NSTEP; i++) step_clear(&t->step[i]);
    t->p[P_SLEN] = 16;
    t->step[0].time = t->step[2].time = t->step[4].time = ST_NOTE;
    t->step[0].hit = 1; t->step[2].hit = 1 | 8; t->step[4].hit = 2; t->step[4].acc = 2;
    t->p[P_MUTE] = 0;
    frame();
    {
        int32_t y = tr_y(3);
        ok &= tr_px(tr_x(0), y + TR_SB - 2) == T_THEME && tr_px(tr_x(0), y + TR_SB - 7) == T_SURF &&
              tr_px(tr_x(2), y + TR_SB - 2) == T_THEME && tr_px(tr_x(2), y + TR_SB - 7) == T_THEME &&
              tr_px(tr_x(4), y + TR_SB - 2) == T_ACCENT && tr_px(tr_x(1), y + TR_SB - 1) == T_RAISE;
    }
    bad += check("1.3 HOME > TRACKS: DRUM hits as pads (two for two hits or more), an accented hit the accent", ok);

    t = &trk[2];                                        /* T3 muted: the MUTE badge, its steps DIM, no meter */
    for (i = 0; i < NSTEP; i++) step_clear(&t->step[i]);
    t->p[P_SLEN] = 16;
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    t->p[P_MUTE] = 1;
    trk[3].peak = 20000;
    frame();
    ok = tr_px(tr_x(0), tr_y(2) + TR_SB - 3) == T_DIM && tr_px(3 + 222 - kc_w(KC_MUTE) + 2, tr_y(2) + TR_NY(0) + 4) == T_KEY &&
         tr_px(3 + 222 - kc_w(KC_MUTE) + 2, tr_y(3) + TR_NY(0) + 4) != T_KEY &&
         tr_px(3 + TR_MX + 1, tr_y(3) + TR_MY + TR_MH - 2) == T_MID && tr_px(3 + TR_MX + 1, tr_y(2) + TR_MY + TR_MH - 2) == T_RAISE;
    t->p[P_MUTE] = 0;
    frame();
    ok &= tr_px(tr_x(0), tr_y(2) + TR_SB - 3) == T_THEME && tr_px(3 + 222 - kc_w(KC_MUTE) + 2, tr_y(2) + TR_NY(0) + 4) != T_KEY;
    bad += check("1.3 HOME > TRACKS: MUTE: its badge, the steps DIM; unmuted again: as before; the output meter", ok);

    /* HOME's knobs: the selected track's; ALGORITHM moves the selection; HOME tapped: MIXER, CLOCK, HOME */
    song.sel = 2; ui.force = 1; frame();
    id = ENGINES[trk[2].eng_req % NENGINES]->knob[0];
    v0 = trk[2].p[id];
    {
        int16_t o0 = trk[0].p[id], o1 = trk[1].p[id];
        turn(EN_K1, 3);
        if (trk[2].p[id] == v0) turn(EN_K1, -6);        /* (at its top: the other way) */
        ok = trk[2].p[id] != v0 && trk[0].p[id] == o0 && trk[1].p[id] == o1 && ui.home && home_tracks();
    }
    turn(EN_ALGO, 1);
    frame();
    ok &= song.sel == 3u && tr_px(3 + 22, tr_y(3) + 1) == T_THEME && tr_px(3 + 22, tr_y(2) + 1) == T_SURF;
    id = ENGINES[trk[3].eng_req % NENGINES]->knob[1];
    v0 = trk[3].p[id];
    turn(EN_K2, -2);
    if (trk[3].p[id] == v0) turn(EN_K2, 4);
    ok &= trk[3].p[id] != v0;
    bad += check("1.3 HOME > TRACKS: K1..K4 edit the selected track (HOME's four), ALGORITHM moves the selected row", ok);
    press(B_HOME); ok = !ui.home && cur_page()->graph == GR_TRK;
    press(B_HOME); ok &= ui.home;
    frame();
    ok &= tr_px(3 + 22, tr_y(3) + 1) == T_THEME;
    bad += check("1.3 HOME > TRACKS: HOME tapped still goes Home ↔ Mixer (the rows back)", ok);

    ui_prefs |= PREF_LARGE;                             /* MENU > LARGE: the cards as on a list page, the rows */
    ui.force = 1; frame();
    ok = large_kind() == LK_LABEL && card_h() == CARD_H && graph_y() == Y_GRAPH && tr_px(3 + 22, tr_y(3) + 1) == T_THEME &&
         tr_px(tr_x(4), tr_y(3) + TR_SB - 2) == T_INK;
    ui_home_view = HV_SCOPE; ui.force = 1; frame();
    ok &= large_kind() == LK_TALL;
    ui_prefs = 0;
    bad += check("1.3 HOME > TRACKS with LARGE: cards with M labels, the rows kept (SCOPE: the tall cards as before)", ok);
    ui_power_on();
    return bad;
}

/* #181: a sound load (PRESETS) or another track (ALGORITHM) draws the page again (ui.force) with no blank frame on the
 * way: MIXER cleared its panel to the background before its four columns landed (HOME > TRACKS its rows), a flash on
 * every detent. Every pixel goes from what it showed straight to what it shows next (host_fl_*), on MIXER, HOME (the
 * scope), HOME > TRACKS and ENV, FLAT and LINE, MENU > LARGE off and on; a full redraw still covers the screen */
static int test_no_flash(void)
{
    static const char *const VIEW[4] = {"MIXER", "HOME", "HOME TRACKS", "ENV"};
    static const uint8_t KN[2] = {EN_PRESET, EN_ALGO};
    int bad = 0, ok = 1;
    uint32_t v, st, lg, k, n, miss;
    for (v = 0; v < 4u; v++)
        for (st = 0; st < 2u; st++)
            for (lg = 0; lg < 2u; lg++)
                for (k = 0; k < 2u; k++) {
                    ui_power_on();
                    ui_style = st ? ST_LINE : ST_FLAT;
                    ui_prefs = lg ? PREF_LARGE : 0u;
                    ui_home_view = v == 2u;
                    if (v == 1u || v == 2u) {
                        ui.home = 1;
                        ui.force = 1;
                    } else {
                        go_title(VIEW[v]);
                    }
                    frames(100);
                    miss = song.sel * 65536u + TSEL->eng_req * 256u + TSEL->preset;
                    host_fl_begin();
                    turn(KN[k], 1);                     /* (a sound loaded / the next track: the page drawn again) */
                    n = host_fl_end(0, 240);
                    ok &= miss != song.sel * 65536u + TSEL->eng_req * 256u + TSEL->preset;
                    if (n)
                        printf("    #181 %s %s%s, %s: %u pixels flashed\n", VIEW[v], st ? "LINE" : "FLAT",
                               lg ? " LARGE" : "", k ? "ALGORITHM" : "PRESETS", n);
                    ok &= n == 0u && ui.force == 0;
                    for (n = 0; n < 240u * 240u; n++)    /* and the redraw still covers the whole screen */
                        host_screen[n] = 0x5A5A;
                    ui.force = 1;
                    frame();
                    for (n = miss = 0; n < 240u * 240u; n++)
                        miss += host_screen[n] == 0x5A5A;
                    if (miss)
                        printf("    #181 %s %s%s: %u pixels not drawn\n", VIEW[v], st ? "LINE" : "FLAT", lg ? " LARGE" : "", miss);
                    ok &= miss == 0u;
                }
    ui_style = ST_FLAT;
    ui_prefs = 0;
    ui_home_view = 0;
    bad += check("#181 PRESETS / ALGORITHM on MIXER, HOME, HOME > TRACKS, ENV (FLAT, LINE, LARGE): redrawn, no blank frame", ok);
    return bad;
}

/* Live graphs (HOME scope, PATTERN steps, FX tech, …): redraw every UI frame. graph_sig must change on each of two
 * consecutive frames, not every other; the strip cache then sends only the bands that changed. */
static int live_panel_each_frame(void)
{
    uint32_t i, sig;
    int ok = 1;
    ui.force = 1;
    draw_graph();
    ui.force = 0;
    for (i = 0; i < 2u; i++) {
        sig = ui.graph_sig;
        ui.frame++;
        draw_graph();
        ok &= ui.graph_sig != sig;
    }
    return ok;
}
static int test_live_graph_rate(void)
{
    int bad = 0, ok;
    ui_power_on();
    go_home();
    ok = live_panel_each_frame();
    bad += check("HOME scope: graph_sig moves every consecutive frame", ok);
    ui_power_on();
    go_title("PATTERN");
    ok = cur_page()->graph == GR_STEPS && live_panel_each_frame();
    bad += check("PATTERN (live steps): graph_sig moves every consecutive frame", ok);
    ui_power_on();
    song.playing = 0;
    go_title("TAPE");
    ok = cur_page()->id[0] == G_TSPEED && live_panel_each_frame();
    bad += check("TAPE transport (stopped): the reels idle, graph_sig moves every frame", ok);
    {   /* the pixels move too: the spokes advance with the UI frame */
        static uint16_t a[240 * 240];
        frame();
        memcpy(a, host_screen, sizeof a);
        frame();
        bad += check("TAPE transport: the reels move between frames", memcmp(a, host_screen, sizeof a) != 0);
    }
    ui_power_on();
    go_title("LFO");
    ok = cur_page()->graph == GR_LFO && live_panel_each_frame();
    bad += check("LFO wave: graph_sig moves every consecutive frame", ok);
    {   /* the wave scrolls on the part's live phase (voice.c keeps it running): a quarter cycle on, other pixels */
        static uint16_t a[240 * 240];
        TSEL->lfo_ph = 0;
        frame();
        memcpy(a, host_screen, sizeof a);
        TSEL->lfo_ph = 0x40000000u;
        frame();
        bad += check("LFO wave: the curve scrolls with the live phase", memcmp(a, host_screen, sizeof a) != 0);
        TSEL->p[P_LWAVE] = 4;                       /* S&H: the boundary between the last two steps sweeps too */
        TSEL->lfo_rnd = 0x60000000u;
        TSEL->lfo_prev = 0;
        TSEL->lfo_ph = 0;
        frame();
        memcpy(a, host_screen, sizeof a);
        TSEL->lfo_ph = 0x40000000u;
        frame();
        bad += check("LFO wave: S&H's step boundary scrolls with the live phase", memcmp(a, host_screen, sizeof a) != 0);
        TSEL->p[P_LSMOOTH] = 0;                     /* SMTH rounds the step: a jump with it at 0 */
        TSEL->p[P_LRATE] = 64;
        TSEL->lfo_prev = -32768;
        TSEL->lfo_rnd = 0xFFFF0000u;
        TSEL->lfo_ph = 0;
        TSEL->p[P_LWAVE] = 0;                       /* leaves S&H: its next draw fills a fresh curve */
        frame();
        TSEL->p[P_LWAVE] = 4;
        frame();
        TSEL->lfo_ph = 0x40000000u;                 /* a quarter cycle on: the step sits mid-panel */
        frame();
        memcpy(a, host_screen, sizeof a);
        TSEL->lfo_ph = 0;                           /* the same run again, SMTH on */
        TSEL->p[P_LWAVE] = 0;
        frame();
        TSEL->p[P_LWAVE] = 4;
        TSEL->p[P_LSMOOTH] = 127;
        frame();
        TSEL->lfo_ph = 0x40000000u;
        frame();
        bad += check("LFO wave: SMTH rounds S&H's step on the graph", memcmp(a, host_screen, sizeof a) != 0);
    }
    return bad;
}

/* the strip cache (gfx.c cv_blit_rows): a band the panel already shows is not expanded and not sent, a band that
 * changed goes out from its first to its last dirty 30 px group, and a palette change sends every band again */
static int test_strip_cache(void)
{
    int bad = 0, ok;
    uint32_t base;
    ui_power_on();
    palette_set(0);
    /* an identical frame: no expand, no DMA */
    cv_cache_drop();
    cv_begin(240, 124, T_BG);
    cv_rect(0, 0, 240, 124, T_THEME);
    cv_blit(0, 0);
    base = host_blit_bytes;
    cv_blit(0, 0);
    ok = host_blit_bytes == base;
    bad += check("strip cache: a second identical frame sends no pixels", ok);
    /* one 30 px group dirty: only that group goes out */
    cv_cache_drop();
    cv_begin(240, 124, T_BG);
    cv_rect(0, 0, 240, 124, T_THEME);
    cv_blit(0, 0);
    cv_begin(240, 124, T_BG);
    cv_rect(0, 0, 240, 124, T_THEME);
    cv_rect(30, 0, 30, 124, T_TEXT);
    base = host_blit_bytes;
    cv_blit(0, 0);
    ok = host_blit_bytes - base == 30u * 124u * 2u;
    bad += check("strip cache: one dirty 30 px group sends that group only", ok);
    /* one 8-row band dirty: only that band goes out */
    cv_cache_drop();
    cv_begin(240, 124, T_BG);
    cv_rect(0, 0, 240, 124, T_THEME);
    cv_blit(0, 0);
    cv_begin(240, 124, T_BG);
    cv_rect(0, 0, 240, 124, T_THEME);
    cv_rect(0, 0, 240, CV_UNIT, T_TEXT);
    base = host_blit_bytes;
    cv_blit(0, 0);
    ok = host_blit_bytes - base == 240u * CV_UNIT * 2u;
    bad += check("strip cache: one dirty 8-row band sends that band only", ok);
    /* palette: the RGB565 values change, so every band goes out again (the canvas is the same) */
    palette_set(1);
    base = host_blit_bytes;
    cv_blit(0, 0);
    ok = host_blit_bytes - base == 240u * 124u * 2u;
    bad += check("strip cache: a palette change sends every band again", ok);
    palette_set(settings.palette);
    return bad;
}

/* 1.4.1 ENGINE QUICK ACTIONS (ui_layer.c EQA): the EDIT layer's black keys are the selected track's engine's. DRUM:
 * black keys 1..8 MUTE its lanes (eng_drum.c drum_mute). qa_beat: T4's own output (track_render) of a beat whose hits
 * strike the lanes of `lanes` (trk_note_on: the keys', MIDI's and the sequencer's way in), 16 steps of 8 blocks; its
 * hash and the energy under ~150 Hz (a one-pole low-pass: the kick's) */
static uint64_t qa_beat(uint32_t lanes, double *low)
{
    static const uint8_t NOTE[NLANE] = {36, 38, 39, 42, 46, 45, 37, 56};
    track_t *t = &trk[3];
    int32_t o[CTL];
    uint32_t s, b, i, l, p;
    uint64_t h = 1469598103934665603ull;
    double lp = 0;
    for (p = 0; p < NPART; p++)
        memset(trk[p].v, 0, sizeof trk[p].v);
    memset(eng_mem, 0, sizeof eng_mem);
    *low = 0;
    for (s = 0; s < 16u; s++) {
        uint32_t hit = (s % 4u == 0u ? 1u : 0u) | (s % 8u == 4u ? 2u : 0u) | (s == 14u ? 4u : 0u) |
                       (s % 2u == 0u ? 8u : 0u) | (s == 6u ? 16u : 0u) | (s == 11u ? 32u : 0u) | (s == 3u ? 64u : 0u) |
                       (s == 9u ? 128u : 0u);
        for (l = 0; l < NLANE; l++)
            if (((hit & lanes) >> l) & 1u)
                trk_note_on(t, NOTE[l], s == 0u ? 120u : 100u);
        for (b = 0; b < 8u; b++) {
            track_render(t, o, CTL);
            for (i = 0; i < CTL; i++) {
                h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
                lp += ((double)o[i] - lp) * 0.02;       /* (~150 Hz at 48 kHz) */
                *low += lp * lp;
            }
        }
        for (l = 0; l < NLANE; l++)
            if (((hit & lanes) >> l) & 1u)
                trk_note_off(t, NOTE[l]);
    }
    return h;
}

static int test_edit_quick_actions(void)
{
    int bad = 0, ok;
    /* EDIT engine hold map removed; EDIT on STEP opens the sound and leaves the step */
    ui_power_on();
    go_home(); frame();
    btn_down(B_EDIT); frames(600);
    ok = !ui.layer && !ui.lock;
    btn_up(B_EDIT); frame();
    ok &= cur_page()->fam == FAM_EDIT && !ui.layer;
    bad += check("EDIT hold never opens the engine quick-action map", ok);

    ui_power_on();
    go_seq_step(); my_steps(TSEL); ui.cursor = 0; frame();
    press(B_EDIT);
    ok = step_on(&TSEL->step[0]) && ui.cursor == 0 && !ui.lock && cur_page()->fam == FAM_EDIT;
    bad += check("EDIT on STEP opens EDIT and leaves the cursor step (no lock)", ok);

    ui_power_on();
    set_engine_of(TSEL, ENGI_DRUM); TSEL->engine = TSEL->eng_req;
    press(B_EDIT); frames(100);
    ok = cur_page()->fam == FAM_EDIT;
    /* drum mute page may live under HOME mixer; EDIT hold mute map is gone */
    btn_down(B_EDIT); key_down(black(0)); frame();
    ok &= !ui.layer || ui.lock == 0;
    key_up(black(0)); btn_up(B_EDIT); frame();
    bad += check("EDIT + lane key with button held is not a sticky mute map", ok);
    return bad;
}


/* A generation gesture owns one lane; undo restores its steps and its density.
 * The same on every drum kit: DRUM-X, DRUMS and CR78. */
static int test_generation_density_undo(void)
{
    step_t before[NSTEP];
    static const uint32_t ENG[3] = {ENGI_DRUM, ENGI_DRUMS, ENGI_SAMPLE};
    int ok = 1;
    uint32_t e, i;
    for (e = 0; e < 3u; e++) {
        ui_power_on();
        set_engine_of(TSEL, ENG[e]); TSEL->engine = TSEL->eng_req;
        if (ENG[e] == ENGI_SAMPLE) TSEL->p[P_E0] = SMP_CR78_INDEX;
        TSEL->p[P_SLEN] = 16;
        grid_hit(TSEL, 0, DV_KICK, 1);
        grid_hit(TSEL, 4, DV_SNARE, 1);
        grid_hit(TSEL, 2, DV_HATC, 1);
        grid_hit(TSEL, 7, DV_TOM, 1);
        memcpy(before, TSEL->step, sizeof before);
        press(B_SEQ); press(B_SEQ);
        turn(EN_K2, -100);
        for (i = 0; i < 16; i++) {
            ok &= !(step_lanes(&TSEL->step[i]) & (1u << DV_KICK));
            ok &= (step_lanes(&TSEL->step[i]) & ~(1u << DV_KICK)) ==
                  (step_lanes(&before[i]) & ~(1u << DV_KICK));
        }
        ok &= TSEL->generation.density[0] == 0 && TSEL->p[P_SLEN] == 16;
        hold(B_SAVE);
        ok &= !memcmp(before, TSEL->step, sizeof before) && TSEL->generation.density[0] == 50;
    }
    return check("GENERATION on DRUM-X, DRUMS and CR78: kick density clears only kicks; SAVE undo restores beat and density", ok);
}

static int test_generation_navigation(void)
{
    int ok;
    ui_power_on();
    set_engine_of(TSEL, ENGI_DRUM); TSEL->engine = TSEL->eng_req;
    press(B_SEQ);
    press(B_SEQ);
    ok = str_eq(cur_page()->title, "GENERATION") && !ui.lock;
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "GENERATION 2") && !ui.lock;
    press(B_SEQ);
    ok &= str_eq(cur_page()->title, "AUTOMATION");

    ui_power_on();                                  /* DRUMS: the second drum engine */
    set_engine_of(TSEL, ENGI_DRUMS); TSEL->engine = TSEL->eng_req;
    press(B_SEQ);
    press(B_SEQ);
    ok &= str_eq(cur_page()->title, "GENERATION") && !ui.lock;
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "GENERATION 2") && !ui.lock;

    ui_power_on();                                  /* CR78: SAMPLE's factory drum kit */
    set_engine_of(TSEL, ENGI_SAMPLE); TSEL->engine = TSEL->eng_req;
    TSEL->p[P_E0] = SMP_CR78_INDEX;
    press(B_SEQ);
    press(B_SEQ);
    ok &= str_eq(cur_page()->title, "GENERATION") && !ui.lock;
    return check("DRUM-X, DRUMS and CR78 SEQ: GENERATION is second section, SELECT opens its map page", ok);
}

static int test_generation_map_display(void)
{
    int ok;
    ui_power_on();
    set_engine_of(TSEL, ENGI_DRUM); TSEL->engine = TSEL->eng_req;
    TSEL->p[P_SLEN] = 16;
    press(B_SEQ); press(B_SEQ);
    turn(EN_K1, 1);
    ok = TSEL->p[P_SLEN] == 17 && TSEL->generation.density[0] == 50;
    turn(EN_SELECT, 1);
    turn(EN_K1, 1);
    ok &= TSEL->generation.x == 192 && TSEL->generation.y == 128;
    turn(EN_K2, 1);
    ok &= generation_map_index(TSEL->generation.x, TSEL->generation.y) == 0;
    ok &= swap16(host_screen[40u * 240u + 50u]) == T_LINE;
    frames(1000);
    ok &= swap16(host_screen[40u * 240u + 50u]) != T_LINE;
    return check("GENERATION: LEN first; MAP presets and CUSTOM XY; full-screen map returns to grid", ok);
}

/* Synth SEQ skips GENERATION; SELECT stays inside STEP / PATTERN / DETAIL. */
static int test_seq_sections(void)
{
    int bad = 0, ok;
    ui_power_on();
    press(B_SEQ);
    ok = str_eq(cur_page()->title, "STEP") && !ui.lock && cur_page()->sec == SEQ_SEC_STEP;
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "PATTERN") && !ui.lock && cur_page()->sec == SEQ_SEC_STEP;
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "DETAIL") && cur_page()->sec == SEQ_SEC_STEP;
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "DETAIL");          /* SELECT stops at the end of the section */
    press(B_SEQ);
    ok &= str_eq(cur_page()->title, "AUTOMATION") && !ui.lock && cur_page()->sec == SEQ_SEC_AUTO;
    press(B_SEQ);
    ok &= str_eq(cur_page()->title, "SEQ TOOLS") && ui.lock == LAYER_SEQ && cur_page()->sec == SEQ_SEC_TOOL;
    press(B_SEQ);
    ok &= str_eq(cur_page()->title, "STEP") && !ui.lock && cur_page()->sec == SEQ_SEC_STEP;
    bad += check("SEQ sections: step group (SELECT STEP>PATTERN>DETAIL) → Automation → SEQ TOOLS (wrap)", ok);
    return bad;
}

/* While playing on the step group, the cursor follows seq_idx; STEP knob pauses ~3 s; idle resumes. */
static int test_seq_follow(void)
{
    int bad = 0, ok;
    track_t *t;
    uint32_t i;
    ui_power_on();
    song.sel = 0;
    t = TSEL;
    t->p[P_SLEN] = 32;
    go_seq_step();
    frame();
    ui.cursor = 0;
    seq_start();
    events_block(CTL);
    t->seq_idx = 5;
    frame();
    ok = song.playing && ui.cursor == 5u && ui.bank == 0u;
    t->seq_idx = 20;
    frame();
    ok &= ui.cursor == 20u && ui.bank == 1u;
    turn(EN_K1, -5);                                    /* pause follow */
    ok &= ui.cursor == 15u && ui.seq_follow_t != 0u;
    t->seq_idx = 22;
    frame();
    ok &= ui.cursor == 15u;                             /* held while paused */
    for (i = 0; i < 200u; i++)
        frame();
    ok &= ui.seq_follow_t == 0u && ui.cursor == 22u;     /* resumed */
    turn(EN_K1, 1);
    ok &= ui.seq_follow_t != 0u;
    turn(EN_K2, 1);                                     /* SEQ edit refreshes the pause clock */
    ok &= ui.seq_follow_t > 100u;
    transport_req = 2;
    events_block(CTL);
    frame();
    ok &= !song.playing && ui.cursor == 23u;             /* stop leaves the cursor */
    bad += check("SEQ follow: tracks seq_idx, STEP pauses, idle resumes, stop keeps cursor", ok);
    return bad;
}

/* OCT++PLAY restarts; OCT++SELECT changes BPM; GLO+SELECT does not. */
static int test_nav_chords(void)
{
    int bad = 0, ok;
    int16_t b0;
    uint32_t page;
    ui_power_on();
    song.playing = 1; transport_req = 0;
    btn_down(B_OCTUP); frame();
    press(B_PLAY);
    ok = transport_req == 3u;
    btn_up(B_OCTUP); frame();
    stop_transport(); transport_req = 0;
    btn_down(B_OCTUP); frame();
    press(B_PLAY);
    ok &= transport_req == 1u;
    btn_up(B_OCTUP); frame();
    bad += check("OCT++PLAY restarts while playing; stopped: PLAY", ok);

    ui_power_on();
    go_title("ENV"); frame();
    page = ui.page;
    b0 = song.g[G_BPM];
    btn_down(B_OCTUP); frame();
    turn(EN_SELECT, 2);
    ok = song.g[G_BPM] == b0 + 2 && ui.bpm_t && ui.page == page;
    btn_up(B_OCTUP); frame();
    bad += check("OCT++SELECT changes BPM; page stays", ok);

    b0 = song.g[G_BPM];
    btn_down(B_GLO); frame();
    turn(EN_SELECT, 2);
    ok = song.g[G_BPM] == b0;
    btn_up(B_GLO); frame();
    bad += check("GLO+SELECT does not change BPM", ok);
    return bad;
}

/* MIXER keys play. A DRUM track stays on MIXER; lane mute is SEQ TOOLS (F4). */
static int test_mixer_solo(void)
{
    int bad = 0, ok;
    uint32_t wk = white(0), bk;
    ui_power_on();
    press(B_HOME);                                      /* Home → Mixer */
    ok = str_eq(cur_page()->title, "MIXER") && cur_page()->sec == TRK_SEC_MIX;
    key_down(wk); frame();
    ok &= perf_solo == 0u && kb_note[wk] != KB_SILENT;
    key_up(wk); frame();
    set_engine_of(TSEL, ENGI_DRUM); TSEL->engine = TSEL->eng_req; frame();
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "MIXER");
    bk = key_at(1, 0);
    key_down(bk); frame();
    ok &= (drum_mute[song.sel % NPART] & 1u) == 0u && kb_note[bk] != KB_SILENT;
    key_up(bk); frame();
    bad += check("MIXER keys play; a DRUM track stays on MIXER", ok);
    return bad;
}

/* Double-tap never locks except Punch-in (FX's second section). */
static int test_no_layer_lock(void)
{
    int bad = 0, ok = 1;
    uint32_t i;
    static const uint8_t B[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
    for (i = 0; i < 4u; i++) {
        ui_power_on();
        go_title("ENV"); frame();
        press(B[i]); frames(64); press(B[i]); frames(200);
        if (B[i] == B_FX)
            ok &= ui.lock == LAYER_FX && cur_page()->sec == FX_SEC_PUNCH; /* Effects → Punch-in */
        else if (B[i] == B_GLO)
            ok &= ui.lock == 0 || cur_page()->sec == GLO_SEC_CLK; /* Master → Clock, no lock */
        else
            ok &= !ui.lock;
    }
    ui_power_on();
    press(B_FX); frames(64); press(B_FX); frame();     /* Effects → Punch-in */
    ok &= ui.lock == LAYER_FX && cur_page()->sec == FX_SEC_PUNCH;
    press(B_GLO); frames(64); press(B_GLO); frames(64); press(B_GLO); frame();
    ok &= !ui.lock && cur_page()->sec == GLO_SEC_SONG; /* Master → Clock → Song */
    bad += check("no layer lock: double-tap SCL/EDIT; Punch-in sticky; GLO has no Record map", ok);
    return bad;
}

/* Araminta nav: FX button cycles sections (Effects → Punch-in); SLICER is an EFFECTS page.
 * SELECT walks pages inside a section. Hold FX alone does not open punch-ins; punch-in keys
 * work only in the Punch-in section. MASTER lives on GLO. */
static int test_fx_sections(void)
{
    int bad = 0, ok;
    uint32_t i, nins, held, saw_slcr;
    ui_power_on();
    press(B_FX);
    ok = !ui.home && cur_page()->fam == FAM_FX && str_eq(cur_page()->title, "EFFECTS");
    for (nins = 0, saw_slcr = 0, i = 0; i < 24u; i++) {
        if (cur_page()->sec != FX_SEC_INS)
            break;
        nins++;
        if (str_eq(cur_page()->title, "SLICER"))
            saw_slcr = 1;
        turn(EN_SELECT, 1);
    }
    ok &= nins >= 9u && saw_slcr;                       /* EFFECTS + stages + DLY REV SLCR */
    ok &= cur_page()->sec == FX_SEC_INS;                /* SELECT stayed in Effects */
    press(B_FX);
    ok &= cur_page()->sec == FX_SEC_PUNCH && ui.lock == LAYER_FX && ui.layer == LAYER_FX;
    key_down(white(4)); frame();                       /* C4: LPF (B3 is 1/2, so the filters sit with the octaves) */
    ok &= (perf_held & PF_BIT(PF_LPF)) != 0u;
    key_up(white(4)); frame();
    turn(EN_SELECT, 1); frame();
    ok &= str_eq(cur_page()->title, "PUNCH-IN") && (perf_held & PF_BIT(PF_LPF)) == 0u;
    key_down(white(4)); frame();                       /* C4 on PUNCH-IN 2/2: PITCH DN */
    ok &= (perf_held & PF_BIT(PF_ODN)) != 0u;
    key_up(white(4)); frame();
    turn(EN_SELECT, -1); frame();
    ok &= str_eq(cur_page()->title, "PUNCH-IN");
    press(B_FX);
    ok &= cur_page()->sec == FX_SEC_INS && str_eq(cur_page()->title, "EFFECTS") && !ui.lock;
    bad += check("FX sections: Effects → Punch-in; SLICER under EFFECTS; punch keys in Punch-in; SELECT walks the two PUNCH-IN pages", ok);
    ui_power_on();
    press(B_GLO);
    ok = str_eq(cur_page()->title, "MASTER") && cur_page()->fam == FAM_GLO && cur_page()->sec == GLO_SEC_MASTER;
    bad += check("MASTER is GLO's first section", ok);

    ui_power_on();
    go_title("EFFECTS");
    ui.ior_sel = 0;
    {
        uint8_t before[FX_LIST_N], after[FX_LIST_N];
        fx_list_get(TSEL, before);
        ok = before[0] == INS_DIST && before[1] == INS_CRUSH && before[2] == INS_FILTER;
        turn(EN_K2, 1);                                 /* ORDER moves the chosen stage (DIST↔CRUSH) */
        fx_list_get(TSEL, after);
        ok &= ui.ior_sel == 1u && before[0] == after[1] && before[1] == after[0] &&
              after[0] == INS_CRUSH && after[1] == INS_DIST;
        /* mid-list move: CRUSH past FILTER (Lehmer must survive non-adjacent ids) */
        ui_power_on();
        go_title("EFFECTS");
        ui.ior_sel = 1;                                 /* CRUSH in the default list */
        fx_list_get(TSEL, before);
        turn(EN_K2, 1);
        fx_list_get(TSEL, after);
        ok &= ui.ior_sel == 2u && before[1] == INS_CRUSH && after[2] == INS_CRUSH &&
              after[1] == before[2] && after[0] == before[0];
        turn(EN_K1, 1);
        ok &= ui.ior_sel == 3u;
        fx_list_get(TSEL, before);
        ok &= !ins_is_send(TSEL, before[3]);
        turn(EN_K3, 1);                                 /* that stage: in the chain → a send (right) */
        fx_list_get(TSEL, after);
        ok &= ins_is_send(TSEL, after[3]) && after[0] == before[0] && after[1] == before[1] &&
              after[2] == before[2];
        turn(EN_K3, -1);                                /* left: back into the chain, and it stays there */
        fx_list_get(TSEL, after);
        ok &= !ins_is_send(TSEL, after[3]);
        {
            uint32_t mix_id = after[3] == INS_DIST ? P_IDMIX : after[3] == INS_FILTER ? P_IFMIX :
                              after[3] == INS_CRUSH ? P_ICMIX : after[3] == INS_PHASR ? P_IPMIX :
                              after[3] == INS_FLANG ? P_ILMIX : P_IHMIX;
            TSEL->p[mix_id] = 0;
            turn(EN_K4, 8);                             /* MIX is that stage's amount */
            ok &= TSEL->p[mix_id] > 0;
        }
        fx_list_get(TSEL, before);
        btn_down(B_FX);
        turn(EN_SELECT, 1);                             /* holding FX does not reorder */
        btn_up(B_FX); frame();
        fx_list_get(TSEL, after);
        ok &= after[0] == before[0] && after[1] == before[1] && after[2] == before[2];
        go_title("EFFECTS");
        {
            uint8_t pr = TSEL->preset;
            turn(EN_PRESET, 1);                         /* PRESETS loads a sound, it does not pick a stage */
            ok &= TSEL->preset != pr && ui.ior_sel == 3u && str_eq(cur_page()->title, "EFFECTS");
        }
    }
    bad += check("EFFECTS: K1 chooses, K2 moves (incl. CRUSH), K3 send or chain, K4 mix; PRESETS loads a sound", ok);

    ui_power_on();
    go_title("EFFECTS");
    {
        uint8_t ord[FX_LIST_N];
        uint32_t i, dly_slot = 0xFFu;
        song.g[G_DLYOWN] = 0;
        TSEL->p[P_DLY] = 40;
        fx_list_get(TSEL, ord);
        for (i = 0; i < FX_LIST_N; i++)
            if (ord[i] == INS_DLY) { dly_slot = i; break; }
        ok = dly_slot < FX_LIST_N && fx_list_mode(TSEL, INS_DLY)[0] == 'S';
        ui.ior_sel = (uint8_t)dly_slot;
        turn(EN_K3, -1);                                /* left: claim delay as CHAIN on track 0 */
        ok &= song.g[G_DLYOWN] == 1 && fx_list_mode(TSEL, INS_DLY)[0] == 'C';
        turn(EN_K3, -1);                                /* already CHAIN: stays (no wrap) */
        ok &= song.g[G_DLYOWN] == 1 && fx_list_mode(TSEL, INS_DLY)[0] == 'C';
        song.sel = 1;
        ok &= fx_list_mode(&trk[1], INS_DLY)[0] == 'U';
        trk[1].p[P_DLY] = 80;
        turn(EN_K4, 8);                                 /* USED: mix ignored */
        ok &= trk[1].p[P_DLY] == 80;
        song.sel = 0;
        ui.ior_sel = (uint8_t)dly_slot;
        turn(EN_K3, 1);                                 /* right: release -> SEND for all */
        ok &= song.g[G_DLYOWN] == 0 && fx_list_mode(&trk[1], INS_DLY)[0] == 'S';
        turn(EN_K3, 1);                                 /* SEND -> MASTER (this track) */
        ok &= fx_master_owned(INS_DLY, 0) && str_eq(fx_list_mode(TSEL, INS_DLY), "MASTER") &&
              str_eq(fx_list_mode(&trk[1], INS_DLY), "USED");
        turn(EN_K3, -1);                                /* MASTER -> SEND again */
        ok &= song.g[G_DLYOWN] == 0 && !fx_master_on(INS_DLY) &&
              fx_list_mode(&trk[1], INS_DLY)[0] == 'S';
        song.g[G_REVOWN] = 0;
        fx_list_get(TSEL, ord);
        for (i = 0; i < FX_LIST_N; i++)
            if (ord[i] == INS_REV) { ui.ior_sel = (uint8_t)i; break; }
        turn(EN_K3, -1);
        ok &= song.g[G_REVOWN] == 1 && fx_list_mode(TSEL, INS_REV)[0] == 'C' &&
              fx_list_mode(&trk[2], INS_REV)[0] == 'S';   /* REV non-owner: SEND (tank 1), not USED */
        {
            int16_t rv = trk[2].p[P_REV];
            song.sel = 2;
            ui.ior_sel = (uint8_t)0;
            fx_list_get(&trk[2], ord);
            for (i = 0; i < FX_LIST_N; i++)
                if (ord[i] == INS_REV) { ui.ior_sel = (uint8_t)i; break; }
            turn(EN_K4, 8);                             /* SEND: mix still editable */
            ok &= trk[2].p[P_REV] != rv;
            turn(EN_K3, -1);                            /* cannot steal CHAIN (left or right) */
            ok &= song.g[G_REVOWN] == 1 && fx_list_mode(&trk[2], INS_REV)[0] == 'S';
            turn(EN_K3, 1);                             /* right: USED stays SEND for this track */
            ok &= song.g[G_REVOWN] == 1 && fx_list_mode(&trk[2], INS_REV)[0] == 'S';
            song.sel = 0;
        }
        song.g[G_DLYOWN] = 1;                           /* DLY still exclusive USED when chained */
        ok &= fx_list_mode(&trk[1], INS_DLY)[0] == 'U';
        /* USED: DELAY page hidden (SELECT skips it; stuck page falls back in the section) */
        song.sel = 1;
        {
            uint32_t saw_dly = 0, k, dly_i = NPAGES;
            go_title("EFFECTS");
            for (k = 0; k < 24u && cur_page()->sec == FX_SEC_INS; k++) {
                if (str_eq(cur_page()->title, "DELAY"))
                    saw_dly = 1;
                turn(EN_SELECT, 1);
            }
            for (k = 0; k < NPAGES; k++)
                if (str_eq(PAGES[k].title, "DELAY") && PAGES[k].fam == FAM_FX) {
                    dly_i = k;
                    break;
                }
            ok &= !saw_dly && dly_i < NPAGES && !page_visible(dly_i);
            go_title("DELAY");
            frame();                                    /* leave the forced DELAY title */
            ok &= !str_eq(cur_page()->title, "DELAY") && cur_page()->fam == FAM_FX &&
                  cur_page()->sec == FX_SEC_INS;
        }
        song.sel = 0;
        song.g[G_DLYOWN] = 0;
    }
    bad += check("EFFECTS: DLY USED when chained; REV non-owners SEND; DELAY page hidden when USED", ok);

    ui_power_on();
    go_title("EFFECTS");
    {
        uint8_t ord[FX_LIST_N];
        uint32_t i, has_chor = 0, has_dly = 0, has_rev = 0, has_slcr = 0, has_tape = 0, sl_slot = 0xFFu, tp_slot = 0xFFu;
        fx_list_get(TSEL, ord);
        for (i = 0; i < FX_LIST_N; i++) {
            has_chor |= ord[i] == INS_CHOR;
            has_dly |= ord[i] == INS_DLY;
            has_rev |= ord[i] == INS_REV;
            has_slcr |= ord[i] == INS_SLCR;
            has_tape |= ord[i] == INS_TAPE;
            if (ord[i] == INS_SLCR)
                sl_slot = i;
            if (ord[i] == INS_TAPE)
                tp_slot = i;
        }
        ok = FX_LIST_N == 10u && has_chor && has_dly && has_rev && has_slcr && has_tape &&
             str_eq(N_ISTAGE[INS_DIST], "DISTORT") && str_eq(N_ISTAGE[INS_PHASR], "PHASER") &&
             str_eq(N_ISTAGE[INS_FLANG], "FLANGER") && str_eq(N_ISTAGE[INS_CHOR], "CHORUS") &&
             str_eq(N_ISTAGE[INS_DLY], "DELAY") && str_eq(N_ISTAGE[INS_REV], "REVERB") &&
             str_eq(N_ISTAGE[INS_SLCR], "SLICER") && str_eq(N_ISTAGE[INS_TAPE], "TAPE") &&
             str_eq(fx_list_mode(TSEL, INS_DLY), "SEND") &&
             str_eq(fx_list_mode(TSEL, INS_SLCR), "SEND") &&
             str_eq(fx_list_mode(TSEL, INS_TAPE), "SEND") &&
             text_w(&AF_S, "FLANGER") == text_w(&AF_S, "DISTORT") &&
             text_w(&AF_S, N_ISTAGE[INS_DIST]) <= text_w(&AF_S, "FLANGER") &&
             text_w(&AF_S, N_ISTAGE[INS_PHASR]) <= text_w(&AF_S, "FLANGER") &&
             text_w(&AF_S, N_ISTAGE[INS_CHOR]) <= text_w(&AF_S, "FLANGER") &&
             text_w(&AF_S, N_ISTAGE[INS_DLY]) <= text_w(&AF_S, "FLANGER") &&
             text_w(&AF_S, N_ISTAGE[INS_REV]) <= text_w(&AF_S, "FLANGER") &&
             text_w(&AF_S, N_ISTAGE[INS_SLCR]) <= text_w(&AF_S, "FLANGER");
        ui.ior_sel = 0;
        ok &= cur_page()->graph == GR_IORD;
        for (i = 0; i < NPAGES; i++)
            if (str_eq(PAGES[i].title, "TAPE"))
                ok &= PAGES[i].id[0] == G_TSPEED && PAGES[i].id[1] == G_TWOW &&
                      PAGES[i].id[2] == G_TDROP && PAGES[i].id[3] == G_TTEMPO;   /* MIX stays on EFFECTS */
        /* SLICER: K3 toggles this track SEND/CHAIN; other tracks keep their own mode (no USED) */
        if (sl_slot < FX_LIST_N) {
            int16_t mix0;
            song.g[G_SLOWN] = 0;                          /* legacy all-SEND → sanitize on toggle */
            TSEL->p[P_SLDEPTH] = 40;
            ui.ior_sel = (uint8_t)sl_slot;
            turn(EN_K3, -1);                                /* left: this track into the chain */
            ok &= sl_trk_chain(TSEL) && fx_list_mode(TSEL, INS_SLCR)[0] == 'C';
            song.sel = 1;
            ok &= sl_trk_send(&trk[1]) && fx_list_mode(&trk[1], INS_SLCR)[0] == 'S';
            trk[1].p[P_SLDEPTH] = 40;
            mix0 = trk[1].p[P_SLDEPTH];
            ui.ior_sel = (uint8_t)sl_slot;
            turn(EN_K4, 8);                             /* track 1 still SEND: MIX edits */
            ok &= trk[1].p[P_SLDEPTH] != mix0;
            song.sel = 0;
            ui.ior_sel = (uint8_t)sl_slot;
            turn(EN_K3, 1);                             /* track 0 back to SEND */
            ok &= sl_trk_send(TSEL) && fx_list_mode(TSEL, INS_SLCR)[0] == 'S';
            mix0 = TSEL->p[P_SLDEPTH];
            turn(EN_K4, 8);
            ok &= TSEL->p[P_SLDEPTH] != mix0 && str_eq(TP[P_SLDEPTH].label, "MIX");
        } else {
            ok = 0;
        }
        /* TAPE: K3 goes CHAIN (this track) -> SEND -> MASTER and clamps at both ends; others USED */
        if (tp_slot < FX_LIST_N) {
            ui.ior_sel = (uint8_t)tp_slot;
            turn(EN_K3, 1);                             /* SEND -> MASTER */
            ok &= song.g[G_TAPEMODE] == TAPE_MASTER && str_eq(fx_list_mode(TSEL, INS_TAPE), "MASTER");
            {   /* MASTER's K4 is the one global amount (G_TMIX); the track's P_TAPEMIX stays put */
                int16_t gm = song.g[G_TMIX], pm = TSEL->p[P_TAPEMIX];
                turn(EN_K4, 8);
                ok &= song.g[G_TMIX] != gm && TSEL->p[P_TAPEMIX] == pm;
            }
            turn(EN_K3, 1);                             /* MASTER: right stays MASTER (no wrap) */
            ok &= song.g[G_TAPEMODE] == TAPE_MASTER;
            turn(EN_K3, -1);                            /* MASTER -> SEND */
            ok &= song.g[G_TAPEMODE] == TAPE_SEND;
            turn(EN_K3, -1);                            /* SEND -> CHAIN (track 0) */
            ok &= song.g[G_TAPEMODE] == TAPE_CH1 && str_eq(fx_list_mode(TSEL, INS_TAPE), "CHAIN");
            turn(EN_K3, -1);                            /* CHAIN: left stays CHAIN */
            ok &= song.g[G_TAPEMODE] == TAPE_CH1;
            turn(EN_K3, 1);                             /* CHAIN -> SEND */
            ok &= song.g[G_TAPEMODE] == TAPE_SEND;
            song.sel = 1;
            ui.ior_sel = (uint8_t)tp_slot;
            turn(EN_K3, -1);                            /* SEND -> CHAIN (track 1) */
            ok &= song.g[G_TAPEMODE] == TAPE_CH2;
            song.sel = 0;
            ok &= str_eq(fx_list_mode(TSEL, INS_TAPE), "USED");
            song.g[G_TAPEMODE] = TAPE_SEND;
            {   /* SEND keeps time: TEMPO is fixed ON, so K4 cannot turn it */
                go_title("TAPE");
                song.g[G_TTEMPO] = 0;
                turn(EN_K4, 8);
                ok &= song.g[G_TTEMPO] == 0;
                song.g[G_TTEMPO] = 1;
                go_title("EFFECTS");
            }
        } else {
            ok = 0;
        }
        /* MASTER on an insert: owner track MASTER, others keep CHAIN / SEND; only the owner clears it */
        {
            uint8_t o[FX_LIST_N];
            uint32_t i, dist = 0xFFu;
            fx_list_get(TSEL, o);
            for (i = 0; i < FX_LIST_N; i++)
                if (o[i] == INS_DIST) { dist = i; break; }
            song.sel = 0;
            ui.ior_sel = (uint8_t)dist;
            turn(EN_K3, 1);                             /* CHAIN -> SEND */
            turn(EN_K3, 1);                             /* SEND -> MASTER */
            ok &= fx_master_owned(INS_DIST, 0) && str_eq(fx_list_mode(TSEL, INS_DIST), "MASTER");
            song.sel = 1;
            ins_send_set(TSEL, INS_DIST, 1);            /* this track stays SEND beside the master */
            ok &= !fx_master_owned(INS_DIST, 1) && str_eq(fx_list_mode(TSEL, INS_DIST), "SEND");
            ui.ior_sel = (uint8_t)dist;
            turn(EN_K3, 1);                             /* another track cannot take MASTER */
            ok &= !fx_master_owned(INS_DIST, 1) && fx_master_owned(INS_DIST, 0);
            turn(EN_K3, -1);                            /* but its own CHAIN / SEND still moves */
            ok &= str_eq(fx_list_mode(TSEL, INS_DIST), "CHAIN");
            song.sel = 0;
            ui.ior_sel = (uint8_t)dist;
            turn(EN_K3, -1);                            /* owner: MASTER -> its per-track route */
            ok &= !fx_master_on(INS_DIST);
        }
        /* MASTER on a shared bus: owner MASTER, others USED, only the owner clears it */
        {
            song.sel = 0;
            ui.ior_sel = (uint8_t)0;
            fx_list_get(TSEL, ord);
            for (i = 0; i < FX_LIST_N; i++)
                if (ord[i] == INS_DLY) { ui.ior_sel = (uint8_t)i; break; }
            song.g[G_DLYOWN] = 0;
            turn(EN_K3, 1);                             /* SEND -> MASTER */
            ok &= fx_master_owned(INS_DLY, 0) && str_eq(fx_list_mode(TSEL, INS_DLY), "MASTER");
            song.sel = 1;
            ok &= str_eq(fx_list_mode(&trk[1], INS_DLY), "USED");
            turn(EN_K3, -1);                            /* not the owner: nothing */
            ok &= fx_master_owned(INS_DLY, 0);
            song.sel = 0;
            ui.ior_sel = (uint8_t)0;
            fx_list_get(TSEL, ord);
            for (i = 0; i < FX_LIST_N; i++)
                if (ord[i] == INS_DLY) { ui.ior_sel = (uint8_t)i; break; }
            turn(EN_K3, -1);
            ok &= !fx_master_on(INS_DLY) && str_eq(fx_list_mode(TSEL, INS_DLY), "SEND");
        }
    }
    bad += check("EFFECTS list: 10 rows with SLCR/TAPE; K3 routes TAPE SEND/CHAIN/MASTER", ok);

    /* SELECT walks stage pages in EFFECTS list order (not static PAGES[]). */
    ui_power_on();
    go_title("EFFECTS");
    {
        uint8_t ord[FX_LIST_N];
        uint32_t i;
        fx_list_get(TSEL, ord);
        ok = str_eq(cur_page()->title, "EFFECTS");
        for (i = 0; i < FX_LIST_N; i++) {
            turn(EN_SELECT, 1);
            ok &= str_eq(cur_page()->title, N_ISTAGE[ord[i]]);
        }
        go_title("EFFECTS");
        ui.ior_sel = 0;
        turn(EN_K2, 1);                                 /* swap slots 0 and 1 */
        fx_list_get(TSEL, ord);
        ok &= ord[0] == INS_CRUSH && ord[1] == INS_DIST;
        for (i = 0; i < FX_LIST_N; i++) {
            turn(EN_SELECT, 1);
            ok &= str_eq(cur_page()->title, N_ISTAGE[ord[i]]);
        }
    }
    bad += check("EFFECTS SELECT pages follow the list order (incl. after K2 reorder)", ok);

    /* SLICER playhead: SEND (default) advances sl_bus; graph_sig must track it. */
    ui_power_on();
    go_title("SLICER");
    {
        uint32_t i, sig0, changed = 0;
        int32_t o[2 * CTL];
        ok = sl_trk_send(TSEL) && cur_page()->graph == GR_SLCR;
        fx_bus_trk.p[P_SLCR] = SL_GATE;
        fx_bus_trk.p[P_SLPAT] = 1;
        fx_bus_trk.p[P_SLRATE] = 1;
        TSEL->p[P_SLDEPTH] = 100;
        TSEL->p[P_SLCR] = SL_OFF;                       /* track MODE unused on SEND */
        song.g[G_BPM] = 180;
        slicer_start();
        transport_req = 1;
        ui.force = 1;
        draw_graph();
        ui.force = 0;
        sig0 = ui.graph_sig;
        for (i = 0; i < 400u; i++) {
            mix_block(o, CTL);
            draw_graph();
            if (ui.graph_sig != sig0) {
                changed = 1;
                break;
            }
        }
        transport_req = 2;
        ok &= changed && sl_state(TSEL) == &sl_bus;
    }
    bad += check("SLICER SEND: playhead graph_sig advances with sl_bus", ok);

    ui_power_on();
    go_title("DELAY");
    ok = song.g[G_DTIME] == 1 && !dtime_free();          /* 1/8, RATE */
    turn(EN_K1, -1);                                     /* 1/8D, still a division (triplets and dotted stay) */
    ok &= song.g[G_DTIME] == 10 && !dtime_free();
    turn(EN_K4, 1);                                      /* right: still RATE */
    ok &= !dtime_free() && song.g[G_DTIME] == 10;
    turn(EN_K4, -1);                                     /* left: TIME, free ms, the view's first knob */
    {
        uint32_t ms = dtime_ms();
        ok &= dtime_free() && ms >= 1u && ms <= DTIME_MS_MAX;
        turn(EN_K1, 1);                                  /* one detent is one millisecond */
        ok &= dtime_ms() == ms + 1u;
        fm1_ms += 60u - 16u; frame();                   /* a real turn: slower than a detent a frame */
        turn(EN_K1, 1);
        ok &= dtime_ms() == ms + 2u;
        song.g[G_DTIME] = (int16_t)(DTIME_FREE + 100);
        ok &= dtime_ms() == 100u && delay_samples() == (dtime_q8() + 128u) >> 8;
        ok &= delay_samples() > 16u && delay_samples() < DLY_LEN;
        {   /* slow: 1 ms a detent; fast flick: tens of ms a detent (accel on the free-ms range) */
            uint32_t k, ms0;
            fm1_ms += 1000;
            for (k = 0; k < 8u; k++) {
                host_enc[panel.enc[EN_K1]] += panel.dir[EN_K1];
                fm1_ms += 60u - 16u;
                frame();
            }
            ok &= dtime_ms() == 108u;
            song.g[G_DTIME] = (int16_t)(DTIME_FREE + 100);
            ms0 = dtime_ms();
            fm1_ms += 1000;
            for (k = 0; k < 8u; k++) {
                host_enc[panel.enc[EN_K1]] += panel.dir[EN_K1];
                fm1_ms += 10u - 16u;
                frame();
            }
            ok &= dtime_ms() >= ms0 + 100u;
        }
        turn(EN_K4, 1);                                  /* right: RATE again, the division we left */
        ok &= !dtime_free() && song.g[G_DTIME] == 10;
    }
    bad += check("DLY: K4 left TIME / right RATE reloads K1; divisions keep dotted; free ms steps by 1", ok);

    ui_power_on();
    go_title("CRUSH");
    TSEL->p[P_ICBITS] = 64;
    TSEL->p[P_ICRATE] = 96;
    turn(EN_K1, 1);
    ok = TSEL->p[P_ICBITS] == 72;                        /* one detent: one BITS label (8 units) */
    turn(EN_K1, -1);
    ok &= TSEL->p[P_ICBITS] == 64;
    turn(EN_K2, 1);
    ok &= TSEL->p[P_ICRATE] == 104;
    turn(EN_K2, -1);
    ok &= TSEL->p[P_ICRATE] == 96;
    TSEL->p[P_ICBITS] = 40;                              /* off a label boundary: snap to the next named step */
    turn(EN_K1, 1);
    ok &= TSEL->p[P_ICBITS] == 48;
    bad += check("CRUSH: BITS and RATE one label per detent", ok);

    ui_power_on();
    btn_down(B_FX); frames(600);
    held = ui.layer;
    btn_up(B_FX); frame();
    ok = !held && !ui.lock && (ui.home || cur_page()->fam == FAM_FX);
    key_down(white(4)); frame();
    ok &= !(perf_held & PF_BIT(PF_LPF));
    key_up(white(4)); frame();
    bad += check("FX hold alone never opens punch-ins; keys inert outside Punch-in section", ok);
    return bad;
}

static int test_wt_graph(void)
{
    int bad = 0;
    uint32_t i, p1 = NPAGES, p2 = NPAGES;
    uint32_t s0, s1, s2, sh;
    ui_power_on();
    set_engine_of(TSEL, (uint32_t)ENGI_WT);
    for (i = 0; i < NPAGES; i++) {
        if (str_eq(PAGES[i].title, "EDIT 1")) p1 = i;
        if (str_eq(PAGES[i].title, "EDIT 2")) p2 = i;
    }
    if (p1 < NPAGES) { ui.home = 0; ui.page = (uint8_t)p1; page_entered(); frame(); }
    s0 = graph_signature();
    TSEL->p[P_E1] = 64u;
    if (p1 < NPAGES) { ui.force = 1; frame(); }
    s1 = graph_signature();
    if (p2 < NPAGES) { ui.home = 0; ui.page = (uint8_t)p2; page_entered(); frame(); }
    TSEL->p[P_E7] = 5u;
    if (p2 < NPAGES) { ui.force = 1; frame(); }
    s2 = graph_signature();
    ui.hot_t = 40; ui.hot_col = 3;                     /* RATIO, EDIT 2's KNOB 4: the graph highlights A */
    if (p2 < NPAGES) { ui.force = 1; frame(); }
    sh = graph_signature();
    bad += check("WAVETABLE graph: EDIT 1/2 draw and redraw on POS / RATIO / the hot knob",
                 p1 < NPAGES && p2 < NPAGES && s0 != s1 && s1 != s2 && s2 != sh);
    return bad;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int bad = 0;
    bad += test_fx_sections();
    bad += test_seq_sections();
    bad += test_generation_navigation();
    bad += test_generation_density_undo();
    bad += test_generation_map_display();
    bad += test_seq_follow();
    bad += test_nav_chords();
    bad += test_no_layer_lock();
    bad += test_mixer_solo();
    bad += test_large_face();
    bad += test_sound_loads();
    bad += test_patterns();
    bad += test_rec();
    bad += test_step_rec();
    bad += test_step_preview_chord_entry();
    bad += test_presets_knob();
    bad += test_midi();
    bad += test_save();
    bad += test_actions();
    bad += test_clear_selected_all();
    bad += test_clear_column_keys();
    bad += test_clear_every_track_all();
    bad += test_tracks();
    bad += test_grid();
    bad += test_screen();
    bad += test_favorites();
    bad += test_categories();
    bad += test_display_preferences();
    bad += test_menu_tabs();
    bad += test_menu_project_rows();
    bad += test_information();
    bad += test_chain();
    bad += test_song_timeline();
    bad += test_song_lanes();
    bad += test_song_lanes_drawn();
    bad += test_song_pictures();
    bad += test_head_slot();
    bad += test_product_ux();
    bad += test_page_cycles();
    bad += test_mono_screens();
    bad += test_roll();
    bad += test_soak_cards();
    bad += test_panel();
    bad += test_layer();
    bad += test_layer_knob_leak();
    bad += test_layer_knob_race();
    bad += test_fx_latch();
    bad += test_fx_map();
    bad += test_layer_lock();
    bad += test_rec_layer();
    bad += test_seq_tools();
    bad += test_edit_quick_actions();
    bad += test_menu_prefs();
    bad += test_style();
    bad += test_head_centres();
    bad += test_head_metronome();
    bad += test_div_order();
    bad += test_knob_accel();
    bad += test_usb_level();
    bad += test_click_menu();
    bad += test_usb_serial();
    bad += test_select_pages();
    bad += test_sound_dice();
    bad += test_large();
    bad += test_browse_no_pattern();
    bad += test_name();
    bad += test_px_probe(); bad += test_edit_cycle();
    /* test_slices: SLICE retired, stub returns 0 */
    bad += test_slices();
    bad += test_sample_alert();
    bad += test_quick_layers();
    bad += test_chord_page();
    bad += test_scale_lock();
    bad += test_bughunt_ui();
    bad += test_bughunt_ui2();
    bad += test_piano_roll();
    bad += test_play_leds();
    bad += test_cursor_step_leds();
    bad += test_midi_leds();
    bad += test_seq_quant();
    bad += test_idle_glow();
    bad += test_breath();
    bad += test_step_leds();
    bad += test_scale_leds();
    bad += test_screen_off();
    bad += test_idle_leds();
    bad += test_momentary();
    bad += test_fm6_charts();
    bad += test_fm6_ops();
    bad += test_home_tracks();
    bad += test_fx_map_columns();
    bad += test_help();
    bad += test_no_flash();
    bad += test_live_graph_rate();
    bad += test_strip_cache();
    bad += test_wt_graph();
    bad += test_tape_locks();
#if FELUCCA_FM4
    bad += test_fm_charts();                        /* (DIGITAL's charts: built with FELUCCA_FM4=1 only) */
#else
    bad += test_fm4_retired();
#endif

    if (getenv("UI_SCREEN_DIR")) chain_screens(getenv("UI_SCREEN_DIR"));
    printf(bad ? "ui: %d FAILED\n" : "ui: all passed\n", bad);
    return bad ? 1 : 0;
}
#endif
