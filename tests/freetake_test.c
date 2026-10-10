/* SPDX-License-Identifier: GPL-3.0-only */
/* Free take (SLOOP port): empty project, REC armed while stopped, first note starts a take with no
 * tempo; REC closes the loop (length sets tempo), PLAY / STOP drops it. */
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
                assert(st->time == ST_NOTE && st->n == 1 && st->note[0] == KN[(j / 4u) & 3u]);
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

    puts("free take: empty project sets loop and tempo, REC closes, PLAY/STOP drops, 24 s auto-close PASS");
    return 0;
}
