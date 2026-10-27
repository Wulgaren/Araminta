/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Listening demos for the FX filters (not a pass/fail test):
 *   cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/filter_demo tests/filter_demo.c -lm
 *   build/host/filter_demo build/filter_demo [TAG]
 * Writes:
 *   punch_lpf_clean.wav / punch_lpf_ms20.wav   the LPF key's one-bar sweep, old clean voice / firmware's
 *   punch_hpf_clean.wav / punch_hpf_ms20.wav   the HPF key's one-bar sweep
 *   punch_k1_clean.wav / punch_k1_ms20.wav     the FILTER macro -100 .. +100 over four bars
 *   comb_dry.wav                               four bars, no insert comb (reference)
 *   comb_insert_TAG.wav                        two bars dry, then two bars of insert FILTER COMB (MIX 127)
 *   comb_analog.wav                            the same phrase with ANALOG TYPE COMB
 *   comb_sweep_insert.wav / comb_sweep_analog.wav   CUT 0 -> 127 -> 0 over four bars
 * The punch files are rendered offline on the same dry mix, so each pair is sample-aligned.
 * The MS-20 voice is now in perform.c; the clean files use the old k = 1 table kept below.
 * The comb sweep moves CUT 0..127 and back, showing the teeth move through the chord. */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define DEMO_STEPS 16u
#define DEMO_BARS 2u

/* the punch filter before the MS-20 voice: k = 1 (kept here so the demo can A/B the old sound) */
static const int16_t PF_SVF_CLEAN[64][3] = {
    {16349, 35, 0}, {16345, 39, 0}, {16341, 43, 0}, {16337, 47, 0}, {16332, 52, 0}, {16327, 57, 0}, {16321, 63, 0},
    {16314, 70, 0}, {16307, 77, 0}, {16299, 85, 0}, {16290, 94, 1}, {16280, 103, 1}, {16269, 114, 1},
    {16257, 126, 1}, {16244, 139, 1}, {16229, 153, 1}, {16213, 169, 2}, {16196, 186, 2}, {16176, 205, 3},
    {16154, 227, 3}, {16130, 250, 4}, {16104, 275, 5}, {16075, 303, 6}, {16043, 334, 7}, {16007, 368, 8},
    {15968, 406, 10}, {15925, 447, 13}, {15877, 492, 15}, {15824, 541, 19}, {15766, 596, 22}, {15702, 655, 27},
    {15631, 720, 33}, {15553, 791, 40}, {15467, 869, 49}, {15372, 953, 59}, {15267, 1046, 72}, {15151, 1146, 87},
    {15024, 1255, 105}, {14883, 1374, 127}, {14729, 1502, 153}, {14559, 1640, 185}, {14372, 1790, 223},
    {14166, 1950, 268}, {13940, 2121, 323}, {13692, 2304, 388}, {13420, 2499, 465}, {13122, 2704, 557},
    {12797, 2921, 667}, {12441, 3147, 796}, {12053, 3382, 949}, {11631, 3624, 1129}, {11173, 3870, 1341},
    {10677, 4119, 1589}, {10140, 4365, 1879}, {9562, 4605, 2218}, {8940, 4832, 2612}, {8274, 5040, 3070},
    {7563, 5219, 3602}, {6807, 5359, 4219}, {6006, 5444, 4934}, {5163, 5456, 5766}, {4282, 5369, 6733},
    {3372, 5149, 7863}, {2449, 4745, 9190},
};

static const uint8_t CHORD[4] = {57, 60, 64, 69};    /* A minor: rich harmonics for the comb and the sweep */

static void demo_fresh(void)
{
    uint32_t k;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(rev_tank, 0, sizeof rev_tank);
    R = &rev_tank[0];
    memset(&fx, 0, sizeof fx);
    memset(&pf, 0, sizeof pf);
    memset(ins_st, 0, sizeof ins_st);
    memset(ins_lines, 0, sizeof ins_lines);
    memset(ins_bus_ran, 0, sizeof ins_bus_ran);
    memset(bus_knobs_on, 0, sizeof bus_knobs_on);
    fx_bus_init();
    dly_q8 = 0;
    dly_free = 0;
    for (k = 0; k < NTRK; k++)
        pf.mg[k] = 32768;
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    vage = 0;
    rng_state = 0x1234567u;
    song.playing = 0;
    memset(trk, 0, sizeof trk);
    MOD_W_CLEAR();
    memset(sl, 0, sizeof sl);
    memset(&motion, 0, sizeof motion);
    host_tracks_init();
    host_preset(&trk[0], 0, 1);                    /* ANALOG SOFT PAD: polyphonic, sustained */
    host_preset(&trk[1], ENGI_DRUM, 0);            /* DRUM KIT */
    song.sel = 0;
    song.g[G_BPM] = 120;
    trk[0].p[P_ATK] = 20;
    trk[0].p[P_DEC] = 80;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_REL] = 40;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_LEVEL] = 100;
    trk[1].p[P_LEVEL] = 60;
    trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    trk[1].p[P_DLY] = trk[1].p[P_REV] = 0;
}

/* one 16th on the bass plus a kick / snare / hat grid; returns the blocks in a step */
static uint32_t demo_step_blocks(void)
{
    return (beat_samples() / 4u + CTL - 1u) / CTL;
}

/* mode 0: dry synth; 1: insert FILTER COMB from bar 3; 2: ANALOG TYPE COMB throughout;
 * 3: the full mix (synth + drums) for the punch-in filter; 4: insert COMB with CUT swept;
 * 5: ANALOG COMB with CUT swept */
static int32_t *render_phrase(uint32_t bars, int mode, uint32_t *frames_out)
{
    uint32_t steps = bars * DEMO_STEPS, step_blocks = demo_step_blocks(), step = step_blocks * CTL;
    uint32_t tail = (FS / 2u + step - 1u) / step;  /* 0.5 s: the pad's release and the comb's tail */
    uint32_t s, k, f = 0, frames = (steps + tail) * step;
    int32_t *buf;

    demo_fresh();
    trk[0].p[P_E4] = 110;                          /* open the synth's own LP: the FX filters colour it */
    trk[0].p[P_E5] = 30;
    trk[0].p[P_ED_FLT] = 0;
    trk[1].p[P_LEVEL] = mode == 3 ? 72 : 0;
    if (mode == 1) {
        trk[0].p[P_IFTYPE] = IFT_COMB;
        trk[0].p[P_IFCUT] = 25;
        trk[0].p[P_IFRES] = 115;
        trk[0].p[P_IFMIX] = 0;                     /* two bars dry, then the comb */
    } else if (mode == 2) {
        trk[0].p[P_FTYPE] = IFT_COMB;
        trk[0].p[P_E4] = 25;
        trk[0].p[P_E5] = 115;
    } else if (mode == 4) {
        trk[0].p[P_IFTYPE] = IFT_COMB;
        trk[0].p[P_IFCUT] = 0;
        trk[0].p[P_IFRES] = 127;
        trk[0].p[P_IFMIX] = 127;
    } else if (mode == 5) {
        trk[0].p[P_FTYPE] = IFT_COMB;
        trk[0].p[P_E4] = 0;
        trk[0].p[P_E5] = 127;
    }
    if (!(buf = calloc((size_t)frames * 2u, sizeof *buf)))
        return 0;
    for (s = 0; s < steps + tail; s++) {
        if (s == 0u)
            for (k = 0; k < 4u; k++)
                trk_note_on(&trk[0], CHORD[k], 100);
        if (mode == 1 && s == 2u * DEMO_STEPS)
            trk[0].p[P_IFMIX] = 127;
        if (s == steps)
            for (k = 0; k < 4u; k++)
                trk_note_off(&trk[0], CHORD[k]);
        if (mode == 3 && s < steps) {
            if ((s % 4u) == 0u)
                trk_note_on(&trk[1], 36, 112);
            if ((s % 8u) == 4u)
                trk_note_on(&trk[1], 38, 100);
            if ((s % 2u) == 0u)
                trk_note_on(&trk[1], 42, 68);
        }
        for (k = 0; k < step_blocks; k++) {
            int32_t o[2 * CTL];
            if ((mode == 4 || mode == 5) && s < steps) {
                uint32_t total = steps * step_blocks, half = total / 2u, pos = s * step_blocks + k;
                int32_t cut = pos < half ? (int32_t)(pos * 127u / (half - 1u))
                                         : (int32_t)(127u - (pos - half) * 127u / (total - half - 1u));
                if (mode == 4)
                    trk[0].p[P_IFCUT] = (int16_t)cut;
                else
                    trk[0].p[P_E4] = (int16_t)cut;
            }
            mix_block(o, CTL);
            memcpy(buf + 2u * f, o, 2u * CTL * sizeof *o);
            f += CTL;
        }
    }
    *frames_out = f;
    return buf;
}

static void wav_write(const char *dir, const char *name, const int32_t *buf, uint32_t frames)
{
    char path[512];
    FILE *w;
    uint32_t i;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(w = fopen(path, "wb"))) {
        fprintf(stderr, "filter_demo: cannot write %s\n", path);
        return;
    }
    wav_hdr(w, frames);
    for (i = 0; i < frames; i++)
        wav_put(w, buf[2u * i], buf[2u * i + 1u]);
    fclose(w);
    printf("filter_demo: %s\n", path);
}

/* coefficients of a PF_SVF row, interpolated at the Q8 cutoff index */
static void pf_coefs(const int16_t tab[64][3], int32_t idx, int16_t c[3])
{
    int32_t j = idx >> 8, fr = idx & 255, k;
    for (k = 0; k < 3; k++)
        c[k] = (int16_t)(tab[j][k] + (((tab[j < 63 ? j + 1 : 63][k] - tab[j][k]) * fr) >> 8));
}

enum { PU_LP, PU_HP, PU_K1 };

/* the punch filter on a dry mix: variant 0 = the old clean voice, 1 = the firmware's MS-20 voice;
 * kind PU_LP / PU_HP = the key's one-bar sweep, PU_K1 = the FILTER macro -100 .. +100 over four bars */
static void punch_filter(int32_t *buf, uint32_t frames, int variant, int kind)
{
    int32_t lz[2] = {0, 0}, rz[2] = {0, 0}, hz[2] = {0, 0}, hrz[2] = {0, 0};
    int32_t lc = PF_TOP, hc = 0, la = 0, ha = 0;
    uint32_t bar = 16u * (beat_samples() >> 2), f = 0;
    while (f < frames) {
        uint32_t n = frames - f < CTL ? frames - f : CTL, t = f < bar ? f : bar, i;
        int32_t lp_on = 1, hp_on = 1;
        if (kind == PU_LP) {
            int32_t lt = PF_TOP - (PF_TOP - PF_LOW) / 64 * (int32_t)(t * 64u / bar);
            lc += clamp(lt - lc, -192, 192);
            hc = 0;
            hp_on = 0;
        } else if (kind == PU_HP) {
            int32_t ht = (PF_TOP - PF_LOW) / 64 * (int32_t)(t * 64u / bar);
            hc += clamp(ht - hc, -192, 192);
            lc = PF_TOP;
            lp_on = 0;
        } else {
            uint32_t kbar = 4u * bar, tt = f < kbar ? f : kbar;
            int32_t m = -100 + (int32_t)(200u * tt / kbar);
            int32_t lt = m < 0 ? PF_TOP + m * (PF_TOP - PF_LOW) / 100 : PF_TOP;
            int32_t ht = m > 0 ? m * (PF_TOP - PF_LOW) / 100 : 0;
            lc += clamp(lt - lc, -192, 192);
            hc += clamp(ht - hc, -192, 192);
            lp_on = lc < PF_TOP;
            hp_on = hc > 0;
        }
        {
            int16_t cl[3], ch[3];
            const int16_t (*tab)[3] = variant == 0 ? PF_SVF_CLEAN : PF_SVF;
            if (la || lp_on)
                pf_coefs(tab, lc, cl);
            if (ha || hp_on)
                pf_coefs(tab, hc, ch);
            for (i = 0; i < n; i++) {
                int32_t l = buf[2u * (f + i)], r = buf[2u * (f + i) + 1u];
                int32_t tla = lp_on ? 32768 : 0, tha = hp_on ? 32768 : 0;
                if (la != tla)
                    la += clamp(tla - la, -SL_SLOPE, SL_SLOPE);
                if (ha != tha)
                    ha += clamp(tha - ha, -SL_SLOPE, SL_SLOPE);
                if (la) {
                    int32_t wl = l, wr = r;
                    if (variant == 0) {
                        wl = pf_svf(clamp(wl >> 2, -32767, 32767), lz, cl, 0) << 2;
                        wr = pf_svf(clamp(wr >> 2, -32767, 32767), rz, cl, 0) << 2;
                    } else {
                        wl = pf_ms20(clamp(wl >> 1, -32767, 32767), lz, cl, 0) << 1;
                        wr = pf_ms20(clamp(wr >> 1, -32767, 32767), rz, cl, 0) << 1;
                    }
                    l = pf_mix(l, wl, la);
                    r = pf_mix(r, wr, la);
                } else if (lz[0] | lz[1] | rz[0] | rz[1]) {
                    lz[0] = lz[1] = rz[0] = rz[1] = 0;
                }
                if (ha) {
                    int32_t wl = l, wr = r;
                    if (variant == 0) {
                        wl = pf_svf(clamp(wl >> 2, -32767, 32767), hz, ch, 1) << 2;
                        wr = pf_svf(clamp(wr >> 2, -32767, 32767), hrz, ch, 1) << 2;
                    } else {
                        wl = pf_ms20(clamp(wl >> 1, -32767, 32767), hz, ch, 1) << 1;
                        wr = pf_ms20(clamp(wr >> 1, -32767, 32767), hrz, ch, 1) << 1;
                    }
                    l = pf_mix(l, wl, ha);
                    r = pf_mix(r, wr, ha);
                } else if (hz[0] | hz[1] | hrz[0] | hrz[1]) {
                    hz[0] = hz[1] = hrz[0] = hrz[1] = 0;
                }
                buf[2u * (f + i)] = l;
                buf[2u * (f + i) + 1u] = r;
            }
        }
        f += n;
    }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/filter_demo";
    const char *tag = argc > 2 ? argv[2] : "current";
    char name[64];
    uint32_t frames;
    int32_t *dry, *copy;

    if (!(dry = render_phrase(DEMO_BARS, 3, &frames)))
        return 1;

    /* the punch LPF / HPF keys: the same dry mix through the old clean voice and the firmware's MS-20 voice */
    if ((copy = malloc((size_t)frames * 2u * sizeof *copy))) {
        memcpy(copy, dry, (size_t)frames * 2u * sizeof *copy);
        punch_filter(copy, frames, 0, PU_LP);
        wav_write(dir, "punch_lpf_clean.wav", copy, frames);
        memcpy(copy, dry, (size_t)frames * 2u * sizeof *copy);
        punch_filter(copy, frames, 1, PU_LP);
        wav_write(dir, "punch_lpf_ms20.wav", copy, frames);
        memcpy(copy, dry, (size_t)frames * 2u * sizeof *copy);
        punch_filter(copy, frames, 0, PU_HP);
        wav_write(dir, "punch_hpf_clean.wav", copy, frames);
        memcpy(copy, dry, (size_t)frames * 2u * sizeof *copy);
        punch_filter(copy, frames, 1, PU_HP);
        wav_write(dir, "punch_hpf_ms20.wav", copy, frames);
        free(copy);
    }
    free(dry);

    /* the FILTER macro: LPF -> open -> HPF over four bars */
    if ((dry = render_phrase(4, 3, &frames))) {
        if ((copy = malloc((size_t)frames * 2u * sizeof *copy))) {
            memcpy(copy, dry, (size_t)frames * 2u * sizeof *copy);
            punch_filter(copy, frames, 0, PU_K1);
            wav_write(dir, "punch_k1_clean.wav", copy, frames);
            memcpy(copy, dry, (size_t)frames * 2u * sizeof *copy);
            punch_filter(copy, frames, 1, PU_K1);
            wav_write(dir, "punch_k1_ms20.wav", copy, frames);
            free(copy);
        }
        free(dry);
    }

    /* the insert comb: two bars dry, then two bars of it */
    if ((dry = render_phrase(4, 1, &frames))) {
        snprintf(name, sizeof name, "comb_insert_%s.wav", tag);
        wav_write(dir, name, dry, frames);
        free(dry);
    }
    /* the analog comb on the same phrase, for comparison */
    if ((dry = render_phrase(4, 2, &frames))) {
        wav_write(dir, "comb_analog.wav", dry, frames);
        free(dry);
    }
    /* and the dry four bars */
    if ((dry = render_phrase(4, 0, &frames))) {
        wav_write(dir, "comb_dry.wav", dry, frames);
        free(dry);
    }
    /* a four-bar CUT sweep: insert COMB and ANALOG COMB */
    if ((dry = render_phrase(4, 4, &frames))) {
        wav_write(dir, "comb_sweep_insert.wav", dry, frames);
        free(dry);
    }
    if ((dry = render_phrase(4, 5, &frames))) {
        wav_write(dir, "comb_sweep_analog.wav", dry, frames);
        free(dry);
    }
    return 0;
}
