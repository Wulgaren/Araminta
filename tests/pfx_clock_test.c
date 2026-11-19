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

/* lane_begin with the tape's clock out of the way (G_TMIX 0) and two steps marked so playback shows which step
 * each lane reads: step 3 an audio LPF and a MIDI 1/16 repeat, step 19 an audio HPF and MIDI RANDOM. */
static void lane_begin_marks(void)
{
    lane_begin();
    song.g[G_TMIX] = 0;                             /* the transport's own clock: no tape slew over it */
    pfx_lane_clear();
    pfx_lane[3] = PF_BIT(PF_LPF) | PF_BIT(PF_S16);
    pfx_lane[19] = PF_BIT(PF_HPF) | PF_BIT(PF_RND);
    perf_start();                                   /* a transport start: both lane clocks from 0 */
    song.playing = 1;
}

/* pfx_block retimes the MIDI lane's place when the tempo changes; the audio lane (perf_begin) must read the
 * same retimed clock. 1000 blocks at 70 BPM put both on step 3; at 400 BPM the MIDI lane keeps step 3 while an
 * unscaled audio clock would jump to step 19 (whose HPF and RANDOM would then play). */
static void lane_tempo_jump(void)
{
    static int32_t out[2 * CTL];
    uint32_t k;
    lane_begin_marks();
    for (k = 0; k < 1000u; k++)
        mix_block(out, CTL);
    ck("70 BPM: the MIDI and audio lanes both play step 3",
       trk[0].pfx == PFX_REP && (perf_act & PF_BIT(PF_LPF)) && !(perf_act & PF_BIT(PF_HPF)));
    song.g[G_BPM] = 400;
    mix_block(out, CTL);
    ck("tempo 70 -> 400: the MIDI lane still plays step 3's effect", trk[0].pfx == PFX_REP);
    ck("tempo 70 -> 400: the audio lane still plays step 3's LPF, not step 19's HPF",
       (perf_act & PF_BIT(PF_LPF)) && !(perf_act & PF_BIT(PF_HPF)));
}

/* A REPEAT recorded from step 4 on, first read after a tempo change, starts on the retimed 1/16 boundary: its
 * quantization must follow the lane's clock, not the old raw transport count. 1200 blocks at 70 BPM put the
 * retimed lane on step 4. */
static void lane_repeat_quantized(void)
{
    static int32_t out[2 * CTL];
    uint32_t k, s16, onset = 0, started = 0;
    lane_begin();
    song.g[G_TMIX] = 0;
    pfx_lane_clear();
    perf_start();
    song.playing = 1;
    for (k = 0; k < 1200u; k++)
        mix_block(out, CTL);
    for (k = 4; k < 8u; k++)
        pfx_lane[k] = PF_BIT(PF_R16);               /* the repeat stays held while the lane moves on */
    song.g[G_BPM] = 400;
    s16 = beat_samples() / 4u;
    for (k = 0; k < 200u && !started; k++) {
        mix_block(out, CTL);
        if (perf_act & PF_BIT(PF_R16)) {
            onset = (pfx_lph - CTL) + pf.split;     /* the block's start, on the retimed lane clock */
            started = 1;
        }
    }
    ck("tempo 70 -> 400: a REPEAT first read after the change starts on the retimed 1/16 boundary",
       started && onset % s16 == 0u);
}

/* A transport restart (transport_req 3) resets the lane clock before pfx_block applies the MIDI lane: the first
 * block plays step 0's audio (LPF) and its MIDI pitch (OCT+, +192), not the old step 3 (HPF, -192). */
static void lane_restart_pitch(void)
{
    static int32_t out[2 * CTL];
    uint32_t k;
    lane_begin();
    song.g[G_TMIX] = 0;
    pfx_lane_clear();
    pfx_lane[3] = PF_BIT(PF_OCTD) | PF_BIT(PF_HPF);
    pfx_lane[0] = PF_BIT(PF_OCTU) | PF_BIT(PF_LPF);
    perf_start();
    song.playing = 1;
    for (k = 0; k < 900u; k++)
        mix_block(out, CTL);                        /* 70 BPM: the lane reaches step 3 */
    ck("before restart: step 3 plays OCT- and HPF", trk[0].pfx_pit == -192 && (perf_act & PF_BIT(PF_HPF)));
    transport_req = 3;                              /* restart from playing, at step 3 */
    mix_block(out, CTL);
    ck("restart: the first block plays step 0's audio (LPF), not step 3's HPF",
       (perf_act & PF_BIT(PF_LPF)) && !(perf_act & PF_BIT(PF_HPF)));
    ck("restart: the first block's MIDI pitch is step 0's OCT+ (+192), not the old -192",
       trk[0].pfx_pit == 192);
    mix_block(out, CTL);
    ck("restart: the next block keeps step 0's MIDI pitch (+192)", trk[0].pfx_pit == 192);
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
    lane_tempo_jump();
    lane_repeat_quantized();
    lane_restart_pitch();
    printf("pfxclock: %d failure(s)\n", bad);
    return bad ? 1 : 0;
}
