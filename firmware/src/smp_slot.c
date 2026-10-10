/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* User sample flash slots (editor SMP_* / backup). SAMPLE / GRAIN / SLICE engines are retired
 * (eng_retire.c); slots stay so old uploads and backups still round-trip. */
#include "../hal/fm1_xip.h"
#define SMP_USER_SLOTS 3
#define SMP_USER_BASE 0xA0000u
#define SMP_USER_SIZE 0x14000u
#define SMP_USER_DATA 512u
#define SMP_USER_MAGIC 0x504D5346u                  /* "FSMP" */
typedef struct {
    uint32_t off, n, ls, le;
    uint32_t rate;
    int16_t root16;
    int16_t pred;
    uint8_t idx, lo, hi, looped;
} smp_zone_t;
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t nz, rsv;
    char name[8];
    uint32_t data_len, crc, rsv2[2];
    smp_zone_t zone[16];
} smp_user_hdr_t;
static uint8_t usr_nz[SMP_USER_SLOTS];
#ifndef SMP_USER_XIP
#define SMP_USER_XIP(k) fm1_xip_ptr(SMP_USER_BASE + (k) * SMP_USER_SIZE)
#endif
static const uint8_t *smp_user_xip(uint32_t k) { return SMP_USER_XIP(k); }

/* validate slot k: usr_nz[k] = zone count when the header and zones look usable */
static void smp_user_scan(uint32_t k)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(k);
    uint32_t i, nz = 0;
    usr_nz[k] = 0;
    if (h->magic != SMP_USER_MAGIC || h->version != 1 || !h->nz || h->nz > 16u ||
        h->data_len > SMP_USER_SIZE - SMP_USER_DATA)
        return;
    for (nz = h->nz, i = 0; i < nz; i++) {
        const smp_zone_t *z = &h->zone[i];
        if (z->off > h->data_len || z->n > 2u * SMP_USER_SIZE || z->off + (z->n + 1u) / 2u > h->data_len ||
            z->idx > 88u || !z->rate || z->rate > 4u << 16 || z->ls > z->le || z->le >= z->n || z->lo > z->hi) {
            return;
        }
    }
    usr_nz[k] = (uint8_t)nz;
}
