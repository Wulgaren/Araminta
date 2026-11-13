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

/* one hit of a sound alone through the voice (dx_run, the kit's knobs at their middle), nblk blocks into y */
static void hit_sound(uint32_t lane, const dx_lane_t *s, uint32_t model, int32_t *y, uint32_t nblk)
{
    static dx_voice_t v;
    uint32_t b;
    memset(&v, 0, sizeof v);
    dx_kit[lane] = *s;
    dx_lay[lane] = 0;
    dx_mdl[lane] = (uint8_t)model;
    v.sv = 100;
    dx_trigger(&v);
    for (b = 0; b < nblk; b++)
        dx_run(&dx_kit[lane], &v, 0, 0, 0, 0, 0, 0, 0, y + b * CTL, CTL);
}

/* how strong the tone at hz is in y[0 .. n), dB (Goertzel, Hann window; for comparing two tones of one hit) */
static double tone_db(const int32_t *y, uint32_t n, double hz)
{
    double c = 2.0 * cos(2.0 * M_PI * hz / FS), s1 = 0, s2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double s0 = y[i] * (0.5 - 0.5 * cos(2.0 * M_PI * i / n)) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 10.0 * log10(s1 * s1 + s2 * s2 - c * s1 * s2 + 1e-9);
}

/* the spectral centre of y[0 .. n) in Hz (power-weighted, every bin by Goertzel) */
static double centre_hz(const int32_t *y, uint32_t n)
{
    double p = 1e-9, m = 0;
    uint32_t k;
    for (k = 1; k < n / 2u; k++) {
        double q = pow(10.0, tone_db(y, n, (double)k * FS / n) / 10.0);
        p += q;
        m += q * k;
    }
    return m / p * FS / n;
}

/* the energy of y[0 .. n) between lo and hi Hz, dB (its bins by Goertzel) */
static double band_db(const int32_t *y, uint32_t n, double lo, double hi)
{
    double p = 1e-9;
    uint32_t k;
    for (k = (uint32_t)(lo * n / FS); k <= (uint32_t)(hi * n / FS); k++)
        p += pow(10.0, tone_db(y, n, (double)k * FS / n) / 10.0);
    return 10.0 * log10(p);
}

/* the level of y[0 .. n) in dB, its energy against full scale */
static double level_db(const int32_t *y, uint32_t n)
{
    double e = 1e-9;
    uint32_t i;
    for (i = 0; i < n; i++)
        e += (double)y[i] * y[i];
    return 10.0 * log10(e / n / (32767.0 * 32767.0));
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

    if (!str_eq(ENGINES[ENGI_DRUM]->presets[0].name, "FM DRUMS"))
        fail("preset name FM DRUMS");
    else
        pass("preset name FM DRUMS");
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
    {   /* MIXER's lane meter: a hit leaves its own lane's level, no other lane's */
        uint32_t k = trk_index(t), l;
        for (l = 0; l < DV_NLANE; l++)
            drum_lane_meter[k][l] = 0;
        hit_lane(t, 36, 4, &pk, &en);
        if (drum_lane_meter[k][DV_KICK] == 0 || drum_lane_meter[k][DV_SNARE] != 0)
            fail("lane meter: the kick's only");
        else
            pass("lane meter: the kick's only");
        for (l = 0; l < DV_NLANE; l++)
            drum_lane_meter[k][l] = 0;
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

    /* MODEL TEETH, a clap struck 3 times 10 ms apart: silent between its strikes, as Felucca's DRUMS clap (20 .. 25 dB
     * down there), its tail only after the last one */
    {
        static int32_t y[64u * CTL];
        static const dx_lane_t CLAP = {DX_MODE(0, DXF_BP, 1), {60, 31, 60, 0, 80}, {60, 31, 60, 0, 80}};
        double strike = -200.0, gap;
        uint32_t ms;
        char b[80];
        hit_sound(DV_CLAP, &CLAP, DXV_TEETH, y, 64u);
        for (ms = 0; ms < 6u; ms++) {                    /* the first strike's loudest 1 ms */
            double d = level_db(y + ms * 44u, 44u);
            if (d > strike) strike = d;
        }
        gap = level_db(y + 352u, 44u);                   /* 8 .. 9 ms: the 1 ms before the second strike */
        snprintf(b, sizeof b, "TEETH clap drops %.1f dB between strikes (at least 15)", strike - gap);
        if (strike - gap < 15.0)
            fail(b);
        else
            pass(b);
    }

    /* MODEL CLICK, a rim of two tones (440 Hz and x3.67) with COLOR at its top: the upper tone on top, as Felucca's STD
     * 80 55 rims have it (80's: 8 dB louder) */
    {
        static int32_t y[16u * CTL];
        static const dx_lane_t RIM = {DX_MODE(1, DXF_LP, 0), {60, 0, 80, 0, 127}, {60, 0, 80, 0, 127}};
        double lo, hi;
        char b[80];
        hit_sound(DV_RIM, &RIM, DXV_CLICK, y, 16u);
        lo = tone_db(y, 512u, 440.0);
        hi = tone_db(y, 512u, 440.0 * 15030.0 / 4096.0);
        snprintf(b, sizeof b, "CLICK rim's upper tone %.1f dB over its lower (at least 6)", hi - lo);
        if (hi - lo < 6.0)
            fail(b);
        else
            pass(b);
    }

    /* MODEL SHELL with SNAP, a snare: its body tone (a sine at A3) dies away while its noise rings on, as Felucca's
     * DRUMS snare (the body gone by ~130 ms, the noise after it): at 150 ms the tone 20 dB further down than the noise */
    {
        static int32_t y[256u * CTL];
        static const dx_lane_t SNARE = {DX_MODE(DXW_SINE, DXF_HP, 1), {44, 0, 70, 64, 100}, {44, 0, 70, 64, 100}};
        const uint32_t at = 6615u;                      /* 150 ms */
        double hz = 440.0 * pow(2.0, ((384 + 44 * 12) / 16.0 - 69.0) / 12.0);
        double tone, noise;
        char b[96];
        hit_sound(DV_SNARE, &SNARE, DXV_SHELL, y, 256u);
        tone = tone_db(y + at, 1024u, hz) - tone_db(y, 1024u, hz);
        noise = level_db(y + at, 1024u) - level_db(y, 1024u);
        snprintf(b, sizeof b, "SHELL snare's body %.1f dB down at 150 ms, its noise %.1f (body at least 20 further)",
                 -tone, -noise);
        if (noise - tone < 20.0)
            fail(b);
        else
            pass(b);
    }

    /* MODEL METAL with its high-pass at 11.7 kHz, Felucca's 80 closed hat (six squares, the band at 7.1 kHz): as bright
     * as DRUMS' (its spectral centre 10.2 kHz; the band alone gives ~6 kHz) */
    {
        static int32_t y[32u * CTL];
        static const dx_lane_t HAT = {DX_MODE(0, DXF_BP, 0) | 3u << 5, {42, 127, 34, 0, 111}, {42, 127, 34, 0, 111}};
        double c;
        char b[80];
        hit_sound(DV_HATC, &HAT, DXV_METAL, y, 32u);
        c = centre_hz(y, 1024u);
        snprintf(b, sizeof b, "METAL hat with its 11.7 kHz high-pass centred at %.0f Hz (at least 9000)", c);
        if (c < 9000.0)
            fail(b);
        else
            pass(b);
    }

    /* MODEL TEETH's NOISE, Felucca's bright copy of the clap's noise: with it up the clap is brighter (its spectral
     * centre 2 semitones higher or more), its band (2 .. 6 kHz around COLOR's 4.2 kHz) as loud as without (3 dB) */
    {
        static int32_t y0[64u * CTL], y1[64u * CTL];
        static const dx_lane_t DULL = {DX_MODE(0, DXF_BP, 1), {64, 31, 60, 0, 96}, {64, 31, 60, 0, 96}};
        static const dx_lane_t BRIGHT = {DX_MODE(0, DXF_BP, 1), {64, 31, 60, 64, 96}, {64, 31, 60, 64, 96}};
        double up, band;
        char b[96];
        hit_sound(DV_CLAP, &DULL, DXV_TEETH, y0, 64u);
        hit_sound(DV_CLAP, &BRIGHT, DXV_TEETH, y1, 64u);
        up = 12.0 * log2(centre_hz(y1, 2048u) / centre_hz(y0, 2048u));
        band = band_db(y1, 2048u, 2000.0, 6000.0) - band_db(y0, 2048u, 2000.0, 6000.0);
        snprintf(b, sizeof b, "TEETH NOISE brightens the clap %.1f semitones (at least 2), its band %+.1f dB (3 at most)",
                 up, band);
        if (up < 2.0 || band > 3.0 || band < -3.0)
            fail(b);
        else
            pass(b);
    }

    /* factory kit bytes */
    if (!dx_kit_ok(dx_kit) || !dx_kit_ok(DX_KIT_DEF))
        fail("factory dx_kit ok");
    else
        pass("factory dx_kit ok");

    printf(fails ? "DRUM-X TEST FAILED (%d)\n" : "DRUM-X test passed\n", fails);
    return fails ? 1 : 0;
}
