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

/* FDBK's last steps: INF holds at unity, FBLP self-oscillates under its limiter. */
static void loop_case(int fdbk, int32_t ceil, const char *what)
{
    static int32_t in[CTL], out[CTL];
    uint32_t b, i, nb = 40u * (uint32_t)FS / CTL;
    host_tracks_init();
    memset(dly_buf, 0, sizeof dly_buf);
    memset(&fx, 0, sizeof fx);
    dly_free = 0;
    song.g[G_DTIME] = GP[G_DTIME].def;
    song.g[G_DFDBK] = (int16_t)fdbk;
    song.g[G_DCOLOR] = 70;
    for (b = 0; b < nb; b++) {
        for (i = 0; i < CTL; i++)
            in[i] = b == 0u ? 12000 : 0;
        AUDIO_FAR(dly_process_late)(in, out, CTL, 32768, 0);
    }
    if (!buf_nonzero()) {
        printf("FAIL FDBK %s: the loop died out after %u s\n", what, (unsigned)(nb * CTL / FS));
        fails++;
    } else {
        int32_t peak = 0;
        for (i = 0; i < DLY_LEN; i++) {
            int32_t p = dly_buf[i] < 0 ? -(int32_t)dly_buf[i] : dly_buf[i];
            if (p > peak)
                peak = p;
        }
        if (peak < 1000 || peak > ceil) {
            printf("FAIL FDBK %s: the loop's peak is %d (ceiling %d)\n", what, (int)peak, (int)ceil);
            fails++;
        }
    }
}

/* the knob's last values and what they show */
static void format_case(void)
{
    char val[12];
    const char *unit;
    param_format(&GP[G_DFDBK], DFDBK_INF, val, &unit);
    if (!str_eq(val, "INF") || unit[0]) {
        printf("FAIL FDBK INF: shows \"%s %s\"\n", val, unit);
        fails++;
    }
    param_format(&GP[G_DFDBK], DFDBK_FBLP, val, &unit);
    if (!str_eq(val, "LOOP") || unit[0]) {
        printf("FAIL FDBK LOOP: shows \"%s %s\"\n", val, unit);
        fails++;
    }
    if (param_turn(&GP[G_DFDBK], DFDBK_INF - 1, 1) != DFDBK_INF ||
        param_turn(&GP[G_DFDBK], DFDBK_INF, 1) != DFDBK_FBLP ||
        param_turn(&GP[G_DFDBK], DFDBK_FBLP, 1) != DFDBK_FBLP) {
        printf("FAIL FDBK INF/FBLP: the knob does not step through them\n");
        fails++;
    }
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
    loop_case(DFDBK_INF, 32768, "INF");
    loop_case(DFDBK_FBLP, (int32_t)DLY_LIM_CEIL, "LOOP");
    format_case();
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
    printf("delay tails reach zero; INF holds; LOOP sings under its limiter\n");
    return 0;
}
