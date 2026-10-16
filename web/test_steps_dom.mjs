// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The step grid and the step rows, as the browser builds them (editor.html's UI half, which web/test_web.mjs does
// not reach: it evaluates only the protocol block). This locks the markup, so the incremental updates -- the grid's
// two spans kept per button and fillStep writing a field only when it changed -- cannot change what the page
// renders. Run from the repo root:  node web/test_steps_dom.mjs
import vm from "node:vm";
import { fileURLToPath } from "node:url";
import { readFileSync } from "node:fs";
import { join } from "node:path";

const HERE = fileURLToPath(new URL(".", import.meta.url));
let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(64)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };
const eq = (a, b) => a === b;

/* a DOM just big enough: element tree, class list, attributes, value / checked, textContent */
class Elem {
  constructor(tag) { this.tagName = tag.toUpperCase(); this.attrs = {}; this.children = []; this._text = ""; this.parent = null; this._cls = new Set(); this.dataset = {}; this.value = ""; this.checked = false; this._listeners = {}; }
  addEventListener(t, fn) { (this._listeners[t] = this._listeners[t] || []).push(fn); }
  removeEventListener() {}
  fire(t, ev) { (this._listeners[t] || []).forEach((f) => f(ev)); }
  setAttribute(k, v) { this.attrs[k] = String(v); if (k === "class") this.className = v; if (k.startsWith("data-")) this.dataset[k.slice(5)] = v; }
  getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; }
  get classList() { const s = this._cls; return { add: (c) => s.add(c), remove: (c) => s.delete(c), toggle: (c, on) => { if (on === undefined) { if (s.has(c)) s.delete(c); else s.add(c); } else on ? s.add(c) : s.delete(c); }, contains: (c) => s.has(c) }; }
  get className() { return [...this._cls].join(" "); }
  set className(v) { this._cls = new Set(String(v).split(/\s+/).filter(Boolean)); this.className ? this.attrs.class = this.className : delete this.attrs.class; }
  get textContent() { return this._text; }
  set textContent(v) { this._text = String(v); }
  append(...ks) { for (const k of ks.flat(9)) { if (k instanceof Elem) { k.parent = this; this.children.push(k); } else if (k != null && k !== false && k !== "") this.children.push({ text: () => String(k), parent: this }); } }
  prepend(...ks) { const n = this.children.length; this.append(...ks); this.children.unshift(...this.children.splice(n)); }
  replaceChildren(...ks) { for (const c of this.children) c.parent = null; this.children = []; this._text = ""; this.append(...ks); }
  contains(n) { for (let e = n; e; e = e.parent) if (e === this) return true; return false; }
  querySelectorAll(sel) { const [, tag, at] = /^(\w+)\[([\w-]+)]$/.exec(sel) || []; const key = at && at.startsWith("data-") ? at.slice(5) : at; const out = []; const walk = (e) => { if (!tag || (e.tagName === tag.toUpperCase() && e.dataset[key] !== undefined)) out.push(e); e.children.forEach((c) => c instanceof Elem && walk(c)); }; walk(this); return out; }
  get outerHTML() {
    const a = Object.entries(this.attrs).map(([k, v]) => ` ${k}="${v}"`).join("");
    const inr = this._text + this.children.map((c) => (c instanceof Elem ? c.outerHTML : c.text())).join("");
    return `<${this.tagName.toLowerCase()}${a}>${inr}</${this.tagName.toLowerCase()}>`;
  }
}
const byId = new Map();
const document = { createElement: (t) => new Elem(t), getElementById: (id) => byId.get(id), documentElement: new Elem("html") };
const active = null;

/* the page's own helpers, sliced out of editor.html and run against that DOM: the protocol block (for stepOn /
   stepNotes / hitsText) and the step UI half */
const html = readFileSync(join(HERE, "editor.html"), "utf8");
const pa = html.indexOf("/*PROTO-BEGIN*/"), pb = html.indexOf("/*PROTO-END*/");
const proto = html.slice(pa, pb);
const ui = html.slice(html.indexOf("/* ===================================================================== UI === */"));
const cut = (name, from = ui) => { const i = from.indexOf(`function ${name}(`); const j = from.indexOf("\n}\n", i); return from.slice(i, j + 3); };
const el = (() => { const i = ui.indexOf("const el = (tag"); return ui.slice(i, ui.indexOf("\n};\n", i) + 3); })();
const half = (name) => { const i = ui.indexOf(`const ${name} = `); return ui.slice(i, ui.indexOf("\n", i)); };
const src = [
  proto,
  el,
  half("stepNotes"), half("stepOn"),
  cut("renderGrid"), cut("fillStep"), cut("updateStepRows"), cut("stepRow"), cut("renderSteps"),
  "const stepLen = () => Math.max(1, Math.min(dev.info.nstep, dev.dump.p[pSlen()] || 16));",
  "const pSlen = () => 29;",
  "const TIMES = [\"NOTE\", \"TIE\", \"REST\"];",
].join("\n");
const sandbox = { document, $: (id) => byId.get(id), get activeElement() { return active; }, dev: null,
  t: (k) => k, navigator: {}, performance: { now: () => 0 }, matchMedia: () => ({ addEventListener() {} }) };
const V = vm.runInNewContext(src + "\n;({ el, renderGrid, fillStep, updateStepRows, stepRow, renderSteps })", sandbox, {});

const step = (o) => ({ index: 0, n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0, hit: 0, acc: 0, chance: 100, ratchet: 1, ...o });
const dev = {
  info: { nstep: 16, chance: true, ratchet: 4, pe0: 95 },
  dump: { p: new Array(127).fill(0), engine: 0, preset: 0 },
  steps: [
    step({ n: 1, notes: [60, 0, 0, 0], time: 0, vel: 96, hit: 0b00000001, acc: 0b00000001, chance: 50, ratchet: 3 }),
    step({ n: 2, notes: [64, 67, 0, 0], time: 0, vel: 80, flags: 3 }),
    step({ n: 0, time: 1, vel: 0 }),
    step({ n: 0, time: 2, vel: 0 }),
  ],
};
sandbox.dev = dev;
byId.set("stepgrid", new Elem("div"));
byId.set("steptables", new Elem("div"));
byId.set("noteshelp", new Elem("span"));

/* one render, then the shape of everything */
V.renderSteps();
const grid = byId.get("stepgrid").outerHTML;
const cell = (i, cls, n, x) => `<button type="button" data-i="${i}"${cls ? ` class="${cls}"` : ""}>`
  + `<span class="n">${n}</span><span class="x">${x}</span></button>`;
ok(grid === "<div>" + [
  cell(0, "on beat", "1 x3", "C4 >KICK"),
  cell(1, "on acc", "2 > ~", "E4 G4"),
  cell(2, "tie", "3", "—"),
  cell(3, "", "4", ""),
  cell(4, "beat", "5", ""),
  cell(5, "", "6", ""),
  cell(6, "", "7", ""),
  cell(7, "", "8", ""),
  cell(8, "beat", "9", ""),
  cell(9, "", "10", ""),
  cell(10, "", "11", ""),
  cell(11, "", "12", ""),
  cell(12, "beat", "13", ""),
  cell(13, "", "14", ""),
  cell(14, "", "15", ""),
  cell(15, "", "16", ""),
].join("") + "</div>", "steps dom: the grid is 16 buttons with a .n and a .x span each, nothing else");
ok(grid.includes('<span class="n">1 x3</span>'), "steps dom: the cell number carries its accent and ratchet marks");
ok(grid.includes('<span class="x">C4 >KICK</span>') && grid.includes('<span class="x">E4 G4</span>')
   && grid.includes('<span class="x">—</span>') && /<span class="x"><\/span>/.test(grid),
   "steps dom: a note cell shows its notes, a tie shows —, a rest shows nothing");

/* the rows' fields, and what a second, unchanged fill leaves alone */
const rows = byId.get("steptables").querySelectorAll("tr[data-i]");
ok(rows.length === 16, "steps dom: one row per step in LEN (16), the empty ones blank");
const first = rows[0];
const seen = (tr) => [tr._c.notes.value, tr._c.hits.value, tr._c.time.value, tr._c.acc.checked, tr._c.sl.checked, tr._c.vel.value, tr._c.chance.value, tr._c.ratchet.value, tr.className].join("|");
const before = seen(first);
dev.steps[1] = step({ n: 1, notes: [55, 0, 0, 0], time: 0, vel: 90 });
V.updateStepRows();
ok(seen(first) === before, "steps dom: a fill of unchanged data writes no field (and breaks no other row)");
ok(seen(rows[1]).startsWith("G3||0|false|false|90|100|1|"), "steps dom: a changed row is updated (its note name, velocity, the reset fields)");
ok(rows[1]._c.hits.value === "" && rows[1]._c.acc.checked === false && rows[1]._c.ratchet.value === "1",
   "steps dom: the empty hits, accent and ratchet fields reset to their defaults");
ok(rows[0].className === "" && rows[2].className === "off" && rows[3].className === "off",
   "steps dom: only a step that does not play carries .off");
ok(seen(rows[0]) === "C4|>KICK|0|false|false|96|50|3|",
   "steps dom: the drum lane's accented hit, the chance and the ratchet are read back as set");
const types = (tr) => [tr._c.notes.attrs.type, tr._c.vel.attrs.type, tr._c.acc.attrs.type, tr._c.chance.attrs.type,
  tr._c.time.children.length, [...tr._c.time.children].map((o) => o.attrs.value).join(","),
  tr._c.ratchet.children.length, [...tr._c.ratchet.children].map((o) => o.attrs.value).join(",")].join("|");
ok(types(rows[0]) === "text|number|checkbox|number|3|0,1,2|4|1,2,3,4",
   "steps dom: the row's inputs keep their types, NOTE/TIE/REST and the ratchet 1..4");

console.log(fails ? `STEPS DOM TESTS FAILED (${fails})` : "steps dom tests passed");
process.exit(fails ? 1 : 0);
