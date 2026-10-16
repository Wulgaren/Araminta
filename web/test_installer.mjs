// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The installer page (web/index_pkg.html) has no test of its own: web/test_web.mjs checks editor.html and
// fm1pkg.js, web/test_backup.mjs the archive code, and web/test_info_tags.mjs the INFO tags. This builds the page the
// way web/make_site.py does and checks what that assembly can get wrong: the inlined script parses, the two TEXT
// tables are symmetric and complete, the template markers are guarded, and the language plumbing is the same line
// in both pages. Run from the repo root:  node web/test_installer.mjs
import { fileURLToPath } from "node:url";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import vm from "node:vm";

const HERE = fileURLToPath(new URL(".", import.meta.url));
let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(64)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };
const nums = (s) => s.replace(/"[^"]*"/g, '""');

/* make_site.py's strip_module + the two markers */
const strip = (src) => src.replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
const lib = ["fm1pkg.js", "fm1ota.js", "fm1backup.js"].map((f) => strip(readFileSync(join(HERE, f), "utf8"))).join("\n");
const meta = JSON.stringify({ version: "9.99", product: "FM-1_999", pkg: "../../firmware/felucca-9.99.fwsc" });
const page = (f, libTxt, metaTxt) => readFileSync(join(HERE, f), "utf8")
  .replace("/*LIB*/", () => libTxt).replace("/*META*/", () => metaTxt);

const built = page("index_pkg.html", lib, meta);
ok(!built.includes("/*LIB*/") && !built.includes("/*META*/"), "installer: both template markers were substituted");
const script = built.slice(built.indexOf('<script type="module">') + 23, built.lastIndexOf("</script>"));
let err = null;
try { new vm.Script(script); } catch (e) { err = e.message; }
ok(!err, "installer: the inlined page script compiles" + (err ? ` (${err})` : ""));

/* the template as it ships: the markers stay comments, and the page says so instead of throwing */
const raw = readFileSync(join(HERE, "index_pkg.html"), "utf8");
ok(raw.includes("if (!meta || !meta.pkg)") && raw.includes("if (!crypto || !crypto.subtle)"),
   "installer: the un-substituted page and an insecure context both give a message");
ok(nums(raw).includes("const meta = /*META*/;") && raw.includes('sayK("template")') && raw.includes('sayK("nosecure")'),
   "installer: the template / secure-context keys are used and defined");
const keys = (s, from, to) => new Set(s.slice(from, to).match(/^\s{4}(\w+):/gm).map((x) => x.trim().replace(":", "")));
const tables = (s) => { const j = s.indexOf("ja: {"), k = s.indexOf("en: {"), e = s.indexOf("\n};", k); return [keys(s, j, k), keys(s, k, e)]; };
for (const [name, src] of [["installer", raw], ["editor", readFileSync(join(HERE, "editor.html"), "utf8")]]) {
  const [ja, en] = tables(src);
  ok(ja.size > 25 && [...ja].every((k) => en.has(k)) && [...en].every((k) => ja.has(k)),
     `installer: ${name} TEXT is symmetric (${ja.size} keys)`);
}

/* the language plumbing: the same detection and lookup in both pages (each page keeps its own TEXT) */
const line = (s, start) => s.slice(s.indexOf(start, s.indexOf("const TEXT")), s.indexOf("\n", s.indexOf(start, s.indexOf("const TEXT")))).trim();
const detect = (s) => { const i = s.indexOf("let lang ="); return s.slice(i, s.indexOf("\n", i)).trim(); };
const lookup = (s) => { const i = s.indexOf("const t = (k)"); return s.slice(i, s.indexOf("\n", i)).trim(); };
ok(detect(raw) === detect(readFileSync(join(HERE, "editor.html"), "utf8")), "installer: lang detection is the same line as the editor's");
ok(nums(lookup(raw)) === nums(lookup(readFileSync(join(HERE, "editor.html"), "utf8"))),
   "installer: t() falls back to the key like the editor's");
ok(raw.includes("const sayK = (k, arg = \"\")") && raw.includes("const say = (txt) =>"),
   "installer: sayK / say match the editor's names");

console.log(fails ? `INSTALLER TESTS FAILED (${fails})` : "installer tests passed");
process.exit(fails ? 1 : 0);
