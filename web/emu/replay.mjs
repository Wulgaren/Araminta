// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wulgaren
// Replay a session recording bit for bit: the same wasm boot as the worklet, then every recorded input at its
// frame, then web_render(128). Shared audio-health maths (web/emu/rec.js) must match the browser's seconds[].sum.
//   node web/emu/replay.mjs WASM REC.json [--wav OUT.wav]
import fs from "fs";
import { pathToFileURL } from "url";
import { restoreSectors } from "./store.js";
import { REC_BUCKET, recAudio, recQuantum } from "./rec.js";

const RATE = 44100;

// boot like the worklet's load, then render every quantum with the recorded inputs applied first
export async function replay(wasmBytes, rec, onQuantum) {
  const { instance } = await WebAssembly.instantiate(wasmBytes, {});
  const ex = instance.exports;
  ex._initialize();
  ex.web_nor_erase();
  const sectors = {};
  for (const [k, v] of Object.entries(rec.sectors)) sectors[k] = new Uint8Array(Buffer.from(v, "base64"));
  restoreSectors(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()), sectors);
  ex.web_master(rec.master);
  ex.web_boot();

  const audio = recAudio();
  const seconds = [], jumps = [];
  const events = rec.events;
  let ei = 0, q = 0, bucketF = 0;
  const frames = rec.frames - (rec.frames % 128);

  for (let F = 0; F < frames; F += 128) {
    while (ei < events.length && events[ei].f <= F) {
      const e = events[ei++];
      if (e.type === "buttons") ex.web_buttons(e.mask >>> 0);
      else if (e.type === "keys") ex.web_keys(e.mask >>> 0);
      else if (e.type === "enc") ex.web_enc(e.role, e.n | 0);
      else if (e.type === "master") ex.web_master(e.value | 0);
      else if (e.type === "midi") ex.web_midi(e.data[0] | 0, e.data[1] | 0, e.data[2] | 0);
    }
    ex.web_render(128);
    const l = new Float32Array(ex.memory.buffer, ex.web_out_l(), 128);
    const r = new Float32Array(ex.memory.buffer, ex.web_out_r(), 128);
    const step = recQuantum(audio, l, r);
    if (audio.click) jumps.push({ f: F, jump: step });
    if (onQuantum) onQuantum(l, r);
    if (!q) bucketF = F;
    q++;
    if (q >= REC_BUCKET) {
      seconds.push({ f: bucketF, sum: audio.sum, peak: audio.peak, jump: audio.jump });
      audio.sum = audio.peak = audio.jump = 0;
      q = 0;
    }
  }
  if (q) seconds.push({ f: bucketF, sum: audio.sum, peak: audio.peak, jump: audio.jump });   // the browser's partial
  return { seconds, jumps };
}

// 44.1 kHz stereo 16-bit PCM WAV from Int16 interleaved chunks
function writeWav(path, chunks) {
  let n = 0;
  for (const c of chunks) n += c.length;
  const data = Buffer.alloc(44 + n * 2);
  data.write("RIFF", 0);
  data.writeUInt32LE(36 + n * 2, 4);
  data.write("WAVEfmt ", 8);
  data.writeUInt32LE(16, 16);
  data.writeUInt16LE(1, 20);
  data.writeUInt16LE(2, 22);
  data.writeUInt32LE(RATE, 24);
  data.writeUInt32LE(RATE * 4, 28);
  data.writeUInt16LE(4, 32);
  data.writeUInt16LE(16, 34);
  data.write("data", 36);
  data.writeUInt32LE(n * 2, 40);
  let off = 44;
  for (const c of chunks) for (let i = 0; i < c.length; i++, off += 2) data.writeInt16LE(c[i], off);
  fs.writeFileSync(path, data);
}

function sec(f) { return (f / RATE).toFixed(3); }

// the page's per-quantum health: base64 of the worklet's Float32Array, copied onto an aligned buffer
function readQuanta(b64) {
  const b = Buffer.from(b64, "base64");
  return new Float32Array(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
}

function eventLabel(e) {
  if (e.type === "buttons" || e.type === "keys") return `${e.type} mask=${e.mask}`;
  if (e.type === "enc") return `enc role=${e.role} n=${e.n}`;
  if (e.type === "master") return `master ${e.value}`;
  if (e.type === "midi") return `midi [${e.data.join(",")}]`;
  if (e.type === "mark") return `mark ${JSON.stringify(e.note)}`;
  if (e.type === "visible") return `visible ${e.on}`;
  return e.type;
}

// the quanta whose whole process() took longer than a quantum lasts, runs within 1 s of each other as one group
function overGroups(q, budget) {
  const groups = [];
  for (let i = 0; i < q.length / 4; i++) {
    const f = i * 128, total = q[i * 4 + 1];
    if (total <= budget) continue;
    const last = groups[groups.length - 1];
    if (last && f - last.endF <= RATE) {
      last.count++;
      last.endF = f;
      last.worst = Math.max(last.worst, total);
    } else groups.push({ f, endF: f, count: 1, worst: total });
  }
  return groups;
}

async function main() {
  const args = process.argv.slice(2);
  const wavAt = args.indexOf("--wav");
  const wavPath = wavAt >= 0 ? args[wavAt + 1] : null;
  const pos = wavAt >= 0 ? args.filter((_, i) => i !== wavAt && i !== wavAt + 1) : args;
  const wasmPath = pos[0], recPath = pos[1];
  if (!wasmPath || !recPath) {
    console.error("usage: node web/emu/replay.mjs WASM REC.json [--wav OUT.wav]");
    process.exit(1);
  }
  const rec = JSON.parse(fs.readFileSync(recPath, "utf8"));
  for (const k of ["master", "sectors", "events", "frames", "seconds", "quanta"])
    if (rec[k] === undefined) { console.error(`${recPath}: not a recording (no ${k})`); process.exit(1); }
  const wasmBytes = fs.readFileSync(wasmPath);
  const chunks = [];
  const out = await replay(wasmBytes, rec, wavPath ? (l, r) => {
    const n = l.length, pcm = new Int16Array(n * 2);
    for (let i = 0; i < n; i++) {
      pcm[i * 2] = Math.max(-32768, Math.min(32767, Math.round(l[i] * 32768)));
      pcm[i * 2 + 1] = Math.max(-32768, Math.min(32767, Math.round(r[i] * 32768)));
    }
    chunks.push(pcm);
  } : undefined);
  if (wavPath) writeWav(wavPath, chunks);

  const dur = (rec.frames / RATE).toFixed(3);
  const head = [rec.hash && `hash ${rec.hash}`, rec.version && `version ${rec.version}`, rec.started && `started ${rec.started}`]
    .filter(Boolean).join(", ");
  if (head) console.log(head);
  console.log(`duration ${dur} s, ${rec.events.length} events`);

  const rows = rec.partial ? [...rec.seconds, rec.partial] : rec.seconds;
  let diverge = -1;
  if (rows.length !== out.seconds.length) diverge = Math.min(rows.length, out.seconds.length);
  for (let i = 0; diverge < 0 && i < rows.length; i++)
    if (out.seconds[i].sum !== rows[i].sum) diverge = i;
  if (diverge < 0)
    console.log(`replay matches the browser's audio bit for bit (${(rec.frames / RATE).toFixed(1)} s)`);
  else
    console.log(`replay diverges from the browser at ${(rows[diverge].f / RATE).toFixed(1)} s`);

  const budget = 1000 * 128 / RATE, q = readQuanta(rec.quanta), nq = q.length / 4, overs = overGroups(q, budget);
  for (const g of overs)
    console.log(`browser render over budget (audible as a dropout, not in the samples): ${sec(g.f)} s, count ${g.count}, worst ${g.worst.toFixed(3)} ms vs budget ${budget.toFixed(3)} ms`);

  if (nq) {
    const totals = [];
    let over = 0;
    for (let i = 0; i < nq; i++) { const t = q[i * 4 + 1]; totals.push(t); if (t > budget) over++; }
    totals.sort((a, b) => a - b);
    console.log(`quanta over budget ${over} / ${nq}, 99th-percentile total ${totals[Math.ceil(nq * 0.99) - 1].toFixed(3)} ms (budget ${budget.toFixed(3)} ms)`);
  }

  for (const j of out.jumps) {
    const before = rec.events.filter((e) => e.f <= j.f).slice(-3);
    console.log(`jump ${sec(j.f)} s size ${j.jump}` + (before.length ? "; last inputs: " + before.map((e) => `${eventLabel(e)} @ ${sec(e.f)} s`).join(", ") : ""));
  }

  for (const e of rec.events.filter((x) => x.type === "mark")) {
    const t0 = e.f - 2 * RATE, t1 = e.f;
    const nearOver = overs.filter((g) => g.endF >= t0 && g.f <= t1);
    const nearJump = out.jumps.filter((j) => j.f >= t0 && j.f <= t1);
    console.log(`mark ${JSON.stringify(e.note)} at ${sec(e.f)} s` +
      (nearOver.length || nearJump.length
        ? ": " + [
          ...nearOver.map((g) => `over x${g.count} @ ${sec(g.f)} s`),
          ...nearJump.map((j) => `jump ${j.jump} @ ${sec(j.f)} s`),
        ].join(", ")
        : ""));
    if (nq) {
      const worst = [];
      for (let i = Math.max(0, Math.floor(t0 / 128)), end = Math.min(nq - 1, Math.floor(t1 / 128)); i <= end; i++)
        worst.push({ f: i * 128, run: q[i * 4], total: q[i * 4 + 1], peak: q[i * 4 + 2], step: q[i * 4 + 3] });
      worst.sort((a, b) => b.total - a.total);
      for (const w of worst.slice(0, 3))
        console.log(`  ${sec(w.f)} s run ${w.run.toFixed(3)} ms total ${w.total.toFixed(3)} ms peak ${w.peak.toFixed(3)} step ${w.step.toFixed(4)}`);
    }
  }
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) await main();
