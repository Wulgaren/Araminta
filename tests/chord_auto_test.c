/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Adapted for Araminta from Melodee f4fdbc7. */
/* AUTO chords (CHRD = AUTO): the key picks an in-scale chord, following what the other tracks have
 * played recently and the previous chord. Against the real input, sequencer, ARP, recorder, MIDI
 * ownership, project codec and chord display. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static uint32_t failures;
static void expect(const char *name, int ok)
{
    printf("AUTO: %-72s %s\n", name, ok ? "PASS" : "FAIL");
    failures += !ok;
}
static track_t *setup(void)
{
    ui_power_on();
    memset(kb_chn, 0, sizeof kb_chn); memset(mchord, 0, sizeof mchord); memset(chord_last, 0, sizeof chord_last);
    memset(kb_strum, 0, sizeof kb_strum); kb_strum_pending = midi_strum_pending = 0;
    memset(chord_auto, 0, sizeof chord_auto); memset(chord_auto_hist, 0, sizeof chord_auto_hist);
    chord_auto_clock = chord_auto_enabled = 0;
    fm1_in.notes = kb_prev = 0; mo_w = mo_r = 0; usb.config = 1;
    track_t *t = &trk[0];
    t->p[P_VOICE] = V_POLY; t->p[P_SCALE] = 0; t->p[P_ROOT] = t->p[P_TRANS] = 0;
    t->p[P_CHRD] = CH_AUTO; t->p[P_QUANT] = QN_OFF; t->p[P_VOIC] = VC_CLOSE;
    events_block(CTL);
    return t;
}
static uint32_t ngated(const track_t *t)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < NVOICE; i++) n += t->v[i].active && t->v[i].gate;
    return n;
}
static uint32_t pcs(const uint8_t *out, uint32_t n)
{
    uint32_t mask = 0;
    for (uint32_t i = 0; i < n; i++) mask |= 1u << (out[i] % 12u);
    return mask;
}
static void chord_expect(track_t *t, uint32_t input, const uint8_t *want, uint32_t count, const char *name)
{
    uint8_t out[4]; int32_t root; uint16_t mask; char text[12];
    uint32_t n = chord_make(t, input, out, &root, &mask);
    chord_auto_name(text, (uint32_t)root, mask);
    int ok = n == count && !memcmp(out, want, count) && str_eq(text, name);
    if (!ok) {
        printf("  %s: %s", name, text);
        for (uint32_t i = 0; i < n; i++) printf(" %u", out[i]);
        printf(" (wanted %u notes)\n", count);
    }
    expect(name, ok);
}
static void musical_examples(void)
{
    track_t *t = setup();
    static const uint8_t major[7][3] = {{60,64,67},{62,65,69},{64,67,71},{65,69,72},{67,71,74},{69,72,76},{71,74,77}};
    static const char *names[] = {"C","Dm","Em","F","G","Am","Bdim"};
    for (uint32_t i = 0; i < 7u; i++) chord_expect(t, major[i][0], major[i], 3, names[i]);
    t->p[P_SCALE] = 1; t->p[P_ROOT] = 9;
    chord_expect(t, 69, (const uint8_t[]){69,72,76}, 3, "Am");
    chord_expect(t, 64, (const uint8_t[]){64,67,71}, 3, "Em");
    t->p[P_SCALE] = 6;
    chord_expect(t, 64, (const uint8_t[]){64,68,71}, 3, "E");
    t->p[P_SCALE] = 2; t->p[P_ROOT] = 2;
    chord_expect(t, 67, (const uint8_t[]){67,71,74}, 3, "G");
    t->p[P_ROOT] = 0; t->p[P_SCALE] = 4;
    chord_expect(t, 60, (const uint8_t[]){60,64,67}, 3, "C");
    chord_expect(t, 62, (const uint8_t[]){62,67,69}, 3, "Dsus4");
    chord_expect(t, 69, (const uint8_t[]){69,72,76}, 3, "Am");
    t->p[P_SCALE] = 5;
    chord_expect(t, 60, (const uint8_t[]){60,63,67}, 3, "Cm");
    t->p[P_SCALE] = 12;
    chord_expect(t, 60, (const uint8_t[]){60,64,68}, 3, "Caug");
    t->p[P_SCALE] = 11;
    chord_expect(t, 66, (const uint8_t[]){66,70,75}, 3, "F# set");
    /* ROOT only, as DIA3/DIA7: TRN moves the played note, the scale stays on ROOT.
     * C# is out of C major, so it takes C; D is the second degree, Dm. */
    t->p[P_SCALE] = 0; t->p[P_TRANS] = 1; t->p[P_QUANT] = QN_ALL;
    chord_expect(t, kb_map(t, 7), (const uint8_t[]){60,64,67}, 3, "C");
    t->p[P_TRANS] = 2;
    chord_expect(t, kb_map(t, 7), (const uint8_t[]){62,65,69}, 3, "Dm");
    t->p[P_TRANS] = 0;
    expect("musical examples", 1);
}
static void every_scale(void)
{
    track_t *t = setup();
    uint32_t cases = 0, bad = 0;
    for (uint32_t s = 0; s < NELEM(SCALE_MASK); s++)
        for (int root = 0; root < 12; root++)
            for (uint32_t v = 0; v <= VC_BASS; v++) {
                t->p[P_SCALE] = (int16_t)s; t->p[P_ROOT] = (int16_t)root; t->p[P_TRANS] = 0; t->p[P_VOIC] = (int16_t)v;
                uint32_t allowed = chord_auto_rotate(scale_mask(t), root);
                for (uint32_t note = 0; note < 128u; note++) {
                    uint8_t out[4], again[4];
                    int32_t r, r2;
                    uint16_t mask, mask2;
                    uint32_t n = chord_make(t, note, out, &r, &mask);
                    uint32_t n2 = chord_make(t, note, again, &r2, &mask2);
                    bad += !(n >= 1u && n <= 4u && n == n2 && r == r2 && mask == mask2 && !memcmp(out, again, n));
                    bad += !!(pcs(out, n) & ~allowed);
                    if (n >= 3u) bad += !(pcs(out, n) & (1u << (r % 12)));
                    for (uint32_t j = 0; j < n; j++) {
                        bad += out[j] > 127u;
                        if (j) bad += out[j] <= out[j - 1u];
                    }
                    cases++;
                }
            }
    printf("AUTO: %u cases across %u scales, every root, voicing and MIDI input\n", cases, (unsigned)NELEM(SCALE_MASK));
    expect("complete, ordered, deterministic and strictly in scale", !bad);
}
static void context(void)
{
    track_t *t = setup();
    uint8_t out[4], repeated[4];
    int32_t r;
    uint16_t mask;
    uint32_t n = chord_build(t, 60, out);
    chord_auto_t saved = chord_auto[0];
    for (uint32_t i = 0; i < 20; i++) chord_make(t, 65, repeated, &r, &mask);
    expect("UI previews do not teach the history", !memcmp(&saved, &chord_auto[0], sizeof saved));
    expect("a repeated key keeps its chord", chord_build(t, 60, repeated) == n && !memcmp(out, repeated, n));
    n = chord_build(t, 65, out);
    expect("C -> F keeps the common C in place", n == 3 && !memcmp(out, (const uint8_t[]){60,65,69}, 3));
    chord_auto_forget(0);
    chord_build(t, 62, out); n = chord_build(t, 67, out);
    expect("ii -> V7", n == 4 && pcs(out, n) == ((1u<<7)|(1u<<11)|(1u<<2)|(1u<<5)));
    n = chord_build(t, 60, out);
    expect("V7 -> I settles on a triad", n == 3 && pcs(out, n) == ((1u<<0)|(1u<<4)|(1u<<7)));
    chord_auto_tick(2u * beat_samples());
    chord_expect(t, 65, (const uint8_t[]){65,69,72}, 3, "F");
    chord_build(t, 60, out);
    t->p[P_SCALE] = 1;
    chord_expect(t, 60, (const uint8_t[]){60,63,67}, 3, "Cm");
    t->p[P_SCALE] = 0; t->p[P_ROOT] = 2;
    chord_expect(t, 62, (const uint8_t[]){62,66,69}, 3, "D");
    t->p[P_ROOT] = 0; t->p[P_VOIC] = VC_INV1;
    chord_expect(t, 60, (const uint8_t[]){64,67,72}, 3, "C");
    expect("phrase gap, settings and voice-leading", 1);
}
static void melody_from_other_tracks(void)
{
    track_t *t = setup();
    track_t *lead = &trk[1];
    lead->p[P_CHRD] = CH_OFF; lead->p[P_MUTE] = PLAY_ON;
    input_on(lead, 71, 100);
    chord_expect(t, 60, (const uint8_t[]){60,64,67,71}, 4, "Cmaj7");
    input_off(lead, 71);
    chord_expect(t, 60, (const uint8_t[]){60,64,67,71}, 4, "Cmaj7");
    chord_auto_tick(2u * beat_samples());
    chord_expect(t, 60, (const uint8_t[]){60,64,67}, 3, "C");
    lead->p[P_MUTE] = PLAY_OFF;
    input_on(lead, 65, 100);
    chord_expect(t, 60, (const uint8_t[]){60,64,67}, 3, "C");
    lead->p[P_MUTE] = PLAY_ON;
    lead->p[P_CHRD] = CH_MAJ;
    chord_expect(t, 60, (const uint8_t[]){60,64,67}, 3, "C");
    lead->p[P_CHRD] = CH_OFF;
    input_off(lead, 65);
    chord_auto_forget(1);
    /* The arp-held notes of another track count too. */
    lead->p[P_AMODE] = 1;
    input_on(lead, 71, 100);
    chord_expect(t, 60, (const uint8_t[]){60,64,67,71}, 4, "Cmaj7");
    input_off(lead, 71);
    expect("other tracks' melody, mute and generated chords", 1);
}
static void integration(void)
{
    track_t *t = setup();
    uint8_t out[4];
    song.g[G_BPM] = 120;
    key_down(7);
    expect("AUTO on a key plays 3 notes", ngated(t) == 3);
    /* Araminta keeps live keys in held[] only for the ARP; with ARP on the
     * long hold refreshes the phrase (a plain held chord expires after 2 beats). */
    t = setup();
    t->p[P_AMODE] = 1;
    key_down(7);
    chord_auto_tick(8u * beat_samples());
    expect("a long ARP-held chord keeps the phrase", chord_auto[0].n == 3);
    key_up(7);
    t = setup();
    key_down(7);
    uint8_t started[4];
    memcpy(started, kb_chord[7], 4);
    chord_build(t, 65, out);
    t->p[P_SCALE] = 1; t->p[P_CHRD] = CH_MIN7;
    expect("a held chord keeps its notes after SCL / CHRD change", !memcmp(started, kb_chord[7], 4));
    key_up(7);
    expect("release ends it", !ngated(t));
    /* STRUM: the first string sounds now; a release cancels the rest. */
    t = setup();
    t->p[P_ALLOC] = sweep_pack(2, 0);
    key_down(7);
    expect("STRUM starts with one string", ngated(t) == 1);
    key_up(7);
    events_block(beat_samples());
    expect("release cancels the unplayed strings", !ngated(t) && !kb_strum_pending);
    /* ARP holds the chord. */
    t = setup();
    t->p[P_AMODE] = 1;
    key_down(7);
    expect("ARP holds the chord", t->nheld == 3);
    key_up(7);
    expect("ARP release clears it", !t->nheld);
    /* STOP clears the history, and recording writes the chord. */
    t = setup();
    song.rec = song.playing = 1;
    key_down(7);
    expect("recording writes the AUTO chord", t->step[0].n == 3 && step_note(&t->step[0], 0) == 60 &&
           step_note(&t->step[0], 1) == 64 && step_note(&t->step[0], 2) == 67);
    key_up(7);
    seq_stop();
    expect("STOP clears the AUTO history", !chord_auto[0].n);
    /* MONO, kits, projects. */
    t = setup();
    t->p[P_VOICE] = V_MONO;
    expect("MONO plays one note", chord_build(t, 60, out) == 1 && out[0] == 60);
    t = setup();
    song.sel = 3;
    trk[3].p[P_CHRD] = CH_AUTO;
    expect("a kit plays one note", chord_build(&trk[3], 36, out) == 1 && out[0] == 36);
    song.sel = 0;
    setup();
    chord_build(&trk[0], 60, out);
    static project_t saved, loaded;
    static project_store_t wire;
    project_capture(&saved);
    expect("CHRD AUTO packs and loads", proj_pack(&wire, &saved) && proj_import(&loaded, &wire, sizeof wire) &&
           loaded.t[0].p[P_CHRD] == CH_AUTO);
    expect("integration", 1);
}
static void render_hash(void)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    setup();
    set_engine_of(&trk[0], 0);
    uint8_t out[4];
    uint32_t n = chord_build(&trk[0], 67, out);
    for (uint32_t i = 0; i < n; i++) trk_note_on(&trk[0], out[i], 80);
    for (uint32_t b = 0; b < 64u; b++) {
        int32_t buf[CTL] = {0};
        track_render(&trk[0], buf, CTL);
        for (uint32_t i = 0; i < CTL; i++) hash = (hash ^ (uint32_t)buf[i]) * UINT64_C(1099511628211);
    }
    printf("AUTO render: hash %016llx\n", (unsigned long long)hash);
    expect("AUTO G chord render is stable", hash == UINT64_C(0xb574dc3fb9aafabb));
}

int main(void)
{
    _Static_assert(CH_POW == 9 && CH_AUTO == 10 && CH_MELODY == 11, "old chord IDs stay unchanged");
    musical_examples();
    every_scale();
    context();
    melody_from_other_tracks();
    integration();
    render_hash();
    puts(failures ? "AUTO CHORD TEST FAILED" : "AUTO chord tests passed");
    return failures != 0;
}
