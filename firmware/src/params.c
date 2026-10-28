/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Parameter descriptors, formatting and the page table. */
static const char *const N_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
/* LFO 2 (1.2, #154). SYNC: OFF (RATE), or one cycle per note value of the tempo, longest first (LSYNC_DIV: its N_DIV);
 * TRIG: NOTE restarts it at PHS on a fresh phrase (as before), FREE never; POL: BI -1..+1 (as before), UNI 0..+1 */
static const char *const N_LSYNC[] = {"OFF", "4BAR", "2BAR", "1/1", "1/2", "1/4", "1/8", "8T", "1/16", "16T", "1/32"};
static const uint8_t LSYNC_DIV[11] = {0, 9, 8, 7, 6, 0, 1, 4, 2, 5, 3};
static const char *const N_LTRIG[] = {"NOTE", "FREE"};
static const char *const N_LPOL[] = {"BI", "UNI"};
static const char *const N_AMODE[] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD", "REPEAT",   /* (append-only: seq.c AM_*) */
                                       "DNUP", "UP+8", "CONV", "DIVG", "PINKY", "THUMB", "WALK", "CHORD", "DRONE"};
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR"};
/* the delay's TIME (G_DTIME): N_DIV's ten (their values kept), then 1.5.1's dotted ones appended (stored values:
 * append-only). A list of its own: ARP RATE, SEQ DIV and LFO SYNC keep N_DIV's ten (fx.c delay_samples) */
static const char *const N_DTIME[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR",
                                      "1/8D", "1/16D", "1/4D"};
/* CHR removed (FUN13): chromatic play is MAP OFF. Older projects: index 0 was CHR -> MAJ; 1..15 -> 0..14 */
static const char *const N_SCALE[] = {"MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM",
                                    "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH"};
static const char *const N_ONOFF[] = {"OFF", "ON"};
/* MIXER KNOB 4 (and VOICE 2): PLAY. Stored 0 is ON (sounds, the default), 1 is OFF (silent), 2 is SOLO
 * (sounds, and silences every track that is not SOLO). The knob shows OFF, ON, SOLO (PLAY_ORDER): left of
 * ON is OFF, right is SOLO. Append-only: stored projects keep 0 playing and 1 silent. */
static const char *const N_PLAY[] = {"ON", "OFF", "SOLO"};
/* ANALOG's filter TYPE (1.5, #104; eng_analog.c): LP / BP / HP, and COMB (insert chain / Araminta) */
static const char *const N_FTYPE[] = {"LP", "BP", "HP", "COMB"};
/* ENV SYNC (1.5, #175; voice.c esync_coef): ATK DEC REL as note values of the tempo, shortest first. 0..127 split
 * evenly over these 25 (param_format's F_INT name list: value v is ESYNC_NAMES[v * 25 / 128]); ESYNC_UNITS: each in
 * 1/384 of a bar of 4/4 (a quarter 96), 0 = the shortest time ATK / DEC / REL 0 has. 0-terminated (param_format) */
#define ESYNC_N 25u
static const char *const ESYNC_NAMES[ESYNC_N + 1u] = {
    "0", "1/64T", "1/64", "1/32T", "1/64D", "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D",
    "1/4", "1/2T", "1/4D", "1/2", "1/1T", "1/2D", "1/1", "1/1D", "2BAR", "3BAR", "4BAR", 0};
static const uint16_t ESYNC_UNITS[ESYNC_N] = {0, 4, 6, 8, 9, 12, 16, 18, 24, 32, 36, 48, 64, 72,
                                              96, 128, 144, 192, 256, 288, 384, 576, 768, 1152, 1536};
static const param_desc_t ESYNC_DESC[3] = {   /* ATK DEC REL with ENV SYNC on: the range and defaults as ever */
    {"ATK", F_INT, 0, 127, 10, ESYNC_NAMES, 0},
    {"DEC", F_INT, 0, 127, 70, ESYNC_NAMES, 0},
    {"REL", F_INT, 0, 127, 60, ESYNC_NAMES, 0},
};
static uint32_t esync_idx(int32_t v) { return (uint32_t)(v & 127) * ESYNC_N >> 7; }   /* (param_format's split) */
/* seq.c kb_map (MAP on SCL). Values kept: 0 OFF, 1 ALL (was SNAP label), 2 KBD (was WHITE), 3 SEQ.
 * KBD: white keys walk the scale; black keys play the closest in-scale note (duplicate OK). ALL: same keys as
 * KBD, and sequenced notes snap as they play. SEQ: live keys as OFF; sequenced notes snap (steps kept). */
/* stored append-only: OFF 0, ALL 1, KBD 2, SEQ 3, CURR 4 (shown third, see QUANT_ORDER) */
static const char *const N_QUANT[] = {"OFF", "ALL", "KBD", "SEQ", "CURR"};
/* chord keys (chord.c): OFF, the diatonic triad / seventh of the track's ROOT and SCALE on the key, fixed shapes */
static const char *const N_CHRD[] = {"OFF", "DIA3", "DIA7", "MAJ", "MIN", "DOM7", "MAJ7", "MIN7", "SUS4", "POW"};
static const char *const N_STRUM[] = {"OFF", "1/8", "1/16", "1/32", "1/64", "1/128", "16T", "32T"};
static const char *const N_STROKE[] = {"DOWN", "UP", "ALT"};
/* P_ALLOC on the editor: both knobs in one list (the device has STRUM and STROKE apart) */
static const char *const N_SWEEP[] = {"OFF", "OFF", "1/8 DOWN", "1/8 UP", "1/8 ALT", "1/16 DOWN", "1/16 UP", "1/16 ALT",
    "1/32 DOWN", "1/32 UP", "1/32 ALT", "1/64 DOWN", "1/64 UP", "1/64 ALT", "1/128 DOWN", "1/128 UP", "1/128 ALT",
    "16T DOWN", "16T UP", "16T ALT", "32T DOWN", "32T UP", "32T ALT"};
static const char *const N_VOIC[] = {"CLOSE", "OPEN", "INV1", "INV2", "+OCT"};   /* VC_CLOSE .. VC_BASS */
static const char *const N_VOICE[] = {"POLY", "MONO", "LEG", "UNI"};   /* V_POLY .. V_UNISON */
static const char *const N_ORDER[] = {"NOTE", "PLAY"};
static const char *const N_CLOCK[] = {"INT", "USB", "TRS"};
static const char *const N_MIDI_INPUT[] = {"USB", "TRS"};
/* MIDI IN (seq.c midi_base): 1..4 -> parts, 5..16 ignored / all -> selected / (1.2, append-only) 5..8, 9..12, 13..16 -> parts */
static const char *const N_ROUTE[] = {"CH1-4", "SEL", "CH5-8", "CH9-12", "CH13-16"};
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
static const char *const N_RTYPE[] = {"ROOM", "SPRING", "HALL", "SHIMR", "CLOUD"};   /* G_RTYPE (fx.c; JIANT SHIMR/CLOUD) */
static const char *const N_GO[] = {"--", "GO"};
static const char *const N_SLCR[] = {"OFF", "GATE", "STUT"};             /* SL_OFF .. SL_STUT (slicer.c) */
static const char *const N_SLDIV[] = {"1/8", "1/16", "1/32", "8T", "16T", "32T"};   /* SL_DEN */
/* insert chain: DIST modes, FILTER types (also N_FTYPE's COMB), CRUSH rate labels */
static const char *const N_IDMODE[] = {"SOFT", "HARD", "FOLD", "FUZZ"};
static const char *const N_IFTYPE[] = {"LP", "HP", "BP", "COMB"};
static const char *const N_IBITS[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
                                      "16", 0};
static const char *const N_IRATE[] = {"689", "919", "1.1k", "1.4k", "1.8k", "2.2k", "2.8k", "3.7k", "4.4k", "5.5k",
                                      "7.4k", "8.8k", "11k", "15k", "22k", "44k", 0};
/* EFFECTS page: stage names in chain order (fx.c). K1 chooses, K2 moves.
 * Same strings as the stage page titles (PAGES[] below); name column sized to FLANGER/DISTORT. */
static const char *const N_ISTAGE[] = {"DISTORT", "FILTER", "CRUSH", "PHASER", "FLANGER", "CHORUS", "DELAY", "REVERB",
                                       "SLICER", "TAPE"};
/* EFFECTS TAPE G_TAPEMODE: the one machine's route (fx.c fx_list_mode) */
static const char *const N_TAPEMODE[] = {"SEND", "CH1", "CH2", "CH3", "CH4", "MASTER"};
/* modulation matrix (mod.c): sources, destinations (E1..E8 = P_E0..P_E7: shown with the engine's labels).
 * Append-only (stored values keep their meaning): S&H SLEW and DEPTH (#132) came after EXPR and E8 */
static const char *const N_MSRC[] = {"OFF", "LFO", "ENV", "VEL", "KEY", "RAND", "MODW", "AT", "EXPR", "S&H", "SLEW"};
/* destinations (append-only): DEPTH = LFO depth (#132); SLPAT / SLRATE / SLDEP = the SLICER (#222);
 * CHRD / VOIC = chord keys; then insert continuous knobs + MIX (no MODE/TYPE enums): TONE/DMIX,
 * CRUSH, FILTER, PHASR, FLANG, CHOR RATE/DEP (CHOR MIX is already CHO; DLY/REV amounts stay DLY/REV;
 * DRIVE stays DIST). Pick-list order is ORD[] in ui_events.c (CRUSH before FILTER). */
static const char *const N_MDST[] = {"OFF", "PITCH", "CUT", "SHP", "AMP", "PAN", "DIST", "CHO", "DLY MIX", "REV MIX", "RATE",
                                     "VIB", "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8", "DEPTH",
                                     "PAT", "SRATE", "SDEP", "CHRD", "VOIC",
                                     "TONE", "DMIX", "FCUT", "FRES", "FMIX", "BITS", "CRAT", "CLPF", "CMIX",
                                     "PRAT", "PDEP", "PFBK", "PMIX", "FRAT", "FDEP", "LMIX", "HRAT", "HDEP",
                                     "DTIME", "DFBK", "DCOL", "RSIZE", "RDAMP", "TSPEED", "TWOW"};
static const char *const N_ENGNAME[] = {"ANALOG", FELUCCA_FM4 ? "DIGITAL" : "-", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN", "PHYS",
                                             "DRUM-X", "NOISE", "FM6", "-", "WAVETABLE", "DRUMS",
};

#define PD(l, f, mn, mx, df) {l, f, mn, mx, df, 0, 0}
#define PE(l, n, df) {l, F_ENUM, 0, (int16_t)(sizeof(n) / sizeof(n[0]) - 1), df, n, 0}

static const param_desc_t TP[P_COUNT] = {
    [P_LEVEL] = PD("LVL", F_DB, 0, 127, 104),
    [P_ATK] = PD("ATK", F_TIME, 0, 127, 10),
    [P_DEC] = PD("DEC", F_TIME, 0, 127, 70),
    [P_SUS] = PD("SUS", F_PCT, 0, 127, 90),
    [P_REL] = PD("REL", F_TIME, 0, 127, 60),
    [P_ED_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_ED_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_ED_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_ED_FX] = PD("TONE", F_BIPCT, -64, 63, 0),   /* DIST: brighter / darker than the drive-linked cutoff */
    [P_LRATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [P_LWAVE] = PE("WAVE", N_LWAVE, 0),
    [P_LPHASE] = PD("PHS", F_INT, 0, 127, 0),
    [P_LFADE] = PD("FADE", F_TIME, 0, 127, 0),
    [P_LD_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_LD_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_LD_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_LD_AMP] = PD("AMP", F_PCT, 0, 127, 0),
    [P_AMODE] = PE("MODE", N_AMODE, 0),
    [P_ARATE] = PE("RATE", N_DIV, 2),
    [P_AOCT] = PD("OCT", F_INT, 1, 4, 1),
    [P_AGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_ASWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_APROB] = PD("PROB", F_PCT, 0, 127, 127),
    [P_AHOLD] = PE("HOLD", N_ONOFF, 0),
    [P_AORDER] = PE("ORD", N_ORDER, 0),
    [P_ROOT] = PD("ROOT", F_NOTE, 0, 11, 0),
    [P_SCALE] = PE("SCL", N_SCALE, 0),
    [P_QUANT] = PE("MAP", N_QUANT, 0),                 /* max: TP[P_QUANT].max, set after N_QUANT */
    [P_TRANS] = PD("TRN", F_SEMI, -24, 24, 0),
    [P_SLEN] = PD("LEN", F_STEPS, 1, NSTEP, 16),
    [P_SDIV] = PE("DIV", N_DIV, 2),
    [P_SSWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_SGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_DIST] = PD("DRIVE", F_PCT, 0, 127, 64),  /* ready when DIST MIX is raised; MIX defaults to bypass */
    [P_CHOR] = PD("FXPOS", F_INT, 0, 127, FX_POS_DEF), /* EFFECTS DLY/REV slots; SLCR in track.fx_sl_pos */
    [P_DLY] = PD("DLY", F_PCT, 0, 127, 0),
    [P_REV] = PD("REV", F_PCT, 0, 127, 0),
    [P_VOICE] = PE("VCE", N_VOICE, 0),
    [P_GLIDE] = PD("GLD", F_TIME, 0, 127, 0),
    [P_TAPEMIX] = PD("-", F_INT, 0, 127, 0),        /* the old GLMOD id: TAPE MIX is the global G_TMIX */
    [P_LSMOOTH] = PD("SMTH", F_PCT, 0, 127, 0),     /* WAVE S&H's smoothing (P_PRIO's inert id: no format change) */
    [P_ALLOC] = PE("SWEEP", N_SWEEP, 0),             /* CHORD K3 / K4 on the device: STRUM and STROKE */
    [P_DETUNE] = PD("UNI DTN", F_INT, 0, 127, 40),   /* UNISON voice spread only (voice.c mono_play) */
    [P_PAN] = PD("PAN", F_BIPCT, -64, 63, 0),
    [P_MUTE] = PE("PLAY", N_PLAY, 0),
    [P_SLCR] = PE("SLCR", N_SLCR, 0),
    [P_SLPAT] = PD("PAT", F_INT, 1, 16, 1),        /* SL_PAT[] */
    [P_SLRATE] = PE("RATE", N_SLDIV, 1),
    [P_SLDEPTH] = PD("MIX", F_PCT, 0, 127, 127),   /* was DEPTH; EFFECTS K4 amount */
#define MSLOT(k) [P_M##k##SRC] = PE("SRC" #k, N_MSRC, 0), [P_M##k##DST] = PE("DST" #k, N_MDST, 0), \
                 [P_M##k##AMT] = PD("AMT" #k, F_BIPCT, -64, 63, 0)
    MSLOT(1), MSLOT(2), MSLOT(3), MSLOT(4),
#undef MSLOT
/* the OP ENV / OP LEVEL values of DIGITAL (ids 61..80): with FELUCCA_FM4 its operator envelopes; without it
 * inert, on no page and in no editor layout, read only when a DIGITAL sound converts (fm4_convert.c). Their labels
 * stay: the editor's library files key parameters by label ("ATK#2" ..), so an old file's values still find them */
#define FMOP(k) [P_FM##k##_ATK] = PD("ATK", F_TIME, 0, 127, 0), \
                [P_FM##k##_DEC] = PD("DEC", F_TIME, 0, 127, 0), \
                [P_FM##k##_SUS] = PD("SUS", F_PCT, 0, 127, 127), \
                [P_FM##k##_REL] = PD("REL", F_TIME, 0, 127, 0), \
                [P_FM##k##_LEVEL] = PD("LVL", F_PCT, 0, 127, 127)
    FMOP(1), FMOP(2), FMOP(3), FMOP(4),
#undef FMOP
    [P_CHRD] = PE("CHRD", N_CHRD, 0),
    [P_VOIC] = PE("VOIC", N_VOIC, 0),
/* the DRUM engine's lane levels (eng_drum.c, #97): 100 % = the kit as designed (the sound before 1.1) */
#define LANE(k, l) [P_LN0 + (k)] = PD(l, F_PCT, 0, 127, 127)
    LANE(0, "KICK"), LANE(1, "SNARE"), LANE(2, "CLAP"), LANE(3, "HATCL"),
    LANE(4, "HATOP"), LANE(5, "TOM"), LANE(6, "RIM"), LANE(7, "BELL"),
#undef LANE
    [P_LSYNC] = PE("SYNC", N_LSYNC, 0),
    [P_LTRIG] = PE("TRIG", N_LTRIG, 0),
    [P_LPOL] = PE("POL", N_LPOL, 0),
    [P_SQNT] = PE("QNTZ", N_ONOFF, 1),            /* ON: every step on its start, as before 1.2 */
    [P_SPRD] = PD("SPRD", F_PCT, 0, 127, 0),      /* #148: 0 = every voice on PAN, as before (fx.c mix_spread) */
    [P_IORD] = PD("ORDER", F_INT, 0, 719, IORD_DEF), /* Lehmer 0..719; default DIST→CRUSH→FILTER→… (fx.c) */
    [P_IDMODE] = PE("MODE", N_IDMODE, 0),
    [P_IDMIX] = PD("MIX", F_PCT, 0, 127, 0),
    [P_IFTYPE] = PE("TYPE", N_IFTYPE, 0),
    [P_IFCUT] = PD("CUT", F_CUTOFF, 0, 127, 96),
    [P_IFRES] = PD("RES", F_PCT, 0, 127, 40),
    [P_IFMIX] = PD("MIX", F_PCT, 0, 127, 0),
    [P_ICBITS] = PD("BITS", F_INT, 0, 127, 64),
    [P_ICRATE] = PD("RATE", F_INT, 0, 127, 96),
    [P_ICLPF] = PD("LPF", F_CUTOFF, 0, 127, 96),
    [P_ICMIX] = PD("MIX", F_PCT, 0, 127, 0),
    [P_IPRATE] = PD("RATE", F_LFOHZ, 0, 127, 64),
    [P_IPDEP] = PD("DEPTH", F_PCT, 0, 127, 96),
    [P_IPFBK] = PD("FDBK", F_PCT, 0, 127, 96),
    [P_IPMIX] = PD("MIX", F_PCT, 0, 127, 0),
    [P_ILRATE] = PD("RATE", F_LFOHZ, 0, 127, 64),
    [P_ILDEP] = PD("DEPTH", F_PCT, 0, 127, 96),
    [P_ILMIX] = PD("MIX", F_PCT, 0, 127, 0),
    [P_IHRATE] = PD("RATE", F_LFOHZ, 0, 127, 64),
    [P_IHDEP] = PD("DEPTH", F_PCT, 0, 127, 96),
    [P_IHMIX] = PD("MIX", F_PCT, 0, 127, 0),
    [P_FTYPE] = PE("TYPE", N_FTYPE, 0),           /* ANALOG filter: LP / BP / HP / COMB */
    [P_ESYNC] = PE("ESYNC", N_ONOFF, 0),          /* 1.5 (#175): OFF = ATK DEC REL in ms, as before (voice.c esync) */
};

static const param_desc_t GP[G_COUNT] = {
    [G_BPM] = PD("BPM", F_BPM, 40, 240, 120),
    [G_SWING] = PD("SWG", F_PCT, 0, 100, 0),
    [G_CLOCK] = PE("CLK", N_CLOCK, 0),
    [G_TUNE] = PD("TUNE", F_INT, -50, 50, 0),     /* cents; edited on MENU > AUDIO TUNE (1.2: menu_items.c MI_TUNE) */
    [G_DTIME] = PE("TIME", N_DTIME, 1),   /* 0..12 divisions. Free ms is DTIME_FREE+ms in the same word (fx.c) */
    [G_DFDBK] = PD("FDBK", F_PCT, 0, 120, 60),
    [G_DCOLOR] = PD("COLR", F_PCT, 0, 127, 70),
    [G_TSPEED] = PD("SPEED", F_PCT, 25, 400, 100), /* EFFECTS TAPE speed % (was G_DMIX, retired return gain) */
    [G_RSIZE] = PD("SIZE", F_PCT, 0, 127, 90),
    [G_RDAMP] = PD("DAMP", F_PCT, 0, 127, 60),
    [G_CRATE] = PD("DLYOWN", F_INT, 0, 4, 0),     /* 0 SEND; 1..4 track with delay CHAIN (was bus CRT) */
    [G_CDEPTH] = PD("REVOWN", F_INT, 0, 4, 0),    /* 0 SEND; 1..4 track with reverb CHAIN (was bus CDP) */
    [G_TWOW] = PD("WOW", F_PCT, 0, 127, 0),      /* TAPE wow/flutter depth (was G_MIDI, the old SYSTEM page) */
    [G_DREL] = PD("REL", F_PCT, 0, 100, 30),     /* DUCK's release: 40 .. 600 ms (fx.c duck_block) */
    /* MIDI IN's routing, edited on MENU > MIDI > MIDI IN (1.2: menu_items.c MI_MIDIIN; up to 1.1.5 GLO > SYSTEM ROUT),
     * stored with the project as before. (was "--": stored 0 = CH1-4, as MIDI IN always was) */
    [G_ROUTE] = PE("ROUT", N_ROUTE, 0),
    [G_TDROP] = PE("DROP", N_ONOFF, 0),          /* TAPE dropouts (was G_INFO, CPU) */
    [G_SLOT] = PD("SLOT", F_INT, 1, 4, 1),
    [G_NAME] = PE("NAME", N_DASH, 0),
    [G_LOAD] = PE("LOAD", N_GO, 0),
    [G_SAVE] = PE("SAVE", N_GO, 0),
    [G_ENGSEL] = PE("ENG", N_ENGNAME, 0),
    [G_CLIP] = PD("CLIP", F_PCT, 0, 100, 0),     /* master clipper: drive x1 .. x4 into the soft clip */
    [G_TMIX] = PD("MIX", F_PCT, 0, 127, 0),      /* TAPE MASTER dry/wet (was G_CLRSEQ, an unused GO) */
    [G_TAPEMODE] = PE("TAPE", N_TAPEMODE, 0),    /* 0 SEND, 1..4 CHAIN owner, 5 MASTER (was G_INITSND) */
    /* the reverb's model on the REVERB page: the id of the old GM drum channel (G_DRCH, inert since 1.0) */
    [G_RTYPE] = PE("TYPE", N_RTYPE, 2),         /* SEND tank; HALL at power-on since 1.2 */
    /* (JIANT) drum bus and kick ducking; ids were G_DRLVL / G_DRREV (GM drum part). proj_drums_to_part clears them. */
    [G_PUNCH] = PD("PNCH", F_PCT, 0, 100, 0),   /* DRUM fattened: attack + body (eng_drum.c) */
    [G_DUCK] = PD("DUCK", F_PCT, 0, 100, 0),    /* the kick ducks the other parts: up to -30 dB */
    [G_CRTYPE] = PE("TYPE", N_RTYPE, 2),        /* CHAIN tank: same models, separate values */
    [G_CRSIZE] = PD("SIZE", F_PCT, 0, 127, 90),
    [G_CRDAMP] = PD("DAMP", F_PCT, 0, 127, 60),
    [G_SLOWN] = PD("SLOWN", F_INT, 0, 15, 0),   /* 0 and 5..15 are the current mask (0 = all SEND); 1..4 remain
                                                 * accepted legacy owner values */
};

static const param_desc_t INS_BITS = {"BITS", F_INT, 0, 127, 64, N_IBITS, 0};
static const param_desc_t INS_RATE = {"RATE", F_INT, 0, 127, 96, N_IRATE, "Hz"};
static const param_desc_t PITCH_DESC = PD("PITCH", F_SEMI, -24, 24, 0);   /* 3.2: the step's semitones */

static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if (id == MSEQ_PITCH)                             /* not in p[]: the AUTOMATION list / graph still draws it */
        return &PITCH_DESC;
    if (id >= P_E0 && id <= P_E7) {                   /* the engine asked for (t->engine follows after a fade) */
        const engine_t *e = ENGINES[eng_idx(t->eng_req)];
        const param_desc_t *d = e->desc ? e->desc(t, id - P_E0) : 0;   /* a mode-dependent label / names */
        return d ? d : &e->edit[id - P_E0];
    }
    if (id == P_ICBITS)
        return &INS_BITS;
    if (id == P_ICRATE)
        return &INS_RATE;
    if (t->p[P_ESYNC] && (id == P_ATK || id == P_DEC || id == P_REL))   /* ENV SYNC: note values (1.5, #175) */
        return &ESYNC_DESC[id == P_ATK ? 0 : id == P_DEC ? 1 : 2];
    return &TP[id];
}

/* The name of an engine parameter in a list (MOD DST, AUTOMATION, a step lock): the EDIT cards are short
 * (WAVE, POS, LEVEL) because the page title says OSC A / OSC B, but the lists have no page context, so
 * WAVETABLE's two oscillators are named here. full = 0 keeps the MOD card's narrow column, 1 gives the
 * longer word for the picker and the automation rows. Other engines' labels go through unchanged. */
static const char *param_list_name(const track_t *t, uint32_t id, int full)
{
    static const char *const SHORT[] = {"WAVE A", "POS A", "WAVE B", "POS B", "B LEVEL"};
    static const char *const FULL[] = {"WAVE A", "POSITION A", "WAVE B", "POSITION B", "B LEVEL"};
    if (id >= P_E0 && id <= P_E7 && ENGINES[eng_idx(t->eng_req)] == &ENG_WT) {
        uint32_t k = id - P_E0;
        if (k == 0u || k == 1u || k == 4u || k == 5u || k == 6u) {
            uint32_t s = k == 0u ? 0u : k == 1u ? 1u : k == 4u ? 2u : k == 5u ? 3u : 4u;
            return full ? FULL[s] : SHORT[s];
        }
    }
    return track_desc(t, id)->label;
}

/* the descriptor of parameter id for engine e without the engine's desc hook (the device display
 * only; range and default are the same): what the editor protocol, user presets and projects use */
static const param_desc_t *param_desc_of(uint32_t e, uint32_t id)
{
    return id >= P_E0 ? &ENGINES[e]->edit[id - P_E0] : &TP[id];
}

/* a retired F_ENUM value kept as an alias, so stored values stay valid: SAMPLE SET and GRAIN SRC 1, once
 * TRANH, and 4, once PERC (a SAMPLE sound of it loads as DRUM: core.h drum_from_perc), play PIANO
 * (tools/gen_samples.py SMP_SET_ORIG). DRUM-X retired Felucca's KIT enum (P_E0 is MRPH 0..127; old KIT values
 * clamp). It shows the original's name; knobs step over it and the editor's SET lands on the original. */
static int32_t enum_orig(const param_desc_t *d, int32_t v)
{
    if (d->names == N_SWEEP && v == 1)
        return 0;                                  /* legacy ALLOC = REUSE is an OFF alias */
    return (d->names == SMP_ALL_NAMES || d->names == SMP_SRC_NAMES) && v >= 0 && v < (int32_t)SMP_NSETS
               ? (int32_t)SMP_SET_ORIG[v] : v;
}

/* a stored value as the parameter takes it (projects, user presets, motion): inside d's range.
 * FM6's SLOT is never stored as a bank slot: 9..40 (1.4.1's B1..B32; 1.0.2's B1..B27 were 8..34) is 8, OWN,
 * and the patch comes with what is loaded */
static int32_t param_fit(const param_desc_t *d, int32_t v)
{
    v = clamp(v, d->min, d->max);
    if (d->names == N_FM6_PATCH)
        return v > (int32_t)FM6_OWN ? (int32_t)FM6_OWN : v;
    return v;
}

/* --------------------------------------------- the stored parameter wire form ---
 * One parameter as a project (project.c) stores it: a biased byte (value + 64, so -64..127),
 * and two bytes LE for P_IORD, whose low 10 bits are the chain order and whose high 6 are the
 * per-stage send flags (core.h). Both the pack and the unpack of every format go through
 * these, so the form and its limits exist once; a value or an order past them is refused,
 * never clamped (a project that holds one is not written and not loaded). */
#define WIRE_BIAS 64
#define WIRE_MIN (-64)
#define WIRE_MAX 127
#define WIRE_IORD_MAX 719u
/* -> 1 byte written (b), 0 = the value is outside the biased range */
static int wire_val_put(uint8_t *b, int16_t v)
{
    if (v < WIRE_MIN || v > WIRE_MAX)
        return 0;
    *b = (uint8_t)(v + WIRE_BIAS);
    return 1;
}
/* -> 1 byte read (*v), 0 = the byte is outside the biased range */
static int wire_val_get(const uint8_t *b, int16_t *v)
{
    if (*b > (uint8_t)(WIRE_MAX - WIRE_MIN))
        return 0;
    *v = (int16_t)*b - WIRE_BIAS;
    return 1;
}
/* P_IORD: order in lo, send flags in hi -> 2 bytes written, 0 = no such order */
static int wire_iord_put(uint8_t *lo, uint8_t *hi, uint16_t raw)
{
    if ((raw & 1023u) > WIRE_IORD_MAX)
        return 0;
    *lo = (uint8_t)raw;
    *hi = (uint8_t)(raw >> 8);
    return 2;
}
/* -> 2 bytes read (lo, hi), 0 = no such order */
static int wire_iord_get(const uint8_t *lo, const uint8_t *hi, uint16_t *raw)
{
    *raw = (uint16_t)(lo[0] | (uint32_t)hi[0] << 8);
    return (*raw & 1023u) <= WIRE_IORD_MAX;
}

/* v is a value a knob passes over: an alias, or one of FM6's bank slots without a voice, or ARP REPEAT
 * (id 6 kept for loads; the MODE knob steps ORD ↔ DNUP) */
static int enum_skip(const param_desc_t *d, int32_t v)
{
    return enum_orig(d, v) != v || (d->names == N_FM6_PATCH && !fm6_slot_ok(v)) ||
           (d->names == N_AMODE && v == 6);
}
/* a knob moved an F_ENUM from `from` to v: past any value it skips in that direction; with none left to the end, the
 * last one it does not skip on the way from `from` (else `from`) */
static int32_t enum_step(const param_desc_t *d, int32_t from, int32_t v)
{
    int32_t dir = v > from ? 1 : -1, w = v;
    while (w != from && enum_skip(d, w))
        w = w + dir > d->max || w + dir < d->min ? from : w + dir;
    while (w == from && v != from && enum_skip(d, v))
        v -= dir;
    return w == from ? v : w;
}

/* #48: the note divisions in the order of their length, longest first (triplets between their neighbours), on the
 * knobs and the gauges; the stored values (N_DIV, N_SLDIV indices: projects, presets, the editor protocol) stay.
 * -> the shown order of d's values (index: position, entry: value), 0 = the values' own order */
static const uint8_t DIV_ORDER[10] = {9, 8, 7, 6, 0, 1, 4, 2, 5, 3};   /* 4BAR 2BAR 1/1 1/2 1/4 1/8 8T 1/16 16T 1/32 */
static const uint8_t SLDIV_ORDER[6] = {0, 3, 1, 4, 2, 5};              /* 1/8 8T 1/16 16T 1/32 32T */
/* Delay TIME: names 0..12 stay (loads). Knob omits 1/1 2BAR 4BAR (7..9); they never fit DLY_LEN. */
static const uint8_t DTIME_ORDER[10] = {6, 12, 0, 10, 1, 11, 4, 2, 5, 3}; /* 1/2 1/4D 1/4 1/8D 1/8 1/16D 8T 1/16 16T 1/32 */
static const uint8_t PLAY_ORDER[3] = {1, 0, 2};                    /* OFF, ON, SOLO (stored PLAY_OFF, PLAY_ON, PLAY_SOLO) */
static const uint8_t QUANT_ORDER[5] = {0, 1, 4, 2, 3};             /* OFF ALL CURR KBD SEQ (stored 0 1 2 3 4) */
static const uint8_t *enum_order(const param_desc_t *d)
{
    return d->names == N_DIV ? DIV_ORDER : d->names == N_SLDIV ? SLDIV_ORDER : d->names == N_DTIME ? DTIME_ORDER
         : d->names == N_PLAY ? PLAY_ORDER : d->names == N_QUANT ? QUANT_ORDER : 0;
}
static int32_t enum_order_n(const param_desc_t *d)   /* shown steps; DTIME shorter than max (legacy 7..9) */
{
    if (d->names == N_DTIME)
        return 10;
    return d->max - d->min + 1;
}
static int32_t enum_rank(const param_desc_t *d, int32_t v)   /* v's place in the shown order (+ min): the gauges */
{
    const uint8_t *o = enum_order(d);
    int32_t r, n = enum_order_n(d), key = v - d->min;
    if (d->names == N_DTIME && key >= 7 && key <= 9)
        key = 6;                                      /* legacy 1/1 2BAR 4BAR sit on 1/2 for the knob */
    for (r = 0; o && r < n; r++)
        if (o[r] == key)
            break;
    if (o && r >= n)
        r = 0;
    return o ? r + d->min : v;
}
/* a knob turned `steps` (signed) from v: clamped, over the shown order, past aliases (enum_step) */
static int32_t param_turn(const param_desc_t *d, int32_t v, int32_t steps)
{
    const uint8_t *o = enum_order(d);
    if (o)
        return o[clamp(enum_rank(d, v) - d->min + steps, 0, enum_order_n(d) - 1)] + d->min;
    if (d->names == ESYNC_NAMES) {                    /* ENV SYNC: a note value a step (the first value showing it) */
        int32_t k = clamp((int32_t)esync_idx(v) + steps, 0, ESYNC_N - 1);
        return (k * 128 + ESYNC_N - 1) / ESYNC_N;
    }
    if (d->names == N_IBITS || d->names == N_IRATE) { /* CRUSH: one detent = one label (audio uses >> 3) */
        int32_t k = clamp((v >> 3) + steps, 0, 15);
        return k << 3;
    }
    return enum_step(d, v, clamp(v + steps, d->min, d->max));
}

static uint8_t fmt_named;        /* the last param_format was a name (F_ENUM, even "4"): ui_draw.c's digits do not roll it */

/* value string (<= 5 chars) and unit for a parameter value */
static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
    fmt_named = d->fmt == F_ENUM;
    switch (d->fmt) {
    case F_PCT:                                     /* a 0..100 range (SWG) is its value, others a share of 127 */
        fmt_int(val, d->max == 100 ? v : (v * 100 + 63) / 127);
        *unit = "%";
        break;
    case F_BIPCT:
        fmt_int(val, v * 100 / 64);
        if (v > 0) {
            char t[8];
            fmt_int(t, v * 100 / 64);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "%";
        break;
    case F_TIME: {
        uint32_t ms10 = TIME_MS_X10[v & 127];
        if (ms10 < 100u) {
            fmt_fix(val, (int32_t)ms10, 1);
            *unit = "ms";
        } else if (ms10 < 10000u) {
            fmt_int(val, (int32_t)((ms10 + 5u) / 10u));
            *unit = "ms";
        } else {
            fmt_fix(val, (int32_t)(ms10 / 100u), 2);
            if (ms10 >= 100000u)
                fmt_fix(val, (int32_t)(ms10 / 1000u), 1);
            *unit = "s";
        }
        break;
    }
    case F_LFOHZ: {
        uint32_t h = LFO_HZ_X100[v & 127];
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else
            fmt_fix(val, (int32_t)(h / 10u), 1);
        *unit = "Hz";
        break;
    }
    case F_CUTOFF: {
        uint32_t h = CUTOFF_HZ[v & 127];
        if (h < 1000u) {
            fmt_int(val, (int32_t)h);
            *unit = "Hz";
        } else {
            fmt_fix(val, (int32_t)(h / 100u), 1);
            *unit = "kHz";
        }
        break;
    }
    case F_DB:
        if (v <= 0) {
            str_cpy(val, "OFF", 6);
        } else {
            fmt_fix(val, LEVEL_DB_X10[v], 1);
            *unit = "dB";
        }
        break;
    case F_SEMI:
        fmt_int(val, v);
        if (v > 0) {
            char t[8];
            fmt_int(t, v);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "st";
        break;
    case F_ENUM:
        str_cpy(val, d->names[v < d->min ? d->min : v > d->max ? d->max : v], 6);
        if (d->unit)
            *unit = d->unit;
        break;
    case F_BPM:
        fmt_int(val, v);
        *unit = "BPM";
        break;
    case F_NOTE:
        str_cpy(val, N_NOTE[v % 12], 6);
        break;
    case F_ONOFF:
        str_cpy(val, N_ONOFF[v ? 1 : 0], 6);
        break;
    case F_STEPS:
        fmt_int(val, v);
        *unit = "STEP";
        break;
    default:
        if (d->names) {                               /* F_INT with a 0-terminated name list: the range */
            uint32_t k = 0;                           /* split evenly over the names (engine desc hooks) */
            while (d->names[k])
                k++;
            str_cpy(val, d->names[(uint32_t)(clamp(v, d->min, d->max) - d->min) * k / (uint32_t)(d->max - d->min + 1)], 6);
        } else {
            fmt_int(val, v);
        }
        if (d->unit)
            *unit = d->unit;
        break;
    }
}

/* ------------------------------------------------------------ pages --- */
enum { FAM_HOME, FAM_ENV, FAM_LFO, FAM_FX, FAM_SCL, FAM_EDIT, FAM_GLO, FAM_SAVE, FAM_ARP, FAM_SEQ, FAM_TRK,
       FAM_COUNT };
enum { SC_TRACK, SC_GLOBAL, SC_ENGINE, SC_STEP, SC_TRK };   /* SC_TRK: the TRACKS page (ui_input.c tracks_edit) */
enum { GR_NONE, GR_ADSR, GR_LFO, GR_STEPS, GR_ARP, GR_SCALE, GR_FX, GR_ROLL, GR_BROWSE, GR_SLOTS, GR_USER, GR_TRK,
       GR_SLCR, GR_MOD, GR_PATS, GR_SONG, GR_CLEAR, GR_CHORD, GR_EVENTS,
       GR_FMOP, GR_FMOP2, GR_FMENV, GR_INS, GR_DESTS, GR_IORD, GR_PUNCH, GR_DXSND, GR_GENERATION };
/* page.sec: button cycles sections; SELECT walks pages that share fam+sec and stops at the ends
 * (ui.c open_family / select_page_step) */
enum { FX_SEC_INS = 0, FX_SEC_PUNCH = 1, FX_NSEC = 2,
       SEQ_SEC_STEP = 0, SEQ_SEC_GEN = 1, SEQ_SEC_AUTO = 2, SEQ_SEC_TOOL = 3, SEQ_NSEC = 4,
       EDIT_SEC_SND = 0, EDIT_SEC_KIT = 1, EDIT_SEC_LAN = 2, EDIT_SEC_VOX = 3, EDIT_NSEC = 4,
       GLO_SEC_MASTER = 0, GLO_SEC_CLK = 1, GLO_SEC_SONG = 2, GLO_NSEC = 3,
       SAVE_SEC_PRE = 0, SAVE_SEC_PROJ = 1, SAVE_SEC_CLEAR = 2, SAVE_NSEC = 3,
       LFO_SEC_LFO = 0, LFO_SEC_MOD = 1, LFO_NSEC = 2,
       TRK_SEC_MIX = 0 };

typedef struct {
    const char *title;
    uint8_t fam, scope, graph, sec;
    uint8_t id[4];               /* param ids; 0xFF = empty slot */
} page_t;
#define PG_STRUM 0xFCu           /* CHORD KNOB 3: the packed P_ALLOC sweep's rate */
#define PG_STROKE 0xFDu          /* CHORD KNOB 4: the packed P_ALLOC sweep's DOWN / UP / ALT */

static const page_t PAGES[] = {
    {"ENV", FAM_ENV, SC_TRACK, GR_ADSR, 0, {P_ATK, P_DEC, P_SUS, P_REL}},
    {"ENV DEST", FAM_ENV, SC_TRACK, GR_NONE, 0, {P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ESYNC}},   /* knob cards + scope (P_ED_FX unused);
                                                                             * 1.5: ESYNC, ENV's times as note values */
    {"LFO", FAM_LFO, SC_TRACK, GR_LFO, LFO_SEC_LFO, {P_LRATE, P_LWAVE, P_LPHASE, P_LFADE}},
    {"LFO 2", FAM_LFO, SC_TRACK, GR_LFO, LFO_SEC_LFO, {P_LSYNC, P_LTRIG, P_LPOL, P_LSMOOTH}},   /* 1.2: SYNC
                                                                             * TRIG POL; S&H: K4 SMOOTH */
    {"LFO DEST", FAM_LFO, SC_TRACK, GR_NONE, LFO_SEC_LFO, {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP}},
    {"MOD", FAM_LFO, SC_TRACK, GR_MOD, LFO_SEC_MOD, {0xFF, P_M1SRC, P_M1DST, P_M1AMT}},   /* its own section: LFO button cycles here */
    /* FX: Effects → Punch-in. SLICER is an EFFECTS page (open from the list / SELECT).
     * SELECT order follows the EFFECTS list (fx_list), not this table order. Each stage's mix
     * lives on EFFECTS (K4). K3 is USE AS: CHAIN, SEND or MASTER (TAPE). DELAY knob 4: left TIME, right RATE. */
    {"EFFECTS", FAM_FX, SC_TRACK, GR_IORD, FX_SEC_INS, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"REVERB", FAM_FX, SC_GLOBAL, GR_NONE, FX_SEC_INS, {G_RTYPE, G_RSIZE, G_RDAMP, 0xFF}},
    {"DELAY", FAM_FX, SC_GLOBAL, GR_NONE, FX_SEC_INS, {G_DTIME, G_DFDBK, G_DCOLOR, 0xFF}},
    {"DISTORT", FAM_FX, SC_TRACK, GR_INS, FX_SEC_INS, {P_IDMODE, P_DIST, P_ED_FX, 0xFF}},
    {"CRUSH", FAM_FX, SC_TRACK, GR_INS, FX_SEC_INS, {P_ICBITS, P_ICRATE, P_ICLPF, 0xFF}},
    {"FILTER", FAM_FX, SC_TRACK, GR_INS, FX_SEC_INS, {P_IFTYPE, P_IFCUT, P_IFRES, 0xFF}},
    {"PHASER", FAM_FX, SC_TRACK, GR_INS, FX_SEC_INS, {P_IPRATE, P_IPDEP, P_IPFBK, 0xFF}},
    {"FLANGER", FAM_FX, SC_TRACK, GR_INS, FX_SEC_INS, {P_ILRATE, P_ILDEP, 0xFF, 0xFF}},
    {"CHORUS", FAM_FX, SC_TRACK, GR_INS, FX_SEC_INS, {P_IHRATE, P_IHDEP, 0xFF, 0xFF}},
    {"SLICER", FAM_FX, SC_TRACK, GR_SLCR, FX_SEC_INS, {P_SLCR, P_SLPAT, P_SLRATE, 0xFF}},
    {"TAPE", FAM_FX, SC_GLOBAL, GR_NONE, FX_SEC_INS, {G_TSPEED, G_TWOW, G_TDROP, 0xFF}},
    {"PUNCH-IN", FAM_FX, SC_TRACK, GR_PUNCH, FX_SEC_PUNCH, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"SCL", FAM_SCL, SC_TRACK, GR_SCALE, 0, {P_ROOT, P_SCALE, P_QUANT, P_TRANS}},
    {"CHORD", FAM_SCL, SC_TRACK, GR_CHORD, 0, {P_CHRD, P_VOIC, PG_STRUM, PG_STROKE}},
    {"EDIT 1", FAM_EDIT, SC_ENGINE, GR_NONE, EDIT_SEC_SND, {P_E0, P_E1, P_E2, P_E3}},
    {"EDIT 2", FAM_EDIT, SC_ENGINE, GR_NONE, EDIT_SEC_SND, {P_E4, P_E5, P_E6, P_E7}},
    /* ANALOG: page 2 FILTER (TYPE CUT RES KTR), page 3 FILTER DRIVE (DRV); EDIT 2 hidden (ui.c) */
    {"FILTER", FAM_EDIT, SC_TRACK, GR_NONE, EDIT_SEC_SND, {P_FTYPE, P_E4, P_E5, P_E7}},
    {"FILTER DRIVE", FAM_EDIT, SC_TRACK, GR_NONE, EDIT_SEC_SND, {P_E6, 0xFF, 0xFF, 0xFF}},
    /* DRUM-X kit (ui_dx.c): SOUND / SOUND 2 (SOUND 3 / X-MOD retired; FUN11 still stores dx_mot) */
    {"SOUND", FAM_EDIT, SC_TRACK, GR_DXSND, EDIT_SEC_KIT, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"SOUND 2", FAM_EDIT, SC_TRACK, GR_DXSND, EDIT_SEC_KIT, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"LANES", FAM_EDIT, SC_TRACK, GR_NONE, EDIT_SEC_LAN, {P_LN0, P_LN1, P_LN2, P_LN3}},
    {"LANES 2", FAM_EDIT, SC_TRACK, GR_NONE, EDIT_SEC_LAN, {P_LN4, P_LN5, P_LN6, P_LN7}},
    {"OPERATOR", FAM_EDIT, SC_TRACK, GR_FMOP, EDIT_SEC_SND, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"OP ENV", FAM_EDIT, SC_TRACK, GR_FMENV, EDIT_SEC_SND, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"OP1 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, EDIT_SEC_SND, {P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL}},
    {"OP2 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, EDIT_SEC_SND, {P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL}},
    {"OP3 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, EDIT_SEC_SND, {P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL}},
    {"OP4 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, EDIT_SEC_SND, {P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL}},
    {"OP LEVEL", FAM_EDIT, SC_TRACK, GR_NONE, EDIT_SEC_SND, {P_FM1_LEVEL, P_FM2_LEVEL, P_FM3_LEVEL, P_FM4_LEVEL}},
    /* VOICE: one page (VCE GLD UNI DTN SPRD). GLMOD / PRIO / ALLOC stay stored, not shown; PAN/PLAY on MIXER */
    {"VOICE", FAM_EDIT, SC_TRACK, GR_NONE, EDIT_SEC_VOX, {P_VOICE, P_GLIDE, P_DETUNE, P_SPRD}},

    /* GLO: Master → Clock → Song (CLICK / COUNT-IN / LEVEL: MENU > AUDIO) */
    {"MASTER", FAM_GLO, SC_GLOBAL, GR_NONE, GLO_SEC_MASTER, {G_CLIP, G_PUNCH, G_DUCK, G_DREL}},
    {"CLOCK", FAM_GLO, SC_GLOBAL, GR_NONE, GLO_SEC_CLK, {G_BPM, G_SWING, G_CLOCK, 0xFF}},
    {"SONG", FAM_GLO, SC_GLOBAL, GR_SONG, GLO_SEC_SONG, {0xFF, 0xFF, 0xFF, 0xFF}},
    /* SAVE: Presets (PRESETS / USER / PHRASES) → Project → Clear (sticky key map) */
    {"PRESETS", FAM_SAVE, SC_GLOBAL, GR_BROWSE, SAVE_SEC_PRE, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"USER", FAM_SAVE, SC_GLOBAL, GR_USER, SAVE_SEC_PRE, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"PHRASES", FAM_SAVE, SC_GLOBAL, GR_PATS, SAVE_SEC_PRE, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"PROJECT", FAM_SAVE, SC_GLOBAL, GR_SLOTS, SAVE_SEC_PROJ, {G_SLOT, 0xFF, G_LOAD, G_SAVE}},
    {"CLEAR", FAM_SAVE, SC_GLOBAL, GR_CLEAR, SAVE_SEC_CLEAR, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"ARP", FAM_ARP, SC_TRACK, GR_ARP, 0, {P_AMODE, P_ARATE, P_AOCT, P_AGATE}},
    {"ARP 2", FAM_ARP, SC_TRACK, GR_NONE, 0, {P_ASWING, P_APROB, P_AHOLD, P_AORDER}},
    /* SEQ: Step group → Generation (drum kits) → Automation → Tools. SELECT walks each section. */
    {"STEP", FAM_SEQ, SC_STEP, GR_ROLL, SEQ_SEC_STEP, {0, 1, 2, 3}},
    {"PATTERN", FAM_SEQ, SC_TRACK, GR_STEPS, SEQ_SEC_STEP, {P_SLEN, P_SDIV, P_SSWING, P_SGATE}},
    {"DETAIL", FAM_SEQ, SC_STEP, GR_ROLL, SEQ_SEC_STEP, {0, 1, 2, 3}},
    {"GENERATION", FAM_SEQ, SC_TRACK, GR_GENERATION, SEQ_SEC_GEN, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"GENERATION 2", FAM_SEQ, SC_TRACK, GR_GENERATION, SEQ_SEC_GEN, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"AUTOMATION", FAM_SEQ, SC_TRACK, GR_EVENTS, SEQ_SEC_AUTO, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"SEQ TOOLS", FAM_SEQ, SC_TRACK, GR_STEPS, SEQ_SEC_TOOL, {P_SLEN, P_SDIV, P_SSWING, P_SGATE}},
    /* HOME: Mixer; ui.home is its own section (home_tap). Drum lane mutes live in SEQ TOOLS. */
    {"MIXER", FAM_TRK, SC_TRK, GR_TRK, TRK_SEC_MIX, {0, 1, 2, 3}},
    {"PUNCH-IN", FAM_FX, SC_TRACK, GR_PUNCH, FX_SEC_PUNCH, {0xFF, 0xFF, 0xFF, 0xFF}},   /* the section's 2/2 (same
                                                     * title, the footer's count tells); its map is perform.c PF2_DEF */
};
#define NPAGES (sizeof(PAGES) / sizeof(PAGES[0]))
static uint8_t mod_ui_slot;      /* the MOD page: the matrix slot (0..3) KNOB 2..4 edit */

/* MAP = CURR: ROOT / SCL shared by every track whose MAP is CURR (MAP stays each track's own) */
static void scale_lock_stash(track_t *t)
{
    if (t->scl_stash_ok)
        return;
    t->scl_stash[0] = t->p[P_ROOT];
    t->scl_stash[1] = t->p[P_SCALE];
    t->scl_stash_ok = 1;
}
static void scale_lock_restore(track_t *t)
{
    if (!t->scl_stash_ok)
        return;
    t->p[P_ROOT] = t->scl_stash[0];
    t->p[P_SCALE] = t->scl_stash[1];
    t->scl_stash_ok = 0;
}
static void scale_lock_copy(track_t *dst, const track_t *src)
{
    dst->p[P_ROOT] = src->p[P_ROOT];
    dst->p[P_SCALE] = src->p[P_SCALE];
}
static void scale_lock_push(track_t *src)
{
    uint32_t k;
    if (src->p[P_QUANT] != QN_CURR)
        return;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        if (t == src || t->p[P_QUANT] != QN_CURR)
            continue;
        scale_lock_copy(t, src);
    }
}
static void scale_lock_after(track_t *t, uint32_t id)
{
    if (id == P_QUANT) {                              /* entering CURR pushes, leaving restores */
        if (t->p[P_QUANT] == QN_CURR) {
            uint32_t k;
            for (k = 0; k < NTRK; k++) {
                track_t *u = &trk[k];
                if (u == t || u->p[P_QUANT] != QN_CURR)
                    continue;
                scale_lock_stash(u);
                scale_lock_copy(u, t);
            }
        } else
            scale_lock_restore(t);
    } else if (id == P_ROOT || id == P_SCALE)
        scale_lock_push(t);
}

static const param_desc_t STRUM_DESC = PE("STRUM", N_STRUM, 0);
static const param_desc_t STROKE_DESC = PE("STROKE", N_STROKE, 0);
static int16_t sweep_tmp;                /* (read at once: the draw and the knob handler use one slot at a time) */

static const param_desc_t *page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t id = pg->id[slot], mode;
    if (id == 0xFFu) {
        *valp = 0;
        return 0;
    }
    if (id == PG_STRUM) {
        sweep_tmp = (int16_t)sweep_rate(TSEL);
        *valp = &sweep_tmp;
        return &STRUM_DESC;
    }
    if (id == PG_STROKE) {
        sweep_tmp = (int16_t)sweep_stroke(TSEL);
        *valp = &sweep_tmp;
        return &STROKE_DESC;
    }
    /* LFO 2 KNOB 4: SMOOTH, only while WAVE is S&H (blank otherwise; the value stays stored) */
    if (id == P_LSMOOTH && TSEL->p[P_LWAVE] != 4) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_STEP || pg->scope == SC_TRK) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_GLOBAL) {
        /* REVERB: selected track owns CHAIN → edit CHAIN tank globals; else SEND. */
        if (pg->id[0] == G_RTYPE && slot < 3u) {
            int32_t own = song.g[G_REVOWN];
            if (own >= 1 && own <= (int32_t)NTRK && own - 1 == (int32_t)(TSEL - trk)) {
                static const uint8_t CR[3] = {G_CRTYPE, G_CRSIZE, G_CRDAMP};
                id = CR[slot];
            }
        }
        *valp = &song.g[id];
        return &GP[id];
    }
    if (pg->graph == GR_MOD)                          /* SRC DST AMT of the slot shown */
        id += 3u * mod_ui_slot;
    /* VOICE page: blank knobs the mode does not use (values stay stored). UNI DTN = UNISON only;
     * SPRD = POLY / UNISON (fx.c mix_spread). MONO / LEG: neither. */
    mode = (uint32_t)TSEL->p[P_VOICE];
    if (id == P_DETUNE && mode != V_UNISON) {
        *valp = 0;
        return 0;
    }
    if (id == P_SPRD && mode != V_POLY && mode != V_UNISON) {
        *valp = 0;
        return 0;
    }
    /* INSERT/SLICER stage knobs (not MIX): SEND edits the shared bus; CHAIN edits the track. */
    {
        uint32_t st = 0xFFu;
        int send = 0;
        if (id == P_IDMODE || id == P_DIST || id == P_ED_FX) st = INS_DIST;
        else if (id == P_IFTYPE || id == P_IFCUT || id == P_IFRES) st = INS_FILTER;
        else if (id == P_ICBITS || id == P_ICRATE || id == P_ICLPF) st = INS_CRUSH;
        else if (id == P_IPRATE || id == P_IPDEP || id == P_IPFBK) st = INS_PHASR;
        else if (id == P_ILRATE || id == P_ILDEP) st = INS_FLANG;
        else if (id == P_IHRATE || id == P_IHDEP) st = INS_CHOR;
        else if (id == P_SLCR || id == P_SLPAT || id == P_SLRATE) st = INS_SLCR;
        if (st == INS_SLCR)
            send = sl_trk_send(TSEL);
        else if (st < INS_N)
            send = ((uint16_t)TSEL->p[P_IORD] & (uint16_t)(1u << (10u + st))) != 0;
        if (st != 0xFFu && send) {
            *valp = &fx_bus_trk.p[id];
            return track_desc(TSEL, id);
        }
    }
    *valp = &TSEL->p[id];
    return track_desc(TSEL, id);
}
