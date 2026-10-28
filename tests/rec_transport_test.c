/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* REC and PLAY through the panel, while USB audio records at 44.1 or 48 kHz.
 * Track changes must not strand the REC LED or click after every track is disarmed;
 * pause must silence the click, including across INT/USB clock changes. Only the
 * display, panel scan, DAC hardware and USB packet consumer are simulated. */
#define FELUCCA_UAC 1
#define UI_TEST_NO_MAIN
#include "ui_test.c"

#define FM1_AUDIO_HALF 0x80u
static volatile uint32_t t5_nested_ticks;
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return 0; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *b, uint32_t n, void (*isr)(void), uint32_t p)
{ (void)b; (void)n; (void)isr; (void)p; }
static void fm1_audio_amp(int on) { (void)on; }
void isr_alnk0(void) {}
/* ui_test.c has the UI breadcrumb stub; audio.c needs its full diagnostics. */
#define felucca_dbg rec_audio_dbg
#include "../firmware/src/audio.c"
#undef felucca_dbg

static uint32_t click_samples, usb_packets;

/* Advance real audio blocks and consume USB packets at 1 kHz, as a recorder does.
 * The pattern has no sounding notes, so any DAC output is the metronome. */
static void render(uint32_t blocks)
{
    uint32_t b, i, packet[UA_MAXF + 2u];
    int32_t out[2u * CTL];
    for (b = 0; b < blocks; b++) {
        uint32_t before = fm1_ms;
        uac_render_start();
        audio_block(out, CTL);
        for (i = 0; i < 2u * CTL; i++)
            click_samples += out[i] != 0;
        host_ticks += CTL * 1000000u / FS;
        fm1_ms = host_ticks / 1000u;
        while (before++ < fm1_ms) {
            uac_packet(packet);
            usb_packets++;
        }
    }
}

/* Let the audio ISR see both edges, rather than processing only after release. */
static void tap(uint32_t button)
{
    btn_down(button); frame(); render(24);
    btn_up(button); frame(); render(24);
}

static void fresh(uint32_t rate)
{
    uint32_t i;
    ui_power_on();
    host_ticks = fm1_ms = 0;
    ft_on = ft_closed = ft_btn_prev = 0;
    memset(&midi_clock, 0, sizeof midi_clock);
    memset(&clk, 0, sizeof clk);
    memset(&uac, 0, sizeof uac);
    ua_r = ua_w = 0;
    click_req = 0;
    boot_fade = 32768u;
    song.master_q12 = 4096u;
    song.g[G_BPM] = 120;
    for (i = 0; i < NTRK; i++) {
        track_defaults_steps(&trk[i]);
        trk[i].p[P_DIST] = trk[i].p[P_CHOR] = trk[i].p[P_DLY] = trk[i].p[P_REV] = 0;
    }
    /* A silent hit makes the project nonempty, so REC uses live recording. */
    trk[0].step[0].n = 1;
    trk[0].step[0].time = ST_REST;
    menu_put(MI_CLICK, CLICK_REC);
    menu_put(MI_COUNTIN, 0);
    frame(); ui_leds();
    /* The UI host stub has no physical key map; assign REC its own LED. */
    led_pos[panel.btn[B_REC]] = 2;
    usb.config = 1;
    uac_rate_set(rate);
    uac_stream(1);
    uac.flowing = 1;
    click_samples = usb_packets = 0;
}

static int rec_lit(void)
{
    ui_leds();
    return (fm1_led[0] & 4u) != 0;
}

static int test_disarm_pause(uint32_t rate, uint32_t other)
{
    uint32_t i;
    int bad = 0, steady = 1, dark = 1;
    fresh(rate);
    tap(B_REC);
    render(FS / CTL);
    bad += check("REC starts live recording and audible clicks while USB audio is consumed",
                 song.playing && song.rec == 1u && !seq_counting() && click_samples && usb_packets && uac.feed);
    if (other) {
        turn(EN_ALGO, 1);
        tap(B_REC); tap(B_REC);
        bad += check("REC on another track toggles that track; the original stays armed",
                     song.sel == 1u && song.rec == 1u && song.playing);
        dark = 1;
        for (i = 0; i < 20u; i++) { render(24); dark &= !rec_lit(); }
        bad += check("REC armed on another track: the LED follows the selected track (dark)",
                     song.rec == 1u && dark);
        tap(B_PLAY); render(FS / CTL / 10u);
        dark = 1;
        for (i = 0; i < 20u; i++) { render(24); dark &= !rec_lit(); }
        bad += check("..nor blinks for it while the selected track is stopped",
                     !song.playing && song.rec == 1u && dark);
        tap(B_PLAY); render(FS / CTL);
        turn(EN_ALGO, -1);
    }
    for (i = 0; i < 20u; i++) { render(24); steady &= rec_lit(); }
    bad += check("REC remains steady while recording, across blink phases", steady);
    tap(B_REC);
    render(FS / CTL / 10u);                         /* let the click's 35 ms tail decay */
    click_samples = 0;
    for (i = 0; i < 60u; i++) { render(24); dark &= !rec_lit(); }
    bad += check("disarming the last track stops clicks and leaves REC dark while playback continues",
                 song.playing && !song.rec && !click_samples && dark);
    midi_in_event(0xFC0Fu); render(1);
    bad += check("USB MIDI Stop does not stop the internal clock", song.playing);
    tap(B_REC);
    tap(B_PLAY);
    render(FS / CTL / 10u);
    click_samples = 0;
    render(FS / CTL);
    bad += check("pause silences the metronome while keeping the track armed",
                 !song.playing && song.rec == 1u && !click_samples);
    tap(B_REC);
    dark = 1;
    for (i = 0; i < 60u; i++) { render(24); dark &= !rec_lit(); }
    bad += check("REC after pause disarms; blinking and clicks stop",
                 !song.playing && !song.rec && dark && !click_samples);
    midi_in_event(0xFA0Fu); render(1);
    bad += check("USB MIDI Start does not restart the internal clock", !song.playing);
    return bad;
}

static int test_clock_switch(uint32_t rate)
{
    uint32_t i;
    int bad = 0, lit = 0, dark = 0;
    fresh(rate);
    tap(B_REC);
    go_title("CLOCK");
    turn(EN_K3, 1); render(1);
    bad += check("INT to USB clock stops playback and leaves recording armed",
                 song.g[G_CLOCK] == 1 && !song.playing && song.rec == 1u);
    render(FS / CTL / 10u);
    click_samples = 0;
    for (i = 0; i < 60u; i++) {
        render(24);
        if (rec_lit()) lit = 1; else dark = 1;
    }
    bad += check("armed and stopped in USB clock: REC blinks, the metronome is silent",
                 lit && dark && !click_samples);
    tap(B_PLAY);
    bad += check("PLAY in USB clock starts playback with the track still armed", song.playing && song.rec == 1u);
    render(FS / CTL);
    bad += check("USB clock without incoming pulses stops playback again", !song.playing && song.rec == 1u);
    midi_in_event(0xFA0Fu); render(1);
    bad += check("USB MIDI Start controls playback when USB clock is selected", song.playing);
    midi_in_event(0xFC0Fu); render(FS / CTL / 10u);
    click_samples = 0;
    for (i = 0; i < 48u; i++) {
        midi_in_event(0xF80Fu); render(29);
    }
    bad += check("USB MIDI Stop silences clicks; subsequent clock pulses cannot restart them",
                 !song.playing && !click_samples && song.rec == 1u);
    tap(B_REC);
    dark = 1;
    for (i = 0; i < 60u; i++) { render(24); dark &= !rec_lit(); }
    bad += check("REC disarms after the clock switch: no stuck blinking or clicks",
                 !song.rec && !song.playing && dark && !click_samples);
    turn(EN_K3, -1); render(1);
    bad += check("returning to INT leaves playback stopped and recording disarmed",
                 !song.g[G_CLOCK] && !song.playing && !song.rec);
    return bad;
}

int main(void)
{
    int bad = 0;
    bad += test_disarm_pause(44100u, 0);
    bad += test_disarm_pause(44100u, 1);
    bad += test_disarm_pause(48000u, 0);
    bad += test_disarm_pause(48000u, 1);
    bad += test_clock_switch(44100u);
    bad += test_clock_switch(48000u);
    printf(bad ? "rec transport: %d FAILED\n" : "rec transport: all passed\n", bad);
    return bad != 0;
}
