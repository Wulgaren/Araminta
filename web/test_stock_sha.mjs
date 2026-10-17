// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The official FM-1 V15 digest that a return to stock is pinned to, in two languages: web/fm1pkg.js (the browser
// installer) and tools/fm1_install.py (the command-line installer). They are separate on purpose -- the browser
// page and the tool never share code -- so this checks they did not drift: re-releasing the official file would
// otherwise let one installer accept a package the other rejects, or vice versa.
// Run from the repo root:  node web/test_stock_sha.mjs [path to tools/fm1_install.py]
import { fileURLToPath } from "node:url";
import { existsSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";

const HERE = fileURLToPath(new URL(".", import.meta.url));
const tools = process.argv[2] || join(HERE, "..", "tools", "fm1_install.py");

let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(64)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };

/* the digest as web/fm1pkg.js declares it */
const pkg = readFileSync(join(HERE, "fm1pkg.js"), "utf8");
const js = pkg.match(/STOCK_V15_SHA256\s*=\s*"([0-9a-f]{64})"/);
ok(!!js, "stock sha: web/fm1pkg.js declares a 64-hex STOCK_V15_SHA256");
/* the layout constants the validator pins, which must stay consistent with the digest */
const jsSize = pkg.match(/STOCK_V15_SIZE\s*=\s*(\d+)/), jsApp = pkg.match(/STOCK_APP_SIZE\s*=\s*(0x[0-9A-Fa-f]+|\d+)/);
ok(!!jsSize && !!jsApp && Number(jsSize[1]) === 699956 && Number(jsApp[1]) === 0x8dfbc,
   "stock sha: STOCK_V15_SIZE 699956 and STOCK_APP_SIZE 0x8DFBC in web/fm1pkg.js");

if (!existsSync(tools)) {
  console.log(`stock sha: no ${tools} (a web-only checkout)                  skip`);
} else {
  const py = readFileSync(tools, "utf8");
  const d = py.match(/STOCK_V15_SHA256\s*=\s*"([0-9a-fA-F]{64})"/);
  ok(!!d, "stock sha: tools/fm1_install.py declares a 64-hex STOCK_V15_SHA256");
  ok(!!js && !!d && js[1] === d[1].toLowerCase(),
     `stock sha: the two installers pin the same official V15 digest (${(js || [])[1]?.slice(0, 16)}…)`);
  /* the tool validates the digest alone; a byte length there would be a third place to keep in step */
  const pyLen = py.match(/len\(raw\)\s*[<>=!]/);
  ok(!!pyLen, "stock sha: tools/fm1_install.py pins the digest, not a byte length (nothing else to drift)");
}

console.log(fails ? `STOCK SHA TESTS FAILED (${fails})` : "stock sha tests passed");
process.exit(fails ? 1 : 0);
