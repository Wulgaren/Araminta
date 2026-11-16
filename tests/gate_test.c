/* SPDX-License-Identifier: GPL-3.0-only */
/* C4: the HALL and send-delay silence gates must stay bit-identical to the ungated path.
 * One source, built twice (FX_GATE=1 default, FX_GATE=0). The gated binary renders
 * the scenario below, prints one hash per section, and when given the ungated
 * binary as argv[1] compares those hashes (popen). FX_GATE=1 also asserts that both gates skipped blocks in the
 * opening silence and, for the delay, after its tail reached zeros (not a vacuous pass).
 * Run by tests/run_tests.sh / check-fw-c4.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t obuf[2 * CTL];
static int bad;

#define SEC_MAX 16
#define SEC_NAME 48

static struct {
    char name[SEC_NAME];
    uint64_t h;
    uint32_t hall_skip;
    uint32_t dly_skip;
} sec[SEC_MAX];
static uint32_t nsec;

static uint32_t blocks_for(uint32_t sec)
{
    return (sec * (uint32_t)FS + CTL - 1u) / CTL;
}

static void boot_fx(void)
{
    uint32_t k;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(rev_tank, 0, sizeof rev_tank);
    R = &rev_tank[0];
    memset(&fx, 0, sizeof fx);
    lim_env = LIM_T;
    memset(trk, 0, sizeof trk);
    MOD_W_CLEAR();
    host_tracks_init();
    song.g[G_RTYPE] = 2;                          /* HALL (host_tracks_init forces ROOM for goldens) */
    song.g[G_RSIZE] = GP[G_RSIZE].def;
    song.g[G_RDAMP] = GP[G_RDAMP].def;
    song.g[G_DTIME] = GP[G_DTIME].def;            /* synced division */
    song.g[G_DFDBK] = 80;
    song.g[G_DCOLOR] = GP[G_DCOLOR].def;
    song.g[G_DLYOWN] = song.g[G_REVOWN] = 0;      /* SEND */
    for (k = 0; k < 2u; k++) {
        host_preset(&trk[k], ENGI_ANALOG, 0);
        trk[k].p[P_LEVEL] = 127;
        trk[k].p[P_VOICE] = V_POLY;
        trk[k].p[P_DLY] = 100;
        trk[k].p[P_REV] = 100;
    }
    for (k = 2; k < NPART; k++)
        host_preset(&trk[k], ENGI_ANALOG, 0);
    fx_boot();
    hall_gate_blocks = 0;
    dly_gate_blocks = 0;
}

static uint64_t hash_blocks(uint32_t nb, void (*per)(uint32_t b))
{
    uint32_t b, i;
    uint64_t h = 0xCBF29CE484222325ull;
    for (b = 0; b < nb; b++) {
        if (per)
            per(b);
        memset(obuf, 0, sizeof obuf);
        mix_block(obuf, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            h ^= (uint32_t)obuf[i];
            h *= 0x100000001B3ull;
        }
    }
    return h;
}

static void notes_hit(uint32_t b)
{
    if (b == 0u) {
        trk_note_on(&trk[0], 48, 120);
        trk_note_on(&trk[0], 55, 110);
        trk_note_on(&trk[1], 60, 120);
        trk_note_on(&trk[1], 67, 100);
    } else if (b == 40u) {
        trk_note_off(&trk[0], 48);
        trk_note_off(&trk[0], 55);
        trk_note_off(&trk[1], 60);
        trk_note_off(&trk[1], 67);
    } else if (b == 80u) {
        trk_note_on(&trk[0], 52, 115);
        trk_note_on(&trk[1], 64, 115);
    } else if (b == 120u) {
        trk_note_off(&trk[0], 52);
        trk_note_off(&trk[1], 64);
    }
}

/* HALL alone (the real render path): one sample of `amp` into a silent send, then nb - 1 silent blocks.
 * out[] is cleared every block because rev_hall adds to it; the stereo pair is hashed. */
static uint64_t hash_hall_impulse(uint32_t nb, int32_t amp)
{
    static int32_t hin[CTL], hout[CTL];
    uint32_t b, i;
    uint64_t h = 0xCBF29CE484222325ull;
    for (b = 0; b < nb; b++) {
        memset(hin, 0, sizeof hin);
        if (b == 0u)
            hin[0] = amp;
        memset(hout, 0, sizeof hout);
        AUDIO_FAR(rev_hall)(hin, hout, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t side = R->hl.side ? rev_side[i] : 0;
            h ^= (uint32_t)(hout[i] + side);
            h *= 0x100000001B3ull;
            h ^= (uint32_t)(hout[i] - side);
            h *= 0x100000001B3ull;
        }
    }
    return h;
}

static void record_sec(const char *name, uint64_t h, uint32_t h0, uint32_t d0)
{
    if (nsec >= SEC_MAX)
        return;
    snprintf(sec[nsec].name, SEC_NAME, "%s", name);
    sec[nsec].h = h;
    sec[nsec].hall_skip = hall_gate_blocks - h0;
    sec[nsec].dly_skip = dly_gate_blocks - d0;
    nsec++;
}

static void print_secs(void)
{
    uint32_t i;
    for (i = 0; i < nsec; i++) {
        printf("SEC %s %016llx\n", sec[i].name, (unsigned long long)sec[i].h);
        printf("GATE %s hall=%u dly=%u\n", sec[i].name, (unsigned)sec[i].hall_skip, (unsigned)sec[i].dly_skip);
    }
}

static void run_scenario(void)
{
    uint32_t h0, d0;
    uint64_t h;

    boot_fx();
    nsec = 0;

    /* 3 s silence from boot, then 6 s after notes: both gates must engage in both (FX_GATE=1 asserts below) */
    h0 = hall_gate_blocks;
    d0 = dly_gate_blocks;
    h = hash_blocks(blocks_for(3u), 0);
    record_sec("boot_silence", h, h0, d0);

    h0 = hall_gate_blocks;
    d0 = dly_gate_blocks;
    h = hash_blocks(200u, notes_hit);
    record_sec("notes1", h, h0, d0);

    h0 = hall_gate_blocks;
    d0 = dly_gate_blocks;
    h = hash_blocks(blocks_for(6u), 0);
    record_sec("silence6", h, h0, d0);

    /* change HALL SIZE and delay time while silent, then notes, then 4 s silence */
    song.g[G_RSIZE] = 40;
    song.g[G_DTIME] = 3;
    h0 = hall_gate_blocks;
    d0 = dly_gate_blocks;
    h = hash_blocks(blocks_for(1u), 0);            /* 1 s silent after the knob moves */
    record_sec("knob_silence", h, h0, d0);

    h0 = hall_gate_blocks;
    d0 = dly_gate_blocks;
    h = hash_blocks(200u, notes_hit);
    record_sec("notes2", h, h0, d0);

    h0 = hall_gate_blocks;
    d0 = dly_gate_blocks;
    h = hash_blocks(blocks_for(4u), 0);
    record_sec("silence4", h, h0, d0);

    /* 20 s more silence: by now the delay's FDBK 80 tail is exact zeros and its gate stays on */
    h0 = hall_gate_blocks;
    d0 = dly_gate_blocks;
    h = hash_blocks(blocks_for(20u), 0);
    record_sec("silence_long", h, h0, d0);

    /* and the delay resumes bit-exact from its gate */
    h0 = hall_gate_blocks;
    d0 = dly_gate_blocks;
    h = hash_blocks(200u, notes_hit);
    record_sec("notes3", h, h0, d0);

    /* free-ms: time changes during silence, note right after */
    song.g[G_DTIME] = (int16_t)(DTIME_FREE + 200);
    h0 = hall_gate_blocks;
    d0 = dly_gate_blocks;
    h = hash_blocks(blocks_for(1u), 0);
    song.g[G_DTIME] = (int16_t)(DTIME_FREE + 450); /* glide while silent */
    h ^= hash_blocks(blocks_for(1u), 0);
    {
        uint32_t b;
        for (b = 0; b < 80u; b++) {
            if (b == 0u) {
                trk_note_on(&trk[0], 60, 120);
                trk_note_on(&trk[1], 67, 110);
            } else if (b == 40u) {
                trk_note_off(&trk[0], 60);
                trk_note_off(&trk[1], 67);
            }
            memset(obuf, 0, sizeof obuf);
            mix_block(obuf, CTL);
            {
                uint32_t i;
                for (i = 0; i < 2u * CTL; i++) {
                    h ^= (uint32_t)obuf[i];
                    h *= 0x100000001B3ull;
                }
            }
        }
    }
    record_sec("free_ms", h, h0, d0);

    /* A one-sample send impulse: the tail decays to exact zeros but the low cut's rounding remainder (he)
     * stays nonzero with hp 0. It must not keep HALL from gating, and keeping it must make the resume
     * bit-identical. Both polarities; silence sections are asserted to skip in gate_on. */
    boot_fx();
    h0 = hall_gate_blocks;
    d0 = dly_gate_blocks;
    h = hash_hall_impulse(600u, 1000);
    record_sec("impulse_up_silence", h, h0, d0);

    h0 = hall_gate_blocks;
    d0 = dly_gate_blocks;
    h = hash_hall_impulse(120u, 5000);
    record_sec("impulse_up_resume", h, h0, d0);

    boot_fx();
    h0 = hall_gate_blocks;
    d0 = dly_gate_blocks;
    h = hash_hall_impulse(600u, -1000);
    record_sec("impulse_dn_silence", h, h0, d0);

    h0 = hall_gate_blocks;
    d0 = dly_gate_blocks;
    h = hash_hall_impulse(120u, -5000);
    record_sec("impulse_dn_resume", h, h0, d0);
}

static int compare_off(const char *off_bin)
{
    char cmd[512], line[256], name[SEC_NAME];
    unsigned long long oh;
    FILE *fp;
    uint32_t i, matched = 0;
    int failed = 0;

    snprintf(cmd, sizeof cmd, "%s", off_bin);
    fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "gate: popen(%s) failed\n", off_bin);
        return 1;
    }
    while (fgets(line, sizeof line, fp)) {
        if (sscanf(line, "SEC %47s %llx", name, &oh) != 2)
            continue;
        for (i = 0; i < nsec; i++) {
            if (strcmp(sec[i].name, name))
                continue;
            matched++;
            if (sec[i].h != (uint64_t)oh) {
                printf("gate: SEC %s MISMATCH gated=%016llx ungated=%016llx\n",
                       name, (unsigned long long)sec[i].h, oh);
                failed = 1;
            }
            break;
        }
    }
    pclose(fp);
    if (matched != nsec) {
        printf("gate: compared %u of %u sections\n", matched, nsec);
        failed = 1;
    }
    return failed;
}

int main(int argc, char **argv)
{
    run_scenario();
    print_secs();

#if FX_GATE
    {
        uint32_t i;
        for (i = 0; i < nsec; i++) {
            if (strstr(sec[i].name, "silence") && !sec[i].hall_skip) {
                printf("gate: %s must skip HALL blocks\n", sec[i].name);
                bad = 1;
            }
            if ((!strcmp(sec[i].name, "boot_silence") || !strcmp(sec[i].name, "silence_long")) &&
                !sec[i].dly_skip) {
                printf("gate: %s must skip DELAY blocks\n", sec[i].name);
                bad = 1;
            }
        }
    }
#endif

    if (argc > 1)
        bad |= compare_off(argv[1]);

    printf("%s\n", bad ? "GATE TEST FAILED" : "gate tests passed");
    return bad != 0;
}
