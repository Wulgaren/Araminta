/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B sector pair. A save goes to the copy that is not
 * the current one: erase the sector, program the payload pages (from offset
 * 256), then the 32-byte header at offset 0 LAST. The header is the commit
 * record (magic, seq, length, payload CRC, header CRC); on load the valid
 * copy with the newest seq wins (including wrap), so a write torn at any point leaves the
 * previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#define ST_MAGIC 0x554C4546u                   /* "FELU" */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (2u * ST_SECTOR - ST_PAYLOAD_OFF)   /* the largest object: a project spans two sectors */
#define ST_PAYLOAD_SECTOR (ST_SECTOR - ST_PAYLOAD_OFF)     /* a one-sector object's payload (3840) */

/* flash map (hal/fm1_flash_map.h has the constants and the write windows): the app area 0x4000..0x93000, then the
 * gap 0x93000..0x97000 (four sectors; Felucca's since 3.10). Autosave B takes 0x93000..0x94FFF (two sectors); the
 * remaining 0x95000..0x96FFF stay free. Settings 0xFC000; projects 0x97000..0x9EFFF (3.10: single-copy, two sectors a
 * slot; same eight sectors as the old A/B pairs); user sample slots 0xA0000..0xDBFFF (eng_sample.c: 80 KiB x 3);
 * user preset banks 0xDC000..0xDFFFF (upreset.c); the user presets' FM6 patches (up_fm6.c, since 1.0.3): copy A
 * 0x9F000, copy B 0xFE000; the autosave (project.c, 3.10 option 2): A 0xE5000..0xE6FFF (two sectors, grew in place),
 * B in the gap, so a 4096 B FUN20 wire fits with A/B kept; OTA staging 0xE0000..0xE4FFF. The FM6 voice bank (1.4.1,
 * fm6_vbank.c): 0xE7000 (untouched); a new bank is first staged in the autosave's older copy (fm6_vbank.c). Pre-3.10
 * autosave B lived at 0xE6000 (one sector); st_autosave_migrate moves a still-valid copy into the gap before A can
 * erase that sector. Those two sectors at 0x9F000 / 0xFE000 also held the FM6 patch bank of 1.0..1.0.2 (OBJ_FM6BANK,
 * retired). */
enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_FM6BANK = OBJ_UPRESET0 + 2, OBJ_UPFM6,
       OBJ_AUTOSAVE, OBJ_COUNT };
#define OBJ_FM6VB 32u                          /* the voice bank's commit records (fm6_vbank.c; >= OBJ_COUNT: st_load
                                                * never takes one, no object here takes its sectors) */
#define ST_AUTO_A 0xE5000u                     /* autosave A base (two sectors through 0xE6FFF) */
#define ST_AUTO_B 0x93000u                     /* autosave B base in the gap (two sectors through 0x94FFF) */
#define ST_AUTO_LEGACY_B 0xE6000u              /* pre-3.10 one-sector B; A's second sector after the split */

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;
_Static_assert(sizeof(st_hdr_t) == 32u, "storage commit record layout");

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t st_crc32_add(uint32_t c, const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step; running:
                                                                         * from 0xFFFFFFFF, the CRC is ~c */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}
static uint32_t st_crc32(const void *p, uint32_t n) { return ~st_crc32_add(0xFFFFFFFFu, p, n); }

static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
    if (obj == OBJ_SETTINGS)
        return 0xFC000u + copy * ST_SECTOR;
    if (obj == OBJ_FM6BANK || obj == OBJ_UPFM6)          /* (the same pair: see the flash map) */
        return copy ? 0xFE000u : 0x9F000u;
    if (obj == OBJ_AUTOSAVE)
        return copy ? ST_AUTO_B : ST_AUTO_A;            /* split: A above OTA, B in the gap */
    if (obj >= OBJ_UPRESET0)
        return 0xDC000u + (obj - OBJ_UPRESET0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    return 0x97000u + (obj - OBJ_PROJECT0) * 2u * ST_SECTOR + copy * ST_SECTOR;
}

/* Sectors one copy of an object spans. A project is single-copy (no B) and two sectors, so slot k still
 * occupies the two sectors that used to be its A and B: the eight project sectors do not move. The
 * autosave is also two sectors a copy (4096 B FUN20 wire); every other object stays one sector per copy. */
static uint32_t st_sectors(uint32_t obj)
{
    return (obj == OBJ_AUTOSAVE || (obj >= OBJ_PROJECT0 && obj < OBJ_UPRESET0)) ? 2u : 1u;
}

/* the largest payload this object can hold (its own sectors, less the commit record) */
static uint32_t st_pmax(uint32_t obj) { return st_sectors(obj) * ST_SECTOR - ST_PAYLOAD_OFF; }

static uint8_t st_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4), section(".pool")));   /* (main loop only) */

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (obj >= OBJ_COUNT || copy > 1u)
        return -1;
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->slot != copy || h->len > st_pmax(obj) ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h)   /* payload -> st_buf, CRC ok: 0 */
{
    if (st_read(st_sector(obj, copy) + ST_PAYLOAD_OFF, st_buf, h->len) || st_crc32(st_buf, h->len) != h->crc)
        return -1;
    return 0;
}

/* the current copy: the newest valid sequence (A on a tie), -1 when
 * neither is valid. Headers first, so only the winner's payload is read (it
 * is left in st_buf); *h gets its header. */
static int st_current(uint32_t obj, st_hdr_t *h)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u))) {
        if (st_body(obj, 1, &b) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body(obj, 0, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body(obj, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}

/* load the whole object into dst; returns its length, or -1 if it does not fit */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    if (obj >= OBJ_COUNT || st_current(obj, &h) < 0 || h.len > max)
        return -1;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}

/* save into copy `to` (0 A, 1 B; -1: the one that is not the current copy, as st_save). A single-copy
 * object (a project) always writes copy 0 over its whole span: its two sectors are erased first, so a save
 * is destructive there by design (the autosave keeps the music being saved). */
static int st_save_to(uint32_t obj, const void *src, uint32_t len, int to)
{
    uint32_t seq, base, off, nsec;
    int cur, rc, single;
    st_hdr_t h;
    if (obj >= OBJ_COUNT || len > st_pmax(obj) || to > 1)
        return -1;
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    nsec = st_sectors(obj);
    single = (obj >= OBJ_PROJECT0 && obj < OBJ_UPRESET0);   /* projects: one copy, destructive */
    if (single) {
        to = 0;
    } else if (to < 0) {
        to = cur == 0 ? 1 : 0;                            /* A/B: write the other copy */
    }
    base = st_sector(obj, (uint32_t)to);
    for (off = 0; off < len; off++)
        st_buf[off] = ((const uint8_t *)src)[off];    /* the driver wants RAM sources */
    for (off = 0; off < nsec; off++)
        if ((rc = st_erase(base + off * ST_SECTOR)) != 0)
            return rc;
    for (off = 0; off < len; off += 256u) {
        uint32_t n = len - off > 256u ? 256u : len - off;
        if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, st_buf + off, n)) != 0)
            return rc;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)to;
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(st_buf, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
        uint32_t c = (uint32_t)to;
        if (st_head(obj, c, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, c, &chk))
            return -7;
    }
    return 0;
}

static int st_save(uint32_t obj, const void *src, uint32_t len) { return st_save_to(obj, src, len, -1); }

/* Pre-3.10 autosave B was one sector at 0xE6000. A now spans 0xE5000..0xE6FFF, so a still-valid old B must move
 * into the gap (new B) before any 2-sector A write erases it. Idempotent and power-cut safe: a half-written gap
 * copy fails st_head/st_body and the next boot retries; the orphan at 0xE6000 is erased only after the new copy
 * verifies. A 2-sector A image already owns 0xE6000 (len > 3840), so that case is left alone. Call after fvb_boot
 * so a bank staged in the old B sector is finished first. */
static void st_autosave_migrate(void)
{
    st_hdr_t leg, a, chk;
    uint32_t off, n;
    int rc;
    if (st_read(ST_AUTO_LEGACY_B, &leg, sizeof leg))
        return;
    if (leg.magic != ST_MAGIC || leg.type != OBJ_AUTOSAVE || leg.slot != 1u || leg.len == 0u ||
        leg.len > ST_PAYLOAD_SECTOR || leg.hcrc != st_crc32(&leg, sizeof leg - 4u))
        return;
    if (st_head(OBJ_AUTOSAVE, 0, &a) == 0 && a.len > ST_PAYLOAD_SECTOR)
        return;                                          /* 0xE6000 is already A's second sector */
    if (st_read(ST_AUTO_LEGACY_B + ST_PAYLOAD_OFF, st_buf, leg.len) || st_crc32(st_buf, leg.len) != leg.crc)
        return;
    if (st_head(OBJ_AUTOSAVE, 1, &chk) == 0) {           /* gap B already holds a copy: drop the orphan */
        st_erase(ST_AUTO_LEGACY_B);
        return;
    }
    for (off = 0; off < 2u; off++)
        if ((rc = st_erase(ST_AUTO_B + off * ST_SECTOR)) != 0)
            return;
    for (off = 0; off < leg.len; off += 256u) {
        n = leg.len - off > 256u ? 256u : leg.len - off;
        if ((rc = st_prog(ST_AUTO_B + ST_PAYLOAD_OFF + off, st_buf + off, n)) != 0)
            return;
    }
    if ((rc = st_prog(ST_AUTO_B, &leg, sizeof leg)) != 0) /* same seq / slot 1 */
        return;
    if (st_head(OBJ_AUTOSAVE, 1, &chk) || memcmp(&chk, &leg, sizeof leg) || st_body(OBJ_AUTOSAVE, 1, &chk))
        return;
    st_erase(ST_AUTO_LEGACY_B);
}
