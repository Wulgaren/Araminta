/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Covering project bins + manifest for the emulator laws-emu runner (tests/laws_emu.sh).
 * Usage: laws_emu_gen OUT_DIR
 *   cc -O1 -w -Ibuild/gen -Ifirmware/src tests/laws_emu_gen.c -o build/host/laws_emu_gen -lm
 *   build/host/laws_emu_gen build/laws_emu
 */
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
static unsigned char host_samples[3][0x14000];
#define SMP_USER_XIP(k) host_samples[k]
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#define FELUCCA_FLASH 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
static void panel_setup(void) {}

static uint8_t nor[0x100000], flash_ok = 1;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n) { memcpy(nor + off, src, n); return 0; }
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

/* white keys F3 .. G5: index into PF_DEF / PF2_DEF (perform.c, same as emu WHITE) */
static const char *const WHITE[PF_KEYS] = {
    "F3", "G3", "A3", "B3", "C4", "D4", "E4", "F4",
    "G4", "A4", "B4", "C5", "D5", "E5", "F5", "G5"
};

static FILE *g_man;
static const char *g_outdir;
static int g_nbins, g_nlines;

static void slot(track_t *t, uint32_t k, int32_t s, int32_t d, int32_t a)
{
    MOD_ROW(t, k)[MF_SRC] = (int16_t)s;
    MOD_ROW(t, k)[MF_DST] = (int16_t)d;
    MOD_ROW(t, k)[MF_AMT] = (int16_t)a;
}

static void clear_matrix(track_t *t)
{
    uint32_t k;
    for (k = 0; k < NMSLOT; k++)
        slot(t, k, MS_OFF, MD_OFF, 0);
}

static uint32_t soft_pad(void)
{
    uint32_t i;
    for (i = 0; i < ENGINES[ENGI_ANALOG]->npresets; i++)
        if (!strcmp(ENGINES[ENGI_ANALOG]->presets[i].name, "SOFT PAD"))
            return i;
    return 0;
}

static void base_light(void)
{
    track_t *t = &trk[0];
    uint32_t s, k;

    host_tracks_init();
    for (k = 0; k < NPART; k++)
        host_preset(&trk[k], k ? 0u : ENGI_ANALOG, k ? 0u : soft_pad());
    song.sel = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_E0] = 4;
    t->p[P_E0 + 2] = 40;
    t->p[P_LD_AMP] = 30;
    t->p[P_LD_PIT] = 16;
    t->p[P_LRATE] = 48;
    for (s = 0; s < INS_N; s++)
        ins_send_set(t, s, 0);
    t->p[P_ED_FX] = 12;
    t->p[P_IDMIX] = 24;
    t->p[P_IFMIX] = 24;
    t->p[P_ICMIX] = 16;
    t->p[P_IPMIX] = 16;
    t->p[P_ILMIX] = 16;
    t->p[P_IHMIX] = 16;
    t->p[P_DLY] = 24;
    t->p[P_REV] = 24;
    t->p[P_CHRD] = CH_DIA3;
    t->p[P_VOIC] = VC_CLOSE;
    t->p[P_LEVEL] = 80;
}

/* synth_heavy PARTS: FM6 / PHASE / VOICE / DRUM-X, full inserts + sends */
static void base_heavy(void)
{
    static const uint8_t PARTS[NPART][2] = {{12, 0}, {2, 0}, {5, 0}, {10, 0}};
    static const int MIX[] = {P_IDMIX, P_IFMIX, P_ICMIX, P_IPMIX, P_ILMIX, P_IHMIX};
    uint32_t p, i;

    host_tracks_init();
    for (p = 0; p < NPART; p++) {
        host_preset(&trk[p], PARTS[p][0], PARTS[p][1]);
        trk[p].p[P_VOICE] = V_POLY;
        trk[p].p[P_SUS] = 127;
        trk[p].p[P_AMODE] = 0;
        trk[p].p[P_LEVEL] = 100;
    }
    for (p = 0; p < NPART; p++) {
        for (i = 0; i < sizeof MIX / sizeof MIX[0]; i++)
            trk[p].p[MIX[i]] = 100;
        trk[p].p[P_DLY] = 100;
        trk[p].p[P_REV] = 100;
    }
    song.g[G_BPM] = 120;
    song.sel = 0;
}

static int write_proj(const char *path)
{
    static project_store_t st;
    static project_t loaded;
    FILE *f;

    project_capture(&proj_scratch);
    if (!proj_pack(&st, &proj_scratch)) {
        fprintf(stderr, "laws_emu_gen: proj_pack failed for %s\n", path);
        return 1;
    }
    if (!proj_import(&loaded, &st, (int)sizeof st)) {
        fprintf(stderr, "laws_emu_gen: proj_import failed for %s\n", path);
        return 1;
    }
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "laws_emu_gen: cannot open %s\n", path);
        return 1;
    }
    if (fwrite(&st, 1, sizeof st, f) != sizeof st || fclose(f) != 0) {
        fprintf(stderr, "laws_emu_gen: writing %s failed\n", path);
        return 1;
    }
    g_nbins++;
    return 0;
}

static int man_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (vfprintf(g_man, fmt, ap) < 0) {
        va_end(ap);
        return 1;
    }
    va_end(ap);
    if (fputc('\n', g_man) == EOF)
        return 1;
    g_nlines++;
    return 0;
}

static int path_join(char *buf, size_t n, const char *name)
{
    int r = snprintf(buf, n, "%s/%s", g_outdir, name);
    return r < 0 || (size_t)r >= n;
}

/* FX stages: same audible setup spirit as laws_test law_fx */
static const struct {
    const char *name;
    uint8_t st;
} FX_STAGES[] = {
    {"DISTORT", INS_DIST}, {"FILTER", INS_FILTER}, {"CRUSH", INS_CRUSH},
    {"PHASER", INS_PHASR}, {"FLANGER", INS_FLANG}, {"CHORUS", INS_CHOR},
    {"DELAY", INS_DLY}, {"REVERB", INS_REV}, {"SLICER", INS_SLCR}, {"TAPE", INS_TAPE},
};
static const uint8_t FX_MIXID[INS_N] = {P_IDMIX, P_IFMIX, P_ICMIX, P_IPMIX, P_ILMIX, P_IHMIX};

static void fx_audible(uint8_t st)
{
    track_t *t = &trk[0];
    uint32_t s;

    base_light();
    clear_matrix(t);
    for (s = 0; s < INS_N; s++) {
        ins_send_set(t, s, 0);
        t->p[FX_MIXID[s]] = 0;
    }
    t->p[P_DLY] = 0;
    t->p[P_REV] = 0;
    t->p[P_SLDEPTH] = 0;
    song.g[G_TMIX] = 0;
    song.g[G_TAPEMODE] = TAPE_SEND;
    bus_own_set(0, -1);
    bus_own_set(1, -1);
    bus_own_set(2, -1);

    if (st < INS_N)
        t->p[FX_MIXID[st]] = 64;
    else if (st == INS_DLY) {
        bus_own_set(0, 0);
        t->p[P_DLY] = 64;
    } else if (st == INS_REV) {
        bus_own_set(1, 0);
        t->p[P_REV] = 64;
    } else if (st == INS_SLCR) {
        ins_send_set(t, INS_SLCR, 0);
        bus_own_set(2, 0);
        t->p[P_SLDEPTH] = 64;
        t->p[P_SLCR] = SL_GATE;
    } else if (st == INS_TAPE) {
        song.g[G_TAPEMODE] = TAPE_CH1;
        song.g[G_TMIX] = 64;
        song.g[G_TTEMPO] = 0;
    }
}

static int emit_matrix(void)
{
    int base;
    char path[512], name[64];

    for (base = 1; base < (int)MD_N; base += (int)NMSLOT) {
        track_t *t = &trk[0];
        int n = (int)MD_N - base, i;

        if (n > (int)NMSLOT)
            n = (int)NMSLOT;
        base_light();
        clear_matrix(t);
        for (i = 0; i < n; i++) {
            int d = base + i;
            int src = (d == MD_AMP || d == MD_VEL) ? MS_KEY : MS_LFO;
            int amt = d == MD_CHRD ? 20 : 63;
            slot(t, (uint32_t)i, src, d, amt);
        }
        snprintf(name, sizeof name, "matrix_%d.bin", base);
        if (path_join(path, sizeof path, name))
            return 1;
        if (write_proj(path))
            return 1;
        if (man_line("matrix_%d 1500 %s/matrix_%d.bin", base, g_outdir, base))
            return 1;
    }
    return 0;
}

static int emit_engines(void)
{
    uint32_t i;
    char path[512], name[64];

    for (i = 0; i < NENG_SHOWN; i++) {
        uint32_t e = ENGINE_ORDER[i];
        base_light();
        host_preset(&trk[0], e, 0);
        trk[0].p[P_LEVEL] = 80;
        clear_matrix(&trk[0]);
        snprintf(name, sizeof name, "eng_%u.bin", i);
        if (path_join(path, sizeof path, name))
            return 1;
        if (write_proj(path))
            return 1;
        if (man_line("eng_%u 1500 %s/eng_%u.bin", i, g_outdir, i))
            return 1;
    }
    return 0;
}

static int emit_fx(void)
{
    uint32_t si;
    char path[512], name[64];

    for (si = 0; si < NELEM(FX_STAGES); si++) {
        snprintf(name, sizeof name, "fx_%s.bin", FX_STAGES[si].name);
        fx_audible(FX_STAGES[si].st);
        if (path_join(path, sizeof path, name))
            return 1;
        if (write_proj(path))
            return 1;
        if (man_line("fx_%s 1500 %s/fx_%s.bin", FX_STAGES[si].name, g_outdir, FX_STAGES[si].name))
            return 1;
    }
    return 0;
}

static int emit_lfo_dest(void)
{
    track_t *t = &trk[0];
    char path[512];

    base_light();
    clear_matrix(t);
    /* non-zero corners: bipolar max, AMP max */
    t->p[P_LD_PIT] = 63;
    t->p[P_LD_FLT] = 63;
    t->p[P_LD_SHP] = 63;
    t->p[P_LD_AMP] = 127;
    if (path_join(path, sizeof path, "lfo_dest.bin"))
        return 1;
    if (write_proj(path))
        return 1;
    return man_line("lfo_dest 1500 %s/lfo_dest.bin", g_outdir);
}

static int emit_locks(void)
{
    /* structural locks stay host-only; pack light project with edit knobs at corners locks would target */
    static const uint16_t IDS[] = {P_ATK, P_PAN, P_DIST, P_DLY, P_REV, P_IDMIX, P_IFCUT, P_ED_FX};
    static const int16_t VALS[] = {10, 20, 40, 50, 60, 70, 80, 30};
    track_t *t = &trk[0];
    uint32_t i;
    char path[512];

    base_light();
    clear_matrix(t);
    for (i = 0; i < NELEM(IDS); i++)
        if (IDS[i] < P_COUNT)
            t->p[IDS[i]] = VALS[i];
    if (path_join(path, sizeof path, "locks.bin"))
        return 1;
    if (write_proj(path))
        return 1;
    return man_line("locks 1500 %s/locks.bin", g_outdir);
}

static int emit_punch_and_stress(void)
{
    static const int DESTS[NMSLOT] = {
        MD_PITCH, MD_CUT, MD_SHP, MD_AMP, MD_PAN, MD_DIST, MD_DLY, MD_REV
    };
    track_t *t;
    uint32_t i;
    char heavy[512], hmat[512];

    base_heavy();
    clear_matrix(&trk[0]);
    if (path_join(heavy, sizeof heavy, "heavy.bin"))
        return 1;
    if (write_proj(heavy))
        return 1;

    base_heavy();
    t = &trk[0];
    clear_matrix(t);
    for (i = 0; i < NMSLOT; i++)
        slot(t, i, MS_LFO, DESTS[i], 40);
    if (path_join(hmat, sizeof hmat, "heavy_matrix.bin"))
        return 1;
    if (write_proj(hmat))
        return 1;

    for (i = 0; i < PF_KEYS; i++) {
        if (PF_DEF[i] < PF_N) {
            if (man_line("punch_1_%s 1500 %s punch=1:%s", WHITE[i], heavy, WHITE[i]))
                return 1;
        }
        if (PF2_DEF[i] < PF_N) {
            if (man_line("punch_2_%s 1500 %s punch=2:%s", WHITE[i], heavy, WHITE[i]))
                return 1;
        }
    }

    /* page 1 only (emu_test punch keys are one page a run): LPF + HPF + OCT+ */
    if (man_line("stress_matrix 2000 %s", hmat))
        return 1;
    if (man_line("stress_punch 2000 %s punch=1:C4,1:D4,1:F4 latch", heavy))
        return 1;
    if (man_line("stress_both 2000 %s punch=1:C4 latch", hmat))
        return 1;
    return 0;
}

static int write_budgets(void)
{
    char path[512];
    FILE *f;

    if (path_join(path, sizeof path, "budgets.txt"))
        return 1;
    f = fopen(path, "w");
    if (!f)
        return 1;
    fputs("# label_glob late_max\nstress_* 50\n* 5\n", f);
    return fclose(f) != 0;
}

int main(int argc, char **argv)
{
    char manpath[512];
    int r;

    if (argc != 2) {
        fprintf(stderr, "usage: %s OUT_DIR\n", argv[0]);
        return 2;
    }
    g_outdir = argv[1];
    if (mkdir(g_outdir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "laws_emu_gen: mkdir %s failed\n", g_outdir);
        return 1;
    }
    if (path_join(manpath, sizeof manpath, "manifest.txt"))
        return 1;
    g_man = fopen(manpath, "w");
    if (!g_man) {
        fprintf(stderr, "laws_emu_gen: cannot write %s\n", manpath);
        return 1;
    }

    r = emit_matrix() || emit_engines() || emit_fx() || emit_lfo_dest() || emit_locks() ||
        emit_punch_and_stress() || write_budgets();
    if (fclose(g_man) != 0)
        r = 1;
    fprintf(stderr, "laws_emu_gen: %d bins, %d manifest lines -> %s\n", g_nbins, g_nlines, g_outdir);
    return r ? 1 : 0;
}
