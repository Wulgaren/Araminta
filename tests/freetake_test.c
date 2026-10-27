/* SPDX-License-Identifier: GPL-3.0-only */
/* Free take (SLOOP port): empty project, REC armed while stopped, first note starts a take with no
 * tempo; REC closes the loop (length sets tempo), PLAY / STOP drops it; a note started while an
 * earlier one is still held sets SF_SLIDE on the previous NOTE step; releasing that tap while the
 * hold continues records a return to the held pitch. A strummed chord keeps its STRUM division
 * at the take's tempo and its strings do not slide. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>

static void clear_patterns(void)
{
    uint32_t c, j;
    for (c = 0; c < NTRK; c++) {
        memset(trk[c].step, 0, sizeof trk[c].step);
        for (j = 0; j < NSTEP; j++)
            trk[c].step[j].time = ST_REST;
    }
}

static void ft_arm(int32_t *out, uint32_t rb)
{
    clear_patterns();
    ft_bars = 0;
    song.playing = 0;
    song.g[G_BPM] = 120;
    song.rec = 1u;
    fm1_in.buttons = 0;
    fm1_in.notes = 0;
    ft_btn_mask = rb;
    mix_block(out, CTL);
}

static uint32_t ft_hold_a(int32_t *out, uint32_t k0)
{
    fm1_in.notes = 1u << k0;
    mix_block(out, CTL);
    assert(ft_on);
    return 1;
}

/* short tap of (k0+koff) at beat beat_i while k0 stays down; returns the block counter */
static uint32_t ft_tap(int32_t *out, uint32_t k0, uint32_t koff, double beat, uint32_t beat_i, uint32_t blk)
{
    uint32_t on = (uint32_t)(beat_i * beat * FS / CTL + 0.5);
    uint32_t off = on + (uint32_t)((beat / 8.0) * FS / CTL);
    while (blk < on) {
        mix_block(out, CTL);
        blk++;
    }
    fm1_in.notes = (1u << k0) | (1u << (k0 + koff));
    mix_block(out, CTL);
    blk++;
    while (blk < off) {
        mix_block(out, CTL);
        blk++;
    }
    fm1_in.notes = 1u << k0;
    mix_block(out, CTL);
    return blk + 1u;
}

static void ft_rec_close(int32_t *out, uint32_t rb, double beat, uint32_t blk)
{
    uint32_t close_at = (uint32_t)(4.0 * beat * FS / CTL + 0.5);
    while (blk < close_at) {
        mix_block(out, CTL);
        blk++;
    }
    fm1_in.buttons = rb;
    mix_block(out, CTL);
    fm1_in.buttons = 0;
    fm1_in.notes = 0;
    mix_block(out, CTL);
}

static void expect_note(uint32_t i, uint8_t note, int slide)
{
    assert(trk[0].step[i].time == ST_NOTE && step_note(&trk[0].step[i], 0) == note);
    if (slide)
        assert(step_fl(&trk[0].step[i]) & SF_SLIDE);
    else
        assert(!(step_fl(&trk[0].step[i]) & SF_SLIDE));
}

static void expect_ties(uint32_t from, uint32_t to, int last_slides)
{
    uint32_t j;
    for (j = from; j < to; j++)
        assert(trk[0].step[j].time == ST_TIE);
    if (last_slides)
        assert(step_fl(&trk[0].step[to - 1u]) & SF_SLIDE);
}

int main(void)
{
    int32_t out[CTL * 2];
    uint32_t j, c, ti;
    static const struct { double bpm; uint32_t bars; int16_t want_bpm, want_bars; } T[] = {
        {100.0, 1, 100, 1},
        {118.0, 1, 120, 1},
        {90.0, 2, 90, 2},
        {140.0, 4, 140, 4},
    };
    uint32_t rb = 1u << 3, k0 = 7u;

    host_tracks_init();
    for (ti = 0; ti < sizeof T / sizeof T[0]; ti++) {
        double beat = 60.0 / T[ti].bpm;
        uint32_t nb = T[ti].bars * 4u, blk = 0, bi, len = 16u * (uint32_t)T[ti].want_bars;
        host_tracks_init();
        song.g[G_BPM] = 120;
        song.g[G_CLOCK] = 0;
        song.sel = 0;
        song.playing = 0;
        song.rec = 0;
        cin_bars = 0;
        clear_patterns();
        trk[0].step[40].time = ST_TIE;                 /* (an old tie: no effect) */
        ft_btn_mask = rb;
        fm1_in.buttons = 0;
        ft_bars = 0;
        song.rec = 1u;
        mix_block(out, CTL);
        assert(!song.playing && !ft_on && song.rec == 1u);
        for (bi = 0; bi <= nb; bi++) {
            uint32_t on = (uint32_t)(bi * beat * FS / CTL + 0.5) + (bi & 1u ? 6u : 0u);
            uint32_t off = on + (uint32_t)((bi == 1u ? beat / 2.0 : beat / 8.0) * FS / CTL);
            if (bi == nb) {
                while (blk < on) {
                    mix_block(out, CTL);
                    blk++;
                }
                fm1_in.buttons = rb;
                mix_block(out, CTL);
                blk++;
                fm1_in.buttons = 0;
                break;
            }
            while (blk < on) {
                mix_block(out, CTL);
                blk++;
            }
            {
                static const uint8_t KOFF[4] = {0, 2, 4, 5};
                fm1_in.notes = 1u << (k0 + KOFF[bi & 3u]);
            }
            mix_block(out, CTL);
            blk++;
            if (bi == 0)
                assert(ft_on && !song.playing && !song.rec);
            while (blk < off) {
                mix_block(out, CTL);
                blk++;
            }
            fm1_in.notes = 0;
            mix_block(out, CTL);
            blk++;
        }
        assert(!ft_on && song.playing && !song.rec);
        assert(ft_bars == (uint8_t)T[ti].want_bars && song.g[G_BPM] == T[ti].want_bpm);
        for (c = 0; c < NTRK; c++)
            assert(trk[c].p[P_SLEN] == (int16_t)len && trk[c].p[P_SDIV] == 2);
        for (j = 0; j < len; j++) {
            const step_t *st = &trk[0].step[j];
            if (j % 4u == 0) {
                static const uint8_t KN[4] = {60, 62, 64, 65};
                assert(st->time == ST_NOTE && st->n == 1 && step_note(st, 0) == KN[(j / 4u) & 3u]);
            } else if (j == 5u) {
                assert(st->time == ST_TIE);
            } else {
                assert(!st->n && st->time != ST_NOTE && st->time != ST_TIE);
            }
        }
        for (j = len; j < NSTEP; j++)
            assert(trk[0].step[j].time == ST_REST);
        transport_req = 2;
        mix_block(out, CTL);
    }

    /* STOP during a free take drops it; PLAY from elsewhere closes it like REC; PLAY button drops */
    host_tracks_init();
    song.g[G_BPM] = 120;
    song.playing = 0;
    song.rec = 0;
    song.sel = 0;
    cin_bars = 0;
    clear_patterns();
    ft_bars = 0;
    song.rec = 1u;
    fm1_in.notes = 1u << k0;
    mix_block(out, CTL);
    fm1_in.notes = 0;
    assert(ft_on);
    for (j = 0; j < 100; j++)
        mix_block(out, CTL);
    transport_req = 2;
    mix_block(out, CTL);
    assert(!ft_on && !song.playing && ft_bars == 0xFF && !trk[0].step[0].n);

    ft_bars = 0;
    song.rec = 1u;
    fm1_in.notes = 1u << k0;
    mix_block(out, CTL);
    fm1_in.notes = 0;
    for (j = 0; j < (uint32_t)(2.0 * FS / CTL); j++)
        mix_block(out, CTL);
    transport_req = 1;
    mix_block(out, CTL);
    assert(!ft_on && song.playing && ft_bars == 1 && song.g[G_BPM] == 120 && trk[0].step[0].n == 1);
    transport_req = 2;
    mix_block(out, CTL);

    clear_patterns();
    ft_drop_mask = 1u << 5;
    ft_bars = 0;
    song.rec = 1u;
    fm1_in.notes = 1u << k0;
    mix_block(out, CTL);
    fm1_in.notes = 0;
    for (j = 0; j < 200; j++)
        mix_block(out, CTL);
    fm1_in.buttons = ft_drop_mask;
    mix_block(out, CTL);
    fm1_in.buttons = 0;
    mix_block(out, CTL);
    assert(!ft_on && !song.playing && ft_bars == 0xFF && !trk[0].step[0].n);
    ft_drop_mask = 0;

    /* the longest take closes by itself (24 s) */
    transport_req = 2;
    mix_block(out, CTL);
    clear_patterns();
    ft_bars = 0;
    song.rec = 1u;
    fm1_in.notes = 1u << k0;
    mix_block(out, CTL);
    fm1_in.notes = 0;
    while (ft_on)
        mix_block(out, CTL);
    assert(ft_bars == 4 && song.g[G_BPM] == 40 && song.playing);
    transport_req = 2;
    mix_block(out, CTL);
    ft_btn_mask = 0;

    /* hold A, tap B/C/D → slides, returns to A, TIEs; staccato/chord → none; same-pitch B taps → returns */
    {
        double beat = 60.0 / 120.0;
        uint32_t blk, bi;
        static const uint8_t KOFF[4] = {0, 2, 4, 5};
        host_tracks_init();
        song.g[G_CLOCK] = 0;
        song.sel = 0;
        cin_bars = 0;
        ft_drop_mask = 0;
        ft_arm(out, rb);
        blk = ft_hold_a(out, k0);
        for (bi = 1; bi < 4u; bi++)
            blk = ft_tap(out, k0, KOFF[bi], beat, bi, blk);
        ft_rec_close(out, rb, beat, blk);
        assert(!ft_on && song.playing && ft_bars == 1);
        expect_note(0, 60, 1);
        expect_note(4, 62, 1);
        expect_note(5, 60, 1);
        expect_note(8, 64, 1);
        expect_note(9, 60, 1);
        expect_note(12, 65, 1);
        expect_note(13, 60, 0);
        expect_ties(1, 4, 1);
        expect_ties(6, 8, 1);
        expect_ties(10, 12, 1);
        transport_req = 2;
        mix_block(out, CTL);

        /* staccato: release A before B → no slides */
        ft_arm(out, rb);
        fm1_in.notes = 1u << k0;
        mix_block(out, CTL);
        assert(ft_on);
        for (j = 0; j < (uint32_t)((beat / 8.0) * FS / CTL); j++)
            mix_block(out, CTL);
        fm1_in.notes = 0;
        mix_block(out, CTL);
        for (j = 0; j < (uint32_t)((beat * 7.0 / 8.0) * FS / CTL); j++)
            mix_block(out, CTL);
        fm1_in.notes = 1u << (k0 + 2);
        mix_block(out, CTL);
        for (j = 0; j < (uint32_t)((beat / 8.0) * FS / CTL); j++)
            mix_block(out, CTL);
        fm1_in.notes = 0;
        for (j = 0; j < (uint32_t)((2.0 * beat) * FS / CTL); j++)
            mix_block(out, CTL);
        fm1_in.buttons = rb;
        mix_block(out, CTL);
        fm1_in.buttons = 0;
        mix_block(out, CTL);
        assert(!ft_on && song.playing);
        for (j = 0; j < 16u; j++)
            assert(!(step_fl(&trk[0].step[j]) & SF_SLIDE));
        transport_req = 2;
        mix_block(out, CTL);

        /* same-step chord: no slide */
        ft_arm(out, rb);
        fm1_in.notes = (1u << k0) | (1u << (k0 + 2));
        mix_block(out, CTL);
        assert(ft_on);
        for (j = 0; j < (uint32_t)((beat / 4.0) * FS / CTL); j++)
            mix_block(out, CTL);
        fm1_in.notes = 0;
        for (j = 0; j < (uint32_t)((2.0 * beat) * FS / CTL); j++)
            mix_block(out, CTL);
        fm1_in.buttons = rb;
        mix_block(out, CTL);
        fm1_in.buttons = 0;
        mix_block(out, CTL);
        assert(!ft_on && song.playing && trk[0].step[0].n >= 2);
        assert(!(step_fl(&trk[0].step[0]) & SF_SLIDE));
        transport_req = 2;
        mix_block(out, CTL);

        /* same pitch twice: returns to A between taps (not a straight B sustain) */
        ft_arm(out, rb);
        blk = ft_hold_a(out, k0);
        for (bi = 1; bi <= 2u; bi++)
            blk = ft_tap(out, k0, 2, beat, bi, blk);
        ft_rec_close(out, rb, beat, blk);
        assert(!ft_on && song.playing && ft_bars == 1);
        expect_note(0, 60, 1);
        expect_note(4, 62, 1);
        expect_note(5, 60, 1);
        expect_note(8, 62, 1);
        expect_note(9, 60, 0);
        expect_ties(1, 4, 1);
        expect_ties(6, 8, 1);
        transport_req = 2;
        mix_block(out, CTL);
    }

    /* 1/16 STRUM on a 1- or 2-bar take: one 1/16 step is too coarse for three strings, so the take
     * closes at 1/32 (LEN 32 / 64) and the C E G strings land on steps 0 1 2. */
    {
        uint32_t fb, close_at;
        for (fb = 1; fb <= 2u; fb++) {
            close_at = (uint32_t)(4.0 * (double)fb * (60.0 / 120.0) * FS / CTL + 0.5);
            host_tracks_init();
            song.g[G_CLOCK] = 0;
            song.sel = 0;
            cin_bars = 0;
            ft_drop_mask = 0;
            ft_bars = 0;
            ft_arm(out, rb);
            trk[0].p[P_CHRD] = CH_MAJ;
            trk[0].p[P_VOIC] = VC_CLOSE;
            trk[0].p[P_ALLOC] = sweep_pack(2, 0);      /* STRUM 1/16, DOWN */
            fm1_in.notes = 1u << k0;
            mix_block(out, CTL);
            assert(ft_on);
            for (j = 1; j < close_at; j++)
                mix_block(out, CTL);
            fm1_in.buttons = rb;
            mix_block(out, CTL);
            fm1_in.buttons = 0;
            fm1_in.notes = 0;
            mix_block(out, CTL);
            assert(!ft_on && song.playing && ft_bars == fb && song.g[G_BPM] == 120);
            assert(trk[0].p[P_SLEN] == (int16_t)(32u * fb) && trk[0].p[P_SDIV] == 3);
            expect_note(0, 60, 0);
            expect_note(1, 64, 0);
            expect_note(2, 67, 0);
            assert(trk[0].step[3].time == ST_TIE);
            transport_req = 2;
            mix_block(out, CTL);
        }
    }

    /* 1/8 STRUM: played 4 bars at 80 BPM while the project was at 120. The take closes at 80, and the
     * strings keep the 1/8 sweep (steps 0 1 2 at 1/16) instead of the shorter 120 BPM sweep, with no
     * SF_SLIDE (a strum is a chord, not a legato tap). */
    {
        double beat80 = 60.0 / 80.0;
        uint32_t close_at = (uint32_t)(16.0 * beat80 * FS / CTL + 0.5);
        host_tracks_init();
        song.g[G_CLOCK] = 0;
        song.sel = 0;
        cin_bars = 0;
        ft_drop_mask = 0;
        ft_bars = 0;
        ft_arm(out, rb);
        trk[0].p[P_CHRD] = CH_MAJ;
        trk[0].p[P_VOIC] = VC_CLOSE;
        trk[0].p[P_ALLOC] = sweep_pack(1, 0);          /* STRUM 1/8, DOWN */
        fm1_in.notes = 1u << k0;
        mix_block(out, CTL);
        assert(ft_on);
        for (j = 1; j < close_at; j++)
            mix_block(out, CTL);
        fm1_in.buttons = rb;
        mix_block(out, CTL);
        fm1_in.buttons = 0;
        fm1_in.notes = 0;
        mix_block(out, CTL);
        assert(!ft_on && song.playing && ft_bars == 4 && song.g[G_BPM] == 80);
        assert(trk[0].p[P_SLEN] == 64 && trk[0].p[P_SDIV] == 2);
        expect_note(0, 60, 0);
        expect_note(1, 64, 0);
        expect_note(2, 67, 0);
        assert(trk[0].step[3].time == ST_TIE);
        transport_req = 2;
        mix_block(out, CTL);
    }

    puts("free take: empty project sets loop and tempo, REC closes, PLAY/STOP drops, 24 s auto-close, "
         "held-note slides and returns, a strummed chord keeps the sweep at the take's tempo, a 1/16 "
         "strum gets the 1/32 grid on a 1-2 bar take PASS");
    return 0;
}
