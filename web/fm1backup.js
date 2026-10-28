// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Local, complete musical archives. Requests name whitelisted objects, never flash addresses.
// 8: the FM6 patch bank of 1.0..1.0.2 (listed empty since 1.0.3: a restore of an older archive's bank moves its patches
// into the user presets restored before it); 9: the user presets' FM6 patches (1.0.3)
export const BACKUP_IDS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 32, 33, 34];
const BACKUP_IDS_V2 = BACKUP_IDS.filter((id) => id !== 9);              // 1.0..1.0.2, and their archives
const BACKUP_IDS_V1 = BACKUP_IDS_V2.filter((id) => id !== 8);           // firmware before FM6, and its archives
const idsOf = (n) => [BACKUP_IDS, BACKUP_IDS_V2, BACKUP_IDS_V1].find((ids) => ids.length === n) || null;
export const BACKUP_CMD = { LIST: 65, GET: 66, PUT: 67 };
const BACKUP_CHUNK = 256;
export const bkU32 = (n) => Array.from({ length: 5 }, (_, i) => (n >>> (i * 7)) & (i === 4 ? 15 : 127));
export const bkR32 = (a, off = 0) => {
  if (a.length < off + 5 || a[off + 4] > 15) throw new Error("Invalid archive number");
  return (a[off] | a[off + 1] << 7 | a[off + 2] << 14 | a[off + 3] << 21 | a[off + 4] << 28) >>> 0;
};
export function bkPack(bytes) {
  const a = [];
  for (let off = 0; off < bytes.length; off += 7) {
    const chunk = bytes.subarray(off, off + 7);
    a.push(chunk.reduce((m, b, i) => m | (b >>> 7) << i, 0), ...chunk.map((b) => b & 127));
  }
  return a;
}
export function bkUnpack(a, size) {
  const out = new Uint8Array(size); let i = 0, j = 0;
  while (j < size) {
    const n = Math.min(7, size - j), mask = a[i++];
    if (mask == null || mask >>> n) throw new Error("Invalid archive bytes");
    for (let k = 0; k < n; k++) {
      if (a[i] == null || a[i] > 127) throw new Error("Short archive chunk");
      out[j++] = a[i++] | (mask >>> k & 1) << 7;
    }
  }
  if (i !== a.length) throw new Error("Trailing archive bytes");
  return out;
}
/* the backup archives' CRC-32. Table-driven, like editor.html's crc32 (the sample header's), which the editor's
   protocol block cannot import: web/test_web.mjs evaluates that block on its own. web/test_crc.mjs checks the two
   agree, so the pairs below stay one checksum. */
const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return t;
})();
export function bkCrc(bytes) {
  let c = 0xFFFFFFFF;
  for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}
function bkCheck(rc) {
  if (rc) throw new Error(({ 1: "Invalid archive object", 2: "Archive data failed validation", 3: "Stop playback first", 4: "Flash write failed", 5: "Start a fresh backup" })[rc] || `Archive error ${rc}`);
}
export function bkManifest(a) {
  if (a[0] !== 1) throw new Error("Unsupported archive protocol");
  bkCheck(a[1]);
  const ids = idsOf(a[2]);
  if (!ids || a.length !== 3 + a[2] * 11) throw new Error("Incomplete archive manifest");
  return ids.map((id, i) => {
    const p = 3 + i * 11;
    if (a[p] !== id) throw new Error("Unexpected archive object");
    const size = bkR32(a, p + 1), crc = bkR32(a, p + 6);
    if (size > (id >= 32 ? 81920 : 3840) || (!size && crc)) throw new Error("Archive object too large");
    return { id, size, crc };
  });
}
const bkBase64 = (bytes) => { let s = ""; for (const b of bytes) s += String.fromCharCode(b); return btoa(s); };
export function readBackup(file) {
  if (typeof file === "string") file = JSON.parse(file);
  const ids = file && Array.isArray(file.objects) ? idsOf(file.objects.length) : null;
  if (!file || file.format !== "felucca-backup" || file.version !== 1 || !ids)
    throw new Error("Not a complete Felucca backup");
  const objects = file.objects.map((o, i) => {
    if (!o || o.id !== ids[i] || !Number.isInteger(o.size) || o.size < 0 || o.size > (o.id >= 32 ? 81920 : 3840) ||
        !Number.isInteger(o.crc) || o.crc < 0 || o.crc > 0xffffffff || typeof o.data !== "string" ||
        o.data.length !== 4 * Math.ceil(o.size / 3) || !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(o.data))
      throw new Error("Invalid backup object");
    const bytes = Uint8Array.from(atob(o.data), (c) => c.charCodeAt(0));
    if (bytes.length !== o.size || bkCrc(bytes) !== o.crc) throw new Error("Backup checksum mismatch");
    if (o.id >= 32 && o.size) {
      if (o.size < 512) throw new Error("Short slot backup");
      const view = new DataView(bytes.buffer);
      const magic = view.getUint32(0, true);
      if (magic === 0x31545746) {                    /* FWT1: a wavetable (eng_sample.c smp_wt_ok) */
        const frames = view.getUint16(6, true), base = bytes[8], mip = bytes[9], length = view.getUint32(20, true);
        const stride = base >= 4 && base <= 12 && mip < base ? (2 ** (base + 1)) - (2 ** mip) : -1;
        if (!frames || frames > 64 || stride < 0 || length !== frames * stride * 2 || length !== o.size - 512 ||
            bkCrc(bytes.subarray(512)) !== view.getUint32(24, true)) throw new Error("Invalid wavetable backup");
      } else {
        const length = view.getUint32(16, true), count = bytes[6];
        if (magic !== 0x504d5346 || view.getUint16(4, true) !== 1 || !count || count > 16 ||
            length !== o.size - 512 || bkCrc(bytes.subarray(512)) !== view.getUint32(20, true)) throw new Error("Invalid sample backup");
        for (let z = 0; z < count; z++) {
          const off = 32 + z * 28, start = view.getUint32(off,true), n = view.getUint32(off+4,true),
            ls = view.getUint32(off+8,true), le = view.getUint32(off+12,true), rate = view.getUint32(off+16,true);
          if (!n || start > length || Math.ceil(n/2) > length - start || n > 163840 || ls > le || le >= n ||
              !rate || rate > 262144 || bytes[off+24] > 88 || bytes[off+25] > bytes[off+26]) throw new Error("Invalid sample zone");
        }
      }
    }
    return { ...o, bytes };
  });
  if (!objects[0].size || !objects[1].size) throw new Error("Backup is missing the current music or settings");
  return { ...file, objects };
}
export async function captureBackup(request, firmware, onProgress = () => {}) {
  const manifest = bkManifest(await request([BACKUP_CMD.LIST, []], { timeout: 3000, retries: 0 }));
  const objects = [], total = manifest.reduce((n, o) => n + o.size, 0); let done = 0;
  for (const o of manifest) {
    const bytes = new Uint8Array(o.size);
    for (let off = 0; off < o.size; off += BACKUP_CHUNK) {
      const size = Math.min(BACKUP_CHUNK, o.size - off);
      const a = await request([BACKUP_CMD.GET, [o.id, ...bkU32(off), size & 127, size >>> 7]], { timeout: 1000, retries: 1 });
      bkCheck(a[1]);
      if (a[0] !== o.id || bkR32(a, 2) !== off || (a[7] | a[8] << 7) !== size) throw new Error("Unexpected backup reply");
      bytes.set(bkUnpack(a.slice(9), size), off);
      done += size; onProgress(done, total);
    }
    if (bkCrc(bytes) !== o.crc) throw new Error("The device changed during backup. Try again while stopped.");
    objects.push({ ...o, data: bkBase64(bytes) });
  }
  const file = { format: "felucca-backup", version: 1, firmware, created: new Date().toISOString(), objects };
  readBackup(file); return file;
}
export async function restoreBackup(request, file, onProgress = () => {}) {
  const archive = readBackup(file); // Validate every byte before the first destructive request.
  const total = archive.objects.reduce((n, o) => n + o.size, 0); let done = 0;
  const ask = async (r, o = {}) => request(r, { timeout: 4000, retries: 0, ...o });
  const put = async (args) => { const a = await ask([BACKUP_CMD.PUT, args]); bkCheck(a[2]); return a; };
  // Restore live music last. Other objects commit individually; a disconnect can leave a partial restore.
  for (const o of [...archive.objects.slice(2), archive.objects[1], archive.objects[0]]) {
    if (o.id >= 32) {
      const slot = o.id - 32;
      const check = (a) => { if (a[0] !== slot) throw new Error("Unexpected sample reply"); bkCheck(a.at(-1)); };
      if (!o.size) check(await ask([14, [slot]]));
      else {
        if (o.size < 512) throw new Error("Short sample backup");
        check(await ask([11, [slot]]));
        for (let off = 512; off < o.size; off += BACKUP_CHUNK) {
          const chunk = o.bytes.subarray(off, off + BACKUP_CHUNK);
          check(await ask([12, [slot, off & 127, off >>> 7 & 127, off >>> 14 & 127, ...bkPack(chunk)]]));
          done += chunk.length; onProgress(done, total);
        }
        check(await ask([13, [slot, ...bkPack(o.bytes.subarray(0, 480))]]));
        done += 512; onProgress(done, total);
      }
    } else {
      if (o.id === 9) {                    // firmware before 1.0.3 does not take id 9 (rc 1 at begin, nothing written): skip it
        const a = await ask([BACKUP_CMD.PUT, [0, o.id, ...bkU32(o.size), ...bkU32(o.crc)]]);
        if (a[2] === 1) { done += o.size; onProgress(done, total); continue; }
        bkCheck(a[2]);
      } else await put([0, o.id, ...bkU32(o.size), ...bkU32(o.crc)]);
      try {
        for (let off = 0; off < o.size; off += BACKUP_CHUNK) {
          const chunk = o.bytes.subarray(off, off + BACKUP_CHUNK);
          await put([1, o.id, ...bkU32(off), ...bkPack(chunk)]);
          done += chunk.length; onProgress(done, total);
        }
        await put([2, o.id]);
      } catch (e) { await put([3, o.id]).catch(() => {}); throw e; }
    }
  }
  return archive;
}

/* One PROJECT slot (backup ids 2..5) as its own file. Uses LIST/GET/PUT only; other slots stay. */
const PROJECT_MAGIC = {
  3388: [0x46554e37], 3584: [0x46554e38], 3648: [0x46554e39],
  3840: [0x46554e41, 0x46554e42, 0x46554e43, 0x46554e44, 0x46554e45, 0x46554e46,
    0x474e5546, 0x484e5546, 0x494e5546, 0x4a4e5546],
};
const projId = (slot) => {
  if (!Number.isInteger(slot) || slot < 0 || slot > 3) throw new Error("Project slot must be 0..3");
  return 2 + slot;
};
const projMagic = (bytes) => (bytes[0] | bytes[1] << 8 | bytes[2] << 16 | bytes[3] << 24) >>> 0;
export function projectName(bytes) {
  if (!bytes || !(bytes.length in PROJECT_MAGIC)) return "";
  let s = "";
  for (let i = bytes.length - 16; i < bytes.length - 4; i++) {
    const c = bytes[i];
    if (!c) break;
    if (c < 32 || c > 126) return "";
    s += String.fromCharCode(c);
  }
  return s.trim();
}
function checkProjectBytes(bytes) {
  const want = PROJECT_MAGIC[bytes.length];
  if (!want || !want.includes(projMagic(bytes))) throw new Error("Not a Felucca project");
  return bytes;
}
export function readProject(file) {
  if (typeof file === "string") file = JSON.parse(file);
  if (!file || file.format !== "felucca-project" || file.version !== 1 ||
      !Number.isInteger(file.size) || !(file.size in PROJECT_MAGIC) ||
      !Number.isInteger(file.crc) || file.crc < 0 || file.crc > 0xffffffff ||
      typeof file.data !== "string" ||
      file.data.length !== 4 * Math.ceil(file.size / 3) ||
      !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(file.data))
    throw new Error("Not a Felucca project file");
  const bytes = checkProjectBytes(Uint8Array.from(atob(file.data), (c) => c.charCodeAt(0)));
  if (bytes.length !== file.size || bkCrc(bytes) !== file.crc) throw new Error("Project checksum mismatch");
  const slot = Number.isInteger(file.slot) && file.slot >= 0 && file.slot <= 3 ? file.slot : 0;
  return { ...file, slot, bytes, name: projectName(bytes) };
}
export async function captureProject(request, slot, firmware, onProgress = () => {}) {
  const id = projId(slot);
  const manifest = bkManifest(await request([BACKUP_CMD.LIST, []], { timeout: 3000, retries: 0 }));
  const o = manifest.find((x) => x.id === id);
  if (!o) throw new Error("Project slot missing from archive list");
  if (!o.size) throw new Error("Empty project slot");
  if (!(o.size in PROJECT_MAGIC)) throw new Error("Unexpected project size");
  const bytes = new Uint8Array(o.size);
  for (let off = 0; off < o.size; off += BACKUP_CHUNK) {
    const size = Math.min(BACKUP_CHUNK, o.size - off);
    const a = await request([BACKUP_CMD.GET, [id, ...bkU32(off), size & 127, size >>> 7]], { timeout: 1000, retries: 1 });
    bkCheck(a[1]);
    if (a[0] !== id || bkR32(a, 2) !== off || (a[7] | a[8] << 7) !== size) throw new Error("Unexpected project reply");
    bytes.set(bkUnpack(a.slice(9), size), off);
    onProgress(off + size, o.size);
  }
  if (bkCrc(bytes) !== o.crc) throw new Error("The device changed during export. Try again while stopped.");
  checkProjectBytes(bytes);
  const file = {
    format: "felucca-project", version: 1, firmware, created: new Date().toISOString(),
    slot, size: o.size, crc: o.crc, data: bkBase64(bytes),
  };
  readProject(file);
  return file;
}
export async function restoreProject(request, file, slot, onProgress = () => {}) {
  const project = readProject(file);
  const id = projId(slot);
  const ask = async (r, o = {}) => request(r, { timeout: 4000, retries: 0, ...o });
  const put = async (args) => { const a = await ask([BACKUP_CMD.PUT, args]); bkCheck(a[2]); return a; };
  await put([0, id, ...bkU32(project.size), ...bkU32(project.crc)]);
  try {
    for (let off = 0; off < project.size; off += BACKUP_CHUNK) {
      const chunk = project.bytes.subarray(off, off + BACKUP_CHUNK);
      await put([1, id, ...bkU32(off), ...bkPack(chunk)]);
      onProgress(off + chunk.length, project.size);
    }
    await put([2, id]);
  } catch (e) { await put([3, id]).catch(() => {}); throw e; }
  return project;
}

// Installer connection: no editor page or watcher owns this input simultaneously.
export class BackupConnection {
  constructor(input, output) {
    this.input = input; this.output = output; this.pending = null;
    input.onmidimessage = (e) => {
      const d = e.data;
      if (d.length < 6 || d[0] !== 240 || d[1] !== 125 || d[2] !== 70 || d[3] !== 76 || d.at(-1) !== 247) return;
      if (this.pending?.cmd === d[4]) { const p = this.pending; this.pending = null; clearTimeout(p.timer); p.resolve(Array.from(d.slice(5, -1))); }
    };
  }
  async request([cmd, args], opt = {}) {
    if (this.pending || this.closed || this.input.state === "disconnected" || this.output.state === "disconnected") throw new Error("Backup connection closed or busy");
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending = null; reject(new Error("Backup timed out. Keep the FM-1 connected and stopped.")); }, opt.timeout || 1200);
      this.pending = { cmd, timer, resolve, reject };
      try { this.output.send([240, 125, 70, 76, cmd, ...args, 247]); }
      catch (e) { clearTimeout(timer); this.pending = null; reject(e); }
    });
  }
  close() {
    this.closed = true; this.input.onmidimessage = null;
    if (this.pending) { const p = this.pending; this.pending = null; clearTimeout(p.timer); p.reject(new Error("Backup connection closed")); }
  }
}
