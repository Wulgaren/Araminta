/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Adapted for Araminta from Melodee f4fdbc7. */
/* MELO chord keys (CHRD = MELO): the played key is the melody, always the top note. The chord stays
 * in the track's scale, every voicing keeps the melody fixed, DOWN strums lead with it, and MONO,
 * kits, STOP and the accompaniment cannot change a held chord. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static uint32_t failures;
static void expect(const char *name, int ok)
{
    printf("MELO: %-72s %s\n", name, ok ? "PASS" : "FAIL");
    failures += !ok;
}
static void reset(void)
{
    ui_power_on();
    memset(kb_chn, 0, sizeof kb_chn); memset(mchord, 0, sizeof mchord); memset(chord_last, 0, sizeof chord_last);
    memset(kb_strum, 0, sizeof kb_strum); kb_strum_pending = midi_strum_pending = 0;
    fm1_in.notes = kb_prev = 0; mo_w = mo_r = 0; usb.config = 1;
    trk[0].p[P_VOICE] = V_POLY; trk[0].p[P_SCALE] = 0; trk[0].p[P_ROOT] = 0;
    trk[0].p[P_CHRD] = CH_MELODY; trk[0].p[P_QUANT] = QN_OFF; trk[0].p[P_VOIC] = VC_CLOSE;
    events_block(CTL);
}
static void chord_is(uint32_t key, const uint8_t *want, uint32_t count, const char *label)
{
    uint8_t out[CHORD_MAX];
    int32_t root;
    uint16_t mask;
    char name[12];
    uint32_t n = chord_make(&trk[0], key, out, &root, &mask);
    chord_melody_name(name, (uint32_t)root, mask);
    int ok = n == count && !memcmp(out, want, count) && str_eq(name, label);
    if (!ok) {
        printf("  key %u: %s", key, name);
        for (uint32_t i = 0; i < n; i++) printf(" %u", out[i]);
        printf(" (wanted %s)\n", label);
    }
    expect(label, ok);
}
static uint32_t melody_gated(const track_t *t, uint32_t note)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < NVOICE; i++) n += t->v[i].active && t->v[i].gate && t->v[i].note == note;
    return n;
}
#define K_C4 7u

static void examples(void)
{
    reset();
    /* The melody C D E F G A B stays on top; the chord root may be different. */
    static const uint8_t key[] = {60, 62, 64, 65, 67, 69, 71};
    static const uint8_t notes[][3] = {{52, 55, 60}, {53, 57, 62}, {55, 60, 64}, {57, 60, 65}, {60, 64, 67}, {60, 64, 69}, {62, 67, 71}};
    static const char *names[] = {"C", "Dm", "C", "F", "C", "Am", "G"};
    for (uint32_t i = 0; i < 7u; i++) chord_is(key[i], notes[i], 3, names[i]);
    trk[0].p[P_SCALE] = 1; trk[0].p[P_ROOT] = 7;
    chord_is(67, (const uint8_t[]){58, 62, 67}, 3, "Gm");
    trk[0].p[P_VOIC] = VC_OPEN;
    chord_is(67, (const uint8_t[]){46, 62, 67}, 3, "Gm");
    trk[0].p[P_VOIC] = VC_INV1;
    chord_is(67, (const uint8_t[]){58, 62, 67}, 3, "Gm");
    trk[0].p[P_VOIC] = VC_INV2;
    chord_is(67, (const uint8_t[]){50, 58, 67}, 3, "Gm");
    trk[0].p[P_VOIC] = VC_BASS;
    chord_is(67, (const uint8_t[]){55, 58, 62, 67}, 4, "Gm");
    trk[0].p[P_VOIC] = VC_CLOSE; trk[0].p[P_ROOT] = 9;
    chord_is(69, (const uint8_t[]){60, 64, 69}, 3, "Am");
    trk[0].p[P_SCALE] = 6; chord_is(68, (const uint8_t[]){59, 64, 68}, 3, "E");
    trk[0].p[P_SCALE] = 2; trk[0].p[P_ROOT] = 2;
    chord_is(62, (const uint8_t[]){53, 57, 62}, 3, "Dm");
    trk[0].p[P_ROOT] = 0; trk[0].p[P_SCALE] = 4;
    chord_is(62, (const uint8_t[]){55, 60, 62}, 3, "Csus2");
    trk[0].p[P_SCALE] = 12;
    chord_is(60, (const uint8_t[]){52, 56, 60}, 3, "Caug");
    trk[0].p[P_SCALE] = 11;
    chord_is(66, (const uint8_t[]){58, 63, 66}, 3, "D#m");
    trk[0].p[P_SCALE] = 0;
}
static void exhaustive(void)
{
    uint32_t cases = 0;
    for (uint32_t scale = 0; scale < NELEM(SCALE_MASK); scale++)
        for (int32_t tonic = 0; tonic < 12; tonic++)
            for (uint32_t v = 0; v <= VC_BASS; v++) {
                track_t *t = &trk[0];
                t->p[P_SCALE] = (int16_t)scale; t->p[P_ROOT] = (int16_t)tonic; t->p[P_TRANS] = 0; t->p[P_VOIC] = (int16_t)v;
                uint32_t allowed = chord_melody_rotate(scale_mask(t), tonic);
                for (uint32_t input = 0; input < 128u; input++) {
                    int32_t melody = (int32_t)input;
                    while (melody >= 0 && !(allowed & (1u << (uint32_t)(melody % 12)))) melody--;
                    if (melody < 0) { melody = 0; while (!(allowed & (1u << (uint32_t)(melody % 12)))) melody++; }
                    uint8_t out[CHORD_MAX], again[CHORD_MAX];
                    int32_t root, r2;
                    uint16_t mask, m2;
                    uint32_t n = chord_make(t, input, out, &root, &mask);
                    uint32_t n2 = chord_make(t, input, again, &r2, &m2);
                    if (!(n >= 1u && n <= CHORD_MAX && n == n2 && root == r2 && mask == m2 && !memcmp(out, again, n) &&
                          out[n - 1u] == (uint8_t)melody)) {
                        expect("exhaustive: stable, ordered, in scale, melody on top", 0);
                        printf("  scale %u root %d voicing %u input %u\n", scale, tonic, v, input);
                        return;
                    }
                    if (melody >= 36) {
                        if (n < 3u) { expect("exhaustive: a playable melody keeps its accompaniment", 0); return; }
                    }
                    for (uint32_t i = 0; i < n; i++)
                        if (out[i] > 127u || !(allowed & (1u << (out[i] % 12u))) || (i && out[i] <= out[i - 1u])) {
                            expect("exhaustive: every note in scale, ascending", 0);
                            return;
                        }
                    cases++;
                }
            }
    printf("MELO: %u exhaustive cases across %u scales, every root, voicing and MIDI input\n",
           cases, (unsigned)NELEM(SCALE_MASK));
    expect("exhaustive scales, roots, voicings and inputs", 1);
}
static void transposition(void)
{
    uint32_t bad = 0;
    for (uint32_t scale = 0; scale < NELEM(SCALE_MASK); scale++)
        for (uint32_t v = 0; v <= VC_BASS; v++)
            for (uint32_t key = 60; key < 72u; key++)
                for (uint32_t shift = 0; shift < 12u; shift++) {
                    uint8_t base[CHORD_MAX], shifted[CHORD_MAX];
                    int32_t r, r2;
                    uint16_t mask, m2;
                    track_t *t = &trk[0];
                    t->p[P_SCALE] = (int16_t)scale; t->p[P_VOIC] = (int16_t)v; t->p[P_ROOT] = 0; t->p[P_TRANS] = 0;
                    uint32_t n = chord_make(t, key, base, &r, &mask);
                    t->p[P_ROOT] = (int16_t)shift;
                    uint32_t n2 = chord_make(t, key + shift, shifted, &r2, &m2);
                    bad += !(n == n2 && mask == m2 && (r + (int32_t)shift) % 12 == r2 % 12);
                    for (uint32_t i = 0; i < n && i < n2; i++) bad += base[i] + shift != shifted[i];
                }
    expect("key or ROOT transposes the same harmony and melody", !bad);
}
static void strum_and_holds(void)
{
    reset();
    /* DOWN leads with the melody; the accompaniment follows. */
    song.g[G_BPM] = 120;
    trk[0].p[P_ALLOC] = sweep_pack(2, 0);             /* 1/16, DOWN */
    key_down(K_C4);
    expect("DOWN strum starts on the melody", melody_gated(&trk[0], 60) == 1 && kb_chord[K_C4][0] == 60);
    trk[0].p[P_SCALE] = 1; trk[0].p[P_CHRD] = CH_POW;      /* captured order does not move */
    chord_strum_tick(8192u);
    expect("a held chord keeps its notes after SCL / CHRD change",
           melody_gated(&trk[0], 52) && melody_gated(&trk[0], 55) && melody_gated(&trk[0], 60));
    key_up(K_C4);
    expect("release ends every note", !melody_gated(&trk[0], 60) && !melody_gated(&trk[0], 52) && !melody_gated(&trk[0], 55));
    /* STRUM OFF: the whole chord on the press. */
    reset();
    key_down(K_C4);
    expect("STRUM OFF plays the whole chord", melody_gated(&trk[0], 60) && melody_gated(&trk[0], 52) && melody_gated(&trk[0], 55));
    key_up(K_C4);
    /* MONO plays the melody alone. */
    reset();
    trk[0].p[P_VOICE] = V_MONO;
    uint8_t out[CHORD_MAX];
    expect("MONO plays the melody", chord_build(&trk[0], 67, out) == 1u && out[0] == 67);
    /* A kit ignores chords. */
    reset();
    song.sel = 3;
    trk[3].p[P_CHRD] = CH_MELODY;
    expect("a kit plays one note per key", chord_build(&trk[3], 36, out) == 1u && out[0] == 36);
    song.sel = 0;
    /* CHRD stays 11 through a project round trip, and 12 clamps. */
    reset();
    static project_t saved, loaded;
    static project_store_t wire;
    project_capture(&saved);
    expect("CHRD MELO packs and loads", proj_pack(&wire, &saved) && proj_import(&loaded, &wire, sizeof wire) &&
           loaded.t[0].p[P_CHRD] == CH_MELODY);
    expect("CHRD above MELO clamps to MELO",
           param_fit(param_desc_of(0, P_CHRD), CH_MELODY + 1) == CH_MELODY);
}
static void render_hash(void)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    reset();
    set_engine_of(&trk[0], 0);
    trk[0].p[P_CHRD] = CH_MELODY;
    uint8_t out[CHORD_MAX];
    uint32_t n = chord_build(&trk[0], 67, out);
    for (uint32_t i = 0; i < n; i++) trk_note_on(&trk[0], out[i], 80);
    for (uint32_t b = 0; b < 64u; b++) {
        int32_t buf[CTL] = {0};
        track_render(&trk[0], buf, CTL);
        for (uint32_t i = 0; i < CTL; i++) hash = (hash ^ (uint32_t)buf[i]) * UINT64_C(1099511628211);
    }
    printf("MELO render: hash %016llx\n", (unsigned long long)hash);
    expect("MELO G chord render is stable", hash == UINT64_C(0xb574dc3fb9aafabb));
}
int main(void)
{
    _Static_assert(CH_AUTO == 10 && CH_MELODY == 11, "AUTO is 10, MELO is 11");
    examples();
    exhaustive();
    transposition();
    strum_and_holds();
    render_hash();
    puts(failures ? "MELO CHORD TEST FAILED" : "MELO chord tests passed");
    return failures != 0;
}
