/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host harness for the punch-in crackle: load FUN* project blobs (from a backup/project JSON
 * extract, or the synthetic heavy mix), hold punch-in stacks (pairs, triples, worst case; key-held
 * and FX LATCH), and report cost vs the half-buffer deadline, a click/discontinuity measure, and a
 * shed-threshold A/B (85/92 % current vs 96/98 % relaxed).
 *
 *   build/host/punch_crackle_test [DIR_OF_BINS ...]
 *   CRACKLE_DIR=build/crackle_projs builds the survey table when DIR args are omitted.
 *
 * Deadline projection: host cycle count (Linux rdtsc, macOS ri_cycles) x2, converted with the perform_test
 * calibration (100 host cycles / sample ≈ 1.7 % of the device half). Device half = 2902 us
 * (HALF_FRAMES * 1e6 / FS). Always asserts on the synthetic mix; the survey prints only.
 */
#include <stdint.h>
/* empty USR1..3 sample/wavetable slots (wt_test.c): a WT track must not read unmapped XIP */
static uint8_t host_smp_slots[3][0x14000] __attribute__((aligned(4)));
#define SMP_USER_XIP(k) (host_smp_slots[(k)])
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
#include "../firmware/src/project.c"

#include <dirent.h>
#include <errno.h>
#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/syscall.h>
#endif
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define HALF_US (HALF_FRAMES * 1000000u / FS)          /* 2902 us at 44.1 kHz */
#define IPC_TO_PCT 0.017                               /* perform_test: 100 ipc ≈ 1.7 % device */
#define SETTLE_BLK (FS / (4u * CTL))                   /* 0.25 s */
#define MEAS_BLK (FS / (2u * CTL))                     /* 0.5 s */
#define DRUM_HITS 0xFFu

static int bad;
static int check(const char *what, int ok)
{
    printf("punch: %-72s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
    return ok ? 0 : 1;
}

/* ---------------------------------------------- instruction / cycle count --- */
static int ipc_kind;                                   /* 0 none, 1 instr, 2 cycles */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri)) {
        ipc_kind = 2;
        return ri.ri_cycles;                /* cycles, as rdtsc below: IPC_TO_PCT is a cycle calibration */
    }
#endif
#ifdef __linux__
    {
        static int fd = -2;
        if (fd == -2) {
            struct perf_event_attr pe;
            memset(&pe, 0, sizeof pe);
            pe.type = PERF_TYPE_HARDWARE;
            pe.size = sizeof pe;
            pe.config = PERF_COUNT_HW_INSTRUCTIONS;
            pe.disabled = 0;
            pe.exclude_kernel = 1;
            pe.exclude_hv = 1;
            fd = (int)syscall(__NR_perf_event_open, &pe, 0, -1, -1, 0);
        }
        if (fd >= 0) {
            uint64_t v = 0;
            if (read(fd, &v, sizeof v) == (ssize_t)sizeof v) {
                ipc_kind = 1;
                return v;
            }
        }
    }
#endif
#if defined(__x86_64__) || defined(__i386__)
    {
        uint32_t lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        ipc_kind = 2;                                  /* cycles as a conservative instruction proxy */
        return ((uint64_t)hi << 32) | lo;
    }
#else
    return 0;
#endif
}

static double pct_from_ipc(double ipc)
{
    /* conservative: instruction (or cycle) count x2 before the % conversion */
    return (ipc * 2.0) * IPC_TO_PCT;
}

/* ----------------------------------------------------------- project apply --- */
static int host_apply_project(const project_t *input)
{
    project_t p;
    uint32_t i, k;
    memcpy(&p, input, sizeof p);
    if (!proj_engines_ok(&p))
        return -1;
    memset(drum_mute, 0, sizeof drum_mute);
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    if (dx_kit_ok(p.dx))
        dx_kit_unpack(dx_kit, dx_lay, dx_mdl, p.dx);
    else
        dx_kit_load(0);
    fx_bus_from_wire(p.fx_bus);
    dx_mute_set(p.dx_mute);
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    seq_stop();
    transport_req = 0;
    chain_config = p.chain;
    motion = p.motion;
    memset(motion_active, 0, sizeof motion_active);
    motion_base_valid = 0;
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_LOAD && i != G_SAVE)
            song.g[i] = i == G_DTIME ? dtime_load(p.g[i])
                                     : (int16_t)clamp(p.g[i], GP[i].min, GP[i].max);
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p.t[k];
        uint32_t e = s->engine < NENGINES ? s->engine : 0u;
        memset(t->v, 0, sizeof t->v);                 /* clean voice state: LOFI arp index etc. */
        t->xf_on = 0;
        t->eng_req = (uint8_t)e;
        t->engine = (uint8_t)eng_idx(e);              /* publish at once on the host */
        t->user = 0;
        for (i = 0; i < P_COUNT; i++) {
            const param_desc_t *d = param_desc_of(e, i);
            t->p[i] = (int16_t)param_fit(d, s->p[i]);
        }
        /* keep engine params in range for tables (P_E6 LOFI arp is 0..3) */
        for (i = P_E0; i <= P_E7; i++)
            if (t->p[i] < 0) t->p[i] = 0;
        t->preset = (uint8_t)(ENGINES[e]->npresets
                                  ? (s->preset >= PROJ_DEF_KEEP ? 0u : s->preset) % ENGINES[e]->npresets
                                  : 0u);
        memcpy(t->step, s->step, sizeof t->step);
        t->fx_sl_pos = s->fx_sl_pos < FX_LIST_N ? s->fx_sl_pos : 8u;
        memcpy(fx_order_tbl[k], s->fx_order, FX_LIST_N);
        if (!proj_order_ok(fx_order_tbl[k]))
            fx_order_default((uint16_t)t->p[P_IORD], t->p[P_CHOR], t->fx_sl_pos, fx_order_tbl[k]);
        fx_order_accept(t);
        {
            uint8_t v[FP_SIZE + 1u];
            fm6_unpack(p.fm6[k], v);
            fm6_set_patch(k, v);
            fm6_adopt(k);
        }
    }
    song.sel = (uint8_t)(p.sel < NTRK ? p.sel : 0u);
    memcpy(fx_master_tr, p.fx_master, FX_LIST_N);
    fx_master_sanitize();
    if (p.fx_master_order && p.fx_master_order <= NTRK)
        fx_master_order_tr = (uint8_t)(p.fx_master_order - 1u);
    return 0;
}

static int load_project_bin(const char *path, project_t *out, char *err, size_t errn)
{
    project_store_t st;
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) {
        snprintf(err, errn, "open: %s", strerror(errno));
        return 0;
    }
    n = fread(st.raw, 1, sizeof st.raw, f);
    fclose(f);
    if (n != sizeof st.raw) {
        snprintf(err, errn, "short read %zu", n);
        return 0;
    }
    if (!proj_import(out, &st, (int)sizeof st)) {
        uint32_t m;
        memcpy(&m, st.raw, 4);
        snprintf(err, errn, "proj_import refused magic=0x%08x", m);
        return 0;
    }
    return 1;
}

/* -------------------------------------------------------------- punch ops --- */
typedef struct {
    const char *name;
    uint8_t n;
    int8_t e[8];                                       /* PF_* ids; -1 ends */
    int latch;                                         /* 1: FX LATCH sticky instead of key-held */
    int crush, throwm, depth;                          /* macros while held */
} punch_stack_t;

static const punch_stack_t STACKS[] = {
    {"idle", 0, {0}, 0, 0, 0, 0},
    {"R16", 1, {PF_R16}, 0, 0, 0, 0},
    {"FRZ", 1, {PF_FRZ}, 0, 0, 0, 0},
    {"GLT", 1, {PF_GLT}, 0, 0, 0, 0},
    {"R16+LPF", 2, {PF_R16, PF_LPF}, 0, 0, 0, 0},
    {"R16+FLG", 2, {PF_R16, PF_FLG}, 0, 0, 0, 0},
    {"REV+PHS", 2, {PF_REV, PF_PHS}, 0, 0, 0, 0},
    {"TAPE+HPF", 2, {PF_TAPE, PF_HPF}, 0, 0, 0, 0},
    {"OUP+R16", 2, {PF_OUP, PF_R16}, 0, 0, 0, 60},
    {"RTHR+ETHR", 2, {PF_RTHR, PF_ETHR}, 0, 0, 0, 0},
    {"R16+LPF+HPF", 3, {PF_R16, PF_LPF, PF_HPF}, 0, 0, 0, 0},
    {"FRZ+FLG+PHS", 3, {PF_FRZ, PF_FLG, PF_PHS}, 0, 0, 0, 0},
    {"GLT+RTHR+ETHR", 3, {PF_GLT, PF_RTHR, PF_ETHR}, 0, 0, 0, 0},
    {"R16+LPF+CRUSH", 3, {PF_R16, PF_LPF, PF_HPF}, 0, 100, 0, 0},
    {"worst", 8, {PF_R16, PF_LPF, PF_HPF, PF_FLG, PF_PHS, PF_RTHR, PF_ETHR, PF_OUP}, 0, 100, 100, 60},
    {"worst/HOLD", 8, {PF_R16, PF_LPF, PF_HPF, PF_FLG, PF_PHS, PF_RTHR, PF_ETHR, PF_OUP}, 1, 100, 100, 60},
    {"MIDI+R16", 4, {PF_R16, PF_OCTU, PF_HALF, PF_X2}, 0, 0, 0, 0},
};

static void perf_clear_all(void)
{
    uint32_t k;
    for (k = 0; k < PF_N; k++)
        perf_press(k, 0);
    perf_latched = 0;
    perf_latch_on = 0;
    perf_kill = 0;
    perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
}

static void stack_engage(const punch_stack_t *s)
{
    uint32_t i;
    perf_clear_all();
    perf_k[1] = (int8_t)s->crush;
    perf_k[2] = (int8_t)s->throwm;
    perf_k[3] = (int8_t)s->depth;
    if (s->latch) {
        perf_latch_on = 1;
        for (i = 0; i < s->n; i++) {
            perf_press((uint32_t)s->e[i], 1);
            perf_press((uint32_t)s->e[i], 0);           /* sticky via perf_latched */
        }
    } else {
        for (i = 0; i < s->n; i++)
            perf_press((uint32_t)s->e[i], 1);
    }
}

/* shed A/B: count how many measured halves would request a shed under each policy */
typedef struct {
    uint32_t shed_cur, shed_rel;                       /* voice-shed requests */
    uint32_t over_cur, over_rel;                       /* halves over the pair threshold */
    uint32_t miss_cur, miss_rel;                       /* halves at/over 100 % */
} shed_t;

static void shed_sim(double pct, shed_t *o)
{
    /* one measurement window ≈ many halves; treat the window's projected % as sustained */
    if (pct > 85.0) {
        o->over_cur++;
        o->shed_cur++;                                 /* two halves in a row at 85 % → shed */
    }
    if (pct > 92.0)
        o->shed_cur++;                                 /* single-half 92 % also sheds (already counted) */
    if (pct > 96.0) {
        o->over_rel++;
        o->shed_rel++;
    }
    if (pct > 98.0)
        o->shed_rel++;
    if (pct > 100.0) {
        o->miss_cur++;
        o->miss_rel++;
        o->shed_cur++;                                 /* 100 % asks for two */
        o->shed_rel++;
    }
}

typedef struct {
    double ipc, ns, pct;
    int32_t click;                                     /* max |Δ| during hold after settle */
    int32_t local;                                     /* max |Δ| in 6 ms before press (bound) */
    uint32_t engaged;
    shed_t shed;
} run_t;

static void synth_heavy(void)
{
    static const uint8_t PARTS[NPART][3] = {{12, 0, 8}, {2, 0, 8}, {5, 0, 4}, {10, DRUM_HITS, 0}};
    static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    static const int MIX[] = {P_IDMIX, P_IFMIX, P_ICMIX, P_IPMIX, P_ILMIX, P_IHMIX};
    uint32_t p, i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    for (p = 0; p < NPART; p++) {
        host_preset(&trk[p], PARTS[p][0], PARTS[p][1]);
        if (PARTS[p][2] == DRUM_HITS)
            continue;
        trk[p].p[P_VOICE] = V_POLY;
        trk[p].p[P_SUS] = 127;
        trk[p].p[P_AMODE] = 0;
        for (i = 0; i < PARTS[p][2]; i++)
            trk_note_on(&trk[p], NOTES[i] + 12u * p, 100);
    }
    for (p = 0; p < NPART; p++) {
        for (i = 0; i < sizeof MIX / sizeof MIX[0]; i++)
            trk[p].p[MIX[i]] = 100;
        trk[p].p[P_DLY] = 100;
        trk[p].p[P_REV] = 100;
    }
    song.g[G_BPM] = 120;
}

static void drum_maybe(uint32_t blk, int drums)
{
    if (drums && (blk * CTL) % (FS / 8u) < CTL)
        drum_hit((blk * CTL) / (FS / 8u));
}

/* the FX layer cleared and the transport started: the start of measure_stack and punch_churn */
static void punch_start(void)
{
    perf_clear_all();
    memset(&pf, 0, sizeof pf);
    pf.src = pf.next = PF_N;
    pf.lc = PF_TOP;
    pf.mg[0] = pf.mg[1] = pf.mg[2] = pf.mg[3] = 32768;
    sl_lent = 0;
    transport_req = 1;
    song.playing = 1;
    perf_start();
}

/* a punch-in at work this block: its stage, filter, macro, mute or a latched bit */
static int pf_engaged(void)
{
    return perf_act || pf.mode != BM_NONE || pf.w || pf.la || pf.ha || pf.cw || pf.fw || pf.pw || pf.thr ||
           pf.mute || perf_latched;
}

static run_t measure_stack(const punch_stack_t *s, int drums)
{
    run_t r;
    int32_t o[2 * CTL], prev_l = 0;
    uint64_t best_ns = ~0ull, acc_ns = 0, acc_i = 0, best_i = ~0ull;
    uint32_t k, seen = 0, seg = MEAS_BLK / 4u, nblk = SETTLE_BLK + MEAS_BLK;
    int32_t mx = 0, local = 0;
    memset(&r, 0, sizeof r);
    punch_start();
    for (k = 0; k < nblk; k++) {
        uint64_t t0, ii0;
        drum_maybe(k, drums);
        if (k == SETTLE_BLK - (6u * FS / 1000u) / CTL) {
            /* 6 ms before press: local bound for kick/transient click tests */
            local = 0;
            prev_l = o[0];
        }
        if (k == SETTLE_BLK)
            stack_engage(s);
        t0 = now_ns();
        ii0 = instr_now();
        mix_block(o, CTL);
        if (k >= SETTLE_BLK) {
            acc_ns += now_ns() - t0;
            if (ii0)
                acc_i += instr_now() - ii0;
            if ((k - SETTLE_BLK) % seg == seg - 1u) {
                if (acc_ns < best_ns)
                    best_ns = acc_ns;
                if (acc_i && acc_i < best_i)
                    best_i = acc_i;
                acc_ns = 0;
                acc_i = 0;
            }
            if (pf_engaged())
                seen = 1;
            {
                uint32_t i;
                for (i = 0; i < CTL; i++) {
                    int32_t d = o[2u * i] - prev_l;
                    if (d < 0)
                        d = -d;
                    if (d > mx)
                        mx = d;
                    prev_l = o[2u * i];
                }
            }
        } else if (k + (6u * FS / 1000u) / CTL >= SETTLE_BLK) {
            uint32_t i;
            for (i = 0; i < CTL; i++) {
                int32_t d = o[2u * i] - prev_l;
                if (d < 0)
                    d = -d;
                if (d > local)
                    local = d;
                prev_l = o[2u * i];
            }
        } else {
            prev_l = o[0];
        }
    }
    r.ns = (double)best_ns / (double)(seg * CTL);
    r.ipc = (best_i != ~0ull && instr_now()) ? (double)best_i / (double)(seg * CTL) : 0;
    r.pct = r.ipc ? pct_from_ipc(r.ipc) : 0;
    r.click = mx;
    r.local = local;
    r.engaged = s->n == 0 ? 1 : seen;
    shed_sim(r.pct, &r.shed);
    perf_clear_all();
    transport_req = 2;
    return r;
}

/* ---------------------------------------------------------------- churn --- */
/* Random punch-in play on the loaded mix. Every 1..32 blocks (3..93 ms) one move: a key down (any PF_* below PF_N:
 * audio, MIDI, mutes), a key up, the MENU visited to flip FX LATCH (as ui_input.c: perf_kill until the keys are let
 * go, the latched ones and the macros off), G5 CLR, or a knob macro. Then CLR with LATCH on, every key up, 0.5 s:
 * nothing may still be at work and every track's parameters are as before (pfx.c: "let go, all is as it was").
 * Clicks: the same mix is rendered first without punch-ins (churn_setup loads it both times). Each block of the
 * moves is held to churn_limit: CHURN_CLICK times the dry mix's largest second difference (max_d2) over the second up
 * to it (the furthest back a REPEAT or REVERSE replays), or the window's peak / CHURN_FLOOR when that is more: a
 * splice stands out against what the music itself does at that moment, and on a pure tone, whose max_d2 is tiny, it
 * still has to be a sizeable jump. Blocks with a crusher at work are left out (pf_crushing: its held samples are
 * steps by design, 4x the limit; perform_test checks CRUSH on its own). Measured (peak ~17000): the heavy mix's worst
 * block 1.7x its dry window; a
 * lone 220 Hz sine's OCT+ (the voice an octave up at once) 310 and the LPF's fade-in 151; with the buffer's crossfade
 * taken out (pf.w jumping to pf.tw) 9129, 5221 and, on a project, 3049: each over both bounds.
 * Its own xorshift, not firmware rng(): the DSP draws from that one, so a changed engine would change the moves
 * and a failing seed would stop reproducing. PUNCH_SEED=<n> runs that seed alone. */
#define CHURN_PRE (FS / CTL)                           /* 1 s before the first move: the dry window's length */
#define CHURN_BLK (4u * FS / CTL)                      /* 4 s of moves */
#define CHURN_END (FS / (2u * CTL))                    /* 0.5 s after the last key is up */
#define CHURN_CLICK 3                                  /* a block's max_d2 at most this times the dry window's */
#define CHURN_FLOOR 16                                 /* .. or the window's peak / this (-24 dB), whichever is more */
static const uint32_t CHURN_SEEDS[] = {1u, 0x2545F491u, 0xC0FFEEu, 0xDEADBEEFu, 77u, 0x9E3779B9u};
static void (*churn_setup)(void);                      /* loads the mix: synth_heavy, or the survey's project */
static struct { int32_t d2, pk; } churn_dry[CHURN_PRE + CHURN_BLK];   /* the dry mix, each block: max_d2, peak */

typedef struct {
    int32_t click, lim, d2, pk;                        /* the worst block's max_d2, its churn_limit, the window's d2, peak */
    double ipc, ns, pct;                               /* the worst 1/8 s of the moves, per sample (ipc 0: no counter) */
    uint32_t moves;
    uint32_t stuck;                                    /* 1: at work after CLR + every key up; 2: track params changed */
} churn_t;

static uint32_t churn_rng;
static uint32_t churn_rnd(uint32_t n)
{
    churn_rng ^= churn_rng << 13;
    churn_rng ^= churn_rng >> 17;
    churn_rng ^= churn_rng << 5;
    return churn_rng % n;
}

/* the seeds to run: PUNCH_SEED's alone, else CHURN_SEEDS */
static uint32_t churn_seeds(const uint32_t **seeds)
{
    static uint32_t one;
    const char *env = getenv("PUNCH_SEED");
    if (env && *env) {
        one = (uint32_t)strtoul(env, 0, 0);
        if (!one) {
            fprintf(stderr, "punch: PUNCH_SEED=%s: xorshift needs a nonzero seed\n", env);
            exit(2);
        }
        *seeds = &one;
        return 1;
    }
    *seeds = CHURN_SEEDS;
    return sizeof CHURN_SEEDS / sizeof CHURN_SEEDS[0];
}

/* the largest second difference |x[n] - 2 x[n-1] + x[n-2]| of a block, L and R, carried on from the last two samples
 * (prev[c][0] the last, prev[c][1] the one before). Small on smooth audio, the jump itself at a splice; the plain step
 * is not enough: a dense mix (FM at full poly) steps as far as a missing crossfade does */
static int32_t max_d2(const int32_t *o, int32_t prev[2][2])
{
    uint32_t i, c;
    int32_t mx = 0;
    for (i = 0; i < CTL; i++)
        for (c = 0; c < 2u; c++) {
            int32_t x = o[2u * i + c], d = x - 2 * prev[c][0] + prev[c][1];
            if (d < 0)
                d = -d;
            if (d > mx)
                mx = d;
            prev[c][1] = prev[c][0];
            prev[c][0] = x;
        }
    return mx;
}

/* one random move; down: the layer keys physically down (PF_BIT) */
static void churn_move(uint32_t *down)
{
    uint32_t r = churn_rnd(16), e, i;
    if (r < 9) {                                       /* a key down */
        e = churn_rnd(PF_N);
        if (!(*down & PF_BIT(e))) {
            perf_press(e, 1);
            *down |= PF_BIT(e);
        }
    } else if (r < 12) {                               /* a key up */
        if (*down) {
            do
                e = churn_rnd(PF_N);
            while (!(*down & PF_BIT(e)));
            perf_press(e, 0);
            *down &= ~PF_BIT(e);
        }
    } else if (r == 12) {                              /* MENU > FX LATCH flipped (ui_input.c: the menu kills) */
        perf_kill = 1;
        perf_latched = 0;
        perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
        perf_latch_on ^= 1u;
    } else if (r == 13) {                              /* G5 CLR tapped */
        perf_press(PF_CLR, 1);
        perf_press(PF_CLR, 0);
    } else {                                           /* a knob: FILTER -100..100, CRUSH / THROW / DEPTH 0..100 */
        i = churn_rnd(4);
        perf_k[i] = (int8_t)(i == 0 ? (int)churn_rnd(201) - 100 : (int)churn_rnd(101));
    }
}

/* the mix without punch-ins into churn_dry (block 0 steps up from silence: 0, not the mix's own) */
static void churn_dry_pass(int drums)
{
    int32_t o[2 * CTL], prev[2][2] = {{0, 0}, {0, 0}};
    uint32_t k, i;
    churn_setup();
    punch_start();
    for (k = 0; k < CHURN_PRE + CHURN_BLK; k++) {
        drum_maybe(k, drums);
        mix_block(o, CTL);
        churn_dry[k].d2 = max_d2(o, prev);
        churn_dry[k].pk = 0;
        for (i = 0; i < 2u * CTL; i++)
            if (abs(o[i]) > churn_dry[k].pk)
                churn_dry[k].pk = abs(o[i]);
    }
    churn_dry[0].d2 = 0;
    perf_clear_all();
    transport_req = 2;
}

/* the most block k's max_d2 may be: the dry mix's over the second up to it and the block after it, CHURN_CLICK times,
 * or its peak / CHURN_FLOOR (a pure tone's max_d2 is tiny: OCT+ or a filter's fade-in would pass it by a few times);
 * the window's max_d2 and peak into d2, pk */
static int32_t churn_limit(uint32_t k, int32_t *d2, int32_t *pk)
{
    uint32_t j = k - CHURN_PRE, end = k + 2u < CHURN_PRE + CHURN_BLK ? k + 2u : CHURN_PRE + CHURN_BLK;
    int32_t lim;
    *d2 = *pk = 0;
    for (; j < end; j++) {
        if (churn_dry[j].d2 > *d2)
            *d2 = churn_dry[j].d2;
        if (churn_dry[j].pk > *pk)
            *pk = churn_dry[j].pk;
    }
    lim = CHURN_CLICK * *d2;
    return lim > *pk / CHURN_FLOOR ? lim : *pk / CHURN_FLOOR;
}

/* what punch_play does in a block of its moves (k: blocks into them; down: the layer keys down, PF_BIT): the moves
 * made, 0 for none */
typedef uint32_t (*play_fn)(uint32_t k, uint32_t *down);

/* random play: a churn_move every 1..32 blocks (churn_rng seeded by punch_churn) */
static uint32_t play_random(uint32_t k, uint32_t *down)
{
    static uint32_t gap;
    if (k == 0u)
        gap = 1;
    if (--gap)
        return 0;
    churn_move(down);
    gap = 1u + churn_rnd(32);
    return 1;
}

/* a stack (STACKS, or one key): engaged at the first block, let go halfway (a LATCH stack: its keys tapped again),
 * the knob macros it set back to 0 with it */
static const punch_stack_t *play_st;
static uint32_t play_stack(uint32_t k, uint32_t *down)
{
    uint32_t i;
    if (k == 0u) {
        stack_engage(play_st);
        for (i = 0; i < play_st->n && !play_st->latch; i++)
            *down |= PF_BIT(play_st->e[i]);
        return 1;
    }
    if (k != CHURN_BLK / 2u)
        return 0;
    for (i = 0; i < play_st->n; i++) {
        if (play_st->latch)
            perf_press((uint32_t)play_st->e[i], 1);
        perf_press((uint32_t)play_st->e[i], 0);
    }
    *down = 0;
    perf_k[1] = perf_k[2] = perf_k[3] = 0;
    return 1;
}

/* a crusher at work: KNOB 2 CRUSH's share, or a GLITCH segment's crush action */
static int pf_crushing(void)
{
    return pf.cw || (pf.mode == BM_GLT && pf.gl.a == GA_CRU);
}

/* the loaded mix (churn_setup) dry, then with play's moves: the click, stuck and cost results */
static churn_t punch_play(play_fn play, int drums)
{
    static int16_t p0[NTRK][P_COUNT];
    churn_t r;
    int32_t o[2 * CTL], prev[2][2] = {{0, 0}, {0, 0}};
    uint64_t acc_ns = 0, acc_i = 0, worst_ns = 0, worst_i = 0;
    uint32_t k, e, down = 0, n = 0, seg = MEAS_BLK / 4u;
    int engaged0 = 0;
    memset(&r, 0, sizeof r);
    r.lim = 1;
    churn_dry_pass(drums);
    churn_setup();
    punch_start();
    for (k = 0; k < CHURN_PRE + CHURN_BLK + CHURN_END; k++) {
        int moving = k >= CHURN_PRE && k < CHURN_PRE + CHURN_BLK, crush;
        int32_t wet_d2;
        uint64_t t0, i0;
        drum_maybe(k, drums);
        if (k == CHURN_PRE) {                          /* before the first move: what "as before" means */
            for (e = 0; e < NTRK; e++)
                memcpy(p0[e], trk[e].p, sizeof p0[e]);
            engaged0 = pf_engaged();                   /* (a project's SOLO mutes the others from the start) */
        }
        if (moving)
            r.moves += play(k - CHURN_PRE, &down);
        if (k == CHURN_PRE + CHURN_BLK) {              /* the end: CLR with LATCH on, every key up */
            perf_latch_on = 1;
            perf_press(PF_CLR, 1);
            perf_press(PF_CLR, 0);
            perf_latch_on = 0;
            for (e = 0; e < PF_N; e++)
                if (down & PF_BIT(e))
                    perf_press(e, 0);
            down = 0;
        }
        if (perf_kill && !down)                        /* ui_input.c: the kill ends when the keys are let go */
            perf_kill = 0;
        crush = pf_crushing();
        t0 = now_ns();
        i0 = instr_now();
        mix_block(o, CTL);
        wet_d2 = max_d2(o, prev);
        if (moving) {
            acc_ns += now_ns() - t0;
            if (i0)
                acc_i += instr_now() - i0;
            if (++n % seg == 0u) {
                if (acc_ns > worst_ns)
                    worst_ns = acc_ns;
                if (acc_i > worst_i)
                    worst_i = acc_i;
                acc_ns = acc_i = 0;
            }
            if (!crush && !pf_crushing()) {            /* the worst block: max_d2 / its limit the largest */
                int32_t d2, pk, lim = churn_limit(k, &d2, &pk);
                if ((int64_t)wet_d2 * r.lim > (int64_t)r.click * lim) {
                    r.click = wet_d2;
                    r.lim = lim;
                    r.d2 = d2;
                    r.pk = pk;
                }
            }
        }
    }
    r.ns = (double)worst_ns / (double)(seg * CTL);
    r.ipc = worst_i ? (double)worst_i / (double)(seg * CTL) : 0;
    r.pct = r.ipc ? pct_from_ipc(r.ipc) : 0;
    if ((pf_engaged() && !engaged0) || pf.td || perf_held || perf_kill || pfx_any || pfx_qon)
        r.stuck |= 1u;
    for (e = 0; e < NTRK; e++)
        if (memcmp(p0[e], trk[e].p, sizeof p0[e]))
            r.stuck |= 2u;
    perf_clear_all();
    transport_req = 2;
    return r;
}

static churn_t punch_churn(uint32_t seed, int drums)
{
    churn_rng = seed;
    return punch_play(play_random, drums);
}

/* one run's result (name: what was played): printed, and the click, stuck and (on a real instruction counter)
 * deadline checks */
static void play_check(const char *name, const churn_t *r)
{
    char what[200];
    printf("punch: %s: %u moves, worst d2 %d (dry d2 %d, peak %d), worst 1/8 s %.0f ns ipc=%.0f -> %.1f %%\n",
           name, r->moves, r->click, r->d2, r->pk, r->ns, r->ipc, r->pct);
    snprintf(what, sizeof what, "%s: no click: d2 %d within %d (%dx dry, peak / %d)", name, r->click, r->lim,
             CHURN_CLICK, CHURN_FLOOR);
    check(what, r->click <= r->lim);
    snprintf(what, sizeof what, "%s: all off after CLR + every key up%s%s", name,
             r->stuck & 1u ? " (still at work)" : "", r->stuck & 2u ? " (track params changed)" : "");
    check(what, r->stuck == 0);
    if (ipc_kind == 1 && r->ipc) {
        snprintf(what, sizeof what, "%s: worst %.1f %% stays under 100 %% of the half", name, r->pct);
        check(what, r->pct < 100.0);
    }
}

static void test_synth_churn(void)
{
    const uint32_t *seeds;
    uint32_t n = churn_seeds(&seeds), i;
    char name[64];
    churn_setup = synth_heavy;
    for (i = 0; i < n; i++) {
        churn_t r = punch_churn(seeds[i], 1);
        snprintf(name, sizeof name, "churn synth %08x", seeds[i]);
        play_check(name, &r);
    }
}

/* every layer key alone (PF_* below PF_N: audio, MIDI, mutes) pressed, held 2 s, let go, on the heavy mix */
static void test_synth_keys(void)
{
    punch_stack_t one = {"key", 1, {0}, 0, 0, 0, 0};
    uint32_t e;
    char name[64];
    churn_setup = synth_heavy;
    play_st = &one;
    for (e = 0; e < PF_N; e++) {
        churn_t r;
        one.e[0] = (int8_t)e;
        r = punch_play(play_stack, 1);
        snprintf(name, sizeof name, "key PF %u alone", e);
        play_check(name, &r);
    }
}

/* survey_churn's mix: its project, loaded again for each pass (it loaded once already in the same child) */
static const project_t *churn_proj;
static void churn_load_proj(void)
{
    if (host_apply_project(churn_proj) != 0)
        _exit(1);
}

/* a real project under random play, in a child of its own: a crash here fails (the stack child already ran the
 * same project to the end, so the WT / user-slot host faults the survey forgives are not it). Three seeds. */
static void survey_churn(const project_t *q, const char *name)
{
    const uint32_t *seeds;
    uint32_t n = churn_seeds(&seeds), i;
    churn_t res[3];
    char what[200];
    ssize_t got;
    int fd[2], st = 0;
    pid_t pid;
    if (n > 3u)
        n = 3u;
    if (pipe(fd)) {
        check("churn: pipe", 0);
        return;
    }
    fflush(stdout);
    if (!(pid = fork())) {
        close(fd[0]);
        churn_proj = q;
        churn_setup = churn_load_proj;
        for (i = 0; i < n; i++)
            res[i] = punch_churn(seeds[i], 0);         /* the project's own pattern, no drum_hit on track 4 */
        (void)!write(fd[1], res, n * sizeof res[0]);
        _exit(0);
    }
    close(fd[1]);
    got = read(fd[0], res, n * sizeof res[0]);
    close(fd[0]);
    waitpid(pid, &st, 0);
    if (got != (ssize_t)(n * sizeof res[0]) || !WIFEXITED(st) || WEXITSTATUS(st)) {
        snprintf(what, sizeof what, "churn %s: the render ran to the end", name);
        check(what, 0);
        return;
    }
    for (i = 0; i < n; i++) {
        snprintf(what, sizeof what, "churn %s %08x", name, seeds[i]);
        play_check(what, &res[i]);
    }
}

/* --------------------------------------------------------- CI synthetic --- */
static int test_borrow_master(void)
{
    uint32_t t;
    int32_t o[2 * CTL];
    host_tracks_init();
    memset(&pf, 0, sizeof pf);
    pf.src = pf.next = PF_N;
    pf.lc = PF_TOP;
    pf.mg[0] = pf.mg[1] = pf.mg[2] = pf.mg[3] = 32768;
    perf_held = perf_act = 0;
    sl_lent = 0;
    memset(sl, 0, sizeof sl);
    memset(sl_m, 0, sizeof sl_m);
    memset(sl_buf, 0, sizeof sl_buf);
    song.g[G_BPM] = 120;
    song.playing = 1;
    transport_req = 1;
    perf_start();
    for (t = 0; t < 44032u; t += CTL)
        mix_block(o, CTL);
    sl_m[0].loop = sl_m[1].loop = 5512u;
    sl_m[0].rec_on = sl_m[1].rec_on = 1u;
    perf_press(PF_R16, 1);
    for (; t < 60000u; t += CTL)
        mix_block(o, CTL);
    return check("borrow: MASTER STUT loop/rec_on cleared when REPEAT takes sl_buf",
                 sl_lent && !sl_m[0].loop && !sl_m[1].loop && !sl_m[0].rec_on && !sl_m[1].rec_on);
}

static int test_synth_stacks(void)
{
    run_t idle, worst, hold;
    char what[200];
    int b = 0;
    synth_heavy();
    idle = measure_stack(&STACKS[0], 1);
    worst = measure_stack(&STACKS[14], 1);              /* worst key-held */
    hold = measure_stack(&STACKS[15], 1);               /* worst HOLD */
    printf("punch: synth heavy mix: idle %.0f ns/sample ipc=%.0f (proxy %s) → cons %.1f %% of half\n",
           idle.ns, idle.ipc, ipc_kind == 1 ? "instr" : ipc_kind == 2 ? "cycles" : "none", idle.pct);
    printf("punch:   worst      %.0f ns (+%.0f) ipc=%.0f → cons %.1f %%  click=%d local=%d engaged=%u\n",
           worst.ns, worst.ns - idle.ns, worst.ipc, worst.pct, worst.click, worst.local, worst.engaged);
    printf("punch:   worst/HOLD %.0f ns (+%.0f) ipc=%.0f → cons %.1f %%  click=%d engaged=%u\n",
           hold.ns, hold.ns - idle.ns, hold.ipc, hold.pct, hold.click, hold.engaged);
    b += check("synth: worst stack engages", worst.engaged != 0);
    b += check("synth: HOLD stack engages", hold.engaged != 0);
    /* click: full mix uses the local pre-press bound so kicks do not hide a splice */
    snprintf(what, sizeof what, "synth: worst click %d within 8x local bound %d", worst.click, worst.local);
    b += check(what, worst.local == 0 || worst.click <= 8 * worst.local || worst.click < 20000);
    if (worst.ipc && ipc_kind == 1) {               /* real retired instructions only: cycles×0.017 is a survey aid */
        snprintf(what, sizeof what, "synth: worst cons %.1f %% stays under 100 %% of the %.0f us half",
                 worst.pct, (double)HALF_US);
        b += check(what, worst.pct < 100.0);
        printf("punch: shed A/B on synth worst: cur(85/92) shed=%u miss=%u  rel(96/98) shed=%u miss=%u\n",
               worst.shed.shed_cur, worst.shed.miss_cur, worst.shed.shed_rel, worst.shed.miss_rel);
    } else if (worst.ipc) {
        printf("punch: cycle-proxy cons %.1f %% (survey aid only; deadline assert needs HW instructions)\n",
               worst.pct);
        printf("punch: shed A/B on synth worst: cur(85/92) shed=%u miss=%u  rel(96/98) shed=%u miss=%u\n",
               worst.shed.shed_cur, worst.shed.miss_cur, worst.shed.shed_rel, worst.shed.miss_rel);
    } else {
        printf("punch: no instruction/cycle counter usable for %% projection on this host; ns reported only\n");
    }
    {   /* every stack pressed, held, let go, against the dry mix (punch_play): the step bound above passes a
         * missing crossfade in this mix */
        uint32_t s;
        char name[64];
        churn_setup = synth_heavy;
        for (s = 1; s < sizeof STACKS / sizeof STACKS[0]; s++) {
            churn_t r;
            play_st = &STACKS[s];
            r = punch_play(play_stack, 1);
            snprintf(name, sizeof name, "stack %s", STACKS[s].name);
            play_check(name, &r);
        }
    }
    return b;
}

static int test_click_synth_parts(void)
{
    /* synth parts alone, tight bound (mx <= 4 * own), as the extras plan */
    static const punch_stack_t one = {"R16", 1, {PF_R16}, 0, 0, 0, 0};
    run_t r;
    int32_t own = 0, t;
    int32_t o[2 * CTL], prev = 0;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], 0, 5);                        /* ANALOG SINE KEY */
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_SUS] = 127;
    trk_note_on(&trk[0], 69, 100);
    song.g[G_BPM] = 120;
    transport_req = 1;
    song.playing = 1;
    for (t = 0; t < (int)(FS / CTL); t++) {
        mix_block(o, CTL);
        {
            uint32_t i;
            for (i = 0; i < CTL; i++) {
                int32_t d = o[2u * i] - prev;
                if (d < 0)
                    d = -d;
                if (d > own)
                    own = d;
                prev = o[2u * i];
            }
        }
    }
    r = measure_stack(&one, 0);
    {
        char what[160];
        snprintf(what, sizeof what, "click synth-only R16: max step %d (own %d, bound 4x)", r.click, own);
        return check(what, own == 0 || r.click <= 4 * own);
    }
}

/* --------------------------------------------------------------- survey --- */
typedef struct {
    char file[96];
    char tag[32];
    char bin[128];
    int loaded;
    char err[96];
    const char *worst_name;
    double idle_ns, worst_ns, worst_pct, idle_pct;
    int32_t click, local;
    shed_t shed_cur_path;                              /* under current thresholds on worst */
    shed_t shed_rel_path;
    int click_ok;
} row_t;

static int ends_with(const char *s, const char *suf)
{
    size_t n = strlen(s), m = strlen(suf);
    return n >= m && !strcmp(s + n - m, suf);
}

static void survey_dir(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    row_t *rows = 0;
    size_t nrow = 0, cap = 0;
    size_t i;
    uint32_t load_ok = 0, load_fail = 0;
    shed_t tot_cur = {0}, tot_rel = {0};
    if (!d) {
        printf("punch: survey: cannot open %s: %s\n", dir, strerror(errno));
        return;
    }
    printf("punch: survey %s (HALF_US=%u, cons = ipc×2×%.3f %%)\n", dir, HALF_US, IPC_TO_PCT);
    while ((e = readdir(d))) {
        char path[512], err[96];
        project_t q;
        row_t row;
        run_t idle, best;
        const punch_stack_t *bw = 0;
        uint32_t s;
        if (!ends_with(e->d_name, ".bin"))
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        memset(&row, 0, sizeof row);
        snprintf(row.bin, sizeof row.bin, "%s", e->d_name);
        /* file__tag.bin */
        {
            char *sep = strstr(row.bin, "__");
            if (sep) {
                size_t n = (size_t)(sep - row.bin);
                if (n >= sizeof row.file)
                    n = sizeof row.file - 1;
                memcpy(row.file, row.bin, n);
                row.file[n] = 0;
                snprintf(row.tag, sizeof row.tag, "%s", sep + 2);
                if (ends_with(row.tag, ".bin"))
                    row.tag[strlen(row.tag) - 4] = 0;
            } else {
                snprintf(row.file, sizeof row.file, "%s", row.bin);
            }
        }
        if (!load_project_bin(path, &q, err, sizeof err)) {
            row.loaded = 0;
            snprintf(row.err, sizeof row.err, "%s", err);
            load_fail++;
            printf("punch: LOAD FAIL  %-40s  %s\n", row.bin, err);
        } else {
            /* child: a wild project must not take down the survey (WT/user-slot faults, etc.) */
            int fd[2];
            pid_t pid;
            struct { run_t idle, best; char wname[24]; int apply_fail; } msg;
            memset(&msg, 0, sizeof msg);
            if (pipe(fd)) {
                row.loaded = 0;
                snprintf(row.err, sizeof row.err, "pipe");
                load_fail++;
            } else if (!(pid = fork())) {
                close(fd[0]);
                if (host_apply_project(&q) != 0) {
                    msg.apply_fail = 1;
                } else {
                    const punch_stack_t *bw = &STACKS[0];
                    uint32_t s;
                    msg.idle = measure_stack(&STACKS[0], 1);
                    msg.best = msg.idle;
                    for (s = 1; s < sizeof STACKS / sizeof STACKS[0]; s++) {
                        run_t r = measure_stack(&STACKS[s], 1);
                        if (!r.engaged && STACKS[s].n)
                            continue;
                        if (r.pct > msg.best.pct || (r.pct == 0 && r.ns > msg.best.ns)) {
                            msg.best = r;
                            bw = &STACKS[s];
                        }
                    }
                    snprintf(msg.wname, sizeof msg.wname, "%s", bw->name);
                }
                (void)!write(fd[1], &msg, sizeof msg);
                _exit(0);
            } else {
                int st = 0;
                close(fd[1]);
                if (read(fd[0], &msg, sizeof msg) != (ssize_t)sizeof msg || (waitpid(pid, &st, 0), !WIFEXITED(st) || WEXITSTATUS(st))) {
                    row.loaded = 0;
                    snprintf(row.err, sizeof row.err, "render crash");
                    load_fail++;
                    printf("punch: RENDER FAIL %-40s  (child crashed; noted, survey continues)\n", row.bin);
                } else if (msg.apply_fail) {
                    row.loaded = 0;
                    snprintf(row.err, sizeof row.err, "apply/engines");
                    load_fail++;
                    printf("punch: APPLY FAIL %-40s\n", row.bin);
                } else {
                    row.loaded = 1;
                    load_ok++;
                    idle = msg.idle;
                    best = msg.best;
                    row.idle_ns = idle.ns;
                    row.idle_pct = idle.pct;
                    row.worst_name = msg.wname[0] ? msg.wname : "idle";
                    row.worst_ns = best.ns;
                    row.worst_pct = best.pct;
                    row.click = best.click;
                    row.local = best.local;
                    row.shed_cur_path = best.shed;
                    row.shed_rel_path = best.shed;
                    row.click_ok = best.local == 0 || best.click <= 8 * best.local || best.click < 20000;
                    tot_cur.shed_cur += best.shed.shed_cur;
                    tot_cur.miss_cur += best.shed.miss_cur;
                    tot_rel.shed_rel += best.shed.shed_rel;
                    tot_rel.miss_rel += best.shed.miss_rel;
                    printf("punch: %-42s %-12s idle=%6.0fns/%4.1f%%  worst=%-12s %6.0fns/%5.1f%%  "
                           "shed cur=%u rel=%u  click=%d%s\n",
                           row.file, row.tag, row.idle_ns, row.idle_pct, row.worst_name, row.worst_ns, row.worst_pct,
                           best.shed.shed_cur, best.shed.shed_rel, row.click, row.click_ok ? "" : " CLICK?");
                    survey_churn(&q, row.bin);
                }
                close(fd[0]);
            }
        }
        if (nrow >= cap) {
            cap = cap ? cap * 2 : 64;
            rows = realloc(rows, cap * sizeof *rows);
        }
        rows[nrow++] = row;
    }
    closedir(d);
    printf("punch: survey done: %zu bins, loaded %u, failed %u\n", nrow, load_ok, load_fail);
    printf("punch: shed totals over worst stacks: current(85/92) shed_events=%u deadline_miss=%u; "
           "relaxed(96/98) shed_events=%u deadline_miss=%u\n",
           tot_cur.shed_cur, tot_cur.miss_cur, tot_rel.shed_rel, tot_rel.miss_rel);
    if (tot_cur.shed_cur == 0 && tot_rel.shed_rel == 0)
        printf("punch: shed recommendation: no projected shed on these projects under either threshold; "
               "do NOT ship the relaxed threshold from this data alone (confirm on device).\n");
    else if (tot_rel.shed_rel < tot_cur.shed_cur && tot_rel.miss_rel <= tot_cur.miss_cur)
        printf("punch: shed recommendation: relaxed 96/98 reduces projected sheds (%u→%u) without more "
               "deadline misses; still confirm on device before shipping.\n",
               tot_cur.shed_cur, tot_rel.shed_rel);
    else
        printf("punch: shed recommendation: relaxed threshold does not clearly help on this host projection; "
               "keep 85/92 until device counters say otherwise.\n");
    free(rows);
}

int main(int argc, char **argv)
{
    const char *env = getenv("CRACKLE_DIR");
    int i;
    printf("punch: HALF_US=%u  ipc_kind probe...\n", HALF_US);
    (void)instr_now();
    printf("punch: counter=%s\n", ipc_kind == 1 ? "instructions" : ipc_kind == 2 ? "rdtsc-cycles" : "none");
    bad += test_borrow_master();
    bad += test_click_synth_parts();
    bad += test_synth_stacks();
    test_synth_keys();
    test_synth_churn();
    if (argc > 1) {
        for (i = 1; i < argc; i++)
            survey_dir(argv[i]);
    } else if (env && *env) {
        survey_dir(env);
    } else {
        printf("punch: no CRACKLE_DIR / bin dirs: survey skipped (CI synthetic only)\n");
    }
    printf("%s\n", bad ? "PUNCH CRACKLE TEST FAILED" : "punch crackle test passed");
    return bad != 0;
}
