/* scratch probe (removed before handoff): what our free-ms glide does to the spectrum.
 * builds like tests/perform_test.c: cc -w -Ibuild/gen -Ifirmware/src -o build/host/dlyprobe dlyprobe.c -lm  */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define main hostsim_main
#include "tests/hostsim.c"
#undef main

/* hostsim already gives dly_buf / dly_q8 / fx_buses (fx.c) and the song */

static void wav_put16(FILE *f, int32_t v)
{
    int16_t s = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    fwrite(&s, 2, 1, f);
}

/* the delay bus alone, like perform_test's dly_run (its own copy of that helper) */
static int trace = 1;
static uint32_t dly_marks;
static void dly_only(const int32_t *in, int32_t *out, uint32_t frames)
{
    static int32_t z[CTL], w[CTL];
    uint32_t t, i;
    for (t = 0; t < frames; t += CTL) {
        fx_buses(in + t, z, w, CTL);
        for (i = 0; i < CTL; i++)
            out[t + i] = w[i];
        if (trace && (t % (CTL * 8u)) < CTL)
            printf("  t=%6.3f s  tgt=%8.2f ms  read=%8.2f ms  err=%6.2f ms\n", (dly_marks + t) / (double)FS,
                   dtime_q8() / 256.0 * 1000.0 / FS, dly_q8 / 256.0 * 1000.0 / FS,
                   (dtime_q8() - dly_q8) / 256.0 * 1000.0 / FS);
    }
    dly_marks += frames;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "dlyglide.wav";
    uint32_t n = FS * 6, i;
    static int32_t in[6 * FS], out[6 * FS];
    FILE *f;
    uint32_t hdr[5] = {0x46464952, 0, 0x45564157, 0x20746d66, 16};
    uint32_t fmt[4] = {0x00010001u, FS, FS * 2u, 0x00100001u};
    uint16_t c;

    host_tracks_init();
    for (i = 0; i < n; i++)
        in[i] = (int32_t)(9000.0 * sin(2 * M_PI * 261.63 * i / FS)      /* C4 + harmonics, like the reference */
                          + 4500.0 * sin(2 * M_PI * 523.25 * i / FS)
                          + 2200.0 * sin(2 * M_PI * 784.0 * i / FS)
                          + 1100.0 * sin(2 * M_PI * 1046.5 * i / FS));
    memset(dly_buf, 0, sizeof dly_buf);
    memset(rev_tank, 0, sizeof rev_tank);
    memset(&fx, 0, sizeof fx);
    dly_q8 = 0;
    dly_free = 0;
    song.g[G_DMIX] = 127;                                        /* wet only: the bus's own mix */
    song.g[G_DFDBK] = 0;                                          /* no feedback: one clean echo path */
    song.g[G_DLYOWN] = 0;                                         /* SEND: the bus processes */
    song.g[G_DTIME] = (int16_t)(DTIME_FREE + 100);                /* 100 ms */
    dly_only(in, out, FS / 2);                                    /* 0.5 s steady at 100 ms */

    /* 1. one jump of 200 ms (a hard flick), 2 s */
    song.g[G_DTIME] = (int16_t)(DTIME_FREE + 300);
    dly_only(in + FS / 2, out + FS / 2, 2 * FS);
    /* 2. back down 200 ms */
    song.g[G_DTIME] = (int16_t)(DTIME_FREE + 100);
    dly_only(in + FS * 5 / 2, out + FS * 5 / 2, FS);
    /* 3. 1 ms steps (a slow turn), 2 s */
    song.g[G_DTIME] = (int16_t)(DTIME_FREE + 100);
    dly_only(in + FS * 7 / 2, out + FS * 7 / 2, FS / 2);
    for (i = 0; i < 8u; i++) {
        song.g[G_DTIME] = (int16_t)(DTIME_FREE + 100 + i);
        dly_only(in + FS * 4 + i * FS / 8, out + FS * 4 + i * FS / 8, FS / 8);
    }

    f = fopen(path, "wb");
    if (!f) { printf("no %s\n", path); return 1; }
    hdr[1] = 36 + n * 2;
    fwrite(hdr, 4, 5, f);
    fwrite(fmt, 4, 4, f);
    fwrite("data", 1, 4, f);
    c = (uint16_t)(n * 2);
    fwrite(&c, 2, 1, f);
    fwrite("    ", 1, 4, f);                                      /* placeholder, size patched below */
    for (i = 0; i < n; i++)
        wav_put16(f, out[i]);
    fseek(f, 4, SEEK_SET);
    hdr[1] = 36 + n * 2;
    fwrite(&hdr[1], 4, 1, f);
    fseek(f, 40, SEEK_SET);
    c = (uint16_t)(n * 2);
    fwrite(&c, 2, 1, f);
    fclose(f);
    printf("wrote %s: %u samples (%.2f s)\n", path, n, n / (double)FS);
    printf("  steady 0.00-0.50 s | 100 -> 300 ms at 0.50 | back to 100 at 2.50 | 1 ms steps at 3.50\n");
    return 0;
}
