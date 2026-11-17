// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wulgaren
// The audio health the session recorder keeps per render quantum: a rolling hash of the samples' bit patterns, their
// peak, and the largest sample-to-sample step. A click is a step far larger than any in the last REC_RECENT quanta: a
// loud saw's reset steps every cycle, so a fixed threshold alone would flag every bass note. web/emu/worklet.js folds
// each quantum it renders in; web/emu/replay.mjs folds the same quanta in from the saved recording, so the two compute
// identical numbers.
// A module for the test and the replay (web/emu/rec_test.mjs, web/emu/replay.mjs). web/emu/build.sh puts this file in
// front of web/emu/worklet.js with the leading "export " stripped, because the browser loads the worklet with
// AudioWorklet.addModule, which compiles a classic script: there a top-level "export" is a SyntaxError and the emulator
// would never boot. Keep the two halves in sync through that one source.

export const REC_BUCKET = 344;   // quanta per summary row, ~1 s at 44.1 kHz
export const REC_JUMP = 0.2;     // a click's step is larger than this (full scale 1.0) ...
export const REC_JUMP_X = 4;     // ... and than REC_JUMP_X times the largest step of the last REC_RECENT quanta
export const REC_RECENT = 8;     // ~23 ms: longer than one cycle of a 44 Hz bass

// a bucket's running state; pl / pr are the previous quantum's last samples, so a step across a quantum boundary
// counts; recent holds the last REC_RECENT quanta's largest steps; qp is the last quantum's own peak
export function recAudio() {
  return { sum: 0, peak: 0, jump: 0, pl: 0, pr: 0, recent: [], click: false, qp: 0 };
}

// fold one rendered quantum (l / r in full scale) into a; returns the quantum's largest sample-to-sample step and
// sets a.click when that step is a click. The hash takes the samples' 32-bit patterns, so a sign or a low bit that a
// rounded 16-bit value loses still counts; every term is non-negative and the products stay below 2^53.
export function recQuantum(a, l, r) {
  const lu = new Uint32Array(l.buffer, l.byteOffset, l.length), ru = new Uint32Array(r.buffer, r.byteOffset, r.length);
  let step = 0, peak = 0, pl = a.pl, pr = a.pr;
  for (let i = 0; i < l.length; i++) {
    a.sum = (a.sum * 31 + lu[i] + 7 * ru[i]) % 1000000007;
    peak = Math.max(peak, Math.abs(l[i]), Math.abs(r[i]));
    step = Math.max(step, Math.abs(l[i] - pl), Math.abs(r[i] - pr));
    pl = l[i];
    pr = r[i];
  }
  a.peak = Math.max(a.peak, peak);
  a.qp = peak;
  a.pl = pl;
  a.pr = pr;
  a.jump = Math.max(a.jump, step);
  a.click = step > REC_JUMP && step > REC_JUMP_X * Math.max(0, ...a.recent);
  a.recent.push(step);
  if (a.recent.length > REC_RECENT) a.recent.shift();
  return step;
}
