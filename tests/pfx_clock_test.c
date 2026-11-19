/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The recorded punch-in lane runs on the song clock. TAPE MASTER with TEMPO OFF moves that clock with
 * SPEED (fx.c beat_samples), and a SPEED driven from the mod matrix (PSYCHODELIA does) moves it block to
 * block. A clock that moves must retime the lane, not jitter the step it reads: the lane step is
 * `pfx_lph / (beat / 4)`, so a shorter beat moved the index forward without moving the lane, the recorded
 * effect changed under it, and every repeat restarted. That restart fired the held notes again every block
 * (6652 fires in 8 s where the pattern has about 24, 18 178 note-ons, 16 537 voices given up) until the
 * overload guard shed what was held: the crackle on K3 MASTER in PSYCHODELIA.
 *   LANE    a moving clock (tempo and the tape's) keeps one step order: no step is skipped, none repeats
 *   STILL   the lane still advances (the retiming the tape is for is kept)                             */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int bad;
static void ck(const char *what, int ok)
{
    printf("pfxclock: %-72s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* pfx.c's own lane index under the beat as it is now */
static uint32_t lane_step(void)
{
    uint32_t s16 = beat_samples() / 4u;
    return (pfx_lph / s16) & 63u;
}

/* The lane filled (a 1/16 repeat on all 64 steps), one held key, the transport running. */
static void lane_begin(void)
{
    uint32_t k;
    ui_power_on();
    song.g[G_BPM] = 70;
    song.g[G_TAPEMODE] = TAPE_MASTER;
    song.g[G_TMIX] = 127;
    song.g[G_TTEMPO] = 0;                           /* TEMPO OFF: SPEED is the source clock */
    song.g[G_TSPEED] = 100;
    pfx_lane_clear();
    for (k = 0; k < 64u; k++)
        pfx_lane[k] = PF_BIT(PF_S16);
    song.sel = 0;
    input_on(&trk[0], 60, 100);
    pfx_lph = 0;
    song.playing = 1;
}

/* Run `blocks` blocks, calling `bump` before each, and count how the lane step moved.
 * One block before the first measurement settles the clock the case starts with. */
static void lane_run(uint32_t blocks, void (*bump)(uint32_t), uint32_t *jumps, uint32_t *back, uint32_t *adv)
{
    static int32_t out[2 * CTL];
    uint32_t k, prev;
    mix_block(out, CTL);
    prev = lane_step();
    for (k = 0; k < blocks; k++) {
        uint32_t s, d;
        if (bump)
            bump(k);
        mix_block(out, CTL);
        s = lane_step();
        d = (s + 64u - prev) % 64u;
        if (d > 1u)
            (*jumps)++;
        if (d > 32u)
            (*back)++;
        *adv += d;
        prev = s;
    }
}

static void bump_tempo(uint32_t k) { song.g[G_BPM] = (k & 1u) ? 400 : 40; }
static void bump_speed(uint32_t k) { song.g[G_TSPEED] = (k & 1u) ? 400 : 25; }

int main(void)
{
    uint32_t jumps, back, adv;
    printf("pfxclock: punch lane under a moving song clock\n");
    /* a clock that jumps between 40 and 400 BPM: the tape's SPEED fold moves it the same way */
    lane_begin();
    jumps = back = adv = 0;
    lane_run(1200u, bump_tempo, &jumps, &back, &adv);
    ck("tempo 40 <-> 400 every block: no step skipped, none repeated", jumps == 0u);
    ck("tempo 40 <-> 400: the lane never runs backwards", back == 0u);
    ck("tempo 40 <-> 400: it still advances", adv > 0u);
    /* the tape's own clock: SPEED 25 <-> 400 through TAPE MASTER, its slew moving the beat */
    lane_begin();
    jumps = back = adv = 0;
    lane_run(4000u, bump_speed, &jumps, &back, &adv);
    ck("TAPE MASTER SPEED 25 <-> 400: no step skipped, none repeated", jumps == 0u);
    ck("TAPE MASTER SPEED 25 <-> 400: the lane never runs backwards", back == 0u);
    ck("TAPE MASTER SPEED 25 <-> 400: it still advances", adv > 0u);
    printf("pfxclock: %d failure(s)\n", bad);
    return bad ? 1 : 0;
}
