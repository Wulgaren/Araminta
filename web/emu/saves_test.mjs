// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// web/emu/saves.js: the copies that protect the live emulator's saves while a preview shares its store.
//   node web/emu/saves_test.mjs
import {
  STORE, AUTO, MANUAL, WRITER, readSnapshot, takeSnapshot, snapshotList, restoreSnapshot,
  markWriter, readWriter, clearWriter,
} from "./saves.js";

let fails = 0;
const check = (what, ok) => { console.log(`saves: ${what.padEnd(70)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };
class Storage {
  constructor() { this.m = new Map(); }
  getItem(k) { return this.m.has(k) ? this.m.get(k) : null; }
  setItem(k, v) { this.m.set(k, String(v)); }
  removeItem(k) { this.m.delete(k); }
}
const meta = { id: "42", sha: "abc1234" };

// ---- the automatic copy is written once, from what the live page held
const s = new Storage();
s.setItem(STORE, '{"97000":"AAAA"}');
const first = takeSnapshot(s, "auto", meta);
check("first snapshot takes the live store", first !== null && JSON.parse(s.getItem(AUTO)).data === '{"97000":"AAAA"}');
check("a second snapshot leaves the first alone", takeSnapshot(s, "auto", meta) === null);
check("force replaces it", takeSnapshot(s, "auto", meta, true) !== null);
check("the store itself is untouched", s.getItem(STORE) === '{"97000":"AAAA"}');

// ---- a preview writes a new format, the copy survives, restore puts it back
s.setItem(STORE, '{"97000":"BBBB"}');
markWriter(s, meta);
const snap = readSnapshot(s, "auto");
check("the copy keeps the pre-preview bytes", snap !== null && snap.data === '{"97000":"AAAA"}');
check("the writer is recorded", readWriter(s)?.id === "42" && readWriter(s)?.sha === "abc1234");
check("restore writes the copy back", restoreSnapshot(s, "auto") === true && s.getItem(STORE) === '{"97000":"AAAA"}');
check("restore drops the writer marker", readWriter(s) === null);

// ---- the manual copy, and the list the page's select shows
takeSnapshot(s, "manual", { id: "43", sha: "def5678" });
const list = snapshotList(s);
check("both copies are listed", list.map((x) => x.kind).sort().join() === "auto,manual");
check("newest first", list[0].at >= list[1].at);
check("a missing copy is null", readSnapshot(s, "manual") !== null && readSnapshot(s, "nope") === null);
check("restoring a missing copy fails", restoreSnapshot(s, "nope") === false);

// ---- an empty store round-trips to no store, not to "null"
const e = new Storage();
takeSnapshot(e, "auto", meta);
e.setItem(STORE, '{"97000":"CCCC"}');
restoreSnapshot(e, "auto");
check("restoring an empty copy clears the store", e.getItem(STORE) === null);

// ---- the writer marker
const w = new Storage();
check("no writer by default", readWriter(w) === null);
check("writing the same build twice is one write", markWriter(w, meta) === true && markWriter(w, meta) === false);
check("another build replaces it", markWriter(w, { id: "44", sha: "0123456" }) === true && readWriter(w).id === "44");
check("clear removes it", clearWriter(w) === true && readWriter(w) === null);

// ---- a storage that throws never escapes
const boom = { getItem() { throw new Error("denied"); }, setItem() { throw new Error("full"); }, removeItem() { throw new Error("x"); } };
check("throwing storage: snapshot is null", takeSnapshot(boom, "auto", meta) === null && readSnapshot(boom, "auto") === null);
check("throwing storage: restore false, list empty", restoreSnapshot(boom, "auto") === false && snapshotList(boom).length === 0);
check("throwing storage: writer is null and harmless", markWriter(boom, meta) === false && readWriter(boom) === null && clearWriter(boom) === false);

console.log(fails ? `saves: ${fails} FAILED` : "saves: all ok");
process.exit(fails ? 1 : 0);
