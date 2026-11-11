// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The emulator's saved flash, and the copies that protect it while a preview build shares it with the live one.
// STORE is the live page's key (web/emu/index.html). web/emu/build.sh gives a preview the same key on purpose, so a
// storage change can be tried against the saves the live emulator already holds. AUTO keeps the live saves as they
// were before a preview firmware could migrate them: written at a preview's first switch-on, never overwritten.
// MANUAL is the preview page's "Back up now". WRITER records the build that wrote the store last, so the live page
// can say which preview touched it.
// Every call tolerates a storage that throws (private mode, full quota): it returns null / false and the page runs
// session-only, as it did before.

export const STORE = "felucca-emu-flash";
export const AUTO = STORE + ".auto";
export const MANUAL = STORE + ".manual";
export const WRITER = STORE + ".writer";

const KEY = { auto: AUTO, manual: MANUAL };

function parse(raw) {
  if (!raw) return null;
  let r;
  try { r = JSON.parse(raw); } catch { return null; }
  if (!r || typeof r !== "object" || typeof r.data !== "string" || !Number.isFinite(r.at)) return null;
  return { at: r.at, id: String(r.id || ""), sha: String(r.sha || ""), data: r.data };
}

/** the copy of the given kind, or null */
export function readSnapshot(storage, kind) {
  try { return parse(storage.getItem(KEY[kind])); } catch { return null; }
}

/** keep the store as it is now; returns the record, or null when the copy exists and force is not set */
export function takeSnapshot(storage, kind, meta, force = false) {
  try {
    if (!force && storage.getItem(KEY[kind])) return null;
    const data = storage.getItem(STORE) || "";
    const rec = { at: Date.now(), id: String(meta.id || ""), sha: String(meta.sha || ""), data };
    storage.setItem(KEY[kind], JSON.stringify(rec));
    return rec;
  } catch { return null; }
}

/** both kinds that exist, newest first */
export function snapshotList(storage) {
  const out = [];
  for (const kind of Object.keys(KEY)) {
    const rec = readSnapshot(storage, kind);
    if (rec) out.push({ kind, at: rec.at, id: rec.id, sha: rec.sha, bytes: rec.data.length });
  }
  return out.sort((a, b) => b.at - a.at);
}

/** put a copy back; the writer marker goes with it (the store is live data again) */
export function restoreSnapshot(storage, kind) {
  const rec = readSnapshot(storage, kind);
  if (!rec) return false;
  try {
    if (rec.data) storage.setItem(STORE, rec.data);
    else storage.removeItem(STORE);
    storage.removeItem(WRITER);
    return true;
  } catch { return false; }
}

/** note that this build wrote the store; unchanged writer = no write */
export function markWriter(storage, meta) {
  try {
    const prev = readWriter(storage);
    if (prev && prev.id === String(meta.id || "") && prev.sha === String(meta.sha || "")) return false;
    storage.setItem(WRITER, JSON.stringify({ at: Date.now(), id: String(meta.id || ""), sha: String(meta.sha || "") }));
    return true;
  } catch { return false; }
}

/** which build wrote the store last, or null */
export function readWriter(storage) {
  try {
    const w = JSON.parse(storage.getItem(WRITER) || "null");
    if (!w || typeof w !== "object" || !Number.isFinite(w.at)) return null;
    return { at: w.at, id: String(w.id || ""), sha: String(w.sha || "") };
  } catch { return null; }
}

export function clearWriter(storage) {
  try { storage.removeItem(WRITER); return true; } catch { return false; }
}
