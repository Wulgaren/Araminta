/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* PFX (JIANT, FELUCCA-TONIC-SPEC.md fase 7): the punch-in MIDI effects of the FX layer. They act on the notes and
 * the sound of the tracks while their key is held (or latched: FX LATCH), never on what is stored: let go, all is
 * as it was. The MIDI keys (perform.c PF_DEF / PF2_DEF; PUNCH-IN 1/2 unless noted):
 *   A4 /2 TEMPO (2/2)  the track's sequencer at half speed; let go, it is back where it would have been
 *   B4 *2 TEMPO (3.2, 2/2)  the same at double speed; /2 + *2 cancel
 *   E4 OCT-, F4 OCT+   every voice an octave down / up (both: none), at once, notes held too
 *   G4 DEC-, A4 DEC+   short / long decays: a synth's DEC and REL, DRUM's DECY (DEC- wins); (JIANT 0.4) DEC- also
 *                      takes a synth's sustain to 0: the whole instrument plucks
 *   B4 C5              STUTTER 1/8, 1/16 (JIANT 0.4; were 1/16 1/32): the notes the track struck last together (a
 *                      step's chord, a drum step's hits) again at the rate, gated half; the sequencer's own notes wait
 *   D5 ATK+            (JIANT, was STUTTER 1/16T) every synth's attack slow (two thirds of the way to the longest);
 *                      (0.4) a DRUM hit faded in over ~35 ms (eng_drum.c): everything softer
 *   E5 ARP             those notes one at a time at 1/16, up two octaves; (JIANT 0.4) DRUM: a fill, a new one at
 *                      every press (pfx_fill: snare, toms down, hats, claps, rim, rests, its own accents)
 *   F5 RANDOM          each new note: an octave, a fifth or a fourth away now and then (DRUM: +-3 semitones); and
 *                      (JIANT) the sequencer plays a step of its pattern at random now and then (seq.c seq_tick)
 * (JIANT) Quantized: an effect pressed while the transport runs goes in on the next 1/16 of the transport (clk_pos),
 * so a repeat starts on the grid; let go, at once. The lane records the presses as they were (to the nearest step).
 * Of the repeats (STUTTER, ARP) the fastest held plays. A#4 (black key 8) steps the tracks they act on: ALL,
 * SYN (the synths), DRM (DRUM tracks) (pfx_tgt; the layer's header says it).
 * The punch-in lane (the automation): 64 steps of 1/16 (4 bars, from the transport's start: one loop for the whole
 * transport, the same in every song section), any punch-ins of either page a step (pfx_lane: a PF_BIT mask a step)
 * and the tracks its MIDI effects act on (pfx_ltgt).
 * Recording (REC armed, playing, not a song): each effect's press and release quantizes on its own (a press in a
 * step's second half starts at the next 1/16, letting go in a step's first half ends at the previous one); the steps
 * between get it, added to what they held (a second pass layers; the lane plays on while it records), a MIDI one
 * with the tracks A#4 picked.
 * Played back with the held ones: the MIDI effects here, the audio ones on the master (perform.c perf_begin).
 * G5 CLR held while recording empties the steps it passes (what is held with it is not recorded); its press and
 * release quantize as an effect's. With FX LATCH its press also turns the latched effects off.
 * FUN21 saves the lane in the project's tail (project.c PROJ_LANE_OFF); a FUN20 loads its one effect a step.
 * Nothing stages it per song section (song_chain.c never mentions it).
 * In the ISR: pfx_block before the events (what acts on each track, the repeats, the decays put in), pfx_end after
 * the parts (the decays back: as mod.c, nothing outside the ISR sees them). Idle: one test a block. */
static uint8_t pfx_in;                /* a repeat's own note-on (pfx_note lets it through) */
static uint8_t pfx_any;               /* a track has something running (or to put back) */
static uint32_t pfx_blk;              /* blocks: the notes of one block were struck together */
static uint32_t pfx_seed = 0x9E3779B9u;

/* the lane holds a recorded effect: AUTOMATION's and SAVE > CLEAR's clears have something to do */
static int pfx_lane_used(void)
{
    uint32_t i;
    for (i = 0; i < 64u; i++)
        if (pfx_lane[i])
            return 1;
    return 0;
}
/* those clears: the lane empty, its MIDI effects back on ALL tracks */
static void pfx_lane_clear(void)
{
    memset(pfx_lane, 0, sizeof pfx_lane);
    pfx_ltgt = 0;
}

/* voice.c trk_note_on, before it starts a note: 1 = not played (a repeat holds the track). Remembers the notes
 * struck together; RANDOM draws the note's offset */
static int pfx_note(track_t *t, uint32_t note, uint32_t vel)
{
    if (pfx_in)
        return 0;
    if (t->pfx & PFX_REP)
        return 1;
    if (t->pfx_lblk != pfx_blk) {
        t->pfx_lblk = pfx_blk;
        t->pfx_lnn = 0;
    }
    if (t->pfx_lnn < 4u) {
        t->pfx_ln[t->pfx_lnn] = (uint8_t)note;
        t->pfx_lv[t->pfx_lnn++] = (uint8_t)vel;
    }
    t->pfx_rpit = 0;
    if (t->pfx & PFX_RND) {
        static const int8_t R[8] = {0, 0, 12, -12, 7, -5, 0, 12};
        pfx_seed = pfx_seed * 1664525u + 1013904223u;
        t->pfx_rpit = (int16_t)(16 * (t->engine == ENGI_DRUM ? (int32_t)(pfx_seed >> 29) - 3 : R[pfx_seed >> 29]));
    }
    return 0;
}

/* the notes a repeat sounds end */
static void pfx_hush(track_t *t)
{
    uint32_t i;
    for (i = 0; i < t->pfx_snn; i++)
        trk_note_off(t, t->pfx_sn[i]);
    t->pfx_snn = 0;
}

/* a repeat: the notes again (STUTTER), or the next of them (ARP) */
static __attribute__((noinline)) void pfx_fire(track_t *t, int arp)
{
    uint32_t i, n = t->pfx_lnn;
    if (!n && !(arp && t->engine == ENGI_DRUM))         /* (a drum fill needs no notes struck) */
        return;
    pfx_hush(t);
    pfx_in = 1;
    t->pfx_rpit = 0;
    if (arp && t->engine == ENGI_DRUM) {               /* (JIANT 0.4) a fill: each press its own (pfx_seed runs on) */
        static const uint8_t FILL[16] = {38, 38, 40, 38, 50, 48, 47, 45, 43, 41, 42, 42, 46, 39, 37, 36};
        uint32_t r, note;
        pfx_seed = pfx_seed * 1664525u + 1013904223u;
        r = pfx_seed >> 24;
        if (r < 40u) {                                  /* (a rest now and then) */
            pfx_in = 0;
            return;
        }
        note = r < 150u ? FILL[(pfx_seed >> 16) & 3u]  /* the snare, most */
             : r < 215u ? FILL[4u + ((t->pfx_si + ((pfx_seed >> 16) & 1u)) % 6u)]   /* toms, mostly downwards */
             : FILL[10u + ((pfx_seed >> 16) % 6u)];
        t->pfx_si++;
        trk_note_on(t, note, 60u + ((pfx_seed >> 8) & 63u) + (r & 1u ? 0u : 4u));
        t->pfx_sn[0] = (uint8_t)note;
        t->pfx_snn = 1;
    } else if (arp) {
        uint32_t k = t->pfx_si++ % (t->engine == ENGI_DRUM ? n : 3u * n);
        uint32_t note = t->pfx_ln[k % n] + 12u * (k / n);
        note = note > 127u ? 127u : note;
        trk_note_on(t, note, t->pfx_lv[k % n]);
        t->pfx_sn[0] = (uint8_t)note;
        t->pfx_snn = 1;
    } else {
        for (i = 0; i < n; i++) {
            trk_note_on(t, t->pfx_ln[i], t->pfx_lv[i]);
            t->pfx_sn[i] = t->pfx_ln[i];
        }
        t->pfx_snn = (uint8_t)n;
    }
    pfx_in = 0;
}

/* the values DEC- / DEC+ took out (pfx_end puts them back) */
static int16_t pfx_keep[NTRK][4];   /* DEC, REL (DRUM: DECY); ATK; SUS (DEC-) */
static uint32_t pfx_qon;            /* (JIANT) the effects in (quantized to the 1/16) */

static int pfx_rec_ok(void);          /* seq.c: REC armed, not a song playing */
static uint32_t pfx_rheld;            /* recording the lane: the effects held last block (the press / release edges) */
static uint32_t pfx_rold;             /* .. the step being written, as it was before this recording wrote it */
static uint8_t pfx_rskip[PF_N];       /* .. per press, the step before its first (not written) */
static uint8_t pfx_rstep = 0xFF;      /* .. the step pfx_rold was taken in */
static uint8_t pfx_rclr;              /* .. CLR held last block; pfx_cskip: as pfx_rskip, for CLR (0xFF: none) */
static uint8_t pfx_cskip = 0xFF;

/* each block, before the events (fx.c mix_block). held: the effects held (PF_* bits, audio ones too: the lane
 * records them; this file acts on the MIDI ones) */
static __attribute__((noinline)) void pfx_block(uint32_t n, uint32_t held)
{
    uint32_t k, b = beat_samples(), lane = 0, raw = held, nw;
    held &= PF_MIDI;
    pfx_blk++;
    pfx_qon &= held;                                    /* (JIANT) let go: at once; pressed: on the next 1/16 */
    nw = held & ~pfx_qon;
    if (nw) {
        uint32_t s16 = div_samples(2), ph = clk_pos == CLK_START ? 0u : (clk_pos + n) % s16;   /* (the transport's 1/16:
                                                         * before events_block clk_pos is the last block's start) */
        if (!song.playing || !ph || s16 - ph < n)
            pfx_qon |= nw;
    }
    held = pfx_qon;
    if (song.playing && b >= 4u) {                      /* the lane: record, or play */
        uint32_t s16 = b / 4u, ph = pfx_lph % s16, st = (pfx_lph / s16) & 63u, half = ph >= s16 / 2u;
        pfx_lph += n;
        if (pfx_rec_ok()) {                             /* recording, to the nearest step: each effect's press and
                                                         * release quantizes on its own */
            uint32_t pressed = raw & ~pfx_rheld, released = pfx_rheld & ~raw, m, e, bit;
            pfx_rheld = raw;
            if (st != pfx_rstep) {                      /* (the step as it was before this recording wrote it) */
                pfx_rstep = (uint8_t)st;
                pfx_rold = pfx_lane[st];
            }
            if (pfx_rclr && !pfx_clr && !half)          /* CLR let go in a step's first half: that step as it was */
                pfx_lane[st] = pfx_rold;
            for (m = raw; m; m &= m - 1u) {             /* each held effect on its own steps: a press in a step's
                                                         * second half starts at the next one */
                e = (uint32_t)__builtin_ctz(m);
                bit = m & -m;
                if (pressed & bit)
                    pfx_rskip[e] = (uint8_t)((st + half - 1u) & 63u);   /* the step before its first */
                if (st != pfx_rskip[e])
                    pfx_lane[st] |= bit;                /* (layered on what the step held) */
            }
            for (m = released; m; m &= m - 1u) {        /* let go in a step's first half: that step as it was
                                                         * (no write in this step: pfx_rold is the step itself) */
                bit = m & -m;
                if (!half)
                    pfx_lane[st] = (pfx_lane[st] & ~bit) | (pfx_rold & bit);
            }
            if (raw & PF_MIDI)                          /* (an audio effect plays on the master: no tracks) */
                pfx_ltgt = pfx_tgt;
            if (pfx_clr) {                              /* CLR held: the steps it passes emptied, what is held with
                                                         * it too; pressed in a step's second half, from the next */
                if (!pfx_rclr)
                    pfx_cskip = (uint8_t)((st + half - 1u) & 63u);
                if (st != pfx_cskip) {
                    pfx_lane[st] = 0;
                    pfx_cskip = 0xFF;
                }
            }
            pfx_rclr = pfx_clr;
        } else {
            pfx_rheld = 0;
            pfx_rstep = 0xFF;
            pfx_rclr = 0;
        }
        lane = pfx_lane[st] & PF_MIDI;                  /* (the audio ones: perform.c perf_begin) */
    }
    if (!held && !pfx_any && !lane && !nw)
        return;
    pfx_any = 0;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        int drum = t->engine == ENGI_DRUM;
        uint32_t on = (!pfx_tgt || (pfx_tgt == 1u) == !drum ? held : 0u) | (!pfx_ltgt || (pfx_ltgt == 1u) == !drum ? lane : 0u);
        uint32_t old = t->pfx, f, rep, per = 0;
        rep = on & (PF_BIT(PF_S16) | PF_BIT(PF_S8) | PF_BIT(PF_ARP));
        per = (rep & (PF_BIT(PF_S16) | PF_BIT(PF_ARP))) ? b / 4u : b / 2u;   /* (JIANT 0.4: 1/8, 1/16; ARP 1/16) */
        t->pfx_pit = (int16_t)(((on >> PF_OCTU) & 1u) * 192 - ((on >> PF_OCTD) & 1u) * 192);
        f = ((on >> PF_RND) & 1u ? PFX_RND : 0u) | ((on >> PF_HALF) & 1u ? PFX_HALF : 0u) |
            ((on >> PF_X2) & 1u ? PFX_X2 : 0u) | (rep ? PFX_REP : 0u) |
            (on & (PF_BIT(PF_DSHT) | PF_BIT(PF_DLNG)) ? PFX_DEC : 0u) | ((on >> PF_ATK) & 1u ? PFX_ATK : 0u);
        if ((f & (PFX_HALF | PFX_X2)) && !(old & (PFX_HALF | PFX_X2)) && song.playing) {
            t->pfx_idx0 = t->seq_idx;                   /* 1/2 or x2 TEMPO: where it was */
            t->pfx_pos0 = t->seq_pos;
            t->pfx_el = 0;
            t->pfx_half = 0;
        } else if (!(f & (PFX_HALF | PFX_X2)) && (old & (PFX_HALF | PFX_X2))) {
            t->pfx_rs = 1;                              /* (seq.c seq_tick: back to the real place) */
        }
        if (rep && !(old & PFX_REP)) {
            t->pfx_si = 0;
            t->pfx_sc = per;                            /* (the first repeat now) */
        } else if (!rep && (old & PFX_REP)) {
            pfx_hush(t);
        }
        t->pfx = (uint8_t)f;
        if (rep) {
            t->pfx_sc += n;
            if (t->pfx_sc >= per) {
                t->pfx_sc = t->pfx_sc - per < per ? t->pfx_sc - per : 0u;
                pfx_fire(t, rep == PF_BIT(PF_ARP));
            } else if (t->pfx_snn && t->pfx_sc >= per / 2u) {
                pfx_hush(t);                            /* gated half */
            }
        }
        if (f & PFX_DEC) {                              /* the decays, short or long */
            int sh = (on >> PF_DSHT) & 1u;
            uint32_t a = drum ? P_E3 : P_DEC, c = drum ? P_E3 : P_REL;
            pfx_keep[k][0] = t->p[a];
            pfx_keep[k][1] = t->p[c];
            pfx_keep[k][3] = t->p[P_SUS];
            if (drum) {
                t->p[a] = (int16_t)clamp(t->p[a] + (sh ? -40 : 40), 0, 127);
            } else {
                t->p[a] = (int16_t)(sh ? t->p[a] / 3 : t->p[a] + (127 - t->p[a]) * 2 / 3);
                t->p[c] = (int16_t)(sh ? t->p[c] / 3 : t->p[c] + (127 - t->p[c]) * 2 / 3);
                if (sh)
                    t->p[P_SUS] = 0;                    /* (JIANT 0.4) DEC-: a pluck */
            }
        }
        if ((f & PFX_ATK) && !drum) {                   /* (JIANT) ATK+: the attack slow (DRUM: eng_drum.c) */
            pfx_keep[k][2] = t->p[P_ATK];
            t->p[P_ATK] = (int16_t)(t->p[P_ATK] + (127 - t->p[P_ATK]) * 2 / 3);
        }
        if (f || t->pfx_rs || t->pfx_pit)
            pfx_any = 1;
    }
}

/* after the parts: what DEC- / DEC+ changed, back */
static __attribute__((noinline)) void pfx_end(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (trk[k].pfx & PFX_DEC) {
            int drum = trk[k].engine == ENGI_DRUM;
            trk[k].p[drum ? P_E3 : P_DEC] = pfx_keep[k][0];
            trk[k].p[drum ? P_E3 : P_REL] = pfx_keep[k][1];
            if (!drum)
                trk[k].p[P_SUS] = pfx_keep[k][3];
        }
    for (k = 0; k < NTRK; k++)
        if ((trk[k].pfx & PFX_ATK) && trk[k].engine != ENGI_DRUM)
            trk[k].p[P_ATK] = pfx_keep[k][2];
}
