/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM1_INLINE: register helpers that must inline into their caller, so the
 * SFR access sequence (and its timing, in ISRs and IRQ-off sections) is the
 * one written at the call site; -Os would otherwise out-line a helper with
 * several callers. RAM-resident code (.ram_text) may only use these, never
 * a called helper (build.py checks .ram_text for calls). */
#pragma once

#define FM1_INLINE static inline __attribute__((always_inline))

/* FM1_FAR(fn): fn, called through a pointer the compiler cannot fold back into a direct call. XIP (0x02xxxxxx) and
 * RAM code (0x01C0xxxx) are beyond each other's 23-bit call range: every call between them goes this way
 * (fm1_flash.h FL_FAR, the flash driver in .ram_text; core.h AUDIO_FAR, the render's code in .hot_text) */
static inline void *fm1_far(void *p) { void *volatile q = p; return q; }
#define FM1_FAR(fn) ((__typeof__(&fn))fm1_far((void *)&fn))
