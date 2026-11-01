// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// Project import conversion, client side. Firmware 3.6 reads FUN7..FUN19 only (the FUN1..FUN6 readers
// are gone) and no longer converts DIGITAL tracks (engine 1) to FM6. This module turns any stored project
// (FUN1..FUN19) into the current FUN19 wire format, applying the same field mapping the old C readers did
// (project.c at 037b07b) and converting DIGITAL tracks with the editor's own FM4 converter.
//
// convertProject(bytes, digital) -> Uint8Array(3840). digital is the editor's converter:
//   (p, pe0) -> { p, fm6 (128 packed bytes), preset }
// It is called for each DIGITAL track after its values are clamped to DIGITAL's ranges, as proj_fm4 did.
// Without it a project holding a DIGITAL track is refused (explicitly: no silent stub).

const NTRK = 4;
const NSTEP = 64;
const P_COUNT = 127;
const P_E0 = 119;
const PROJ_STORE_SIZE = 3840;
const PROJ_NAME_LEN = 12;
const PROJ_NAME_OFF = PROJ_STORE_SIZE - 4 - PROJ_NAME_LEN;      // 3824
const PROJ_FM6_OFF = PROJ_NAME_OFF - NTRK * 128;                 // 3312
const PROJ_DX_OFF = PROJ_FM6_OFF - 96;                           // 3216
const PROJ_CRV_OFF = 3214;
const PROJ_SCL_OFF = 3196;
const PROJ_SCL_SIZE = 16;
const PROJ_SL_SEND_OFF = PROJ_DX_OFF + 96 - 1;                  // 3311
const PROJ_DX_MOT_OFF = PROJ_DX_OFF + 88 + 2 + 1;               // 3307
const PROJ_SLPOS_OFF = PROJ_SCL_OFF + PROJ_SCL_SIZE;            // 3212
const PROJ_SCL_MARK_OFF = PROJ_SLPOS_OFF + 1;                    // 3213
const PROJ_FXBUS_SCL_N = 15;
const FX_BUS_N = 19;
const MOTION_FILE_MAX = 88;
const MOTION_LOCK = 0x80;
const MFX_TIME = 49;                                            // P_M1SRC
const MFX_DAMP = 53;
const MFX_TSPEED = 41;                                          // P_TAPEMIX
const QN_ALL = 1;
const QN_CURR = 4;
const PROJ_DEF_SOUND = 0xff;
const PROJ_DEF_KEEP = 0xfe;
const NPART = 4;
const PROJ_PHYS = 2;

const MAGIC = {
  1: 0x46554e31, 2: 0x46554e32, 3: 0x46554e33, 4: 0x46554e34,
  5: 0x46554e35, 6: 0x46554e36, 7: 0x46554e37, 8: 0x46554e38, 9: 0x46554e39,
  10: 0x46554e41, 11: 0x46554e42, 12: 0x46554e43, 13: 0x46554e44,
  14: 0x46554e45, 15: 0x46554e46, 16: 0x474e5546, 17: 0x484e5546, 18: 0x494e5546, 19: 0x4a4e5546,
};
const SIZE = { 1: 688, 2: 2552, 3: 2584, 4: 2680, 5: 3352, 6: 3388, 7: 3388, 8: 3584, 9: 3648,
  10: 3840, 11: 3840, 12: 3840, 13: 3840, 14: 3840, 15: 3840, 16: 3840, 17: 3840, 18: 3840, 19: 3840 };
const PROJ_STORE_V7 = 3388, PROJ_STORE_V8 = 3584, PROJ_STORE_V9 = 3648, PROJ_MOTION_V9 = 260;
const MOTION_FILE_SIZE_128 = 388, MOTION_FILE_SIZE_V10 = 334, MOTION_FILE_SIZE_V11 = 238,
  MOTION_FILE_SIZE_V15 = 220, MOTION_FILE_SIZE = 268;
const CHAIN_WIRE15_SIZE = 84, CHAIN_WIRE16_SIZE = 36, CHAIN_ROWS = 16;
const PROJ_GENERATION_MARK = 0x47;
const PROJ_MASTER_ORDER_OFF = 48 + 10 * 4;                       // 88
const P_LEVEL = 0, P_SCALE = 26, P_QUANT = 27, P_DIST = 33, P_CHOR = 34, P_REV = 36,
  P_TAPEMIX = 41, P_SLCR = 45, P_IORD = 96, P_FTYPE = 117, P_E7 = 126;
const P_FM1_ATK = 61, P_FM4_LEVEL = 80, P_MORPH = 71, P_DRIFT = 73, P_WAVE2 = 66;
const G_TSPEED = 7, G_RSIZE = 8, G_RDAMP = 9, G_CRATE = 10, G_CDEPTH = 11, G_TWOW = 12,
  G_TDROP = 15, G_TMIX = 22, G_TAPEMODE = 23, G_RTYPE = 24, G_PUNCH = 25, G_DUCK = 26,
  G_CRTYPE = 27, G_CRSIZE = 28, G_CRDAMP = 29, G_SLOWN = 30, G_TTEMPO = 31;
const ENGI_ANALOG = 0, ENGI_DIGITAL = 1, ENGI_SAMPLE = 4, ENGI_PHYS = 9, ENGI_DRUM = 10,
  ENGI_FM6 = 12, ENGI_SLICE = 13;
const DRUM_KIT_E = [64, 64, 70, 64, 64, 0, 0, 0];
const DRUM_LANE_NOTE = [36, 38, 39, 42, 46, 45, 37, 56];
const GENERATION_DEFAULT = [50, 50, 50, 128, 128, 0];

// Parameter defaults (params.c TP), the DIGITAL clamp ranges, global defaults (GP), the engine E defaults
// (engines.c), FM6_INIT (felucca_fm6.h) and the factory DRUM-X kit (drumx_voice.c).
const TP_DEF = [104,10,70,90,60,0,0,0,0,60,0,0,0,0,0,0,0,0,2,1,64,0,127,0,0,0,0,0,0,16,2,0,64,64,126,0,0,0,0,0,0,0,0,0,40,0,1,1,127,0,0,0,0,0,0,0,0,0,0,0,0,0,0,127,0,127,0,0,127,0,127,0,0,127,0,127,0,0,127,0,127,0,0,127,127,127,127,127,127,127,127,0,0,0,1,0,24,0,0,0,96,40,0,64,96,96,0,64,96,96,0,64,96,0,64,96,0,0,0];
const TP_MIN = [0,0,0,0,0,-64,-64,-64,-64,0,0,0,0,-64,-64,-64,0,0,0,1,1,0,0,0,0,0,0,0,-24,1,0,0,1,0,0,0,0,0,0,-64,0,0,0,0,0,0,1,0,0,0,0,-64,0,0,-64,0,0,-64,0,0,-64,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0];
const TP_MAX = [127,127,127,127,127,63,63,63,63,127,4,127,127,63,63,63,127,15,9,4,127,100,127,1,1,11,14,4,24,64,9,100,127,127,127,127,127,3,127,63,2,127,127,22,127,2,16,5,127,10,51,63,10,51,63,10,51,63,10,51,63,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,11,4,127,127,127,127,127,127,127,127,10,1,1,1,127,719,3,127,3,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,3,1];
const DIG_MIN = [0, 0, 0, 0, 0, 0, 0, 0];
const DIG_MAX = [7, 14, 14, 14, 127, 127, 127, 0];
const GP_DEF = [120,0,0,0,1,60,70,100,90,60,0,0,0,30,0,0,1,0,0,0,0,0,0,0,2,0,0,2,90,60,0,1];
const ENG_E_DEF = [
  [0,10,64,0,90,30,0,0], [0,1,1,1,60,60,0,0], [0,0,60,64,0,0,0,0], [0,0,64,0,0,0,0,127],
  [0,0,0,1,127,0,0,0], [0,64,0,0,64,10,64,0], [0,0,-12,6,0,80,40,64], [4,0,0,0,0,40,0,1],
  [0,32,80,80,0,30,10,127], [0,64,80,80,20,90,0,0], [64,64,64,64,64,0,0,0], [0,64,80,30,64,0,0,0],
  [0,0,0,0,0,0,0,0], [0,0,0,0,0,0,0,0], [0,0,0,0,0,0,0,3], [0,64,64,64,64,100,0,0],
];
const FM6_INIT = [99,99,99,99,99,99,99,0,39,0,0,0,56,0,0,2,0,99,99,99,99,99,99,99,0,39,0,0,0,56,0,0,2,0,99,99,99,99,99,99,99,0,39,0,0,0,56,0,0,2,0,99,99,99,99,99,99,99,0,39,0,0,0,56,0,0,2,0,99,99,99,99,99,99,99,0,39,0,0,0,56,0,0,2,0,99,99,99,99,99,99,99,0,39,0,0,0,56,0,99,2,0,99,99,99,99,50,50,50,50,0,8,35,0,0,0,49,24,73,78,73,84,32,86,79,73,67,69];
const DX_KIT = [16,9,70,72,4,20,14,100,88,10,60,5,40,30,52,80,90,46,18,64,104,70,4,60,0,46,127,84,54,0,58,127,76,10,100,0,22,96,116,108,0,14,112,122,10,100,0,62,96,112,108,0,74,112,118,16,30,40,66,10,30,36,60,76,20,50,27,63,10,14,16,46,70,24,20,30,60,6,69,0,72,0,40,74,0,84,12,56];
const FX_BUS_DEF = [];
for (const id of [97, 33, 8, 99, 100, 101, 103, 104, 105, 107, 108, 109, 111, 112, 114, 115, 45, 46, 47])
  FX_BUS_DEF.push(TP_DEF[id] < 0 ? 0 : TP_DEF[id] > 127 ? 127 : TP_DEF[id]);

const u16 = (b, o) => b[o] | b[o + 1] << 8;
const s16 = (b, o) => (u16(b, o) << 16) >> 16;
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
const magicOf = (b) => u32(b, 0);
const formatOf = (bytes) => {
  const m = magicOf(bytes);
  for (const [f, v] of Object.entries(MAGIC)) if (v === m && SIZE[f] === bytes.length) return Number(f);
  return 0;
};

/* FNV-1a, as project.c proj_hash */
export function projectHash(bytes, n = bytes.length) {
  let s = 0x811c9dc5;
  for (let i = 0; i < n; i++) s = Math.imul(s ^ bytes[i], 16777619) >>> 0;
  return s;
}

function emptyTrack(engine = 0) {
  return {
    p: TP_DEF.slice(), engine, preset: 0, steps: Array.from({ length: NSTEP }, () => ({ note: [0, 0, 0, 0], n: 0, time: 0, flags: 0, vel: 0, hit: 0, acc: 0, prob: 0, ratchet: 1, nudge: 0 })),
    fx_sl_pos: 8, fx_order: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
    generation: GENERATION_DEFAULT.slice(),
  };
}
function emptyProject() {
  return {
    g: GP_DEF.slice(), sel: 0, parts: 0, phys: 0, fm6_vib: 0,
    t: Array.from({ length: NTRK }, () => emptyTrack()),
    chain: { count: 0, row: Array.from({ length: CHAIN_ROWS }, () => ({ slot: [0, 0, 0, 0], repeat: 1 })) },
    motion: { count: 0, on: 0, event: [] },
    fm6: Array.from({ length: NTRK }, () => FM6_INIT.slice()),
    dx: DX_KIT.slice(), dx_mute: 0, dx_mot: [0, 0, 0, 0],
    fx_bus: FX_BUS_DEF.slice(), fx_master: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0], fx_master_order: 0,
    name: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  };
}

function tapeDefaults(g) {
  g[G_TSPEED] = GP_DEF[G_TSPEED]; g[G_TWOW] = GP_DEF[G_TWOW]; g[G_TDROP] = GP_DEF[G_TDROP];
  g[G_TMIX] = GP_DEF[G_TMIX]; g[G_TAPEMODE] = GP_DEF[G_TAPEMODE];
}
function globalsFromWire(w, count = 27) {
  const g = GP_DEF.slice();
  for (let i = 0; i < count; i++) g[i] = w[i];
  tapeDefaults(g);
  g[G_CRTYPE] = g[G_RTYPE]; g[G_CRSIZE] = g[G_RSIZE]; g[G_CRDAMP] = g[G_RDAMP];
  return g;
}
function rtypeRoom(g) { g[G_RTYPE] = 0; }

function paramsByCount(out, inp, np, def) {
  const nc = np - 8;
  for (let i = 0; i < P_E0; i++) out[i] = i < nc ? inp[i] : def[i];
  for (let i = 0; i < 8; i++) out[P_E0 + i] = inp[nc + i];
}
function insMigrate(p, np) {
  if (np > 111) return;
  let ty = 0, a = 64, b = 96, c = 96, mix = 127, ft = 0, es = 0;
  const dist = p[P_DIST];
  if (np === 111) {
    ty = p[96]; a = p[97]; b = p[98]; c = p[99]; mix = p[100]; ft = p[101]; es = p[102];
  }
  for (let i = P_IORD; i < P_FTYPE; i++) p[i] = 0;
  for (let i = P_IORD; i <= 118; i++) p[i] = TP_DEF[i];
  p[P_CHOR] = TP_DEF[P_CHOR];
  p[P_FTYPE] = ft; p[P_FTYPE + 1] = es;
  if (dist) p[98] = 127;
  if (np < 111 || ty <= 0) return;
  if (ty >= 1 && ty <= 4) {
    p[97] = ty - 1; p[P_DIST] = a; p[98] = mix;
    const base = 96 - (clamp(a, 0, 127) >> 2);
    p[8] = clamp(b - base, -64, 63);
  } else if (ty === 5) {
    p[103] = a; p[104] = b; p[105] = c; p[106] = mix;
  } else if (ty === 6) {
    p[107] = a; p[108] = b; p[109] = c; p[110] = mix;
  } else if (ty === 7) {
    p[111] = a; p[112] = b; p[113] = mix;
  } else if (ty === 8) {
    p[114] = a; p[115] = b; p[116] = mix;
  }
}
const sclFromV12 = (sc) => sc <= 0 || sc > 15 ? 0 : sc - 1;

function readStep8(b, o) {
  const note = [0, 1, 2, 3].map((j) => b[o + j] > 127 ? 127 : b[o + j]);
  return { note, n: b[o + 4], time: b[o + 5], flags: b[o + 6] & 3, vel: b[o + 7], hit: 0, acc: 0, prob: 0, ratchet: 1, nudge: 0 };
}
function trackFromOld(p, np, engine, preset, steps, analogLegacy = true, def = TP_DEF) {
  const d = emptyTrack(engine);
  paramsByCount(d.p, p, np, def);
  insMigrate(d.p, np);
  d.preset = preset;
  if (analogLegacy && engine === ENGI_ANALOG) { d.p[P_E7] = 0; d.p[P_MORPH] = 0; d.p[P_DRIFT] = 0; }
  d.steps = steps;
  return d;
}
function defaultTrack(i) {
  const engine = [ENGI_ANALOG, ENGI_FM6, 3, ENGI_DRUM][i];
  const d = emptyTrack(engine);
  for (let k = 0; k < 8; k++) d.p[P_E0 + k] = ENG_E_DEF[engine][k];
  d.preset = PROJ_DEF_SOUND;
  for (const s of d.steps) { s.time = 2; s.n = 0; }
  return d;
}
function drumLane(note) {
  const ix = (note >= 35 && note <= 81 ? note : 36 + (note + 120 - 36) % 12) - 35;
  const GM = [0,0,6,1,2,1,5,3,5,3,5,4,5,5,7,5,7,7,7,3,7,7,7,6,7,5,5,5,5,5,5,5,7,7,3,3,6,6,3,3,6,6,6,5,5,7,7];
  return GM[ix];
}
function stepToGrid(s) {
  if (s.time !== 0) return;
  const notes = [], hit = s.hit;
  let h = hit;
  for (let i = 0; i < s.n && i < 4; i++) {
    const n = s.note[i], l = drumLane(n);
    if (n === DRUM_LANE_NOTE[l]) h |= 1 << l;
    else notes.push(n);
  }
  for (let i = 0; i < 4; i++) s.note[i] = notes[i] ?? 0;
  s.n = notes.length; s.hit = h;
  if (!notes.length && (s.flags & 1)) { s.acc |= s.hit; s.flags &= ~1; }
}
function drumsToPart(q) {
  if (q.parts === NPART) return;
  const d = q.t[NTRK - 1];
  d.engine = ENGI_DRUM; d.preset = PROJ_DEF_KEEP;
  d.p[P_LEVEL] = clamp(q.g[G_PUNCH], 0, 127);
  d.p[P_REV] = clamp(q.g[G_DUCK], 0, 127);
  q.g[G_PUNCH] = q.g[G_DUCK] = 0;
  q.parts = NPART;
}
function physConvert(q) {
  if (q.phys >= PROJ_PHYS) return;
  for (const d of q.t) {
    if (d.engine !== ENGI_PHYS) continue;
    const e = d.p.slice(P_E0);
    if (!q.phys && e[0] === 2) { e[0] = 0; e[6] = 64; }
    if (e[0] === 4) {
      e[0] = clamp(e[7], 0, 127) >> 5;
      e[6] = e[6] >= 64 ? 1 : 0; e[7] = 0;
      d.engine = ENGI_DRUM; d.preset = 0;
    }
    for (let i = 0; i < 8; i++) d.p[P_E0 + i] = e[i];
  }
  q.phys = PROJ_PHYS;
}
function dropMotionOn(q, tracks, test) {
  q.motion.event = q.motion.event.filter((e) => !(tracks.has(e.place >> 6) && test(e.param & 127)));
  q.motion.count = q.motion.event.length;
}
function percConvert(q) {
  const hit = new Set();
  for (let k = 0; k < NTRK; k++) {
    const d = q.t[k];
    if (d.engine !== ENGI_SAMPLE || d.p[P_E0] !== 4) continue;
    for (let i = 0; i < 8; i++) d.p[P_E0 + i] = DRUM_KIT_E[i];
    d.engine = ENGI_DRUM;
    if (d.preset < PROJ_DEF_KEEP) d.preset = 0;
    hit.add(k);
  }
  if (hit.size) dropMotionOn(q, hit, (id) => id >= P_E0);
}
function retireConvert(q) {
  const hit = new Set();
  for (let k = 0; k < NTRK; k++) {
    const d = q.t[k];
    if (d.engine !== ENGI_SLICE) continue;
    for (let i = 0; i < 8; i++) d.p[P_E0 + i] = DRUM_KIT_E[i];
    d.engine = ENGI_DRUM;
    if (d.preset < PROJ_DEF_KEEP) d.preset = 0;
    hit.add(k);
  }
  if (hit.size) dropMotionOn(q, hit, (id) => id >= P_E0);
}
function fm4Convert(q, digital) {
  const hit = new Set();
  for (let k = 0; k < NTRK; k++) {
    const d = q.t[k];
    if (d.engine !== ENGI_DIGITAL) continue;
    if (!digital) throw new Error("This project holds a DIGITAL track: the editor's FM4 converter is needed");
    for (let i = 0; i < P_COUNT; i++) {
      const lo = i < P_E0 ? TP_MIN[i] : DIG_MIN[i - P_E0];
      const hi = i < P_E0 ? TP_MAX[i] : DIG_MAX[i - P_E0];
      d.p[i] = clamp(d.p[i], lo, hi);
    }
    const r = digital(d.p.slice(), P_E0);
    if (!r || !r.p || !r.fm6) throw new Error("DIGITAL conversion failed");
    for (let i = 0; i < P_COUNT; i++) d.p[i] = r.p[i];
    q.fm6[k] = Array.from(r.fm6);
    d.engine = ENGI_FM6;
    if (d.preset < PROJ_DEF_KEEP) d.preset = r.preset & 0xff;
    hit.add(k);
  }
  if (hit.size) dropMotionOn(q, hit, (id) => id >= P_E0 || (id >= P_FM1_ATK && id <= P_FM4_LEVEL));
}

/* FUN1..FUN6 images, as project.c's old readers saw them */
function decodeOld(b, f) {
  const q = emptyProject();
  let i;
  if (f === 1) {
    q.g = globalsFromWire(Array.from({ length: 27 }, (_, k) => s16(b, 8 + 2 * k)));
    rtypeRoom(q.g); q.g[G_CRTYPE] = q.g[G_RTYPE]; q.g[G_CRSIZE] = q.g[G_RSIZE]; q.g[G_CRDAMP] = q.g[G_RDAMP];
    q.parts = NPART;
    q.t[0] = trackFromOld(readP(b, 62, 53), 53, b[168], b[169], readSteps8(b, 170));
    for (i = 1; i < NTRK; i++) q.t[i] = defaultTrack(i);
  } else if (f === 2) {
    q.g = globalsFromWire(Array.from({ length: 27 }, (_, k) => s16(b, 8 + 2 * k)));
    rtypeRoom(q.g); q.g[G_CRTYPE] = q.g[G_RTYPE]; q.g[G_CRSIZE] = q.g[G_RSIZE]; q.g[G_CRDAMP] = q.g[G_RDAMP];
    q.sel = b[62];
    for (i = 0; i < NTRK; i++) {
      const o = 66 + i * 620;
      q.t[i] = trackFromOld(readP(b, o, 53), 53, b[o + 106], b[o + 107], readSteps8(b, o + 108));
    }
    drumsToPart(q);
  } else if (f === 3) {
    q.g = globalsFromWire(Array.from({ length: 27 }, (_, k) => s16(b, 8 + 2 * k)));
    rtypeRoom(q.g); q.g[G_CRTYPE] = q.g[G_RTYPE]; q.g[G_CRSIZE] = q.g[G_RSIZE]; q.g[G_CRDAMP] = q.g[G_RDAMP];
    q.sel = b[62]; q.parts = b[63];
    for (i = 0; i < NTRK; i++) {
      const o = 66 + i * 628;
      q.t[i] = trackFromOld(readP(b, o, 57), 57, b[o + 114], b[o + 115], readSteps8(b, o + 116));
    }
    drumsToPart(q);
  } else if (f === 4) {
    q.g = globalsFromWire(Array.from({ length: 27 }, (_, k) => s16(b, 8 + 2 * k)));
    rtypeRoom(q.g); q.g[G_CRTYPE] = q.g[G_RTYPE]; q.g[G_CRSIZE] = q.g[G_RSIZE]; q.g[G_CRDAMP] = q.g[G_RDAMP];
    q.sel = b[62]; q.parts = b[63]; q.phys = b[64];
    for (i = 0; i < NTRK; i++) {
      const o = 66 + i * 652;
      q.t[i] = trackFromOld(readP(b, o, 69), 69, b[o + 138], b[o + 139], readSteps8(b, o + 140));
    }
    drumsToPart(q);
  } else {
    const v6 = f === 6;
    q.g = globalsFromWire(Array.from({ length: 27 }, (_, k) => s16(b, 8 + 2 * k)));
    rtypeRoom(q.g); q.g[G_CRTYPE] = q.g[G_RTYPE]; q.g[G_CRSIZE] = q.g[G_RSIZE]; q.g[G_CRDAMP] = q.g[G_RDAMP];
    q.sel = b[62]; q.parts = b[63]; q.phys = b[64];
    for (i = 0; i < NTRK; i++) {
      const o = 66 + i * 820;
      const steps = [];
      for (let k = 0; k < NSTEP; k++) {
        const so = o + 140 + k * 10;
        steps.push({
          note: [b[so] & 127, b[so + 1] & 127, b[so + 2] & 127, b[so + 3] & 127],
          n: Math.min(b[so + 4], 4), time: Math.min(b[so + 5], 2), flags: b[so + 6] & 3,
          vel: Math.min(b[so + 7], 127), hit: b[so + 8], acc: b[so + 9], prob: 0, ratchet: 1, nudge: 0,
        });
      }
      q.t[i] = trackFromOld(readP(b, o, 69), 69, b[o + 138], b[o + 139], steps, false, engineDefs(b[o + 138]));
    }
    if (v6) {
      const o = 3346;
      q.chain.count = b[o];
      for (let r = 0; r < CHAIN_ROWS; r++) {
        const slot = b[o + 4 + r * 2], rep = b[o + 5 + r * 2];
        if (r < q.chain.count) q.chain.row[r] = { slot: [slot, slot, slot, slot], repeat: rep };
      }
    }
    drumsToPart(q);
    physConvert(q);
    // The v5/v6 path returns before proj_grid (only formats 1..4 get it).
    return finishOld(q);
  }
  physConvert(q);
  for (let k = 0; k < NTRK; k++) if (q.t[k].engine === ENGI_DRUM) q.t[k].steps.forEach(stepToGrid);
  return finishOld(q);
}
function readP(b, o, n) { return Array.from({ length: n }, (_, i) => s16(b, o + 2 * i)); }
function engineDefs(engine) {
  const d = TP_DEF.slice();
  if (engine === ENGI_ANALOG) { d[P_WAVE2] = 0; d[P_MORPH] = 0; d[P_DRIFT] = 0; }
  return d;
}
function readSteps8(b, o) {
  return Array.from({ length: NSTEP }, (_, k) => readStep8(b, o + k * 8));
}
function finishOld(q) {
  for (const d of q.t) {
    d.generation = GENERATION_DEFAULT.slice();
    d.p[P_TAPEMIX] = 0;
    d.fx_sl_pos = 8;
    d.fx_order = fxOrderDefault(d.p[P_IORD], d.p[P_CHOR], 8);
    d.p[P_SCALE] = sclFromV12(d.p[P_SCALE]);
  }
  q.fm6 = Array.from({ length: NTRK }, () => FM6_INIT.slice());
  q.motion = { count: 0, on: 0, event: [] };
  q.dx = new Array(88).fill(0);
  q.dx_mute = 0; q.dx_mot = [0, 0, 0, 0];
  q.fx_bus = new Array(FX_BUS_N).fill(0);
  q.fx_master = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
  q.fx_master_order = 0;
  return q;
}

function wireValGet(b, o) {
  if (b[o] > 191) throw new Error("Invalid project parameter");
  return b[o] - 64;
}
function fxOrderDefault(iord, fchor, slpos) {
  let n = iord & 1023; if (n > 719) n = 0;
  const ins = [], av = [0, 1, 2, 3, 4, 5];
  for (let i = 0; i < 6; i++) {
    const f = [120, 24, 6, 2, 1, 1][i], idx = Math.floor(n / f);
    let k = Math.min(idx, 5 - i);
    n %= f;
    ins[i] = av[k]; av.splice(k, 1);
  }
  let sp = slpos < 9 ? slpos : 8, d8, r8;
  if (fchor < 64) { d8 = 6; r8 = 7; sp = 8; }
  else { const v = fchor - 64; d8 = v & 7; r8 = (v >> 3) & 7; if (r8 === d8) r8 = d8 + 1 < 8 ? d8 + 1 : 0; }
  const dp = d8 < sp ? d8 : d8 + 1, rp = r8 < sp ? r8 : r8 + 1;
  const used = new Array(10).fill(false), ord = new Array(10).fill(0);
  ord[dp] = 6; used[dp] = true;      // INS_DLY
  ord[rp] = 7; used[rp] = true;      // INS_REV
  ord[sp] = 8; used[sp] = true;      // INS_SLCR
  ord[9] = 9; used[9] = true;        // INS_TAPE
  let j = 0;
  for (let i = 0; i < 10; i++) if (!used[i]) ord[i] = ins[j++];
  return ord;
}
function orderOk(ord) {
  let seen = 0;
  for (const v of ord) { if (v >= 10 || (seen & (1 << v))) return false; seen |= 1 << v; }
  return true;
}
function tspeedGet(raw) {
  raw &= 127;
  return raw <= 64 ? 25 + Math.floor((raw * 75 + 32) / 64) : 100 + Math.floor(((raw - 64) * 300 + 31) / 63);
}
function tspeedPut(v) {
  v = clamp(v, 25, 400);
  return v <= 100 ? Math.floor(((v - 25) * 64 + 37) / 75) : 64 + Math.floor(((v - 100) * 63 + 150) / 300);
}
function motionIds(m, np) {
  for (const e of m.event) {
    const id = e.param & 127;
    if (id === 127) continue;
    if (id >= np) throw new Error("Invalid project motion record");
    if (np < P_COUNT && id >= np - 8) e.param = (e.param & MOTION_LOCK) | (id + P_COUNT - np);
  }
}
function ktrMotion(m, t) {
  m.event = m.event.filter((e) => {
    const tr = e.place >> 6, id = e.param & 127;
    return !(tr < NTRK && t[tr].engine === ENGI_ANALOG && (id === P_E7 || id === P_MORPH || id === P_DRIFT || id === P_WAVE2));
  });
  m.count = m.event.length;
}

/* FUN7..FUN19 wire images -> model (project.c proj_unpack at 037b07b) */
function decodeModern(b, f) {
  const st = b.length, v7 = f === 7, v10 = st === PROJ_STORE_SIZE;
  const q = emptyProject();
  const nameOff = st - 4 - PROJ_NAME_LEN;
  const end = v7 ? nameOff : nameOff - NTRK * 128;
  let v11 = 0, v12 = 0, v13 = 0, v15 = 0, v16 = 0, v17 = 0, v18 = 0, v19 = 0;
  const v14 = f >= 14 && f <= 19;
  v19 = f === 19; v18 = f === 18 || v19; v17 = f === 17 || v18; v16 = f === 16; v15 = f === 15;
  if (v10) {
    if (v14) v11 = v12 = v13 = 1;
    else if (f === 13) v11 = v12 = v13 = 1;
    else if (f === 12) v11 = v12 = 1;
    else if (f === 11) v11 = 1;
  }
  const np = b[66];
  const iord2 = np > 111 ? 1 : 0;
  let chn = v10 ? CHAIN_WIRE15_SIZE : 36;
  let mot, motMax, dataEnd = end;
  if ((v16 || v17) && v10) chn = CHAIN_WIRE16_SIZE;
  if (v12) {
    mot = np > 111 ? ((v16 || v17) ? MOTION_FILE_SIZE : MOTION_FILE_SIZE_V15) : MOTION_FILE_SIZE_128;
    dataEnd = PROJ_DX_OFF; motMax = (v16 || v17) ? MOTION_FILE_MAX : 72;
  } else if (v11) {
    mot = np > 111 ? MOTION_FILE_SIZE_V11 : MOTION_FILE_SIZE_128; dataEnd = PROJ_DX_OFF; motMax = 78;
  } else if (v10) {
    mot = np > 111 ? MOTION_FILE_SIZE_V10 : MOTION_FILE_SIZE_128; motMax = 110;
  } else { mot = PROJ_MOTION_V9; motMax = 64; }
  if (68 + NTRK * (np + iord2 + 2 + NSTEP * 9) + chn + mot > dataEnd) throw new Error("Invalid project layout");
  q.fm6_vib = v19 && (b[67] & 4) ? 1 : 0;
  q.g = GP_DEF.slice();
  for (let i = 0; i < 27; i++) q.g[i] = s16(b, 8 + 2 * i);
  if (!v17) tapeDefaults(q.g);
  q.sel = b[62]; q.parts = b[63]; q.phys = b[64];
  if (q.g[G_CRATE] < 0 || q.g[G_CRATE] > NTRK) q.g[G_CRATE] = 0;
  if (q.g[G_CDEPTH] < 0 || q.g[G_CDEPTH] > NTRK) q.g[G_CDEPTH] = 0;
  if (v14 && (b[PROJ_SCL_OFF] & 0x80) && (b[PROJ_SL_SEND_OFF] & 0x10)) {
    const mask = b[PROJ_SL_SEND_OFF] & 15;
    q.g[G_SLOWN] = mask === 15 ? 0 : mask;
  } else if (v14 && (b[PROJ_SCL_OFF] & 0x80)) {
    const so = (b[PROJ_SCL_OFF] >> 4) & 7; q.g[G_SLOWN] = so <= 4 ? so : 0;
  } else if (v12 || v13) {
    const so = b[PROJ_SCL_OFF + PROJ_SCL_SIZE]; q.g[G_SLOWN] = so <= 4 ? so : 0;
  } else q.g[G_SLOWN] = 0;
  if (v7 && (q.g[G_RTYPE] < 0 || q.g[G_RTYPE] > 1)) rtypeRoom(q.g);
  if (v11 && (b[67] & 1)) {
    q.g[G_CRTYPE] = b[65] <= 4 ? b[65] : 0;
    q.g[G_CRSIZE] = b[PROJ_CRV_OFF]; q.g[G_CRDAMP] = b[PROJ_CRV_OFF + 1];
  } else {
    q.g[G_CRTYPE] = q.g[G_RTYPE]; q.g[G_CRSIZE] = q.g[G_RSIZE]; q.g[G_CRDAMP] = q.g[G_RDAMP];
  }
  if (v19) q.g[G_TTEMPO] = (b[67] & 0x80) ? 1 : 0;
  let pos = 68;
  const osc2 = v19 && (b[67] & 2) !== 0;
  for (let t = 0; t < NTRK; t++) {
    const values = new Array(P_COUNT).fill(0);
    for (let i = 0; i < np; i++) {
      if (iord2 && i === P_IORD) {
        values[i] = u16(b, pos);
        if ((values[i] & 1023) > 719) throw new Error("Invalid project order");
        pos += 2;
      } else values[i] = wireValGet(b, pos++);
    }
    const d = q.t[t];
    d.engine = b[pos++]; d.preset = b[pos++];
    d.generation = GENERATION_DEFAULT.slice();
    paramsByCount(d.p, values, np, TP_DEF);
    insMigrate(d.p, np);
    if (!osc2 && d.engine === ENGI_ANALOG) { d.p[P_E7] = 0; d.p[P_MORPH] = 0; d.p[P_DRIFT] = 0; d.p[P_WAVE2] = 0; }
    if (!v13) d.p[P_SCALE] = sclFromV12(d.p[P_SCALE]);
    for (let i = 0; i < NSTEP; i++) {
      const s = d.steps[i];
      let nd = 0;
      for (let j = 0; j < 4; j++) { nd |= (b[pos] >> 7) << j; s.note[j] = b[pos++] & 127; }
      let meta = b[pos++];
      s.n = meta & 7; s.time = (meta >> 3) & 3; s.flags = (meta >> 5) & 3;
      s.vel = b[pos] & 127; meta |= (b[pos++] & 128) << 1;
      s.hit = b[pos++]; s.acc = b[pos++];
      s.prob = b[pos] & 127; meta |= (b[pos++] & 128) << 2;
      s.ratchet = (meta >> 8) + 1; s.nudge = (nd ^ 8) - 8;
    }
  }
  if (v16 || v17) {
    q.chain.count = b[pos];
    if (v18) {
      const mb = b[pos + 1] | b[pos + 2] << 8 | b[pos + 3] << 16;
      for (let t = 0; t < 8; t++) q.fx_master[t] = (mb >> (3 * t)) & 7;
    }
    for (let i = 0; i < CHAIN_ROWS; i++) {
      const w = u16(b, pos + 4 + i * 2);
      q.chain.row[i].repeat = ((w >> 12) + 1) & 0xff;
      for (let t = 0; t < NTRK; t++) q.chain.row[i].slot[t] = (w >> (3 * t)) & 7;
    }
  } else if (v10) {
    q.chain.count = b[pos];
    for (let i = 0; i < CHAIN_ROWS; i++) {
      const o = pos + 4 + i * 5;
      q.chain.row[i] = { slot: [b[o], b[o + 1], b[o + 2], b[o + 3]], repeat: b[o + 4] };
    }
  } else {
    const o = pos;
    q.chain.count = b[o];
    for (let i = 0; i < CHAIN_ROWS && i < q.chain.count; i++) {
      const slot = b[o + 4 + i * 2], rep = b[o + 5 + i * 2];
      q.chain.row[i] = { slot: [slot, slot, slot, slot], repeat: rep };
    }
  }
  pos += chn;
  if ((v15 || v16 || v17) && b[pos + 2] === PROJ_GENERATION_MARK) {
    for (let t = 0; t < NTRK; t++) {
      const g = new Array(6).fill(0);
      for (let i = 0; i < 6; i++) for (let bit = 0; bit < 8; bit++)
        g[i] |= ((b[PROJ_FM6_OFF + t * 128 + i * 8 + bit] >> 7) & 1) << bit;
      q.t[t].generation = g;
    }
  }
  if (v10) {
    q.motion.count = b[pos]; q.motion.on = b[pos + 1];
    if (q.motion.count > motMax) throw new Error("Invalid project motion count");
    if (v18) {
      q.fx_master[8] = b[pos + 3] & 7; q.fx_master[9] = (b[pos + 3] >> 3) & 7;
    }
    q.motion.event = [];
    for (let i = 0; i < q.motion.count; i++) {
      const o = pos + 4 + i * 3, id = b[o + 1] & 127;
      const e = { place: b[o], param: b[o + 1], value: 0 };
      if ((v15 || v16 || v17) && (id === MFX_TIME || (id > MFX_DAMP && id <= MFX_DAMP + 6))) {
        const hi = id === MFX_TIME ? 0 : id - MFX_DAMP;
        e.param = (MFX_TIME | (b[o + 1] & MOTION_LOCK)) & 0xff;
        e.value = (hi << 8) | b[o + 2];
      } else {
        e.value = id === MFX_TSPEED ? tspeedGet(b[o + 2]) : ((b[o + 2] << 24) >> 24);
      }
      q.motion.event.push(e);
    }
  } else {
    q.motion.count = b[pos]; q.motion.on = b[pos + 1];
    q.motion.event = [];
    for (let i = 0; i < q.motion.count; i++) {
      const o = pos + 4 + i * 4;
      q.motion.event.push({ place: b[o], param: b[o + 1], value: s16(b, o + 2) });
    }
  }
  motionIds(q.motion, np);
  if (!osc2) ktrMotion(q.motion, q.t);
  if (!v14) {
    for (const e of q.motion.event) {
      const id = e.param & 127;
      if (id === P_CHOR && e.value >= 0 && e.value < 64) e.param = (e.param & MOTION_LOCK) | 110; // P_IHMIX
    }
  }
  const busLayout = (v14 && (b[PROJ_SCL_OFF] & 0x80)) || (f === 13 && b[PROJ_SCL_MARK_OFF] === 1);
  for (let t = 0; t < NTRK; t++) {
    const d = q.t[t];
    if (v12 && busLayout) {
      if ((b[PROJ_SCL_OFF] & (1 << t)) && d.p[P_QUANT] === QN_ALL) d.p[P_QUANT] = QN_CURR;
      d.fx_sl_pos = v14 ? ((b[PROJ_SLPOS_OFF + (t >> 1)] >> ((t & 1) * 4)) & 15) : 8;
      if (d.fx_sl_pos >= 10) d.fx_sl_pos = 8;
    } else if (v12) {
      const o = PROJ_SCL_OFF + t * 4;
      if ((b[o] & 1) && d.p[P_QUANT] === QN_ALL) d.p[P_QUANT] = QN_CURR;
      d.fx_sl_pos = 8;
    } else {
      d.fx_sl_pos = 8;
    }
  }
  if (busLayout) {
    for (let i = 0; i < PROJ_FXBUS_SCL_N; i++) q.fx_bus[i] = b[PROJ_SCL_OFF + 1 + i];
    for (let i = PROJ_FXBUS_SCL_N; i < FX_BUS_N; i++) q.fx_bus[i] = b[PROJ_DX_MOT_OFF + i - PROJ_FXBUS_SCL_N];
  } else q.fx_bus = FX_BUS_DEF.slice();
  for (let t = 0; t < NTRK; t++) {
    q.fm6[t] = v7 ? FM6_INIT.slice() : Array.from({ length: 128 }, (_, i) => b[end + t * 128 + i] & 0x7f);
    if (v17) {
      const ord = new Array(10).fill(0);
      for (let k = 0; k < 10; k++) for (let j = 0; j < 4; j++)
        ord[k] |= ((b[PROJ_FM6_OFF + t * 128 + 48 + k * 4 + j] >> 7) & 1) << j;
      if (!orderOk(ord)) throw new Error("Invalid project EFFECTS order");
      q.t[t].fx_order = ord;
    } else {
      q.t[t].fx_order = fxOrderDefault(q.t[t].p[P_IORD], q.t[t].p[P_CHOR], q.t[t].fx_sl_pos);
      q.t[t].p[P_TAPEMIX] = 0;
    }
  }
  if (v18) {
    for (let i = 0; i < 3; i++) q.fx_master_order |= ((b[PROJ_FM6_OFF + PROJ_MASTER_ORDER_OFF + i] >> 7) & 1) << i;
  }
  if (v11) {
    const kitLayout = (v14 && (b[PROJ_SCL_OFF] & 0x80)) || (f === 13 && b[PROJ_SCL_MARK_OFF] === 1);
    q.dx = Array.from({ length: 88 }, (_, i) => b[PROJ_DX_OFF + i]);
    q.dx_mute = (b[PROJ_DX_OFF + 88] & 15) | (b[PROJ_DX_OFF + 89] << 8);
    if (!kitLayout) {
      q.dx_mot = [b[PROJ_DX_OFF + 91], b[PROJ_DX_OFF + 92], b[PROJ_DX_OFF + 93], b[PROJ_DX_OFF + 94]];
      if (q.dx_mot[0] > 8) q.dx_mot[0] = 0;
      q.dx_mot[1] &= 127; if (q.dx_mot[2] > 4) q.dx_mot[2] = 0; q.dx_mot[3] &= 127;
    }
  }
  const n = [];
  for (let i = 0, k = nameOff; i < PROJ_NAME_LEN && b[k]; i++, k++) {
    let c = b[k];
    if (c < 32 || c > 126) { n.length = 0; break; }
    if (c >= 97 && c <= 122) c -= 32;
    n.push(c);
  }
  while (n.length < PROJ_NAME_LEN) n.push(0);
  q.name = n;
  drumsToPart(q);
  physConvert(q);
  return q;
}

function packProject(q) {
  const b = new Uint8Array(PROJ_STORE_SIZE);
  const put16 = (o, v) => { b[o] = v & 255; b[o + 1] = (v >> 8) & 255; };
  const put32 = (o, v) => { b[o] = v & 255; b[o + 1] = (v >> 8) & 255; b[o + 2] = (v >> 16) & 255; b[o + 3] = (v >>> 24) & 255; };
  put32(0, MAGIC[19]); put32(4, PROJ_STORE_SIZE);
  for (let i = 0; i < 27; i++) put16(8 + i * 2, q.g[i]);
  b[62] = q.sel; b[63] = q.parts; b[64] = q.phys;
  b[65] = clamp(q.g[G_CRTYPE], 0, 4); b[66] = P_COUNT;
  b[67] = 1 | (clamp(q.g[G_TTEMPO], 0, 1) << 7) | 2 | 4;
  b[PROJ_CRV_OFF] = clamp(q.g[G_CRSIZE], 0, 127);
  b[PROJ_CRV_OFF + 1] = clamp(q.g[G_CRDAMP], 0, 127);
  let pos = 68;
  for (let t = 0; t < NTRK; t++) {
    const d = q.t[t];
    for (let i = 0; i < P_COUNT; i++) {
      const v = d.p[i];
      if (i === P_IORD) {
        if ((v & 1023) > 719) throw new Error("Project order out of range");
        b[pos++] = v & 255; b[pos++] = (v >> 8) & 255;
      } else {
        if (v < -64 || v > 127) throw new Error(`Project parameter ${i} out of range: ${v}`);
        b[pos++] = (v + 64) & 255;
      }
    }
    b[pos++] = d.engine; b[pos++] = d.preset;
    for (const s of d.steps) {
      if (s.n > 4 || s.time > 2 || s.prob > 101) throw new Error("Invalid project step");
      const nd = s.nudge & 15;
      for (let j = 0; j < 4; j++) b[pos++] = (clamp(s.note[j], 0, 127) | ((nd >> j & 1) << 7)) & 255;
      b[pos++] = (s.n | s.time << 3 | (s.flags & 3) << 5) & 255;
      const r = (s.ratchet || 1) - 1;
      b[pos++] = (clamp(s.vel, 0, 127) | (r & 1) << 7) & 255;
      b[pos++] = s.hit & 255;
      b[pos++] = (s.acc & s.hit) & 255;
      b[pos++] = ((s.prob & 127) | ((r >> 1) << 7)) & 255;
    }
  }
  if (pos + CHAIN_WIRE16_SIZE + MOTION_FILE_SIZE > PROJ_SCL_OFF) throw new Error("Project layout overflow");
  b[pos++] = q.chain.count;
  let mb = 0;
  for (let t = 0; t < 8; t++) mb |= (q.fx_master[t] & 7) << (3 * t);
  b[pos++] = mb & 255; b[pos++] = (mb >> 8) & 255; b[pos++] = (mb >> 16) & 255;
  for (let i = 0; i < CHAIN_ROWS; i++) {
    const r = q.chain.row[i], rp = r.repeat || 1;
    let w = (rp ? rp - 1 : 0) << 12;
    for (let t = 0; t < NTRK; t++) w |= (r.slot[t] & 7) << (3 * t);
    b[pos++] = w & 255; b[pos++] = (w >> 8) & 255;
  }
  const mc = Math.min(q.motion.count, MOTION_FILE_MAX);
  b[pos] = mc; b[pos + 1] = q.motion.on; b[pos + 2] = PROJ_GENERATION_MARK;
  b[pos + 3] = (q.fx_master[8] & 7) | ((q.fx_master[9] & 7) << 3);
  for (let i = 0; i < mc; i++) {
    const e = q.motion.event[i];
    let id = e.param & 127;
    const o = pos + 4 + i * 3;
    b[o] = e.place;
    if (id === MFX_TIME && e.value >= 256) id = MFX_DAMP + (e.value >> 8);
    b[o + 1] = (id | (e.param & MOTION_LOCK)) & 255;
    b[o + 2] = (id === MFX_TSPEED ? tspeedPut(e.value) : e.value) & 255;
  }
  const locks = [], slpos = [];
  let slp = 0;
  for (let t = 0; t < NTRK; t++) {
    if (q.t[t].p[P_QUANT] === QN_CURR) locks.push(t);
    const sp = q.t[t].fx_sl_pos < 10 ? q.t[t].fx_sl_pos : 8;
    slp |= (sp & 15) << (t * 4);
  }
  let mask, leg = 0;
  const raw = q.g[G_SLOWN];
  if (raw <= NTRK) {
    leg = raw;
    mask = raw === 0 ? 15 : 15 ^ (1 << (raw - 1));
  } else {
    mask = raw & 15;
    const ch = (~mask) & 15;
    if (ch && !(ch & (ch - 1))) { leg = Math.log2(ch) + 1; }
  }
  b[PROJ_SCL_OFF] = (0x80 | (leg << 4) | locks.reduce((m, t) => m | (1 << t), 0)) & 255;
  b[PROJ_SL_SEND_OFF] = (mask | 0x10) & 255;
  for (let i = 0; i < PROJ_FXBUS_SCL_N; i++) b[PROJ_SCL_OFF + 1 + i] = q.fx_bus[i] & 255;
  for (let i = PROJ_FXBUS_SCL_N; i < FX_BUS_N; i++) b[PROJ_DX_MOT_OFF + i - PROJ_FXBUS_SCL_N] = q.fx_bus[i] & 255;
  b[PROJ_SLPOS_OFF] = slp & 255; b[PROJ_SLPOS_OFF + 1] = (slp >> 8) & 255;
  for (let i = 0; i < 88; i++) b[PROJ_DX_OFF + i] = q.dx[i] & 255;
  b[PROJ_DX_OFF + 88] = q.dx_mute & 15; b[PROJ_DX_OFF + 89] = (q.dx_mute >> 8) & 255; b[PROJ_DX_OFF + 90] = 1;
  for (let t = 0; t < NTRK; t++) for (let i = 0; i < 128; i++) {
    if (q.fm6[t][i] > 127) throw new Error("Project FM6 patch out of range");
    b[PROJ_FM6_OFF + t * 128 + i] = q.fm6[t][i];
  }
  for (let t = 0; t < NTRK; t++) {
    for (let i = 0; i < 6; i++) for (let bit = 0; bit < 8; bit++)
      b[PROJ_FM6_OFF + t * 128 + i * 8 + bit] |= ((q.t[t].generation[i] >> bit) & 1) << 7;
    const ord = orderOk(q.t[t].fx_order) ? q.t[t].fx_order : fxOrderDefault(q.t[t].p[P_IORD], q.t[t].p[P_CHOR], q.t[t].fx_sl_pos);
    for (let k = 0; k < 10; k++) for (let j = 0; j < 4; j++)
      b[PROJ_FM6_OFF + t * 128 + 48 + k * 4 + j] |= ((ord[k] >> j) & 1) << 7;
  }
  for (let i = 0; i < 3; i++) b[PROJ_FM6_OFF + PROJ_MASTER_ORDER_OFF + i] |= ((q.fx_master_order >> i) & 1) << 7;
  for (let i = 0; i < PROJ_NAME_LEN; i++) b[PROJ_NAME_OFF + i] = q.name[i] & 255;
  const sum = projectHash(b, PROJ_STORE_SIZE - 4);
  b[PROJ_STORE_SIZE - 4] = sum & 255; b[PROJ_STORE_SIZE - 3] = (sum >> 8) & 255;
  b[PROJ_STORE_SIZE - 2] = (sum >> 16) & 255; b[PROJ_STORE_SIZE - 1] = (sum >> 24) & 255;
  return b;
}

function validHash(b) {
  return u32(b, b.length - 4) === projectHash(b, b.length - 4);
}

// A project of any format -> FUN19 bytes. digital: the editor's DIGITAL -> FM6 converter (see above).
export function convertProject(rawBytes, digital) {
  const bytes = rawBytes instanceof Uint8Array ? rawBytes : Uint8Array.from(rawBytes);
  const f = formatOf(bytes);
  if (!f) throw new Error("Not a Felucca project");
  if (!validHash(bytes)) throw new Error("Project checksum mismatch");
  const q = f <= 6 ? decodeOld(bytes, f) : decodeModern(bytes, f);
  fm4Convert(q, digital);
  percConvert(q);
  retireConvert(q);
  return packProject(q);
}


// Whether a stored project needs the client-side conversion (an old format, or any DIGITAL track).
export function needsConversion(rawBytes) {
  const bytes = rawBytes instanceof Uint8Array ? rawBytes : Uint8Array.from(rawBytes);
  const f = formatOf(bytes);
  if (!f) return false;
  if (f <= 6) return true;
  const np = bytes[66];
  if (np < 8 || np > P_COUNT) return false;
  const iord2 = np > 111 ? 1 : 0;
  const stride = np + iord2 + 2 + NSTEP * 9;
  let pos = 68;
  for (let t = 0; t < NTRK; t++, pos += stride) if (bytes[pos + np + iord2] === ENGI_DIGITAL) return true;
  return false;
}
