// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// fm1backup.js against a simulated device: 7-bit packing, CRC, manifest checks, a full capture,
// validation before the first write, restore order (live music last) and abort on a failed chunk.
import { readFileSync } from "node:fs";
import vm from "node:vm";
import { BACKUP_IDS, BACKUP_CMD, BACKUP_MODULE_VERSION, bkU32, bkR32, bkPack, bkUnpack, bkCrc, bkManifest, readBackup, captureBackup, restoreBackup, readProject, captureProject, restoreProject, projectName, firmwareMismatch } from "./fm1backup.js";

/* Real project fixtures (tests/project_test.c images, converted by the old C firmware) and the editor's own
   DIGITAL -> FM6 converter, so the archive's project objects are the formats the device and the page see. */
const FIX = JSON.parse(readFileSync(new URL("./testdata/projconv.json", import.meta.url), "utf8"));
const b64 = (s) => Uint8Array.from(Buffer.from(s, "base64"));
const html = readFileSync(new URL("./editor.html", import.meta.url), "utf8");
const proto = html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/"));
const E = vm.runInNewContext(proto + ";({FM4, FM6})", { setTimeout, clearTimeout, setInterval, clearInterval, console });
const digitalFm4 = (p, pe0) => {
  const r = E.FM4.convert(p.slice(), pe0);
  return { p: r.p, fm6: E.FM6.pack(r.voice), preset: r.preset };
};
const current = b64(FIX.current);
const fun19 = (bytes) => bytes[0] === 0x46 && bytes[1] === 0x55 && bytes[2] === 0x4e && bytes[3] === 0x4a;
const engineAt = (bytes) => 68 + bytes[66] + (bytes[66] > 111 ? 1 : 0);
const record = (file, id, bytes) => {
  const o = file.objects.find((x) => x.id === id);
  o.size = bytes.length; o.crc = bkCrc(bytes); o.data = Buffer.from(bytes).toString("base64");
  return file;
};

let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(72)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };
const throws = (f) => { try { f(); return false; } catch { return true; } };
const athrows = async (f) => { try { await f(); return false; } catch { return true; } };

const rnd = (n, seed) => Uint8Array.from({ length: n }, (_, i) => (i * 131 + seed * 17 + (i >> 3)) & 255);
ok(bkR32(bkU32(0xdeadbeef)) === 0xdeadbeef && bkCrc(new TextEncoder().encode("123456789")) === 0xcbf43926, "backup: numbers and CRC-32");
ok(BACKUP_MODULE_VERSION === 3.6 && firmwareMismatch({ firmware: "ARAMINTA v3.6" }, "ARAMINTA v3.6") === null &&
   firmwareMismatch({ firmware: "ARAMINTA v3.5" }, "ARAMINTA v3.6")?.from === "ARAMINTA v3.5" &&
   firmwareMismatch({}, "ARAMINTA v3.6") === null,
   "backup: module version and firmware mismatch check");
ok(html.includes('import("./fm1backup.js?v=3.6")') && html.includes("m.BACKUP_MODULE_VERSION !== BACKUP_MODULE_VERSION"),
   "backup: the editor imports a cache-busted module and checks its version");
const b = rnd(1000, 1);
ok(bkUnpack(bkPack(b), b.length).every((v, i) => v === b[i]), "backup: 7-bit pack / unpack round trip");
ok(throws(() => bkUnpack([...bkPack(b.subarray(0, 14)), 0], 14)), "backup: trailing bytes refused");

/* a device: objects by id, sample slots, the order of writes */
function device(objs, opt = {}) {
  const d = { objs: new Map(objs), log: [], staged: null };
  d.request = async ([cmd, a]) => {
    if (cmd === BACKUP_CMD.LIST) {
      const out = [1, 0, BACKUP_IDS.length];
      for (const id of BACKUP_IDS) { const v = d.objs.get(id) || new Uint8Array(0); out.push(id, ...bkU32(v.length), ...bkU32(v.length ? bkCrc(v) : 0)); }
      return out;
    }
    if (cmd === BACKUP_CMD.GET) {
      const id = a[0], off = bkR32(a, 1), n = a[6] | a[7] << 7, v = d.objs.get(id);
      if (opt.changeOnGet === id && off === 0) v[0] ^= 1;
      return [id, 0, ...bkU32(off), n & 127, n >> 7, ...bkPack(v.subarray(off, off + n))];
    }
    if (cmd === BACKUP_CMD.PUT) {
      const [op, id] = a;
      if (op === 0 && id > (opt.maxId ?? 13)) { d.log.push(`refused ${id}`); return [op, id, 1]; }   // (an older firmware)
      if (op === 0) { d.staged = { id, size: bkR32(a, 2), crc: bkR32(a, 7), bytes: [] }; return [op, id, 0]; }
      if (op === 1) {
        if (opt.failChunk === id) return [op, id, 2];
        const off = bkR32(a, 2), p = a.slice(7), n = Math.min(256, d.staged.size - off);
        d.staged.bytes.push(...bkUnpack(p, n)); return [op, id, 0];
      }
      if (op === 2) { const s = Uint8Array.from(d.staged.bytes); const rc = bkCrc(s) === d.staged.crc ? 0 : 2; if (!rc) { d.objs.set(id, s); d.log.push(id); } return [op, id, rc]; }
      if (op === 3) { d.log.push(`abort ${id}`); return [op, id, 0]; }
    }
    if (cmd === 11 || cmd === 12 || cmd === 13 || cmd === 14) { if (cmd === 13 || cmd === 14) d.log.push(32 + a[0]); return [a[0], 0]; }
    throw new Error(`unexpected ${cmd}`);
  };
  return d;
}
const objs = [[0, rnd(3388, 2)], [1, rnd(1200, 3)], [2, current], [6, rnd(3080, 5)]];
const dev = device(objs);
const file = await captureBackup(dev.request, "TEST");
ok(file.objects.length === BACKUP_IDS.length && file.objects[0].size === 3388 && file.objects[3].size === 0, "backup: capture lists every object, empty ones as 0");
ok(readBackup(JSON.stringify(file)).objects[2].bytes.every((v, i) => v === objs[2][1][i]), "backup: capture -> file -> bytes round trip");
ok(await athrows(() => captureBackup(device(objs.map(([i, v]) => [i, v.slice()]), { changeOnGet: 2 }).request, "TEST")), "backup: a device that changes during capture fails the capture");

{
  /* a wavetable slot (FWT1) in a backup: 512-byte header + PCM, its own CRC over the data */
  const data = rnd(8160, 34), wt = new Uint8Array(512 + data.length), wv = new DataView(wt.buffer);
  wt.set(data, 512);
  wv.setUint32(0, 0x31545746, true); wv.setUint16(4, 1, true); wv.setUint16(6, 2, true); wt[8] = 10; wt[9] = 3;
  wv.setUint32(20, data.length, true); wv.setUint32(24, bkCrc(data), true);
  const wf = await captureBackup(device([...objs, [34, wt]]).request, "TEST");
  const wo = wf.objects.find((o) => o.id === 34);
  ok(wo && wo.size === wt.length && !!readBackup(JSON.stringify(wf)).objects.find((o) => o.id === 34),
    "backup: a wavetable slot (FWT1) is captured and validated");
  const badWt = JSON.parse(JSON.stringify(wf)), bo = badWt.objects.find((o) => o.id === 34);
  const bb = Uint8Array.from(wt); bb[700] ^= 1;
  bo.data = Buffer.from(bb).toString("base64"); bo.crc = bkCrc(bb);
  ok(throws(() => readBackup(badWt)), "backup: a wavetable whose data CRC is wrong is refused");
}
const bad = JSON.parse(JSON.stringify(file)); bad.objects[2].crc ^= 1;
ok(throws(() => readBackup(bad)), "backup: a damaged file is refused");
const noRun = JSON.parse(JSON.stringify(file)); noRun.objects[0] = { ...noRun.objects[0], size: 0, crc: 0, data: "" };
ok(throws(() => readBackup(noRun)), "backup: a file without the current music is refused");
ok(throws(() => bkManifest([1, 0, 3])), "backup: a short manifest is refused");

const target = device([]);
const damaged = JSON.parse(JSON.stringify(file)); damaged.objects[6].crc ^= 1;
ok(await athrows(() => restoreBackup(target.request, damaged)) && target.log.length === 0, "backup: restore validates every byte before the first write");
await restoreBackup(target.request, file);
const order = target.log.filter((x) => typeof x === "number");
ok(order.at(-1) === 0 && order.at(-2) === 1 && order.indexOf(2) < order.indexOf(1), "backup: restore order: projects, banks, samples, settings, live music last");
ok([0, 1, 2, 6].every((id) => target.objs.get(id).every((v, i) => v === objs.find((o) => o[0] === id)[1][i])), "backup: restored objects equal the source");
{   /* an archive from another firmware: the restore asks first and writes nothing when declined */
  const declined = device([]);
  let asked = null;
  const result = await restoreBackup(declined.request, file, () => {}, undefined,
    { firmware: "ARAMINTA v3.5", confirmMismatch: (m) => { asked = m; return false; } });
  ok(result === null && asked && asked.from === file.firmware && asked.to === "ARAMINTA v3.5" && declined.log.length === 0,
    "backup: a firmware mismatch asks first; a decline writes nothing");
  const accepted = device([]);
  await restoreBackup(accepted.request, file, () => {}, undefined,
    { firmware: "ARAMINTA v3.6", confirmMismatch: () => true });
  ok(accepted.log.length > 0 && accepted.objs.get(0).every((v, i) => v === objs[0][1][i]),
    "backup: .. an accepted mismatch restores");
}
{   /* the FM6 patch bank (id 8) and the archives of firmware before it (11 objects, no id 8) */
  ok(BACKUP_IDS.length === 17 && file.objects.some((o) => o.id === 8) && file.objects.some((o) => o.id === 9) &&
     file.objects.some((o) => o.id === 10) && file.objects.some((o) => o.id === 13),
    "backup: 17 objects: id 8 (retired FM6 bank), id 9 (FM6 patches), ids 10..13 (banks 2..5)");
  const v4 = JSON.parse(JSON.stringify(file)); v4.objects = v4.objects.filter((o) => o.id < 10 || o.id >= 32);
  ok(readBackup(v4).objects.length === 13, "backup: an archive before banks 2..5 (13 objects) still reads");
  const v2 = JSON.parse(JSON.stringify(file)); v2.objects = v2.objects.filter((o) => o.id !== 9 && (o.id < 10 || o.id >= 32));
  ok(readBackup(v2).objects.length === 12, "backup: an archive of 1.0..1.0.2 (12 objects, the bank as id 8) still reads");
  const old = JSON.parse(JSON.stringify(file)); old.objects = old.objects.filter((o) => o.id !== 8 && o.id !== 9 && (o.id < 10 || o.id >= 32));
  ok(readBackup(old).objects.length === 11, "backup: an archive of the 11 objects before FM6 still reads");
  const odd = JSON.parse(JSON.stringify(file)); odd.objects = odd.objects.filter((o) => o.id !== 33);
  ok(throws(() => readBackup(odd)), "backup: an archive missing another object is refused");
}
{   /* 1.0.3: an old archive with a bank restores its id 8 (after 6, 7); a new archive onto 1.0.2 skips id 9 */
  const bankBytes = rnd(3472, 8), patches = rnd(3728, 9);
  const v2dev = device([...objs, [8, bankBytes]]);
  const v2file = await captureBackup(async ([cmd, a]) => {   // (a 1.0.2 device: 12 objects)
    const r = await v2dev.request([cmd, a]);
    if (cmd !== BACKUP_CMD.LIST) return r;
    const n = r[2], keep = [];
    for (let i = 0; i < n; i++) {
      const id = r[3 + i * 11];
      if (id !== 9 && !(id >= 10 && id <= 13)) keep.push(...r.slice(3 + i * 11, 14 + i * 11));
    }
    return [1, 0, keep.length / 11, ...keep];
  }, "1.0.2");
  const to103 = device([]);
  await restoreBackup(to103.request, v2file);
  const lg = to103.log.filter((x) => typeof x === "number");
  ok(v2file.objects.length === 12 && lg.indexOf(8) > lg.indexOf(7) && lg.indexOf(7) > lg.indexOf(6) &&
     to103.objs.get(8).every((v, i) => v === bankBytes[i]), "backup: a 1.0.2 archive (id 8, the bank) restores, its bank after the user presets");
  const newFile = await captureBackup(device([...objs, [9, patches]]).request, "1.0.3");
  const to102 = device([], { maxId: 8 });
  await restoreBackup(to102.request, newFile);
  ok(to102.log.includes("refused 9") && !to102.objs.has(9) && !to102.objs.has(10) && to102.log.at(-1) === 0,
    "backup: a 1.0.3 archive onto older firmware: id 9 skipped, the rest restored");
  const to103b = device([]);
  await restoreBackup(to103b.request, newFile);
  ok(to103b.objs.get(9).every((v, i) => v === patches[i]), "backup: a 1.0.3 archive restores id 9");
}
{   /* a complete archive with FUN1, FUN6 and DIGITAL project objects: capture keeps the bytes, restore converts */
  const fun1 = b64(FIX.old[1].input), fun6 = b64(FIX.old[6].input), dig = b64(FIX.digital.input);
  const src = device([[0, rnd(3388, 60)], [1, rnd(1200, 61)], [2, fun1], [3, fun6], [4, dig], [5, current]]);
  const archive = await captureBackup(src.request, "3.6");
  const object = (f, id) => f.objects.find((o) => o.id === id);
  ok(object(archive, 2).size === 688 && object(archive, 2).data === Buffer.from(fun1).toString("base64") &&
     object(archive, 3).size === 3388 && object(archive, 4).size === 3840 &&
     object(archive, 4).data === Buffer.from(dig).toString("base64"),
    "backup: capture keeps FUN1 / FUN6 / DIGITAL project bytes");
  const ready = readBackup(JSON.stringify(archive), digitalFm4);
  ok([2, 3, 4, 5].every((id) => object(ready, id).size === 3840 && fun19(object(ready, id).bytes)),
    "backup: readBackup converts every project object to FUN19");
  ok(object(ready, 2).bytes.every((v, i) => v === b64(FIX.old[1].expected)[i]) &&
     object(ready, 3).bytes.every((v, i) => v === b64(FIX.old[6].expected)[i]),
    "backup: FUN1 / FUN6 convert byte for byte == the C reader");
  const target = device([]);
  await restoreBackup(target.request, archive, () => {}, digitalFm4);
  const got = (id) => target.objs.get(id);
  ok(got(2).length === 3840 && got(2).every((v, i) => v === b64(FIX.old[1].expected)[i]) &&
     got(3).every((v, i) => v === b64(FIX.old[6].expected)[i]),
    "backup: restore writes the converted FUN1 / FUN6, not the old size");
  ok(got(4).every((v, i) => v === b64(FIX.digital.expected)[i]) && got(4)[engineAt(got(4))] === 12,
    "backup: restore converts a DIGITAL track to FUN19 / engine 12");
  ok(object(archive, 2).size === 688 && object(archive, 2).crc === bkCrc(fun1),
    "backup: the user's original archive object keeps its size and crc");

  const junk = record(JSON.parse(JSON.stringify(archive)), 2, rnd(688, 62));
  const noPut = device([]);
  ok(await athrows(() => restoreBackup(noPut.request, junk, () => {}, digitalFm4)) && noPut.log.length === 0,
    "backup: an invalid project object is refused before any PUT");
  const noConverter = device([]);
  ok(await athrows(() => restoreBackup(noConverter.request, archive)) && noConverter.log.length === 0,
    "backup: a DIGITAL project without the editor's converter is refused before any PUT");
}
const failing = device([], { failChunk: 2 });
ok(await athrows(() => restoreBackup(failing.request, file)) && failing.log.includes("abort 2") && !failing.log.includes(0), "backup: a refused chunk aborts that object and stops before the live music");

{   /* one PROJECT slot (backup id 2..5) as its own file: other slots stay */
  const fun10 = (seed, name) => {
    const b = rnd(3840, seed);
    b[0] = 0x41; b[1] = 0x4e; b[2] = 0x55; b[3] = 0x46;           // LE "FUNA"
    for (let i = 0; i < 12; i++) b[3824 + i] = name.charCodeAt(i) || 0;
    return b;
  };
  const a = fun10(20, "SONG A"), b = fun10(21, "SONG B"), c = fun10(22, "KEEP C");
  const src = device([[0, rnd(3840, 30)], [1, rnd(1200, 31)], [2, a], [3, b], [4, c]]);
  const exported = await captureProject(src.request, 0, "TEST");
  ok(exported.format === "felucca-project" && exported.size === 3840 && exported.slot === 0
    && projectName(readProject(exported).bytes) === "SONG A", "project: export slot A as felucca-project");
  ok(await athrows(() => captureProject(device([[2, new Uint8Array(0)]]).request, 0, "TEST")), "project: empty slot refused");
  const bad = JSON.parse(JSON.stringify(exported)); bad.crc ^= 1;
  ok(throws(() => readProject(bad)), "project: a damaged file is refused");
  ok(throws(() => readProject({ ...exported, crc: 1, bytes: b64(exported.data) })),
    "project: in-memory bytes cannot bypass the transport CRC");
  const converted = readProject(JSON.parse(JSON.stringify(
    { ...exported, size: b64(FIX.old[2].input).length, crc: bkCrc(b64(FIX.old[2].input)), data: FIX.old[2].input })), digitalFm4);
  ok(converted.converted && converted.data === Buffer.from(converted.bytes).toString("base64") &&
     converted.crc === bkCrc(converted.bytes) && converted.size === converted.bytes.length,
    "project: conversion rebuilds data / size / crc together");
  ok(readProject(converted, digitalFm4).converted === false, "project: a converted project reads back as a no-op");
  const target = device([[0, rnd(3840, 40)], [1, rnd(1200, 41)], [2, fun10(42, "OLD A")], [3, b], [4, c]]);
  await restoreProject(target.request, exported, 2);   // import A's song into slot C
  ok(target.objs.get(4).every((v, i) => v === a[i]) && target.objs.get(2).every((v, i) => v === fun10(42, "OLD A")[i])
    && target.objs.get(3).every((v, i) => v === b[i]) && !target.log.includes(0) && !target.log.includes(1),
    "project: import writes one slot only; A, B, live music and settings stay");
  ok(target.log.filter((x) => typeof x === "number").join() === "4", "project: only backup id 4 was committed");
  for (const magicByte of [0x42, 0x43, 0x44, 0x45, 0x46]) {
    const bytes = fun10(50, "FX LOCKS"); bytes[0] = magicByte;
    const fixture = { ...exported, data: Buffer.from(bytes).toString("base64"), crc: bkCrc(bytes) };
    ok(!throws(() => readProject(fixture)), `project: FUN${magicByte - 0x41 + 10} recognized at its existing 3840-byte size`);
  }
  const fun15 = fun10(51, "FX LOCKS"); fun15[0] = 0x46;
  const fun15Device = device([[2, fun15]]);
  let currentFile;
  try { currentFile = await captureProject(fun15Device.request, 0, "TEST"); } catch { currentFile = null; }
  ok(currentFile !== null, "project: export current FUN15 with FX locks");
  if (currentFile !== null) {
    const restored = device([]);
    await restoreProject(restored.request, currentFile, 1);
    ok(restored.objs.get(3).every((v, i) => v === fun15[i]), "project: FUN15 export/import retains every lock byte");
  }
  for (const magicByte of [0x48, 0x49, 0x4a]) {
    const bytes = fun10(52, "TAPE TEMPO");
    bytes.set([0x46, 0x55, 0x4e, magicByte]);
    let captured;
    try { captured = await captureProject(device([[2, bytes]]).request, 0, "TEST"); }
    catch { captured = null; }
    ok(captured !== null, `project: FUN${magicByte - 0x41 + 10} exports from the device`);
    if (captured !== null) {
      const restored = device([]);
      await restoreProject(restored.request, captured, 1);
      ok(restored.objs.get(3).every((v, i) => v === bytes[i]),
        `project: FUN${magicByte - 0x41 + 10} export/import preserves all project bytes`);
    }
  }
  /* a slot whose stored project holds a DIGITAL track: export needs the editor's converter */
  const dig = b64(FIX.digital.input);
  const digFile = await captureProject(device([[2, dig]]).request, 0, "3.6", () => {}, digitalFm4);
  ok(digFile.size === 3840 && digFile.data === Buffer.from(dig).toString("base64"),
    "project: DIGITAL slot export keeps the stored bytes");
  ok(throws(() => readProject(digFile)), "project: DIGITAL export without the converter is refused");
  const digRead = readProject(digFile, digitalFm4);
  ok(digRead.converted && digRead.bytes.length === 3840 && digRead.bytes[engineAt(digRead.bytes)] === 12,
    "project: DIGITAL export with the converter yields FUN19 / engine 12 (so the file name is readable)");
  const digDst = device([]);
  await restoreProject(digDst.request, digFile, 1, () => {}, digitalFm4);
  ok(digDst.objs.get(3).every((v, i) => v === b64(FIX.digital.expected)[i]),
    "project: DIGITAL import writes the converted FUN19");
}

console.log(fails ? `BACKUP WEB TESTS FAILED (${fails})` : "backup web tests passed");
process.exit(fails ? 1 : 0);
