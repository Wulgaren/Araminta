/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DRUM: JIANT's drum engine, DRUM-X (drumx_voice.c, after Sonic Charge's Microtonic): a kit of 8 lanes, KICK, SNARE,
 * CLAP, HAT CL, HAT OP, TOM, RIM, BELL, each a synthesized sound with two patches A and B and a MORPH between them.
 * The kit is the section's (drumx_voice.c dx_kit), edited on EDIT > SOUND. Felucca's DRUM kits (STD and the model
 * kits 80 10 66 55 77, drum_voice.c) were retired in JIANT 0.1: a sound or project that picked one plays DRUM-X.
 * Before Felucca 1.0 this engine was PHYS's MODEL DRUM; those sounds load as it (drum_from_phys in core.h).
 *
 * Keys and notes: the keys play the General MIDI drum map with the kick on the first C (C KICK, C# RIM,
 * D SNARE, D# CLAP, F# HAT CL, A# HAT OP, F G A B C D TOMS, C# CYM, G# BELL; OCT -/+ move it), and a note
 * plays its GM drum on its lane (35..81: kicks, snares, toms, hats, cymbals and bells on BELL, congas on TOM, claves
 * on RIM, each at its pitch from the lane's; other notes as their octave of 36..47), so GM patterns and MIDI parts
 * play as drums.
 *
 * Parameters (stored as before: KIT's slot is MRPH, SNAP's NOIS, KICK's WARP): MRPH the MORPH A..B of every
 * lane; TUNE (+-12 semitones), TONE (COLOR), DECY (DECAY) and NOIS (NOISE) move every lane from its patch (64: as
 * the kit has it); FM (JIANT 0.6.36, once ACC) the FM struck with each hit and gone with its pitch
 * envelope: a click, a beater, a metal edge (drumx_voice.c dx_run); FOLD (JIANT 0.5, was WARP) every sound's oscillator
 * through a wavefolder after its envelope: a bright attack folding back to the plain tail (0: as the kit has it); DRV
 * (JIANT 0.6.16) the kit's sum into a soft clip (x1..x4, level kept), then the knee (drum_bus). Each lane has its LEVEL
 * (P_LN0..P_LN7, MIXER 2/3 and 3/3, #97: square law, 100 % the default; the cowbell's 4 dB under) on the voice
 * amplitude. The track's LFO tunes the kit (DEST PIT) and moves MORPH (DEST SHP). The knobs move a hit while it rings.
 *
 * Polyphony: a lane per drum and part, mono: a hit reuses its lane's voice even at another pitch, the 8 lanes ring
 * together. A sounding voice plays one lane (s[0]); the lane remembers its voice (owner), and a voice
 * whose lane was struck again by another voice, or has rung out (-84 dB), ends itself (drum_amp). The
 * voices come from the shared budget (voice.c), up to 8 per part. A closed hat chokes the open one.
 *
 * What applies (engine_t.oneshot): the hit is the envelope. VOICE (always POLY), GLIDE, the ADSR, ENV DEST,
 * LFO DEST FLT, the global TUNE and the matrix's PITCH CUT SHP do nothing; LEVEL, PAN, the
 * sends, DIST, the SLICER, LFO DEST AMP, velocity, the matrix's AMP and its E1..E8 (the knobs, per
 * block) do. A released key does not end a hit.
 *
 * State: 8 lanes per part in the pool section (drum_kit: each lane's hit). */
#include "drumx_voice.c"

typedef struct {
    dx_voice_t x;                /* the hit */
    uint8_t owner;               /* the voice playing the lane: index + 1, 0 = none */
    int8_t st;                   /* its semitones from the lane's pitch (the GM map) */
    uint8_t mg;                  /* its group mute's fade: DXG_FADE (sounding) .. 0 (muted, silent) */
    uint8_t age;                 /* blocks since the hit (255: and more): PUNCH's shape */
} drum_lane_t;
/* (JIANT 0.5.1) X-MOD, the kit's own MORPH LFO and RAND, is retired (its page went in 3.x; its playback from older
 * projects too, for the flash JIANT's kits took): the track's LFO and the mod matrix move MORPH */

ENG_MEM_FITS("DRUM-X", sizeof(drum_lane_t) * DV_NLANE);
/* 1.4.1: each part's lanes muted (bit l: lane l). SEQ TOOLS F4 mutes the selected lane; the EDIT layer's black keys
 * 1..8 do the same (ui_layer.c EQA_DRUM). Performance state: not saved, not recorded, no undo; a sound or project
 * load clears it */
static uint8_t drum_mute[NPART];

/* The group mutes (JIANT): KICK (the kick), SNARE (snare and clap), HAT (both hats), PERCS (tom, rim, bell), on every
 * DRUM track. A section's (project.c keeps them with its DRUM-X kit, the song stages them); set live on the
 * GLO layer (ui_layer.c). A muted group's new hits do not sound (voice.c trk_note_on); what rings when it is muted
 * fades out (drum_render: DXG_FADE blocks, ~5 ms, no click), then ends */
enum { DXG_KICK = 1, DXG_SNARE = 2, DXG_HAT = 4, DXG_PERC = 8, DXG_ALL = 15 };
static const uint8_t DXG_LANE[DV_NLANE] = {DXG_KICK, DXG_SNARE, DXG_SNARE, DXG_HAT, DXG_HAT, DXG_PERC, DXG_PERC, DXG_PERC};
#define DXG_FADE 8u
/* .. and each sound's own mute (EDIT held + black keys 1..8 on a DRUM track, ui_layer.c): bit 8 + lane */
#define DXM_LANE(l) (1u << (8u + (l)))
#define DXM_ALL 0xFF0Fu
static volatile uint16_t dx_mute;                       /* DXG_* groups | DXM_LANE lanes */
/* MIXER's lane pages (ui_graph.c graph_lanes): each track's lane meters since the UI last read them (Q15, the
 * hit's own envelope x its lane LEVEL: the render loops keep their budget). The audio writes a max, the UI reads
 * and clears its track's; .pool: no audio .bss budget */
static uint16_t drum_lane_meter[NTRK][DV_NLANE] __attribute__((section(".pool")));
static void dx_mute_set(uint32_t m) { dx_mute = (uint16_t)(m & DXM_ALL); }
static int dx_lane_muted_g(uint32_t l) { return (DXG_LANE[l & 7u] & dx_mute) || (dx_mute & DXM_LANE(l & 7u)); }
static int dx_lane_muted(const track_t *t, uint32_t l)
{
    uint32_t p = (uint32_t)(t - trk);
    return dx_lane_muted_g(l) || (p < NPART && ((drum_mute[p] >> (l & 7u)) & 1u));
}

/* The drum bus (JIANT, MENU-less: FX > MASTER). PUNCH (G_PUNCH 0..100, no detector: each hit's own age), made to
 * fatten (JIANT 0.5; it was Drum Buss's +11 dB / -14 dB, which thinned everything out): the whole hit driven into the
 * voice's knee, its first ~3 ms up to x1.45, its body (~15 .. 115 ms) up to x1.7, back to unity by ~180 ms: the peak
 * held by the knee, the body denser and louder, the tail whole (the master's leveler does not pump on a spike). DUCK
 * (G_DUCK, fx.c): a kick struck on any kit's track (its group not muted) ducks the other parts; duck_hit (dsp.c, shared
 * with SAMPLE kits) tells fx.c's mix_block */
#define DRUM_ATK_BLK 48u                                 /* (JIANT 0.4) ATK+ on a DRUM track: a hit's fade-in, blocks */

/* General MIDI notes 35..81 -> the lane (DV_*) and semitones from its pitch */
static const int8_t DRUM_GM[47][2] = {
    {DV_KICK, -2}, {DV_KICK, 0}, {DV_RIM, 0}, {DV_SNARE, 0}, {DV_CLAP, 0}, {DV_SNARE, 2},             /* 35 */
    {DV_TOM, -7}, {DV_HATC, 0}, {DV_TOM, -4}, {DV_HATC, -2}, {DV_TOM, 0}, {DV_HATO, 0},               /* 41 */
    {DV_TOM, 3}, {DV_TOM, 5}, {DV_BELL, 0}, {DV_TOM, 8}, {DV_BELL, -3}, {DV_BELL, 2},                 /* 47 */
    {DV_BELL, 5}, {DV_HATC, 5}, {DV_BELL, 4}, {DV_BELL, 0}, {DV_BELL, 1}, {DV_RIM, -12},              /* 53 */
    {DV_BELL, -2}, {DV_TOM, 10}, {DV_TOM, 7}, {DV_TOM, 5}, {DV_TOM, 5}, {DV_TOM, 0},                  /* 59 */
    {DV_TOM, 10}, {DV_TOM, 7}, {DV_BELL, 7}, {DV_BELL, 3}, {DV_HATC, 3}, {DV_HATC, 7},                /* 65 */
    {DV_RIM, 7}, {DV_RIM, 5}, {DV_HATC, -4}, {DV_HATC, -6}, {DV_RIM, 0}, {DV_RIM, -4},                /* 71 */
    {DV_RIM, -7}, {DV_TOM, 12}, {DV_TOM, 8}, {DV_BELL, 12}, {DV_BELL, 12},                            /* 77 */
};
static uint32_t drum_gm_ix(uint32_t note) { return (note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u) - 35u; }

/* ----------------------------------------------------- the grid's lanes --- */
/* A step's lane hits (step_t.hit / acc, the DRUM grid: SEQ > STEP on a DRUM track) play these GM notes, on any
 * engine: DRUM strikes its lanes, a synth plays the pitches. Each is the designed pitch of
 * its lane (st 0 in DRUM_GM) */
static const uint8_t DRUM_LANE_NOTE[NLANE] = {36, 38, 39, 42, 46, 45, 37, 56};

/* the lane a note strikes */
static uint32_t drum_lane(uint32_t note) { return (uint32_t)DRUM_GM[drum_gm_ix(note)][0]; }

/* a note of a muted group or lane (dx_mute, drum_mute): voice.c trk_note_on drops it */
static int drum_muted_of(const track_t *t, uint32_t note) { return dx_lane_muted(t, drum_lane(note)); }

/* the lane's name (5 characters at most) */
static const char *drum_lane_name(const track_t *t, uint32_t l)
{
    static const char *const N[NLANE] = {"KICK", "SNARE", "CLAP", "HATCL", "HATOP", "TOM", "RIM", "BELL"};
    (void)t;
    return N[l & (NLANE - 1u)];
}
/* .. in two letters, the drum machine way (the grid's lane column) */
static const char *drum_lane_abbr(const track_t *t, uint32_t l)
{
    static const char *const N[NLANE] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"};
    (void)t;
    return N[l & (NLANE - 1u)];
}

/* the lanes step s strikes: its hits, and its notes on their lanes (a NOTE step only) */
static uint32_t step_lanes(const step_t *s)
{
    uint32_t m = 0, i;
    if (s->time != ST_NOTE)
        return 0;
    for (i = 0; i < s->n && i < 4u; i++)
        m |= 1u << drum_lane(step_note(s, i));
    return m | s->hit;
}

/* .. and which of them are accented */
static uint32_t step_accents(const step_t *s) { return step_fl(s) & SF_ACCENT ? step_lanes(s) : s->acc & step_lanes(s); }

/* a step's notes that are a lane's note become that lane's hits (the same note, the same velocity: nothing
 * sounds different). Other notes (a low tom 41, a crash 49) stay notes, shown on their lane. A step accent
 * of a step left with hits only becomes the hits' accents. Pattern loads into a DRUM track, projects of
 * before the grid, the grid's edits */
static void step_to_grid(step_t *s)
{
    uint32_t i, k = 0;
    if (s->time != ST_NOTE)
        return;
    for (i = 0; i < s->n && i < 4u; i++) {
        uint32_t n = step_note(s, i), l = drum_lane(n);
        if (n == DRUM_LANE_NOTE[l])
            s->hit |= (uint8_t)(1u << l);
        else
            step_set_note(s, k++, n);
    }
    for (i = k; i < 4u; i++)
        step_set_note(s, i, 0);
    s->n = (uint8_t)k;
    if (!k && (step_fl(s) & SF_ACCENT)) {
        s->acc |= s->hit;
        step_set_fl(s, step_fl(s) & ~SF_ACCENT);
    }
}

static drum_lane_t *drum_kit_of(const track_t *t)
{
    return t >= &trk[0] && t < &trk[NPART] ? (drum_lane_t *)ENG_MEM(t - trk) : 0;
}

/* the lane voice v plays, 0 when it plays none (any more) */
static drum_lane_t *drum_lane_of(track_t *t, const voice_t *v)
{
    drum_lane_t *K = drum_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v);
    if (!K || i >= NVOICE)
        return 0;
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    return L->owner == i + 1u ? L : 0;
}

/* Different GM pitches on a lane still share its one sounding voice. */
static voice_t *drum_reuse(track_t *t, uint32_t note)
{
    drum_lane_t *K = drum_kit_of(t);
    uint32_t lane = drum_lane(note), owner = K ? K[lane].owner : 0;
    voice_t *v;
    if (!owner || owner > NVOICE)
        return 0;
    v = &t->v[owner - 1u];
    return v->active && (uint32_t)v->s[0] == lane ? v : 0;
}

static void drum_note_on(track_t *t, voice_t *v)
{
    drum_lane_t *K = drum_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v), lane;
    if (!K || i >= NVOICE)
        return;
    lane = drum_lane(v->note);
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    if (L->owner == i + 1u)                              /* this voice played another lane: it stops there */
        L->owner = 0;
    L = &K[lane];
    L->st = (int8_t)(DRUM_GM[drum_gm_ix(v->note)][1] + t->pfx_rpit / 16);   /* (pfx.c RANDOM) */
    L->owner = (uint8_t)(i + 1u);                        /* (its last voice, if another, ends: drum_amp) */
    v->s[0] = (int32_t)lane;
    v->env_out = v->vel * 258;                           /* the hit starts at its level (no ramp from 0) */
    v->env = 1 << 24;                                    /* the ADSR held at full from here, not from drum_amp:
                                                          * a key-off before the first block (zero-length MIDI
                                                          * notes) would end a fresh voice at env 0 in env_tick */
    L->mg = DXG_FADE;
    L->age = 0;
    if (lane == DV_KICK)
        duck_hit = 1;
    L->x.sv = (uint8_t)v->vel;                           /* (JIANT V10.7.8: GR's stroke, drumx_voice.c) */
    dx_trigger(&L->x);
    if (lane == DV_HATC)                                 /* a closed hat chokes the open one */
        dx_choke(&K[DV_HATO].x);
}

/* the voice's amplitude: the hit, not the ADSR (which still runs, held at full so a release never ends
 * it). A voice that plays no lane any more, or whose drum has rung out, ends here */
static int32_t drum_amp(track_t *t, voice_t *v, int32_t adsr)
{
    const drum_lane_t *L = drum_lane_of(t, v);
    (void)adsr;
    if (!v->active)                                      /* (taken for another part: env_tick ended it) */
        return 0;
    if (!L || (!L->x.live && !L->x.trig)) {
        v->active = v->gate = 0;
        v->stage = 0;
        v->env = 0;
        return 0;
    }
    v->env = 1 << 24;
    return 32767;
}

static void drum_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    drum_lane_t *L = drum_lane_of(t, v);
    int32_t y[CTL], pb0 = 4096, pb1 = 4096, lfo, lfo_pit, lfo_shp;
    int32_t lv = clamp(p[P_LN0 + ((uint32_t)v->s[0] & (DV_NLANE - 1u))], 0, 127);
    uint32_t i;
    vmod_t ml;                                           /* (only its amplitude ramp is read: voice_amp) */
    if (!L)
        return;
    if (((uint32_t)v->s[0] & (DV_NLANE - 1u)) == DV_BELL)   /* (JIANT 0.6.49) the cowbell 4 dB under the others: it */
        lv = lv * 100 / 127;                                  /* stood out of every kit (its LEVEL at 100 % is this) */
    if (dx_lane_muted(t, (uint32_t)v->s[0]) || L->mg < DXG_FADE) {   /* its group / lane muted:
                                                          * fading (to the end, if unmuted meanwhile), then ended */
        uint32_t g0 = L->mg;
        if (!g0) {
            L->x.live = L->x.trig = 0;                   /* (drum_amp ends the voice) */
            L->x.hl = 0;                                 /* (the next hit starts its upsampling from 0) */
            return;
        }
        L->mg = (uint8_t)(g0 - 1u);
        ml.amp0 = m->amp0 * (int32_t)g0 / (int32_t)DXG_FADE;
        ml.amp1 = m->amp1 * (int32_t)(g0 - 1u) / (int32_t)DXG_FADE;
        m = &ml;
    }
    if (lv < 127) {                                      /* the lane's LEVEL (square law) on the block's amplitude
                                                          * ramp, not per sample; 100 %: the ramp as it was */
        int32_t gl = lv * lv * 2 + (lv * lv >> 6);       /* Q15, 127: 32508 (not used), 64: 8256 */
        ml.amp0 = (m->amp0 >> 4) * gl >> 11;
        ml.amp1 = (m->amp1 >> 4) * gl >> 11;
        m = &ml;
    }
    if (n > CTL)
        n = CTL;
    lfo = mulq15(t->lfo_val, t->lfo_fade);               /* (JIANT 0.6.81) the track's LFO on the kit: PITCH tunes it,
                                                          * SHAPE moves MORPH */
    lfo_pit = (lfo * p[P_LD_PIT] * 3) >> 15;
    lfo_shp = (lfo * p[P_LD_SHP]) >> 15;
    dx_run(&dx_kit[(uint32_t)v->s[0] & (DV_NLANE - 1u)], &L->x, p[P_E0] + lfo_shp,
           L->st * 16 + (p[P_E1] - 64) * 3 + t->pfx_pit + lfo_pit,
           p[P_E3] - 64, p[P_E2] - 64, p[P_E4] - 64, p[P_E6], p[P_E5], y, n);
    {                                                    /* PUNCH: the boost before the knee (driven into it, as
                                                          * Drum Buss), the tail's cut on the amplitude ramp */
        int32_t pu = pnch_eff;
        if (pu > 0) {
            int32_t g0 = punch_gain(L->age, pu), g1 = punch_gain(L->age + 1u, pu);
            if (g0 < 32768 || g1 < 32768) {              /* (gains < 1: amp stays <= 32767) */
                int32_t a0 = (m->amp0 * (g0 < 32768 ? g0 : 32768)) >> 15;
                ml.amp1 = (m->amp1 * (g1 < 32768 ? g1 : 32768)) >> 15;
                ml.amp0 = a0;
                m = &ml;
            }
            pb0 = g0 > 32768 ? g0 >> 3 : 4096;           /* Q12 */
            pb1 = g1 > 32768 ? g1 >> 3 : 4096;
        }
        if ((t->pfx & PFX_ATK) && L->age < DRUM_ATK_BLK) {   /* (JIANT 0.4) ATK+: the hit faded in (~35 ms), softer */
            int32_t a0 = m->amp0 * (int32_t)L->age / (int32_t)DRUM_ATK_BLK;
            ml.amp1 = m->amp1 * (int32_t)(L->age + 1u) / (int32_t)DRUM_ATK_BLK;
            ml.amp0 = a0;
            m = &ml;
        }
        if (L->age < 255u)
            L->age++;
    }
    if (pb0 != 4096 || pb1 != 4096)                      /* (JIANT 0.6.16: DRV and the knee on the kit's sum, drum_bus) */
        for (i = 0; i < n; i++)
            y[i] = (y[i] * (pb0 + (((pb1 - pb0) * (int32_t)i) >> CTL_LOG2))) >> 12;
    {                                                    /* x1.25; a MODEL's sound x4 more (+12 dB: Felucca's DRUMS
                                                          * voices, their own levels; the lane LEVELs balance them) */
        uint32_t sh = dx_mdl[(uint32_t)v->s[0] & (DV_NLANE - 1u)] ? 3u : 1u;
        for (i = 0; i < n; i++) {
            int32_t s = y[i];
            out[i] += voice_amp(s + (s >> 2), m, i) << sh;
        }
    }
    {   /* MIXER's lane pages: the hit's envelope (Q30, the loudest of its amp, noise and clap tails) as its live
         * level, before the lane's LEVEL (the page draws it up to the LEVEL's tick). An audio-loop peak would
         * exceed the render budget; the envelope follows the sound */
        int32_t e = L->x.ea;
        uint16_t *lm = &drum_lane_meter[(uint32_t)(t - trk)][(uint32_t)v->s[0] & (DV_NLANE - 1u)];
        if (L->x.en > e)
            e = L->x.en;
        if (L->x.ec > e)
            e = L->x.ec;
        e >>= 15;
        if (e > 32767)
            e = 32767;
        if ((uint16_t)e > *lm)
            *lm = (uint16_t)e;
    }
}

/* (JIANT 0.6.16) the kit's sum, once a block (voice.c track_render): DRV x1..x4 into the soft clip, its level kept,
 * and the knee (were each hit's: eight lanes, two curves a sample each; now the bus's, a glue). One mono sum: SPREAD
 * is off on DRUM-X (fx.c) */
static __attribute__((noinline)) void drum_bus(const track_t *t, int32_t *out, uint32_t n)
{
    int32_t drv = t->p[P_E7], g = 0, mk = 0;
    uint32_t i;
    if (drv > 0) {
        g = 4096 + drv * 3 * 4096 / 127;
        mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));
    }
    for (i = 0; i < n; i++) {
        int32_t y = out[i] >> 1;
        if (g)                                           /* (held to +-131071: y * g fits) */
            y = (softclip((clamp(y, -131071, 131071) * g) >> 12) * mk) >> 15;
        out[i] = soft_knee(y, 24000) << 1;
    }
}

/* the GM drum map, the first C (key 7) is the kick (C2, 36) */
static int32_t drum_keys(const track_t *t, uint32_t k)
{
    (void)t;
    return clamp(29 + 12 * song.octave + (int32_t)k, 0, 127);
}

/* {MRPH, TUNE, TONE, DECY, NOIS, FM, FOLD, DRV}; the kit suggests the BEAT pattern (GM notes). One a factory kit
 * (JIANT 0.7.2: drumx_voice.c DX_KITS, this order; browsing loads it, ui.c apply_preset), as JIANT V10.7.24 has them;
 * a few move the whole kit (the LFO FREE: a hit does not restart it). JIANT's DIST amounts (LOFI DUST 8, INDUSTRIAL 16,
 * TECHNO RAW 10, BREAKS 4, HIP HOP 6) are its own track drive, not this fork's DIST stage: left out, as every preset
 * here leaves DIST. LOFI DUST's LFO DRIFT (none here) is S&H smoothed; HIP HOP's BOOM BAP pattern (none here) is BEAT */
static const preset_t DRUM_PRESETS[] = {
    {"FM DRUMS", DRUM_KIT_E, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), PAT(12)},   /* (JIANT's DRUM-X; core.h: SAMPLE
                                                                                     * PERC's too) */
    {"909 CLUB", {60, 64, 78, 64, 64, 0, 0, 10}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 16), PAT(12)},
    {"808 BOOM", {58, 60, 54, 76, 59, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 24), PAT(12)},
    {"LOFI DUST", {70, 64, 46, 54, 78, 0, 0, 20}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 18), PAT(12)},
    {"INDUSTRIAL", {72, 66, 99, 70, 75, 20, 0, 40}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 10, 30), PAT(12)},
    {"ACOUSTIC", {56, 64, 56, 62, 64, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 34), PAT(12)},
    {"DUB ROOTS", {60, 60, 50, 70, 59, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 18, 36), PAT(12)},
    {"TECHNO RAW", {66, 64, 83, 62, 66, 10, 0, 30}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 14), PAT(12)},
    {"BREAKS", {62, 66, 74, 62, 71, 0, 0, 10}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 22), PAT(12)},
    {"HIP HOP", {58, 62, 56, 64, 67, 0, 0, 18}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), PAT(12)},
    {"LATIN PERC", {60, 64, 64, 64, 64, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 22), PAT(12)},
    /* Felucca's DRUMS kits (its engine retired: drums_kit), every knob at its middle (TONE too: the kits as translated) */
    {"STD", {64, 64, 64, 64, 64, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), PAT(12)},
    {"80", {64, 64, 64, 64, 64, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), PAT(12)},
    {"10", {64, 64, 64, 64, 64, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), PAT(12)},
    {"66", {64, 64, 64, 64, 64, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), PAT(12)},
    {"55", {64, 64, 64, 64, 64, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), PAT(12)},
    {"77", {64, 64, 64, 64, 64, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), PAT(12)},
};
/* .. their movement (core.h MODS), DRUM_PRESETS' order; 0 none. DRUMS' kits: their lane LEVELs (MIXER 2/3, 3/3), Felucca's
 * balance (each sound's level against its DRUMS hit, the quietest at 100 %) */
#define LANES(a, b, c, d, e, f, g, h) P_LN0, (a), P_LN1, (b), P_LN2, (c), P_LN3, (d), P_LN4, (e), P_LN5, (f), P_LN6, (g), \
                                      P_LN7, (h)
static const int8_t *const DRUM_MODS[] = {
    0, 0, 0,
    MODS(MLFO(LW_SH, LS_1BAR, 0, 0), P_LSMOOTH, 95, P_LTRIG, 1, P_LD_SHP, 8),   /* LOFI DUST: MORPH wanders, a worn tape */
    MODS(MLFO(LW_SH, LS_16TH, 0, 0), P_LTRIG, 1, MMOD(1, PS_LFO, PD_E(6), 16)),   /* INDUSTRIAL: FOLD at random, 16ths */
    0,
    MODS(MMOD(1, PS_RAND, PD_DLY, 20)),                  /* DUB ROOTS: each hit its own throw into the echo */
    MODS(MLFO(LW_TRI, LS_4BAR, 0, 0), P_LTRIG, 1, MMOD(1, PS_LFO, PD_E(2), 10)),   /* TECHNO RAW: TONE over 4 bars */
    0, 0, 0,
    MODS(LANES(124, 95, 78, 91, 126, 105, 67, 108)),  /* STD */
    MODS(LANES(117, 102, 107, 83, 116, 126, 82, 108)),/* 80 */
    MODS(LANES(111, 79, 52, 90, 82, 118, 102, 106)),  /* 10 */
    MODS(LANES(118, 101, 97, 44, 51, 126, 97, 94)),   /* 66 */
    MODS(LANES(116, 96, 97, 41, 33, 126, 77, 117)),   /* 55 */
    MODS(LANES(116, 76, 83, 54, 49, 118, 65, 85)),    /* 77 */
};
_Static_assert(sizeof DRUM_MODS / sizeof DRUM_MODS[0] == DX_NKIT, "a DRUM preset's movement each");
_Static_assert(sizeof DRUM_PRESETS / sizeof DRUM_PRESETS[0] == DX_NKIT, "a DRUM preset a kit");

static const engine_t ENG_DRUM = {
    .name = "DRUM-X",
    .page_title = {"KIT", "KIT"},
    .edit = {
        {"MRPH", F_PCT, 0, 127, 64, 0, 0},
        {"TUNE", F_PCT, 0, 127, 64, 0, 0},
        {"TONE", F_PCT, 0, 127, 64, 0, 0},
        {"DECY", F_PCT, 0, 127, 64, 0, 0},
        {"NOIS", F_PCT, 0, 127, 64, 0, 0},
        {"FM", F_PCT, 0, 127, 0, 0, 0},                 /* (JIANT 0.6.36: the FM strikes with the hit, once ACC) */
        {"FOLD", F_PCT, 0, 127, 0, 0, 0},              /* (JIANT 0.5: a wavefolder, was WARP) */
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = DRUM_PRESETS,
    .npresets = NELEM(DRUM_PRESETS),
    .note_on = drum_note_on,
    .render = drum_render,
    .amp = drum_amp,
    .knob = {P_E0, P_E1, P_E2, P_E3},
    .poly = DV_NLANE,
    .oneshot = 1,
    .keys = drum_keys,
    .mem = sizeof(drum_lane_t) * DV_NLANE,
};

/* (retired DRUMS, engine 15: project.c proj_retire) the DRUM-X kit Felucca's DRUMS KIT kit plays as (0..8: STD 80 10 66
 * 55 77, the DRUMS presets', and the mix kits DEEP TIGHT PERC, 1.5.6's pieces of the others, assembled lane by lane
 * from theirs; DEEP's kick 2 semitones down, as Felucca's) into k / lay / mdl, its lane LEVELs into ln */
static void drums_kit(uint32_t kit, dx_lane_t *k, uint8_t *lay, uint8_t *mdl, int16_t *ln)
{
    static const uint8_t MIX[3][8] = {   /* the kit each lane comes from: 0 STD 1 80 2 10 3 66 4 55 5 77 */
        {5, 1, 5, 1, 1, 5, 1, 2},        /* DEEP */
        {4, 3, 1, 4, 4, 4, 3, 1},        /* TIGHT */
        {2, 5, 5, 3, 3, 3, 5, 3},        /* PERC */
    };
    uint32_t l;
    kit = kit > 8u ? 0u : kit;
    for (l = 0; l < 8u; l++) {
        uint32_t src = DXK_DRUMS + (kit < 6u ? kit : MIX[kit - 6u][l]);
        const int8_t *m;
        k[l] = DX_KITS[src].lane[l];
        lay[l] = DX_KITS[src].lay[l];
        mdl[l] = DX_KITS[src].mdl[l];
        ln[l] = 127;
        for (m = DRUM_MODS[src]; m && m[0] >= 0; m += 2)
            if (m[0] == (int8_t)(P_LN0 + l))
                ln[l] = m[1];
    }
    if (kit == 6u) {                     /* (2 semitones: 3 steps of 3/4) */
        k[DV_KICK].a[DXP_PITCH] = (uint8_t)(k[DV_KICK].a[DXP_PITCH] - 3);
        k[DV_KICK].b[DXP_PITCH] = (uint8_t)(k[DV_KICK].b[DXP_PITCH] - 3);
    }
}

/* a preset's movement (core.h MODS, params.c preset_mods): DRUM-X's kits' (DRUM_MODS); the other engines' none */
static const int8_t *preset_mod(const engine_t *e, uint32_t pi)
{
    return e == &ENG_DRUM && pi < DX_NKIT ? DRUM_MODS[pi] : 0;
}
