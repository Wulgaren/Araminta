// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The two CRC-32s in web/ are one checksum: fm1backup.js's bkCrc (the backup archives, the project files) and
// editor.html's crc32 (an FSMP sample header). They cannot share a module -- web/test_web.mjs evaluates the
// editor's protocol block on its own, so it has no import -- so this compares them, and would fail if either
// drifted. Run from the repo root:  node web/test_crc.mjs
import vm from "node:vm";
import { fileURLToPath } from "node:url";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { bkCrc } from "./fm1backup.js";

const HERE = fileURLToPath(new URL(".", import.meta.url));
const html = readFileSync(join(HERE, "editor.html"), "utf8");
const proto = html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/"));
const crc32 = vm.runInNewContext(proto + ";crc32", {});

let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(64)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };

ok(bkCrc(new TextEncoder().encode("123456789")) === 0xcbf43926, "crc: the editor's and the backup's known vector 0xcbf43926");
ok(crc32(new TextEncoder().encode("123456789")) === 0xcbf43926, "crc: editor.html crc32 agrees");
ok(bkCrc(new Uint8Array(0)) === 0 && crc32(new Uint8Array(0)) === 0, "crc: an empty buffer is 0");
for (const n of [1, 2, 3, 7, 31, 255, 256, 4095, 4096, 8191, 65535]) {
  const b = Uint8Array.from({ length: n }, (_, i) => (i * 131 + (i >> 3)) & 255);
  if (bkCrc(b) !== crc32(b)) { fails++; console.log(`  crc: differs at ${n} bytes`); }
}
ok(true, "crc: bkCrc == crc32 over 11 lengths (0..65535)");

console.log(fails ? `CRC WEB TESTS FAILED (${fails})` : "crc web tests passed");
process.exit(fails ? 1 : 0);
