/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Araminta: technical graphs for FX pages (after JIANT FM's ui_tech.c). Flat theme tokens only
 * (T_THEME / T_MID / T_DIM / T_RAISE / T_TEXT / T_ACCENT) — no thermal heat_col.
 *   DIST     transfer curve by TYPE + DRIVE; TONE as a low-pass response
 *   FILTER   frequency response by TYPE / CUT / RES
 *   CRUSH    bit staircase + hold steps; LPF corner
 *   PHASR    allpass notches sweeping with RATE / DEP / FBK
 *   FLANG    short comb modulating with RATE / DEP
 *   CHOR     three modulated delay taps (JIANT chorus graph; insert RATE / DEPTH)
 *   DLY      echo impulse train (MIX / FDBK / COLR); playhead while playing
 *   REVERB   impulse response by TYPE (ROOM / SPRING / HALL / SHIMR / CLOUD)
 *   MASTER   CLIP curve, PUNCH envelope, DUCK meter */
#define TK_X0 PANEL_X0
#define TK_X1 (PANEL_X0 + PANEL_W)
#define TK_Y0 8
#define TK_Y1 112

static int32_t tk_sin(uint32_t ang) { return sine_i(ang << 16); }   /* Q15; ang = 16-bit turn */

/* scope peak 0..256, slewed (once a frame) */
static int32_t tk_energy(void)
{
    static int32_t e;
    static uint32_t fr = 0xFFFFFFFFu;
    uint32_t w = scope_w, i;
    int32_t pk = 0, v;
    if (fr == ui.frame)
        return e;
    fr = ui.frame;
    for (i = 1; i <= 256u; i += 2u) {
        v = scope_buf[(w - i) & (SCOPE_N - 1u)];
        if (v < 0) v = -v;
        if (v > pk) pk = v;
    }
    v = pk * 256 / 12000;
    if (v > 256) v = 256;
    e += v > e ? (v - e) * 3 / 4 : (v - e) / 6;
    return e;
}

static int32_t tk(uint32_t c)
{
    int16_t *vp;
    const param_desc_t *d = page_desc(cur_page(), c, &vp);
    int32_t r = d && d->label && d->label[0] != '-' ? RATIO(d, enum_rank(d, *vp)) : -1;
    return r < 0 ? 500 : r;
}

static void tk_axes(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    cv_rect(x0, y1, x1 - x0, 1, T_RAISE);
    cv_rect(x0, y0, 1, y1 - y0, T_RAISE);
    cv_rect(x1 - 4, y0, 5, 1, T_RAISE);
    cv_rect(x1, y0, 1, 5, T_RAISE);
}

static void tk_dot(int32_t x, int32_t y) { cv_rrect(x - 3, y - 3, 7, 7, 3, T_TEXT, T_SURF); }

/* DIST: in -> out matching fx.c ins_shape / ins_run_dist gain */
static int32_t tk_dist_y(uint32_t mode, int32_t x, int32_t g)
{
    int32_t v = ((x >> 3) * g) >> 9;
    return ins_shape(mode, v, 0) * 32767 / 26000;
}

static void tech_dist(const track_t *t)
{
    int32_t d = t->p[P_DIST], g = 4096 + d * d * 3, i, py = 0, x0 = 22, y0 = TK_Y0, s = 100, cx = x0 + s / 2;
    int32_t cy = y0 + s / 2, en = tk_energy(), tone = t->p[P_ED_FX];
    uint32_t mode = (uint32_t)clamp(t->p[P_IDMODE], 0, IDM_N - 1);
    tk_axes(x0, y0, x0 + s, y0 + s);
    cv_rect(x0, cy, s, 1, T_LINE);
    cv_rect(cx, y0, 1, s, T_LINE);
    for (i = 0; i <= s; i += 4)
        cv_rect(x0 + i, y0 + s - i, 1, 1, T_DIM);
    for (i = 0; i <= s; i++) {
        int32_t x = (i - s / 2) * 131072 / (s / 2), y = cy - tk_dist_y(mode, x, d ? g : 4096) * (s / 2) / 32767;
        y = clamp(y, y0, y0 + s);
        if (i)
            cv_line_t(x0 + i - 1, py, x0 + i, y, T_THEME, 2);
        py = y;
    }
    if (en > 2) {
        int32_t xi = s / 2 + en * (s / 2) / 256;
        tk_dot(x0 + xi, clamp(cy - tk_dist_y(mode, en * 512, d ? g : 4096) * (s / 2) / 32767, y0, y0 + s));
    }
    {   /* TONE: low-pass corner from drive-linked cutoff + P_ED_FX (fx.c ins_run_dist) */
        int32_t k = clamp(32000 - d * 95 + tone * 300, 2500, 32767), fx = 140, fw = 82;
        int32_t fc = fx + 8 + (k - 2500) * (fw - 34) / 30267, f;
        tk_axes(fx, y0 + 30, fx + fw, y0 + s);
        py = y0 + 34;
        for (f = 0; f <= fw; f++) {
            int32_t r = fx + f <= fc ? 0 : (fx + f - fc) * (fx + f - fc) / 6, y = clamp(y0 + 34 + r, y0 + 34, y0 + s);
            if (f)
                cv_line_t(fx + f - 1, py, fx + f, y, T_MID, 2);
            py = y;
        }
    }
}

/* FILTER insert: TYPE response across the panel, CUT under the peak, RES the Q */
static void tech_filter(const track_t *t)
{
    uint32_t ft = (uint32_t)clamp(t->p[P_IFTYPE], 0, IFT_N - 1);
    int32_t cut = t->p[P_IFCUT], res = t->p[P_IFRES], x0 = TK_X0 + 6, w = PANEL_W - 12, base = TK_Y1 - 4, top = TK_Y0 + 4;
    int32_t cx = x0 + 8 + cut * (w - 16) / 127, i, py = 0, q = 8 + res * 40 / 127;
    tk_axes(x0, top, x0 + w, base);
    cv_rect(cx, top, 1, base - top, T_DIM);
    for (i = 0; i <= w; i++) {
        int32_t x = x0 + i, df = i - (cx - x0), y, mag;
        if (ft == IFT_COMB) {
            int32_t tooth = (df * 8 / (8 + cut / 16)) % 28;
            tooth = tooth < 0 ? -tooth : tooth;
            mag = 900 - tooth * tooth * 2 + res * 2;
        } else if (ft == IFT_LP) {
            mag = df < 0 ? 1000 : 1000 - df * df / (6 + (127 - res) / 4);
        } else if (ft == IFT_HP) {
            mag = df > 0 ? 1000 : 1000 - df * df / (6 + (127 - res) / 4);
        } else {                                        /* BP */
            mag = 1000 - df * df / (2 + q / 4);
        }
        mag = clamp(mag, 0, 1100);
        y = base - mag * (base - top - 4) / 1100;
        if (i)
            cv_line_t(x - 1, py, x, y, T_THEME, 2);
        py = y;
    }
}

/* CRUSH: staircase transfer from BITS; hold marks from RATE; LPF corner */
static void tech_crush(const track_t *t)
{
    uint32_t sh = ins_bits_sh(t->p[P_ICBITS]), steps = sh > 14u ? 2u : 1u << (15u - sh);
    int32_t i, py = 0, x0 = 22, y0 = TK_Y0, s = 100, cy = y0 + s / 2, hold = 1 + (t->p[P_ICRATE] >> 3);
    int32_t lpf = t->p[P_ICLPF];
    tk_axes(x0, y0, x0 + s, y0 + s);
    cv_rect(x0, cy, s, 1, T_LINE);
    for (i = 0; i <= s; i++) {
        int32_t x = (i - s / 2) * 32767 / (s / 2), q, y;
        if (steps < 2u) q = x;
        else {
            int32_t cell = 65536 / (int32_t)steps;
            q = (x / cell) * cell;
        }
        y = clamp(cy - q * (s / 2) / 32767, y0, y0 + s);
        if (i)
            cv_line_t(x0 + i - 1, py, x0 + i, y, T_THEME, 2);
        py = y;
    }
    for (i = 0; i < 8; i++) {                          /* hold: sample period as ticks */
        int32_t x = 140 + i * (hold + 2);
        if (x > TK_X1 - 8) break;
        cv_rect(x, y0 + 20, 2, 20 + (i & 1) * 10, i < 4 ? T_THEME : T_MID);
    }
    {   /* LPF corner */
        int32_t fx = 140, fw = 82, fc = fx + 8 + lpf * (fw - 20) / 127, f;
        tk_axes(fx, y0 + 50, fx + fw, y0 + s);
        py = y0 + 54;
        for (f = 0; f <= fw; f++) {
            int32_t r = fx + f <= fc ? 0 : (fx + f - fc) * (fx + f - fc) / 8, y = clamp(y0 + 54 + r, y0 + 54, y0 + s);
            if (f)
                cv_line_t(fx + f - 1, py, fx + f, y, T_MID, 2);
            py = y;
        }
    }
}

/* PHASR: notches across the band; DEPTH / FBK deepen them; RATE sweeps while sound is up */
static void tech_phasr(const track_t *t)
{
    int32_t dep = t->p[P_IPDEP], fbk = t->p[P_IPFBK], rate = t->p[P_IPRATE];
    int32_t x0 = TK_X0 + 6, w = PANEL_W - 12, base = TK_Y1 - 4, top = TK_Y0 + 8, i, py = 0;
    int32_t live = tk_energy() > 2 && !(ui_prefs & PREF_ANIM_OFF);
    uint32_t ph = live ? ui.frame * (uint32_t)(20 + rate) : (uint32_t)rate * 40u;
    tk_axes(x0, top, x0 + w, base);
    for (i = 0; i <= w; i++) {
        int32_t n0 = (tk_sin((uint32_t)(i * 900 + (int32_t)ph)) * dep / 127) >> 15;
        int32_t n1 = (tk_sin((uint32_t)(i * 1800 + (int32_t)ph + 12000)) * dep / 127) >> 15;
        int32_t n2 = (tk_sin((uint32_t)(i * 2700 + (int32_t)ph + 24000)) * fbk / 127) >> 15;
        int32_t mag = 900 + n0 * 8 + n1 * 5 + n2 * 4, y = base - clamp(mag, 80, 1100) * (base - top) / 1100;
        if (i)
            cv_line_t(x0 + i - 1, py, x0 + i, y, T_THEME, 2);
        py = y;
    }
}

/* FLANG: short comb teeth modulating */
static void tech_flang(const track_t *t)
{
    int32_t dep = t->p[P_ILDEP], rate = t->p[P_ILRATE];
    int32_t x0 = TK_X0 + 6, w = PANEL_W - 12, mid = (TK_Y0 + TK_Y1) / 2, i, py = mid;
    int32_t live = tk_energy() > 2 && !(ui_prefs & PREF_ANIM_OFF);
    uint32_t ph = live ? ui.frame * (uint32_t)(30 + rate) * 6u : 0u;
    cv_rect(x0, mid, w, 1, T_RAISE);
    for (i = 0; i <= w; i++) {
        int32_t y = mid - ((tk_sin((uint32_t)(i * (80 + rate / 3)) + ph) * dep / 127 * 28) >> 15);
        int32_t tooth = (i * 3 + (int32_t)(ph >> 10)) % 18;
        if (tooth > 9) tooth = 18 - tooth;
        y -= tooth * dep / 200;
        y = clamp(y, TK_Y0, TK_Y1);
        if (i)
            cv_line_t(x0 + i - 1, py, x0 + i, y, T_THEME, 2);
        py = y;
    }
}

/* CHOR (insert): JIANT chorus — three taps a third apart */
static void tech_chor(const track_t *t)
{
    int32_t rate = tk(0), depth = tk(1), x0 = TK_X0 + 6, w = PANEL_W - 12, mid = (TK_Y0 + TK_Y1) / 2, k, i;
    int32_t spread = 64;                                /* insert has no STER; mono fan as JIANT default */
    int32_t live = tk_energy() > 2 && !(ui_prefs & PREF_ANIM_OFF);
    uint32_t ph = live ? ui.frame * (uint32_t)(40 + rate) * 8u : 0u;
    (void)t;
    cv_rect(x0, mid, w, 1, T_RAISE);
    for (k = 0; k < 3; k++) {
        int32_t py = mid, off = (k - 1) * spread * 9 / 64;
        for (i = 0; i <= w; i++) {
            int32_t y = mid - off - ((tk_sin((uint32_t)(i * (60 + rate / 4)) + ph + (uint32_t)(k * 21845 * spread / 64)) *
                                      depth / 1000 * 30) >> 15) * (spread + 64) / 128;
            y = clamp(y, TK_Y0, TK_Y1);
            if (i)
                cv_line_t(x0 + i - 1, py, x0 + i, y, k ? T_MID : T_THEME, k ? 1 : 2);
            py = y;
        }
    }
}

/* DLY: echoes as an impulse train */
static void tech_dly(void)
{
    int32_t time = (int32_t)delay_samples(), mix = song.g[G_DMIX], fb = song.g[G_DFDBK], amp = mix * 1000 / 127, n;
    int32_t x0 = TK_X0 + 6, w = PANEL_W - 12, base = TK_Y1 - 4, h = 88, tone = song.g[G_DCOLOR] - 64;
    if (amp < 40)                                       /* MIX low: still show from the selected track's send */
        amp = TSEL->p[P_DLY] * 1000 / 127;
    tk_axes(x0, TK_Y0, x0 + w, base);
    cv_rect(x0, base - h, 3, h, T_TEXT);
    for (n = 1; n <= 6 && amp > 4; n++) {
        int32_t x = x0 + n * w / 7, bh = amp * h / 1000;
        uint16_t col = tone * n > 40 ? T_THEME : tone * n < -40 ? T_DIM : T_MID;
        cv_rect(x - 2, base - bh, 4, bh, col);
        amp = amp * (fb > 100 ? 100 : fb) / 100;
        if (fb > 100) amp = 1000;
    }
    if (song.playing && time > 0) {
        uint32_t span = (uint32_t)time * 7u / ((uint32_t)FS / 1000u);
        int32_t x;
        if (!span) span = 1;
        x = x0 + (int32_t)((fm1_ms % span) * (uint32_t)w / span);
        cv_rect(x, TK_Y0, 1, base - TK_Y0, T_ACCENT);
    }
}

/* REVERB: IR by the page's TYPE (SEND or CHAIN via page_desc) */
static void tech_rev(void)
{
    int16_t *vp;
    int32_t ty, size, damp;
    int32_t x0 = TK_X0 + 6, w = PANEL_W - 12, base = TK_Y1 - 4, h = 92, len, i;
    page_desc(cur_page(), 0, &vp); ty = vp ? *vp : song.g[G_RTYPE];
    page_desc(cur_page(), 1, &vp); size = vp ? *vp : song.g[G_RSIZE];
    page_desc(cur_page(), 2, &vp); damp = vp ? *vp : song.g[G_RDAMP];
    ty = (uint32_t)ty <= 4u ? ty : 0;
    len = 40 + size * (w - 50) / 127;
    tk_axes(x0, TK_Y0, x0 + w, base);
    if (ty == 4) {                                      /* CLOUD: drifting grain swarm */
        int32_t cy = (TK_Y0 + base) / 2, nd = 24 + size / 3;
        int32_t spread = 40 + size * (w - 40) / 127, live = tk_energy() > 2 && !(ui_prefs & PREF_ANIM_OFF);
        uint32_t fr = live ? ui.frame : 0u;
        for (i = 0; i < nd; i++) {
            uint32_t hsh = (uint32_t)(i + 1) * 2654435761u, sp = 1u + (hsh >> 28);
            int32_t dx = (int32_t)(((hsh >> 8) + fr * sp) % (uint32_t)(spread > 1 ? spread : 1));
            int32_t gx = x0 + w - 6 - dx, wob = (tk_sin((hsh >> 6) + fr * 900u) * 4) >> 15;
            int32_t gy = cy + (int32_t)((hsh >> 4) % 13u) - 6 + wob - damp / 8;
            int32_t fade = spread ? 1000 - dx * 1000 / spread : 1000;
            uint16_t c = fade > 600 ? T_THEME : fade > 250 ? T_MID : T_DIM;
            if (gy > TK_Y0 + 1 && gy < base - 2)
                cv_rect(gx, gy, 2, 2, c);
        }
        if (size >= 127)
            cv_rect(x0 + 4, TK_Y0 + 4, w - 8, 1, T_ACCENT);
        return;
    }
    cv_rect(x0, base - h, 3, h, T_TEXT);
    for (i = 0; i < len && x0 + 6 + i < x0 + w; i += 2) {
        int32_t env = 1000 - i * 1000 / len, r = (int32_t)(((uint32_t)(i + 7) * 2654435761u) >> 22) % 1000;
        int32_t bh = env * (400 + r * 6 / 10) / 1000 * h / 1000;
        uint16_t c = env > 700 ? T_THEME : env > 350 ? T_MID : T_DIM;
        if (ty == 1)                                    /* SPRING: chirp */
            bh = bh * (600 + (tk_sin((uint32_t)(i * i * 9)) >> 6) * 400 / 512) / 1000;
        if (ty == 2)                                    /* HALL: denser (shorter gaps look fuller) */
            bh = bh * (850 + (r & 150)) / 1000;
        bh = bh - i * damp / (len * 4 + 1);
        if (bh > 0)
            cv_rect(x0 + 6 + i, base - bh, 1, bh, c);
        if (ty == 3 && i % 6 == 0 && bh > 4) {          /* SHIMR: octave sparks over the tail */
            int32_t sy = base - bh - 6 - (i * 7 % 23);
            if (sy > TK_Y0 && sy < base - 2)
                cv_rect(x0 + 6 + i, sy, 2, 2, T_ACCENT);
        }
    }
}

/* MASTER (GLO): CLIP curve, PUNCH shape, DUCK meter */
static void tech_master(void)
{
    int32_t clip = song.g[G_CLIP], g = 1024 + clip * 31, i, py = 0, x0 = 22, y0 = TK_Y0, s = 100, cy = y0 + s / 2;
    int32_t en = tk_energy(), punch = song.g[G_PUNCH], duck = song.g[G_DUCK];
    tk_axes(x0, y0, x0 + s, y0 + s);
    cv_rect(x0, cy, s, 1, T_LINE);
    for (i = 0; i <= s; i++) {
        int32_t x = (i - s / 2) * 32767 / (s / 2), y = cy - softclip(x * g >> 10) * (s / 2) / 32767;
        if (i)
            cv_line_t(x0 + i - 1, py, x0 + i, y, T_THEME, 2);
        py = y;
    }
    if (en > 2)
        tk_dot(x0 + s / 2 + en * (s / 2) / 256, cy - softclip(en * 128 * g >> 10) * (s / 2) / 32767);
    {
        int32_t px0 = 136, pw = 60, b = y0 + s, py2 = b - 45;
        tk_axes(px0, y0 + 20, px0 + pw, b);
        for (i = 0; i <= pw; i++) {
            int32_t e = 45 * (pw - i) / pw, y = b - (e * punch_gain((uint32_t)i * 255u / (uint32_t)pw, punch) >> 15);
            if (i)
                cv_line_t(px0 + i - 1, py2, px0 + i, y, T_THEME, 2);
            py2 = y;
        }
    }
    {
        int32_t mx = 210, mh = s, gh = duck_g1 * mh / 32767;
        cv_rect(mx, y0, 10, mh, T_RAISE);
        cv_rect(mx, y0 + mh - gh, 10, gh, duck ? T_THEME : T_DIM);
    }
}

/* 1 if this page drew its technical graph */
static int graph_tech(const track_t *t, const page_t *pg)
{
    if (pg->fam == FAM_GLO && pg->id[0] == G_CLIP) {
        tech_master();
        return 1;
    }
    if (pg->fam != FAM_FX)
        return 0;
    if (str_eq(pg->title, "DIST")) { tech_dist(t); return 1; }
    if (str_eq(pg->title, "FILTER")) { tech_filter(t); return 1; }
    if (str_eq(pg->title, "CRUSH")) { tech_crush(t); return 1; }
    if (str_eq(pg->title, "PHASR")) { tech_phasr(t); return 1; }
    if (str_eq(pg->title, "FLANG")) { tech_flang(t); return 1; }
    if (str_eq(pg->title, "CHOR")) { tech_chor(t); return 1; }
    if (str_eq(pg->title, "DLY")) { tech_dly(); return 1; }
    if (str_eq(pg->title, "REVERB")) { tech_rev(); return 1; }
    return 0;
}
