# Parameter, ID and lock contract

> **Read this first.** This is the current source of truth for Araminta's parameter IDs, special IDs,
> aliases and PITCH locks. It describes the code that ships with 3.2 (FUN16). `web/EDITOR_PROTOCOL.md` and
> `CHANGELOG.md` are cumulative: their sections are labelled by the version that introduced them, and an
> older section can describe an earlier design. When either disagrees with this file, the code wins and this
> file is the one to update.

The single rule behind the tables: a number has meaning only in the table that produced it. `p[]` is
indexed only by real P_* ids; every other space is either a separate storage area or a UI-only row id.

## 1. ID spaces

| Space | Members and values | Indexes `p[]`? | Persisted where | Carried by the wire? | Shown in the UI |
| --- | --- | --- | --- | --- | --- |
| Real per-track parameters | `P_*`, ids 0..126; `P_COUNT` is 127; `P_E0..P_E7` are the last eight, 119..126 | yes, `t->p[id]` | projects (`np = P_COUNT`, by-count migration) and user presets | yes: `SET` / `GET` / `TRACK_PARAM` id 0..126, `DESC` / `DUMP` | sound pages, EDIT 1/2, AUTOMATION when `motion_param()` allows it |
| Global parameters | `G_*`, ids 0..30; `G_COUNT` is 31 | no, `song.g[id]` | project `g[]`; some are device settings | yes: `SET` / `GET` with parameter index 1 | GLO / MENU pages |
| Lock-only PITCH | `MSEQ_PITCH` 127 (`== P_COUNT`) | **never** | project motion block only, id 127 with bit 7 set; not in user presets | `MOTION` ops; project motion record | AUTOMATION `PITCH` row; SCL K4 |
| Motion-only FX targets | `MFX_TIME..MFX_DAMP`, ids 49..53; numerically `P_M1SRC..P_M2DST`; `MFX_TSPEED` / `MFX_TWOW` are ids 41 / 42 (`P_TAPEMIX` / `P_LSMOOTH`) | **never as a target**: `fx_target_ptr()` reaches `song.g` or a bus | project motion records; long DLY TIME takes ids 54..59 on disk; TAPE SPEED packs 25..400 into one byte | `MOTION` ops; project motion record | AUTOMATION FX rows; EFFECTS / DEST lists |
| UI-only step rows | `EV_VEL` 251, `EV_NUDGE` 252, `EV_CHANCE` 253, `EV_RATCH` 254; 0xFF = none | no | the step's own `chance` / `ratchet` / `nudge` / `vel` fields | **never** | AUTOMATION rows |
| Page-only CHORD cards | `PG_STRUM` 252, `PG_STROKE` 253 | no | none; the edit packs `P_ALLOC` | **never** | CHORD K3 / K4 |

Rules that fall out:

- `param_real(id)` is `id < P_COUNT`; `param_lock_only(id)` is `id == MSEQ_PITCH`; `param_id_known(id)` is
  either. These helpers in `core.h` are the classification to use, not ad-hoc range tests.
- `MOTION_ID(e)` masks off bit 7 (`MOTION_LOCK`). A PITCH lock's `param` byte is `0xFF`, so never treat
  `param` as a signed id or as `p[]` index.
- `252` is `EV_NUDGE` in AUTOMATION and `PG_STRUM` on CHORD; `49` is the matrix `SRC1` parameter in
  `SET` / `GET` and `MFX_TIME` in `MOTION`. Context decides the namespace; never pass a number from one
  namespace to another.
- `0` is a valid PITCH value. Presence is the motion record (and `track_t.plk_fit`), not a nonzero value.
- `EV_*` and `PG_*` are deliberately outside `param_id_known()`: `motion_param()`, `motion_put()` and the
  project motion reader reject them.

Motion storage (`core.h`, `motion.c`): one record per `(track, step, id)`, either an automation event or a
lock. `MOTION_MAX` is 192 records in RAM shared by the four tracks; `MOTION_FILE_MAX` is 88 saved in a
project; user presets store no motion. The record's `param` byte is `id | MOTION_LOCK`, and `MOTION_ID`
recovers the id. An automation event holds its value; a lock sounds on its step and goes back at the next.

## 2. PITCH (`MSEQ_PITCH`, 127)

Representation:

- One motion record per **track and step**, id `127 | MOTION_LOCK`, value -24..24 semitones. It is one
  record per step, not one per note.
- It never indexes `p[]`. `PITCH_DESC` in `params.c` exists only so AUTOMATION and the graph can draw it.
  It is not a user-preset field (user presets store no motion).
- At playback `track_t.plk_pit` holds the value and `plk_fit` is the presence bit. Both reset at every
  step and in `motion_restore()`; `plk_fit` is what makes a stored `0` override TRN.

Playback (`seq.c`, `seq_step()`): on a kit (an engine whose `keys` maps the keys, e.g. DRUM), PITCH is
ignored: stored notes and lane hits play as stored, and TRN is ignored too. Otherwise, if `plk_fit`, every
stored note of the step gets `+ plk_pit`, with **no** TRN and **no** scale snap; without a lock, `P_TRANS`
is added and (for MAP SEQ / ALL / CURR) the result is snapped. Drum hits are not in this note loop (see the
Kit row).

Capture (`motion.c`, `motion_capture()`): only when REC is armed **and playing** on the selected track,
no song chain is running (`rec_on()`), the track is not a kit, and only for `P_TRANS`. It uses the playing
step, or the next one once past that step's midpoint. The value written is a lock, never an automation event:

1. start with `off = value` (the TRN value);
2. for MAP SEQ / ALL / CURR on a non-kit step with at least one note, `off = scale_snap(first note + value)
   - first note`;
3. clamp to -24..24 and call `motion_set_lock()`.

### Acceptance table

| Case | Current behaviour | Code path |
| --- | --- | --- |
| MAP OFF, REC + TRN | No scale fit: the lock is the TRN value in semitones. Playback adds it and skips TRN. | `motion.c motion_capture()`; `seq.c seq_step()` |
| MAP SEQ, REC + TRN | On a note step, the lock is the fitted offset of the **first stored note** (C4 +1 in C major -> 0 ST). On an empty or hit-only step, the lock is the raw TRN value. Playback never snaps it. | `motion.c motion_capture()`; `seq.c seq_step()` |
| MAP CURR, REC + TRN | Same capture fit as ALL (`qn_mode()` maps CURR to ALL), plus CURR's shared ROOT / SCALE. Playback still skips snap because a PITCH lock is present. | `seq.c qn_mode()`; `params.c scale_lock_after()`; `motion.c motion_capture()` |
| MAP ALL / KBD | ALL fits like SEQ; KBD does not fit (only SEQ / ALL / CURR are in the capture condition). | `motion.c motion_capture()` |
| Value 0 | A real lock, not "no lock": stored with value 0, `plk_fit = 1`, it overrides TRN (and suppresses a SEQ / ALL / CURR snap) so the step plays as stored. Do not delete or skip it because the value is zero. | `core.h track_t.plk_fit`; `motion.c motion_step()`; `seq.c seq_step()` |
| REC on | Requires armed + playing + selected track + no chain + not a kit. Writes the lock at the nearest step; no `P_TRANS` motion event is created. The TRN knob itself has already moved `p[P_TRANS]` before `motion_capture()` is called, so other steps without a lock follow the new TRN. | `motion.c motion_capture()`; `ui_input.c` page knob; `editor.c ED_SET` |
| REC off / stopped / chain / other track | No PITCH capture. TRN is an ordinary `P_TRANS` edit. An existing PITCH lock on that step still overrides TRN. | `motion.c motion_capture()`; `seq.c rec_on()` |
| Manual edit (AUTOMATION value, `MOTION` op 5, K3 / + ADD) | Changes the semitone value and leaves the record a lock. It is not fitted, may be off-scale, and `0` is allowed. `motion_move()` forces `MOTION_LOCK` for PITCH; OCT+ cannot turn it into automation ("NOW A LOCK"). On a kit it is refused (no record). | `motion.c motion_move()` / `motion_put()`; `ui_events.c ev_oct()` |
| Chord | One lock per step, applied to every stored note. Capture fits against the first stored note; chord keys store their tones ascending, so that is the lowest tone for a keyed chord. Notes entered one by one keep their stored order. | `motion.c motion_capture()`; `chord.c chord_make()`; `seq.c seq_step()` |
| Kit (DRUM or an engine that maps keys) | PITCH is ignored. Capture and `MOTION` op 5 / SCL K4 refuse to create a PITCH lock on a kit; a stored kit PITCH lock is inert. Note events and lane hits play as stored, and TRN is ignored too. | `motion.c motion_capture()` / `motion_put()`; `seq.c seq_step()` |
| Project roundtrip | The record is written with id 127 and bit 7, counts toward the 88 saved records, survives load, and validates as a lock in -24..24. Older firmware rejects the project because 127 is not one of its p[] ids. | `project.c proj_pack()` / `proj_unpack()`; `motion.c motion_valid()` |
| Editor `MOTION` op 3 (set event) | Refused for 127: `motion_set_event()` -> `motion_put(kind 0)` returns 1 (rc 1). PITCH cannot be stored as automation. | `editor.c` MOTION op 3; `motion.c motion_put()` |
| Editor `MOTION` op 4 (delete) | Deletes the PITCH lock like any other record (rc 0 when arguments are valid), whether it was created by capture or op 5. | `editor.c` MOTION op 4; `motion.c motion_delete_event()` |
| Editor `MOTION` op 5 (set lock) | Writes or changes the PITCH lock on a non-kit track (rc 0 when the step and value are valid). This is the only write op for 127; a kit returns rc 1. | `editor.c` MOTION op 5; `motion.c motion_set_lock()` |
| Editor `MOTION` op 8 (list every record) | Lists the PITCH lock with id 127 and its kind byte 1. `MOTION` op 7 replies the same kind byte after the query; ops 1..4 keep their older reply shape. | `editor.c ed_motion_reply()`; `web/EDITOR_PROTOCOL.md` MOTION |

`MOTION` op 6 (`clear the step's locks`, step 127 = every step) also removes PITCH locks; automation
events stay. AUTOMATION's DELETE (`EDIT`) removes the row's record; PLAY OFF bypasses its playback.

## 3. Other special cases

### MFX_*: motion-only FX targets

- `MFX_TIME..MFX_DAMP` are `P_M1SRC..P_M2DST` numerically (49..53): DLY TIME / FDBK / COLOR and REV SIZE /
  DAMP. Their **motion** meaning is reached through `fx_target_ptr()` (usually `song.g`, or the CHAIN
  tank when this track owns the reverb chain), never through `p[]`.
- `MFX_TSPEED` / `MFX_TWOW` are `P_TAPEMIX` / `P_LSMOOTH` numerically (41 / 42): TAPE SPEED and WOW. They
  reach `song.g[G_TSPEED]` / `[G_TWOW]` through `fx_target_ptr()`. `P_TAPEMIX` is otherwise unused;
  `P_LSMOOTH` keeps its LFO S&H smoothing meaning as a real parameter, but automation never used it.
- They are normal motion targets: automation or locks, values TIME 65..1550, FDBK 0..120, others 0..127,
  TAPE SPEED 25..400. `MOTION` / AUTOMATION use them; `SET` / `GET` on 49 edit the matrix's `SRC1`, a
  different parameter.
- TAPE ownership: in CHAIN only the owner track may use SPEED / WOW; in MASTER every track may; in SEND the
  first track to claim one keeps it (the shared-knob rule). The value sounds from the next audio block.
- On disk, a DLY TIME value above 255 uses ids 54..59 for its high byte (`MFX_DAMP + (value >> 8)`); 54..59
  are not real motion targets in that record. TAPE SPEED's 25..400 is packed into its value byte, exact at
  25 / 100 / 400 (`proj_tspeed_put` / `proj_tspeed_get`). Rounded encoding and decoding keep the first
  quantized value stable through repeated saves. `project.c proj_pack()` / `proj_unpack()` must agree.
- Files: `core.h`, `fx.c` (`fx_target_ptr`, `fx_target_desc`, `fx_target_stage`), `motion.c`
  (`motion_param`, `motion_valid`, `motion_capture_base`, `motion_restore`, `motion_fx_route`),
  `ui_events.c` (`FX_DST`, `ev_name`, `pk_build_auto`), `ui.c lock_id()`, `project.c`, `editor.c`,
  `web/editor.html`, `tests/param_contract_test.c`.

### FUN18 MASTER owners

- Each of the ten EFFECTS stages has one MASTER owner track (0 = none), packed as a 3-bit field in the chain
  header's spare bytes 1..3 (stages 0..7) and the motion header's spare byte 3 (stages 8..9). Runtime:
  `core.h fx_master_tr[]`; project: `project_t.fx_master[]`.
- Inserts (`INS_DIST..INS_CHOR`) keep their per-track CHAIN / SEND and add a private stereo MASTER instance.
  DLY / REV / SLCR / TAPE are exclusive while MASTER: their per-track routes are suppressed and the list shows
  USED. The last track to assign a MASTER stage supplies the EFFECTS order (`fx_master_order_tr`). Its index
  plus one is stored in three FM6 high bits after track 0's EFFECTS order (`project_t.fx_master_order`). Zero
  identifies older saves and uses their first owner track. Autosave tracks both ownership and this order source.
- UI: `fx_list_mode()` shows the owner's stage as MASTER; K3 sets / clears it; other tracks cannot take over.
  Files: `fx.c` (`fx_master_run`, `fx_list_mode`, `ior_knob`), `project.c` (`proj_pack` / `proj_unpack`),
  `tests/ui_test.c`, `tests/inschain_test.c`, `tests/project_test.c`.

### EV_*: AUTOMATION's own step rows

- `EV_VEL` 251, `EV_NUDGE` 252, `EV_CHANCE` 253, `EV_RATCH` 254; `0xFF` is none. They are row kinds, not
  motion ids and not `p[]` ids.
- Their values live in `step_t`: `vel`, `step_nudge()`, `step_chance()`, `step_ratchet()`. Projects save
  those step fields; no EV id is ever written to a motion record or a parameter wire.
- `ev_id_ok()`, `ev_name()`, `ev_id_pos()` / `ev_id_at()`, `ev_skind()`, `ev_sval()`, `ev_sput()` and the
  `EV_ROWS` sizing in `ui_events.c` must stay in agreement. `ev_id_pos()` puts CHANCE, RATCH, NUDGE and VEL
  at 0..3, real parameters at 4..`P_COUNT + 3`, and PITCH at `P_COUNT + 4` (after every real id);
  `ev_id_at()` must invert it, and `ev_id_step()` walks that order.
- Files: `ui_events.c`, `core.h` (`step_t`), `seq.c` (playback), `ui_draw.c` / `ui_graph.c`, tests.

### PG_*: CHORD's page cards

- `PG_STRUM` 252 and `PG_STROKE` 253 are page ids for CHORD K3 / K4. They are not stored, not `p[]`, not
  motion ids, and never reach the wire.
- `page_desc()` synthesizes `STRUM_DESC` / `STROKE_DESC` and a temporary value; the knob handler writes the
  pair back as one `P_ALLOC = sweep_pack(rate, stroke)` and returns, so it never reaches `*vp` or
  `motion_capture()`.
- The numbers collide with `EV_NUDGE` / `EV_CHANCE`. That is safe only because each is read through its
  own table: never pass a PG id to `ev_*`, `motion_*` or `param_*`, and never pass an EV id to `page_desc()`.
- Files: `params.c` (`PG_STRUM`, `PG_STROKE`, `PAGES`, `page_desc`), `ui_input.c` (the page knob write),
  `core.h` (sweep helpers), `chord.c`, `tests/param_contract_test.c`.

### P_ALLOC: the packed STRUM / STROKE sweep

- One real parameter (id 43) stores both CHORD knobs. Encoding: `0` and `1` are OFF (legacy ROT / REUSE);
  otherwise `2 + (rate - 1) * 3 + stroke`, rate 1..7 = 1/8 .. 32T, stroke 0 DOWN / 1 UP / 2 ALT; new values
  are 2..22.
- `sweep_rate()` / `sweep_stroke()` read 0 for 0/1; `sweep_pack(0, ...)` writes 0. The device edits it only
  through PG_STRUM / PG_STROKE; the editor sees it as the `SWEEP` enum (`OFF, OFF, 1/8 DOWN, ...`).
- Values above 22 are not produced by this UI; `sweep_rate()` then reads a rate > 7 and `chord.c` treats the
  sweep as off. Do not reuse 0/1 for a new sweep.
- Files: `core.h` (`sweep_rate`, `sweep_stroke`, `sweep_pack`), `params.c` (`N_SWEEP`, `P_ALLOC`, `page_desc`),
  `ui_input.c`, `chord.c`, `project.c` / `upreset.c` (ordinary P_* storage), `editor.c`, tests.

### P_CHOR: EFFECTS list slots, not a chorus send

- `P_CHOR` (34, label `FXPOS`) packs where DELAY and REVERB appear in the EFFECTS list: bit 6 marks the
  layout, low 3 bits are DLY's non-SLCR slot (0..7), next 3 bits REV's. SLICER's absolute slot is
  `track_t.fx_sl_pos` (0..8, default 8), not `P_CHOR`.
- Values below 64 are legacy bus-chorus sends: they load as DLY at 6, REV at 7, SLCR at 8. The project
  loader maps old `P_CHOR` motion values below 64 to `P_IHMIX` (insert CHORUS mix). `P_IHMIX` is the chorus
  mix; `P_CHOR` is only the list layout.
- Files: `fx.c` (`FX_POS_MARK`, `fx_pos_get`, `fx_pos_set`, `fx_list_get`), `core.h` (`FX_POS_DEF`),
  `params.c`, `project.c` (the old CHO-send migration), `ui_graph.c`, `ui_input.c`.

### P_QUANT: MAP, and CURR's shared ROOT / SCALE

- `P_QUANT` (27) is MAP: 0 OFF, 1 ALL, 2 KBD, 3 SEQ, 4 CURR; the UI shows 0, 1, 4, 2, 3
  (`QUANT_ORDER`). CURR quantises like ALL (`qn_mode()` maps it) and links ROOT / SCALE across tracks
  whose MAP is CURR.
- `track_t.scl_stash[3]` / `scl_stash_ok` hold the root and scale from before a CURR lock overwrote them.
  `scale_lock_stash()`, `scale_lock_copy()`, `scale_lock_push()`, `scale_lock_restore()` and
  `scale_lock_after()` implement entering, following edits, and leaving CURR.
- Projects save CURR as ALL plus the per-track lock bit; the current loader turns that back into CURR.
  Older firmware reads it as ALL plus SCALE LOCK. User presets save CURR as ALL (no link bit).
- Files: `core.h`, `seq.c` (`qn_mode`, scale snap), `params.c` (`N_QUANT`, `QUANT_ORDER`, the scale-lock
  functions), `project.c` (lock bits / load), `upreset.c` (CURR -> ALL), `editor.c` (`scale_lock_after()`
  on `SET`).

### P_IORD: packed insert order and send flags

- `P_IORD` (96) is a 16-bit value: bits 0..9 are the six-stage Lehmer order 0..719; bits 10..15 are one
  flag per stage (0 = in the chain, 1 = parallel SEND). Default 24 = DIST, CRUSH, FILTER, PHASR, FLANG,
  CHOR.
- `iord_perm_of()` / `iord_set_perm()` read and write only the order; `ins_is_send()` / `ins_send_set()`
  own the flags. `GET` / `SET`, `TRACK_DUMP`, `CHANGED` and `DUMP` expose the order only
  (`iord_perm_of`), so flags stay on the device. `MOTION` excludes `P_IORD` because 0..719 is outside the
  track-value range.
- Projects store two bytes (low order byte, then flags) when `np > 111`; user presets use the same two-byte
  pack in `upreset.c` (the editor's `UP_PUT` accepts an order 0..719, no flags).
- Files: `core.h` (`IORD_DEF`), `fx.c` (`IORD_PERM`, `IORD_SEND`, `ins_order_get/set`, `ior_pct_id`),
  `params.c` (wire helpers), `project.c`, `upreset.c`, `editor.c`, `ui_graph.c`, `ui_input.c`.

### G_SLOWN: legacy owner vs per-track SEND mask

- `G_SLOWN` is a global, not `p[]` and not motion. Current RAM can hold either form:
  - legacy `0` = all tracks SEND;
  - legacy `1..4` = that track has the SLICER CHAIN, the others are USED (until sanitized);
  - mask `0..15`: bit `k` = track k is SEND (1) or CHAIN/private (0). `0` still means all SEND
    (equivalent to mask 15).
- `sl_own_sanitize()` converts legacy 0 -> 15 and 1..4 -> `15 ^ (1 << (v - 1))`; invalid -> 15. It runs at
  FX init and when the UI toggles a track's USE AS. `sl_trk_send()`, `sl_trk_chain()` and `sl_trk_used()`
  accept both forms so a project that was just loaded also behaves.
- `project.c` saves the mask in the DX pad with the `0x10` marker (and keeps a readable legacy owner in
  the SCL lock bits when exactly one track owns CHAIN); on load, the marked mask wins. `GP[G_SLOWN]`
  describes the 0..15 mask (`0` and `5..15`; `1..4` are accepted legacy owner values) and is not a page
  control.
- Files: `core.h` (`sl_own_sanitize`, `sl_trk_*`), `fx.c` (`bus_own`, `bus_own_sanitize`), `project.c`,
  `ui_input.c` (the toggle), `slicer.c`, `params.c` (legacy descriptor).

### P_MUTE: PLAY ON / OFF / SOLO

- `P_MUTE` (40) is a real parameter with append-only values: 0 `PLAY_ON`, 1 `PLAY_OFF`, 2 `PLAY_SOLO`.
  The UI order is OFF, ON, SOLO (`PLAY_ORDER`), but the stored values stay 0/1/2.
- OFF silences the track; SOLO silences every track that is not SOLO. `voice.c` / `perform.c` must use the
  stored values, not the display order. Old projects with 0/1 keep their meaning.
- The editor's `TRACK_MIX` exposes a Boolean mute (`p == PLAY_OFF`): setting it writes OFF and clearing it
  changes OFF back to ON. A SOLO track reads as not muted and is left as SOLO, so SOLO is device-only.
- Files: `core.h` (`PLAY_*`), `params.c` (`N_PLAY`, `PLAY_ORDER`), `voice.c`, `perform.c`, `editor.c`,
  `ui_layer.c`, `ui_graph.c`.

### P_LSMOOTH: reused P_PRIO id

- `P_LSMOOTH` (42) is the old `P_PRIO` id, now the LFO WAVE S&H smoothing amount (0..127). The ID stayed
  so no format changes; the meaning did.
- It is read only when `P_LWAVE == 4` (S&H); otherwise the value is kept but the LFO 2 card is blank
  (`page_desc()`). It is not motion (`motion_param()` excludes it).
- Files: `core.h`, `params.c` (`SMTH`, `page_desc`), `voice.c` (`track_lfo_end`), tests (`fun10_test.c`).

### P_ED_FX: DIST tone, not ENV -> FX

- `P_ED_FX` (8, label `TONE`, -64..63) is the DIST stage's tone offset on the drive-linked cutoff; 0
  follows the drive. It is **not** the ENV destination FX. ENV DEST's fourth card is `P_ESYNC`; the page
  deliberately leaves `P_ED_FX` unused.
- It is motion-capable and an FX target (`FX_DST` / `fx_target_stage`). Older 1.5 INSERT type `P_IB`
  (tone) migrates into it (`ins_migrate`).
- Files: `core.h` (comments and migration), `params.c` (`TONE`, DISTORT page), `fx.c` (`ins_run_dist`,
  `fx_target_stage`), `ui_tech.c`, `ui_events.c` (`FX_DST`), `project.c` / `upreset.c` (`ins_migrate`).

### Reserved engine indices and SAMPLE SET 4

- The engine table is append-only; `NENGINES` is 15. Index 1 is `ENGI_DIGITAL`: with `FELUCCA_FM4=0` it is
  reserved and never selectable, and stored DIGITAL sounds load as FM6; with `FELUCCA_FM4=1` it is a live
  engine. Index 13 is `ENGI_SLICE`: always retired, never selectable, stored SLICE sounds load as DRUM
  with the default kit. `NENG_SHOWN` and `ENGINE_ORDER` hide both reserved indices as appropriate.
- `SAMPLE` is engine index 4; do not confuse it with the SAMPLE **set** index 4. Set 4 was `PERC` (the GM
  kit), retired after 1.0.2: a stored SAMPLE sound with set 4 loads as the DRUM engine with the default
  kit (`drum_from_perc`); the SAMPLE / GRAIN source alias for set 4 displays as PIANO (`SMP_SET_ORIG[4] =
  0`), and choosing SAMPLE preset 4 switches the track to DRUM. Favourites map it too.
- Never renumber an engine or a sample set; append new ones, and teach every loader (`eng_ok`,
  `eng_play_as`, `eng_retired`, `retire_remap_params`, `drum_from_perc`, `drum_from_phys`) before storing.
- Files: `core.h`, `engines.c`, `eng_retire.c`, `fm4_convert.c`, `eng_sample.c`, `params.c`,
  `project.c`, `upreset.c`, `settings_persist.c`, `ui.c`, `tools/gen_samples.py`.

## 4. Adding a parameter or lock

Decide the space first. A real P_* is not the same thing as a motion target, a UI row or a page card. Then
touch every representation below; missing one is how a stored value silently changes meaning.

1. **`core.h`**: append the P_* to the enum before `P_E0` (the last eight ids are always `P_E0..P_E7`, and
   `params_by_count()` depends on that); update `P_COUNT`; keep the `_Static_assert`s. A lock-only id goes
   outside `P_COUNT` next to `MSEQ_PITCH`; a UI-only id goes next to `EV_*` / `PG_*` and must not be added
   to `p[]`. Add it to `param_*` helpers only if motion may store it.
2. **`params.c`**: `TP[]` descriptor (label, format, min / max / default, enum names, unit), the engine
   edit table if it is an E parameter, the page table and `page_desc()` if it is editable, `param_fit()` /
   `enum_orig()` aliases, and the `DESC` / `DUMP` output. A page-only card needs a `PG_*` value and a
   handler in `ui_input.c`; it must not fall through to `*vp` / `motion_capture()`.
3. **`motion.c`**: `motion_param()` (what may be recorded), `motion_target_desc()` (range / label),
   `motion_capture_base()` / `motion_restore()` if the value lives outside `p[]`, `motion_valid()` (stored
   range, lock-only rules), `motion_put()` / `motion_capture()` / `motion_step()` / `motion_move()`
   (storage and playback), and `motion_mask()` / `motion_rows()` where relevant. A lock-only id must not
   index `p[]`; reject the automation kind explicitly.
4. **`ui_events.c`**: `ev_id_ok()`, `ev_name()`, `ev_id_pos()` / `ev_id_at()`, `ev_skind()` /
   `ev_sval()` / `ev_sput()` if it is a step field, `pk_build_auto()` / `pk_apply()` (the AUTOMATION
   picker), `FX_DST` for an FX target, and `EV_ROWS` sizing. The K3 picker must still offer PITCH when a
   row has been changed away from it.
5. **Device UI**: `ui.c` `lock_page_ok()` / `lock_id()` for a lockable knob, `ui_input.c` page knob and
   `motion_capture()` call, `scale_lock_after()` for MAP effects, `ui_draw.c` / `ui_graph.c` rendering,
   `ui_tech.c` if it has a graph.
6. **Projects and presets**: `project.c` `proj_pack()` / `proj_unpack()`, `proj_motion_ids()` (old np
   migration), `project_t` fields, and the format magic / size / spare-byte accounting if the wire grows.
   `upreset.c` stores real P_* values and the pattern, but **no motion records**; a lock-only id must not
   be added there. Update both packers if the parameter is ordinary.
7. **Editor**: `editor.c` `ed_desc()`, `GET` / `SET` / `TRACK_PARAM` / `DUMP`, the `MOTION` ops, and any
   `INFO` capability tag. Then `web/EDITOR_PROTOCOL.md` (historical sections stay labelled), the
   `web/editor.html` mock / parser / labels, `tests/descdump.c` if the JSON tables expose it, and the web
   tests.
8. **Tests**: `tests/motion_test.c` (storage, playback, project roundtrip), `tests/param_contract_test.c`
   (ID classification and the special spaces), `tests/editor_test.c` (wire roundtrip, op 3/4/5/6/8),
   `tests/project_test.c` (old and new formats), `tests/ui_test.c` (pages, knobs, lock gestures), and
   `web/test_web.mjs` for the editor mock. Add a negative test for every id that must be refused.
9. **Docs**: `docs/PARAM_CONTRACT.md` (this file), `README.md`, `CHANGELOG.md`, and the editor protocol.
   For a lock-only id, also document the downgrade: older firmware refuses a project that holds an id it
   cannot name (PITCH does this because 127 is its `P_COUNT`, not a p[] index).

## 5. Fast verification

After `./build.sh` once (it creates `build/gen/`), run only what covers the change:

| Command | Covers |
| --- | --- |
| `tests/run_one.sh motion` | motion records, PITCH lock, automation, ARP, song restore |
| `tests/run_one.sh param` | `tests/param_contract_test.c`: ID spaces, MFX / EV / PG, P_ALLOC, PITCH |
| `tests/run_one.sh editor` | editor protocol, `MOTION` ops, MENU settings |
| `tests/run_one.sh project` | project formats and migration |
| `tests/run_one.sh ui` | UI pages, knobs, lock gestures against stubs |
| `tests/run_one.sh web` | `desc.json`, `menu.json`, then `node web/test_web.mjs` |
| `tests/run_one.sh all` | execs the full gate |

`tests/run_one.sh` is the edit loop, not the final gate. The canonical full command is
`tests/run_tests.sh` (or `SANITIZE=1 tests/run_tests.sh` before handoff). If `run_one.sh` is not present,
run `tests/run_tests.sh` and its binaries directly. `build/host/motion_test`,
`build/host/param_contract_test`, `build/host/editor_test`, `build/host/project_test` and
`build/host/ui_test` are the focused binaries it builds. The `param` target fails with a clear message if
`tests/param_contract_test.c` has not landed yet.

## 6. Resolved notes

The acceptance table above is what the code does today. The four items this contract used to leave open are
now resolved; no product question from this contract remains.

1. **Resolved: TRN stays live during PITCH capture.** The PITCH branch does not restore `P_TRANS`
   (`motion.c motion_capture()`), and the callers write `p[P_TRANS]` before calling it
   (`ui_input.c` page knob; `editor.c ED_SET`). The step gets the fitted lock, while every other step without
   a lock follows the moved TRN. `tests/motion_test.c` asserts the same "no reset" behaviour. README,
   CHANGELOG and `EDITOR_PROTOCOL.md` say the knob still moves. If the intended behaviour was instead to
   restore `P_TRANS`, `motion_capture()` and `tests/motion_test.c` must change together.
2. **Resolved: kits ignore PITCH.** Capture and `MOTION` op 5 / SCL K4 refuse a PITCH lock on a kit; a stored
   kit lock is inert. Note events and lane hits play as stored; TRN is ignored too. Existing projects still
   load. The kit test is the sequencer's `e->keys` opt-out; SAMPLE drum-grid slots that are not key-mapped are
   not included.
3. **Resolved: `GP[G_SLOWN]` is 0..15.** The descriptor now matches the mask; `0` and `5..15` are mask values,
   `1..4` are accepted legacy owner values and are sanitized on load/toggle. No page edits it today.
4. **Resolved: `DUMP` exposes the `P_IORD` order.** `GET` / `SET`, `TRACK_DUMP`, `CHANGED` and `DUMP` all
   send 0..719; SEND flags stay on the device. The web editor mock follows.
