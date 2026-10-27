/* SPDX-License-Identifier: GPL-3.0-only
 * DRUMS host smoke test: the ported Felucca drum engine (engine 15). */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;

static void fail(const char *s) { printf("FAIL %s\n", s); fails++; }
static void pass(const char *s) { printf("ok   %s\n", s); }

static void reset(track_t *t)
{
    uint32_t i;
    host_tracks_init();
    memset(eng_mem, 0, sizeof eng_mem);
    memset(eng_mem_owner, 0, sizeof eng_mem_owner);
    for (i = 0; i < NVOICE; i++)
        t->v[i].active = t->v[i].gate = 0;
    host_preset(t, ENGI_DRUMS, 0);
    t->eng_req = t->engine = ENGI_DRUMS;
    eng_mem_claim(t);
}

static int32_t hit(track_t *t, uint32_t note, uint32_t blocks, uint64_t *energy)
{
    int32_t o[2u * CTL], peak = 0;
    uint32_t b, i;
    if (energy)
        *energy = 0;
    trk_note_on(t, note, 100);
    for (b = 0; b < blocks; b++) {
        memset(o, 0, sizeof o);
        track_render(t, o, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t a = o[2u * i] < 0 ? -o[2u * i] : o[2u * i];
            if (a > peak)
                peak = a;
            if (energy)
                *energy += (uint64_t)(a * a);
        }
    }
    return peak;
}

int main(void)
{
    static const uint8_t NOTES[8] = {36, 38, 39, 42, 46, 45, 37, 56};
    static const char *const LN[8] = {"KICK", "SNARE", "CLAP", "HAT CL", "HAT OP", "TOM", "RIM", "BELL"};
    track_t *t = &trk[0];
    uint32_t i;
    reset(t);
    for (i = 0; i < 8u; i++) {
        int32_t peak;
        char b[40];
        reset(t);
        peak = hit(t, NOTES[i], 120, 0);
        snprintf(b, sizeof b, "%s sounds (peak %d)", LN[i], peak);
        if (peak < 200 || peak > 50000)
            fail(b);
        else
            pass(b);
    }

    /* kit select changes the kick */
    {
        uint64_t a, b;
        int32_t pa, pb;
        reset(t);
        t->p[P_E0] = 1;                                  /* 80 */
        pa = hit(t, 36, 80, &a);
        reset(t);
        t->p[P_E0] = 2;                                  /* 10 */
        pb = hit(t, 36, 80, &b);
        if (a == b || pa == pb)
            fail("KIT 80 and KIT 10 differ");
        else
            pass("KIT 80 and KIT 10 differ");
    }

    /* closed hat chokes the open one */
    {
        uint64_t e_open = 0, e_after = 0;
        int32_t o[2u * CTL];
        uint32_t b, i;
        reset(t);
        trk_note_on(t, 46, 100);
        for (b = 0; b < 40u; b++) {
            memset(o, 0, sizeof o);
            track_render(t, o, CTL);
            for (i = 0; i < CTL; i++) {
                int32_t a = o[2u * i] < 0 ? -o[2u * i] : o[2u * i];
                e_open += (uint64_t)(a * a);
            }
        }
        trk_note_on(t, 42, 100);
        for (b = 0; b < 40u; b++) {
            memset(o, 0, sizeof o);
            track_render(t, o, CTL);
            if (b >= 10u)
                for (i = 0; i < CTL; i++) {
                    int32_t a = o[2u * i] < 0 ? -o[2u * i] : o[2u * i];
                    e_after += (uint64_t)(a * a);
                }
        }
        if (e_after >= e_open)
            fail("closed hat chokes open");
        else
            pass("closed hat chokes open");
    }

    /* the 8 lanes ring together */
    {
        uint32_t active = 0;
        reset(t);
        for (i = 0; i < 8u; i++)
            trk_note_on(t, NOTES[i], 90);
        track_render(t, (int32_t[2u * CTL]){0}, CTL);
        for (i = 0; i < NVOICE; i++)
            active += t->v[i].active ? 1u : 0u;
        if (active < 8u)
            fail("8 lanes ring together");
        else
            pass("8 lanes ring together");
    }

    printf(fails ? "DRUMS TEST FAILED (%d)\n" : "DRUMS test passed\n", fails);
    return fails ? 1 : 0;
}
