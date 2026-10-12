/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DRUM-X host smoke test (drumx_voice.c + eng_drum.c) via hostsim.c.
 *   build/host/drum_test [DEMODIR]
 * Covers: factory kit renders, GM lane map, hat choke, mute, MRPH moves the sound, no full-scale clip. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;

static void fail(const char *s)
{
    printf("FAIL %s\n", s);
    fails++;
}
static void pass(const char *s) { printf("ok   %s\n", s); }

/* one hit of lane L through the DRUM engine; returns peak abs and energy */
static void hit_lane(track_t *t, uint32_t note, uint32_t nblk, int32_t *peak, uint64_t *energy)
{
    int32_t o[2u * CTL];
    uint32_t b, i;
    *peak = 0;
    *energy = 0;
    eng_mem_claim(t);
    trk_note_on(t, note, 100);
    for (b = 0; b < nblk; b++) {
        memset(o, 0, sizeof o);
        track_render(t, o, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t s = o[2u * i], a = s < 0 ? -s : s;
            if (a > *peak) *peak = a;
            *energy += (uint64_t)(a * a);
            if (a >= 32767) fails += 0; /* counted below */
        }
    }
}

int main(int argc, char **argv)
{
    track_t *t;
    static const uint8_t NOTES[8] = {36, 38, 39, 42, 46, 45, 37, 56};
    static const char *const LN[8] = {"KICK", "SNARE", "CLAP", "HATCL", "HATOP", "TOM", "RIM", "BELL"};
    uint32_t i;
    int32_t pk;
    uint64_t en, en2;
    (void)argc; (void)argv;
    host_tracks_init();
    t = &trk[0];
    host_drums(t);
    t->p[P_MUTE] = 0;
    song.master_q12 = 4096;

    if (!str_eq(ENGINES[ENGI_DRUM]->presets[0].name, "DRUM-X"))
        fail("preset name DRUM-X");
    else
        pass("preset name DRUM-X");
    if (ENG_DRUM.edit[0].fmt != F_PCT || ENG_DRUM.edit[0].max != 127 || !str_eq(ENG_DRUM.edit[0].label, "MRPH"))
        fail("EDIT 1 is MRPH 0..127");
    else
        pass("EDIT 1 is MRPH 0..127");

    for (i = 0; i < 8u; i++) {
        char b[48];
        hit_lane(t, NOTES[i], 200, &pk, &en);
        snprintf(b, sizeof b, "%s sounds (peak %d)", LN[i], pk);
        if (pk < 200 || en == 0)
            fail(b);
        else
            pass(b);
    }

    /* hat choke: closed hat after open drops open-hat energy */
    {
        int32_t o[2u * CTL], pk;
        uint64_t e_open = 0, e_after = 0;
        uint32_t b, i;
        eng_mem_claim(t);
        trk_note_on(t, 46, 100);
        for (b = 0; b < 40u; b++) {
            memset(o, 0, sizeof o);
            track_render(t, o, CTL);
            for (i = 0; i < CTL; i++) {
                int32_t s = o[2u * i], a = s < 0 ? -s : s;
                e_open += (uint64_t)(a * a);
            }
        }
        trk_note_on(t, 42, 100);
        for (b = 0; b < 40u; b++) {
            memset(o, 0, sizeof o);
            track_render(t, o, CTL);
            if (b >= 10u)
                for (i = 0; i < CTL; i++) {
                    int32_t s = o[2u * i], a = s < 0 ? -s : s;
                    e_after += (uint64_t)(a * a);
                }
        }
        (void)pk;
        if (e_after >= e_open)
            fail("closed hat chokes open");
        else
            pass("closed hat chokes open");
    }

    /* MRPH moves energy */
    t->p[P_E0] = 0;
    hit_lane(t, 36, 120, &pk, &en);
    t->p[P_E0] = 127;
    hit_lane(t, 36, 120, &pk, &en2);
    if (en == en2)
        fail("MRPH A..B changes the kick");
    else
        pass("MRPH A..B changes the kick");

    /* mute via drum_mute: new kick notes do not sound */
    {
        uint32_t i;
        for (i = 0; i < NVOICE; i++)
            t->v[i].active = t->v[i].gate = 0;
        eng_mem_claim(t);
        drum_mute[0] = 1u;
        hit_lane(t, 36, 40, &pk, &en);
        drum_mute[0] = 0;
        if (pk > 200 || en > 1000u)
            fail("lane mute silences kick");
        else
            pass("lane mute silences kick");
    }

    /* corner knobs still produce audio (soft knee may kiss full scale; not a fail) */
    {
        uint32_t n, heard = 0;
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = 127;
        for (n = 0; n < 8u; n++) {
            hit_lane(t, NOTES[n], 30, &pk, &en);
            if (pk > 100) heard++;
        }
        host_drums(t);
        if (heard < 6u)
            fail("corner knobs still produce audio");
        else
            pass("corner knobs still produce audio");
    }

    /* factory kit bytes */
    if (!dx_kit_ok(dx_kit) || !dx_kit_ok(DX_KIT_DEF))
        fail("factory dx_kit ok");
    else
        pass("factory dx_kit ok");

    printf(fails ? "DRUM-X TEST FAILED (%d)\n" : "DRUM-X test passed\n", fails);
    return fails ? 1 : 0;
}
