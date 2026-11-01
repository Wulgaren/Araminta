/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the MASTER page's knobs (GLO > MASTER: CLIP / PNCH / DUCK / DREL; src/fx.c master, src/dsp.c's
 * punch and sidechain, the kits' hit paths), same sources as the firmware (through hostsim.c):
 *   one rendered mix per setting, a child each (as the other matrix / mix tests): three loud ANALOG pads and a
 *   DRUM-X kick. CLIP 50 / 100 change an ordinary mix (the ceiling falls 32000 -> 6000; a quiet mix barely moved
 *   before), PNCH 100 the DRUM-X bus and the kick of a CR78 and a DRUMS kit, DUCK 100 the pads while a kick hits
 *   (the kit's own track is not ducked), and DREL 0 / 100 the release of that duck (the kicks are more than a hold
 *   apart, or the release never runs).
 * Run by tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <sys/wait.h>

static int32_t obuf[2 * CTL];
static int bad;

static void check(const char *what, int ok)
{
    printf("master: %-70s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static uint32_t preset_named(uint32_t e, const char *n)
{
    uint32_t i;
    for (i = 0; i < ENGINES[e]->npresets; i++)
        if (!strcmp(ENGINES[e]->presets[i].name, n))
            return i;
    return 0;
}

/* three pads (tracks 1..3) at full level, a kick on track 4 every 96 blocks (more than a duck hold apart, so DREL's
 * release runs); the master knobs at the given values */
static uint64_t render(int clip, int punch, int duck, int drel)
{
    uint32_t b, i, k;
    uint64_t h = 0xCBF29CE484222325ull;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(rev_tank, 0, sizeof rev_tank);
    R = &rev_tank[0];
    memset(&fx, 0, sizeof fx);
    lim_env = LIM_T;
    memset(trk, 0, sizeof trk);
    MOD_W_CLEAR();
    host_tracks_init();
    for (k = 0; k < NPART; k++)
        host_preset(&trk[k], k < 3u ? ENGI_ANALOG : ENGI_DRUM, k < 3u ? preset_named(ENGI_ANALOG, "SOFT PAD") : 0u);
    song.sel = 0;
    song.g[G_CLIP] = (int16_t)clip;
    song.g[G_PUNCH] = (int16_t)punch;
    song.g[G_DUCK] = (int16_t)duck;
    song.g[G_DREL] = (int16_t)drel;
    for (k = 0; k < 3u; k++) {
        track_t *t = &trk[k];
        t->p[P_LEVEL] = 127;
        t->p[P_VOICE] = V_POLY;
        trk_note_on(t, 48u + 4u * k, 127);
        trk_note_on(t, 55u + 4u * k, 127);
        trk_note_on(t, 60u + 4u * k, 127);
    }
    for (b = 0; b < 400u; b++) {
        if (b % 96u == 0)
            trk_note_on(&trk[3], 36, 127);              /* kick: PNCH's bus, DUCK's trigger */
        if (b == 200u)
            for (i = 0; i < 3u; i++) {
                trk_note_on(&trk[i], 64u + 4u * i, 110);
                trk_note_on(&trk[i], 67u + 4u * i, 110);
            }
        memset(obuf, 0, sizeof obuf);
        mix_block(obuf, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            h ^= (uint32_t)obuf[i];
            h *= 0x100000001B3ull;
        }
    }
    return h;
}

/* a pad (track 1) and one other kit's kick (track 2): PNCH / DUCK away from DRUM-X */
static uint64_t kit_render(uint32_t eng, uint32_t set0, int punch, int duck)
{
    uint32_t b, i, k;
    uint64_t h = 0xCBF29CE484222325ull;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(rev_tank, 0, sizeof rev_tank);
    R = &rev_tank[0];
    memset(&fx, 0, sizeof fx);
    lim_env = LIM_T;
    memset(trk, 0, sizeof trk);
    MOD_W_CLEAR();
    host_tracks_init();
    for (k = 0; k < NPART; k++)
        host_preset(&trk[k], k == 0u ? ENGI_ANALOG : k == 1u ? eng : 0u, k == 0u ? preset_named(ENGI_ANALOG, "SOFT PAD") : 0u);
    if (eng == ENGI_SAMPLE)
        trk[1].p[P_E0] = (int16_t)set0;                /* CR78 */
    song.sel = 0;
    song.g[G_PUNCH] = (int16_t)punch;
    song.g[G_DUCK] = (int16_t)duck;
    song.g[G_DREL] = 30;
    trk[0].p[P_LEVEL] = 127;
    trk[0].p[P_VOICE] = V_POLY;
    trk_note_on(&trk[0], 48, 127);
    trk_note_on(&trk[0], 55, 127);
    trk_note_on(&trk[0], 60, 127);
    for (b = 0; b < 400u; b++) {
        if (b % 96u == 0)
            trk_note_on(&trk[1], 36, 127);              /* the kick */
        if (b == 200u)
            for (i = 0; i < 2u; i++) {
                trk_note_on(&trk[0], 64u, 110);
                trk_note_on(&trk[0], 67u, 110);
            }
        memset(obuf, 0, sizeof obuf);
        mix_block(obuf, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            h ^= (uint32_t)obuf[i];
            h *= 0x100000001B3ull;
        }
    }
    return h;
}

static uint64_t child(int clip, int punch, int duck, int drel)
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    fflush(stdout);
    if (!(pid = fork())) {
        h = render(clip, punch, duck, drel);
        if (write(fd[1], &h, sizeof h) != sizeof h)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != sizeof h)
        h = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}

/* the same, for a kit rig (kit_render) */
static uint64_t kit_child(uint32_t eng, uint32_t set0, int punch, int duck)
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    fflush(stdout);
    if (!(pid = fork())) {
        h = kit_render(eng, set0, punch, duck);
        if (write(fd[1], &h, sizeof h) != sizeof h)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != sizeof h)
        h = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}

int main(void)
{
    uint64_t base = child(0, 0, 0, 30);
    check("CLIP 50 bites at an ordinary level", base && child(50, 0, 0, 30) != base);
    check("CLIP 100 drives the mix", base && child(100, 0, 0, 30) != base);
    check("PNCH 100 reaches the DRUM-X bus", base && child(0, 100, 0, 30) != base);
    check("DUCK 100 ducks the pads on a kick", base && child(0, 0, 100, 30) != base);
    check("DREL 0 / 100 change the duck's release", child(0, 0, 100, 0) != child(0, 0, 100, 100));
    {
        uint64_t k = kit_child(ENGI_SAMPLE, SMP_CR78_INDEX, 0, 0);
        check("PNCH 100 reaches a CR78 kick", k && kit_child(ENGI_SAMPLE, SMP_CR78_INDEX, 100, 0) != k);
        check("DUCK is triggered by a CR78 kick", k && kit_child(ENGI_SAMPLE, SMP_CR78_INDEX, 0, 100) != k);
    }
    {
        uint64_t k = kit_child(ENGI_DRUMS, 0, 0, 0);
        check("PNCH 100 reaches a DRUMS kick", k && kit_child(ENGI_DRUMS, 0, 100, 0) != k);
        check("DUCK is triggered by a DRUMS kick", k && kit_child(ENGI_DRUMS, 0, 0, 100) != k);
    }
    printf("%s\n", bad ? "MASTER TEST FAILED" : "master tests passed");
    return bad != 0;
}
