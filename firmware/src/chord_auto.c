/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Adapted for Araminta from Melodee f4fdbc7. */
/* Western AUTO harmony. Included by chord.c after chord_kit. All searches have
 * fixed bounds; no allocation, look-ahead, floating point or random choices.
 * chord_make/UI previews are read-only. Only chord_build commits history, and
 * the existing key/MIDI ownership freezes its result through release/strum.
 *
 * Araminta adaptation: the history is packed to 20 bytes per track (14 for the
 * held chord, 6 for the last two unharmonized onsets) so the pool gate stays
 * green. Melodee keeps four onsets; two still cover a 2-beat window and the
 * pool has only 84 bytes of headroom. TRN is not part of the scale tonic here:
 * DIA3/DIA7 add it to the played note, and AUTO follows them. */
typedef struct {
    uint8_t note[CHORD_MAX];
    uint16_t at;                       /* clock, in 256-sample units */
    uint16_t mask, melody;
    uint8_t root, n;
    uint8_t cfg[2];                    /* scale | tonic << 4; quant | voicing << 3 */
} chord_auto_t;
typedef struct {                       /* the last two unharmonized onsets of a track */
    uint8_t notes;                     /* pitch class per nibble */
    uint8_t next_n;                    /* next index in bit 0, count in bits 1..2 */
    uint16_t at[2];                    /* clock, in 256-sample units */
} chord_auto_hist_t;                   /* 6 bytes: both bytes first, then the aligned times */
/* Runtime-only history belongs in pool SRAM; the small ordinary RAM bank is
 * reserved for the interrupt/transport state that already nearly fills it. */
static chord_auto_t chord_auto[NTRK] __attribute__((section(".pool")));
static chord_auto_hist_t chord_auto_hist[NTRK] __attribute__((section(".pool")));
static uint32_t chord_auto_clock;
static uint32_t chord_auto_enabled;
static uint32_t chord_auto_tick_s(uint32_t h) { return h >> 8; }
static uint32_t chord_auto_age(uint16_t at) { return (uint16_t)(chord_auto_tick_s(chord_auto_clock) - at); }
static int32_t chord_auto_abs(int32_t n) { return n < 0 ? -n : n; }

typedef struct { uint8_t n, pitch[4], cost; const char *name; } chord_auto_shape_t;
static const chord_auto_shape_t CH_AUTO_SHAPE[] = {
    {3, {0,4,7}, 0, ""}, {3, {0,3,7}, 0, "m"},
    {3, {0,3,6}, 6, "dim"}, {3, {0,4,8}, 6, "aug"},
    {3, {0,5,7}, 12, "sus4"}, {3, {0,2,7}, 12, "sus2"},
    {4, {0,4,7,10}, 16, "7"}, {4, {0,4,7,11}, 16, "maj7"},
    {4, {0,3,7,10}, 16, "m7"}, {4, {0,3,6,10}, 16, "m7b5"},
    {4, {0,3,6,9}, 16, "dim7"}, {4, {0,3,7,11}, 18, "mM7"},
    {4, {0,4,8,10}, 20, "+7"}, {4, {0,4,8,11}, 20, "+M7"},
    {4, {0,4,7,9}, 18, "6"}, {4, {0,3,7,9}, 18, "m6"},
    {4, {0,5,7,10}, 24, "7sus4"}, {4, {0,2,7,10}, 24, "7sus2"},
};

static uint32_t chord_auto_bits(uint32_t bits)
{
    uint32_t n = 0;
    while (bits) { bits &= bits - 1u; n++; }
    return n;
}
static uint32_t chord_auto_rotate(uint32_t bits, int32_t semitones)
{
    bits &= 0xFFFu;
    uint32_t n = (uint32_t)((semitones % 12 + 12) % 12);
    return ((bits << n) | (bits >> (12u - n))) & 0xFFFu;
}
static uint32_t chord_auto_scale(const chord_auto_t *h) { return h->cfg[0] & 15u; }
static uint32_t chord_auto_tonic(const chord_auto_t *h) { return h->cfg[0] >> 4; }
static uint32_t chord_auto_quant(const chord_auto_t *h) { return h->cfg[1] & 7u; }
static uint32_t chord_auto_voic(const chord_auto_t *h) { return (h->cfg[1] >> 3) & 7u; }
static void chord_auto_config_set(chord_auto_t *h, const track_t *t)
{
    h->cfg[0] = (uint8_t)((t->p[P_SCALE] & 15) | ((t->p[P_ROOT] & 15) << 4));
    h->cfg[1] = (uint8_t)((t->p[P_QUANT] & 7) | ((t->p[P_VOIC] & 7) << 3));
}
static int chord_auto_config(const track_t *t, const chord_auto_t *h)
{
    return h->n && trk_vmode(t) == V_POLY && chord_auto_scale(h) == (uint32_t)t->p[P_SCALE] &&
        chord_auto_tonic(h) == (uint32_t)t->p[P_ROOT] && chord_auto_quant(h) == (uint32_t)t->p[P_QUANT] &&
        chord_auto_voic(h) == (uint32_t)t->p[P_VOIC];
}
static uint32_t chord_auto_window(void)
{
    uint32_t w = (2u * beat_samples()) >> 8;
    return w ? w : 1u;
}
static int chord_auto_same(const track_t *t, const chord_auto_t *h)
{
    return chord_auto_config(t, h) && chord_auto_age(h->at) < chord_auto_window();
}
static void chord_auto_forget(uint32_t k)
{
    memset(&chord_auto[k], 0, sizeof chord_auto[k]);
    memset(&chord_auto_hist[k], 0, sizeof chord_auto_hist[k]);
}
static void chord_auto_tick(uint32_t samples)
{
    chord_auto_clock += samples;
    uint32_t enabled = 0, previous = chord_auto_enabled;
    for (uint32_t k = 0; k < NTRK; k++)
        if (trk[k].p[P_CHRD] == CH_AUTO) enabled |= 1u << k;
    chord_auto_enabled = enabled;
    if (!enabled) {
        if (previous) { memset(chord_auto, 0, sizeof chord_auto); memset(chord_auto_hist, 0, sizeof chord_auto_hist); }
        return;
    }
    for (uint32_t k = 0; k < NTRK; k++) {
        /* A long held chord is still the current harmony. The phrase gap begins
         * at release, not at its original press; slow songs keep their context. */
        if (chord_auto_config(&trk[k], &chord_auto[k]) && trk[k].nheld)
            chord_auto[k].at = (uint16_t)chord_auto_tick_s(chord_auto_clock);
        if (trk[k].p[P_CHRD] != CH_AUTO || (chord_auto[k].n && !chord_auto_same(&trk[k], &chord_auto[k])))
            chord_auto[k].n = 0;
        /* Eager expiry also prevents timestamp wrap reviving ancient notes. */
        if (trk[k].p[P_CHRD] != CH_OFF)
            chord_auto_hist[k].next_n = 0;
        else if (chord_auto_hist[k].next_n >> 1) {
            uint32_t newest = (chord_auto_hist[k].next_n + 1u) & 1u;
            if (chord_auto_age(chord_auto_hist[k].at[newest]) >= (8u * FS) >> 8)
                chord_auto_hist[k].next_n = 0;
        }
    }
}
static void chord_auto_observe(const track_t *t, uint32_t note)
{
    if (!chord_auto_enabled || t->p[P_CHRD] != CH_OFF || chord_kit(t) || note > 127u) return;
    uint32_t k = trk_index(t), n = chord_auto_hist[k].next_n >> 1, at = chord_auto_hist[k].next_n & 1u;
    if (!n) at = 0;
    chord_auto_hist[k].notes = (uint8_t)((chord_auto_hist[k].notes & (0xFu << (4u * (1u - at)))) |
                                        ((note % 12u) << (4u * at)));
    chord_auto_hist[k].at[at] = (uint16_t)chord_auto_tick_s(chord_auto_clock);
    chord_auto_hist[k].next_n = (uint8_t)((at ^ 1u) | ((n < 2u ? n + 1u : n) << 1));
}
static uint32_t chord_auto_melody(const track_t *t, uint32_t allowed)
{
    uint32_t pcs = 0;
    for (uint32_t k = 0; k < NTRK; k++) {
        const track_t *other = &trk[k];
        if (other == t || !trk_heard(k) || other->p[P_CHRD] != CH_OFF || chord_kit(other)) continue;
        uint32_t n = chord_auto_hist[k].next_n >> 1;
        for (uint32_t j = 0; j < n; j++)
            if (chord_auto_age(chord_auto_hist[k].at[j]) < chord_auto_window())
                pcs |= 1u << ((chord_auto_hist[k].notes >> (4u * j)) & 15u);
        for (uint32_t j = 0; j < other->nheld; j++)
            pcs |= 1u << (other->held[j] % 12u);
        for (uint32_t j = 0; j < other->seq_n; j++) pcs |= 1u << (other->seq_notes[j] % 12u);
    }
    return pcs & allowed;
}

/* Return a complete voicing even at MIDI endpoints. AUTO uses nearby inversions
 * with CLOSE; explicit OPEN/INV/+OCT remain deliberate player overrides. */
static void chord_auto_voice(const track_t *t, int32_t root, const uint8_t *pitch, uint32_t n,
                             const chord_auto_t *h, uint8_t *out)
{
    int32_t best = 0x7FFFFFFF;
    uint32_t inversions = t->p[P_VOIC] == VC_CLOSE ? n : 1u;
    for (uint32_t inv = 0; inv < inversions; inv++) {
        int32_t notes[4];
        for (uint32_t i = 0; i < n; i++) {
            uint32_t at = (i + inv) % n;
            notes[i] = root + pitch[at] + (at < inv ? 12 : 0);
        }
        if (t->p[P_VOIC] == VC_OPEN && n >= 3u) {
            int32_t x = notes[1];
            for (uint32_t i = 1; i + 1u < n; i++) notes[i] = notes[i + 1u];
            notes[n - 1u] = x + 12;
        } else if (t->p[P_VOIC] == VC_INV1 || t->p[P_VOIC] == VC_INV2) {
            uint32_t count = t->p[P_VOIC] == VC_INV2 ? 2u : 1u;
            for (uint32_t j = 0; j < count; j++) {
                int32_t x = notes[0] + 12;
                for (uint32_t i = 0; i + 1u < n; i++) notes[i] = notes[i + 1u];
                notes[n - 1u] = x;
            }
        } else if (t->p[P_VOIC] == VC_BASS) {
            /* Keep the selected third/seventh when the four-voice limit drops a fifth. */
            if (n == 4u) { notes[2] = notes[3]; }
            for (uint32_t i = n - 1u; i; i--) notes[i] = notes[i - 1u];
            notes[0] = root - 12;
        }
        for (int32_t oct = -3; oct <= 1; oct++) {
            int32_t low = notes[0] + 12 * oct, high = notes[n - 1u] + 12 * oct;
            if (low < 0 || high > 127) continue;
            int32_t score = chord_auto_abs(oct) * 2 + (int32_t)inv;
            if (h && t->p[P_VOIC] == VC_CLOSE) {
                for (uint32_t i = 0; i < n; i++) {
                    int32_t distance = 128;
                    for (uint32_t j = 0; j < h->n; j++) {
                        int32_t d = chord_auto_abs(notes[i] + 12 * oct - h->note[j]);
                        if (d < distance) distance = d;
                    }
                    score += 4 * distance;
                }
                score += chord_auto_abs(low - h->note[0]);
                /* Follow intentional register changes rather than drifting indefinitely. */
                score += chord_auto_abs(notes[n / 2u] + 12 * oct - root - 4);
            } else score += 12 * chord_auto_abs(oct) + 12 * (int32_t)inv;
            if (score >= best) continue;
            best = score;
            for (uint32_t i = 0; i < n; i++) out[i] = (uint8_t)(notes[i] + 12 * oct);
        }
    }
}

static uint32_t chord_auto_make(const track_t *t, uint32_t input, uint8_t *out, int32_t *rp, uint16_t *maskp)
{
    int32_t tonic = t->p[P_ROOT], root = (int32_t)input;
    uint32_t allowed = chord_auto_rotate(scale_mask(t), tonic);
    /* Use the same downward policy as DIA/SNAP, except the lower endpoint must
     * take the next available note instead of clamping to an out-of-scale C. */
    while (root >= 0 && !(allowed & (1u << (uint32_t)(root % 12)))) root--;
    if (root < 0) { root = 0; while (!(allowed & (1u << (uint32_t)(root % 12)))) root++; }
    *rp = root;
    if (trk_vmode(t) != V_POLY) { out[0] = (uint8_t)root; *maskp = 1; return 1; }
    uint32_t relative = chord_auto_rotate(allowed, -root), melody = chord_auto_melody(t, allowed);
    const chord_auto_t *h = &chord_auto[trk_index(t)];
    if (!chord_auto_same(t, h)) h = 0;
    if (h && h->root == root && h->melody == melody) {
        for (uint32_t i = 0; i < h->n; i++) out[i] = h->note[i];
        *maskp = h->mask;
        return h->n;
    }
    uint8_t degree[12], pitch[4];
    uint32_t count = 0, natural = 0, chosen = 0xFFFFu, n = 3;
    for (uint32_t i = 0; i < 12u; i++) if (relative & (1u << i)) degree[count++] = (uint8_t)i;
    if (count == 7u) natural = 1u | (1u << degree[2]) | (1u << degree[4]);
    int32_t best = 0x7FFFFFFF;
    for (uint32_t i = 0; i < NELEM(CH_AUTO_SHAPE); i++) {
        const chord_auto_shape_t *s = &CH_AUTO_SHAPE[i];
        uint32_t mask = 0;
        for (uint32_t j = 0; j < s->n; j++) mask |= 1u << s->pitch[j];
        if (mask & ~relative) continue;
        uint32_t pcs = chord_auto_rotate(mask, root);
        int32_t score = s->cost + 32 * (int32_t)chord_auto_bits(melody & ~pcs);
        if (natural && (mask & natural) == natural) score -= 12;
        if (h) {
            uint32_t previous = chord_auto_rotate(h->mask, h->root);
            score -= 2 * (int32_t)chord_auto_bits(previous & pcs);
            uint32_t prev = (uint32_t)((h->root - tonic + 120) % 12);
            uint32_t now = (uint32_t)((root - tonic + 120) % 12);
            /* ii/IV -> V invites a dominant seventh when the scale permits it;
             * V -> I prefers a settled triad. No forbidden tones are introduced. */
            if (now == 7u && (prev == 2u || prev == 5u) && mask == 0x491u) score -= 28;
            if (!now && prev == 7u && s->n == 3u) score -= 8;
        }
        if (score < best) { best = score; chosen = i; }
    }
    uint32_t mask = 0;
    if (chosen < NELEM(CH_AUTO_SHAPE)) {
        n = CH_AUTO_SHAPE[chosen].n;
        for (uint32_t i = 0; i < n; i++) pitch[i] = CH_AUTO_SHAPE[chosen].pitch[i];
    } else {
        /* Some blues-scale roots admit no named triad. Keep an honest in-scale
         * set instead of secretly adding a note or claiming a conventional name. */
        for (uint32_t i = 0; i < n; i++) pitch[i] = degree[2u * i % count];
    }
    if (n == 3u && t->p[P_VOIC] == VC_BASS) { pitch[3] = pitch[2]; n = 4; }
    for (uint32_t i = 0; i < n; i++) mask |= 1u << pitch[i];
    chord_auto_voice(t, root, pitch, n, h, out);
    *maskp = (uint16_t)mask;
    return n;
}

static void chord_auto_remember(const track_t *t, int32_t root, uint16_t mask, const uint8_t *notes, uint32_t n)
{
    chord_auto_t *h = &chord_auto[trk_index(t)];
    h->root = (uint8_t)root; h->mask = mask; h->n = (uint8_t)n;
    for (uint32_t i = 0; i < n; i++) h->note[i] = notes[i];
    chord_auto_config_set(h, t);
    h->melody = (uint16_t)chord_auto_melody(t, chord_auto_rotate(scale_mask(t), t->p[P_ROOT]));
    h->at = (uint16_t)chord_auto_tick_s(chord_auto_clock);
}

static void chord_auto_name(char *b, uint32_t root, uint32_t mask)
{
    const char *name = mask == 1u ? "" : " set";
    for (uint32_t i = 0; i < NELEM(CH_AUTO_SHAPE); i++) {
        uint32_t shape = 0;
        for (uint32_t j = 0; j < CH_AUTO_SHAPE[i].n; j++) shape |= 1u << CH_AUTO_SHAPE[i].pitch[j];
        if (shape == mask) { name = CH_AUTO_SHAPE[i].name; break; }
    }
    str_cpy(b, N_NOTE[root % 12u], 12);
    str_cpy(b + str_len(b), name, 12 - str_len(b));
}
