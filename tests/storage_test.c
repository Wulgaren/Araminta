/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of firmware/src/storage.c against a simulated NOR flash:
 * erase -> 0xFF, program can only clear bits, page writes must not wrap. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define __attribute__(x)   /* (the sources' .pool sections: no Mach-O section names) */

static uint8_t nor[0x100000];
static int fail_after = -1;            /* torn-write injection: stop after N programs */
static int fail_bytes = -1;            /* power loss after any programmed byte */
static int erase_error, write_protected;
static uint32_t io_calls;

static int st_read(uint32_t off, void *dst, uint32_t n) { io_calls++; memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    io_calls++;
    if (erase_error) return -8;
    if (!write_protected) memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    io_calls++;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    if ((off & 0xFFu) + n > 256u) {
        printf("page wrap at %#x\n", off);
        exit(1);
    }
    for (i = 0; i < n; i++) {
        if (fail_bytes == 0) return -9;
        if (fail_bytes > 0) fail_bytes--;
        if (!write_protected) nor[off + i] &= s[i];
    }
    return 0;
}
#include "../firmware/src/storage.c"
#include "../firmware/hal/fm1_flash_map.h"   /* pure arithmetic: the regions, no registers */

static int check(const char *what, int ok)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* 3.10: the gap's sectors (0x93000..0x97000) are not any object's st_sector, so a save there cannot be reached
 * through OBJ_*. These are st_save_to / st_current's discipline spelled out for an arbitrary base: erase, the payload
 * pages, then the commit record LAST, so a cut at any byte leaves one whole copy (the older one) loadable. The order is
 * what the gap test checks; whether the address is allowed is flash_map_test's. */
static void st_save_manual(uint32_t base, const void *src, uint32_t len)
{
    const uint8_t *s = src;
    st_hdr_t h;
    uint32_t off;
    st_erase(base);
    for (off = 0; off < len; off += 256u) {
        uint32_t n = len - off > 256u ? 256u : len - off;
        if (st_prog(base + ST_PAYLOAD_OFF + off, s + off, n))
            return;                                  /* power cut: no commit record, the other copy stands */
    }
    h.magic = ST_MAGIC; h.type = 0; h.slot = 0; h.seq = 1u;
    h.len = len; h.crc = st_crc32(s, len); h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    st_prog(base, &h, sizeof h);
}
static int st_load_manual(uint32_t base, void *dst, uint32_t max)
{
    st_hdr_t h;
    if (st_read(base, &h, sizeof h) || h.magic != ST_MAGIC || h.len > max ||
        h.hcrc != st_crc32(&h, sizeof h - 4u))
        return -1;
    if (st_read(base + ST_PAYLOAD_OFF, dst, h.len) || st_crc32(dst, h.len) != h.crc)
        return -1;
    return (int)h.len;
}

int main(void)
{
    char a[600], b[600], got[600];
    int bad = 0, n;
    memset(nor, 0xFF, sizeof nor);
    memset(a, 'A', sizeof a);
    memset(b, 'B', sizeof b);
    /* 3.10: projects are single-copy (no A/B redundancy); test basic save/load with settings (A/B) */
    bad += check("empty flash loads nothing", st_load(OBJ_SETTINGS, got, sizeof got) < 0);
    bad += check("save A", st_save(OBJ_SETTINGS, a, sizeof a) == 0);
    n = st_load(OBJ_SETTINGS, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save B", st_save(OBJ_SETTINGS, b, sizeof b) == 0);
    n = st_load(OBJ_SETTINGS, got, sizeof got);
    bad += check("load returns B (newer seq)", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 1;                                  /* payload page 1 written, the rest torn */
    st_save(OBJ_SETTINGS, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_SETTINGS, got, sizeof got);
    bad += check("torn save keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 3;                                  /* all payload pages, header torn */
    st_save(OBJ_SETTINGS, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_SETTINGS, got, sizeof got);
    bad += check("header not written keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    bad += check("save A again", st_save(OBJ_SETTINGS, a, sizeof a) == 0);
    n = st_load(OBJ_SETTINGS, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    nor[st_sector(OBJ_SETTINGS, 0) + ST_PAYLOAD_OFF + 10] ^= 0x01;   /* bit rot in one copy */
    nor[st_sector(OBJ_SETTINGS, 1) + ST_PAYLOAD_OFF + 10] ^= 0x01;
    n = st_load(OBJ_SETTINGS, got, sizeof got);
    bad += check("both copies corrupt -> nothing", n < 0);
    bad += check("other objects untouched", st_load(OBJ_AUTOSAVE, got, sizeof got) < 0);
    bad += check("project save/load (single-copy, two sectors)",
                 st_save(OBJ_PROJECT0, "hello", 5) == 0 && st_load(OBJ_PROJECT0, got, 5) == 5 && !memcmp(got, "hello", 5));
    bad += check("CRC-32 is zlib's (check value 0xCBF43926)", st_crc32("123456789", 9) == 0xCBF43926u);
    /* 3.10: single-copy project torn write loses data (the autosave is the recovery path) */
    {
        memset(nor, 0xFF, sizeof nor);
        st_save(OBJ_PROJECT0, a, sizeof a);
        fail_after = 1;
        st_save(OBJ_PROJECT0, b, sizeof b);
        fail_after = -1;
        bad += check("single-copy project: torn save corrupts the only copy",
                     st_load(OBJ_PROJECT0, got, sizeof got) < 0);
    }
    /* A/B fallback: test with autosave (has A/B redundancy) */
    memset(nor, 0xFF, sizeof nor);
    st_save(OBJ_AUTOSAVE, a, sizeof a);
    st_save(OBJ_AUTOSAVE, b, sizeof b);              /* B is newer, in the other copy */
    {
        st_hdr_t h;
        int cur = st_current(OBJ_AUTOSAVE, &h);
        nor[st_sector(OBJ_AUTOSAVE, (uint32_t)cur) + ST_PAYLOAD_OFF + 300] ^= 0x10;   /* rot in the newer copy */
    }
    n = st_load(OBJ_AUTOSAVE, got, sizeof got);
    bad += check("newer copy rotten -> the older one loads", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save over the rotten copy", st_save(OBJ_AUTOSAVE, b, sizeof b) == 0);
    n = st_load(OBJ_AUTOSAVE, got, sizeof got);
    bad += check("... loads the new data", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    nor[st_sector(OBJ_AUTOSAVE, 0) + 8] ^= 0x01;    /* both headers broken */
    nor[st_sector(OBJ_AUTOSAVE, 1) + 8] ^= 0x01;
    bad += check("both headers broken -> nothing", st_load(OBJ_AUTOSAVE, got, sizeof got) < 0);
    bad += check("data stays in the Felucca regions",
                 st_sector(OBJ_SETTINGS, 1) + 4096 <= 0xFF000 && st_sector(OBJ_PROJECT0 + 3, 1) + 4096 <= 0xE0000 &&
                     st_sector(OBJ_UPRESET0, 0) >= 0xDC000 && st_sector(OBJ_UPRESET0 + 1, 1) + 4096 <= 0xE0000);
    /* the user presets' FM6 patches (1.0.3) in the two sectors of the retired FM6 bank: 0x9F000 (after the projects)
     * and 0xFE000 (after the settings); the two objects share them, told apart by the commit record's type */
    bad += check("FM6 user preset patches (and the retired bank) in the sectors 0x9F000 / 0xFE000",
                 OBJ_FM6BANK == OBJ_UPFM6 - 1 && st_sector(OBJ_UPFM6, 0) == 0x9F000u &&
                     st_sector(OBJ_PROJECT0 + 3, 1) + 4096 == 0x9F000u && st_sector(OBJ_UPFM6, 1) == 0xFE000u &&
                     st_sector(OBJ_FM6BANK, 0) == 0x9F000u && st_sector(OBJ_FM6BANK, 1) == 0xFE000u &&
                     st_sector(OBJ_SETTINGS, 1) + 4096 == 0xFE000u);
    /* the autosave (3.10): A at 0xE5000..0xE6FFF (two sectors), B at 0x93000..0x94FFF in the gap; a save there
     * touches no other object (VBANK, samples, preset bank 5 at 0x95000..0x96FFF, store) */
    {
        uint32_t o, c, ok = st_sector(OBJ_AUTOSAVE, 0) == 0xE5000u &&
                               st_sector(OBJ_AUTOSAVE, 1) == 0x93000u && st_sectors(OBJ_AUTOSAVE) == 2u &&
                               st_pmax(OBJ_AUTOSAVE) >= 4096u;
        static uint8_t snap[0x100000];
        for (o = 0; o < OBJ_AUTOSAVE; o++)
            for (c = 0; c < 2u; c++) {
                uint32_t sec = st_sector(o, c), n = st_sectors(o);
                ok &= sec + n * 4096u <= 0xE5000u || sec >= 0xE7000u;
                ok &= sec + n * 4096u <= 0x93000u || sec >= 0x95000u;
            }
        memcpy(snap, nor, sizeof nor);
        ok &= st_save(OBJ_AUTOSAVE, a, sizeof a) == 0 && st_load(OBJ_AUTOSAVE, got, sizeof got) == (int)sizeof a &&
              !memcmp(got, a, sizeof a);
        for (o = 0; o < 0x100000u && ok; o++)
            if (!(o >= 0xE5000u && o < 0xE7000u) && !(o >= 0x93000u && o < 0x95000u))
                ok &= nor[o] == snap[o];
        bad += check("the autosave in A 0xE5000 and B 0x93000 (2 sectors each), nothing else touched", ok);
    }
    {
        static uint8_t bank[3472], back[3728], tab[3728];
        uint32_t i;
        memset(nor, 0xFF, sizeof nor);
        for (i = 0; i < sizeof bank; i++) bank[i] = (uint8_t)(i * 7u);
        for (i = 0; i < sizeof tab; i++) tab[i] = (uint8_t)(i * 3u);
        st_save(OBJ_FM6BANK, bank, sizeof bank);         /* A (seq 1) */
        bank[0] ^= 1;
        st_save(OBJ_FM6BANK, bank, sizeof bank);         /* B (seq 2): the newest */
        bad += check("a bank copy does not read as the patches object (its type)", st_load(OBJ_UPFM6, back, sizeof back) < 0);
        bad += check("st_save_to: the first patches write into A, the bank's older copy; the bank's B still loads",
                     st_save_to(OBJ_UPFM6, tab, sizeof tab, 0) == 0 &&
                     st_load(OBJ_UPFM6, back, sizeof back) == (int)sizeof tab && !memcmp(back, tab, sizeof tab) &&
                     st_load(OBJ_FM6BANK, back, sizeof back) == (int)sizeof bank && !memcmp(back, bank, sizeof bank));
        tab[0] ^= 1;
        bad += check("the next save goes to B (over the bank), A/B as any object",
                     st_save(OBJ_UPFM6, tab, sizeof tab) == 0 && st_load(OBJ_FM6BANK, back, sizeof back) < 0 &&
                     st_load(OBJ_UPFM6, back, sizeof back) == (int)sizeof tab && back[0] == tab[0]);
        {
            int erased = 1;
            for (i = 256u + sizeof tab; i < 4096u; i++) erased &= nor[0x9F000u + i] == 0xFFu && nor[0xFE000u + i] == 0xFFu;
            bad += check("FM6 patches: the sector tails stay erased", erased);
        }
    }
    {
        /* A/B torn-write recovery: test with autosave (has A/B redundancy; A is rewritten, B in the gap stays) */
        uint8_t copies[2 * ST_SECTOR];
        uint32_t off = st_sector(OBJ_AUTOSAVE, 0), cut;
        int ok = 1;
        memset(nor, 0xFF, sizeof nor);
        st_save(OBJ_AUTOSAVE, a, sizeof a);
        st_save(OBJ_AUTOSAVE, b, sizeof b);
        memcpy(copies, nor + off, sizeof copies);
        for (cut = 0; cut < sizeof a + sizeof(st_hdr_t); cut++) {
            memcpy(nor + off, copies, sizeof copies);
            fail_bytes = (int)cut;
            ok &= st_save(OBJ_AUTOSAVE, a, sizeof a) != 0;
            fail_bytes = -1;
            ok &= st_load(OBJ_AUTOSAVE, got, sizeof got) == (int)sizeof b && !memcmp(got, b, sizeof b);
        }
        bad += check("every payload/header byte cut keeps old data", ok);
        erase_error = 1;
        ok = st_save(OBJ_AUTOSAVE, a, sizeof a) != 0;
        erase_error = 0;
        bad += check("erase failure keeps old data", ok && st_load(OBJ_AUTOSAVE, got, sizeof got) == (int)sizeof b &&
                                                      !memcmp(got, b, sizeof b));
        write_protected = 1;
        ok = st_save(OBJ_AUTOSAVE, a, sizeof a) != 0;
        write_protected = 0;
        bad += check("write protection is a save error", ok && st_load(OBJ_AUTOSAVE, got, sizeof got) == (int)sizeof b &&
                                                            !memcmp(got, b, sizeof b));
    }
    {
        /* sequence wrap: test with autosave (A/B) */
        st_hdr_t h;
        uint32_t copy;
        memset(nor, 0xFF, sizeof nor);
        st_save(OBJ_AUTOSAVE, a, sizeof a);
        st_save(OBJ_AUTOSAVE, b, sizeof b);
        for (copy = 0; copy < 2u; copy++) {
            uint32_t off = st_sector(OBJ_AUTOSAVE, copy);
            memcpy(&h, nor + off, sizeof h);
            h.seq = 0xFFFFFFFEu + copy;
            h.hcrc = st_crc32(&h, sizeof h - 4u);
            memcpy(nor + off, &h, sizeof h);
        }
        bad += check("save sequence wraps", st_save(OBJ_AUTOSAVE, a, sizeof a) == 0);
        n = st_load(OBJ_AUTOSAVE, got, sizeof got);
        bad += check("wrapped sequence loads new data", n == (int)sizeof a && !memcmp(got, a, sizeof a));
        bad += check("save after sequence wrap", st_save(OBJ_AUTOSAVE, b, sizeof b) == 0 &&
                                                st_load(OBJ_AUTOSAVE, got, sizeof got) == (int)sizeof b &&
                                                !memcmp(got, b, sizeof b));
    }
    {
        uint32_t calls = io_calls;
        bad += check("invalid object load does no flash access", st_load(OBJ_COUNT, got, sizeof got) < 0 && io_calls == calls);
        bad += check("invalid object save does no flash access", st_save(OBJ_COUNT, a, sizeof a) < 0 && io_calls == calls);
        bad += check("wrapped object rejected", st_save(0xFFFFFFFFu, a, sizeof a) < 0 && io_calls == calls);
        memset(got, 'X', sizeof got);
        bad += check("small destination rejects whole object", st_load(OBJ_AUTOSAVE, got, sizeof got - 1u) < 0 && got[0] == 'X');
        bad += check("oversized save rejected", st_save(OBJ_AUTOSAVE, a, ST_PAYLOAD_MAX + 1u) < 0);
    }
    {
        /* single-copy project size constraint (two sectors minus header); use large buffer */
        static uint8_t big[4096], gbig[4096];
        uint32_t i;
        for (i = 0; i < sizeof big; i++) big[i] = (uint8_t)(i * 13u);
        memset(nor, 0xFF, sizeof nor);
        bad += check("single-copy project allows larger payload",
                     st_save(OBJ_PROJECT0, big, 3840u + 200u) == 0 &&
                     st_load(OBJ_PROJECT0, gbig, 4040u) == 4040 &&
                     !memcmp(big, gbig, 4040u));
    }
    {   /* 3.10: a save into the gap (0x93000..0x97000) is power-cut safe at every byte. The gap sectors are not any
         * object's st_sector, so this drives st_save_to's sequence by hand at a gap address; flash_map_test proves the
         * address is allowed.
         *
         * What it proves: a cut at any byte never damages the copy that is not being written, and the rewritten copy
         * is either intact or has no valid commit record (never mixed old and new data). A full-size payload ends at
         * the sector's last 256 B, where the SPL looks for an update record, so the sweep covers that tail too.
         *
         * What it does NOT prove: that the commit record is written last. The payload CRC alone already rejects a
         * half-written body, so a reversed order is caught by the same check. The order matters for a different reason
         * (a header that lands before its data would make a torn save look valid only if the CRC were also wrong), and
         * it is enforced in one place, st_save_to. */
        static const uint32_t GAP_A = 0x93000u, GAP_B = 0x94000u;
        /* gap sectors are A/B, one sector each; use single-sector payload max */
        static uint8_t big_a[ST_PAYLOAD_SECTOR], big_b[ST_PAYLOAD_SECTOR], copy[2 * ST_SECTOR], gb[ST_PAYLOAD_SECTOR];
        uint32_t cut, i;
        int ok = 1, tail_ok = 1;
        for (i = 0; i < ST_PAYLOAD_SECTOR; i++) {
            big_a[i] = (uint8_t)(i * 5u + 1u);
            big_b[i] = (uint8_t)(i * 3u + 7u);
        }
        memset(nor, 0xFF, sizeof nor);
        st_save_manual(GAP_A, big_a, ST_PAYLOAD_SECTOR);     /* A: the copy rewritten below */
        st_save_manual(GAP_B, big_b, ST_PAYLOAD_SECTOR);     /* B: never touched */
        memcpy(copy, nor + GAP_A, sizeof copy);
        for (cut = 0; cut < 2u * ST_SECTOR; cut++) {
            int b_ok, a_ok;
            memcpy(nor + GAP_A, copy, sizeof copy);
            fail_bytes = (int)cut;
            st_save_manual(GAP_A, big_a, ST_PAYLOAD_SECTOR); /* cut at byte `cut` of the rewrite */
            fail_bytes = -1;
            b_ok = st_load_manual(GAP_B, gb, sizeof gb) == (int)ST_PAYLOAD_SECTOR && !memcmp(gb, big_b, ST_PAYLOAD_SECTOR);
            a_ok = st_load_manual(GAP_A, gb, sizeof gb);     /* -1: no commit record yet, which is fine */
            ok &= b_ok && (a_ok < 0 || (a_ok == (int)ST_PAYLOAD_SECTOR && !memcmp(gb, big_a, ST_PAYLOAD_SECTOR)));
            /* cut counts payload bytes, so the record tail (flash base+0xF00..0xFFF) is payload 0xE00..0xEFF */
            if (cut >= ST_SECTOR - ST_PAYLOAD_OFF - 0x100u && cut < ST_SECTOR - ST_PAYLOAD_OFF)
                tail_ok &= b_ok && a_ok < 0;
        }
        bad += check("a gap save cut at every byte keeps the untouched copy whole", ok);
        bad += check("a cut in the SPL's record tail leaves no commit record behind", tail_ok);
    }
    {   /* ---- sector ownership: every object's A/B sector is one the map claims --------------------
         * AM-3: the invariant that matters. A save erases a whole sector, so an st_sector() that lands
         * outside a claimed region, or on the stock firmware's VM, destroys data that is not ours. This
         * enumerates every object at both copies rather than trusting the arithmetic in st_sector. */
        int land = 1, reserved = 1, gap_b = 1, gap_up = 1, vbank = 1, reclaim = 1;
        uint32_t obj, copy, s;
        for (obj = 0; obj < OBJ_COUNT; obj++)
            for (copy = 0; copy < 2u; copy++) {
                uint32_t sec = st_sector(obj, copy), nsec = st_sectors(obj);
                for (s = 0; s < nsec; s++) {
                    uint32_t off = sec + s * ST_SECTOR;
                    land &= FL_STORE_OK(off, 0x1000u);
                    /* stock VM / BTIF / USR outside the reclaim band F4000..F9FFF stay off-limits */
                    if (off >= 0xE8000u && off < 0xFC000u &&
                        !(off >= FL_RECLAIM_LO && off < FL_RECLAIM_HI))
                        reserved = 0;
                    if (off >= FL_VBANK_LO && off < FL_VBANK_HI)
                        vbank = 0;
                }
            }
        gap_b &= st_sector(OBJ_AUTOSAVE, 1) == 0x93000u && st_sectors(OBJ_AUTOSAVE) == 2u;
        gap_up &= st_sector(OBJ_UPRESET5, 0) == 0x95000u && st_sector(OBJ_UPRESET5, 1) == 0x96000u &&
                  st_sectors(OBJ_UPRESET5) == 1u;
        reclaim &= st_sector(OBJ_UPRESET2, 0) == 0xF5000u && st_sector(OBJ_UPRESET2, 1) == 0xF5000u &&
                   st_sector(OBJ_UPRESET3, 0) == 0xF6000u && st_sector(OBJ_UPRESET4, 0) == 0xF7000u &&
                   st_sectors(OBJ_UPRESET2) == 1u;
        bad += check("every object at both copies lands in a region the map claims", land);
        bad += check("no object lands on stock VM/BTIF/USR outside reclaim F4000..F9FFF", reserved);
        bad += check("autosave B claims the first two gap sectors 0x93000..0x94FFF", gap_b);
        bad += check("preset bank 5 claims the last two gap sectors 0x95000..0x96FFF", gap_up);
        bad += check("user banks 2..4 are single-copy at 0xF5000 / 0xF6000 / 0xF7000", reclaim);
        bad += check("no storage object claims the FM6 voice bank at 0xE7000", vbank);
        bad += check("factory bank is single-copy at 0xF4000",
                     st_sector(OBJ_FACBANK, 0) == 0xF4000u && st_sector(OBJ_FACBANK, 1) == 0xF4000u &&
                         st_sectors(OBJ_FACBANK) == 1u);
    }
    {   /* migration: a pre-3.10 one-sector B at 0xE6000 moves into the gap; A at 0xE5000 still loads */
        static uint8_t old_a[200], old_b[200], got[200];
        st_hdr_t h;
        uint32_t i;
        int ok;
        memset(nor, 0xFF, sizeof nor);
        for (i = 0; i < sizeof old_a; i++) {
            old_a[i] = (uint8_t)(i * 3u + 1u);
            old_b[i] = (uint8_t)(i * 5u + 7u);
        }
        /* hand-build the old one-sector pair at 0xE5000 / 0xE6000 (slot 0 / 1, seq 1 / 2) */
        {
            st_hdr_t ha, hb;
            memset(&ha, 0xFF, sizeof ha);
            ha.magic = ST_MAGIC; ha.type = OBJ_AUTOSAVE; ha.slot = 0; ha.seq = 1u; ha.len = sizeof old_a;
            memcpy(st_buf, old_a, sizeof old_a);
            ha.crc = st_crc32(st_buf, ha.len);
            ha.hcrc = st_crc32(&ha, sizeof ha - 4u);
            memcpy(nor + 0xE5000u, &ha, sizeof ha);
            memcpy(nor + 0xE5000u + ST_PAYLOAD_OFF, old_a, sizeof old_a);
            memset(&hb, 0xFF, sizeof hb);
            hb.magic = ST_MAGIC; hb.type = OBJ_AUTOSAVE; hb.slot = 1; hb.seq = 2u; hb.len = sizeof old_b;
            memcpy(st_buf, old_b, sizeof old_b);
            hb.crc = st_crc32(st_buf, hb.len);
            hb.hcrc = st_crc32(&hb, sizeof hb - 4u);
            memcpy(nor + 0xE6000u, &hb, sizeof hb);
            memcpy(nor + 0xE6000u + ST_PAYLOAD_OFF, old_b, sizeof old_b);
        }
        st_autosave_migrate();
        ok = st_current(OBJ_AUTOSAVE, &h) == 1 && h.seq == 2u &&
             st_load(OBJ_AUTOSAVE, got, sizeof got) == (int)sizeof old_b && !memcmp(got, old_b, sizeof old_b) &&
             st_sector(OBJ_AUTOSAVE, 1) == 0x93000u &&
             ((st_hdr_t *)(nor + 0xE6000u))->magic != ST_MAGIC;   /* orphan erased */
        st_autosave_migrate();                                   /* idempotent */
        ok &= st_load(OBJ_AUTOSAVE, got, sizeof got) == (int)sizeof old_b && !memcmp(got, old_b, sizeof old_b);
        bad += check("pre-3.10 autosave B at 0xE6000 migrates into the gap and the newer copy loads", ok);
    }
    {   /* power-cut while rewriting the 2-sector gap B: A (0xE5000) stays whole at every byte */
        static uint8_t big_a[4096], big_b[4096], got[4096], snap[2 * ST_SECTOR];
        uint32_t cut, i, off = st_sector(OBJ_AUTOSAVE, 1);
        int ok = 1;
        for (i = 0; i < sizeof big_a; i++) {
            big_a[i] = (uint8_t)(i * 7u + 1u);
            big_b[i] = (uint8_t)(i * 11u + 3u);
        }
        memset(nor, 0xFF, sizeof nor);
        st_save(OBJ_AUTOSAVE, big_a, sizeof big_a);              /* A */
        st_save(OBJ_AUTOSAVE, big_b, sizeof big_b);              /* B in the gap (current) */
        st_save(OBJ_AUTOSAVE, big_a, sizeof big_a);              /* A again so the next write is B */
        memcpy(snap, nor + off, sizeof snap);
        for (cut = 0; cut < sizeof big_b + sizeof(st_hdr_t); cut++) {
            memcpy(nor + off, snap, sizeof snap);
            fail_bytes = (int)cut;
            ok &= st_save(OBJ_AUTOSAVE, big_b, sizeof big_b) != 0;
            fail_bytes = -1;
            ok &= st_load(OBJ_AUTOSAVE, got, sizeof got) == (int)sizeof big_a && !memcmp(got, big_a, sizeof big_a);
        }
        bad += check("a 4096 B autosave cut while rewriting gap B keeps A whole", ok);
    }
    printf("%s\n", bad ? "STORAGE TEST FAILED" : "storage test passed");
    return bad != 0;
}