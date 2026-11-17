// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wulgaren
// The recorder: the real worklet source driven in Node, then web/emu/replay.mjs on what it saved.
//   node web/emu/rec_test.mjs build/emu/felucca.wasm
// Checks: every input goes into the recording once, in order; a key sounds; the replay's samples are the worklet's
// bit for bit; and the replay's summary rows carry the same sums.
import fs from "fs";
import os from "os";
import path from "path";
import { spawnSync } from "child_process";
import vm from "vm";
import { fileURLToPath } from "url";
import { replay } from "./replay.mjs";

const wasmPath = process.argv[2] || "build/emu/felucca.wasm";
const wasmBytes = new Uint8Array(fs.readFileSync(wasmPath));
let fails = 0;
const check = (what, ok) => { console.log(`rec: ${what.padEnd(76)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };

const here = (f) => fileURLToPath(new URL(f, import.meta.url));
const source = ["store.js", "rec.js", "worklet.js"]
  .map((f) => fs.readFileSync(here(f), "utf8").replace(/^export /gm, ""))
  .join("\n");

// ---- the AudioWorkletGlobalScope, as far as the worklet reaches into it
const posted = [];
let cls = null;
let now = 0;
const sandbox = {
  sampleRate: 44100,
  currentTime: 0,
  performance,
  WebAssembly,
  console,
  Date, Math, Object, Array, String, Number, Boolean, Error, JSON, Promise,
  Uint8Array, Uint16Array, Float32Array, ArrayBuffer, Atomics,
  globalThis: null,
  AudioWorkletProcessor: class {
    constructor() {
      this.port = { postMessage: (m) => posted.push(m), onmessage: null };
    }
  },
  registerProcessor: (name, c) => { cls = c; },
};
sandbox.globalThis = sandbox;
vm.createContext(sandbox);
vm.runInContext(source, sandbox);
const proc = new cls();

const waitFor = async (type) => {
  for (let i = 0; i < 4000 && !posted.some((m) => m.type === type); i++) await new Promise((r) => setTimeout(r, 5));
  const m = posted.find((x) => x.type === type);
  if (!m) throw new Error(`the worklet never posted ${type}`);
  return m;
};
const send = (data) => proc.port.onmessage({ data });

const left = [], right = [];
let frames = 0;
const render = (n) => {                       // n frames, whole 128-frame quanta
  for (let k = 0; k < n; k += 128) {
    const l = new Float32Array(128), r = new Float32Array(128);
    sandbox.currentTime = now += 128 / 44100;
    proc.process([], [[l, r]]);
    left.push(l);
    right.push(r);
    frames += 128;
  }
};
const sec = (s) => render(Math.max(128, Math.round(s * 44100 / 128) * 128));

// the page's saveRec: Float32Array quanta -> base64 (+ fields), for the CLI
function pageRec(dump) {
  const q = dump.quanta;
  check("quanta length is 4 floats per recorded quantum", q.length === 4 * dump.frames / 128);
  const bytes = new Uint8Array(q.buffer, q.byteOffset, q.byteLength);
  let s = "";
  for (let i = 0; i < bytes.length; i += 4096) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 4096));
  return { master: 700, sectors: {}, ...dump, quanta: btoa(s), quantaFields: ["run", "total", "peak", "step"] };
}

function rowsOf(rec) { return rec.partial ? [...rec.seconds, rec.partial] : rec.seconds; }

function runCli(rec, wavPath) {
  const recPath = path.join(os.tmpdir(), `araminta-rec-test-${process.pid}-${Math.random().toString(36).slice(2)}.json`);
  fs.writeFileSync(recPath, JSON.stringify(rec));
  const args = [here("replay.mjs"), wasmPath, recPath];
  if (wavPath) args.push("--wav", wavPath);
  const r = spawnSync(process.execPath, args, { encoding: "utf8" });
  try { fs.unlinkSync(recPath); } catch { /* tmp */ }
  return r;
}

send({ type: "load", wasm: wasmBytes.buffer, sectors: {}, master: 700, rec: true });
await waitFor("ready");
sec(1.5);
send({ type: "keys", mask: 1 << 7 });         // C4
sec(0.3);
send({ type: "keys", mask: 0 });
sec(0.1);
send({ type: "enc", role: 0, n: 1 });         // SELECT
sec(0.05);
send({ type: "enc", role: 0, n: 1 });
sec(0.05);
send({ type: "master", value: 500 });
sec(0.05);
send({ type: "midi", data: [0x90, 64, 100] });  // E4
sec(0.2);
send({ type: "midi", data: [0x80, 64, 0] });
sec(0.05);
send({ type: "buttons", mask: 1 << 10 });     // PLAY
sec(0.1);
send({ type: "buttons", mask: 0 });
sec(0.05);
send({ type: "mark", note: "here" });
sec(1.0);
send({ type: "rec-dump" });
const dump = await waitFor("rec");
const rec = pageRec(dump.data);
const workL = Float32Array.from(left.flatMap((c) => Array.from(c)));
const workR = Float32Array.from(right.flatMap((c) => Array.from(c)));

// ---- replay it
const repL = [], repR = [];
const out = await replay(wasmBytes.buffer, rec, (l, r) => { repL.push(Float32Array.from(l)); repR.push(Float32Array.from(r)); });

// ---- a) every input once, in order
const types = rec.events.filter((e) => e.type !== "mark").map((e) => e.type);
check("the recording logged every input once, in order",
      types.join() === ["keys", "keys", "enc", "enc", "master", "midi", "midi", "buttons", "buttons"].join() &&
      rec.events.some((e) => e.type === "mark" && e.note === "here"));

// ---- b) the key sounds
let peak = 0;
for (let i = 0; i < workL.length; i++) peak = Math.max(peak, Math.abs(workL[i]), Math.abs(workR[i]));
check("the output is not silent", peak > 0.01);

// ---- c) the replay's samples are the worklet's
const flat = (chunks) => Float32Array.from(chunks.flatMap((c) => Array.from(c)));
const gotL = flat(repL), gotR = flat(repR);
let diff = 0;
for (let i = 0; i < rec.frames && diff === 0; i++) if (workL[i] !== gotL[i] || workR[i] !== gotR[i]) diff = i + 1;
check("replay renders the browser's samples bit for bit",
      frames === rec.frames && gotL.length >= rec.frames && diff === 0);

// ---- d) the summary rows agree (seconds + partial, equal counts)
const rows = rowsOf(rec);
const bad = rows.findIndex((row, i) => !out.seconds[i] || out.seconds[i].sum !== row.sum);
check("the summary rows match", rows.length === out.seconds.length && bad === -1);

// ---- e) the CLI replays the page-shaped file (and --wav size)
const cli = runCli(rec);
check("replay.mjs exits 0 and reports a bit-for-bit match",
      cli.status === 0 && (cli.stdout || "").includes("replay matches the browser's audio bit for bit"));
const wavPath = path.join(os.tmpdir(), `araminta-rec-test-${process.pid}.wav`);
const cliWav = runCli(rec, wavPath);
const wavOk = cliWav.status === 0 && fs.existsSync(wavPath) && fs.statSync(wavPath).size === 44 + rec.frames * 4;
check("replay.mjs --wav writes 44 + frames*4 bytes", wavOk);
try { fs.unlinkSync(wavPath); } catch { /* tmp */ }

// ---- f) rec-stop freezes frames; a later dump still replays (partial flushed into seconds)
const stopF = frames;
send({ type: "rec-stop" });
sec(0.5);
posted.length = 0;
send({ type: "rec-dump" });
const dump2 = await waitFor("rec");
const rec2 = pageRec(dump2.data);
check("a dump after stop keeps frames at the stop", dump2.data.frames === stopF);
const out2 = await replay(wasmBytes.buffer, rec2);
const rows2 = rowsOf(rec2);
const bad2 = rows2.findIndex((row, i) => !out2.seconds[i] || out2.seconds[i].sum !== row.sum);
check("the stopped recording's rows match (partial flushed)",
      rows2.length === out2.seconds.length && bad2 === -1);
const cli2 = runCli(rec2);
check("replay.mjs matches the stopped recording",
      cli2.status === 0 && (cli2.stdout || "").includes("replay matches the browser's audio bit for bit"));

console.log(fails ? `rec: ${fails} FAILED` : "rec: all ok");
process.exit(fails ? 1 : 0);
