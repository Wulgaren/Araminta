/* SPDX-License-Identifier: GPL-3.0-only */
/* A busy 4-track project for the emulator's CPU load (emu_test --scenario project): FM6 tied chords,
 * ANALOG acid bass, WT lead and the DRUM kit at 120 BPM, HALL reverb, both sends at 100, a FILTER insert
 * on the chords, a DIST insert on the bass, a MASTER phaser and the SLICER as a SEND on every track.
 * Writes the raw FUN21 project_store_t (4864 B) to argv[1]. Regenerate with:
 *   cc -O1 -w -Ibuild/gen -Ifirmware/src tests/emu/busy_project.c -o build/host/busy_project -lm
 *   build/host/busy_project tests/emu/busy_project.bin */
static unsigned char host_samples[3][0x14000];
#define SMP_USER_XIP(k) host_samples[k]
#define main hostsim_main
#include "../hostsim.c"
#undef main

static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }   /* (MENU > SCREEN OFF: lcd.c) */
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#define FELUCCA_FLASH 1
#include "../../firmware/src/gfx.c"
#include "../../firmware/src/panel.c"
#include "../../firmware/src/ui.c"
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
#include "../../firmware/src/storage.c"
#include "../../firmware/src/upreset.c"
#include "../../firmware/src/project.c"

/* the four patterns of regress.c job_song (ACID bass 16 steps, tied chords over 32, lead 12, drums 16) */
static void build_busy(void)
{
    static const uint8_t ACID[16] = {45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50};
    static const uint8_t ACIDF[16] = {1, 0, 2, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 2, 0, 1};
    static const uint8_t AM[4] = {57, 60, 64, 67}, FMI[4] = {53, 57, 60, 64};
    static const uint8_t LEAD[12] = {76, 0, 0, 79, 0, 0, 81, 0, 79, 0, 76, 0};
    static const int16_t SL[4] = {SL_GATE, 1, 1, 127};
    uint32_t i, k;

    host_tracks_init();
    song.g[G_BPM] = 120;
    song.g[G_RTYPE] = 2;                          /* HALL */
    host_preset(&trk[0], 12, 0);                  /* FM6, the tied chords */
    host_preset(&trk[1], 0, 4);                   /* ANALOG, the acid bass */
    host_preset(&trk[2], 14, 0);                  /* WT, the lead */
    host_drums(&trk[3]);
    for (i = 0; i < 16u; i++) {
        uint8_t n = ACID[i];
        put_step(&trk[1], i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, ACIDF[i]);
    }
    trk[0].p[P_SLEN] = 32;
    trk[0].p[P_SGATE] = 120;
    for (i = 0; i < 32u; i++)
        put_step(&trk[0], i, i % 16u == 0u ? 4u : 0u, i < 16u ? AM : FMI,
                 i % 16u == 0u ? ST_NOTE : i % 16u < 14u ? ST_TIE : ST_REST, 0);
    trk[2].p[P_SLEN] = 12;
    for (i = 0; i < 12u; i++) {
        uint8_t n = LEAD[i];
        put_step(&trk[2], i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, i == 0u ? SF_ACCENT : 0u);
    }
    for (i = 0; i < 16u; i++) {
        uint8_t n[4];
        uint32_t kn = 0;
        if (i % 4u == 0u)
            n[kn++] = 36;
        if (i == 4u || i == 12u)
            n[kn++] = 38;
        if (i % 2u == 0u)
            n[kn++] = i == 14u ? 46 : 42;
        put_step(&trk[3], i, kn, n, kn ? ST_NOTE : ST_REST, i % 4u == 0u ? SF_ACCENT : 0u);
    }
    for (i = 0; i < 4u; i++) {
        trk[i].p[P_DLY] = 100;
        trk[i].p[P_REV] = 100;
        for (k = 0; k < 4u; k++)
            trk[i].p[P_SLCR + k] = SL[k];
    }
    song.g[G_SLOWN] = 0xF;                        /* SLICER: SEND on every track (G_DLYOWN / G_REVOWN stay SEND) */
    trk[0].p[P_IFTYPE] = 0;                       /* FILTER: LP */
    trk[0].p[P_IFCUT] = 70;
    trk[0].p[P_IFMIX] = 100;
    trk[1].p[P_DIST] = 60;
    trk[1].p[P_IDMIX] = 100;
    trk[2].p[P_IPMIX] = 100;
    fx_master_set(INS_PHASR, 2);
}

int main(int argc, char **argv)
{
    static project_store_t st;
    static project_t loaded;
    FILE *f;

    if (argc != 2) {
        fprintf(stderr, "usage: %s OUT.bin\n", argv[0]);
        return 2;
    }
    build_busy();
    project_capture(&proj_scratch);
    if (!proj_pack(&st, &proj_scratch)) {
        fprintf(stderr, "busy_project: proj_pack failed\n");
        return 1;
    }
    if (!proj_import(&loaded, &st, (int)sizeof st)) {
        fprintf(stderr, "busy_project: the packed project does not import\n");
        return 1;
    }
    f = fopen(argv[1], "wb");
    if (!f) {
        fprintf(stderr, "busy_project: cannot open %s\n", argv[1]);
        return 1;
    }
    if (fwrite(&st, 1, sizeof st, f) != sizeof st || fclose(f) != 0) {
        fprintf(stderr, "busy_project: writing %s failed\n", argv[1]);
        return 1;
    }
    fprintf(stderr, "busy_project: wrote %zu bytes to %s\n", sizeof st, argv[1]);
    return 0;
}
