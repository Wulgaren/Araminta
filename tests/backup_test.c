/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The editor's full backup (editor_backup.c: LIST / GET / PUT) against simulated NOR flash:
 * CRC before any write, stale runtime copies, USB resets and timeouts, malformed objects, older
 * project formats, settings values and user preset banks kept byte for byte. */
static unsigned char host_samples[3][0x14000];
#define SMP_USER_XIP(k) host_samples[k]
#define main hostsim_main
#include "hostsim.c"
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
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
static void panel_setup(void) {}

static uint8_t nor[0x100000], flash_ok = 1;
static int erase_error;
static uint32_t erases;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { if (erase_error) return -8; memset(nor + off, 0xFF, 4096); erases++; return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n) { memcpy(nor + off, src, n); return 0; }
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"
#include "old_pack.h"

enum { ED_BACKUP_LIST = 65, ED_BACKUP_GET, ED_BACKUP_PUT };
static uint8_t rep[4096];
static uint32_t rep_n;
static void ed_b(uint32_t v) { if (rep_n < sizeof rep) rep[rep_n++] = (uint8_t)(v & 127u); }
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        uint32_t k = na > 7u ? 7u : na;
        if (!k || n + k > max || (m >> k))
            return 0;
        for (j = 0; j < k; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return na ? 0u : n;
}
static uint8_t ed_smp_buf[512] __attribute__((aligned(4)));
static void fm1_wdt_feed(void) {}
static int ed_flash_stop(void) { return transport_busy(); }
#include "../firmware/src/editor_backup.c"

static int check(const char *what, int ok)
{
    printf("backup: %-72s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}

static void reset(void)
{
    memset(nor, 0xFF, sizeof nor);
    memset(&song, 0, sizeof song);
    memset(trk, 0, sizeof trk);
    memset(&chain, 0, sizeof chain);
    chain_defaults(&chain_config);
    proj_slot_used = 0;
    memset(proj_slot_sum, 0, sizeof proj_slot_sum);
    memset(up_bank, 0, sizeof up_bank);
    memset(&persist_saved, 0, sizeof persist_saved);
    memset(&settings, 0, sizeof settings);
    memset(&ui, 0, sizeof ui);
    panel = PANEL_DEFAULT;
    settings_init();
    host_tracks_init();
    fm1_ms = 0;
    transport_req = 0;
    usb.resets = 0;
    erase_error = 0;
    erases = 0;
    ed_bk_valid = ed_bk_put = 0;
}

static uint32_t call(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    rep_n = 0;
    ed_backup_handle(cmd, a, n);
    return rep_n;
}
static void put32(uint8_t *a, uint32_t v) { for (uint32_t i = 0; i < 5u; i++) a[i] = (uint8_t)((v >> (7u * i)) & 127u); }

/* PUT begin: rc */
static uint32_t put_begin(uint32_t id, uint32_t len, uint32_t crc)
{
    uint8_t a[12] = {0, (uint8_t)id};
    put32(a + 2, len);
    put32(a + 7, crc);
    call(ED_BACKUP_PUT, a, sizeof a);
    return rep[2];
}
/* PUT one chunk of data at off: rc */
static uint32_t put_chunk(uint32_t id, uint32_t off, const uint8_t *p, uint32_t n)
{
    uint8_t a[16 + 300];
    uint32_t k = 7;
    a[0] = 1;
    a[1] = (uint8_t)id;
    put32(a + 2, off);
    while (n) {
        uint32_t g = n > 7u ? 7u : n, m = 0, i;
        for (i = 0; i < g; i++) m |= (uint32_t)(p[i] >> 7) << i;
        a[k++] = (uint8_t)m;
        for (i = 0; i < g; i++) a[k++] = p[i] & 127u;
        p += g;
        n -= g;
    }
    call(ED_BACKUP_PUT, a, k);
    return rep[2];
}
static uint32_t put_end(uint32_t id, uint32_t op)   /* op 2 commit, 3 abort */
{
    uint8_t a[2] = {(uint8_t)op, (uint8_t)id};
    call(ED_BACKUP_PUT, a, 2);
    return rep[2];
}
/* a whole object: begin, 256-byte chunks, commit; the first failing rc or the commit's */
static uint32_t put_all(uint32_t id, const void *v, uint32_t len, uint32_t crc)
{
    const uint8_t *p = v;
    uint32_t off, rc = put_begin(id, len, crc);
    for (off = 0; !rc && off < len; off += 256u)
        rc = put_chunk(id, off, p + off, len - off > 256u ? 256u : len - off);
    return rc ? rc : put_end(id, 2);
}
/* LIST: rc; the length and CRC of object id */
static uint32_t list(uint32_t id, uint32_t *len, uint32_t *crc)
{
    uint32_t i;
    call(ED_BACKUP_LIST, 0, 0);
    if (rep[1])
        return rep[1];
    for (i = 0; i < rep[2]; i++) {
        const uint8_t *o = rep + 3 + i * 11u;
        if (o[0] == id) {
            *len = ed_bk_r32(o + 1);
            *crc = ed_bk_r32(o + 6);
        }
    }
    return 0;
}
static uint32_t get(uint32_t id, uint32_t off, uint32_t count)
{
    uint8_t a[8] = {(uint8_t)id};
    put32(a + 1, off);
    a[6] = (uint8_t)(count & 127u);
    a[7] = (uint8_t)(count >> 7);
    call(ED_BACKUP_GET, a, sizeof a);
    return rep[1];
}

int main(void)
{
    int bad = 0;
    uint32_t len = 0, crc = 0, before;
    static project_store_t st, keep;
    persist_t ps;

    reset();
    trk[0].step[0] = (step_t){{60}, 1, ST_NOTE, 0, 96, 0, 0};
    bad += check("LIST captures the runtime: 17 objects (id 8 empty, id 9 FM6 patches, ids 10..13 banks 2..5), runtime 4864 B (FUN21)",
                 list(0, &len, &crc) == 0 && rep[2] == 17u && len == sizeof(project_store_t) && len == 4864u &&
                 crc == st_crc32(ED_BK_RAW, len));
    {   /* a wavetable slot (id 34, USR3) is backed up as its 512-byte header + PCM data */
        smp_wt_hdr_t *w = (smp_wt_hdr_t *)host_samples[2];
        int ok;
        memset(host_samples, 0, sizeof host_samples);
        memset(w, 0, sizeof *w);
        w->magic = SMP_WT_MAGIC; w->version = SMP_WT_VERSION; w->frames = 2u;
        w->base_log2 = SMP_WT_BASE_LOG2; w->mip_last_log2 = SMP_WT_MIP_LAST_LOG2;
        memcpy(w->name, "BACK", 4);
        w->data_len = w->frames * smp_wt_stride(w) * 2u;
        memset((uint8_t *)w + SMP_USER_DATA, 0x5A, w->data_len);
        w->crc = st_crc32((const uint8_t *)w + SMP_USER_DATA, w->data_len);
        len = crc = 0;
        list(34u, &len, &crc);
        ok = len == SMP_USER_DATA + w->data_len && crc == st_crc32(host_samples[2], len);
        w->magic = 0;
        len = crc = 0;
        list(34u, &len, &crc);
        ok &= len == 0 && crc == 0;
        bad += check("LIST includes a wavetable slot as raw header+data with its CRC", ok);
    }
    bad += check("an empty project slot lists as length 0", list(2, &len, &crc) == 0 && len == 0);
    {   /* a slot of earlier firmware holds a shorter record (a FUN20, a 3840 B field image): it lists at its own
         * length, the one its magic goes with (the web checks the pair) */
        int ok;
        project_capture(&proj_scratch);
        ok = proj_pack_st(&st, &proj_scratch, PROJ_STORE_OLD) && !st_save(OBJ_PROJECT0, &st, PROJ_STORE_OLD) &&
             proj_fetch(0);
        bad += check("a slot holding a 3840 B record lists as 3840 B with that record's CRC",
                     ok && list(2, &len, &crc) == 0 && len == PROJ_STORE_OLD && crc == st_crc32(&st, PROJ_STORE_OLD));
        proj_slot_used = 0;
    }
    bad += check("GET of the runtime copy", get(0, 0, 64) == 0);
    project_save(0);
    bad += check("a project save after LIST reused the staging RAM: GET of the runtime is stale (5)",
                 get(0, 0, 64) == 5u);
    bad += check("a new LIST makes it readable again", list(0, &len, &crc) == 0 && get(0, 0, 64) == 0);
    usb.resets++;
    bad += check("a USB reset after LIST: GET is stale (5)", get(2, 0, 16) == 5u);

    {   /* LIST of filled project slots / FM6 patches must not clobber the runtime snapshot (id 0): listed CRC
         * and a full GET must still match the music captured at LIST, or the browser reports a CRC mismatch */
        static uint8_t got[sizeof(project_store_t)];
        uint32_t list_crc, off, ok, n;
        reset();
        trk[2].step[1] = (step_t){{67}, 1, ST_NOTE, 0, 96, 0, 0};
        project_save(0);                                             /* slot 0: different music from the runtime below */
        memset(&trk[2].step[1], 0, sizeof trk[2].step[1]);
        trk[0].step[0] = (step_t){{60}, 1, ST_NOTE, 0, 96, 0, 0};
        trk[1].step[2] = (step_t){{64}, 1, ST_NOTE, 0, 96, 0, 0};
        project_capture(&proj_scratch);
        proj_pack(&keep, &proj_scratch);
        upf_empty();
        upf_set(0, FM6_FACTORY[0]);                                  /* id 9 also packs into the staging RAM */
        list_crc = len = 0;
        ok = list(0, &len, &list_crc) == 0 && len == sizeof(project_store_t) &&
             list_crc == st_crc32(&keep, sizeof keep);
        for (off = 0; ok && off < sizeof keep; off += 256u) {
            n = sizeof keep - off > 256u ? 256u : (uint32_t)(sizeof keep - off);
            ok &= get(0, off, n) == 0 && ed_unpack7(rep + 9, rep_n - 9u, got + off, n) == n;
        }
        ok = ok && list_crc == st_crc32(got, sizeof keep) && !memcmp(got, &keep, sizeof keep);
        bad += check("LIST leaves the runtime snapshot intact across project / FM6 listing", ok);
    }

    /* settings */
    reset();
    memset(&ps, 0, sizeof ps);
    settings_export(&ps);
    ps.lowcut = 2;                                       /* SPEAKER BASS+ */
    ps.palette = palette_to_stored(6);
    bad += check("settings with SPEAKER BASS+ restore", put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 &&
                 settings.lowcut == 2u && fx_lowcut == 2u && settings.palette == 6u);
    ps.palette = 13;                                     /* an older backup: its PAPER (old id 13) */
    bad += check("settings of an older backup restore with the palette migrated",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && settings.palette == 6u);
    ps.palette = palette_to_stored(6);
    before = erases;
    ps.lowcut = 3;
    bad += check("settings with an unknown SPEAKER value are refused, nothing written",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 2u && erases == before && settings.lowcut == 2u);
    ps.lowcut = 0;
    ps.bold = hold_to_stored(0, 3);                      /* HOLD 0.6 s */
    bad += check("settings with HOLD 0.6 s restore",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && HOLD_MS[settings_hold] == 600u);
    ps.bold = 1;                                         /* an older backup (its font weight): HOLD 0.4 s */
    bad += check("settings of an older backup restore with HOLD 0.4 s",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && HOLD_MS[settings_hold] == 400u);
    before = erases;
    ps.bold = HOLD_TAG + 7u;
    bad += check("settings with an unknown HOLD value are refused, nothing written",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 2u && erases == before);
    ps.bold = 0;
    ps.zoom = LEDS_TAG | LEDS_INV;                       /* LEDS INV */
    bad += check("settings with LEDS INV restore",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && settings_leds == LEDS_INV);
    ps.zoom = 1;                                         /* an older backup (its large readout): LEDS DIM */
    bad += check("settings of an older backup restore with LEDS DIM",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && settings_leds == LEDS_DIM);
    before = erases;
    ps.zoom = LEDS_TAG | LEDS_DIM_LO;                    /* LEDS DIM LO, OFF (appended) */
    bad += check("settings with LEDS DIM LO restore",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && settings_leds == LEDS_DIM_LO);
    ps.zoom = LEDS_TAG | LEDS_OFF;
    bad += check("settings with LEDS OFF restore",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && settings_leds == LEDS_OFF);
    before = erases;
    ps.zoom = LEDS_TAG + 4u;
    bad += check("settings with an unknown LEDS value are refused, nothing written",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 2u && erases == before);
    ps.zoom = 0;
    bad += check("a wrong CRC is refused before any write",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps) ^ 1u) == 2u && erases == before);

    /* malformed requests */
    bad += check("an unknown object id is refused", put_begin(8, sizeof ps, 0) == 1u);
    bad += check("a project of the wrong length is refused", put_begin(2, sizeof(project_store_t) - 4u, 0) == 1u);
    bad += check("settings of the wrong length are refused", put_begin(1, sizeof ps + 4u, 0) == 1u);
    bad += check("a chunk without a begin is refused (5)", (ed_bk_put = 0, put_chunk(1, 0, (const uint8_t *)&ps, 16)) == 5u);
    put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
    bad += check("a chunk at the wrong offset is refused", put_chunk(1, 16, (const uint8_t *)&ps, 16) == 1u);
    bad += check("a chunk for another object is refused (5)", put_chunk(2, 0, (const uint8_t *)&ps, 16) == 5u);
    {
        uint32_t off = 0, step = sizeof ps - 8u;
        put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
        for (; off + 256u <= step; off += 256u)
            put_chunk(1, off, (const uint8_t *)&ps + off, 256);
        if (off < step) put_chunk(1, off, (const uint8_t *)&ps + off, step - off);
        bad += check("a chunk past the length is refused", put_chunk(1, step, (const uint8_t *)&ps, 16) == 1u);
    }
    bad += check("commit before all bytes arrived is refused", put_end(1, 2) == 2u && erases == before);
    put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
    put_chunk(1, 0, (const uint8_t *)&ps, 64);
    usb.resets++;
    bad += check("a USB reset during PUT ends it (5)", put_chunk(1, 64, (const uint8_t *)&ps + 64, 64) == 5u);
    put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
    fm1_ms += 16000u;
    bad += check("16 s without a chunk ends the PUT (5)", put_chunk(1, 0, (const uint8_t *)&ps, 64) == 5u);
    put_begin(2, sizeof st, 0);
    put_chunk(2, 0, (const uint8_t *)&ps, 64);
    project_save(1);
    bad += check("a project save during PUT (the staging RAM) ends it (5)",
                 put_chunk(2, 64, (const uint8_t *)&ps, 64) == 5u);
    transport_req = 1;
    bad += check("PUT while starting PLAY is refused (3)", put_begin(1, sizeof ps, 0) == 3u);
    transport_req = 0;

    /* projects */
    reset();
    trk[1].step[3] = (step_t){{64}, 1, ST_NOTE, 0, 96, 0, 0};
    project_capture(&proj_scratch);
    proj_pack(&st, &proj_scratch);
    bad += check("a FUN10 project restores into slot 3 (flash)",
                 put_all(4, &st, sizeof st, st_crc32(&st, sizeof st)) == 0 && project_used(2) &&
                 st_load(OBJ_PROJECT0 + 2, &proj_wire, sizeof proj_wire) == (int)sizeof st &&
                 !memcmp(&proj_wire, &st, sizeof st));
    {   /* FUN8 of 1.0.x (3584 bytes, 91 parameters) and FUN9 of 1.1 (3648, 99): restore as FUN10, E0 at P_E0, lanes
         * 100 %, the four 1.2 parameters at their defaults; a FUN9 with 64 motion records keeps them all */
        static uint8_t v8[3584], v9[3648];
        uint32_t k, ok;
        trk[2].p[P_E0] = 2;
        trk[2].p[P_LN3] = 90;
        project_capture(&proj_scratch);
        proj_scratch.t[2].p[P_LN3] = 127;              /* (FUN8 had no lane levels) */
        bad += check("a FUN8 project (1.0.x) restores as FUN10 into slot 2, E0 at P_E0, lane levels 100 %",
                     old_pack(v8, &proj_scratch, OLD_FUN8, 91u) == sizeof v8 &&
                     put_all(3, v8, sizeof v8, st_crc32(v8, sizeof v8)) == 0 && project_used(1) &&
                     proj_fetch(1) && proj_scratch.t[2].p[P_E0] == 2 &&
                     proj_scratch.t[2].p[P_LN3] == 127 && step_note(&proj_scratch.t[1].step[3], 0) == 64u);
        project_capture(&proj_scratch);
        for (k = 0; k < 64u; k++)
            proj_scratch.motion.event[k] = (motion_event_t){(uint8_t)(1u << 6 | k), P_REV, (int8_t)k};
        proj_scratch.motion.count = 64;
        proj_scratch.motion.on = 2;
        ok = old_pack(v9, &proj_scratch, OLD_FUN9, 99u) == sizeof v9 &&
             put_all(4, v9, sizeof v9, st_crc32(v9, sizeof v9)) == 0 && project_used(2) &&
             proj_fetch(2) && proj_scratch.t[2].p[P_E0] == 2 &&
             proj_scratch.t[2].p[P_LN3] == 90 && proj_scratch.motion.count == 64u && proj_scratch.motion.event[63].value == 63 &&
             proj_scratch.t[0].p[P_LSYNC] == 0 && proj_scratch.t[0].p[P_LTRIG] == 0 && proj_scratch.t[0].p[P_LPOL] == 0 &&
             proj_scratch.t[0].p[P_SQNT] == 1;
        bad += check("a FUN9 project (1.1) restores as FUN10 into slot 3: its 64 records, the lanes, LFO 2 / QUANTIZE defaults", ok);
    }
    {   /* 1.0.3: the FM6 patch bank (id 8) is retired; an older archive's bank moves into its user presets (ids 6, 7
         * restored first), the user presets' patches are id 9 */
        static up_bank_t ub;
        static fm6_bank_t bk;
        static uint8_t packed[ST_PAYLOAD_SECTOR], got[ST_PAYLOAD_SECTOR];
        uint8_t pk[FM6_PACKED];
        uint32_t pn;
        static const int16_t SL[5] = {8 + 2, 1, 8 + 4, 8 + 2, 8 + 2};
        memset(&ub, 0, sizeof ub);
        ub.magic = UP_BANK_MAGIC; ub.rsize = sizeof(up_rec_t); ub.nslot = UP_PER_BANK;
        for (uint32_t k = 0; k < 5u; k++) {             /* slots 1..5: B3, F2, B5 (empty), B3 on DRUM, B3 */
            up_rec_t *r = &ub.r[k];
            uint32_t e = k == 3u ? ENGI_DRUM : ENGI_FM6;
            r->used = UP_USED; r->ver = UP_VER; r->engine = (uint8_t)e; r->np = P_COUNT;
            r->name[0] = (char)('A' + k);
            for (uint32_t i = 0; i < P_COUNT; i++) up_set_value(r, i, param_desc_of(e, i)->def);
            up_set_value(r, P_E7, SL[k]);                 /* (the raw stored value: B slots as 1.0.2 wrote them) */
        }
        ub.r[4].name[1] = 'X';
        up_set_value(&ub.r[4], P_E0, 3);                 /* (another sound than slot 1's) */
        memset(&bk, 0, sizeof bk);
        bk.magic = FM6_BANK_MAGIC; bk.ver = 1; bk.nslot = FM6_BANK_N; bk.used = 1u << 2;
        memcpy(bk.v[2], FM6_FACTORY[5], FM6_PACKED);
        memcpy(bk.v[2] + 118, "BANK B3   ", 10);
        upf_empty();
        bad += check("the user presets of an old archive (id 6) restore", put_all(6, &ub, sizeof ub, st_crc32(&ub, sizeof ub)) == 0);
        bad += check("an old archive's FM6 bank (id 8) is taken (0)", put_all(8, &bk, sizeof bk, st_crc32(&bk, sizeof bk)) == 0);
        bad += check("  .. its B3 patch is now the B3 presets' own (slots 1 and 5)",
                     !upf_get(0, pk) && !memcmp(pk + 118, "BANK B3   ", 10) && !upf_get(4, pk) && !memcmp(pk + 118, "BANK B3", 7));
        bad += check("  .. an F slot, an empty B slot and a DRUM sound get none (factory / init / no patch, as before)",
                     upf_get(1, pk) && upf_get(2, pk) && upf_get(3, pk));
        pn = upf_pack(packed, sizeof packed);
        bad += check("  .. written to flash (the user presets' FM6 patches, packed)",
                     pn && st_load(OBJ_UPFM6, got, sizeof got) == (int)pn && !memcmp(got, packed, pn));
        up_load(0);
        bad += check("  .. and the B3 preset loads it, SLOT OWN", TSEL->eng_req == ENGI_FM6 &&
                     !memcmp(fm6_patch[song.sel] + FP_NAME, "BANK B3", 7) && TSEL->p[P_E7] == FM6_OWN);
        up_load(1);
        bad += check("  .. the F2 preset loads the factory patch, SLOT F2", TSEL->p[P_E7] == 1 &&
                     !memcmp(fm6_patch[song.sel] + FP_NAME, FM6_FACTORY[1] + 118, 10));
        bad += check("a restore of id 8 twice moves nothing more (idempotent)",
                     put_all(8, &bk, sizeof bk, st_crc32(&bk, sizeof bk)) == 0 &&
                     (pn = upf_pack(packed, sizeof packed)) && st_load(OBJ_UPFM6, got, sizeof got) == (int)pn &&
                     !memcmp(got, packed, pn));
        bad += check("an empty id 8 (a 1.0.3 archive) is taken and ignored", put_all(8, 0, 0, st_crc32(0, 0)) == 0 &&
                     st_load(OBJ_UPFM6, got, sizeof got) == (int)pn && !memcmp(got, packed, pn));
        bk.v[3][0] = 200;
        bad += check("an FM6 bank with a byte above 127 is refused (2)", put_all(8, &bk, sizeof bk, st_crc32(&bk, sizeof bk)) == 2u);
        pn = upf_pack(packed, sizeof packed);
        bad += check("id 8 lists empty, id 9 with the packed patches' length", list(8, &len, &crc) == 0 && len == 0 &&
                     list(9, &len, &crc) == 0 && len == pn && crc == st_crc32(packed, pn));
        upf_empty();
        bad += check("id 9 restores the user presets' FM6 patches (flash and RAM)",
                     put_all(9, packed, pn, st_crc32(packed, pn)) == 0 &&
                     !upf_get(0, pk) && !memcmp(pk + 118, "BANK B3", 7));
        packed[0] ^= 1;
        bad += check("an id 9 of another layout is refused (2), the patches kept",
                     put_all(9, packed, pn, st_crc32(packed, pn)) == 2u && !upf_get(0, pk));
        packed[0] ^= 1;
        bad += check("an id 9 of the wrong size is refused at commit (2)",
                     put_begin(9, pn - 4u, st_crc32(packed, pn - 4u)) == 0u &&
                         put_chunk(9, 0, packed, pn - 4u) == 0u && put_end(9, 2) == 2u);
        bad += check("id 10 (bank 2) accepts an empty object",
                     put_begin(10, 0, 0) == 0u && put_end(10, 2) == 0u);
        bad += check("id 10 lists empty until written", list(10, &len, &crc) == 0 && len == 0);
        bad += check("id 10 restores preset bank 2",
                     put_all(10, &ub, sizeof ub, st_crc32(&ub, sizeof ub)) == 0 && up_bank[2].magic == UP_BANK_MAGIC &&
                     up_used(32));
        bad += check("id 13 restores preset bank 5",
                     put_all(13, &ub, sizeof ub, st_crc32(&ub, sizeof ub)) == 0 && up_bank[5].magic == UP_BANK_MAGIC &&
                     up_used(80));
        bad += check("an id 14 is refused (1)", put_begin(14, 0, 0) == 1u);
    }
    bad += check("live slot 3 image loads for the flash-error check",
                 st_load(OBJ_PROJECT0 + 2, &st, sizeof st) == (int)sizeof st);
    erase_error = 1;
    memcpy(&keep, &st, sizeof keep);
    bad += check("a flash error on commit (4) keeps the previous flash image",
                 put_all(4, &st, sizeof st, st_crc32(&st, sizeof st)) == 4u &&
                 st_load(OBJ_PROJECT0 + 2, &proj_wire, sizeof proj_wire) == (int)sizeof keep &&
                 !memcmp(&proj_wire, &keep, sizeof keep) && project_used(2));
    erase_error = 0;
    bad += check("an empty project object clears the slot", put_all(4, 0, 0, st_crc32(0, 0)) == 0 && !project_used(2));
    st.raw[200] ^= 1;                                    /* hash no longer matches */
    bad += check("a project whose own hash fails is refused", put_all(4, &st, sizeof st, st_crc32(&st, sizeof st)) == 2u);

    /* the runtime */
    reset();
    trk[2].step[5] = (step_t){{67}, 1, ST_NOTE, 0, 96, 0, 0};
    trk[2].p[P_LEVEL] = 77;
    project_capture(&proj_scratch);
    proj_pack(&st, &proj_scratch);
    host_tracks_init();
    bad += check("the runtime object restores the tracks",
                 put_all(0, &st, sizeof st, st_crc32(&st, sizeof st)) == 0 && step_note(&trk[2].step[5], 0) == 67u &&
                 trk[2].p[P_LEVEL] == 77);

    /* user preset banks: unknown record versions are kept as bytes */
    reset();
    {
        static up_bank_t b;
        memset(&b, 0, sizeof b);
        b.magic = UP_BANK_MAGIC;
        b.rsize = sizeof(up_rec_t);
        b.nslot = UP_PER_BANK;
        b.r[4].used = UP_USED;
        b.r[4].ver = 99;                                 /* a future record */
        memcpy(b.r[4].name, "Future", 7);
        b.r[4].p[0] = 55;
        bad += check("a bank with a future record version restores byte for byte",
                     put_all(6, &b, sizeof b, st_crc32(&b, sizeof b)) == 0 &&
                     st_load(OBJ_UPRESET0, &proj_wire, sizeof proj_wire) == (int)sizeof b &&
                     !memcmp(&proj_wire, &b, sizeof b));
        b.nslot = 3;
        bad += check("a bank with the wrong slot count is refused", put_all(7, &b, sizeof b, st_crc32(&b, sizeof b)) == 2u);
    }
    printf("backup test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
