/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Effects: per-track insert chain (6 reorderable stages) plus shared delay/reverb
 * (SEND for all, or CHAIN on one track). Mono buses, stereo dry mix.
 * Reverb: two tanks in .pool. CHAIN owner runs tank 0 as an insert; other tracks
 * (and all tracks when no CHAIN) SEND into tank 1 when chained, else tank 0.
 * TYPE/SIZE/DAMP are shared globals; each tank keeps its own running model state. */
#define DLY_LEN 65536u           /* 1.49 s: 1/4 fits from 41 BPM, 1/4D from 61, 1/2 from 81 */
static int16_t dly_buf[DLY_LEN] __attribute__((section(".pool")));
static const uint16_t REV_COMB[4] = {1116, 1188, 1277, 1356};
static const uint16_t REV_AP[2] = {556, 441};
static struct {
    uint32_t dly_w;
    int32_t dly_lp;
} fx;

/* SPRING (G_RTYPE 1): one spring of a spring tank, mono like the other buses, in the ROOM's own buffers (no
 * RAM of its own): the input and the loop's return -> a low cut (~110 Hz: a spring carries little bass) ->
 * SP_N stretched first-order allpasses, (a + z^-4) / (1 + a z^-4) (after Valimaki, Parker and Abel: below
 * fs / 8 = 5.5 kHz the group delay rises with frequency, the chirp; each pass round the loop adds more of
 * it: the "boing", the drips) -> the loop's delay line (rev_comb, SP_LEN) -> back through a one-pole
 * low-pass (DAMP) and the decay gain (SIZE). The output: the spring's far end, half way along the loop
 * (the first sound 15 .. 30 ms after the send: the tank's own pre-delay; a slow wobble of a sample or two
 * on it), plus a second, quieter pickup at three quarters (a shorter spring beside it: denser). SIZE sets
 * the loop's length (30 .. 60 ms) and its decay; DAMP the loop's low-pass. The allpasses' states: 4
 * samples each (int32), in rev_ap's memory. Changing the model fades the old one's block out and clears both buffers. */
#define SP_LEN 4096u                     /* the loop's line in rev_comb (4937 samples) */
#define SP_MASK (SP_LEN - 1u)
#define SP_N 10u                         /* allpass stages */
#define SP_A 2867                        /* their coefficient, Q12 (0.7: Q12 keeps (x - o) * a in 32 bits up to
                                          * |x - o| < 749000, far past any peak the chain reaches) */

/* HALL (G_RTYPE 2, 1.2): a smooth, dense, stereo reverb, again in ROOM's buffers (its few states and one block
 * of work in the pool, no .bss). ROOM's 4 combs each ring on their own (the metallic tone); here every echo
 * goes round 8 lines that all feed each other, and they run at half the rate (22.05 kHz: the same memory holds
 * twice the time, so twice the resonances, at half the work; the tail stops near 10 kHz, as a hall's air
 * does). The send (every other sample of it, low-passed 1 2 1; ROOM's level) -> a low cut (~110 Hz: no boom
 * in a long tail) -> 2 allpass diffusers (rev_ap: a drum's click smeared into a burst) -> into the 8 lines
 * (rev_comb, mutually prime lengths, 21 .. 34 ms). Lines 1, 2, 4, 7 are read through slow sines (+-HL_MOD
 * samples, between samples; 0.37 or 0.74 Hz, each in its own phase): the resonances drift instead of ringing.
 * Lines 0, 3, 5, 6, read at their ends, go through a one-pole low-pass (DAMP: the mixing takes every echo
 * through them); every
 * line through its gain (from its length: all decay alike; RT60 below the damping 0.5 s at SIZE 0, 4 s at
 * 90, 9 s at 127; ~2.4 s heard at 90 with DAMP 60); then the 8 x 8 mixing: a conference matrix (Paley,
 * order 8: every line feeds the 7 others, +-1 each, none itself: no flutter at a line's own length), with
 * the send added. Stereo: left lines 0 3 4 -7, right 1 2 5 6 (sums spread over the matrix's rows); the wet
 * bus (mono) gets left + right, rev_side left - right, which mix_block adds to the left and takes from the
 * right (rev_side_mix). Back to 44.1 kHz between samples. Rounding: a loud line's gain rounds to the nearest
 * (the tail decays as it should), a quiet one's towards zero (it ends in exact silence: no limit cycle), and
 * the low-passes keep their steps' remainders (no offset). */
#define HL_NL 8u                         /* lines */
#define HL_NAP 2u                        /* diffusers */
#define HL_NC (CTL / 2u)                 /* a block at half the rate */
#define HL_MOD 8                         /* the taps' swing, samples (read 2 .. 18 samples short of the end) */
#define HL_LPM 0x69u                     /* the lines with a low-pass */
#define HL_KD(d) (32767 - (d) * 180)     /* its coefficient (Q15) for DAMP d: none at 0 .. ~1 kHz at 127 */
#define HL_LOUD 32                       /* a line this loud (two samples' sum) rounds its gain to the nearest */
#define HL_OUT 4                         /* the outputs' gain: the level of ROOM's tail */
static const uint16_t HL_LEN[HL_NL] = {467, 509, 547, 593, 631, 673, 709, 743};   /* (+1 each: the mirror) */
static const uint16_t HL_AP[HL_NAP] = {131, 307};
static const int16_t HL_APG[HL_NAP] = {11469, 10240};               /* Q14: 0.7, 0.625 */
_Static_assert(CTL % 2u == 0u && HL_NC + 2u * HL_MOD + 2u < 467u && HL_NC <= 131u, "HALL: one wrap a block at most");

/* SHIMR / CLOUD grain lines (one model at a time per tank: CLOUD is larger). */
#define SH_LEN 2048u
#define SH_AMT 15200                     /* Q15 ~0.46 (JIANT MOD 80) */
#define CL_LEN 8192u                                     /* half rate: ~0.37 s (POOL: was 16384 / ~0.74 s) */
#define CL_N 12u
#define CL_GAP 1000                                      /* samples between grain births */
#define CL_MOD 48                                        /* chance a grain is pitched (JIANT MOD 48) */

/* One reverb engine's buffers + running state. Tank 0 = CHAIN insert (or sole SEND bus);
 * tank 1 = SEND bus while another track owns REV as CHAIN. */
typedef struct {
    int16_t comb[1116 + 1188 + 1277 + 1356];
    union {                          /* ROOM's allpasses; SPRING's allpass chain (int32: no clamps) */
        int16_t ap[556 + 441];
        int32_t sp[(556 + 441) / 2];
    } u;
    struct {                          /* HALL's state */
        uint16_t i[HL_NL], d[HL_NAP];
        int32_t lp[HL_NL], le[HL_NL];
        int32_t x, m, s, hp, he;
        int16_t g[HL_NL], sz;
        uint32_t ph;
        uint8_t side;
        int32_t sd[CTL];
        int32_t bx[HL_NC], l[HL_NC], r[HL_NC];
        int16_t y[HL_NL][HL_NC];
    } hl;
    union {
        int16_t shim[SH_LEN];
        int16_t cloud[CL_LEN];
    } gran;
    int32_t shim_fb[CTL];
    struct { uint32_t pos, inc; uint16_t len, age; int16_t pl, pr; } cl_g[CL_N];
    uint16_t comb_i[4], ap_i[2];
    int32_t comb_lp[4];
    uint8_t rtype;                       /* model running on this tank (tracks G_RTYPE) */
    uint32_t sh_w, sh_ph;                /* SHIMR */
    uint32_t cl_w, cl_rng;               /* CLOUD */
    int32_t cl_next, cl_lp, cl_half;
    uint16_t sp_w;                       /* SPRING */
    int32_t sp_lp, sp_hp, sp_he, sp_size;
    uint32_t sp_ph;
} rev_tank_t;
static rev_tank_t rev_tank[2] __attribute__((section(".pool")));
static rev_tank_t *R = &rev_tank[0];
#define rev_comb (R->comb)
#define rev_ap (R->u.ap)
#define rev_side (R->hl.sd)
#define rev_gran (R->gran)
#define shim_buf (R->gran.shim)
#define cloud_buf (R->gran.cloud)
#define shim_fb (R->shim_fb)
#define cl_g (R->cl_g)
_Static_assert(sizeof ((rev_tank_t *)0)->comb / 2u >= SP_LEN &&
               sizeof ((rev_tank_t *)0)->u.sp / 4u >= 4u * (SP_N + 1u), "SPRING in ROOM's buffers");
_Static_assert(467 + 509 + 547 + 593 + 631 + 673 + 709 + 743 + HL_NL <= sizeof ((rev_tank_t *)0)->comb / 2u &&
               131 + 307 <= sizeof ((rev_tank_t *)0)->u.ap / 2u, "HALL in ROOM's buffers");
_Static_assert(CL_LEN >= SH_LEN, "CLOUD line holds SHIMR");

/* P_IORD: low 10 bits are the chain order (factorial 0..719). Bits 10..15 are one flag per
 * stage (INS_DIST .. INS_CHOR): 0 the stage is in the chain, 1 it is a parallel send.
 * Flags 0 is every older project. A project with a flag set does not load on firmware that
 * still rejects any P_IORD above 719. */
#define IORD_PERM 1023u
#define IORD_SEND 0xFC00u
static uint16_t iord_bits(const track_t *t)
{
    return (uint16_t)t->p[P_IORD];
}
static uint32_t iord_perm_of(uint16_t raw)
{
    uint32_t n = raw & IORD_PERM;
    return n > 719u ? 0u : n;
}
static int ins_is_send(const track_t *t, uint32_t st)
{
    return st < INS_N && (iord_bits(t) & (uint16_t)(1u << (10u + st))) != 0;
}
static void ins_send_set(track_t *t, uint32_t st, int on)
{
    uint16_t raw, bit;
    if (st >= INS_N)
        return;
    raw = iord_bits(t);
    bit = (uint16_t)(1u << (10u + st));
    raw = on ? (uint16_t)(raw | bit) : (uint16_t)(raw & (uint16_t)~bit);
    if (raw == iord_bits(t)) return;
    motion_fx_route(t, st, 0);
    t->p[P_IORD] = (int16_t)raw;
    motion_fx_route(t, st, 1);
}
static void iord_set_perm(track_t *t, uint32_t n)
{
    uint16_t raw = iord_bits(t);
    if (n > 719u)
        n = 719u;
    t->p[P_IORD] = (int16_t)(uint16_t)((raw & (uint16_t)~IORD_PERM) | (uint16_t)n);
}
/* insert chain order: the low 10 bits of P_IORD, a factorial (Lehmer) number 0..719.
 * ins_fact(k) is k!; callers use ins_fact(INS_N-1-i) so the first slot is weighted by 5!. */
static uint32_t ins_fact(uint32_t n)
{
    static const uint16_t F[6] = {1, 1, 2, 6, 24, 120};   /* 0! .. 5! */
    return n < 6u ? F[n] : 1u;
}
static void ins_order_get(const track_t *t, uint8_t ord[INS_N])
{
    uint32_t n = iord_perm_of(iord_bits(t)), i, j;
    uint8_t av[INS_N] = {0, 1, 2, 3, 4, 5};
    for (i = 0; i < INS_N; i++) {
        uint32_t f = ins_fact(INS_N - 1u - i), idx = n / f;
        n %= f;
        if (idx >= INS_N - i)
            idx = INS_N - 1u - i;
        ord[i] = av[idx];
        for (j = idx; j + 1u < INS_N - i; j++)
            av[j] = av[j + 1u];
    }
}
static void ins_order_set(track_t *t, const uint8_t ord[INS_N])
{
    uint32_t n = 0, i, j;
    uint8_t av[INS_N] = {0, 1, 2, 3, 4, 5};
    for (i = 0; i < INS_N; i++) {
        uint32_t f = ins_fact(INS_N - 1u - i), idx = 0;
        for (j = 0; j < INS_N - i; j++)
            if (av[j] == ord[i]) { idx = j; break; }
        n += idx * f;
        for (j = idx; j + 1u < INS_N - i; j++)
            av[j] = av[j + 1u];
    }
    iord_set_perm(t, n);
}
/* swap chain slots at highlight h with h+dir (dir ±1); REORDER KNOB 2 */
static void ins_order_move(track_t *t, uint32_t h, int32_t dir)
{
    uint8_t ord[INS_N];
    uint32_t n = h + (uint32_t)dir;
    if (h >= INS_N || n >= INS_N)
        return;
    ins_order_get(t, ord);
    { uint8_t tmp = ord[h]; ord[h] = ord[n]; ord[n] = tmp; }
    ins_order_set(t, ord);
}
/* G_DLYOWN: 0 = SEND for every track; 1..4 = that track has CHAIN (others USED).
 * G_REVOWN: 0 = SEND for every track; 1..4 = that track has CHAIN (others still SEND into tank 1).
 * G_SLOWN:  per-track SEND/CHAIN mask (core.h sl_trk_*); bus_own(2) is legacy-only. */
static int32_t bus_own(uint32_t which)   /* 0 delay, 1 reverb, 2 slicer(legacy); -1 SEND, else track index */
{
    int32_t v = song.g[which == 2u ? G_SLOWN : which ? G_REVOWN : G_DLYOWN];
    if (which == 2u) {                               /* after sanitize: mask, not an owner index */
        if (v >= 1 && v <= (int32_t)NTRK)
            return v - 1;
        return -1;
    }
    return v >= 1 && v <= (int32_t)NTRK ? v - 1 : -1;
}
static void bus_own_set(uint32_t which, int32_t tr)   /* tr -1 = SEND; else claim for track tr */
{
    uint32_t k, st = which == 2u ? INS_SLCR : INS_REV;
    if (which)
        for (k = 0; k < NTRK; k++) motion_fx_route(&trk[k], st, 0);
    if (which == 2u) {                               /* SLICER: per-track toggle lives in ui_input */
        if (tr < 0)
            song.g[G_SLOWN] = (int16_t)((1u << NTRK) - 1u);
        else if (tr < (int32_t)NTRK)
            song.g[G_SLOWN] = (int16_t)(((1u << NTRK) - 1u) ^ (1u << (uint32_t)tr));
        for (k = 0; k < NTRK; k++) motion_fx_route(&trk[k], st, 1);
        return;
    }
    song.g[which ? G_REVOWN : G_DLYOWN] =
        tr < 0 || tr >= (int32_t)NTRK ? 0 : (int16_t)(tr + 1);
    if (which)
        for (k = 0; k < NTRK; k++) motion_fx_route(&trk[k], st, 1);
}
static void bus_own_sanitize(void)
{
    if (song.g[G_DLYOWN] < 0 || song.g[G_DLYOWN] > (int16_t)NTRK)
        song.g[G_DLYOWN] = 0;
    if (song.g[G_REVOWN] < 0 || song.g[G_REVOWN] > (int16_t)NTRK)
        song.g[G_REVOWN] = 0;
    sl_own_sanitize();
}
/* P_CHOR packs EFFECTS DLY/REV among the 8 non-SLCR slots (bit6 marks the layout; low 3 = DLY,
 * next 3 = REV). SLCR's absolute slot is track.fx_sl_pos (0..8; default 8; FUN14 project wire).
 * Older CHO send values (0..63) load as DLY at 6, REV at 7, SLCR at 8. */
#define FX_POS_MARK 64
static void fx_pos_get(const track_t *t, uint32_t *dp, uint32_t *rp, uint32_t *sp)
{
    int32_t v = t->p[P_CHOR];
    uint32_t d8, r8, s;
    s = t->fx_sl_pos < FX_LIST_N ? t->fx_sl_pos : 8u;
    if (v < FX_POS_MARK) {
        d8 = 6;
        r8 = 7;
        s = 8;
    } else {
        v -= FX_POS_MARK;
        d8 = (uint32_t)v & 7u;
        r8 = ((uint32_t)v >> 3) & 7u;
        if (r8 == d8)
            r8 = d8 + 1u < 8u ? d8 + 1u : 0u;
    }
    *sp = s;
    *dp = d8 < s ? d8 : d8 + 1u;                     /* absolute slots: skip SLCR's */
    *rp = r8 < s ? r8 : r8 + 1u;
}
static void fx_pos_set(track_t *t, uint32_t dp, uint32_t rp, uint32_t sp)
{
    uint32_t d8, r8;
    if (sp >= FX_LIST_N)
        sp = 8u;
    if (dp >= FX_LIST_N)
        dp = 6u;
    if (rp >= FX_LIST_N)
        rp = 7u;
    if (dp == sp)
        dp = sp > 0u ? sp - 1u : sp + 1u;
    if (rp == sp || rp == dp)
        rp = (sp + 1u) % FX_LIST_N == dp ? (sp + 2u) % FX_LIST_N : (sp + 1u) % FX_LIST_N;
    d8 = dp < sp ? dp : dp - 1u;
    r8 = rp < sp ? rp : rp - 1u;
    if (d8 > 7u)
        d8 = 7u;
    if (r8 > 7u)
        r8 = 7u;
    if (r8 == d8)
        r8 = d8 + 1u < 8u ? d8 + 1u : 0u;
    t->p[P_CHOR] = (int16_t)(FX_POS_MARK + (int32_t)d8 + (int32_t)(r8 << 3));
    t->fx_sl_pos = (uint8_t)sp;
}
/* EFFECTS list: 6 inserts (P_IORD) with DLY/REV/SLCR slotted via P_CHOR. */
static void fx_list_get(const track_t *t, uint8_t ord[FX_LIST_N])
{
    uint8_t ins[INS_N], used[FX_LIST_N];
    uint32_t i, j = 0, dp, rp, sp;
    ins_order_get(t, ins);
    fx_pos_get(t, &dp, &rp, &sp);
    for (i = 0; i < FX_LIST_N; i++)
        used[i] = 0;
    ord[dp] = INS_DLY;
    used[dp] = 1;
    ord[rp] = INS_REV;
    used[rp] = 1;
    ord[sp] = INS_SLCR;
    used[sp] = 1;
    for (i = 0; i < FX_LIST_N; i++) {
        if (used[i])
            continue;
        ord[i] = ins[j++];
    }
}
static void fx_list_set(track_t *t, const uint8_t ord[FX_LIST_N])
{
    uint8_t ins[INS_N];
    uint32_t i, j = 0, dp = 6, rp = 7, sp = 8;
    for (i = 0; i < FX_LIST_N; i++) {
        if (ord[i] == INS_DLY)
            dp = i;
        else if (ord[i] == INS_REV)
            rp = i;
        else if (ord[i] == INS_SLCR)
            sp = i;
        else if (j < INS_N)
            ins[j++] = ord[i];
    }
    if (j != INS_N)
        for (j = 0; j < INS_N; j++)
            ins[j] = (uint8_t)j;
    fx_pos_set(t, dp, rp, sp);
    ins_order_set(t, ins);
}
/* swap list slots h and h+dir; 1 if swapped, 0 if out of range (ORDER keeps the highlight) */
static int fx_list_move(track_t *t, uint32_t h, int32_t dir)
{
    uint8_t ord[FX_LIST_N];
    uint32_t n = h + (uint32_t)dir;
    if (h >= FX_LIST_N || n >= FX_LIST_N)
        return 0;
    fx_list_get(t, ord);
    { uint8_t tmp = ord[h]; ord[h] = ord[n]; ord[n] = tmp; }
    fx_list_set(t, ord);
    return 1;
}
static int32_t ins_mix_of(const track_t *t, uint32_t st)
{
    static const uint8_t MIX[INS_N] = {P_IDMIX, P_IFMIX, P_ICMIX, P_IPMIX, P_ILMIX, P_IHMIX};
    if (st == INS_DLY)
        return t->p[P_DLY];
    if (st == INS_REV)
        return t->p[P_REV];
    if (st == INS_SLCR)
        return t->p[P_SLDEPTH];
    return st < INS_N ? t->p[MIX[st]] : 0;
}
/* mode string for EFFECTS: SEND, CHAIN, or USED (delay owned elsewhere; slicer never USED after sanitize).
 * REV non-owners stay SEND (second tank) while another track has CHAIN. */
static const char *fx_list_mode(const track_t *t, uint32_t st)
{
    int32_t own, tr = (int32_t)(t - trk);
    if (st < INS_N)
        return ins_is_send(t, st) ? "SEND" : "CHAIN";
    if (st == INS_SLCR) {
        if (sl_trk_used(t))
            return "USED";
        return sl_trk_send(t) ? "SEND" : "CHAIN";
    }
    own = bus_own(st == INS_REV ? 1u : 0u);
    if (own < 0)
        return "SEND";
    if (own == tr)
        return "CHAIN";
    return st == INS_REV ? "SEND" : "USED";
}
static int ins_any(const track_t *t)
{
    uint32_t s;
    for (s = 0; s < INS_N; s++)
        if (ins_mix_of(t, s))
            return 1;
    return 0;
}

/* master: peak limiter in front of the soft clipper. Fast attack (~0.1 ms),
 * ~150 ms release, threshold where tanh is still nearly linear, so chords
 * get quieter instead of crushed. */
#define LIM_T 18000
static int32_t lim_env = LIM_T;
/* (JIANT) CLIP drive coeffs (clip_block after mix buffers); DUCK state (duck_block) */
static int32_t clip_g, clip_mk;
static int32_t duck_env, duck_g0 = 32767, duck_g1 = 32767;
static uint8_t duck_att, duck_hold;
/* MENU > USB LEVEL FIXED (for #42: record over USB with the speaker turned down): the mix goes to master_out at the
 * full MASTER level, USB audio takes that (audio.c uac_tap), and only then does MASTER scale what the DAC gets
 * (usb_fixed_dac). MASTER (0, the default): MASTER before master_out, as always (USB follows the knob) */
static volatile uint8_t fx_usb_fixed;
#define MASTER_FULL 4096                /* main.c: the MASTER knob's top, Q12 */
/* the MASTER pot (ADC 0..1023, smoothed in main.c) to its level, square law: 0 .. 4088 (Q12) */
static inline uint32_t master_of_pot(uint32_t k10) { return (k10 * k10) >> 8; }
static __attribute__((noinline)) void usb_fixed_dac(int32_t *out, uint32_t n)   /* audio ISR, after uac_tap */
{
    uint32_t i;
    int32_t m = (int32_t)song.master_q12;
    for (i = 0; i < 2u * n; i++)
        out[i] = (out[i] * m) >> 12;                /* (|out| <= 32767 after the soft clip: fits) */
}
static volatile uint8_t fx_lowcut;     /* settings: 1 LOWCUT 12 dB/oct ~110 Hz, 2 BASS+ (the small speaker):
                                        * 12 dB/oct ~220 Hz plus the harmonics of the bass (spk_bass) */
static int32_t lc_l1, lc_l2, lc_r1, lc_r2, dc_l, dc_r, dce_l, dce_r;

/* DC blocker (~2 Hz), always on: a leaky integrator of the input (Q6 state) subtracted from it.
 * The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next one, so no
 * part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (A rounded step of (x - dc) / 4096 would stop moving at |x - dc| < 2048 and leave an offset of up
 * to +-31 at the output after silence.) */
static inline int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = (x << 6) - *dc + *err, d = e >> 12;
    *err = e - (d << 12);
    *dc += d;
    return x - ((*dc + 32) >> 6);
}

static int32_t lce[4];
static inline int32_t lowcut1(int32_t x, int32_t *lc, int32_t *err, uint32_t sh)   /* x minus its one-pole low-pass */
{
    int32_t e = x - *lc + *err, d = e >> sh;
    *err = e - (d << sh);
    *lc += d;
    return x - *lc;
}

/* BASS+: what the speaker cannot play, heard through its harmonics. The bass below ~150 Hz is clipped at its
 * own envelope (a level-following trapezoid: odd harmonics), then band-passed ~220 Hz..1 kHz and added.
 * The low-pass has 4 poles (#42: with 2, the trapezoid rebuilt the 300 .. 600 Hz of the mix itself, late,
 * and cancelled up to 6 dB of it; now under 0.5 dB) */
static int32_t sb_lp1, sb_lp2, sb_lp3, sb_lp4, sb_env, sb_h1, sb_h2, sb_hl;
static inline int32_t spk_bass(int32_t m)
{
    int32_t a, t, u;
    sb_lp1 += ((m - sb_lp1) * 692) >> 15;
    sb_lp2 += ((sb_lp1 - sb_lp2) * 692) >> 15;
    sb_lp3 += ((sb_lp2 - sb_lp3) * 692) >> 15;
    sb_lp4 += ((sb_lp3 - sb_lp4) * 692) >> 15;
    a = sb_lp4 < 0 ? -sb_lp4 : sb_lp4;
    if (a > sb_env)
        sb_env += (a - sb_env) >> 2;
    else if (sb_env > 0)
        sb_env -= (sb_env >> 11) + 1;
    t = clamp(sb_lp4 * 8, -sb_env, sb_env);
    sb_h1 += (t - sb_h1) >> 5;
    u = t - sb_h1;
    sb_h2 += (u - sb_h2) >> 5;
    u -= sb_h2;
    sb_hl += (u - sb_hl) >> 3;
    return sb_hl + (sb_hl >> 1);      /* x1.5 (#180: x3 peaked at 1.5 .. 3.5 x a full-scale kick, the limiter */
}                                     /* pulled the mix down up to 16 dB on each hit and the buzz took over) */

static inline void master_out(int32_t *l, int32_t *r)
{
    int32_t al, ar, a;
    *l = dc_block(*l, &dc_l, &dce_l);
    *r = dc_block(*r, &dc_r, &dce_r);
    if (fx_lowcut) {                  /* two one-pole high-passes, error feedback as dc_block (the */
        uint32_t sh = fx_lowcut == 2u ? 5u : 6u;    /* rounded step stopped at |x - lc| < 32: an offset) */
        int32_t b = fx_lowcut == 2u ? spk_bass((*l + *r) >> 1) : 0;
        *l = lowcut1(*l, &lc_l1, &lce[0], sh);
        *l = lowcut1(*l, &lc_l2, &lce[1], sh) + b;
        *r = lowcut1(*r, &lc_r1, &lce[2], sh);
        *r = lowcut1(*r, &lc_r2, &lce[3], sh) + b;
    }
    al = *l < 0 ? -*l : *l;
    ar = *r < 0 ? -*r : *r;
    a = al > ar ? al : ar;
    if (a > lim_env)
        lim_env += (a - lim_env) >> 2;
    else if (lim_env > LIM_T)
        lim_env -= ((lim_env - LIM_T) >> 12) + 1;
    if (lim_env > LIM_T) {
        int32_t g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)lim_env);   /* < 32768 */
        *l = ((*l >> 4) * g) >> 11;                      /* >> 4 first: |l| may be far above Q15 */
        *r = ((*r >> 4) * g) >> 11;
    }
    *l = softclip(*l);
    *r = softclip(*r);
}

/* length of one division (N_DIV order) in samples at the song tempo */
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* original IDs stay fixed; slow rates append */
static uint32_t midi_beat_samples;                    /* zero until an external clock has a measured tempo */
static uint32_t beat_samples(void)
{
    return song.g[G_CLOCK] && midi_beat_samples ? midi_beat_samples : (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM];
}
static uint32_t div_samples(uint32_t div)
{
    uint32_t quarter = beat_samples();
    return div < 6u ? quarter / DIV_DEN[div] : div < 10u ? quarter << (div - 5u) : quarter / DIV_DEN[div % 6u];
}

#include "perform.c"                                 /* the FX hold layer's effects (the master) */
#include "pfx.c"                                     /* .. its punch-in MIDI effects (on the notes) */
#include "click.c"                                   /* the metronome's click (after the master: audio.c) */

/* the delay's TIME (params.c N_DTIME): N_DIV's ten (div_samples), then 1.5.1's dotted 1/8D 1/16D 1/4D (10, 11, 12: 3/4,
 * 3/8, 3/2 of a quarter); at most DLY_LEN - 1 samples (1.486 s): 1/4D clamps at 60 BPM and below (1/2 below 81).
 * G_DTIME stays that enum (0..12, the descriptor) in RATE mode. TIME mode stores a free millisecond at
 * DTIME_FREE + ms (1 .. DTIME_MS_MAX), in the same global: 13..63 stay free so another division can still be
 * appended. A project load keeps a free-ms value (dtime_load); the descriptor's max stays 12. */
#define DTIME_FREE 64
#define DTIME_MS_MAX ((DLY_LEN - 1u) * 1000u / (uint32_t)FS)   /* 1486 ms: the line, one millisecond at a time */
static const uint8_t DIV_DOT[3] = {2, 3, 1};           /* 3 quarters >> this */
static int dtime_free(void) { return song.g[G_DTIME] >= DTIME_FREE; }
static uint32_t dtime_ms(void)
{
    int32_t m = (int32_t)song.g[G_DTIME] - DTIME_FREE;
    if (m < 1)
        m = 1;
    if (m > (int32_t)DTIME_MS_MAX)
        m = (int32_t)DTIME_MS_MAX;
    return (uint32_t)m;
}
static void dtime_set_ms(uint32_t ms)
{
    if (ms < 1u)
        ms = 1u;
    if (ms > DTIME_MS_MAX)
        ms = DTIME_MS_MAX;
    song.g[G_DTIME] = (int16_t)(DTIME_FREE + (int32_t)ms);
}
/* free milliseconds as a fractional delay (Q8 samples). One millisecond is 44.1 samples: the fraction is the
 * time, not a rounded step */
static uint32_t dtime_q8(void)
{
    uint32_t num = dtime_ms() * (uint32_t)FS;
    uint32_t q = (num / 1000u << 8) + (num % 1000u) * 256u / 1000u;
    if (q < 16u << 8)
        q = 16u << 8;
    if (q > (DLY_LEN - 1u) << 8)
        q = (DLY_LEN - 1u) << 8;
    return q;
}
static int16_t dtime_load(int32_t v)               /* a saved free-ms time survives the descriptor's 0..12 */
{
    if (v >= DTIME_FREE && v <= DTIME_FREE + (int32_t)DTIME_MS_MAX)
        return (int16_t)v;
    return (int16_t)clamp(v, GP[G_DTIME].min, GP[G_DTIME].max);
}
static uint32_t delay_samples(void)
{
    uint32_t d, s;
    if (dtime_free())
        return (dtime_q8() + 128u) >> 8;
    d = (uint32_t)song.g[G_DTIME];
    s = d - 10u < 3u ? (beat_samples() * 3u) >> DIV_DOT[d - 10u] : div_samples(d);
    return s < 16u ? 16u : s >= DLY_LEN ? DLY_LEN - 1u : s;
}

/* The stage owning a shared SEND setting; MIX always stays on its track. */
static uint32_t fx_target_stage(uint32_t id)
{
    if (id == P_DIST || id == P_ED_FX || id == P_IDMODE) return INS_DIST;
    if (id >= P_IFTYPE && id <= P_IFRES) return INS_FILTER;
    if (id >= P_ICBITS && id <= P_ICLPF) return INS_CRUSH;
    if (id >= P_IPRATE && id <= P_IPFBK) return INS_PHASR;
    if (id >= P_ILRATE && id <= P_ILDEP) return INS_FLANG;
    if (id >= P_IHRATE && id <= P_IHDEP) return INS_CHOR;
    if (id == P_SLPAT || id == P_SLRATE) return INS_SLCR;
    return FX_LIST_N;
}
/* Motion ids identify settings without adding track parameters to the frozen project layout. */
static int16_t *fx_target_ptr(const track_t *t, uint32_t id)
{
    uint32_t st;
    if (MFX_PARAM(id)) {
        static const uint8_t G[] = {G_DTIME, G_DFDBK, G_DCOLOR, G_RSIZE, G_RDAMP};
        uint32_t g = G[id - MFX_TIME];
        if (id >= MFX_SIZE && bus_own(1) == (int32_t)(t - trk))
            g = id == MFX_SIZE ? G_CRSIZE : G_CRDAMP;
        return &song.g[g];
    }
    if ((id == P_SLPAT || id == P_SLRATE) && sl_trk_send(t))
        return &fx_bus_trk.p[id];
    st = fx_target_stage(id);
    if (st < INS_N && ins_is_send(t, st))
        return &fx_bus_trk.p[id];
    return (int16_t *)&t->p[id];
}
/* the knob the matrix writes while a part renders: its copy of the shared ones (core.h bus_knobs), its own
 * p[] otherwise. The real shared knobs stay the UI's, motion's and the bus DSP's. */
static int16_t *mod_target_ptr(const track_t *t, uint32_t id)
{
    uint32_t st;
    if (MFX_PARAM(id))
        return &bus_knobs[C1_PI(t)].g[id - MFX_TIME];
    if ((id == P_SLPAT || id == P_SLRATE) && sl_trk_send(t))
        return &bus_knobs[C1_PI(t)].p[id];
    st = fx_target_stage(id);
    if (st < INS_N && ins_is_send(t, st))
        return &bus_knobs[C1_PI(t)].p[id];
    return (int16_t *)&t->p[id];
}
/* mod_begin: the SEND insert / slicer knobs of a part's copy, as they are before its modulation (fx_bus_trk.p
 * is never written while the parts render: the matrix moves the copies) */
static void bus_knobs_fill(const track_t *t)
{
    memcpy(bus_knobs[C1_PI(t)].p, fx_bus_trk.p, sizeof bus_knobs[0].p);
}
/* The DLY / REV bus settings of a part's copy (core.h bus_knobs). Every part's, at the block's start before the
 * split (fx.c mix_block, after events_block: motion's values are in): the CHAIN owners put their copies into song.g
 * while they render (fx_bus_stage), so nothing else may read the real ones then. */
static void bus_knobs_g(const track_t *t)
{
    bus_knobs_t *b = &bus_knobs[C1_PI(t)];
    uint32_t i;
    for (i = 0; i < 5u; i++)
        b->g[i] = *fx_target_ptr(t, MFX_TIME + i);
}
/* RATE's selected division is the starting point for continuous TIME modulation and locks. */
static int16_t fx_time_value(int16_t raw)
{
    uint32_t d = (uint32_t)raw, s;
    if (raw >= DTIME_FREE) return raw;
    s = d - 10u < 3u ? (beat_samples() * 3u) >> DIV_DOT[d - 10u] : div_samples(d);
    s = s < 16u ? 16u : s >= DLY_LEN ? DLY_LEN - 1u : s;
    return (int16_t)(DTIME_FREE + clamp((int32_t)((s * 1000u + FS / 2u) / FS),
                                      1, (int32_t)DTIME_MS_MAX));
}
static const param_desc_t *fx_target_desc(const track_t *t, uint32_t id)
{
    static const param_desc_t TIME = {"TIME", F_INT, DTIME_FREE + 1, DTIME_FREE + DTIME_MS_MAX,
                                    DTIME_FREE + 250, 0, "ms"};
    if (id == MFX_TIME)
        return &TIME;
    if (MFX_PARAM(id)) {
        static const uint8_t G[] = {G_DTIME, G_DFDBK, G_DCOLOR, G_RSIZE, G_RDAMP};
        return &GP[G[id - MFX_TIME]];
    }
    return id >= P_E0 ? &ENGINES[t->engine]->edit[id - P_E0] : &TP[id];
}
/* These settings are consumed by the SEND DSP after all track renders. */
static int fx_target_send(const track_t *t, uint32_t id)
{
    if (id >= MFX_TIME && id <= MFX_COLOR)
        return bus_own(0) < 0;
    if (id == MFX_SIZE || id == MFX_DAMP)
        return bus_own(1) != (int32_t)(t - trk);
    return (id == P_SLPAT || id == P_SLRATE) && sl_trk_send(t);
}
/* Free-ms read position (Q8). The first block after RATE snaps to the loaded time. Subsequent changes follow
 * the knob: 1/256 of the remaining error per sample, capped at 224 Q8 units, so the read head keeps up with a
 * quick turn. The tape pitch is therefore how fast the knob turns, not how far it moves: 455 -> 25 ms in half
 * a second lands in half a second and swoops about 10.9 semitones (36 the other way: the head can speed up
 * freely but barely slow). */
#define DLY_EASE 8u
#define DLY_SLEW 224u
static uint32_t dly_q8;
static uint8_t dly_free;

/* Active tank's globals: tank 0 uses CHAIN params while someone owns CHAIN; else SEND. Tank 1 is always SEND. */
static int rev_use_chain_g(void) { return R == &rev_tank[0] && bus_own(1) >= 0; }
static int32_t rev_pg_size(void) { return song.g[rev_use_chain_g() ? G_CRSIZE : G_RSIZE]; }
static int32_t rev_pg_damp(void) { return song.g[rev_use_chain_g() ? G_CRDAMP : G_RDAMP]; }
/* clamp TYPE to a known model (out of range -> ROOM) */
static int32_t rev_rtype(void)
{
    int32_t rt = song.g[rev_use_chain_g() ? G_CRTYPE : G_RTYPE];
    return (uint32_t)rt <= 4u ? rt : 0;
}

/* ROOM (G_RTYPE 0): 4 damped combs + 2 allpasses (Freeverb-like, mono), added to out */
static __attribute__((noinline)) void rev_room(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k;
    int32_t size = 25000 + rev_pg_size() * 50, damp = 32767 - rev_pg_damp() * 200;
    for (i = 0; i < n; i++) {
        int32_t a = 0;
        int16_t *c = rev_comb;
        int32_t in = mulq15(rev_in[i], 2580);           /* 1/8 at -4 dB: level as before the allpass fix */
        for (k = 0; k < 4u; k++) {
            int32_t o = c[R->comb_i[k]];
            R->comb_lp[k] = o + mulq15(R->comb_lp[k] - o, 32767 - damp);
            c[R->comb_i[k]] = (int16_t)clamp(in + mulq15(R->comb_lp[k], size), -32768, 32767);
            if (++R->comb_i[k] >= REV_COMB[k])
                R->comb_i[k] = 0;
            a += o;
            c += REV_COMB[k];
        }
        c = rev_ap;
        for (k = 0; k < 2u; k++) {
            int32_t o = c[R->ap_i[k]];
            int32_t v = a + (o >> 1);
            c[R->ap_i[k]] = (int16_t)clamp(v, -32768, 32767);
            a = o - a;                                  /* Freeverb: out = buf - in (o - v is a notch comb) */
            if (++R->ap_i[k] >= REV_AP[k])
                R->ap_i[k] = 0;
            c += REV_AP[k];
        }
        out[i] += a;
    }
}

/* SPRING (see the top), added to out */
static __attribute__((noinline)) void rev_spring(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k, s = (uint32_t)rev_pg_size();
    int32_t g = 19661 + (int32_t)s * 85;                /* the loop's gain: 0.6 .. 0.93 */
    int32_t kl = 26000 - rev_pg_damp() * 160;           /* its low-pass: ~9 kHz .. ~1.3 kHz */
    int32_t len = (int32_t)(1323u + ((s * 1323u) >> 7)) << 8, L, L2, L3, f, w;
    int16_t *ln = rev_comb;
    int32_t *ap = R->u.sp;
    if (!R->sp_size)
        R->sp_size = len;
    R->sp_size += clamp(len - R->sp_size, -256, 256);   /* SIZE glides (a sample a block at most) */
    L = R->sp_size >> 8;
    R->sp_ph += 2u * LFO_INC[24];                       /* the wobble: a slow sine, 1.5 samples deep */
    w = (R->sp_size >> 1) + ((osc_sine(R->sp_ph) * 3) >> 8);   /* the far end, Q8 */
    L2 = w >> 8;
    f = w & 255;
    L3 = (L * 3) >> 2;
    for (i = 0; i < n; i++) {
        uint32_t wp = R->sp_w, j = (wp & 3u) * (SP_N + 1u);
        int32_t x = mulq15(rev_in[i], 2580), r = ln[(wp - (uint32_t)L) & SP_MASK], p, o;
        int32_t t0 = ln[(wp - (uint32_t)L2) & SP_MASK], t1 = ln[(wp - (uint32_t)L2 - 1u) & SP_MASK];
        R->sp_lp += mulq15(r - R->sp_lp, kl);
        o = R->sp_lp * g;
        x += (o + ((o >> 31) & 32767)) >> 15;           /* towards 0: a loop of floors would hold an offset */
        o = x - R->sp_hp + R->sp_he;                    /* the low cut, its step's remainder kept (as */
        R->sp_he = o & 63;                              /* dc_block): no dead band to hold an offset in the loop */
        R->sp_hp += o >> 6;
        x -= R->sp_hp;
        p = ap[j];                                      /* the chain: ap[j + k], stage k's output 4 samples ago */
        ap[j] = x;
        for (k = 1; k <= SP_N; k++) {                   /* (lossless: bounded by the loop's input, no clamp) */
            int32_t v = (x - ap[j + k]) * SP_A;         /* towards 0, as the loop's gain: floors would feed */
            o = ap[j + k];                              /* the loop a little offset and noise for ever */
            x = ((v + ((v >> 31) & 4095)) >> 12) + p;
            p = o;
            ap[j + k] = x;
        }
        ln[wp & SP_MASK] = (int16_t)clamp(x, -32768, 32767);
        R->sp_w = (uint16_t)(wp + 1u);
        out[i] += (t0 + (((t1 - t0) * f) >> 8)) * 4 + ln[(wp - (uint32_t)L3) & SP_MASK] * 2;
    }
}

/* HALL's helpers: m / 2^sh rounded towards zero (a product that feeds back must not floor: a loop of floors
 * holds an offset and a little noise for ever) */
static inline int32_t sh_tz(int32_t m, uint32_t sh) { return (m + ((m >> 31) & ((1 << sh) - 1))) >> sh; }
/* 2^-x, x Q16 (0 .. 16): Q15 (32768 at x = 0). 2^-x = 2^u / 2^(i + 1), u = 1 - frac(x); 2^u by a cubic (1e-4) */
static uint32_t exp2_neg(uint32_t x)
{
    uint32_t i = x >> 16, u = 32768u - ((x & 0xFFFFu) >> 1), p = 2579u;
    p = ((p * u) >> 15) + 7409u;
    p = ((p * u) >> 15) + 22780u;
    p = ((p * u) >> 15) + 32768u;
    return i < 15u ? p >> (i + 1u) : 0u;
}
/* the lines' gains for SIZE s: RT60 = 0.5 s * 2^(s / 30); a line of D samples (22.05 kHz) loses 60 dB in
 * RT60 * 22050 / D passes: g = 2^(-D * 3 log2(10) / (22050 * RT60)) = 2^(-D * 59.2 / 65536 * 2^(-s / 30)),
 * at most 0.99, over sqrt(7) (the matrix's rows have 7 entries of +-1: / sqrt(7) makes it orthogonal) */
static __attribute__((noinline)) void hall_gains(int32_t s)
{
    uint32_t q = exp2_neg((uint32_t)s * 65536u / 30u), k, g;
    for (k = 0; k < HL_NL; k++) {
        g = exp2_neg((HL_LEN[k] * 59u * q) >> 15);
        R->hl.g[k] = (int16_t)(((g > 32440u ? 32440u : g) * 6193u) >> 15);
    }
    R->hl.sz = (int16_t)(s + 1);
}

/* an allpass diffuser over x[0 .. n) from q (its next samples, no wrap): its state rounded towards zero */
static inline void hall_ap(int16_t *q, int32_t g, int32_t *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y = q[i], v = x[i] + sh_tz(y * g, 14);
        q[i] = (int16_t)clamp(v, -32768, 32767);
        x[i] = y - ((v * g + 8192) >> 14);
    }
}
/* a line's end over y[0 .. n) from b (no wrap; b[n] is still the line's: its mirror at the end): between b[i]
 * and b[i + 1], f / 256 of the way (rounded to the nearest: it stays between the two), times its gain g (Q14).
 * The gain's product: rounded to the nearest while the line is loud (lo 0: the tail decays as it should; a
 * truncation would make it die early, the sooner the quieter), towards zero once it is quiet (lo 1: the last
 * steps of the tail, which then ends in exact silence) */
static inline void hall_end(const int16_t *b, int32_t f, int16_t *y, uint32_t n, int32_t g, int lo)
{
    uint32_t i;
    if (lo)
        for (i = 0; i < n; i++)
            y[i] = (int16_t)sh_tz((b[i] + (((b[i + 1u] - b[i]) * f + 128) >> 8)) * g, 14);
    else
        for (i = 0; i < n; i++)
            y[i] = (int16_t)(((b[i] + (((b[i + 1u] - b[i]) * f + 128) >> 8)) * g + 8192) >> 14);
}
/* a line's end over y[0 .. n) from b (no wrap), through the low-pass (kd, Q15; the state *lp and its step's
 * remainder *le), times its gain g (rounded as hall_end's) */
static inline void hall_end_lp(const int16_t *b, int16_t *y, uint32_t n, int32_t g, int lo, int32_t *lp, int32_t *le,
                               int32_t kd)
{
    uint32_t i;
    int32_t l = *lp, r = *le, e;
    if (lo)
        for (i = 0; i < n; i++) {
            e = (b[i] - l) * kd + r, r = e & 32767, l += e >> 15;
            y[i] = (int16_t)sh_tz(l * g, 14);
        }
    else
        for (i = 0; i < n; i++) {
            e = (b[i] - l) * kd + r, r = e & 32767, l += e >> 15;
            y[i] = (int16_t)((l * g + 8192) >> 14);
        }
    *lp = l;
    *le = r;
}
/* the block's HL_NC samples of a circular buffer of len from index d in at most two runs, no wrap inside them:
 * body with s (the run's start in the block) and k (its length); d moves on */
#define HL_RUNS(d, len, k, s, body)                                                         \
    for (s = 0; s < HL_NC; s += k) {                                                        \
        k = (len) - (d) < HL_NC - s ? (len) - (d) : HL_NC - s;                              \
        body;                                                                               \
        d = (d) + k >= (len) ? 0u : (d) + k;                                                \
    }

/* HALL (see the top): a block of the send (n = CTL samples: audio.c) -> the mid added to out, the side into
 * rev_side. At half the rate (HL_NC samples) in passes over the block, each with few states (every line is
 * longer than a block: what the lines give in this block was written before it): the send, every other
 * sample, low cut, diffusers (R->hl.bx); the lines' ends at their drifting taps, low-passed, scaled (R->hl.y); the
 * outputs and the matrix; back into the lines; the outputs back to 44.1 kHz. */
#define HL_LFO ((uint32_t)(0.37 * CTL / FS * 4294967296.0))   /* the drift: 0.37 Hz (even lines), 0.74 Hz (odd) */
static __attribute__((noinline)) void rev_hall(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    int16_t *b = rev_comb, *a = rev_ap;
    uint32_t i, k, s, m, d;
    int32_t kd = HL_KD(rev_pg_damp());
    int32_t xp = R->hl.x, hp = R->hl.hp, he = R->hl.he, mp = R->hl.m, sp = R->hl.s;
    (void)n;
    if (R->hl.sz != rev_pg_size() + 1)
        hall_gains(rev_pg_size());
    for (i = 0; i < HL_NC; i++) {
        int32_t x = (xp + 2 * rev_in[2u * i] + rev_in[2u * i + 1u]) >> 2, v;   /* every other sample, 1 2 1 */
        xp = rev_in[2u * i + 1u];
        x = mulq15(clamp(x, -262143, 262143), 2580);    /* (ROOM's level) */
        v = x - hp + he;                                /* the low cut, its step's remainder kept */
        he = v & 31;
        hp += v >> 5;
        R->hl.bx[i] = x - hp;
    }
    R->hl.x = xp, R->hl.hp = hp, R->hl.he = he;
    for (k = 0; k < HL_NAP; k++) {                      /* the diffusers */
        d = R->hl.d[k];
        HL_RUNS(d, HL_AP[k], m, s, hall_ap(a + d, HL_APG[k], R->hl.bx + s, m));
        R->hl.d[k] = (uint16_t)d;
        a += HL_AP[k];
    }
    R->hl.ph += HL_LFO;
    for (k = 0; k < HL_NL; k++) {                       /* the lines' ends */
        int32_t g = R->hl.g[k], f = 0, lo;
        if ((HL_LPM >> k) & 1u)
            d = R->hl.i[k];                                /* (low-passed: the line's end) */
        else {                                          /* (drifting: HL_MOD + 2 +- HL_MOD samples short of it) */
            uint32_t r = (uint32_t)(((HL_MOD + 2) << 8) +
                                    ((osc_sine(R->hl.ph * (1u + (k & 1u)) + k * 0x2C000000u) * HL_MOD) >> 7));
            f = (int32_t)(r & 255u);
            d = R->hl.i[k] + (r >> 8);
            d = d >= HL_LEN[k] ? d - HL_LEN[k] : d;
        }
        s = d + HL_LEN[k] / 2u;                         /* quiet? (two samples half a line apart) */
        s = s >= HL_LEN[k] ? s - HL_LEN[k] : s;
        lo = (b[d] < 0 ? -b[d] : b[d]) + (b[s] < 0 ? -b[s] : b[s]) < HL_LOUD;
        if ((HL_LPM >> k) & 1u) {
            HL_RUNS(d, HL_LEN[k], m, s, hall_end_lp(b + d, R->hl.y[k] + s, m, g, lo, &R->hl.lp[k], &R->hl.le[k], kd));
        } else {
            HL_RUNS(d, HL_LEN[k], m, s, hall_end(b + d, f, R->hl.y[k] + s, m, g, lo));
        }
        b += HL_LEN[k] + 1u;
    }
    for (i = 0; i < HL_NC; i++) {
        int32_t y0 = R->hl.y[0][i], y1 = R->hl.y[1][i], y2 = R->hl.y[2][i], y3 = R->hl.y[3][i];
        int32_t y4 = R->hl.y[4][i], y5 = R->hl.y[5][i], y6 = R->hl.y[6][i], y7 = R->hl.y[7][i], x = R->hl.bx[i];
        int32_t t, p0, p1, p2, p3, p4, p5, p6;
        R->hl.l[i] = y0 + y3 + y4 - y7;                    /* the outputs */
        R->hl.r[i] = y1 + y2 + y5 + y6;
        /* the matrix: C = [0 1; -1 Q], Q (7 x 7) = chi(j - i), the quadratic character mod 7 (+1 at distances
         * 1 2 4, -1 at 3 5 6): row 1 + i of Q y = 2 (the three at +1) - (the sum of the 7) + y_1+i */
        t = y1 + y2 + y3 + y4 + y5 + y6 + y7;
        p0 = y2 + y3 + y5, p1 = y3 + y4 + y6, p2 = y4 + y5 + y7, p3 = y5 + y6 + y1;
        p4 = y6 + y7 + y2, p5 = y7 + y1 + y3, p6 = y1 + y2 + y4;
        y0 += t;
        R->hl.y[0][i] = (int16_t)clamp(t + x, -32768, 32767);   /* (+ the send: signs spread over the rows) */
        R->hl.y[1][i] = (int16_t)clamp(2 * p0 + y1 - y0 + x, -32768, 32767);
        R->hl.y[2][i] = (int16_t)clamp(2 * p1 + y2 - y0 + x, -32768, 32767);
        R->hl.y[3][i] = (int16_t)clamp(2 * p2 + y3 - y0 + x, -32768, 32767);
        R->hl.y[4][i] = (int16_t)clamp(2 * p3 + y4 - y0 + x, -32768, 32767);
        R->hl.y[5][i] = (int16_t)clamp(2 * p4 + y5 - y0 + x, -32768, 32767);
        R->hl.y[6][i] = (int16_t)clamp(2 * p5 + y6 - y0 + x, -32768, 32767);
        R->hl.y[7][i] = (int16_t)clamp(2 * p6 + y7 - y0 - x, -32768, 32767);
    }
    b = rev_comb;
    for (k = 0; k < HL_NL; k++) {                       /* back into the lines (and the mirror after a wrap) */
        d = R->hl.i[k];
        HL_RUNS(d, HL_LEN[k], m, s, for (i = 0; i < m; i++) b[d + i] = R->hl.y[k][s + i]; if (!d) b[HL_LEN[k]] = b[0]);
        R->hl.i[k] = (uint16_t)d;
        b += HL_LEN[k] + 1u;
    }
    for (i = 0; i < HL_NC; i++) {                       /* back to 44.1 kHz, between samples */
        int32_t mm = (R->hl.l[i] + R->hl.r[i]) * HL_OUT, ss = (R->hl.l[i] - R->hl.r[i]) * HL_OUT;
        out[2u * i] += (mp + mm) >> 1;
        out[2u * i + 1u] += mm;
        rev_side[2u * i] = (sp + ss) >> 1;
        rev_side[2u * i + 1u] = ss;
        mp = mm;
        sp = ss;
    }
    R->hl.m = mp, R->hl.s = sp;
    R->hl.side = 1;
}

/* SHIMR (G_RTYPE 3, JIANT): ROOM with its output an octave up fed back into it (two crossfaded grains reading
 * its own line twice as fast). SIZE / DAMP as ROOM; shimmer amount is fixed (no MOD global here). */
/* CLOUD (G_RTYPE 4, JIANT): granular wash into ROOM. SIZE decay (127 freezes the line); DAMP darkens.
 * Grain density / pitch chance / width are fixed (no RATE / MOD / WIDE globals here). */
static __attribute__((noinline)) void rev_shimmer(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    int32_t in[CTL], t[CTL];
    uint32_t i, j;
    for (i = 0; i < n; i++) {
        in[i] = rev_in[i] + mulq15(shim_fb[i], SH_AMT);
        t[i] = 0;
    }
    rev_room(in, t, n);
    for (i = 0; i < n; i++) {
        int32_t x = 0;
        out[i] += t[i];
        shim_buf[R->sh_w & (SH_LEN - 1u)] = (int16_t)clamp(t[i] >> 1, -32768, 32767);
        for (j = 0; j < 2u; j++) {                      /* the delay falls a sample a sample: read at twice the speed */
            uint32_t ph = (R->sh_ph + j * (SH_LEN / 2u)) & (SH_LEN - 1u), d = SH_LEN - 1u - ph;
            int32_t w = (int32_t)(ph < SH_LEN / 2u ? ph : SH_LEN - 1u - ph);   /* the triangles: their sum flat */
            x += (shim_buf[(R->sh_w - d) & (SH_LEN - 1u)] * w) >> 9;
        }
        R->sh_ph++;
        R->sh_w++;
        shim_fb[i] = x;                                 /* (into the next block's input) */
    }
}

static uint32_t cl_rand(void) { R->cl_rng = R->cl_rng * 1664525u + 1013904223u; return R->cl_rng >> 8; }
static __attribute__((noinline)) void rev_cloud(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    static const uint32_t PITCH[4] = {65536u, 131072u, 32768u, 98184u};   /* x1, octave up, down, fifth up (Q16) */
    uint32_t i, k, freeze = rev_pg_size() >= 127;
    int32_t dk = 32767 - rev_pg_damp() * 220;
    int32_t sz = rev_pg_size(), fb = sz * 24, fg = sz * 110, g[CTL], t[CTL];
    for (i = 0; i < n; i++) {
        int32_t l = 0, r = 0, m;
        if (!freeze) {                                   /* the line: the input and the tail, two samples into one */
            int32_t x = (rev_in[i] >> 1) + mulq15(shim_fb[i], fb) + mulq15(R->cl_lp, fg);
            if (R->cl_w & 1u)
                cloud_buf[(R->cl_w >> 1) & (CL_LEN - 1u)] = (int16_t)clamp((R->cl_half + x) >> 1, -32768, 32767);
            else
                R->cl_half = x;
            R->cl_w++;
        }
        if (--R->cl_next <= 0) {                         /* a grain is born, in the first one free */
            R->cl_next = CL_GAP / 2 + (int32_t)(cl_rand() % (uint32_t)CL_GAP);
            for (k = 0; k < CL_N && cl_g[k].len; k++) {}
            if (k < CL_N) {
                uint32_t pk = cl_rand() % 100u < (uint32_t)CL_MOD * 3u / 4u ? 1u + cl_rand() % 3u : 0u;
                cl_g[k].len = (uint16_t)(2000u + cl_rand() % 4200u);
                cl_g[k].pos = ((R->cl_w >> 1) - 200u - cl_rand() % (CL_LEN - 6000u)) << 16;
                cl_g[k].inc = (PITCH[pk] >> 1) + (cl_rand() % 129u) - 64u;
                cl_g[k].age = 0;
                cl_g[k].pl = 128;                        /* centre (no WIDE) */
                cl_g[k].pr = 128;
            }
        }
        for (k = 0; k < CL_N; k++) {
            if (!cl_g[k].len) continue;
            {
                uint32_t p = cl_g[k].pos >> 16, f = (cl_g[k].pos >> 8) & 255u, a = cl_g[k].age, L = cl_g[k].len;
                int32_t x0 = cloud_buf[p & (CL_LEN - 1u)], x1 = cloud_buf[(p + 1u) & (CL_LEN - 1u)];
                int32_t x = x0 + (((x1 - x0) * (int32_t)f) >> 8), w = (int32_t)((a < L - a ? a : L - a) * 2u * 256u / L);
                x = (x * w) >> 8;
                l += (x * cl_g[k].pl) >> 7;
                r += (x * cl_g[k].pr) >> 7;
                cl_g[k].pos += cl_g[k].inc;
                if (++cl_g[k].age >= cl_g[k].len) cl_g[k].len = 0;
            }
        }
        m = (l + r) >> 1;
        R->cl_lp = m + mulq15(R->cl_lp - m, 32767 - dk);
        g[i] = R->cl_lp * 2;
        t[i] = 0;
    }
    rev_room(g, t, n);
    for (i = 0; i < n; i++) {
        out[i] += t[i] + (t[i] >> 1) + (g[i] >> 2);
        shim_fb[i] = t[i];
    }
}

/* the model ty's block into out */
static void rev_run(uint32_t ty, const int32_t *rev_in, int32_t *out, uint32_t n)
{
    switch (ty) {
    case 1: rev_spring(rev_in, out, n); break;
    case 2: rev_hall(rev_in, out, n); break;
    case 3: rev_shimmer(rev_in, out, n); break;
    case 4: rev_cloud(rev_in, out, n); break;
    default: rev_room(rev_in, out, n); break;
    }
}

/* the active tank's buffers and states to silence (the model changed). Indices and
 * SPRING's glide (sp_w / sp_size / sp_ph) stay: ROOM <-> SPRING stays bit-identical. */
static void rev_clear(void)
{
    uint32_t i;
    for (i = 0; i < sizeof R->comb / 2u; i++)
        R->comb[i] = 0;
    for (i = 0; i < sizeof R->u.ap / 2u; i++)          /* (int16: 997 of them, an odd count the int32 view misses one of) */
        R->u.ap[i] = 0;
    for (i = 0; i < 4u; i++)
        R->comb_lp[i] = 0;
    for (i = 0; i < HL_NL; i++)
        R->hl.lp[i] = R->hl.le[i] = 0;
    R->sp_lp = R->sp_hp = R->sp_he = 0;
    R->hl.x = R->hl.m = R->hl.s = R->hl.hp = R->hl.he = 0;
    memset(&R->gran, 0, sizeof R->gran);
    for (i = 0; i < CTL; i++)
        shim_fb[i] = 0;
    for (i = 0; i < CL_N; i++)
        cl_g[i].len = 0;
    R->sh_w = R->sh_ph = 0;
    R->cl_w = R->cl_rng = 0;
    R->cl_next = R->cl_lp = R->cl_half = 0;
}
/* both tanks silent; R left on tank 0 */
static void rev_clear_all(void)
{
    rev_tank_t *save = R;
    R = &rev_tank[0];
    rev_clear();
    memset(&R->hl, 0, sizeof R->hl);
    R->comb_i[0] = R->comb_i[1] = R->comb_i[2] = R->comb_i[3] = 0;
    R->ap_i[0] = R->ap_i[1] = 0;
    R->sp_w = 0;
    R->sp_size = R->sp_ph = 0;
    R = &rev_tank[1];
    rev_clear();
    memset(&R->hl, 0, sizeof R->hl);
    R->comb_i[0] = R->comb_i[1] = R->comb_i[2] = R->comb_i[3] = 0;
    R->ap_i[0] = R->ap_i[1] = 0;
    R->sp_w = 0;
    R->sp_size = R->sp_ph = 0;
    R = save ? save : &rev_tank[0];
}

/* the dry mix and the sends, as each core adds its parts into them (mx[1] is the second core's; mix_block merges
 * the two: the sums are integers, so the order they add up in changes nothing: the output is bit for bit one
 * core's) and a part's own block (b) */
typedef struct { int32_t l[CTL], r[CTL], d[CTL], rv[CTL], b[CTL]; } mixsum_t;
static mixsum_t mx[2];
static uint16_t part_cost[NTRK];                       /* what a part's render cost the last block (mix_split) */
#define mix_l (mx[0].l)
#define mix_r (mx[0].r)
#define send_d (mx[0].d)
#define send_r (mx[0].rv)
#define part_buf (mx[0].b)
#define SM_DECL , mixsum_t *sm                          /* mix_part / mix_spread: the sums its parts add into */
#define SM_ARG(x) , (x)
#define SM(f) (sm->f)

/* the model changed (to rt): the old one's block fades out (HALL's stereo difference with it), its buffers are
 * cleared, the new one starts from silence. Its own scratch: the mixer's per-part buffer (part_buf) may be
 * the caller's mix (track_insert) */
static __attribute__((noinline)) void rev_switch(const int32_t *rev_in, int32_t *wet, uint32_t n, int32_t rt)
{
    static int32_t rev_fade[CTL];
    int32_t *t = rev_fade, g = 65536, d = 65536 / (int32_t)n;
    uint32_t i;
    for (i = 0; i < n; i++)
        t[i] = 0;
    rev_run(R->rtype, rev_in, t, n);
    for (i = 0; i < n; i++, g -= d) {
        wet[i] += mulq16(t[i], (uint32_t)g);
        rev_side[i] = mulq16(rev_side[i], (uint32_t)g);   /* (HALL only: R->hl.side set) */
    }
    rev_clear();
    R->rtype = (uint8_t)rt;
}

/* Shared delay line: write `in`, read into `out` (may alias). Used as SEND bus or insert CHAIN. */
static void dly_process(const int32_t *in, int32_t *out, uint32_t n, int32_t wet_q15)
{
    uint32_t i, dl = delay_samples();
    int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240;
    int free_ms = dtime_free();
    uint32_t tgt = 0;
    if (!free_ms) {
        dly_q8 = 0;
        dly_free = 0;
    } else {
        tgt = dtime_q8();
        if (!dly_free) {
            dly_q8 = tgt;
            dly_free = 1;
        }
    }
    if (pf.ew1 && fb < THR_FB)
        fb += mulq16(THR_FB - fb, (uint32_t)pf.ew1 << 1);
    for (i = 0; i < n; i++) {
        int32_t x;
        if (free_ms) {
            int32_t err = (int32_t)tgt - (int32_t)dly_q8;
            int32_t st = err >> DLY_EASE;              /* 1/256 of what is left to go */
            if (st > (int32_t)DLY_SLEW)
                st = (int32_t)DLY_SLEW;                /* the tape pitch: 10.9 semitones up, 36 down */
            else if (st < -(int32_t)DLY_SLEW)
                st = -(int32_t)DLY_SLEW;
            else if (!st && err)
                st = err > 0 ? 1 : -1;                 /* the last stretch creeps: an exact landing, no stall */
            uint32_t ri;
            int32_t fr, c0, c1;
            dly_q8 = (uint32_t)((int32_t)dly_q8 + st);
            ri = dly_q8 >> 8;
            fr = (int32_t)(dly_q8 & 255u);
            c0 = dly_buf[(fx.dly_w - ri) & (DLY_LEN - 1u)];
            c1 = dly_buf[(fx.dly_w - ri - 1u) & (DLY_LEN - 1u)];
            x = c0 + (((c1 - c0) * fr) >> 8);
        } else {
            x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)];
        }
        fx.dly_lp += mulq15(x - fx.dly_lp, col);
        dly_buf[fx.dly_w & (DLY_LEN - 1u)] =
            (int16_t)clamp((in[i] >> 1) + mulq15(fx.dly_lp, fb), -32768, 32767);
        fx.dly_w++;
        out[i] = mulq15(x << 1, wet_q15);
    }
}
/* process delay + reverb buses; sends in, wet stereo-equal out.
 * Delay CHAIN: send path silent (owner advances the line in track_insert).
 * Reverb: always runs the SEND tank (tank 1 while CHAIN is owned, else tank 0). */
static void fx_buses(const int32_t *dly_in, const int32_t *rev_in, int32_t *wet, uint32_t n)
{
    uint32_t i;
    int32_t rt;
    int dly_send = bus_own(0) < 0, rev_chain = bus_own(1) >= 0;
    mod_bus_begin();
    if (dly_send)
        dly_process(dly_in, wet, n, 32768);
    else
        for (i = 0; i < n; i++)
            wet[i] = 0;                           /* CHAIN: owner advances the line in track_insert */
    R = rev_chain ? &rev_tank[1] : &rev_tank[0];
    rt = rev_rtype();
    R->hl.side = 0;                               /* (both tanks cleared at mix_block's start) */
    if (rt != R->rtype)
        rev_switch(rev_in, wet, n, rt);
    else
        rev_run((uint32_t)rt, rev_in, wet, n);
    mod_bus_end();
}

/* after the buses: HALL stereo difference from either tank (CHAIN insert and/or SEND) */
static __attribute__((noinline)) void rev_side_mix(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t t, i;
    for (t = 0; t < 2u; t++) {
        if (!rev_tank[t].hl.side)
            continue;
        for (i = 0; i < n; i++) {
            l[i] += rev_tank[t].hl.sd[i];
            r[i] -= rev_tank[t].hl.sd[i];
        }
    }
}

/* Insert chain: six stages per track (DIST, FILTER, CRUSH, PHASR, FLANG, CHOR), order from P_IORD
 * (factory IORD_DEF: DIST, CRUSH, FILTER, …). MIX 0 skips the stage (no DSP). Shared delay-line
 * pool (2 lines/track for FLANG+CHOR). State per stage in the pool; SLICER stays after the chain. */
#define IL_LEN 256u
#define IC_LEN 128u                     /* FILTER COMB delay (~2.9 ms: teeth down to ~345 Hz) */
_Static_assert((IC_LEN & (IC_LEN - 1u)) == 0u, "insert FILTER COMB line is a power of two (dsp.c comb_process)");
static const uint8_t INS_HOLD[16] = {64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1};
typedef struct {
    int32_t w;                          /* wet share Q15 */
    uint8_t cnt, line_i;                /* CRUSH hold count; which shared line (0/1) */
    uint16_t wp;
    uint32_t ph;
    int32_t hp, lp1, lp2, dc, hold, fb, ic1, ic2;
    int32_t px[PH_ST], py[PH_ST];
    int16_t comb[IC_LEN];
} ins_st_t;
static ins_st_t ins_st[NTRK][INS_N] __attribute__((section(".pool")));
/* SEND DSP reuses ins_st[bus_tr][st] / ins_lines[bus_tr] (first SEND track for that stage).
 * Max 4 instances/stage: CHAIN private + SEND shared coexist, never 4+1. Parts render at once on the two
 * cores (mix_block): the shared state runs its one block in the lowest SEND consumer's part (ins_bus_block
 * picks it; its own knobs and input, as the parts rendered one after another), every other consumer runs the
 * copy of the block's start here. ins_bus_join puts the run in place after the join. */
static ins_st_t ins_bus_blk[INS_N];                                  /* SEND state at mix-block start */
static uint8_t ins_bus_driver[INS_N];                                /* the part that runs it (0xFF: no consumer) */
static uint8_t ins_bus_parallel;                                    /* SEND line windows commit after the join only on two cores */
static uint8_t ins_bus_ran[C1_NP];                                   /* bit per stage: this part ran the SEND DSP */
static int32_t ins_send_in[C1_NP][CTL];                              /* .. its input, when the shared run is redone */
static int16_t ins_win[C1_NP][2][CTL];                               /* each part's window of the line it writes */
static int32_t ins_rb[CTL];                                          /* ins_bus_join's run (its output stands) */
static int16_t ins_lines[NTRK][2][IL_LEN] __attribute__((section(".pool")));
/* SEND bus knobs live in fx_bus_trk.p (core.h); packed into reclaimed project bytes. MIX stays per-track. */
static uint32_t ins_bus_tr(uint32_t st)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (ins_is_send(&trk[k], st))
            return k;
    return 0;
}
static void fx_bus_init(void)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        fx_bus_trk.p[i] = TP[i].def;
}
static void fx_bus_from_wire(const uint8_t w[FX_BUS_N])
{
    uint32_t i;
    fx_bus_init();
    for (i = 0; i < FX_BUS_N; i++)
        fx_bus_trk.p[FX_BUS_PID[i]] = (int16_t)w[i];
}
static void fx_bus_to_wire(uint8_t w[FX_BUS_N])
{
    uint32_t i;
    for (i = 0; i < FX_BUS_N; i++) {
        int32_t v = fx_bus_trk.p[FX_BUS_PID[i]];
        w[i] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
    }
}
static void fx_bus_wire_def(uint8_t w[FX_BUS_N])
{
    fx_bus_init();
    fx_bus_to_wire(w);
}
static const track_t *ins_param_src(const track_t *t, uint32_t st)
{
    return st < INS_N && ins_is_send(t, st) ? bus_knobs_track(t) : t;
}

/* Power-on (audio.c audio_init): clear delay/reverb lines so the first blocks are silent. The pool is
 * zeroed in fm1_cstart; clear again here and match R->rtype to G_RTYPE. boot_fade stays at 0 until
 * audio_amp_on (main.c) starts a ~20 ms ramp (audio.c audio_block). */
static uint16_t boot_fade = 32768;           /* Q15 out gain; 32768 = full (host tests that skip audio_init) */
static uint8_t boot_fade_run;                /* 1: ramp boot_fade up (after the amp is on) */
#define BOOT_FADE_STEP 37                    /* ~20 ms: 32768 / (0.02 * FS) */
static void fx_boot(void)
{
    uint32_t i;
    memset(dly_buf, 0, sizeof dly_buf);
    rev_clear_all();
    bus_own_sanitize();
    memset(&fx, 0, sizeof fx);
    dly_q8 = 0;
    dly_free = 0;
    memset(ins_st, 0, sizeof ins_st);
    memset(ins_lines, 0, sizeof ins_lines);
    memset(ins_bus_ran, 0, sizeof ins_bus_ran);
    fx_bus_init();
    for (i = 0; i < NTRK; i++)
        trk[i].ins_run = 0;
    dc_l = dc_r = dce_l = dce_r = 0;            /* master_out: a prior spike must not settle audibly at unmute */
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = 0;
    memset(lce, 0, sizeof lce);
    lim_env = LIM_T;
    sb_lp1 = sb_lp2 = sb_lp3 = sb_lp4 = sb_env = sb_h1 = sb_h2 = sb_hl = 0;
    R = &rev_tank[1];
    rev_tank[1].rtype = (uint8_t)rev_rtype();           /* SEND tank */
    R = &rev_tank[0];
    rev_tank[0].rtype = (uint8_t)rev_rtype();           /* CHAIN if owned, else SEND */
    boot_fade = 0;
    boot_fade_run = 0;
}

static inline int32_t ins_mixq(int32_t m) { return m >= 127 ? 32768 : m <= 0 ? 0 : m * 258; }   /* MIX -> Q15 */
/* the one-pole low-pass (topology-preserving) coefficient of cutoff index c, Q16: G = g / (1 + g), g = tan(pi fc / FS) */
static int32_t ins_lpk(int32_t c)
{
    uint32_t g = SVF_G[clamp(c, 0, 127)];
    return (int32_t)((g << 16) / (4096u + g));
}
/* tanh(x / 24000) * 24000 through the shared table: a soft plateau at the level HARD clips at, and
 * unity slope at zero (89478 Q16 = 32768 / 24000, taking x to the table's own units and back) */
static inline __attribute__((always_inline)) int32_t ins_sat(int32_t x)
{
    return (softclip(mulq16(x, 89478u)) * 24000) >> 15;
}
/* The shapers, one curve per MODE (SOFT .. FUZZ; the default is CRUSH's quantizer). The drive gain
 * (ins_run_dist, per mode) keeps each in its own regime at DRIVE 127: they used to all square off
 * and be indistinguishable maxed out. SOFT a rounded curve that never flat-tops, HARD a hard edge at
 * 24000, FOLD a wavefolder that folds further in as it is driven, FUZZ a gated octave wave (it
 * repeats twice a period, so unlike any square or fold whatever the drive). g is the mode's drive
 * gain (Q12): FUZZ gates against the level it is driven to. All peak near 24000 so a MODE switch
 * stays even. */
static inline __attribute__((always_inline)) int32_t ins_shape(uint32_t mode, int32_t v, uint32_t sh, uint32_t g)
{
    switch (mode) {
    case IDM_SOFT: return ins_sat(v);
    case IDM_HARD: return clamp(v, -24000, 24000);
    case IDM_FOLD: {
        int32_t p = (int32_t)((uint32_t)(v + 65536) & 0x1FFFFu) - 65536;
        int32_t u = p < -32768 ? -65536 - p : p < 32768 ? p : 65536 - p;
        return (u * 3) >> 2;
    }
    case IDM_FUZZ: {
        int32_t m = v < 0 ? -v : v;
        return ins_sat((m - (int32_t)(4096u + (g << 1))) * 4);
    }
    default: return sh ? ((v + (1 << (sh - 1u))) >> sh) << sh : v;
    }
}
static inline uint32_t ins_bits_sh(int32_t a) { return 15u - (uint32_t)(clamp(a, 0, 127) >> 3); }

/* per MODE: the drive gain (Q12, 4096 = 1x) and how far the drive-linked cutoff drops (a >> DK of
 * the drive). Each MODE keeps its own regime at DRIVE 127 instead of all squaring off together. */
static const uint8_t INS_GSH[IDM_N] = {4, 0, 1, 3};       /* g = 4096 + (a * a * 3) >> GSH: 1.7 .. 12.8 x */
static const uint8_t INS_DK[IDM_N] = {2, 4, 5, 4};        /* cutoff index 96 - (a >> DK) + TONE: SOFT darkest */

static __attribute__((noinline)) void ins_run_dist(ins_st_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, mode = (uint32_t)clamp(t->p[P_IDMODE], 0, IDM_N - 1);
    int32_t a = t->p[P_DIST], g = 4096 + ((a * a * 3) >> INS_GSH[mode]);
    /* cutoff follows drive (96 at 0, darker as drive rises); P_ED_FX (TONE) offsets it, 0 = as before */
    int32_t k = ins_lpk(clamp((96 - (a >> INS_DK[mode])) + t->p[P_ED_FX], 0, 127));
    for (i = 0; i < n; i++) {
        int32_t x = b[i], y, v;
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        if (!s->w && !tw) { b[i] = x; continue; }
        s->hp += ((x << 8) - s->hp) >> 8;
        v = clamp(x - (s->hp >> 8), -230000, 230000);
        y = ins_shape(mode, ((v >> 3) * g) >> 9, 0, (uint32_t)g);
        y <<= 4;
        v = mulq16(y - s->lp1, (uint32_t)k);
        y = v + s->lp1; s->lp1 = y + v;
        v = mulq16(y - s->lp2, (uint32_t)k);
        y = v + s->lp2; s->lp2 = y + v;
        y = (y + 8) >> 4;
        s->dc += ((y << 8) - s->dc) >> 9;
        y -= s->dc >> 8;
        b[i] = pf_mix(x, y, s->w);
    }
}

static __attribute__((noinline)) void ins_run_filter(ins_st_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, ft = (uint32_t)clamp(t->p[P_IFTYPE], 0, IFT_N - 1);
    int32_t cut = t->p[P_IFCUT], res = t->p[P_IFRES], k = 8192 - res * 7600 / 127;
    tsvf_t flt;
    int32_t clen = ft == IFT_COMB ? (int32_t)comb_delay_len(cut, IC_LEN) : 0;   /* block-constant */
    /* tsvf_coef takes cutoff in Q8 (0..127 << 8), as ANALOG / TRIO / NOISE do */
    tsvf_coef(&flt, cut << 8, res);
    for (i = 0; i < n; i++) {
        int32_t x = b[i], y;
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        if (!s->w && !tw) { b[i] = x; continue; }
        if (ft == IFT_COMB) {
            /* CUT = delay across the line (0 longest); RES = feedback; out = x + g * delayed */
            y = comb_process(s->comb, IC_LEN, &s->wp, x, clen, res);
        } else {
            int32_t v3 = (x >> 1) - s->ic2;
            int32_t v1 = (flt.a1 * s->ic1 + flt.a2 * v3) >> 13;
            int32_t v2 = s->ic2 + ((flt.a2 * s->ic1 + flt.a3 * v3) >> 13);
            s->ic1 = clamp(2 * v1 - s->ic1, -150000, 150000);
            s->ic2 = clamp(2 * v2 - s->ic2, -150000, 150000);
            y = !ft ? v2 : ft == IFT_BP ? v1 : (x >> 1) - ((k * v1) >> 12) - v2;
            y <<= 1;
        }
        b[i] = pf_mix(x, y, s->w);
    }
}

static __attribute__((noinline)) void ins_run_crush(ins_st_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, sh = ins_bits_sh(t->p[P_ICBITS]), hold = INS_HOLD[clamp(t->p[P_ICRATE], 0, 127) >> 3];
    int32_t k = ins_lpk(t->p[P_ICLPF]);
    for (i = 0; i < n; i++) {
        int32_t x = b[i], y, v;
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        if (!s->w && !tw) { b[i] = x; continue; }
        if (!s->cnt) {
            s->hold = ins_shape(4u, clamp(x, -262144, 262143), sh, 4096u); /* crush quantize */
            s->cnt = (uint8_t)hold;
        }
        s->cnt--;
        y = s->hold << 4;
        v = mulq16(y - s->lp1, (uint32_t)k);
        y = v + s->lp1; s->lp1 = y + v;
        v = mulq16(y - s->lp2, (uint32_t)k);
        y = v + s->lp2; s->lp2 = y + v;
        b[i] = pf_mix(x, (y + 8) >> 4, s->w);
    }
}

static __attribute__((noinline)) void ins_run_swept(ins_st_t *s, const track_t *t, int32_t *b, uint32_t n,
                                                    int32_t tw, uint32_t kind, int16_t *line, int16_t *win)
{
    uint32_t i;
    int32_t rate, dep, fbk_p;
    if (kind == INS_PHASR) {
        rate = t->p[P_IPRATE]; dep = t->p[P_IPDEP]; fbk_p = t->p[P_IPFBK];
    } else if (kind == INS_FLANG) {
        rate = t->p[P_ILRATE]; dep = t->p[P_ILDEP]; fbk_p = 96;
    } else {
        rate = t->p[P_IHRATE]; dep = t->p[P_IHDEP]; fbk_p = 64;
    }
    {
        uint32_t inc = LFO_INC[clamp(rate, 0, 127)] / CTL, depu = (uint32_t)clamp(dep, 0, 127);
        if (kind == INS_PHASR) {
            uint32_t tr = pf_tri(s->ph + (n >> 1) * inc), q = 32768u + (uint32_t)(((int32_t)tr - 32768) * (int32_t)depu / 127);
            int32_t a = PH_A[q >> 11] + (((PH_A[(q >> 11) + 1u] - PH_A[q >> 11]) * (int32_t)(q & 2047u)) >> 11);
            int32_t fbk = clamp(fbk_p, 0, 127) * 180;
            for (i = 0; i < n; i++) {
                int32_t x = b[i], xs = clamp((x + 4) >> 3, -16383, 16383), u = xs + ((s->fb * fbk + 16384) >> 15), y = u;
                uint32_t st;
                s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
                for (st = 0; st < PH_ST; st++) {
                    y = clamp(s->px[st] + ((a * (u - s->py[st]) + 8192) >> 14), -65535, 65535);
                    s->px[st] = u; s->py[st] = y; u = y;
                }
                s->fb = clamp(y, -32767, 32767);
                y <<= 2;
                s->dc += ((y << 8) - s->dc) >> 9;
                b[i] = pf_mix(x, (x >> 1) + y - (s->dc >> 8), s->w);
            }
            s->ph += n * inc;
            return;
        }
        {
            int32_t chorus = kind == INS_CHOR, fbk = clamp(fbk_p, 0, 127) * (chorus ? 103 : 219);
            uint32_t dq = depu * 516u, wbase = s->wp;   /* win[i] is this run's write at wbase + i */
            for (i = 0; i < n; i++, s->ph += inc) {
                int32_t x = b[i], r0, r1, v, fr;
                uint32_t d;
                s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
                if (chorus)
                    d = (uint32_t)((140 << 16) + ((osc_sine(s->ph) * (int32_t)depu * 50) >> 5));
                else {
                    uint32_t tri = pf_tri(s->ph), sw = (tri * tri) >> 16;
                    d = (11u << 16) + 209u * (((sw >> 1) * dq) >> 15);
                }
                r0 = (int32_t)(s->wp - (d >> 16)) & (int32_t)(IL_LEN - 1u);
                r1 = (r0 - 1) & (int32_t)(IL_LEN - 1u);
                fr = (int32_t)((d >> 1) & 0x7FFFu);
                /* the window holds this run's writes (the runs of two parts never share one: fx.c
                 * ins_stage); a read back into it takes its own write, behind it the shared line. One
                 * CHOR depth past 107 wraps its delay into the window's tail early in a block, where
                 * one-after-another renders left the part before's writes: those samples of the later
                 * parts need the preceding consumer's writes, so deep shared CHORUS stays on one core
                 * and commits each window before the next consumer (FLANG never wraps there) */
                {
                    int32_t o0 = (r0 - (int32_t)wbase) & (int32_t)(IL_LEN - 1u);
                    int32_t o1 = (r1 - (int32_t)wbase) & (int32_t)(IL_LEN - 1u);
                    int32_t a0 = o0 < (int32_t)i ? win[o0] : line[r0];
                    int32_t a1 = o1 < (int32_t)i ? win[o1] : line[r1];
                    v = a0 + (((a1 - a0) * fr) >> 15);
                }
                win[i] = (int16_t)clamp((x >> 2) + softclip(mulq15(v, fbk)), -32767, 32767);
                s->wp = (uint16_t)((s->wp + 1u) & (IL_LEN - 1u));
                v <<= 1;
                s->dc += ((v << 8) - s->dc) >> 9;
                b[i] = pf_mix(x, (x >> 1) + v - (s->dc >> 8), s->w);
            }
        }
    }
}

static __attribute__((noinline)) void ins_rest(track_t *t)
{
    uint32_t k = (uint32_t)(t - trk) % NTRK, s, clear_lines = 1;
    for (s = 0; s < INS_N; s++) {
        /* Shared SEND state lives in the first SEND track's slot; keep it while
         * another SEND track still has MIX > 0 on this stage. */
        if (ins_is_send(t, s) && ins_bus_tr(s) == k) {
            uint32_t j, keep = 0;
            for (j = 0; j < NTRK; j++)
                if (j != k && ins_is_send(&trk[j], s) && ins_mix_of(&trk[j], s) > 0)
                    keep = 1;
            if (keep) {
                clear_lines = 0;                     /* FLANG/CHOR lines may be shared too */
                continue;
            }
        }
        memset(&ins_st[k][s], 0, sizeof(ins_st_t));
    }
    if (clear_lines)
        memset(ins_lines[k], 0, sizeof ins_lines[k]);
    t->ins_run = 0;
}

static void ins_bus_block(void)                      /* mix_block: new block for shared SEND DSP */
{
    uint32_t k, st;
    for (k = 0; k < C1_NP; k++)
        ins_bus_ran[k] = 0;
    for (st = 0; st < INS_N; st++) {
        ins_bus_blk[st] = ins_st[ins_bus_tr(st)][st];   /* before any consumer: the copies' start */
        ins_bus_driver[st] = 0xFF;
        for (k = 0; k < NTRK; k++)                     /* the lowest SEND consumer runs the shared state */
            if (ins_is_send(&trk[k], st) && ins_mix_of(&trk[k], st) != 0) {
                ins_bus_driver[st] = (uint8_t)k;
                break;
            }
    }
}

/* the window of a swept run into its line at wbase..wbase + n: what the run left there */
static __attribute__((noinline)) void ins_win_put(int16_t *line, const int16_t *win, uint32_t wbase, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        line[(wbase + i) & (IL_LEN - 1u)] = win[i];
}

/* one stage's DSP on run, cold-started from silence when it faded out completely. Returns where the swept
 * stages wrote their window (run->wp at the run's start). */
static uint32_t ins_run_st(ins_st_t *run, const track_t *src, int32_t *b, uint32_t n, int32_t tw, uint32_t st,
                           int16_t (*lines)[IL_LEN], uint32_t pi)
{
    uint32_t wbase;
    if (!run->w && tw) {
        memset(run, 0, sizeof *run);
        if (st == INS_FLANG) run->line_i = 0;
        if (st == INS_CHOR) run->line_i = 1;
    }
    wbase = run->wp;
    if (st == INS_DIST) ins_run_dist(run, src, b, n, tw);
    else if (st == INS_FILTER) ins_run_filter(run, src, b, n, tw);
    else if (st == INS_CRUSH) ins_run_crush(run, src, b, n, tw);
    else ins_run_swept(run, src, b, n, tw, st, lines[run->line_i & 1u], ins_win[pi][run->line_i & 1u]);
    return wbase;
}

/* tw_force < 0: the stage's own mix. Else that Q15 wet (a send runs fully wet; the amount
 * scales the add in track_insert). SEND: shared DSP in first SEND track's slot + fx_bus knobs;
 * CHAIN: private ins_st[tr][st]. */
static __attribute__((noinline)) void ins_stage(track_t *t, uint32_t st, int32_t *b, uint32_t n, int32_t tw_force)
{
    uint32_t k = (uint32_t)(t - trk) % NTRK, pi = C1_PI(t), wbase, i;
    int send = st < INS_N && ins_is_send(t, st);
    ins_st_t *s, tmp, *run;
    const track_t *src = ins_param_src(t, st);
    int32_t tw = tw_force >= 0 ? tw_force : ins_mixq(ins_mix_of(t, st));
    int16_t (*lines)[IL_LEN];
    if (send) {
        uint32_t bus_tr = ins_bus_tr(st);
        s = &ins_st[bus_tr][st];
        lines = ins_lines[bus_tr];
        if (!ins_bus_parallel && ins_bus_driver[st] == 0xFF) {
            ins_bus_blk[st] = *s;
            ins_bus_driver[st] = (uint8_t)k;
        }
        ins_bus_ran[pi] |= (uint8_t)(1u << st);
        if (k == ins_bus_driver[st]) {
            run = s;                                   /* the block's one run of the shared state */
        } else {
            tmp = ins_bus_blk[st];                     /* the block's start: every other part runs its copy */
            run = &tmp;
            for (i = 0; i < n; i++)                    /* ins_bus_join redoes the run from this input */
                ins_send_in[pi][i] = b[i];
        }
    } else {
        s = &ins_st[k][st];
        run = s;
        lines = ins_lines[k];
    }
    if (!run->w && !tw)
        return;                                         /* MIX 0 and faded: skip DSP */
    wbase = ins_run_st(run, src, b, n, tw, st, lines, pi);
    if ((!send || !ins_bus_parallel) && (st == INS_FLANG || st == INS_CHOR))
        /* one core commits before the next consumer, including deep shared CHORUS */
        ins_win_put(lines[run->line_i & 1u], ins_win[pi][run->line_i & 1u], wbase, n);
    if (run->w)
        t->ins_run = 1;
}

/* after the parts (mix_block): the shared state's one run of the block, when the picked driver did not make
 * it (that part went silent this block, or the matrix brought a lower one in): the lowest part that ran the
 * stage, its own input and knobs, from the block's start. Then the swept stages' line: the last consumer's
 * writes are what a block of one-after-another renders leaves in the shared line. */
static __attribute__((noinline)) void ins_bus_join(uint32_t n)
{
    uint32_t st, k;
    for (st = 0; st < INS_N; st++) {
        uint32_t lo = NPART, hi = NPART, bus_tr = ins_bus_tr(st);
        ins_st_t tmp;
        for (k = 0; k < NPART; k++)
            if (ins_bus_ran[k] >> st & 1u) {
                if (lo == NPART)
                    lo = k;
                hi = k;
            }
        if (lo == NPART)
            continue;                                    /* no consumer this block: the state stands */
        if ((uint8_t)lo != ins_bus_driver[st]) {
            tmp = ins_bus_blk[st];
            for (k = 0; k < n; k++)                    /* the runs are in place: a fresh copy per stage */
                ins_rb[k] = ins_send_in[lo][k];
            (void)ins_run_st(&tmp, bus_knobs_track(&trk[lo]), ins_rb, n, 32768, st,
                             ins_lines[bus_tr], lo);
            ins_st[bus_tr][st] = tmp;
        }
        if (st == INS_FLANG || st == INS_CHOR) {
            uint32_t wbase = ins_bus_blk[st].w ? ins_bus_blk[st].wp : 0u;
            uint32_t li = ins_bus_blk[st].w ? (ins_bus_blk[st].line_i & 1u) : (st == INS_CHOR);
            ins_win_put(ins_lines[bus_tr][li], ins_win[hi][li], wbase, n);
        }
    }
}

static int32_t ins_dry_buf[C1_NP][CTL], ins_wet_buf[C1_NP][CTL];   /* one part's send scratch (its own core) */
/* The DLY / REV bus settings a part's render may modulate (core.h bus_knobs): its copy goes into song.g for
 * its CHAIN run and comes out after (the DLY and REV owners write disjoint settings, never one slot at once) */
static __attribute__((noinline)) void bus_g_put(const track_t *t, uint32_t first, uint32_t last, int32_t *keep,
                                               int restore)
{
    uint32_t j;
    for (j = first; j < last; j++) {
        int16_t *p = fx_target_ptr(t, MFX_TIME + j);
        if (restore)
            *p = (int16_t)keep[j];
        else {
            keep[j] = *p;
            *p = bus_knobs[C1_PI(t)].g[j];
        }
    }
}
/* Run shared DLY/REV as an insert stage (CHAIN owner only): wet mixed by the track's send amount.
 * noinline: the CHAIN owner's path stays out of track_insert's loop (target_budget). */
static __attribute__((noinline)) void fx_bus_stage(track_t *t, uint32_t st, int32_t *b, uint32_t n)
{
    uint32_t k, pi = C1_PI(t), g0 = st == INS_DLY ? 0u : 3u, g1 = st == INS_DLY ? 3u : 5u;
    int32_t *ins_dry = ins_dry_buf[pi], *ins_wet = ins_wet_buf[pi];
    int32_t amt = ins_mix_of(t, st), g, rt, mfx_keep[5];
    if (bus_knobs_on[pi])
        bus_g_put(t, g0, g1, mfx_keep, 0);
    if (amt <= 0 && st == INS_DLY) {
        dly_process(b, ins_wet, n, 0);           /* advance the line even at mix 0 */
        if (bus_knobs_on[pi])
            bus_g_put(t, g0, g1, mfx_keep, 1);
        return;
    }
    if (amt <= 0) {
        if (bus_knobs_on[pi])
            bus_g_put(t, g0, g1, mfx_keep, 1);
        return;
    }
    g = ins_mixq(amt);
    for (k = 0; k < n; k++)
        ins_dry[k] = b[k];
    if (st == INS_DLY) {
        dly_process(ins_dry, ins_wet, n, 32768);
    } else {
        R = &rev_tank[0];                         /* CHAIN insert always tank 0 */
        rt = rev_rtype();
        R->hl.side = 0;
        for (k = 0; k < n; k++)
            ins_wet[k] = 0;
        if (rt != R->rtype)
            rev_switch(ins_dry, ins_wet, n, rt);
        else
            rev_run((uint32_t)rt, ins_dry, ins_wet, n);
    }
    for (k = 0; k < n; k++) {
        int64_t s = (int64_t)ins_dry[k] + ((((int64_t)ins_wet[k] - ins_dry[k]) * g) >> 15);
        b[k] = s > 2147483647 ? 2147483647 : s < -2147483647 - 1 ? (int32_t)(-2147483647 - 1) : (int32_t)s;
    }
    t->ins_run = 1;
    if (bus_knobs_on[pi])
        bus_g_put(t, g0, g1, mfx_keep, 1);
}
/* Chain stages run in list order. Insert SEND stages are left out of the series and added after.
 * Shared DLY/REV/SLCR run here only when this track owns them as CHAIN. */
static __attribute__((noinline)) void track_insert(track_t *t, int32_t *b, uint32_t n)
{
    uint8_t ord[FX_LIST_N];
    uint32_t i;
    uint16_t raw;
    int32_t tr = (int32_t)(t - trk);
    int32_t *ins_dry = ins_dry_buf[C1_PI(t)], *ins_wet = ins_wet_buf[C1_PI(t)];   /* this part's scratch */
    t->ins_run = 0;
    fx_list_get(t, ord);
    raw = iord_bits(t);
    for (i = 0; i < FX_LIST_N; i++) {
        uint32_t st = ord[i];
        if (st == INS_DLY || st == INS_REV) {
            if (bus_own(st == INS_REV ? 1u : 0u) == tr)
                fx_bus_stage(t, st, b, n);
            continue;
        }
        if (st == INS_SLCR) {
            if (sl_trk_chain(t)) {
                /* CHAIN at list position: gate mono b; with SPREAD also gate side d
                 * (spread_buf already holds left-right from mix_spread). SEND still
                 * runs after the chain via slicer_track_sd. */
                if (t->spread_on)
                    slicer_track_sd(t, b, spread_buf[C1_PI(t)], n);
                else
                    slicer_track(t, b, n);
            }
            continue;
        }
        if (!(raw & (uint16_t)(1u << (10u + st))))
            ins_stage(t, st, b, n, -1);
    }
    if (!(raw & IORD_SEND) || !n || n > CTL)
        return;
    for (i = 0; i < n; i++)
        ins_dry[i] = b[i];
    for (i = 0; i < FX_LIST_N; i++) {
        uint32_t st = ord[i], k;
        int32_t amt, g;
        if (st >= INS_N || !(raw & (uint16_t)(1u << (10u + st))))
            continue;
        amt = ins_mix_of(t, st);
        if (amt <= 0)
            continue;
        for (k = 0; k < n; k++)
            ins_wet[k] = ins_dry[k];
        ins_stage(t, st, ins_wet, n, 32768);
        g = ins_mixq(amt);
        for (k = 0; k < n; k++) {
            int64_t s = (int64_t)b[k] + (((int64_t)ins_wet[k] * g) >> 15);
            b[k] = s > 2147483647 ? 2147483647 : s < -2147483647 - 1 ? (int32_t)(-2147483647 - 1) : (int32_t)s;
        }
    }
}

/* one block of the whole mix (shared with hostsim.c): events -> each part (with its modulation matrix)
 * -> dist -> INSERT -> SLICER -> level / pan / sends -> buses -> master; out: stereo Q15 */
static void events_block(uint32_t n);                    /* seq.c */
static int32_t wet[CTL];                                /* (the dry mix and the sends: mx[], above) */

/* (JIANT) CLIP: mix driven into soft clip ahead of the limiter (before MASTER); wet folded in */
static __attribute__((noinline)) void clip_block(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        mix_l[i] = ((softclip(((mix_l[i] + wet[i]) >> 4) * clip_g >> 10) * clip_mk) >> 15) << 2;
        mix_r[i] = ((softclip(((mix_r[i] + wet[i]) >> 4) * clip_g >> 10) * clip_mk) >> 15) << 2;
        wet[i] = 0;
    }
}
static __attribute__((noinline)) void master_begin(void)
{
    int32_t a = clip_eff;
    clip_g = a > 0 ? 4096 + a * 12288 / 100 : 0;
    if (clip_g)
        clip_mk = (int32_t)((6000u << 15) / (uint32_t)softclip((6000 * clip_g) >> 12));
}
/* DUCK: kick (eng_drum.c duck_hit) ducks non-DRUM parts */
static __attribute__((noinline)) void duck_block(void)
{
    int32_t dep = song.g[G_DUCK] * 31731 / 100, e;
    if (duck_hit) {
        duck_hit = 0;
        duck_att = 1;
        duck_hold = (uint8_t)(1103u / CTL);
    }
    duck_g0 = duck_g1;
    if (duck_att) {
        duck_env += (32767 - duck_env) >> 1;
        if (duck_env > 31000)
            duck_att = 0;
    } else if (duck_hold) {
        duck_hold--;
    } else if (duck_env) {
        duck_env = (int32_t)(((uint32_t)duck_env * dv_kb(40000u + (uint32_t)song.g[G_DREL] * 5600u)) >> 16);
    }
    e = (duck_env * (65536 - duck_env)) >> 15;
    duck_g1 = 32767 - mulq15(dep, e > 32767 ? 32767 : e);
}
static __attribute__((noinline)) int32_t duck_ramp(int32_t lvl, int32_t *dl)
{
    int32_t l0 = ((lvl * (duck_g0 >> 3)) >> 12) << 8;
    *dl = ((((lvl * (duck_g1 >> 3)) >> 12) << 8) - l0) >> CTL_LOG2;
    return l0;
}

/* SPREAD (#148): a POLY or UNISON part whose SPRD is above 0. Its voices start left and right of PAN in turn (voice.c
 * voice_start; UNISON's detuned voices alternate), at PAN -/+ SPRD / 2 (SPRD 127: hard left and right), with the pan
 * law PAN has (the far side down, the near one as it is). track_render gives every voice (b, as always) and the left
 * ones (spread_buf); here d = left - right, the side signal, so for a pair of pans L = b (gla + glb) / 2 + d (gla - glb)
 * / 2 (R alike): the mono sum is b's, the level of a centre pan less by SPRD / 256 at most. b goes through DIST, the
 * SLICER and the FX layer's mute key as always; d gets the SLICER's gate and the mute's ramp (slicer_track_sd,
 * perf_mute_sd) and passes DIST by (the spread of a distorted chord is its voices' clean difference). The sends
 * take b, mono, as before. SPRD 0, MONO or LEGATO: mix_part's own loop, bit for bit as before. The cost: a block's
 * clear and sum of spread_buf, the side and a wider mix loop, per part; per voice none (it picks its buffer) */
static __attribute__((noinline)) void mix_spread(track_t *t, int32_t *b, uint32_t n SM_DECL)
{
    int32_t *d = spread_buf[C1_PI(t)];
    uint32_t i;
    int32_t lvl = LEVEL_Q12[t->p[P_LEVEL] & 127], pan = t->p[P_PAN], s = (t->p[P_SPRD] + 1) >> 1;
    int32_t pa = clamp(pan - s, -64, 63), pb = clamp(pan + s, -64, 63);
    int32_t gla = 4096 - (pa > 0 ? pa * 64 : 0), gra = 4096 + (pa < 0 ? pa * 64 : 0);
    int32_t glb = 4096 - (pb > 0 ? pb * 64 : 0), grb = 4096 + (pb < 0 ? pb * 64 : 0);
    int32_t glm = (gla + glb) >> 1, grm = (gra + grb) >> 1, gld = (gla - glb) >> 1, grd = (gra - grb) >> 1;
    int32_t dl = bus_own(0) < 0 ? t->p[P_DLY] * 258 : 0;
    int32_t r = bus_own(1) == (int32_t)(t - trk) ? 0 : t->p[P_REV] * 258, pk = t->peak;
    int32_t xmax = dl > r ? dl : r, lq = lvl << 8, dlv = 0;
    xmax = 0x7FFFFFFF / (xmax | 1);
    if ((duck_g0 < 32767 || duck_g1 < 32767) && ENGINES[t->engine] != &ENG_DRUM)
        lq = duck_ramp(lvl, &dlv);
    for (i = 0; i < n; i++)
        d[i] = (d[i] << 1) - b[i];                      /* left - right */
    if (ins_any(t) | t->ins_run || bus_own(0) == (int32_t)(t - trk) || bus_own(1) == (int32_t)(t - trk) ||
        sl_trk_chain(t))
        track_insert(t, b, n);
    if (sl_trk_send(t))                                 /* SEND: shared bus after the chain */
        slicer_track_sd(t, b, d, n);
    /* CHAIN: already in track_insert; USED: skip */
    if ((pf.mute >> (t - trk)) & 1u)
        perf_mute_sd((uint32_t)(t - trk), b, d, n);
    for (i = 0; i < n; i++, lq += dlv) {
        int32_t x = ((b[i] >> 2) * (lq >> 8)) >> 10, a = x < 0 ? -x : x, y = ((d[i] >> 2) * (lq >> 8)) >> 10;
        int32_t xs = clamp(x, -xmax, xmax);
        if (a > pk)
            pk = a;
        if (dl)
            SM(d)[i] += mulq15(xs, dl);
        if (r)
            SM(rv)[i] += mulq15(xs, r);
        SM(l)[i] += ((x * glm) >> 12) + ((y * gld) >> 12);
        SM(r)[i] += ((x * grm) >> 12) + ((y * grd) >> 12);
    }
    t->peak = pk;
}

/* Keys are the effective values after mod_begin, so automation and matrix
 * modulation invalidate the same cache as direct edits and preset loads. */
typedef struct {
    int16_t level, pan, dly, rev, dlyown, revown;
    uint8_t valid;
    int32_t lvl, gl, gr, d, r, xmax;
} mix_cache_t;
static mix_cache_t mix_cache[NTRK];
static __attribute__((noinline)) void mix_cache_update(const track_t *t, mix_cache_t *mc)
{
    mc->level = t->p[P_LEVEL];
    mc->pan = t->p[P_PAN];
    mc->dly = t->p[P_DLY];
    mc->rev = t->p[P_REV];
    mc->dlyown = song.g[G_DLYOWN];
    mc->revown = song.g[G_REVOWN];
    mc->valid = 1;
    mc->lvl = LEVEL_Q12[mc->level & 127];
    mc->gl = 4096 - (mc->pan > 0 ? mc->pan * 64 : 0);
    mc->gr = 4096 + (mc->pan < 0 ? mc->pan * 64 : 0);
    mc->d = bus_own(0) < 0 ? mc->dly * 258 : 0;
    /* REV CHAIN owner mixes in insert; everyone else still SENDS (tank 1 when chained). */
    mc->r = bus_own(1) == (int32_t)(t - trk) ? 0 : mc->rev * 258;
    mc->xmax = 0x7FFFFFFF / ((mc->d > mc->r ? mc->d : mc->r) | 1);
}

/* one synth part into the dry mix and the sends; a part with no voice sounding costs
 * its control clocks only after insert and stutter tails have run out (Melodee).
 * CHAIN delay/reverb: keep feeding zeros so the bus rings after a short note (SEND already does).
 * need_zeros operand order matters: the audio ISR inlines this, and a different short-circuit order
 * reshapes the CFG into an extra nested loop that blows the target_budget cost. */
static void mix_part(track_t *t, uint32_t n SM_DECL)
{
    int32_t *b = SM(b);
    uint32_t i;
    int32_t tr = (int32_t)(t - trk);
    int chain = bus_own(0) == tr || bus_own(1) == tr || sl_trk_chain(t);
    int sl_send = sl_trk_send(t);
    mod_begin(t);                                       /* the matrix's per-block values into t->p (mod.c) */
    /* after mod: insert MIX may turn on and still need a cleared part_buf (shared across tracks) */
    int need_zeros = (t->tail && (ins_any(t) | t->ins_run)) || slicer_busy(t) || chain;
    t->spread_on = t->p[P_SPRD] > 0 && (trk_vmode(t) == V_POLY || trk_vmode(t) == V_UNISON);   /* (SPREAD, mix_spread) */
    uint32_t nr = track_render_audio(t, b, n, need_zeros);
    part_cost[(uint32_t)tr % NTRK] = (uint16_t)(nr ? 40u * trk_nvoice(t) : 0u) +   /* mix_split's estimate */
                                     (uint16_t)((nr || t->tail ? 60u : 0u) + 24u * (ins_any(t) != 0));
    if (nr)
        t->tail = ins_any(t) ? 64 : 16;                 /* blocks of insert state to run out after the last voice */
    else if ((!t->tail || !(ins_any(t) | t->ins_run) || !--t->tail) && !slicer_busy(t) && !chain) {
        if (sl_send)
            slicer_track(t, 0, n);
        if (t->ins_run && !ins_any(t))
            ins_rest(t);                                /* all MIX 0 while silent: clear state for the next note */
        if (MOD(t).on)
            mod_end(t);
        return;
    }
    if (t->spread_on)
        mix_spread(t, b, n SM_ARG(sm));
    else {
        uint32_t ti = (uint32_t)(t - trk);
        mix_cache_t *mc = &mix_cache[ti];
        if (!mc->valid || mc->level != t->p[P_LEVEL] || mc->pan != t->p[P_PAN] ||
            mc->dly != t->p[P_DLY] || mc->rev != t->p[P_REV] ||
            mc->dlyown != song.g[G_DLYOWN] || mc->revown != song.g[G_REVOWN])
            mix_cache_update(t, mc);
        int32_t lvl = mc->lvl, gl = mc->gl, gr = mc->gr, d = mc->d, r = mc->r, xmax = mc->xmax, pk = t->peak;
        if (ins_any(t) | t->ins_run || chain)
            track_insert(t, b, n);
        if (sl_send)
            slicer_track(t, b, n);
        if ((pf.mute >> (t - trk)) & 1u)
            perf_mute((uint32_t)(t - trk), b, n);       /* perform.c: a black key in the FX layer */
        int32_t lq = lvl << 8, dl = 0;
        if ((duck_g0 < 32767 || duck_g1 < 32767) && ENGINES[t->engine] != &ENG_DRUM)   /* DUCK: the kick's */
            lq = duck_ramp(lvl, &dl);
        for (i = 0; i < n; i++, lq += dl) {
            int32_t x = ((b[i] >> 2) * (lq >> 8)) >> 10, a = x < 0 ? -x : x;   /* pre-shift: 8 loud voices */
            int32_t xs = clamp(x, -xmax, xmax);         /* sends: mulq15 would overflow */
            if (a > pk)
                pk = a;
            if (d)
                SM(d)[i] += mulq15(xs, d);
            if (r)
                SM(rv)[i] += mulq15(xs, r);
            SM(l)[i] += (x * gl) >> 12;
            SM(r)[i] += (x * gr) >> 12;
        }
        t->peak = pk;
    }
    if (pf.thr)
        perf_throw(t, b, SM(d), SM(rv), n);             /* perform.c: REV / ECHO THROW (1.5.1), the sends towards full */
    if (MOD(t).on)
        mod_end(t);                                     /* the stored values back */
}

/* the HOME oscilloscope (ui_graph.c graph_scope): every other sample of the left channel. MENU > DISPLAY > SCOPE (1.2,
 * Discussion #165): OUT (0, the default) what goes out, after MASTER (audio.c audio_block, before the click); MIX the
 * mix before MASTER (and before the FX layer, SPEAKER EQ and the limiter), at the level MASTER at its top gives, so its
 * size does not follow the volume. The click is in neither */
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;
static volatile uint8_t scope_mix;
static __attribute__((noinline)) void scope_take_mix(uint32_t n)
{
    uint32_t i;
    for (i = 1; i < n; i += 2u)                         /* (((x >> 2) * MASTER_FULL) >> 10 is x) */
        scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int16_t)clamp(mix_l[i] + wet[i], -32767, 32767);
}

/* the MASTER level this block: MASTER_FULL when USB LEVEL FIXED took it (USB audio took the full
 * level: the DAC gets it after, audio.c usb_fixed_dac), else the knob */
static inline int32_t master_gain(void)
{
    return fx_usb_fixed ? MASTER_FULL : (int32_t)song.master_q12;
}

/* the master with the FX layer's effects between its level and master_out (perform.c) */
static __attribute__((noinline)) void perf_master(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t mg = master_gain();
    for (i = 0; i < n; i++) {
        mix_l[i] = (((mix_l[i] + wet[i]) >> 2) * mg) >> 10;
        mix_r[i] = (((mix_r[i] + wet[i]) >> 2) * mg) >> 10;
    }
    perf_block(mix_l, mix_r, n);
    for (i = 0; i < n; i++) {
        int32_t l = mix_l[i], r = mix_r[i];
        master_out(&l, &r);
        out[2u * i] = l;
        out[2u * i + 1u] = r;
    }
}

/* DRUM-X X-MOD LFO (eng_drum.c dx_mot / dx_mot_v): tempo-synced while playing (clk_step + clk_pos), free while stopped */
static uint32_t dx_mot_free, dx_mot_cyc, dx_ms;
static int32_t dx_mot_sh;
static __attribute__((noinline)) void dx_mot_block(uint32_t n)
{
    static const uint8_t Q8[9] = {0, 64, 32, 16, 8, 4, 2, 1, 1};   /* cycle in 1/16 notes x 4 .. (1/32: half) */
    uint32_t r = dx_mot[XM_RATE], len, pos, ph, cyc;
    int32_t w;
    dx_mot_free += n;
    if (song.playing)
        dx_ms = clk_pos == CLK_START ? dx_ms + n : clk_step * div_samples(2) + clk_pos + n;
    if (!r || r > 8u || !dx_mot[XM_DPTH]) {
        dx_mot_v = 0;
        return;
    }
    len = beat_samples() * Q8[r] / 4u;
    if (r == 8u)
        len = beat_samples() / 8u;
    if (len < 64u)
        len = 64u;
    pos = song.playing ? dx_ms : dx_mot_free;
    cyc = pos / len;
    ph = (uint32_t)(((uint64_t)(pos % len) << 32) / len);
    switch (dx_mot[XM_SHPE]) {
    case 1: w = ph < 0x80000000u ? (int32_t)(ph >> 15) - 32768 : 98303 - (int32_t)(ph >> 15); break;   /* TRI */
    case 2: w = (int32_t)(ph >> 16) - 32768; break;                                                   /* SAW */
    case 3: w = 32767 - (int32_t)(ph >> 16); break;                                                   /* RAMP */
    case 4:                                                                                             /* S&H */
        if (cyc != dx_mot_cyc) {
            dx_mot_rng = dx_mot_rng * 1664525u + 1013904223u;
            dx_mot_sh = (int32_t)(dx_mot_rng >> 16) - 32768;
        }
        w = dx_mot_sh;
        break;
    default: w = osc_sine(ph); break;                                                                  /* SINE */
    }
    dx_mot_cyc = cyc;
    dx_mot_v = (w * dx_mot[XM_DPTH]) >> 16;             /* (+-DPTH / 2) */
}

/* ---- the second core (hal/fm1_cpu1.h) renders some of the parts of each block into its own sums (mx[1]) while
 * this core renders the others; then this core adds them in and goes on (the LFOs' blocks, the FX layer, the buses,
 * the master) as with one core. The sums are integers, so the order they add up in changes nothing: the output is
 * bit for bit one core's. A part's render touches its own state only (its track_t with its SPREAD, ENV SYNC and LFO
 * phase, its voices, its matrix: mod.c MOD, its SPREAD buffer, its engine's memory) and what nobody writes meanwhile
 * (song, the tables); what parts share is done before the split (events_block: the notes, voice stealing; the FX
 * layer's perf_begin) or after it (the LFOs' S&H draw from the shared generator, part by part: voice.c
 * track_lfo_end). tests/dualcore_test.c checks it, under ThreadSanitizer too. Without a second core (the host, the
 * browser) C1_RUN takes nothing and this core renders them all */
#ifndef C1_ON
#define C1_ON() 0                                       /* a second core is answering */
#define C1_RUN(fn, arg) 0                               /* hand it fn(arg): 1 = taken */
#define C1_WAIT() 0                                     /* wait for it: 0 = done */
#endif
static uint32_t c1_blocks;                              /* blocks the second core took parts of */

static mixsum_t c1_part;                /* a part's sums while it renders: whole only into the core's sums (a job
                                         * cut mid-part leaves its half here, never in mx[1]) */
volatile uint32_t c1_done;              /* the parts whose sums are whole in mx[1] (mix_block: only these merge) */

static void c1_parts(uint32_t arg)                      /* arg: the parts (bit k = part k) | n << 8 */
{
    uint32_t i, k, n = arg >> 8;
    mixsum_t *sm = &mx[1];
    for (k = 0; k < NPART; k++)
        if (arg >> k & 1u) {
            for (i = 0; i < n; i++)
                c1_part.l[i] = c1_part.r[i] = c1_part.d[i] = c1_part.rv[i] = 0;
            mix_part(&trk[k], n, &c1_part);
            for (i = 0; i < n; i++) {
                sm->l[i] += c1_part.l[i];
                sm->r[i] += c1_part.r[i];
                sm->d[i] += c1_part.d[i];
                sm->rv[i] += c1_part.rv[i];
            }
            c1_done |= 1u << k;                         /* this part's sums are whole in mx[1] */
        }
}

/* which parts go to the second core: the split of the last block's costs that leaves the busier core the least; none
 * when that saves less than handing them over costs (C1_HAND). Keep this per-block scheduler out of the audio
 * interrupt's render loops (target_budget). */
#define C1_HAND 40u
static __attribute__((noinline)) uint32_t mix_split(void)
{
    uint32_t m, i, best = 0, all = 0, lo, chor = 0, deep = 0;
    for (i = 0; i < NTRK; i++)                      /* deep CHORUS as a shared send: its delay (up to 238) wraps into
                                                     * the swept line's window, where this block's reads saw the
                                                     * previous consumer's writes (ins_run_swept): that chain runs in
                                                     * part order, so no split (exact to DEP 107; oracle-tested) */
        if (ins_is_send(&trk[i], INS_CHOR) && ins_mix_of(&trk[i], INS_CHOR) != 0) {
            chor++;
            deep |= (uint32_t)ins_param_src(&trk[i], INS_CHOR)->p[P_IHDEP] >= 108u;
        }
    if (chor >= 2u && deep)
        return 0;
    for (i = 0; i < NPART; i++)
        all += part_cost[i];
    lo = all;
    for (m = 1; m < (1u << NPART) - 1u; m++) {
        uint32_t a = 0, w;
        for (i = 0; i < NPART; i++)
            if (m >> i & 1u)
                a += part_cost[i];
        w = (a > all - a ? a : all - a) + C1_HAND;
        if (w < lo) {
            lo = w;
            best = m;
        }
    }
    return best;
}

static void mix_block(int32_t *out, uint32_t n)
{
    uint32_t i;
    int perf;
    mod_bus_n = 0;
    int32_t mg = master_gain();                          /* (USB LEVEL FIXED: MASTER after usb_fixed_dac) */
    for (i = 0; i < n; i++)
        send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    slicer_block();                                     /* shared SEND bus: one clock advance this block */
    ins_bus_block();                                    /* shared insert SEND DSP */
    pfx_block(n, (perf_kill ? 0u : (perf_held | perf_latched) & PF_MIDI));
    events_block(n);
    clip_eff = song.g[G_CLIP];                          /* (JIANT) CLIP / PNCH for this block */
    pnch_eff = song.g[G_PUNCH];
    dx_mot_block(n);                                    /* DRUM-X X-MOD LFO */
    master_begin();
    duck_block();
    perf = perf_begin(n);                               /* the FX hold layer at work (perform.c) */
    rev_tank[0].hl.side = rev_tank[1].hl.side = 0;      /* HALL sides set this block by insert / send */
    for (i = 0; i < NTRK; i++)                          /* every part's copy of the bus settings (core.h bus_knobs) */
        bus_knobs_g(&trk[i]);
    {   /* the parts: some on the second core, into its own sums (mx[1]) */
        uint32_t m = C1_ON() ? mix_split() : 0u;
#ifdef C1_SPLIT
        if (m)                                         /* host splits cannot bypass the shared-chorus safety gate */
            m = C1_SPLIT() & ((1u << NPART) - 1u);
#endif
        ins_bus_parallel = m != 0;
        if (m) {
            c1_done = 0;
            for (i = 0; i < n; i++)                    /* rejected jobs never enter c1_parts */
                mx[1].l[i] = mx[1].r[i] = mx[1].d[i] = mx[1].rv[i] = 0;
            if (!C1_RUN(c1_parts, m | n << 8))
                m = 0;
        }
        if (!m) {
            ins_bus_parallel = 0;
            memset(ins_bus_driver, 0xFF, sizeof ins_bus_driver);
        }
        c1_blocks += m != 0;
        for (i = 0; i < NPART; i++)
            if (!(m >> i & 1u))
                mix_part(&trk[i], n, &mx[0]);
        if (m) {
            const mixsum_t *sm = &mx[1];
            if (C1_WAIT()) {                            /* it stopped answering (held from now on) */
                uint32_t left = m & ~c1_done;           /* the parts whose sums never landed: this core renders them
                                                         * (the one cut mid-part advances its state twice: one block's
                                                         * artifact, not a dropout) */
                for (i = 0; i < NPART; i++)
                    if (left >> i & 1u)
                        mix_part(&trk[i], n, &mx[0]);
            }
            for (i = 0; i < n; i++) {
                mix_l[i] += sm->l[i];
                mix_r[i] += sm->r[i];
                send_d[i] += sm->d[i];
                send_r[i] += sm->rv[i];
            }
        }
    }
    if (ins_bus_parallel)
        ins_bus_join(n);                                /* the shared SEND state's run and line windows */
    for (i = 0; i < NPART; i++)                         /* the LFOs' blocks, part by part (voice.c) */
        track_lfo_end(&trk[i]);
    if (pfx_any)
        pfx_end();                                      /* (DEC- / DEC+ / ATK+ params back) */
    if (perf)
        perf_pre(mix_l, mix_r, send_d, send_r, n);
    fx_buses(send_d, send_r, wet, n);
    if (rev_tank[0].hl.side || rev_tank[1].hl.side)
        rev_side_mix(mix_l, mix_r, n);                  /* HALL: stereo (either tank) */
    if (clip_g)
        clip_block(n);
    if (scope_mix)
        scope_take_mix(n);
    if (perf) {
        perf_master(out, n);
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t l = (((mix_l[i] + wet[i]) >> 2) * mg) >> 10;
        int32_t r = (((mix_r[i] + wet[i]) >> 2) * mg) >> 10;
        master_out(&l, &r);
        out[2u * i] = l;
        out[2u * i + 1u] = r;
    }
}
