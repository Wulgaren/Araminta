/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLICER: tempo-synced 16-step gate / stutter on the EFFECTS list (INS_SLCR).
 * CHAIN: private DSP in list order (fx.c track_insert); each track may CHAIN at once.
 * SEND: one shared bus DSP (sl_bus); MODE/PAT/RATE shared (fx_bus_trk), MIX per-track.
 * Legacy G_SLOWN 1..4: one CHAIN owner, others USED (untouched) until sanitized to a mask.
 *   P_SLCR    OFF / GATE / STUT
 *   P_SLPAT   one of SL_NPAT patterns of 16 steps ('x' live / open, '.' gated / repeated)
 *   P_SLRATE  the step: 1/8, 1/16, 1/32, 8T, 16T, 32T
 *   P_SLDEPTH MIX: GATE how far a '.' step closes (100 % = silent); STUT: how much of the repeat
 *             replaces live on a '.' step. MIX 0 is transparent (bypass) for both.
 * GATE: the gain moves at most 1 / SL_RAMP per sample (2.9 ms from open to closed): a closing
 *   ramp ends on the step boundary, an opening one starts on it.
 * STUT: an 'x' step plays live and is recorded (22.05 kHz, 16 bit, SL_LEN); a '.' step cross-fades
 *   live with that recording at MIX. With a decayed live note, MIX scales how loud the hit is.
 *   A quiet 'x' (rest / ring) does not replace the last loud take — so acid rests on 'x' do not
 *   punch holes on the following '.'.
 *   Repeat playback slew-limits so a sharp zap in the take is not machine-gunned on long '.' runs
 *   (pat 6/9): that read as electric clicks.
 * Active GATE/STUT (MIX > 0 or a ramp left): output is ~-3 dB so the slicer sits under dry.
 * The step clock: always running (also with the SLICER OFF, so switching it on lands in time),
 * restarted with the transport (seq_start -> slicer_start). With OFF and no ramp left, untouched. */
#define SL_NPAT 16
#define SL_LEN 4096u                    /* recording, 22.05 kHz samples a track: 186 ms, 8 KB */
#define SL_RAMP_LOG2 7
#define SL_RAMP (1 << SL_RAMP_LOG2)     /* 128 samples, 2.9 ms */
#define SL_SLOPE (32768 / SL_RAMP)
#define SL_PAD 23170                    /* Q15: ~-3 dB while GATE/STUT is active (MIX 0 = bypass) */
#define SL_MINPEAK 2048                 /* live peak below this: keep the previous loud take */
#define SL_SLEW 2500                    /* max |Δ| per sample on the repeat (soften recorded zaps) */
enum { SL_OFF, SL_GATE, SL_STUT };      /* P_SLCR (params.c N_SLCR) */

/* bit k = step k: 1 = 'x' (open / live), 0 = '.' (closed / repeat) */
static const uint16_t SL_PAT[SL_NPAT] = {
    0x5555,   /*  1 x.x.x.x.x.x.x.x. */
    0xB6DB,   /*  2 xx.xx.xx.xx.xx.x */
    0x5249,   /*  3 x..x..x..x..x.x. */
    0xB777,   /*  4 xxx.xxx.xxx.xx.x */
    0x6D6D,   /*  5 x.xx.xx.x.xx.xx. */
    0x0F0F,   /*  6 xxxx....xxxx.... */
    0x1111,   /*  7 x...x...x...x... */
    0x54A5,   /*  8 x.x..x.x..x.x.x. */
    0x5333,   /*  9 xx..xx..xx..x.x. */
    0xA929,   /* 10 x..x.x..x..x.x.x */
    0xAAFF,   /* 11 xxxxxxxx.x.x.x.x */
    0xD501,   /* 12 x.......x.x.x.xx */
    0xAAAA,   /* 13 .x.x.x.x.x.x.x.x */
    0xAB6B,   /* 14 xx.x.xx.xx.x.x.x */
    0xBB5D,   /* 15 x.xxx.x.xx.xxx.x */
    0x7597,   /* 16 xxx.x..xx.x.xxx. */
};
static const uint8_t SL_DEN[6] = {2, 4, 8, 3, 6, 12};   /* P_SLRATE (N_SLDIV): a step = 1 / DEN beats */

static int16_t sl_buf[NTRK][SL_LEN] __attribute__((section(".pool")));
static int16_t sl_pending[SL_LEN] __attribute__((section(".pool"))); /* last loud STUT take */
/* SEND STUT recording uses a SEND track's sl_buf (CHAIN tracks keep theirs for private STUT). */
typedef struct {
    uint32_t pos, len;           /* samples into the step, its length */
    uint32_t base;               /* the step without swing */
    uint32_t rp, loop;           /* repeat: read position, loop length (44.1 kHz samples), 0 = none */
    uint32_t rec;                /* recorded (22.05 kHz samples) */
    uint32_t rec_ok;             /* last loud take length (22.05 kHz); '.' loops this */
    int32_t gc;                  /* gate closure, Q15: 0 = open */
    int32_t w;                   /* STUT cross-fade, Q15: 0 = live */
    int32_t pa;                  /* output pad attenuation, Q15: 0 = full level */
    int32_t half;                /* recording: the first sample of a pair */
    int32_t rec_peak;            /* peak |x| while recording this live step */
    int32_t ok_peak;             /* peak of the committed take (reject quieter rests) */
    int32_t r_prev;              /* last repeat sample (slew limit) */
    uint8_t idx;                 /* step 0..15 */
    uint8_t bit;                 /* this step's pattern bit (latched at its start) */
    uint8_t rec_on;              /* recording this step */
} sl_t;
static sl_t sl[NTRK];
static sl_t sl_bus;              /* shared SEND bus clock (idx/pos/len/bit/loop) */
static sl_t sl_bus_blk;          /* sl_bus at the start of the current mix block */
static int32_t sl_send_gc[NTRK]; /* per-track gate envelope while SEND (MIX is per-track) */
static int32_t sl_send_w[NTRK];
static int32_t sl_send_pa[NTRK];
static uint8_t sl_bus_applied;   /* 1 after the first SEND track committed the bus clock this block */
static uint8_t sl_bus_driver;    /* track that advanced the bus this block (re-calls keep advancing) */
static uint8_t sl_lent;          /* perform.c has borrowed sl_buf: STUT plays live, records nothing */

/* which DSP state this track uses: shared bus (SEND), private (CHAIN), or null (USED) */
static sl_t *sl_state(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk);
    if (sl_trk_used(t))
        return 0;
    if (sl_trk_send(t))
        return &sl_bus;
    return &sl[tr];
}
/* a free STUT buffer for the SEND bus: any SEND track's slot (idle while that track is SEND) */
static uint32_t sl_send_buf(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (sl_trk_send(&trk[k]))
            return k;
    return 0;
}

static void slicer_block(void)   /* fx.c mix_block: new audio block for the shared SEND bus */
{
    sl_bus_applied = 0;
    sl_bus_driver = 0xFF;
}

static void slicer_start(void)   /* seq_start: the next block starts step 0 of every track */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        sl[k].idx = 15;
        sl[k].pos = sl[k].len = 0;
        sl[k].pa = 0;
        sl_send_gc[k] = sl_send_w[k] = sl_send_pa[k] = 0;
    }
    sl_bus.idx = 15;
    sl_bus.pos = sl_bus.len = 0;
    sl_bus_applied = 0;
}

static uint32_t sl_pattern(const track_t *t) { return SL_PAT[(uint32_t)(t->p[P_SLPAT] - 1) % SL_NPAT]; }

/* SEND bus clock: song swing only (one shared length). CHAIN: track + song swing. */
static uint32_t sl_step_len(const track_t *pt, uint32_t base, uint32_t idx)
{
    if (pt == &fx_bus_trk) {
        int32_t sw = song.g[G_SWING];
        if (sw < 0) sw = 0;
        if (sw > SWING_MAX) sw = SWING_MAX;
        sw = sw * (int32_t)base / 250;
        return base + (uint32_t)((idx & 1u) ? -sw : sw);
    }
    return swing_step_len(pt, base, idx);
}
/* the step clock enters the next step; buf is this track's STUT recording (or 0 if clock-only) */
static void sl_enter(const track_t *t, sl_t *s, int16_t *buf)
{
    uint32_t mode = (uint32_t)t->p[P_SLCR];
    /* commit a loud take, or restore if this live step is quiet / a rest tail */
    if (mode == SL_STUT && !sl_lent && buf && s->rec_on) {
        int loud = s->rec_peak >= SL_MINPEAK && s->rec >= 2u * SL_RAMP &&
                   (s->ok_peak == 0 || s->rec_peak * 2 >= s->ok_peak);
        if (loud) {
            s->rec_ok = s->rec;
            s->ok_peak = s->rec_peak;
            memcpy(sl_pending, buf, s->rec_ok * sizeof(int16_t));
        } else if (s->rec_ok >= 2u * SL_RAMP) {
            memcpy(buf, sl_pending, s->rec_ok * sizeof(int16_t));
            s->rec = s->rec_ok;
        }
    }
    s->idx = (uint8_t)((s->idx + 1u) & 15u);
    s->base = (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM] / SL_DEN[(uint32_t)t->p[P_SLRATE] % 6u];
    s->len = sl_step_len(t, s->base, s->idx);
    s->pos = 0;
    s->bit = (uint8_t)((sl_pattern(t) >> s->idx) & 1u);
    s->rp = 0;
    s->loop = 0;
    s->rec_on = 0;
    if (mode != SL_STUT || sl_lent) {
        s->rec = 0;                                 /* nothing old to repeat when STUT comes on */
        s->rec_ok = 0;
        s->rec_peak = 0;
        s->ok_peak = 0;
    } else if (s->bit) {
        s->rec = 0;                                 /* a live step: record it */
        s->rec_peak = 0;
        s->rec_on = 1;
    } else if (s->rec_ok >= 2u * SL_RAMP) {         /* a repeat: the last loud take, looped */
        uint32_t l = s->base;
        while (l > 2u * SL_LEN)
            l >>= 1;                                /* (a long step: a half, a quarter .. of it) */
        s->loop = l < 2u * s->rec_ok ? l : 2u * s->rec_ok;
    }
}

/* m samples of one step (no boundary inside). sd: SPREAD's side signal (fx.c mix_spread) or 0: GATE closes it
 * and STUT ducks it with MIX (the repeat is mono on PAN). Inlined into both callers. */
/* pt: MODE/PAT (and STUT clock); t: MIX (P_SLDEPTH). CHAIN passes the same track for both. */
static inline __attribute__((always_inline)) void sl_seg(const track_t *pt, const track_t *t, sl_t *s,
                                                          int16_t *buf, int32_t *b, int32_t *sd, uint32_t m)
{
    uint32_t mode = (uint32_t)pt->p[P_SLCR], j, nbit = (sl_pattern(pt) >> ((s->idx + 1u) & 15u)) & 1u;
    int32_t mix = t->p[P_SLDEPTH] * 258;            /* Q15, 0..32766 (MIX amount) */
    int32_t tc = mode == SL_GATE && !s->bit ? mix : 0;             /* gate closure of this step */
    int32_t tn = mode == SL_GATE && !nbit ? mix : 0;               /* .. of the next one */
    int32_t tw = mode == SL_STUT && s->loop ? mix : 0;             /* STUT cross-fade toward repeat */
    int32_t tp = (mode == SL_GATE || mode == SL_STUT) && mix ? 32768 - SL_PAD : 0;
    for (j = 0; j < m; j++) {
        int32_t x = b[j], y, tg = tc, ax;
        uint32_t left = s->len - s->pos - j;        /* samples to the step end, >= 1 */
        if (left <= (uint32_t)SL_RAMP && tn > tg)
            tg = tn;                                /* close by the boundary */
        s->gc += clamp(tg - s->gc, -SL_SLOPE, SL_SLOPE);
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        s->pa += clamp(tp - s->pa, -SL_SLOPE, SL_SLOPE);
        y = s->gc ? x - mulq16(x, (uint32_t)s->gc << 1) : x;
        if (sd) {
            int32_t z = sd[j];
            if (s->gc)
                z -= mulq16(z, (uint32_t)s->gc << 1);
            if (s->w)
                z -= mulq16(z, (uint32_t)s->w << 1);
            sd[j] = z;
        }
        if (s->rec_on) {                            /* 2:1, the pair's mean; Q15 >> 2 (the mix's headroom) */
            ax = x < 0 ? -x : x;
            if (ax > s->rec_peak)
                s->rec_peak = ax;
            if ((s->pos + j) & 1u) {
                buf[s->rec] = (int16_t)clamp((s->half + x) >> 3, -32768, 32767);
                if (++s->rec >= SL_LEN)
                    s->rec_on = 0;
            } else {
                s->half = x;
            }
        }
        if (s->loop) {                              /* repeat at MIX, windowed, cross-faded with live */
            uint32_t rp = s->rp, e = s->loop - 1u - rp, rl = s->rec_ok ? s->rec_ok : SL_LEN;
            uint32_t i0 = rp >> 1;
            int32_t r, d;
            if (i0 >= rl)
                i0 = 0;
            r = buf[i0];
            if (rp & 1u) {
                uint32_t i1 = i0 + 1u;
                if (i1 >= rl)
                    i1 = 0;
                r = (r + buf[i1]) >> 1;
            }
            r <<= 2;
            if (rp < e)
                e = rp;
            if (left - 1u < e)
                e = left - 1u;
            if (e < (uint32_t)SL_RAMP)
                r = (r * (int32_t)e) >> SL_RAMP_LOG2;
            d = r - s->r_prev;                      /* soften a brick-wall in the take */
            if (d > SL_SLEW)
                r = s->r_prev + SL_SLEW;
            else if (d < -SL_SLEW)
                r = s->r_prev - SL_SLEW;
            s->r_prev = r;
            if (++s->rp >= s->loop)
                s->rp = 0;
            y += mulq16(r - y, (uint32_t)s->w << 1);
        } else {
            s->r_prev = 0;
            if (s->w)
                y -= mulq16(y, (uint32_t)s->w << 1); /* leave a repeat: fade live in as w falls */
        }
        if (s->pa) {                                /* ramped ~-3 dB under dry while GATE/STUT MIX > 0 */
            y -= mulq16(y, (uint32_t)s->pa << 1);
            if (sd)
                sd[j] -= mulq16(sd[j], (uint32_t)s->pa << 1);
        }
        b[j] = y;
    }
}

/* pt: MODE/PAT/RATE (SEND: fx_bus_trk; CHAIN: the track). MIX from t. */
static void sl_run(const track_t *pt, const track_t *t, sl_t *s, int16_t *buf, int32_t *b, int32_t *sd,
                   uint32_t n)
{
    uint32_t i = 0;
    /* MIX 0 with envelopes settled: bypass (no record, no pad). Clock still advances. */
    int act = b && ((pt->p[P_SLCR] != SL_OFF && t->p[P_SLDEPTH]) || s->gc || s->w || s->pa);
    while (i < n) {
        uint32_t m;
        if (s->pos >= s->len)
            sl_enter(pt, s, buf);
        m = s->len - s->pos;
        if (m > n - i)
            m = n - i;
        if (act)
            sl_seg(pt, t, s, buf, b + i, sd ? sd + i : 0, m);
        s->pos += m;
        i += m;
    }
}

/* SEND: shared clock in sl_bus, per-track MIX envelopes in sl_send_gc/w.
 * First SEND track in a mix block advances the bus; others replay from the block start.
 * The same driver calling again (host tests without slicer_block) keeps advancing. */
static void sl_run_send(const track_t *t, int32_t *b, int32_t *sd, uint32_t n)
{
    uint32_t k = (uint32_t)(t - trk);
    const track_t *pt = &fx_bus_trk;                 /* SEND bus MODE/PAT/RATE */
    sl_t tmp;
    int drive = !sl_bus_applied || k == sl_bus_driver;
    if (!sl_bus_applied)
        sl_bus_blk = sl_bus;
    tmp = drive && sl_bus_applied ? sl_bus : sl_bus_blk;
    tmp.gc = sl_send_gc[k];
    tmp.w = sl_send_w[k];
    tmp.pa = sl_send_pa[k];
    /* record into a SEND track's buffer; followers replay from the driver's pick */
    sl_run(pt, t, &tmp, sl_buf[drive ? sl_send_buf() : sl_bus_driver], b, sd, n);
    sl_send_gc[k] = tmp.gc;
    sl_send_w[k] = tmp.w;
    sl_send_pa[k] = tmp.pa;
    if (drive) {
        tmp.gc = tmp.w = tmp.pa = 0;
        sl_bus = tmp;
        sl_bus_applied = 1;
        sl_bus_driver = (uint8_t)sl_send_buf();        /* buffer index followers replay from */
    }
}

/* the step clock over n samples and the SLICER on b (0: the clock only, the track is silent) */
static void slicer_track(const track_t *t, int32_t *b, uint32_t n)
{
    uint32_t k = (uint32_t)(t - trk);
    sl_t *s = sl_state(t);
    if (!s)                                          /* USED: another track has CHAIN */
        return;
    if (s == &sl_bus) {
        sl_run_send(t, b, 0, n);
        return;
    }
    sl_run(t, t, s, sl_buf[k], b, 0, n);
}

/* slicer_track with SPREAD's side signal sd (fx.c mix_spread; b != 0) */
static __attribute__((noinline)) void slicer_track_sd(const track_t *t, int32_t *b, int32_t *sd, uint32_t n)
{
    uint32_t k = (uint32_t)(t - trk);
    sl_t *s = sl_state(t);
    if (!s)
        return;
    if (s == &sl_bus) {
        sl_run_send(t, b, sd, n);
        return;
    }
    sl_run(t, t, s, sl_buf[k], b, sd, n);
}

/* a silent part must still be rendered: a repeat is playing or fading (fx.c mix_part) */
static int slicer_busy(const track_t *t)
{
    uint32_t k = (uint32_t)(t - trk);
    const sl_t *s = sl_state(t);
    if (!s)
        return 0;
    if (s == &sl_bus)
        return sl_send_w[k] || sl_send_pa[k] ||
               (t->p[P_SLDEPTH] && fx_bus_trk.p[P_SLCR] == SL_STUT && sl_bus.loop);
    return s->w || s->pa || (t->p[P_SLDEPTH] && t->p[P_SLCR] == SL_STUT && s->loop);
}
