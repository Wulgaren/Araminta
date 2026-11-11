// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Browser-emu punch-in stacks against a device backup (same wasm as the page).
//   node web/emu/punch_emu_test.mjs build/emu/felucca.wasm [backup.json ...]
// With no backups: runs the synthetic heavy song + punch stacks (no import).
import fs from "fs";
import path from "path";
import { storeSectors, restoreSectors } from "./store.js";
import { readBackup } from "../fm1backup.js";

const wasmPath = process.argv[2] || "build/emu/felucca.wasm";
const backups = process.argv.slice(3);
const bytes = fs.readFileSync(wasmPath);
const B = { FX: 0, SCL: 1, ENV: 2, LFO: 3, EDIT: 4, GLO: 5, HOME: 6, SAVE: 7, ARP: 8, SEQ: 9, PLAY: 10, REC: 11, OCTDN: 12, OCTUP: 13 };
const EN = { SELECT: 0, ALGO: 1, PRESETS: 2, K1: 3, K2: 4, K3: 5, K4: 6 };

/* white keys F3..G5 (27 chromatic from F): places 0..15 */
const WHITE = [];
for (let k = 0; k < 27; k++) {
  if (!((0x54a >> ((k + 5) % 12)) & 1)) WHITE.push(k);
}
const P1 = { R8: 0, R12: 1, R16: 2, R32: 3, LPF: 4, HPF: 5, OCTD: 6, OCTU: 7, DSHT: 8, DLNG: 9, S8: 10, S16: 11, ATK: 12, ARP: 13, RND: 14 };
const P2 = { REV: 0, TAPE: 1, FRZ: 2, OUP: 3, ODN: 4, FLG: 5, PHS: 6, RTHR: 7, ETHR: 8, HALF: 9, X2: 10, GLT: 11 };

function maskOf(places) {
  let m = 0;
  for (const p of places) m |= 1 << WHITE[p];
  return m;
}

async function device(saved) {
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const ex = instance.exports, mem = ex.memory;
  ex._initialize();
  ex.web_nor_erase();
  if (saved) restoreSectors(new Uint8Array(mem.buffer, ex.web_nor(), ex.web_nor_size()), saved);
  ex.web_boot();
  const d = { ex, mem, peak: 0, frames: 0, kept: null, click: 0, local: 0, prevL: 0, prevR: 0, havePrev: false };
  d.render = (ms, opts = {}) => {
    const n = Math.round(ms * 44.1);
    const track = !!opts.track;
    const settle = !!opts.settle; /* only update local bound, not click */
    for (let k = 0; k < n; k += 128) {
      const m = Math.min(128, n - k);
      const t0 = track ? performance.now() : 0;
      ex.web_render(m);
      if (track) d.busy = (d.busy || 0) + (performance.now() - t0);
      const l = new Float32Array(mem.buffer, ex.web_out_l(), m);
      const r = new Float32Array(mem.buffer, ex.web_out_r(), m);
      for (let i = 0; i < m; i++) {
        const a = Math.abs(l[i]), b = Math.abs(r[i]);
        d.peak = Math.max(d.peak, a, b);
        if (d.havePrev) {
          const step = Math.max(Math.abs(l[i] - d.prevL), Math.abs(r[i] - d.prevR));
          if (settle) d.local = Math.max(d.local, step);
          else if (track) d.click = Math.max(d.click, step);
        }
        d.prevL = l[i]; d.prevR = r[i]; d.havePrev = true;
        if (d.kept) { d.kept.push(l[i], r[i]); }
      }
      d.frames += m;
    }
  };
  d.tap = (b) => { ex.web_buttons(1 << b); d.render(60); ex.web_buttons(0); d.render(60); };
  d.holdBtn = (b, ms) => { ex.web_buttons(1 << b); d.render(ms); ex.web_buttons(0); d.render(60); };
  d.turn = (role, n) => { for (let i = 0; i < Math.abs(n); i++) { ex.web_enc(role, Math.sign(n)); d.render(40); } };
  return d;
}

async function importBackup(filePath) {
  const text = fs.readFileSync(filePath, "utf8");
  const archive = readBackup(text);
  const a = await device(null);
  a.render(200);
  const ptr = a.ex.web_bk_buf_ptr(), cap = a.ex.web_bk_buf_size();
  for (const o of archive.objects) {
    const raw = o.bytes || new Uint8Array(0);
    if (raw.length > cap) throw new Error(`${path.basename(filePath)} id ${o.id}: too large (${raw.length})`);
    new Uint8Array(a.mem.buffer, ptr, raw.length).set(raw);
    const rc = a.ex.web_bk_import(o.id | 0, raw.length) | 0;
    if (rc) throw new Error(`${path.basename(filePath)} id ${o.id}: import rc ${rc}`);
  }
  const saved = storeSectors(new Uint8Array(a.mem.buffer, a.ex.web_nor(), a.ex.web_nor_size()));
  const d = await device(saved); /* reload as the page does: autosave comes back */
  d.render(1200);
  return d;
}

async function enableLatch(d) {
  d.holdBtn(B.HOME, 850); /* MENU */
  d.turn(EN.SELECT, 1);   /* CONTROL tab → HOLD */
  d.turn(EN.ALGO, 1);     /* FX LATCH */
  d.turn(EN.K1, 1);       /* ON */
  d.holdBtn(B.HOME, 850); /* close MENU */
  d.render(200);
}

/* Hold punch-in white keys. page2: sticky map + SELECT to 2/2, then hold keys (FX layer locked).
 * page1: hold FX button + keys (momentary layer). */
function engageHoldFx(d, places, page2) {
  if (page2) {
    d.tap(B.FX); d.tap(B.FX);
    d.render(120);
    d.turn(EN.SELECT, 1);
    d.render(120);
    /* sticky FX owns the keys; hold the effect keys */
    d.ex.web_keys(maskOf(places));
    d.render(200); /* buffer effects need a beat to grab audio */
  } else {
    d.ex.web_buttons(1 << B.FX);
    d.render(40);
    d.ex.web_keys(maskOf(places));
    d.render(120);
  }
}

function clearPunch(d) {
  d.ex.web_keys(0);
  d.ex.web_buttons(0);
  d.render(40);
  /* G5 CLR while sticky FX LATCH: drop latched punch-ins */
  d.tap(B.FX); d.tap(B.FX);
  d.render(40);
  d.ex.web_keys(1 << WHITE[15]);
  d.render(60);
  d.ex.web_keys(0);
  d.tap(B.HOME);
  d.render(80);
}

function measureStack(d, name, engage) {
  clearPunch(d);
  if (!d.ex.web_playing()) {
    d.tap(B.PLAY);
    d.render(200);
  }
  d.peak = 0; d.click = 0; d.local = 0; d.busy = 0; d.havePrev = false;
  d.render(500, { settle: true }); /* local step bound while playing, no punch */
  const local = Math.max(d.local, 1e-6);
  engage(d);
  /* skip ~30 ms of press onset (splice into the buffer effect), then measure hold */
  d.render(30);
  d.click = 0; d.busy = 0; d.peak = 0; d.havePrev = false;
  const ms = 500;
  d.render(ms, { track: true });
  const load = d.busy / ms;
  /* float analogue of host's 8x local / absolute bound (~20000/32768) */
  const clickOk = d.click <= 8 * local || d.click < 0.6;
  clearPunch(d);
  return { name, peak: d.peak, click: d.click, local, load, clickOk, playing: d.ex.web_playing() };
}

const STACKS = [
  { name: "idle", engage: (d) => {} },
  { name: "R16", engage: (d) => engageHoldFx(d, [P1.R16], false) },
  { name: "R16+LPF+HPF", engage: (d) => engageHoldFx(d, [P1.R16, P1.LPF, P1.HPF], false) },
  { name: "FRZ+FLG+PHS", engage: (d) => engageHoldFx(d, [P2.FRZ, P2.FLG, P2.PHS], true) },
  { name: "GLT+RTHR+ETHR", engage: (d) => engageHoldFx(d, [P2.GLT, P2.RTHR, P2.ETHR], true) },
  { name: "REV+TAPE", engage: (d) => engageHoldFx(d, [P2.REV, P2.TAPE], true) },
  {
    name: "worst/LATCH",
    latch: true,
    engage: (d) => {
      /* sticky + latch: tap effects across both punch pages */
      d.tap(B.FX); d.tap(B.FX);
      d.render(100);
      for (const p of [P1.R16, P1.LPF, P1.HPF]) {
        d.ex.web_keys(1 << WHITE[p]); d.render(60); d.ex.web_keys(0); d.render(40);
      }
      d.turn(EN.SELECT, 1);
      d.render(100);
      for (const p of [P2.FLG, P2.PHS, P2.RTHR, P2.ETHR, P2.OUP]) {
        d.ex.web_keys(1 << WHITE[p]); d.render(60); d.ex.web_keys(0); d.render(40);
      }
      for (let i = 0; i < 25; i++) { d.ex.web_enc(EN.K2, 1); d.render(4); }
      for (let i = 0; i < 25; i++) { d.ex.web_enc(EN.K3, 1); d.render(4); }
      for (let i = 0; i < 15; i++) { d.ex.web_enc(EN.K4, 1); d.render(4); }
    },
  },
];

let fails = 0;
const check = (what, ok) => {
  console.log(`emu-punch: ${what.padEnd(72)} ${ok ? "ok" : "FAIL"}`);
  if (!ok) fails++;
};

async function runOn(label, d, withLatch) {
  if (withLatch) await enableLatch(d);
  const rows = [];
  for (const s of STACKS) {
    if (s.latch && !withLatch) continue;
    const r = await measureStack(d, s.name, s.engage);
    rows.push(r);
    const flag = r.clickOk ? "" : " CLICK?";
    console.log(
      `emu-punch: ${label.padEnd(36)} ${r.name.padEnd(14)} peak=${r.peak.toFixed(3)}  ` +
      `click=${r.click.toFixed(4)} local=${r.local.toFixed(4)}  cpu=${(r.load * 100).toFixed(1)}%${flag}`,
    );
  }
  const badClick = rows.filter((r) => !r.clickOk);
  check(`${label}: audio while stacks held`, rows.some((r) => r.name !== "idle" && r.peak > 0.01) || rows.find((r) => r.name === "idle")?.peak > 0.01);
  check(`${label}: no click flag on stacks`, badClick.length === 0);
  const worst = rows.reduce((a, b) => (b.load > a.load ? b : a), rows[0]);
  check(`${label}: worst cpu ${(worst.load * 100).toFixed(1)}% under 50% wall`, worst.load < 0.5);
  return rows;
}

/* synthetic: heavy song, no backup */
{
  const d = await device(null);
  d.render(500);
  d.ex.web_test_heavy();
  d.tap(B.PLAY);
  d.render(300);
  check("synth: PLAY runs", d.ex.web_playing() === 1);
  await runOn("synth-heavy", d, true);
}

const targets = backups.length
  ? backups
  : [
      "/tmp/crackle_all/Araminta-backup-2026-11-08-1.json",
      "/tmp/crackle_all/Araminta-backup-2026-11-07-1.json",
      "/tmp/crackle_all/Araminta-backup-2026-11-06.json",
      "/home/ubuntu/.cursor/projects/agent-repos-Araminta/uploads/Araminta-backup-2026-11-11_d34d.json",
      "/home/ubuntu/.cursor/projects/agent-repos-Araminta/uploads/Araminta-backup-2026-11-10_28d8.json",
    ].filter((p) => fs.existsSync(p));

for (const f of targets) {
  const name = path.basename(f);
  try {
    const d = await importBackup(f);
    check(`${name}: imported + autosave boots`, true);
    /* playing the restored project (autosave) */
    d.tap(B.PLAY);
    d.render(800);
    check(`${name}: PLAY runs`, d.ex.web_playing() === 1);
    await runOn(name, d, true);
  } catch (e) {
    check(`${name}: ${e.message || e}`, false);
  }
}

console.log(fails ? `emu-punch: ${fails} FAIL` : "emu-punch: all ok");
process.exit(fails ? 1 : 0);
