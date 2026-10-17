/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA core types: tracks, voices, engines, parameters.
 * Four tracks, each a synth part: its own engine, preset, parameters, voices and 64-step
 * pattern. The parts share one budget of NVOICE sounding voices (voice.c). Drums are the DRUM
 * engine (General MIDI map) on any part. SLICE is retired (eng_retire.c); SAMPLE PERC still maps
 * to DRUM (drum_from_perc).
 * Sections: sizes, parameters, voices and engines, tracks and the song, system. */
#include <stdint.h>

/* ------------------------------------------------------------ sizes --- */
#define NVOICE 8                 /* voices per part, and the budget shared by all parts */
#define NPART 4                  /* synth parts: tracks 1..4 */
#define NTRK NPART               /* tracks (the formats and the protocol count these): every track is a part */
/* the second core (fx.c mix_block) renders parts at once: C1_NP copies of the state a part's render writes that
 * parts shared before (mod.c, voice.c SPREAD, eng_formant.c, eng_phys.c) */
#define C1_NP NPART
#define C1_PI(t) ((uint32_t)((t) - trk) % C1_NP)
#define NSTEP 64
#define HALF_FRAMES 128          /* I2S half buffer: 2.9 ms at 44.1 kHz (a key waits 0..1 half, then plays 1 half later) */
#ifndef FELUCCA_FM4
#define FELUCCA_FM4 0            /* the DIGITAL engine (eng_digital.c, four-operator FM): kept in the tree, not built
                                  * by default; replaced by FM6, its sounds convert (fm4_convert.c) */
#endif
#ifndef FELUCCA_SLICE
#define FELUCCA_SLICE 0          /* SLICE engine retired (eng_retire.c ENG_SLICE_GONE); keep 0 so sample/grain
                                  * stubs compile without eng_slice.c */
#endif
/* Engine table is append-only. Index 13 SLICE stays reserved (eng_retire.c): never selectable; old stores
 * load as DRUM. SAMPLE (4), GRAIN (8), NOISE (11) are live again. */
#define NENGINES 14u
#define ENGI_DIGITAL 1u          /* reserved without FELUCCA_FM4: never selectable (eng_ok), its sounds load as FM6 */
#define ENGI_SAMPLE 4u
#define ENGI_GRAIN 8u
#define ENGI_NOISE 11u
#define ENGI_SLICE 13u           /* reserved: SLICE retired (eng_retire.c) */
#define NENG_SHOWN (12u + FELUCCA_FM4)   /* pickable: ANALOG FM6 [DIGITAL] PHASE LOFI SAMPLE VOICE TRIO WHEEL GRAIN PHYS NOISE DRUM */
#define UP_SLOTS 32u             /* user presets (upreset.c) */
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS
};

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;   /* F_ENUM */
    const char *unit;           /* F_INT / F_ENUM optional unit */
} param_desc_t;

enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,     /* P_ED_FX: DIST tone, an offset on the drive-linked
                                                * cutoff (0 = follows drive). Kept at this id for the formats */
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    P_AMODE, P_ARATE, P_AOCT, P_AGATE,
    P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_DIST, P_CHOR, P_DLY, P_REV,              /* P_DIST: DIST drive. P_CHOR: EFFECTS DLY/REV/SLCR list slots
                                                * (fx.c; was bus CHO send). P_DLY/P_REV: send or chain mix */
    P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,      /* SLICER (slicer.c): MODE PAT RATE; P_SLDEPTH is MIX */
    P_M1SRC, P_M1DST, P_M1AMT,                 /* modulation matrix (mod.c): 4 slots of SRC, DST, AMT; */
    P_M2SRC, P_M2DST, P_M2AMT,                 /* new common parameters go just before P_E0 (user presets */
    P_M3SRC, P_M3DST, P_M3AMT,                 /* and projects map by count) */
    P_M4SRC, P_M4DST, P_M4AMT,
    P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL, P_FM1_LEVEL,
    P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL, P_FM2_LEVEL,
    P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL, P_FM3_LEVEL,
    P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL, P_FM4_LEVEL,
    P_CHRD, P_VOIC,                            /* chord keys (chord.c): one key plays a chord; its voicing */
    P_LN0, P_LN1, P_LN2, P_LN3, P_LN4, P_LN5, P_LN6, P_LN7,   /* DRUM lane levels (eng_drum.c, #97): KICK .. BELL */
    P_LSYNC, P_LTRIG, P_LPOL,                  /* 1.2 (#154): LFO SYNC (OFF, a note value), TRIG (NOTE, FREE), POL (BI, UNI) */
    P_SQNT,                                    /* 1.2 (#162): QUANTIZE, the steps' recorded / nudged timing (ON: none) */
    P_SPRD,                                    /* (#148): SPREAD, the voices of a POLY / UNISON track around PAN (fx.c) */
    /* insert chain (6 stages, reorderable; MIX 0 = bypass). Replaces 1.5's P_ITYPE..P_IMIX. Drive = P_DIST. */
    P_IORD,                                    /* low 10 bits: stage order 0..719 (fx.c). Bits 10..15:
                                                * that stage is a send, not in the chain */
    P_IDMODE, P_IDMIX,                         /* DIST: mode SOFT/HARD/FOLD/FUZZ, mix.
                                                * Tone is P_ED_FX: 0 follows drive, else an offset */
    P_IFTYPE, P_IFCUT, P_IFRES, P_IFMIX,       /* FILTER: LP/HP/BP/COMB */
    P_ICBITS, P_ICRATE, P_ICLPF, P_ICMIX,      /* CRUSH */
    P_IPRATE, P_IPDEP, P_IPFBK, P_IPMIX,       /* PHASR */
    P_ILRATE, P_ILDEP, P_ILMIX,                /* FLANG (fixed feedback) */
    P_IHRATE, P_IHDEP, P_IHMIX,                /* CHOR (fixed feedback) */
    P_FTYPE,                                   /* ANALOG's filter TYPE: LP / BP / HP / COMB (eng_analog.c) */
    P_ESYNC,                                   /* 1.5 (#175): ENV SYNC: ATK DEC REL as note values of the tempo (voice.c) */
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_COUNT
};
/* P_MUTE (params.c N_PLAY). Append-only: stored 0 plays (PLAY ON), 1 is silent (PLAY OFF). */
enum { PLAY_ON = 0, PLAY_OFF = 1, PLAY_SOLO = 2 };
/* 1.5 INSERT ids (np 111): migrated to the chain in project.c / upreset.c ins_migrate */
enum { P_ITYPE_LEG = 96, P_IA_LEG = 97, P_IB_LEG = 98, P_IC_LEG = 99, P_IMIX_LEG = 100 };
enum { INS_N = 6, INS_DIST = 0, INS_FILTER = 1, INS_CRUSH = 2, INS_PHASR = 3, INS_FLANG = 4, INS_CHOR = 5,
       INS_DLY = 6, INS_REV = 7, INS_SLCR = 8, FX_LIST_N = 9 }; /* DLY/REV/SLCR: shared buses; list with inserts */
/* Default P_IORD permutation (Lehmer): DIST, CRUSH, FILTER, PHASR, FLANG, CHOR. Stage ids stay
 * FILTER=1 / CRUSH=2 so send flags and saved orders keep their meaning; only the factory order moved. */
#define IORD_DEF 24u
/* EFFECTS DLY/REV list slots in P_CHOR (fx.c). SLCR slot is track.fx_sl_pos (FUN14 wire). */
#define FX_POS_DEF (64 + 6 + (7 << 3))           /* DLY at 6, REV at 7; SLCR defaults to 8 */
/* SEND bus knobs (no MIX): packed into reclaimed FUN13 bytes (SCALE LOCK stash + X-MOD). */
#define FX_BUS_N 19u
static const uint8_t FX_BUS_PID[FX_BUS_N] = {
    P_IDMODE, P_DIST, P_ED_FX,
    P_IFTYPE, P_IFCUT, P_IFRES,
    P_ICBITS, P_ICRATE, P_ICLPF,
    P_IPRATE, P_IPDEP, P_IPFBK,
    P_ILRATE, P_ILDEP,
    P_IHRATE, P_IHDEP,
    P_SLCR, P_SLPAT, P_SLRATE
};
enum { IDM_SOFT, IDM_HARD, IDM_FOLD, IDM_FUZZ, IDM_N };
enum { IFT_LP, IFT_HP, IFT_BP, IFT_COMB, IFT_N };

enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK, G_TUNE,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX, /* G_DMIX is reserved and inert for old projects. */
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,       /* G_CRATE/G_CDEPTH: was bus chorus; now DLY/REV chain owner
                                                * (0 = SEND for all, 1..4 = track). Old CRT/CDP values > 4 load as 0. */
    G_MIDI, G_DREL, G_ROUTE, G_INFO,   /* G_DREL (JIANT): DUCK's release; was G_SYNC, a placeholder.
                                        * G_ROUTE: MIDI IN, 0 CH1-4 (channels 1..4 -> parts 1..4, 5..16 ignored), 1 SEL;
                                        * 1.2: 2..4 CH5-8 CH9-12 CH13-16 (seq.c midi_base) */
    G_SLOT, G_NAME, G_LOAD, G_SAVE,
    G_ENGSEL, G_CLIP,           /* no page for ENGSEL: the editor switches the engine with a SET of G_ENGSEL.
                                 * G_CLIP (JIANT): master soft-clip amount on FX > MASTER */
    G_CLRSEQ, G_INITSND,
    G_RTYPE,                    /* REVERB TYPE (SEND tank): 0 ROOM, 1 SPRING, 2 HALL (1.2 power-on), 3 SHIMR,
                                 * 4 CLOUD (JIANT) (fx.c). Was G_DRCH, the GM drum part's MIDI channel (inert since
                                 * 1.0, never read); projects of formats before FUN7 load it as ROOM
                                 * (project.c proj_rtype_room) */
    G_PUNCH, G_DUCK,            /* (JIANT) drum bus PNCH and kick DUCK of the synths (fx.c, eng_drum.c).
                                 * Was G_DRLVL / G_DRREV (GM drum part); old projects still read them in
                                 * proj_drums_to_part, which clears them after */
    G_CRTYPE, G_CRSIZE, G_CRDAMP, /* REVERB CHAIN tank (separate from SEND G_R*). Wire: FUN11 spare bytes
                                   * (project.c); older projects copy SEND on load. */
    G_SLOWN,                      /* EFFECTS SLICER: bits 0..3 = per-track SEND (1) / CHAIN (0).
                                   * Legacy 0 = all SEND, 1..4 = one CHAIN owner (others USED); sanitized to a mask. */
    G_COUNT
};
#define G_SYNC G_DREL                   /* (old names: formats' importers and their tests) */
#define G_ENGGO G_CLIP
#define G_DRLVL G_PUNCH
#define G_DRREV G_DUCK
#define G_DLYOWN G_CRATE                /* EFFECTS: who has delay as CHAIN (0 = SEND) */
#define G_REVOWN G_CDEPTH               /* EFFECTS: who has reverb as CHAIN (0 = SEND) */

/* stored parameters of an older layout -> today's P_* order. A store keeps np = the P_COUNT it was
 * written with; common parameters are only ever added just before P_E0, so the first np - 8 are
 * P_LEVEL.. in order and the last 8 are P_E0..P_E7; the parameters a store does not have take def[]
 * (user presets, projects of formats 1 and 2) */
static void params_by_count(int16_t *out, const int16_t *in, uint32_t np, const int16_t *def)
{
    uint32_t i, nc = np - 8u;
    for (i = 0; i < P_E0; i++)
        out[i] = i < nc ? in[i] : def[i];
    for (i = 0; i < 8u; i++)
        out[P_E0 + i] = in[nc + i];
}

/* 1.5 (np 111): INSERT at 96..100 and FTYPE/ESYNC at 101..102 land mid-chain after params_by_count
 * (P_E0 moved). Rebuild the chain and park TYPE/ESYNC at today's ids. np > 111: no-op. */
static void ins_migrate(int16_t *p, uint32_t np)
{
    int16_t ty = 0, a = 64, b = 96, c = 96, mix = 127, ft = 0, es = 0, dist = p[P_DIST];
    uint32_t i;
    if (np > 111u)
        return;
    if (np == 111u) {
        ty = p[96];
        a = p[97];
        b = p[98];
        c = p[99];
        mix = p[100];
        ft = p[101];
        es = p[102];
    }
    for (i = P_IORD; i < P_FTYPE; i++)
        p[i] = 0;
    p[P_IORD] = (int16_t)IORD_DEF;
    p[P_CHOR] = (int16_t)FX_POS_DEF;           /* EFFECTS: DLY at 6, REV at 7 */
    p[P_IDMODE] = IDM_SOFT;
    p[P_IFTYPE] = IFT_LP;
    p[P_IFCUT] = 96;
    p[P_IFRES] = 40;
    p[P_ICBITS] = 64;
    p[P_ICRATE] = 96;
    p[P_ICLPF] = 96;
    p[P_IPRATE] = p[P_ILRATE] = p[P_IHRATE] = 64;
    p[P_IPDEP] = p[P_ILDEP] = p[P_IHDEP] = 96;
    p[P_IPFBK] = 96;
    p[P_FTYPE] = ft;
    p[P_ESYNC] = es;
    if (dist)
        p[P_IDMIX] = 127;
    if (np < 111u || ty <= 0)
        return;
    if (ty >= 1 && ty <= 4) {
        int32_t base, off;
        p[P_IDMODE] = (int16_t)(ty - 1);
        p[P_DIST] = a;
        p[P_IDMIX] = mix;
        /* 1.5 INSERT: P_IB was the distortion's TONE (a cutoff). The chain's cutoff follows drive;
         * the difference is P_ED_FX so the old tone comes back as closely as ±64 allows */
        base = 96 - ((a < 0 ? 0 : a > 127 ? 127 : a) >> 2);
        off = (int32_t)b - base;
        p[P_ED_FX] = (int16_t)(off < -64 ? -64 : off > 63 ? 63 : off);
    } else if (ty == 5) {
        p[P_ICBITS] = a;
        p[P_ICRATE] = b;
        p[P_ICLPF] = c;
        p[P_ICMIX] = mix;
    } else if (ty == 6) {
        p[P_IPRATE] = a;
        p[P_IPDEP] = b;
        p[P_IPFBK] = c;
        p[P_IPMIX] = mix;
    } else if (ty == 7) {
        p[P_ILRATE] = a;
        p[P_ILDEP] = b;
        p[P_ILMIX] = mix;
    } else if (ty == 8) {
        p[P_IHRATE] = a;
        p[P_IHDEP] = b;
        p[P_IHMIX] = mix;
    }
}

/* engine indices the stores name (engines.c ENGINES[]: append-only) */
#define ENGI_PHYS 9u
#define ENGI_DRUM 10u
/* PHYS MODEL DRUM (MODEL 4, before 1.0) -> the DRUM engine, its E values in place: {MODEL, TUNE, TONE, DECY,
 * SNAP, ACC, KICK 0..127, PERC 0..127} -> {MRPH, TUNE, TONE, DECY, NOIS, FM, FOLD, DRV} with the old PERC/KICK
 * packed into the first slots so stored projects stay finite (DRUM-X plays them as MRPH / FOLD). 1 = it was one
 * (its engine is ENGI_DRUM now); projects (project.c) and user presets (upreset.c) */
static int drum_from_phys(uint32_t engine, int16_t *e)
{
    if (engine != ENGI_PHYS || e[0] != 4)
        return 0;
    e[0] = (int16_t)((e[7] < 0 ? 0 : e[7] > 127 ? 127 : e[7]) >> 5);   /* PERC -> MRPH (was KIT index) */
    e[6] = (int16_t)(e[6] >= 64);                                       /* KICK -> FOLD on/off (was PUNCH/ROUND) */
    e[7] = 0;
    return 1;
}

/* SAMPLE SET 4 was PERC, the GM kit (tools/gen_samples.py); retired after 1.0.2: its index stays
 * (SET / GRAIN SRC 4 is an alias of PIANO). A sound that selected it is the DRUM engine with
 * its default kit (same GM map). projects (proj_perc), user presets (up_migrate), favourites
 * (settings_persist.c). Idempotent. */
#define SMP_SET_PERC 4u
#define DRUM_KIT_E {64, 64, 70, 64, 64, 0, 0, 0}   /* {MRPH, TUNE, TONE, DECY, NOIS, FM, FOLD, DRV} (DRUM-X) */
static int drum_from_perc(uint32_t engine, int16_t *e)
{
    static const int16_t KIT[8] = DRUM_KIT_E;
    uint32_t i;
    if (engine != ENGI_SAMPLE || e[0] != (int16_t)SMP_SET_PERC)
        return 0;
    for (i = 0; i < 8u; i++)
        e[i] = KIT[i];
    return 1;
}

/* ------------------------------------------------- voices, engines --- */
enum { V_POLY, V_MONO, V_LEGATO, V_UNISON };   /* P_VOICE */
typedef struct {
    uint8_t note, vel, gate, active;
    uint8_t stage;               /* env: 0 off, 1 attack, 2 decay/sustain, 3 release */
    uint8_t side;                /* SPREAD (fx.c mix_spread): 0 left of PAN, 1 right; set as the voice starts */
    int32_t env;                 /* Q24 */
    int32_t env_out;             /* last control-rate amplitude, Q15 */
    int32_t pitch16, pitch_cur;  /* 1/16 semitone, with glide */
    int32_t gstep;               /* glide TIME mode: 1/16 st per control tick, 0 = RATE mode */
    int32_t fine;                /* unison detune: phase increment * (1 + fine / 4096) */
    uint32_t ph[3];
    int32_t s[8];                /* engine state (filters, envs) */
    uint32_t age;
    uint8_t mvel;                /* mod.c: the note-on velocity (before UNISON scaling) and RAND of the note */
    int16_t mrnd;
} voice_t;

typedef struct {                 /* per-voice control-rate modulation, computed in voice.c */
    uint32_t inc;                /* phase increment of the base pitch */
    int32_t pitch16;
    int32_t amp0, amp1;          /* Q15 ramp over the block */
    int32_t cutoff;              /* 0..127 << 8 */
    int32_t shape;               /* 0..127 << 8 */
    int32_t envq15;              /* env value (for engines that use it as a mod source) */
    int32_t fine;                /* the residual below 1/16 semitone in inc: unison detune, TUNE, bend (1/4096) */
} vmod_t;

typedef struct {
    const char *name;
    int8_t e[8];                 /* P_E0..P_E7 (signed: an interval below the note; every value fits) */
    uint8_t env[4];              /* ATK DEC SUS REL */
    int8_t fenv;                 /* ENV -> FILTER amount (-64..63) */
    uint8_t mono;                /* 1 = MONO (bass / lead), 0 = POLY */
    /* the rest of the patch; each value is stored + 1, 0 = the default */
    uint8_t fx[4];               /* DIST, CHORUS, DELAY, REVERB sends */
    uint8_t pat;                 /* suggested pattern (PATTERNS[pat - 1], 0 = none): only a hint. A sound load
                                  * never touches the steps; SEQ > PATTERNS loads a pattern (ui.c pat_load). The
                                  * arp is the track's too: a preset does not set it */
} preset_t;
#define FX(d, c, dl, r) .fx = {(d) + 1, (c) + 1, (dl) + 1, (r) + 1}
#define PAT(n) .pat = (n)

struct track;
typedef struct {                 /* an engine (engines.c ENGINES[]; the eng_*.c files) */
    const char *name;            /* "ANALOG" (PRESETS, the editor) */
    const char *page_title[2];   /* EDIT 1 and EDIT 2 */
    param_desc_t edit[8];        /* P_E0..P_E7 */
    const preset_t *presets;
    uint8_t npresets;
    uint8_t knob[4];             /* HOME: the four parameters on KNOB 1..4 */
    uint8_t poly;                /* voice cap for POLY and UNISON, 0 = NVOICE */
    uint8_t sampled;             /* 1 = plays recorded material (a position, not a phase): voice.c keeps
                                  * no phases over a retrigger, spreads none for UNISON, renders at SUS 0 */
    uint8_t keep;                /* bit k: s[k] is kept when a sounding voice is retriggered (filters) */
    uint8_t oneshot;             /* 1 = hits (DRUM): always POLY, no glide; amp ends its voices (eng_drum.c) */
    void (*note_on)(struct track *t, voice_t *v);
    void (*render)(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m);
    /* optional (0 = none): the voice amplitude instead of the ADSR curve, once per control tick;
     * gets the ADSR value (Q15, env_tick already ran: it still gates the voice), returns Q15 */
    int32_t (*amp)(struct track *t, voice_t *v, int32_t adsr);
    /* optional: a mode-dependent descriptor of EDIT k (the same range and default as edit[k],
     * another label / value names), 0 = edit[k] */
    const param_desc_t *(*desc)(const struct track *t, uint32_t k);
    /* optional: once per block and part, before its voices (also with no voice sounding) */
    void (*block)(struct track *t);
    /* optional: the note of key k (0 = the lowest) when the engine maps the keys itself, else -1
     * (then the scale keyboard of seq.c kb_map) */
    int32_t (*keys)(const struct track *t, uint32_t k);
    /* 1 = the engine's own envelopes are the voice's amplitude (FM6): voice.c renders it at 1.0 (no ADSR, no
     * velocity; LFO -> AMP, the matrix's AMP and the fades still apply; ENV is 0) and the voice ends when
     * done() says so (once per control tick, before the render), not at the end of the ADSR's release */
    uint8_t ownenv;
    int (*done)(struct track *t, voice_t *v);
    /* bytes of the part's engine memory (engines.c eng_mem, at most ENG_MEM_PART) the engine keeps its state
     * in, 0 = none: a part plays one engine at a time, so they share one region; on a switch to the engine
     * (after the old one's fade) the region is zeroed when another engine had it (eng_mem_claim) */
    uint16_t mem;
} engine_t;

/* ------------------------------------------------- tracks, the song --- */
enum { ST_NOTE, ST_TIE, ST_REST };
#define SF_ACCENT 1u
#define SF_SLIDE 2u
#define SF_RATCH_SH 3u                   /* RATCH: the hits of a NOTE step - 1 (0..3: x1..x4, seq.c seq_ratchet); */
#define SF_RATCH (3u << SF_RATCH_SH)     /* bits 3..4, so a user preset's pattern (flag 4 = tie) carries it too */
#define NLANE 8                  /* drum lanes of a step (the DRUM engine's: eng_drum.c DRUM_LANE_NOTE) */
typedef struct {                 /* acid-style step: up to 4 notes (POLY), time, accent, slide; drum hits */
    uint8_t note[4];
    uint8_t n;                   /* notes in use, 0 = empty */
    uint8_t time;                /* ST_NOTE / ST_TIE / ST_REST */
    uint8_t flags;               /* SF_ACCENT | SF_SLIDE | SF_RATCH */
    uint8_t vel;
    uint8_t hit;                 /* bit l: lane l hits (its GM note, on any engine): the DRUM grid */
    uint8_t acc;                 /* bit l: that hit is accented (velocity 127) */
    uint8_t probability;         /* 0 = legacy 100%; 1..100 = percent, 101 = silent */
} step_t;
typedef struct {                 /* a step as formats 1..4 (projects to FUN4) stored it: no hits */
    uint8_t note[4];
    uint8_t n, time, flags, vel;
} step8_t;

/* Probability keeps zero-initialized and legacy patterns at 100%. */
static uint32_t step_chance(const step_t *s) { return !s->probability ? 100u : s->probability <= 100u ? s->probability : 0u; }
static void step_set_chance(step_t *s, uint32_t chance) { s->probability = (uint8_t)(chance >= 100u ? 0u : chance ? chance : 101u); }
/* RATCH: a NOTE step plays its notes and hits this many times (1..4), in equal parts of the step; every older
 * pattern holds 0 there, x1 */
static uint32_t step_ratchet(const step_t *s) { return ((s->flags & SF_RATCH) >> SF_RATCH_SH) + 1u; }
static void step_set_ratchet(step_t *s, uint32_t hits)
{
    s->flags = (uint8_t)((s->flags & ~SF_RATCH) | ((hits < 1u ? 0u : hits > 4u ? 3u : hits - 1u) << SF_RATCH_SH));
}
/* NUDGE (1.2, #162, D98): a step's timing, in 1/NUDGE_DIV of the track's DIV, -8..+7 (half a step early .. 7/16 late);
 * live recording keeps it, SEQ > AUTOMATION's NUDGE rows and DETAIL's NUDGE set it, the track's QUANTIZE
 * OFF (P_SQNT, AUTOMATION's QUANTIZE row) plays it (seq.c seq_tick). On the drum grid, steps held + PRESETS moves
 * those steps in time; on the piano roll, PRESETS moves the note under the cursor (its ties with it).
 * Four bits of the flags no other field uses: 4 (bit 3 of the nudge, its sign) and 32 64 128 (bits 0..2). 0 = on the
 * step: every older pattern. A user preset's pattern keeps none (upreset.c up_pat_norm) */
#define NUDGE_DIV 16
#define SF_NUDGE (4u | 0xE0u)
static int32_t step_nudge(const step_t *s)
{
    uint32_t u = (uint32_t)(s->flags >> 5) | (uint32_t)(s->flags & 4u) << 1;
    return (int32_t)(u ^ 8u) - 8;
}
static void step_set_nudge(step_t *s, int32_t v)
{
    uint32_t u = (uint32_t)(v < -8 ? -8 : v > 7 ? 7 : v) & 15u;
    s->flags = (uint8_t)((s->flags & ~SF_NUDGE) | (u & 7u) << 5 | (u >> 1 & 4u));
}
#define MOTION_MAX 128u          /* RAM / old FUN10 (np<=111): 128 records. New saves (np==P_COUNT): see MOTION_FILE_MAX */
#define MOTION_FILE_MAX_V10 110u /* FUN10 disk: 4+110*3=334; +2 B/track for P_IORD left 2 B before FM6 */
#define MOTION_FILE_MAX_V11 78u  /* FUN11 disk: 4+78*3=238; frees 96 B for the DRUM-X kit (project.c PROJ_DX_*) */
#define MOTION_FILE_MAX 72u      /* FUN12 disk: 4+72*3=220; 16 B for SCALE LOCK + stash before CRV */
/* Four tracks x64 steps fit one byte. Track values are -64..127; FX TIME retains exact milliseconds in RAM.
 * param: the P_* or MFX_* id (<128), bit 7 (MOTION_LOCK, 1.1) a parameter lock: the value
 * sounds on that step only and goes back after it (motion.c motion_step); without it an automation event, the
 * value holds until another one changes it. One record per (track, step, id), of either kind */
#define MOTION_LOCK 0x80u
/* Motion-only targets reuse ids excluded from track automation. They never index p[]. */
enum { MFX_TIME = P_M1SRC, MFX_FB, MFX_COLOR, MFX_SIZE, MFX_DAMP };
#define MFX_PARAM(id) ((id) >= MFX_TIME && (id) <= MFX_DAMP)
#define MOTION_ID(e) ((uint32_t)(e)->param & 0x7Fu)
typedef struct { uint8_t place, param; int16_t value; } motion_event_t;
typedef struct { uint8_t place, param, value; } motion_file_event_t;
typedef struct { uint8_t count, on, rsv[2]; motion_event_t event[MOTION_MAX]; } motion_store_t;
_Static_assert(sizeof(motion_store_t) == 516u, "motion RAM layout (128 records, exact FX values)");
_Static_assert(sizeof(motion_file_event_t) == 3u, "motion disk record preserves capacity");
#define MOTION_FILE_SIZE_V10 (4u + MOTION_FILE_MAX_V10 * sizeof(motion_file_event_t))
#define MOTION_FILE_SIZE_V11 (4u + MOTION_FILE_MAX_V11 * sizeof(motion_file_event_t))
#define MOTION_FILE_SIZE (4u + MOTION_FILE_MAX * sizeof(motion_file_event_t))
_Static_assert(MOTION_FILE_SIZE_V10 == 334u, "FUN10 motion disk layout");
_Static_assert(MOTION_FILE_SIZE_V11 == 238u, "FUN11 motion disk layout (kit before FM6)");
_Static_assert(MOTION_FILE_SIZE == 220u, "FUN12 motion disk layout (SCALE LOCK before CRV)");
enum { SCL_LOCK_OFF = 0, SCL_LOCK_SCALE = 1 };   /* track_t.scl_lock / CHORD KNOB 3 (params.c PG_SLOCK) */
#define CHAIN_ROWS 16u
/* A song section (1.2, FUN10): the PROJECT slot each track plays (0..3, A..D; CHAIN_SILENT "-": that track plays
 * nothing there) and its repeats (1..16). Up to 1.1 a row was {slot, repeat}, all four tracks on that slot
 * (chain_v9_t, FUN6..FUN9): it loads as a section with the four on it (song_chain.c chain_from_v9) */
#define CHAIN_SILENT 4u
typedef struct { uint8_t slot[NTRK], repeat; } chain_row_t;
typedef struct {
    uint8_t count, rsv[3];
    chain_row_t row[CHAIN_ROWS];
} chain_config_t;
typedef struct { uint8_t slot, repeat; } chain_row_v9_t;   /* frozen: the rows of FUN6..FUN9 */
typedef struct {
    uint8_t count, rsv[3];
    chain_row_v9_t row[CHAIN_ROWS];
} chain_v9_t;
_Static_assert(sizeof(chain_config_t) == 84u && sizeof(chain_v9_t) == 36u, "song chain layouts (FUN10 / FUN6..FUN9)");

/* GENERATION's per-track controls; LEN uses the existing sequencer P_SLEN. */
typedef struct {
    uint8_t density[3];          /* KICK / SNARE / HAT percentages, 0..100 */
    uint8_t x, y;                /* original Grids map coordinates, 0..255 */
    uint8_t chaos;               /* variation percentage, 0..100 */
} generation_t;
_Static_assert(sizeof(generation_t) == 6u, "GENERATION controls occupy six bytes");
static const generation_t GENERATION_DEFAULT = {{50, 50, 50}, 128, 128, 0};

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t engine, preset;      /* engine: what the audio ISR renders */
    uint8_t eng_req;             /* engine the UI asked for (the ISR switches at a block start) */
    uint8_t user;                /* user preset slot + 1 the sound came from (UI), 0 = none */
    voice_t v[NVOICE];
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;             /* Q15 */
    int32_t lfo_fade;            /* Q15 ramp after note-on */
    uint32_t lfo_rnd;
    int16_t lfo_prev;            /* the S&H value of the cycle before (mod.c SLEW glides from it to lfo_rnd's) */
    /* keyboard / arp input: held notes in press order */
    uint8_t held[16];
    uint8_t nheld;
    uint8_t arp_phys;            /* keys physically held for the arp */
    uint8_t latched;             /* HOLD: keep notes after release */
    /* arp runtime */
    uint32_t arp_pos;            /* q8 samples into the current arp step */
    uint32_t arp_idx;
    uint8_t arp_note;            /* sounding arp note, 0 = none */
    uint8_t arp_walk;            /* WALK: the place in the note list it stands on */
    uint8_t arp_ch[15];          /* CHORD / DRONE: other notes with arp_note (up to 16), 0 = none */
    uint32_t arp_off;            /* q8 sample time of its note-off */
    /* sequencer */
    step_t step[NSTEP];
    uint32_t seq_pos;            /* q8 samples into the current step */
    uint16_t seq_idx;
    uint8_t seq_notes[4 + NLANE];   /* sounding seq notes (the step's notes, then its hits) */
    uint8_t seq_n;
    uint8_t seq_hold;            /* last step slides: keep the notes until the next step */
    uint8_t slide_glide;         /* next legato note glides (slide) */
    uint8_t rat_left;            /* RATCH: repeats of the playing step still to come (seq.c seq_ratchet) */
    uint32_t seq_off;
    uint8_t rskip_idx;           /* live recording put notes into the step about to play: */
    uint8_t rskip_n, rskip[NLANE];   /* do not trigger them again there (they sound already) */
    /* NUDGE (QUANTIZE OFF, seq.c seq_tick): a step plays at its start + its nudge. Latched as a step starts: */
    uint32_t nd_late;            /* the step playing now plays at this seq_pos + 1 (0: it played) */
    uint32_t nd_early;           /* the next step plays early, at this seq_pos + 1 of this one (0: on its start) */
    uint8_t nd_fired;            /* that step + 1 once it played early (its start then plays nothing), 0 = none */
    /* live recording of held notes (seq.c rec_hold): the steps they are held into become TIEs */
    uint8_t rh_n, rh_note[4];    /* recorded notes still held, 0 = none */
    uint8_t rh_start;            /* the step they were recorded into */
    uint8_t rh_ties;             /* TIE steps written after it */
    uint8_t rh_last;             /* the last of them; rh_bak: what it held (an early release puts it back) */
    step_t rh_bak;
    /* mono */
    uint8_t mono_stack[8];
    uint8_t nmono;
    uint8_t mono_note;           /* note the MONO / LEGATO / UNISON voice(s) play, 0 = none */
    uint8_t rr;                  /* POLY ROTATE: next voice to try */
    /* mix runtime */
    int32_t peak;
    int32_t dist_hp, dist_lp1, dist_lp2;   /* DIST insert state (fx.c) */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST / INSERT tail) */
    uint8_t ins_run;             /* the INSERT still sounds (fx.c track_insert: its wet share above 0) */
    /* punch-in MIDI (pfx.c): ISR only, never saved */
    uint8_t pfx;                 /* PFX_* */
    int16_t pfx_pit;             /* OCT- / OCT+: 1/16 semitones on every voice */
    int16_t pfx_rpit;            /* RANDOM: the next note's offset (1/16 semitones) */
    uint8_t pfx_half;            /* 1/2 TEMPO: the odd sample carried; pfx_rs: let go, resync */
    uint8_t pfx_rs;
    uint16_t pfx_idx0;           /* where 1/2 TEMPO was pressed (step, samples into it) */
    uint32_t pfx_pos0, pfx_el;
    uint8_t pfx_ln[4], pfx_lv[4], pfx_lnn;   /* last notes struck together (STUTTER / ARP) */
    uint32_t pfx_lblk;           /* block they were struck in */
    uint8_t pfx_sn[4], pfx_snn;  /* notes a repeat holds now */
    uint8_t pfx_si;              /* ARP: the next of them */
    uint32_t pfx_sc;             /* samples since the last repeat */
    /* SCALE LOCK (CHORD KNOB 3): not a P_* (P_COUNT is at the 127 ceiling). Default SCALE. */
    int16_t scl_lock;            /* SCL_LOCK_OFF / SCL_LOCK_SCALE */
    int16_t scl_stash[3];        /* ROOT / SCL / MAP restored on OFF (project.c FUN12+) */
    uint8_t scl_stash_ok;        /* 1: scl_stash holds values from before a lock overwrite */
    uint8_t fx_sl_pos;           /* EFFECTS INS_SLCR list slot 0..8; RAM until FUN14 (default 8) */
    generation_t generation;
    uint8_t rsv_sz[3];           /* sizeof(track_t) a multiple of 16 */
    int16_t armp, aholdp;        /* P_AMODE / P_AHOLD as last seen by the ISR */
    int16_t ahold_sv;            /* HOLD before DRONE forced it on (restored on leave) */
    /* engine switch (voice.c engine_block): the old engine's voices fade out, then it switches */
    uint8_t xf_on, xf;           /* fading; blocks of the fade still to render */
    int16_t pe_old[8];           /* P_E0..P_E7 of the sounding engine: the fade renders with these */
    uint8_t xp_n, xp_note[4], xp_vel[4];   /* note-ons during the fade, played on the new engine */
    /* modulation matrix (mod.c): MIDI performance controllers of the track's channel, 0..127 */
    uint8_t mw, at;              /* CC1 mod wheel, channel aftertouch */
    uint8_t ex_off;              /* 127 - CC11 expression (0: full, the MIDI default) */
    uint8_t m_vel, m_key, m_vi;  /* the latest note-on: velocity, note, voice index (per-block destinations) */
    uint8_t sp_alt;              /* SPREAD: the side the next voice starts on (alternating, voice.c voice_start) */
    int16_t m_rnd;               /* .. its RAND */
    int32_t m_env;               /* the amp envelope of voice m_vi, last block (Q15) */
    /* the second core (fx.c mix_block): what a part's render keeps of its own (was shared scratch, one part at a time) */
    uint32_t esync_k[3];         /* ATK's step, DEC's and REL's coefficient (Q16; voice.c esync_coef / env_tick) */
    uint8_t spread_on;           /* SPREAD while this part renders (voice.c / fx.c mix_part) */
    uint8_t lfo_wrap;            /* the LFO's phase wrapped this block (track_lfo_adv) */
    uint8_t rsv_c1[2];           /* sizeof(track_t) a multiple of 16 */
} track_t;
enum { PFX_RND = 1, PFX_HALF = 2, PFX_REP = 4, PFX_DEC = 8, PFX_ATK = 16 };   /* (track_t.pfx) */
/* the audio ISR indexes trk[] with a multiply by a small immediate and a shift while the size is a multiple of 16; 1.5's
 * new track parameters (the INSERT, the filter TYPE, ENV SYNC) and state would make it no multiple (the constant
 * loaded first: +3 % on the ISR's static cost, target_budget.py): one pad, rsv_sz, keeps it */
_Static_assert(sizeof(track_t) % 16u == 0, "track_t: a multiple of 16 bytes (a cheap trk[] index on the target)");

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t grid;                /* 1: the keys are the DRUM grid (ui.c grid_on): only the lane keys play (seq.c);
                                  * 2: NAME (ui_name.c): no key plays; 3: SONG, A..D are its (seq.c song_key) */
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t sel;                 /* selected track 0..NTRK-1: keys, pages, editor */
    int8_t octave;
    uint32_t tick;               /* sub-blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
} song_t;

static track_t trk[NTRK];        /* the instrument: four parts */
static song_t song;
#define TSEL (&trk[song.sel])    /* the selected track */
static track_t fx_bus_trk;       /* SEND insert/slicer knobs in .p[]; other fields unused */

/* The shared SEND knobs, per part (the second core renders parts at once: fx.c mix_block). mod.c mod_begin
 * fills one copy with fx_bus_trk.p and the DLY / REV bus settings of song.g, then writes the matrix's values
 * into it; the part's DSP reads the copy (mod.c mod_target_ptr, fx.c ins_param_src, slicer.c sl_run_send), so
 * two parts never write or read the same knobs and each sees its own modulation, as they did when the renders
 * ran one after another. Nothing outside a render touches the copies; the real knobs stay the UI's, motion's
 * and the bus DSP's (fx_buses after the parts). p[] is first, as track_t's: the DSP functions take a track_t
 * and read its p[] (a cast of the copy). */
typedef struct {
    int16_t p[P_COUNT];
    int16_t g[5];              /* MFX_TIME..MFX_DAMP: the DLY / REV bus settings of song.g */
} bus_knobs_t;
static bus_knobs_t bus_knobs[C1_NP];
static uint8_t bus_knobs_on[C1_NP];         /* 1: this part's copy is its render's view (mod_begin) */
_Static_assert(__builtin_offsetof(track_t, p) == 0 && __builtin_offsetof(bus_knobs_t, p) == 0 &&
               sizeof ((track_t *)0)->p == sizeof ((bus_knobs_t *)0)->p,
               "bus_knobs_t.p overlays track_t.p (the DSP reads the cast copy)");
/* the knobs a part's render reads: its copy while the matrix modulates shared ones, else the real */
static const track_t *bus_knobs_track(const track_t *t)
{
    return bus_knobs_on[C1_PI(t)] ? (const track_t *)&bus_knobs[C1_PI(t)] : &fx_bus_trk;
}

/* SLICER ownership (G_SLOWN). Legacy exclusive owner is still readable until sl_own_sanitize. */
static void sl_own_sanitize(void)
{
    int32_t v = song.g[G_SLOWN];
    if (v >= 0 && v <= (int32_t)NTRK) {
        if (v == 0)
            song.g[G_SLOWN] = (int16_t)((1u << NTRK) - 1u);   /* all SEND */
        else
            song.g[G_SLOWN] = (int16_t)(((1u << NTRK) - 1u) ^ (1u << (uint32_t)(v - 1)));
    } else if (v < 0 || v > (int16_t)((1u << NTRK) - 1u))
        song.g[G_SLOWN] = (int16_t)((1u << NTRK) - 1u);
}
static int sl_trk_send(const track_t *t)
{
    int32_t v = song.g[G_SLOWN];
    uint32_t tr = (uint32_t)(t - trk);
    if (v >= 0 && v <= (int32_t)NTRK)
        return v == 0;                                   /* legacy: only all-SEND; else CHAIN/USED */
    return (v & (int32_t)(1u << tr)) != 0;
}
static int sl_trk_chain(const track_t *t)
{
    int32_t v = song.g[G_SLOWN];
    uint32_t tr = (uint32_t)(t - trk);
    if (v >= 0 && v <= (int32_t)NTRK)
        return v == (int32_t)(tr + 1u);
    return !(v & (int32_t)(1u << tr));
}
static int sl_trk_used(const track_t *t)
{
    int32_t v = song.g[G_SLOWN];
    if (v >= 1 && v <= (int32_t)NTRK)
        return v != (int32_t)((t - trk) + 1u);            /* legacy exclusive owner */
    return 0;                                            /* mask form: never USED */
}

/* SWING of a track's step clock: the track's own plus the global one, at most 100 (#31). The sequencer
 * (seq.c step_samples) and the SLICER (slicer.c sl_enter) both time steps with it. At 100 the even
 * steps are 1.4 x the straight length and the odd ones 0.6 x. */
#define SWING_MAX 100
static inline int32_t track_swing(const track_t *t)
{
    int32_t sw = t->p[P_SSWING] + song.g[G_SWING];
    return sw < 0 ? 0 : sw > SWING_MAX ? SWING_MAX : sw;
}
/* the length of step idx of a straight length `base`, swung: even steps longer, odd ones shorter */
static inline uint32_t swing_step_len(const track_t *t, uint32_t base, uint32_t idx)
{
    int32_t sw = track_swing(t) * (int32_t)base / 250;
    return base + (uint32_t)((idx & 1u) ? -sw : sw);
}

/* ----------------------------------------------------------- system --- */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> UBOOT */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
