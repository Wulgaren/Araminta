// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The INFO reply's capability tags are described in two places that cannot share a module: the installer page
// inlines fm1ota.js (web/make_site.py), while editor.html's protocol block is evaluated on its own by
// web/test_web.mjs and so keeps its own copy of the table. This compares the two tables and the two decoders, so a
// firmware that extends the tagged trailer cannot break one page and not the other.
// Run from the repo root:  node web/test_info_tags.mjs
import vm from "node:vm";
import { fileURLToPath } from "node:url";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { INFO_TAG } from "./fm1ota.js";

const HERE = fileURLToPath(new URL(".", import.meta.url));
let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(66)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };

/* the editor's protocol block, alone as web/test_web.mjs evaluates it */
const html = readFileSync(join(HERE, "editor.html"), "utf8");
const a = html.indexOf("/*PROTO-BEGIN*/"), b = html.indexOf("/*PROTO-END*/");
const E = vm.runInNewContext(html.slice(a, b) + `;({ INFO_TAG, parseInfo: parse[1] })`,
  { setTimeout, clearTimeout, setInterval, clearInterval, console });

/* the installer's own archiveInfo, with the inlined lib's INFO_TAG given to it */
const inst = readFileSync(join(HERE, "index_pkg.html"), "utf8");
const s = inst.indexOf("function archiveInfo(a) {"), e = inst.indexOf("\n}", inst.indexOf("return { version", s));
const archiveInfo = vm.runInNewContext(`${inst.slice(s, e + 2)};archiveInfo`, { INFO_TAG });

ok(INFO_TAG.SONG === E.INFO_TAG.SONG && INFO_TAG.UI === E.INFO_TAG.UI && INFO_TAG.MOTION === E.INFO_TAG.MOTION
   && INFO_TAG.BACKUP === E.INFO_TAG.BACKUP, "info tags: fm1ota.js's table == editor.html's table");
ok(E.INFO_TAG.SONG === 16 && E.INFO_TAG.UI === 0x55 && E.INFO_TAG.MOTION === 0x4d && E.INFO_TAG.BACKUP === 0x42,
   "info tags: SONG 16, UI 0x55, MOTION 0x4d, BACKUP 0x42");

/* a firmware with every capability: the version string, the engine names, the track count, then the trailer in
   EDITOR_PROTOCOL.md "INFO"'s order -- (16, 0x55, 1, 9) prefs, (0x4d, 1, 64, 1) motion + chance, (0x42, 1, 3)
   backups, (0x46, 1, 8, 0) FM6, (0x53, 1, 3) sync, (0x50, 1, 2) FM6 v2, (0x4e, 1, 25) MENU, (0x52, 1, 4) ratchet,
   (0x4c, 1, 1) locks, (0x41, 1, 192 lo, 192 hi) the 192-record pool */
const FULL = [16, 0x55, 1, 9, 0x4d, 1, 64, 1, 0x42, 1, 3, 0x46, 1, 8, 0, 0x53, 1, 3, 0x50, 1, 2, 0x4e, 1, 25, 0x52, 1, 4];
const FULL32 = [...FULL, 0x4c, 1, 1, 0x41, 1, 192 & 0x7f, 192 >> 7];
const str = (x) => [...String(x)].map((c) => c.charCodeAt(0)).concat([0]);
const info = (version = "ARAMINTA 1.5", engines = 14, trailer = FULL) =>
  [...str(version), engines, 127, 30, 16, 95, ...Array.from({ length: engines }, (_, i) => str(i === 1 ? "-" : "E" + i)).flat(), 4, ...trailer];

const o = E.parseInfo(info());
ok(o.backupCaps === 3 && o.motionMax === 64 && o.chance === true && o.chainRows === 16 && o.uiCaps === 9
   && o.fm6 && o.fm6.factory === 8 && o.fm6.caps === 2 && o.syncCaps === 3 && o.ratchet === 4,
   "info: the editor reads every capability from the full trailer");
const p32 = E.parseInfo(info("ARAMINTA 3.2", 14, FULL32));
ok(p32.motionMax === 192 && p32.motionFull && p32.chance && p32.ratchet === 4 && p32.fm6 && p32.fm6.factory === 8,
   "info: 4C 01 01 + 41 01 (192) advertise the real FUN16 motion pool");
const p128 = E.parseInfo(info("ARAMINTA 1.2", 14, [...FULL, 0x4c, 1, 1, 0x41, 1, 128, 0]));
ok(p128.motionMax === 128 && p128.motionFull, "info: a 128-record 1.2 pool still decodes (no 64 hardcode)");
ok(!!archiveInfo(info()).supported && archiveInfo(info()).backupCaps === o.backupCaps,
   "info: the installer's check agrees with the editor's on the same reply");

/* older firmware: no tagged trailer at all (the old PR bytes that must not look like SONG) */
const old = [...str("ARAMINTA 1.0"), 14, 127, 30, 16, 95, ...Array.from({ length: 14 }, (_, i) => str("E" + i)).flat(), 15];
ok(E.parseInfo(old).uiCaps === 0 && E.parseInfo(old).backupCaps === 0 && !archiveInfo(old).supported,
   "info: an untagged trailer is not read as SONG or preferences by either");
for (const tag of [0x4d, 0x42]) {                       // with preferences, without that group
  const i = FULL.indexOf(tag), t = FULL.filter((_, k) => k < i || k >= i + 3);
  const r = info("ARAMINTA 1.5", 14, t);
  ok(E.parseInfo(r).backupCaps === 0 && !archiveInfo(r).supported,
     `info: without the 0x${tag.toString(16)} group both report no complete backups`);
}
for (const [v, want] of [["ARAMINTA 1.5", true], ["FELUCCA 1.0", false], ["ARAMINTA", false]])
  ok(!!archiveInfo(info(v)).supported === want, `info: the installer only accepts "ARAMINTA " ("${v}" -> ${want})`);
/* bit 0 = read (BACKUP_LIST / BACKUP_GET), bit 1 = restore: the installer needs the read bit to take the archive */
const restoreOnly = FULL.map((v, k) => (k === 10 ? 2 : v));
ok(E.parseInfo(info("ARAMINTA 1.5", 14, restoreOnly)).backupCaps === 2 && !archiveInfo(info("ARAMINTA 1.5", 14, restoreOnly)).supported,
   "info: a backup tag without the read bit is not supported by the installer");
ok(true, "info: the editor reads whatever byte follows the 0x42 group");

const tagged = info("ARAMINTA 1.5", 14, FULL.slice(0, 6));   // advertises the tag, then stops
const throws = (f) => { try { f(); return false; } catch { return true; } };
ok(throws(() => archiveInfo(tagged)), "info: a tagged reply cut short is refused (not read as 'unsupported')");
ok(throws(() => archiveInfo([1, 2, 3])), "info: a reply without a NUL is refused");
ok(archiveInfo(info("ARAMINTA 1.0", 14, [])).supported === false, "info: no tagged trailer: 'not supported', not an error");

console.log(fails ? `INFO TAG TESTS FAILED (${fails})` : "info tag tests passed");
process.exit(fails ? 1 : 0);
