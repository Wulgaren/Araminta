// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Felucca in an AudioWorklet: felucca.wasm is the whole FM-1 firmware against a simulated FM-1
// (web/emu/felucca_web.c). Each render quantum asks it for 128 frames, which runs the device's clock forward;
// between quanta the page's input goes in and, about 30 times a second (4 when the page is hidden), the screen,
// the lights and any changed flash go out. After X0X's web/emu/worklet.js (charlesvestal/fm1-x0x, GPL-3.0).
//
// No top-level "export": the browser loads this file with AudioWorklet.addModule, which compiles a classic script
// (a top-level "export" is a SyntaxError there). build/emu/worklet.js is web/emu/store.js + web/emu/rec.js (each
// "export " stripped) in front of this file: storeSectors / restoreSectors / STORE_AREAS and the session recorder's
// recAudio / recQuantum / REC_*; the same store.js / rec.js are the modules the tests and web/emu/replay.mjs import.
// The page's ?rec=1 sets load's rec: every input as it reaches the wasm, and each quantum's render time and audio
// health, go into a recording that web/emu/replay.mjs plays back bit for bit.
// Nothing else may be added at the top level here.

const HOLD_MS = 24;

if (typeof registerProcessor === "function") {
  const clock = globalThis.performance ? () => globalThis.performance.now() : () => Date.now();
  const REC_CHUNK = 4096 * 4;                       // floats per health chunk: 4096 quanta of [run, total, peak, step]
  class Felucca extends AudioWorkletProcessor {
    constructor() {
      super();
      this.ex = null;
      this.sentDraws = -1;
      this.sentWrites = -1;
      this.sentLeds = "";
      this.lastFrame = 0;
      this.period = 1 / 30;
      this.busy = 0;
      this.frames = 0;
      this.f = 0;                                     // frames since load (recorder; not the load figure)
      this.rec = null;                                // session recorder; set on load when m.rec
      this.queue = [];                                // button / key changes, each held >= HOLD_MS of device time
      this.nextOk = 0;
      this.port.onmessage = (e) => this.onMessage(e.data);
    }

    // one control input (or mark / visible) into the recording, at the frame it reaches the wasm
    recEvent(payload) {
      if (!this.rec || !this.rec.on) return;
      this.rec.events.push({ f: this.f, ms: this.ex.web_now_ms(), wall: Date.now(), ...payload });
    }

    // the current partial summary row, or null when the bucket is empty
    recPartial() {
      const r = this.rec;
      if (!r || !r.q) return null;
      const a = r.audio;
      return { f: r.bucketF, sum: a.sum, peak: a.peak, jump: a.jump };
    }

    // one quantum's [run ms, total ms, peak, step] into the recording, four floats at a time: a recording is minutes
    // long, so a per-quantum object would be garbage on the audio thread and a new chunk only comes when one fills
    recHealth(run, total, peak, step) {
      const r = this.rec;
      if (!r.qat) r.quanta.push(new Float32Array(REC_CHUNK));
      const c = r.quanta[r.quanta.length - 1];
      c[r.qat] = run; c[r.qat + 1] = total; c[r.qat + 2] = peak; c[r.qat + 3] = step;
      r.qat += 4;
      if (r.qat === REC_CHUNK) r.qat = 0;
      r.qn++;
    }

    // the recorded quanta as one Float32Array, 4 x qn long (rec-dump)
    recQuanta() {
      const r = this.rec, n = r.qn * 4, out = new Float32Array(n);
      for (let i = 0, off = 0; off < n; i++, off += REC_CHUNK)
        out.set(r.quanta[i].subarray(0, Math.min(REC_CHUNK, n - off)), off);
      return out;
    }

    // close the current summary bucket into seconds[] (rec-stop, or every REC_BUCKET quanta)
    recFlush() {
      const row = this.recPartial();
      if (!row) return;
      this.rec.seconds.push(row);
      const a = this.rec.audio;
      a.sum = a.peak = a.jump = 0;
      this.rec.q = 0;
    }

    async onMessage(m) {
      if (m.type === "load") {
        try {
          const { instance } = await WebAssembly.instantiate(m.wasm, {});
          const ex = instance.exports;
          ex._initialize();
          ex.web_nor_erase();
          restoreSectors(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()), m.sectors);
          ex.web_master(m.master ?? 700);
          ex.web_boot();
          this.ex = ex;
          this.f = 0;
          this.rec = m.rec ? { events: [], seconds: [], audio: recAudio(), q: 0, on: true, bucketF: 0,
                               quanta: [], qat: 0, qn: 0, endF: 0 } : null;
          this.sentWrites = ex.web_flash_writes_count();
          this.port.postMessage({ type: "ready", rate: ex.web_sample_rate() });
        } catch (err) {
          this.port.postMessage({ type: "error", message: String(err && err.message || err) });
        }
        return;
      }
      const ex = this.ex;
      if (!ex) return;
      if (m.type === "buttons" || m.type === "keys") this.queue.push(m);
      else if (m.type === "enc") { ex.web_enc(m.role, m.n | 0); this.recEvent({ type: "enc", role: m.role, n: m.n }); }
      else if (m.type === "master") { ex.web_master(m.value | 0); this.recEvent({ type: "master", value: m.value }); }
      else if (m.type === "midi") { ex.web_midi(m.data[0] | 0, m.data[1] | 0, m.data[2] | 0); this.recEvent({ type: "midi", data: [m.data[0], m.data[1], m.data[2]] }); }
      else if (m.type === "visible") { this.period = m.on ? 1 / 30 : 1 / 4; this.recEvent({ type: "visible", on: m.on }); }
      else if (m.type === "mark") this.recEvent({ type: "mark", note: m.note });
      else if (m.type === "rec-stop" && this.rec) {
        if (this.rec.on) { this.recFlush(); this.recEvent({ type: "stop" }); this.rec.on = false; this.rec.endF = this.f; }
      } else if (m.type === "rec-dump" && this.rec) {
        this.port.postMessage({
          type: "rec",
          data: { frames: this.rec.on ? this.f : this.rec.endF, events: this.rec.events, seconds: this.rec.seconds,
                  partial: this.recPartial(), quanta: this.recQuanta() },
        });
      } else if (m.type === "import") this.importBackup(m.objects);
      else if (m.type === "export") this.exportProject();
    }

    // a device backup, object by object, through the firmware's own storage code (felucca_web.c web_bk_import)
    importBackup(objects) {
      const ex = this.ex, mem = ex.memory.buffer;
      const ptr = ex.web_bk_buf_ptr(), cap = ex.web_bk_buf_size();
      let rc = 0;
      for (const o of objects || []) {
        const bytes = o.bytes instanceof Uint8Array ? o.bytes : new Uint8Array(o.bytes || []);
        if (bytes.length > cap) { rc = 2; break; }
        new Uint8Array(mem, ptr, bytes.length).set(bytes);
        rc = ex.web_bk_import(o.id | 0, bytes.length) | 0;
        if (rc) break;
      }
      this.port.postMessage({
        type: "sectors",
        sectors: storeSectors(new Uint8Array(mem, ex.web_nor(), ex.web_nor_size())),
      });
      this.port.postMessage({ type: "imported", rc });
    }

    // the live project as project_store_t (the page wraps it in a project file)
    exportProject() {
      const ex = this.ex;
      const n = ex.web_project();
      if (!n) { this.port.postMessage({ type: "project", error: "the live project did not pack" }); return; }
      const bytes = new Uint8Array(ex.memory.buffer, ex.web_bk_buf_ptr(), n).slice();
      this.port.postMessage({ type: "project", bytes }, [bytes.buffer]);
    }

    publish() {
      const ex = this.ex, mem = ex.memory.buffer;
      const msg = { type: "frame" };
      const leds = [ex.web_lit_buttons(), ex.web_lit_keys(), ex.web_dim_buttons(), ex.web_dim_keys(), ex.web_dim_level(),
                    ex.web_breath_buttons(), ex.web_breath_keys(), ex.web_mid_keys ? ex.web_mid_keys() : 0];
      const an = ex.web_anim_levels ? ex.web_anim_levels() : 0;   // the power-on sweep: each LED's level
      const anim = an ? Array.from(new Uint8Array(mem, an, ex.web_anim_count())) : null;
      const key = leds.join(",") + (anim ? ";" + anim.join(",") : "");
      if (key !== this.sentLeds) {
        this.sentLeds = key;
        msg.leds = leds;
        msg.anim = anim;
      }
      if (this.frames >= 44100) {                     // the share of real time spent running the device
        msg.load = this.busy / (this.frames / 44.1);
        this.busy = this.frames = 0;
      }
      const transfer = [];
      const draws = ex.web_screen_draws();
      if (draws !== this.sentDraws) {
        this.sentDraws = draws;
        msg.fb = new Uint16Array(mem, ex.web_screen(), 240 * 240).slice();
        transfer.push(msg.fb.buffer);
      }
      const writes = ex.web_flash_writes_count();
      if (writes !== this.sentWrites) {               // a save is done within one main-loop pass: whole here
        this.sentWrites = writes;
        msg.sectors = storeSectors(new Uint8Array(mem, ex.web_nor(), ex.web_nor_size()));
      }
      if (msg.leds || msg.fb || msg.sectors || msg.load !== undefined) this.port.postMessage(msg, transfer);
    }

    // A click's down and up can arrive within one render quantum, but the FM-1's contacts never close for less than
    // its debounce, and the UI reads them once a frame: each change of the buttons or the keys stays HOLD_MS of the
    // device's time before the next one goes in (a tap: down for a frame and more, then up).
    input() {
      const ex = this.ex, now = ex.web_now_ms();
      while (this.queue.length && now - this.nextOk >= 0) {
        const m = this.queue.shift();
        if (m.type === "buttons") { ex.web_buttons(m.mask >>> 0); this.recEvent({ type: "buttons", mask: m.mask }); }
        else { ex.web_keys(m.mask >>> 0); this.recEvent({ type: "keys", mask: m.mask }); }
        this.nextOk = now + HOLD_MS;
      }
    }

    process(inputs, outputs) {
      const ex = this.ex;
      if (!ex) return true;
      if (this.queue.length) this.input();
      const out = outputs[0], n = out[0].length;
      const rec = this.rec, tAll = rec && rec.on ? clock() : 0;
      const t0 = clock();
      ex.web_render(n);
      const run = clock() - t0;
      this.busy += run;
      this.frames += n;
      const l = new Float32Array(ex.memory.buffer, ex.web_out_l(), n);
      const r = new Float32Array(ex.memory.buffer, ex.web_out_r(), n);
      out[0].set(l);
      if (out[1]) out[1].set(r);
      if (currentTime - this.lastFrame >= this.period) {
        this.lastFrame = currentTime;
        this.publish();
      }
      if (rec) {
        if (rec.on) {
          const step = recQuantum(rec.audio, l, r);
          if (!rec.q) rec.bucketF = this.f;
          rec.q++;
          this.recHealth(run, clock() - tAll, rec.audio.qp, step);   // every quantum's health: a mark points at these
          if (rec.q >= REC_BUCKET) this.recFlush();
        }
        this.f += n;
      }
      return true;
    }
  }
  registerProcessor("felucca", Felucca);
}
