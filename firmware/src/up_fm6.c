/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The user presets' FM6 patches (since 1.0.3): an FM6 user preset carries its track's patch, as a project does.
 * One storage.c object (OBJ_UPFM6, A/B: 0x9F000 / 0xFE000), mirrored in RAM (the pool): per user preset slot the
 * 128-byte packed patch (eng_fm6.c fm6_pack) with its 7-bit bytes packed 8 to the byte (112 bytes), and a tag.
 *
 * The tag ties an entry to its record: a hash of the user preset record (upreset.c up_rec_t) without its name. An
 * entry counts only while its record is an FM6 sound and still has that hash, so a rename keeps the patch, and any
 * other change of the record (UP_PUT from an editor that sends no patch, an erase, a restore of the banks, a power
 * cut between the record's write and this object's) leaves the record without one. Such a record loads as before
 * 1.0.3: SLOT F n -> that factory patch, OWN -> the init voice. The record's write comes first, this object's second
 * (up_store): a cut between them never gives a record another record's patch.
 *
 * The FM6 patch bank of 1.0..1.0.2 (OBJ_FM6BANK, 27 slots B1..B27 that SLOT 8..34 loaded; the editor's FM6 target
 * 1, backup id 8) is retired. Its patches move into the user presets that used them, once (upf_boot):
 *   1. no valid OBJ_UPFM6 copy: the bank's newest valid copy is read; every FM6 user preset whose stored SLOT is
 *      8..34 (B1..B27) and has no entry gets that bank slot's patch (an empty bank slot: none, the init voice as
 *      before; SLOT F1..F8: none, which loads the factory patch as before);
 *   2. the object is written into the sector that does NOT hold that bank copy (st_save_to), and read back;
 *   3. a valid OBJ_UPFM6 copy is the "migration done" mark: the bank is never read again, and the next save of
 *      this object (A/B as any other) may erase the bank's last sector.
 * A power cut in 2 leaves the bank's copy whole and no valid OBJ_UPFM6: the next boot starts again at 1
 * (idempotent: the records did not change). No bank, or no flash: nothing to move, nothing written.
 * A full backup of older firmware (id 8, the bank) restores the same way into the restored user presets
 * (upf_migrate, editor_backup.c). Included by upreset.c.
 *
 * Ver 2: UP_SLOTS may exceed one sector. RAM holds at most UPF_MAX_USED dense entries (used bits cover all slots);
 * flash is packed (header + used entries in slot order). Legacy ver-1 3728 B (32 slots) still loads. */
#define FM6_BANK_MAGIC 0x42364D46u               /* "FM6B": the retired bank (read only) */
#define FM6_BANK_N 27u
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* 1, FM6_BANK_N */
    uint32_t used;                               /* bit k: slot k holds a patch */
    uint32_t rsv;
    uint8_t v[FM6_BANK_N][FM6_PACKED];
} fm6_bank_t;
_Static_assert(sizeof(fm6_bank_t) == 3472u, "FM6 bank layout");

static int fm6_bank_valid(const fm6_bank_t *b)
{
    uint32_t k, i;
    if (b->magic != FM6_BANK_MAGIC || b->ver != 1u || b->nslot != FM6_BANK_N || (b->used >> FM6_BANK_N))
        return 0;
    for (k = 0; k < FM6_BANK_N; k++)
        for (i = 0; i < FM6_PACKED; i++)
            if (b->v[k][i] > 127u)
                return 0;
    return 1;
}

#define UPF_MAGIC 0x36465055u                    /* "UPF6" */
#define UPF_PK 112u                              /* 128 x 7 bits */
#define UPF_VER 2u                               /* 2: packed flash; used[3] covers UP_SLOTS */
#define UPF_VER1_SLOTS 32u                       /* on-flash ver 1 (1.0.3..3.9): 32 slots, 3728 B */
#define UPF_VER1_SIZE 3728u
#define UPF_MAX_USED 32u                         /* sector budget: 20 + 32 * 116 = 3732 */
#define UPF_HDR 20u                              /* magic + ver/nslot + used[3] */
typedef struct {
    uint32_t tag;                                /* upf_tag of the record it belongs to */
    uint8_t pk[UPF_PK];
} upf_ent_t;
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* UPF_VER, UP_SLOTS */
    uint32_t used[3];                            /* bit k: dense e[] holds slot k's patch */
    upf_ent_t e[UPF_MAX_USED];                   /* dense, ascending slot order */
} upf_t;
_Static_assert(sizeof(upf_t) == UPF_HDR + UPF_MAX_USED * sizeof(upf_ent_t), "user preset FM6 patches RAM layout");
_Static_assert(sizeof(upf_t) <= 3840u, "user preset FM6 patches at most one sector (ST_PAYLOAD_SECTOR)");
_Static_assert(sizeof(up_rec_t) == 192u && sizeof(((up_rec_t *)0)->name) == 12u, "upf_tag: the name at bytes 4..15");
static upf_t upf __attribute__((section(".pool")));

static int upf_valid(const upf_t *u)
{
    return u->magic == UPF_MAGIC && u->ver == UPF_VER && u->nslot == UP_SLOTS;
}

static void upf_empty(void)
{
    memset(&upf, 0, sizeof upf);
    upf.magic = UPF_MAGIC;
    upf.ver = UPF_VER;
    upf.nslot = UP_SLOTS;
}

static int upf_bit(uint32_t k)
{
    return k < UP_SLOTS && ((upf.used[k / 32u] >> (k % 32u)) & 1u);
}

static void upf_bit_set(uint32_t k, int on)
{
    uint32_t *w = &upf.used[k / 32u], m = 1u << (k % 32u);
    if (on) *w |= m;
    else *w &= ~m;
}

static uint32_t upf_nused(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < 3u; i++) {
        uint32_t w = upf.used[i];
        while (w) {
            n += w & 1u;
            w >>= 1;
        }
    }
    return n;
}

/* dense index of slot k among used bits below k (assumes bit k is set, or would be after insert) */
static uint32_t upf_idx(uint32_t k)
{
    uint32_t i, n = 0;
    for (i = 0; i < k; i++)
        if (upf_bit(i))
            n++;
    return n;
}

/* Pack used entries into dst (one sector). Returns length, or 0 if too many used patches for one sector. */
static uint32_t upf_pack(uint8_t *dst, uint32_t max)
{
    uint32_t k, off = UPF_HDR, di = 0;
    if (max < UPF_HDR)
        return 0;
    for (k = 0; k < UP_SLOTS; k++)
        if (upf_bit(k)) {
            if (off + sizeof(upf_ent_t) > max || di >= UPF_MAX_USED)
                return 0;
            memcpy(dst + off, &upf.e[di], sizeof(upf_ent_t));
            off += (uint32_t)sizeof(upf_ent_t);
            di++;
        }
    memset(dst, 0, UPF_HDR);
    ((uint32_t *)dst)[0] = UPF_MAGIC;
    dst[4] = (uint8_t)UPF_VER;
    dst[5] = 0;
    dst[6] = (uint8_t)UP_SLOTS;
    dst[7] = 0;
    memcpy(dst + 8, upf.used, 12);
    return off;
}

/* Load a packed ver-2 image, or a legacy ver-1 3728 B blob, into the RAM mirror. */
static int upf_unpack(const uint8_t *src, uint32_t len)
{
    uint32_t k, off, di;
    uint32_t used[3];
    if (len == UPF_VER1_SIZE && ((const uint32_t *)src)[0] == UPF_MAGIC && src[4] == 1u &&
        src[6] == (uint8_t)UPF_VER1_SLOTS && src[7] == 0) {
        const uint32_t *u32 = (const uint32_t *)(src + 8);
        uint32_t u = u32[0];
        upf_empty();
        upf.used[0] = u;                                 /* ver 1: uint32 used at +8, rsv at +12, e[] at +16 */
        di = 0;
        for (k = 0; k < UPF_VER1_SLOTS; k++)
            if ((u >> k) & 1u) {
                if (di >= UPF_MAX_USED)
                    return 0;
                memcpy(&upf.e[di], src + 16u + k * sizeof(upf_ent_t), sizeof(upf_ent_t));
                di++;
            }
        return 1;
    }
    if (len < UPF_HDR || ((const uint32_t *)src)[0] != UPF_MAGIC || src[4] != (uint8_t)UPF_VER ||
        src[6] != (uint8_t)UP_SLOTS)
        return 0;
    memcpy(used, src + 8, 12);
    upf_empty();
    memcpy(upf.used, used, 12);
    off = UPF_HDR;
    di = 0;
    for (k = 0; k < UP_SLOTS; k++)
        if ((used[k / 32u] >> (k % 32u)) & 1u) {
            if (off + sizeof(upf_ent_t) > len || di >= UPF_MAX_USED)
                return 0;
            memcpy(&upf.e[di], src + off, sizeof(upf_ent_t));
            off += (uint32_t)sizeof(upf_ent_t);
            di++;
        }
    return off == len;
}

static uint32_t upf_tag(const up_rec_t *r)       /* FNV-1a of the record without its name (bytes 4..15) */
{
    const uint8_t *b = (const uint8_t *)r;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof *r; i++)
        if (i < 4u || i >= 16u)
            h = (h ^ b[i]) * 16777619u;
    return h;
}

static int upf_fm6(uint32_t k) { return up_used(k) && up_rec(k)->engine == ENGI_FM6; }

/* a 128-byte packed record's 7-bit bytes, 8 to 7 bytes (UPF_PK) and back (the voice bank too, fm6_vbank.c) */
static void upf_pk_get(const uint8_t *d, uint8_t *pk)
{
    uint32_t i, acc = 0, n = 0, o = 0;
    for (i = 0; i < FM6_PACKED; i++) {
        while (n < 7u) {
            acc |= (uint32_t)d[o++] << n;
            n += 8u;
        }
        pk[i] = (uint8_t)(acc & 127u);
        acc >>= 7;
        n -= 7u;
    }
}
static void upf_pk_put(const uint8_t *pk, uint8_t *d)
{
    uint32_t i, acc = 0, n = 0, o = 0;
    memset(d, 0, UPF_PK);
    for (i = 0; i < FM6_PACKED; i++) {
        acc |= (uint32_t)(pk[i] & 127u) << n;
        n += 7u;
        while (n >= 8u) {
            d[o++] = (uint8_t)acc;
            acc >>= 8;
            n -= 8u;
        }
    }
}

/* slot k's patch -> pk (128 bytes), 0 = it has one */
static int upf_get(uint32_t k, uint8_t *pk)
{
    uint32_t di;
    if (k >= UP_SLOTS || !upf_valid(&upf) || !upf_bit(k) || !upf_fm6(k))
        return 1;
    di = upf_idx(k);
    if (di >= UPF_MAX_USED || upf.e[di].tag != upf_tag(up_rec(k)))
        return 1;
    upf_pk_get(upf.e[di].pk, pk);
    return 0;
}

/* slot k's patch = pk (128 bytes; every value into its range), tagged with the record as it is now. RAM only */
static void upf_set(uint32_t k, const uint8_t *pk)
{
    uint8_t v[FP_SIZE + 1u], c[FM6_PACKED];
    uint32_t di;
    int had;
    if (k >= UP_SLOTS)
        return;
    if (!upf_valid(&upf))
        upf_empty();
    had = upf_bit(k);
    if (!had && upf_nused() >= UPF_MAX_USED)
        return;                                          /* sector / RAM cap: keep existing 32 */
    di = upf_idx(k);
    if (!had) {
        uint32_t n = upf_nused(), i;
        for (i = n; i > di; i--)
            upf.e[i] = upf.e[i - 1u];
        upf_bit_set(k, 1);
    }
    fm6_unpack(pk, v);
    fm6_pack(v, c);
    upf_pk_put(c, upf.e[di].pk);
    upf.e[di].tag = upf_tag(up_rec(k));
}

/* the object to flash: 0 ok, 2 flash error, 3 no flash (kept in RAM). Its first write keeps the retired bank's
 * newest copy (see the top). Packed so UP_SLOTS still fit one sector (at most UPF_MAX_USED used patches). */
static int upf_save(void)
{
#if FELUCCA_FLASH
    st_hdr_t h;
    int to = -1;
    uint32_t n;
    if (!flash_ok)
        return 3;
    /* pick the target copy before packing: st_current loads into st_buf */
    if (st_current(OBJ_UPFM6, &h) < 0)
        to = st_current(OBJ_FM6BANK, &h) == 0 ? 1 : 0;
    n = upf_pack(st_buf, ST_PAYLOAD_SECTOR);
    if (!n)
        return 2;
    return st_save_to(OBJ_UPFM6, st_buf, n, to) ? 2 : 0;   /* st_save_to: src == st_buf keeps the pack */
#else
    return 3;
#endif
}

/* the retired bank's patches -> the FM6 user presets whose stored SLOT is a B slot and have no patch yet; the count */
static uint32_t upf_migrate(const fm6_bank_t *b)
{
    uint32_t k, moved = 0;
    if (!upf_valid(&upf))
        upf_empty();
    for (k = 0; k < UP_SLOTS; k++) {
        const up_rec_t *r = up_rec(k);
        uint8_t pk[FM6_PACKED];
        int32_t s;
        if (!upf_fm6(k) || !upf_get(k, pk))
            continue;
        s = up_value(r, r->np - 1u) - (int32_t)FM6_NFACTORY;   /* SLOT (P_E7): the record's last value */
        if (s >= 0 && s < (int32_t)FM6_BANK_N && ((b->used >> s) & 1u)) {
            upf_set(k, b->v[s]);
            moved++;
        }
    }
    return moved;
}

static void upf_boot(void)                       /* up_boot, after the user preset banks */
{
#if FELUCCA_FLASH
    st_hdr_t h;
    int n;
    if (flash_ok && (n = st_load(OBJ_UPFM6, st_buf, ST_PAYLOAD_SECTOR)) > 0 && upf_unpack(st_buf, (uint32_t)n))
        return;
    upf_empty();
    if (flash_ok && st_current(OBJ_FM6BANK, &h) >= 0 && h.len == sizeof(fm6_bank_t) &&
        fm6_bank_valid((const fm6_bank_t *)st_buf)) {
        upf_migrate((const fm6_bank_t *)st_buf);   /* (st_buf holds the bank until upf_save) */
        upf_save();
    }
#else
    upf_empty();
#endif
}

/* up_load: track t (its values just loaded from slot k) gets the record's patch; without one, as before 1.0.3:
 * SLOT F n that factory patch, OWN the init voice. SLOT then shows F n or OWN (fm6_adopt) */
static void upf_track_load(track_t *t, uint32_t k)
{
    uint8_t pk[FM6_PACKED], v[FP_SIZE + 1u];
    uint32_t tr = (uint32_t)(t - trk);
    if (tr >= NTRK || t->eng_req != ENGI_FM6)
        return;
    if (upf_get(k, pk)) {
        int32_t s = t->p[P_E7];
        memcpy(pk, s >= 0 && s < (int32_t)FM6_NFACTORY ? FM6_FACTORY[s] : FM6_INIT, FM6_PACKED);
    }
    fm6_unpack(pk, v);
    if (!up_vib(k) && t->p[P_E5]) {                     /* a pre-VIB record: P_E5 was VMOD */
        fm6_old_vmod(v, t->p);
        t->p[P_E5] = 0;
    }
    fm6_set_patch(tr, v);
    fm6_adopt(tr);
}

/* up_store: slot k (its record just written from track tr) gets that track's patch; upf_save's result */
static int upf_store(uint32_t k, uint32_t tr)
{
    uint8_t pk[FM6_PACKED];
    fm6_pack(fm6_patch[tr % NTRK], pk);
    upf_set(k, pk);
    return upf_save();
}
