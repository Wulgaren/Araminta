/* SPDX-License-Identifier: GPL-3.0-only */
/* The send delay's tail must die out to exact zeros: after a burst and enough silence, the line, the
 * low-pass and the output are all 0 at any FDBK and COLR, through the plain and the late (send bus) path.
 * A loop of rounded products can instead hold a small offset for ever, which keeps a silence gate off.
 * Run by tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main

#include <stdio.h>
#include <string.h>

static int fails;

static uint32_t buf_nonzero(void)
{
    uint32_t i, nz = 0;
    for (i = 0; i < DLY_LEN; i++)
        nz += dly_buf[i] != 0;
    return nz;
}

/* one burst block of `amp` (both signs), then `sec` seconds of silence; late: the send bus's latency */
static void decay_case(int fdbk, int color, int32_t amp, uint32_t late, uint32_t sec)
{
    static int32_t in[CTL], out[CTL];
    uint32_t b, i, nb = sec * (uint32_t)FS / CTL, out_nz = 0, g0 = dly_gate_blocks;
    host_tracks_init();
    memset(dly_buf, 0, sizeof dly_buf);
    memset(&fx, 0, sizeof fx);
    dly_free = 0;
    song.g[G_DTIME] = GP[G_DTIME].def;
    song.g[G_DFDBK] = (int16_t)fdbk;
    song.g[G_DCOLOR] = (int16_t)color;
    for (b = 0; b < nb; b++) {
        for (i = 0; i < CTL; i++)
            in[i] = b == 0u ? ((i & 1u) ? amp : -amp) + (int32_t)i * 37 : 0;
        AUDIO_FAR(dly_process_late)(in, out, CTL, 32768, late);
    }
    for (i = 0; i < CTL; i++)
        out_nz += out[i] != 0;
    if (buf_nonzero() || fx.dly_lp != 0 || out_nz) {
        printf("FAIL FDBK %d COLR %d amp %d late %u: %u line samples, lp %d, %u output samples not zero after %u s\n",
               fdbk, color, (int)amp, (unsigned)late, (unsigned)buf_nonzero(), (int)fx.dly_lp, (unsigned)out_nz,
               (unsigned)sec);
        fails++;
    }
    if (dly_gate_blocks == g0) {
        printf("FAIL FDBK %d COLR %d amp %d late %u: the silence gate skipped no block in %u s\n",
               fdbk, color, (int)amp, (unsigned)late, (unsigned)sec);
        fails++;
    }
}

int main(void)
{
    static const int FDBK[] = {1, 60, 120};
    static const int COLR[] = {0, 70, 127};
    static const int32_t AMP[] = {3, 20000, -20000};
    uint32_t f, c, a;
    for (f = 0; f < 3u; f++)
        for (c = 0; c < 3u; c++)
            for (a = 0; a < 3u; a++) {
                decay_case(FDBK[f], COLR[c], AMP[a], 0, 40);
                decay_case(FDBK[f], COLR[c], AMP[a], CTL, 40);
            }
    if (fails) {
        printf("%d delay tails did not reach zero\n", fails);
        return 1;
    }
    printf("delay tails reach zero\n");
    return 0;
}
