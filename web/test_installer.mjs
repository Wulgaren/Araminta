// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The installer page (web/index_pkg.html) has no test of its own: web/test_web.mjs checks editor.html and
// fm1pkg.js, web/test_backup.mjs the archive code, and web/test_info_tags.mjs the INFO tags. This builds the page the
// way web/make_site.py does and checks what that assembly can get wrong: the inlined script parses, the two TEXT
// tables are symmetric and complete, the template markers are guarded, and the language plumbing is the same line
// in both pages. With build/felucca.fwsc it also runs the page's package validation: the pinned SHA-256, the full
// Package parse, and the identity check all run before the image exists. Run from the repo root:
//   node web/test_installer.mjs
import { execFileSync } from "node:child_process";
import { createHash, webcrypto } from "node:crypto";
import { existsSync, mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { fileURLToPath } from "node:url";
import { join } from "node:path";
import vm from "node:vm";

const HERE = fileURLToPath(new URL(".", import.meta.url));
let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(64)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };
const nums = (s) => s.replace(/"[^"]*"/g, '""');

/* make_site.py's strip_module + the markers; this list is the composition make_site.py inlines */
const LIBS = ["fm1pkg.js", "fm1ota.js", "projconv.js", "fm1backup.js"];
const strip = (src) => src.replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
const lib = LIBS.map((f) => strip(readFileSync(join(HERE, f), "utf8"))).join("\n");
const meta = JSON.stringify({ version: "9.99", product: "FM-1_999", pkg: "../../firmware/felucca-9.99.fwsc" });
const page = (f, libTxt, metaTxt) => readFileSync(join(HERE, f), "utf8")
  .replace("/*LIB*/", () => libTxt).replace("/*META*/", () => metaTxt);

const built = page("index_pkg.html", lib, meta);
ok(!built.includes("/*LIB*/") && !built.includes("/*META*/"), "installer: both template markers were substituted");
const script = built.slice(built.indexOf('<script type="module">') + 23, built.lastIndexOf("</script>"));
let err = null;
try { new vm.Script(script); } catch (e) { err = e.message; }
ok(!err, "installer: the inlined page script compiles" + (err ? ` (${err})` : ""));

/* the bundle composition is make_site.py's: an omission here is how the projconv duplicate-declaration bug shipped */
const makeSite = readFileSync(join(HERE, "make_site.py"), "utf8");
const inlined = [...makeSite.matchAll(/strip_module\(\(HERE \/ "([^"]+)"\)/g)].map((m) => m[1]);
ok(inlined.join(",") === LIBS.join(","), `installer: make_site.py inlines ${LIBS.join(" + ")}`);

/* the template as it ships: the markers stay comments, and the page says so instead of throwing */
const raw = readFileSync(join(HERE, "index_pkg.html"), "utf8");
ok(raw.includes("if (!meta || !meta.pkg || !meta.product || !meta.sha256)") && raw.includes("if (!crypto || !crypto.subtle)"),
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

/* ------------------------------------------------------------- package integrity --- */
/* The page's own verifyPackage, sliced out of the assembled bundle and run in a vm: a good package verifies,
   and truncated, altered or wrongly-identified ones are rejected before logicalImage runs. */
const reject = async (f) => { try { await f(); return null; } catch (e) { return e; } };
const sha = (bytes) => createHash("sha256").update(bytes).digest("hex");
const fwsc = join(HERE, "..", "build", "felucca.fwsc");
if (!existsSync(fwsc)) {
  console.log("installer: no build/felucca.fwsc, package integrity          skip");
} else {
  const out = mkdtempSync(join(tmpdir(), "araminta-site-"));
  execFileSync("python3", [join(HERE, "make_site.py"), fwsc, "9.99", out], { stdio: "pipe" });
  const site = readFileSync(join(out, "webapp", "installer", "index.html"), "utf8");
  const siteScript = site.slice(site.indexOf('<script type="module">') + 23, site.lastIndexOf("</script>"));
  const siteMeta = JSON.parse(/const meta = (\{[^\n]*\});/.exec(siteScript)[1]);
  const good = new Uint8Array(readFileSync(fwsc));
  ok(siteMeta.sha256 === sha(good), "installer: make_site.py pins the package SHA-256 in /*META*/");

  const verifySrc = script.slice(script.indexOf("/*VERIFY-BEGIN*/"), script.indexOf("/*VERIFY-END*/"));
  const V = vm.runInNewContext(lib + "\n" + verifySrc
    + "\nlet __images = 0; const __logical = logicalImage;"
    + " logicalImage = (b) => { __images++; return __logical(b); };"
    + "\n;({ verifyPackage, images: () => __images, reset: () => { __images = 0; } })",
    { crypto: webcrypto, console }, {});
  const rejectedWith = async (bytes, m) => {
    V.reset();
    const e = await reject(() => V.verifyPackage(bytes, m));
    return !!e && V.images() === 0 ? e.code : null;
  };

  V.reset();
  const v = await V.verifyPackage(good, siteMeta);
  ok(v.pkg.product === siteMeta.product && v.image.length === good.length - 20 && V.images() === 1,
     "installer: a good package verifies and derives the image once");

  const cut = good.subarray(0, 100000);
  ok(await rejectedWith(cut, siteMeta) === "badsha", "installer: a truncated package is rejected by its digest");

  const flipped = good.slice();
  flipped[flipped.length >> 1] ^= 0x01;
  ok(await rejectedWith(flipped, siteMeta) === "badsha", "installer: one altered byte is rejected by its digest");

  /* the digest is updated so only the Package parse can catch it: a truncated payload with valid identity bytes */
  const cutMeta = { ...siteMeta, sha256: sha(cut) };
  ok(await rejectedWith(cut, cutMeta) === "badpkg", "installer: a truncated package is rejected by the Package parse");

  /* a package whose first 960 bytes still name FM-1_9xx but whose product differs from the page's */
  const wrong = good.slice();
  wrong[0x2F] = 0x42;                          /* the first identity marker: product starts with "A" */
  const wrongMeta = { ...siteMeta, sha256: sha(wrong) };
  ok(await rejectedWith(wrong, wrongMeta) === "badpkg", "installer: a wrong identity is rejected after a valid parse");
}

console.log(fails ? `INSTALLER TESTS FAILED (${fails})` : "installer tests passed");
process.exit(fails ? 1 : 0);
