/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The FM6 voice bank (1.4.1, Discussion #168): 32 voices loaded at once from the web editor (a 32-voice .syx, its
 * 128-byte packed records as they are), picked on the device with FM6's SLOT B1..B128 (eng_fm6.c). A voice is read
 * from flash when SLOT loads it, into the track's patch as F1..F14 load theirs: the patch is the track's from then on,
 * and a project or a user preset keeps it whatever the bank holds later. RAM: the used mask (eng_fm6.c
 * fm6_bank_used) and the transfer's state, in the pool; no copy of the bank.
 *
 * Where: four home sectors (not storage.c objects). Bank 0 at 0xE7000 (FL_VBANK_*), banks 1..3 at 0xF8000, 0xF9000
 * and 0xF4000 in the reclaim band (fm1_flash_map.h FL_RECLAIM_*). Gap 0x95000 holds user-preset bank 5, not FM6.
 * Sector: the commit record (storage.c st_hdr_t, type OBJ_FM6VB) at 0, the bank's header (fvb_info_t) at 224, the
 * 32 voices at 256..3839, 112 bytes each (the record's 7-bit bytes 8 to 7, up_fm6.c upf_pk_put); the record's CRC
 * covers 224..3839. The tail from 0xF00 on stays erased. 0xE8000 and stock outside F4000..F9FFF stay untouched.
 *
 * Commit slot: home uses slot = bank (0..3); a staged transfer uses slot = 0x10 | bank. Boot still accepts a
 * legacy bank-0 stage with slot == 1. A new bank (FM6B_BEGIN / WRITE / END) is written into the autosave's older
 * copy first, committed with the staged slot, then copied to that bank's home. A power cut leaves the old bank or
 * the new one. fvb_boot also finishes a bank still staged at the pre-3.10 B address 0xE6000. Included by upreset.c. */
static const uint32_t fvb_home[FM6_NBANKS] = { 0xE7000u, 0xF8000u, 0xF9000u, 0xF4000u };
#define FVB_INFO 224u                          /* the bank's header; the voices from FVB_VOICES */
#define FVB_VOICES 256u
#define FVB_MAGIC 0x56364D46u                  /* "FM6V" */
#define FVB_SLOT_HOME(b)   ((uint32_t)(b))
#define FVB_SLOT_STAGED(b) (0x10u | (uint32_t)(b))
typedef struct {
    uint32_t magic;
    uint16_t ver, n;                           /* 1, FM6_NBANK */
    uint32_t used;                             /* bit k: voice k (B k+1) is there */
    char name[12];                             /* the bank's name: 10 ASCII bytes, space-padded, then 0 0 */
    uint32_t rsv[2];
} fvb_info_t;
_Static_assert(sizeof(fvb_info_t) == FVB_VOICES - FVB_INFO, "FM6 voice bank header");
#define FVB_LEN (FVB_VOICES - FVB_INFO + FM6_NBANK * UPF_PK)   /* the record's payload: 3616 bytes */
_Static_assert(FVB_INFO + FVB_LEN == 0xF00u, "the sector's tail (0xF00..) stays erased");

static struct {
    uint32_t used;                             /* the voices written since FM6B_BEGIN */
    uint8_t on, copy;                          /* a transfer is open; its sector: the autosave's copy A (0) / B (1) */
    uint8_t bank;                              /* target bank 0..2 for this transfer */
} fvb_tx __attribute__((section(".pool")));    /* (zeroed at boot) */

/* the bank was replaced: a track on a bank slot keeps the voice it loaded, as its own patch (SLOT OWN) */
static void fm6_bank_changed(void)
{
    uint32_t tr;
    for (tr = 0; tr < NTRK; tr++)
        if (trk[tr].eng_req == ENGI_FM6 && fm6_bank_slot(fm6_slot[tr]) >= 0)
            fm6_adopt(tr);
}

#if FELUCCA_FLASH
_Static_assert(FVB_LEN <= ST_PAYLOAD_MAX, "the payload fits st_buf");
static uint32_t fvb_stage(void) { return st_sector(OBJ_AUTOSAVE, fvb_tx.copy); }

/* n bytes from RAM to flash at off, a page (256 bytes) at a time */
static int fvb_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    while (n) {
        uint32_t k = 256u - (off & 255u);
        if (k > n)
            k = n;
        if (st_prog(off, s, k))
            return -1;
        off += k;
        s += k;
        n -= k;
    }
    return 0;
}

/* a whole bank at base: its commit record (slot as given) and payload valid; 0, with *h its commit record and
 * st_buf its payload */
static int fvb_valid(uint32_t base, uint32_t slot, st_hdr_t *h)
{
    const fvb_info_t *in = (const fvb_info_t *)st_buf;
    if (st_read(base, h, sizeof *h) || h->magic != ST_MAGIC || h->type != OBJ_FM6VB || h->slot != slot ||
        h->len != FVB_LEN || h->hcrc != st_crc32(h, sizeof *h - 4u) || st_read(base + FVB_INFO, st_buf, FVB_LEN) ||
        st_crc32(st_buf, FVB_LEN) != h->crc)
        return -1;
    return in->magic == FVB_MAGIC && in->ver == 1u && in->n == FM6_NBANK ? 0 : -1;
}

/* staged commit slot -> bank index, or -1. Legacy slot 1 is bank 0 staged. */
static int fvb_staged_bank(uint32_t slot)
{
    if (slot == 1u)
        return 0;
    if ((slot & 0xF0u) == 0x10u && (slot & 0x0Fu) < FM6_NBANKS)
        return (int)(slot & 0x0Fu);
    return -1;
}

/* any valid OBJ_FM6VB at base (any slot); 0 with *h and st_buf filled */
static int fvb_any(uint32_t base, st_hdr_t *h)
{
    const fvb_info_t *in = (const fvb_info_t *)st_buf;
    if (st_read(base, h, sizeof *h) || h->magic != ST_MAGIC || h->type != OBJ_FM6VB ||
        h->len != FVB_LEN || h->hcrc != st_crc32(h, sizeof *h - 4u) || st_read(base + FVB_INFO, st_buf, FVB_LEN) ||
        st_crc32(st_buf, FVB_LEN) != h->crc)
        return -1;
    return in->magic == FVB_MAGIC && in->ver == 1u && in->n == FM6_NBANK ? 0 : -1;
}

/* the commit record of the payload in st_buf, at base (written last) */
static int fvb_commit(uint32_t base, uint32_t slot, uint32_t seq)
{
    st_hdr_t h;
    h.magic = ST_MAGIC;
    h.type = OBJ_FM6VB;
    h.slot = (uint16_t)slot;
    h.seq = seq;
    h.len = FVB_LEN;
    h.crc = st_crc32(st_buf, FVB_LEN);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    return st_prog(base, &h, sizeof h);
}

/* flat voice k (0..95) -> pk (128 bytes): 0, 1 = none there (eng_fm6.c fm6_bank_read). */
static int fvb_read(uint32_t k, uint8_t *pk)
{
    uint8_t d[UPF_PK];
    uint32_t bank = k / FM6_NBANK, voice = k % FM6_NBANK;
    if (bank >= FM6_NBANKS || !((fm6_bank_used[bank] >> voice) & 1u))
        return 1;
    if (st_read(fvb_home[bank] + FVB_VOICES + voice * UPF_PK, d, UPF_PK))
        return 1;
    upf_pk_get(d, pk);
    return 0;
}

/* up_boot, and after a bank was staged: a staged bank newer than its home is copied there; then each home's
 * used mask. Also finishes a bank still staged at the pre-3.10 autosave B (0xE6000). Home headers are
 * compared from a saved st_hdr_t so fvb_valid(home) never clobbers the staged payload in st_buf. */
static void fvb_boot(void)
{
    st_hdr_t home_h[FM6_NBANKS], s, t;
    int home_ok[FM6_NBANKS];
    uint32_t c, best = 3u, base[3];
    int bank = -1, b;
    memset(fm6_bank_used, 0, sizeof fm6_bank_used);
    fm6_bank_read = fvb_read;
    if (!flash_ok)
        return;
    for (b = 0; b < (int)FM6_NBANKS; b++)
        home_ok[b] = !fvb_valid(fvb_home[b], FVB_SLOT_HOME(b), &home_h[b]);   /* 1 = home present */
    base[0] = st_sector(OBJ_AUTOSAVE, 0);
    base[1] = st_sector(OBJ_AUTOSAVE, 1);
    base[2] = ST_AUTO_LEGACY_B;                          /* pre-split B; skipped when it equals a live copy */
    for (c = 0; c < 3u; c++) {
        int sb;
        if (c == 2u && (base[2] == base[0] || base[2] == base[1]))
            continue;
        if (fvb_any(base[c], &t) || (sb = fvb_staged_bank(t.slot)) < 0)
            continue;
        if (!(home_ok[sb] == 0 || (t.seq != home_h[sb].seq && t.seq - home_h[sb].seq < 0x80000000u)))
            continue;                                    /* home present and not older than this stage */
        if (best > 2u || (t.seq != s.seq && t.seq - s.seq < 0x80000000u)) {
            s = t;
            best = c;
            bank = sb;
        }
    }
    if (best < 3u && bank >= 0 && !fvb_any(base[best], &s) && fvb_staged_bank(s.slot) == bank &&
        !st_erase(fvb_home[bank]) && !fvb_prog(fvb_home[bank] + FVB_INFO, st_buf, FVB_LEN))
        fvb_commit(fvb_home[bank], FVB_SLOT_HOME(bank), s.seq);
    for (b = 0; b < (int)FM6_NBANKS; b++)
        if (!fvb_valid(fvb_home[b], FVB_SLOT_HOME(b), &s))
            fm6_bank_used[b] = ((const fvb_info_t *)st_buf)->used;
}

/* the transfer's sector holds only what it wrote: no autosave took it since FM6B_BEGIN (its commit record still
 * erased); else the transfer ends */
static int fvb_tx_ok(void)
{
    uint8_t b[32];
    uint32_t i, x = 0xFFu;
    if (!fvb_tx.on || st_read(fvb_stage(), b, sizeof b))
        return 0;
    for (i = 0; i < sizeof b; i++)
        x &= b[i];
    if (x != 0xFFu)
        fvb_tx.on = 0;
    return fvb_tx.on;
}

/* FM6B_BEGIN: bank 0..2, the sector erased, no voice yet. rc 0, 1 bank, 4 flash */
static uint32_t fvb_begin(uint32_t bank)
{
    st_hdr_t h;
    fvb_tx.on = 0;
    if (bank >= FM6_NBANKS)
        return 1;
    if (!flash_ok)
        return 4;
    fvb_tx.bank = (uint8_t)bank;
    fvb_tx.copy = st_current(OBJ_AUTOSAVE, &h) == 0 ? 1u : 0u;   /* the copy st_save writes next */
    if (st_erase(fvb_stage()))
        return 4;
    fvb_tx.used = 0;
    fvb_tx.on = 1;
    return 0;
}

/* FM6B_WRITE: voice k = pk (128 bytes), once each. rc 0, 1 index, 4 flash, 5 no transfer */
static uint32_t fvb_write(uint32_t k, const uint8_t *pk)
{
    uint8_t d[UPF_PK];
    if (!fvb_tx_ok())
        return 5;
    if (k >= FM6_NBANK || ((fvb_tx.used >> k) & 1u))
        return 1;
    upf_pk_put(pk, d);
    if (fvb_prog(fvb_stage() + FVB_VOICES + k * UPF_PK, d, UPF_PK)) {
        fvb_tx.on = 0;
        return 4;
    }
    fvb_tx.used |= 1u << k;
    return 0;
}

/* FM6B_END: name (10 bytes), crc: the CRC-32 of the written voices' records in index order. Staged, committed,
 * copied to the transfer's home. rc 0, 2 the CRC (nothing changed), 4 flash, 5 no transfer */
static uint32_t fvb_end(const uint8_t *name, uint32_t crc)
{
    fvb_info_t *in = (fvb_info_t *)st_buf;
    st_hdr_t h;
    uint32_t k, c = 0xFFFFFFFFu, seq, base = fvb_stage(), bank = fvb_tx.bank, home, stg;
    if (!fvb_tx_ok())
        return 5;
    fvb_tx.on = 0;
    if (bank >= FM6_NBANKS)
        return 1;
    home = fvb_home[bank];
    stg = FVB_SLOT_STAGED(bank);
    for (k = 0; k < FM6_NBANK; k++)
        if ((fvb_tx.used >> k) & 1u) {
            uint8_t d[UPF_PK], pk[FM6_PACKED];
            if (st_read(base + FVB_VOICES + k * UPF_PK, d, UPF_PK))
                return 4;
            upf_pk_get(d, pk);
            c = st_crc32_add(c, pk, FM6_PACKED);
        }
    if (~c != crc)
        return 2;
    seq = fvb_valid(home, FVB_SLOT_HOME(bank), &h) ? 0u : h.seq;   /* after the newest for this bank */
    for (k = 0; k < 2u; k++) {
        if (fvb_any(st_sector(OBJ_AUTOSAVE, k), &h))
            continue;
        if (fvb_staged_bank(h.slot) == (int)bank && h.seq - seq < 0x80000000u)
            seq = h.seq;
    }
    seq++;
    if (st_read(base + FVB_INFO, st_buf, FVB_LEN))           /* the voices as written, the header still erased */
        return 4;
    memset(in, 0, sizeof *in);
    in->magic = FVB_MAGIC;
    in->ver = 1;
    in->n = FM6_NBANK;
    in->used = fvb_tx.used;
    for (k = 0; k < 10u; k++)
        in->name[k] = (char)(name[k] >= 32u && name[k] <= 126u ? name[k] : ' ');
    if (fvb_prog(base + FVB_INFO, in, sizeof *in) || fvb_commit(base, stg, seq) || fvb_valid(base, stg, &h))
        return 4;
    fvb_boot();                                              /* the copy to this bank's home, all used masks */
    fm6_bank_changed();
    return fvb_valid(home, FVB_SLOT_HOME(bank), &h) || h.seq != seq ? 4u : 0u;
}

/* FM6B_LIST: the bank at its home -> its header (0 = none) */
static const fvb_info_t *fvb_info(uint32_t bank)
{
    st_hdr_t h;
    if (bank >= FM6_NBANKS || !flash_ok)
        return 0;
    return !fvb_valid(fvb_home[bank], FVB_SLOT_HOME(bank), &h) ? (const fvb_info_t *)st_buf : 0;
}
#else
static void fvb_boot(void) {}
static uint32_t fvb_begin(uint32_t bank) { (void)bank; return 4; }
static uint32_t fvb_write(uint32_t k, const uint8_t *pk) { (void)k; (void)pk; return 5; }
static uint32_t fvb_end(const uint8_t *name, uint32_t crc) { (void)name; (void)crc; return 5; }
static const fvb_info_t *fvb_info(uint32_t bank) { (void)bank; return 0; }
#endif
