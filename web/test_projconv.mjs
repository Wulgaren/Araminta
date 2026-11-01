// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// web/projconv.js: FUN1..FUN6 -> FUN19, and a FUN19 project with a DIGITAL track -> FM6, byte for byte
// against the old C firmware (037b07b). Run from the repo root:
//   node web/test_projconv.mjs
// The fixtures in web/testdata/projconv.json were made with tests/project_test.c's images and
// /tmp build of the old firmware's proj_import + proj_pack (the DIGITAL one also with fm4_convert.c).

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
