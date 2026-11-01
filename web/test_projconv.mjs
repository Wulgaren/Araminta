// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// web/projconv.js: FUN1..FUN6 -> FUN19, and a FUN19 project with a DIGITAL track -> FM6, byte for byte
// against the old C firmware (037b07b). Run from the repo root:
//   node web/test_projconv.mjs
// The fixtures in web/testdata/projconv.json were made with tests/project_test.c's images and
// /tmp build of the old firmware's proj_import + proj_pack (the DIGITAL one also with fm4_convert.c).
// projconv_vib.json holds FUN8 / FUN10 / FUN16 / FUN18 images with a DIGITAL track and pre-VIB FM6 tracks
// (E5 set, VIB bit clear) and their proj_import + proj_pack outputs (proj_fm4 then proj_fm6m).

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import vm from "node:vm";
import { convertProject, needsConversion, projectHash } from "./projconv.js";
import { bkCrc, readProject } from "./fm1backup.js";

const HERE = fileURLToPath(new URL(".", import.meta.url));
const FIX = JSON.parse(readFileSync(new URL("./testdata/projconv.json", import.meta.url), "utf8"));
const html = readFileSync(new URL("./editor.html", import.meta.url), "utf8");
const proto = html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/"));
const E = vm.runInNewContext(proto + ";({FM4, FM6})", { setTimeout, clearTimeout, setInterval, clearInterval, console });
const digital = (p, pe0) => {
  const r = E.FM4.convert(p.slice(), pe0);
  return { p: r.p, fm6: E.FM6.pack(r.voice), preset: r.preset };
};

let fails = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(72)} ${cond ? "ok" : "FAIL"}`); if (!cond) fails++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const b64 = (s) => Uint8Array.from(Buffer.from(s, "base64"));

for (let f = 1; f <= 6; f++) {
  const input = b64(FIX.old[f].input), expected = b64(FIX.old[f].expected);
  ok(needsConversion(input), `projconv: FUN${f} needs conversion`);
  const out = convertProject(input);
  ok(out.length === 3840 && out[0] === 0x46 && out[1] === 0x55 && out[2] === 0x4e && out[3] === 0x4a,
    `projconv: FUN${f} -> FUN19 size and magic`);
  ok(eq(out, expected), `projconv: FUN${f} -> FUN19 byte for byte == the C reader`);
  const stored = (out[3836] | out[3837] << 8 | out[3838] << 16 | out[3839] << 24) >>> 0;
  ok(projectHash(out, 3836) === stored, `projconv: FUN${f} -> FUN19 checksum`);
}

for (const f of [7, 8, 9]) {
  const input = b64(FIX.legacy[f].input), expected = b64(FIX.legacy[f].expected);
  ok(needsConversion(input) === false, `projconv: FUN${f} without DIGITAL needs no conversion`);
  ok(eq(convertProject(input), expected), `projconv: FUN${f} -> FUN19 byte for byte == the C reader`);
}

{
  const input = b64(FIX.digital.input), expected = b64(FIX.digital.expected);
  ok(needsConversion(input), "projconv: a FUN19 with a DIGITAL track needs conversion");
  const out = convertProject(input, digital);
  ok(eq(out, expected), "projconv: DIGITAL -> FM6 + patch byte for byte == the C converter");
  const engine = 68 + input[66] + (input[66] > 111 ? 1 : 0);
  ok(out[engine] === 12, "projconv: DIGITAL track engine byte is FM6 (12)");
  const patch = 3312;
  let only = true;
  for (let i = 0; i < 3840; i++) {
    // DIGITAL's whole parameter block (the FM4 values become FM6's), engine, preset, patch and hash.
    if ((i >= 68 && i < engine + 2) || (i >= patch && i < patch + 512) || i >= 3836) continue;
    if (out[i] !== input[i]) { only = false; break; }
  }
  ok(only, "projconv: only DIGITAL's parameters, engine, patch and checksum change");
  ok(!eq(out.subarray(patch, patch + 128), input.subarray(patch, patch + 128)),
    "projconv: the converted FM6 patch replaces the stored one");
}

{
  /* a current FUN19 project without DIGITAL: no conversion, and a decode/repack is a fixed point */
  const input = b64(FIX.current);
  ok(!needsConversion(input), "projconv: a current FUN19 project needs no conversion");
  ok(eq(convertProject(input), input), "projconv: FUN19 round trips byte for byte");
}

{
  /* a FUN8 / FUN10 / FUN16 / FUN18 project with BOTH a DIGITAL track and pre-VIB FM6 tracks (E5 set,
   * VIB bit clear): the results are the old C firmware's proj_import + proj_pack (037b07b), which runs
   * proj_fm4 then proj_fm6m. Track 1 is DIGITAL, 2 and 3 are FM6 (VMOD 5 and -3), 4 is DRUM. */
  const VIB = JSON.parse(readFileSync(new URL("./testdata/projconv_vib.json", import.meta.url), "utf8"));
  const CAR = [40, 40, 36, 36, 42, 42, 40, 40, 40, 36, 36, 40, 40, 40, 40, 32,
    32, 32, 38, 52, 54, 46, 54, 62, 62, 52, 52, 41, 58, 57, 62, 63];
  const P_E0 = 119, P_E5 = 124, P_COUNT = 127, NSTEP = 64;
  const tracksOf = (bytes) => {
    const np = bytes[66], iord2 = np > 111 ? 1 : 0, stride = np + iord2 + 2 + NSTEP * 9;
    const out = [];
    for (let t = 0; t < 4; t++) {
      let pos = 68 + t * stride;
      const p = new Array(P_COUNT).fill(null);
      for (let i = 0; i < np; i++) {
        let v;
        if (iord2 && i === 96) { v = bytes[pos] | bytes[pos + 1] << 8; pos += 2; }
        else v = bytes[pos++] - 64;
        p[np < P_COUNT && i >= np - 8 ? P_E0 + i - (np - 8) : i] = v;
      }
      out.push({ p, engine: bytes[pos], preset: bytes[pos + 1], steps: bytes.subarray(pos + 2, pos + 2 + 576) });
    }
    return out;
  };
  const motionOf = (bytes) => {
    const n = bytes[2928];
    return Array.from({ length: n }, (_, i) => [bytes[2932 + i * 3], bytes[2933 + i * 3] & 127, bytes[2934 + i * 3]]);
  };
  const patchSameExceptKvs = (inPk, outPk, alg, d) => {
    const car = CAR[alg & 31];
    for (let op = 0; op < 6; op++) {
      const kvsIn = (inPk[op * 17 + 13] >> 2) & 7;
      const want = (car >> op) & 1 ? kvsIn : Math.max(0, Math.min(7, kvsIn + d));
      if (((outPk[op * 17 + 13] >> 2) & 7) !== want) return false;
    }
    for (let i = 0; i < 128; i++) {
      const modKvs = [...Array(6).keys()].some((op) => i === op * 17 + 13 && !((car >> op) & 1));
      if (!modKvs && (outPk[i] & 0x7f) !== (inPk[i] & 0x7f)) return false;
    }
    return true;
  };

  for (const f of [8, 10, 16, 18]) {
    const input = b64(VIB[f].input), expected = b64(VIB[f].expected);
    ok(needsConversion(input), `projconv: FUN${f} with DIGITAL needs conversion`);
    const out = convertProject(input, digital);
    ok(eq(out, expected), `projconv: FUN${f} with DIGITAL + pre-VIB FM6 -> FUN19 == the C firmware`);
    ok((out[67] & 4) !== 0, `projconv: FUN${f}: VIB bit set after the migration`);
    const it = tracksOf(input), ot = tracksOf(out);
    for (const t of [1, 2]) {
      let onlyE5 = true;
      for (let i = 0; i < P_COUNT; i++) {
        if (i !== P_E5 && it[t].p[i] !== null && it[t].p[i] !== ot[t].p[i]) onlyE5 = false;
      }
      ok(onlyE5 && ot[t].p[P_E5] === 0 && it[t].p[P_E5] !== 0,
        `projconv: FUN${f}: FM6 track ${t + 1} keeps every parameter but E5 (cleared)`);
    }
    let drumSame = ot[3].engine === 10;
    for (let i = 0; i < P_COUNT; i++) {
      if (it[3].p[i] !== null && it[3].p[i] !== ot[3].p[i]) drumSame = false;
    }
    ok(drumSame, `projconv: FUN${f}: the DRUM track is untouched`);
    const patchIn = input.length === 3584 ? 3056 : 3312, patchOut = 3312;
    const pk = (b, o, t) => b.subarray(o + t * 128, o + t * 128 + 128);
    ok(patchSameExceptKvs(pk(input, patchIn, 1), pk(out, patchOut, 1), 0, 5),
      `projconv: FUN${f}: VMOD 5 moves into the modulators' velocity sensitivity`);
    ok(patchSameExceptKvs(pk(input, patchIn, 2), pk(out, patchOut, 2), 5, -3),
      `projconv: FUN${f}: VMOD -3 clamps into the modulators, carriers and the rest unchanged`);
    ok(eq(pk(input, patchIn, 3), pk(out, patchOut, 3)) ||
      eq(pk(input, patchIn, 3).map((x) => x & 0x7f), pk(out, patchOut, 3).map((x) => x & 0x7f)),
      `projconv: FUN${f}: the DRUM track's patch is untouched`);
    const mv = motionOf(out);
    ok(mv.length === 2 && mv[0][0] === 68 && mv[0][1] === 123 && mv[0][2] === 9 &&
      mv[1][0] === 208 && mv[1][1] === 124 && mv[1][2] === 2,
      `projconv: FUN${f}: E5 motion dropped on FM6 (and DIGITAL) tracks, others kept`);
    ok(eq(convertProject(out), out), `projconv: FUN${f}: the converted project is a fixed point`);
  }
}

{
  /* Malformed projects are refused, as project.c proj_unpack / proj_pack and the chain / motion validators
   * refuse them: a value that C rejects must not be clamped into a plausible one. Each case starts from a
   * fixture the C reader accepted, changes one field and fixes the hash, so only the field is at fault. */
  const rehash = (b) => {
    const sum = projectHash(b, b.length - 4);
    b[b.length - 4] = sum & 255; b[b.length - 3] = (sum >> 8) & 255;
    b[b.length - 2] = (sum >> 16) & 255; b[b.length - 1] = (sum >>> 24) & 255;
    return b;
  };
  const crafted = (input, fn) => { const b = input.slice(); fn(b); return rehash(b); };
  const VIBF = JSON.parse(readFileSync(new URL("./testdata/projconv_vib.json", import.meta.url), "utf8"));
  const refuses = (bytes, why) => {
    let out, err;
    try { out = convertProject(bytes, digital); } catch (e) { err = e; }
    ok(!out && err instanceof Error && /^Invalid project/.test(err.message),
      `projconv refuses ${why}${err ? `: ${err.message}` : ""}`);
  };

  const fun1 = b64(FIX.old[1].input);
  refuses(crafted(fun1, (b) => { b[168] = 200; }), "a FUN1 engine >= NENGINES");

  const fun6 = b64(FIX.old[6].input);
  refuses(crafted(fun6, (b) => { b[3346] = 17; }), "a FUN6 chain count > 16");
  refuses(crafted(fun6, (b) => { b[3346] = 1; b[3350] = 4; }), "a FUN6 chain slot >= 4");
  refuses(crafted(fun6, (b) => { b[3346] = 1; b[3351] = 0; }), "a FUN6 chain repeat 0");
  refuses(crafted(fun6, (b) => { b[3346] = 1; b[3351] = 17; }), "a FUN6 chain repeat > 16");

  const fun7 = b64(FIX.legacy[7].input);
  const mot7 = 2780, step7 = 68 + 91 + 2;       // FUN7: np 91, no insert-order byte
  const chain7 = 2744;                           // FUN7: 4 + 16 x {slot, repeat}
  refuses(crafted(fun7, (b) => { b[chain7] = 1; b[chain7 + 4] = 9; b[chain7 + 5] = 200; }),
    "a FUN7 chain row {slot 9, repeat 200}");
  refuses(crafted(fun7, (b) => { b[mot7] = 65; }), "a FUN7 motion count > 64");
  refuses(crafted(fun7, (b) => { b[mot7] = 1; b[mot7 + 6] = 200; }), "a FUN7 motion value > 127");
  refuses(crafted(fun7, (b) => { b[mot7] = 1; b[mot7 + 5] = 49; }), "a FUN7 MFX motion id");
  refuses(crafted(fun7, (b) => { b[mot7] = 1; b[mot7 + 5] = 100; }), "a FUN7 motion id past np");
  refuses(crafted(fun7, (b) => { b[step7] = 200; }), "a FUN7 note byte > 127");
  refuses(crafted(fun7, (b) => { b[step7 + 6] = 0; b[step7 + 7] = 1; }), "a FUN7 accent outside hit");

  const fun10 = b64(VIBF["10"].input);
  refuses(crafted(fun10, (b) => { b[2981] = 49; }), "a FUN10 MFX motion id");

  const cur = b64(FIX.current);
  refuses(crafted(cur, (b) => { b[3216] = 12; }), "a DRUM-X kit mode");
  refuses(crafted(cur, (b) => { b[3217] = 200; }), "a DRUM-X kit value > 127");
  refuses(crafted(cur, (b) => { b[3400] |= 0x80; b[3402] |= 0x80; }), "a MASTER order past NTRK");

  const fun16 = b64(VIBF["16"].input);
  refuses(crafted(fun16, (b) => { b[3319] |= 0x80; }), "a GENERATION density > 100");
}

{
  /* the fm1backup import path converts and reports it */
  const raw = b64(FIX.old[2].input);
  const file = { format: "felucca-project", version: 1, slot: 1, size: raw.length, crc: bkCrc(raw),
    data: Buffer.from(raw).toString("base64") };
  const p = readProject(file);
  ok(p.converted && p.size === 3840 && p.bytes.length === 3840 && p.crc === bkCrc(p.bytes),
    "projconv: readProject converts an old project and updates size / crc");
  ok(eq(p.bytes, b64(FIX.old[2].expected)), "projconv: readProject's bytes == the C conversion");
  const again = readProject(p);
  ok(again.converted === false && eq(again.bytes, p.bytes), "projconv: reading the converted project again is a no-op");
}

console.log(fails ? `\n${fails} FAIL` : "\nall ok");
process.exit(fails ? 1 : 0);
