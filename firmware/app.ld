/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA memory map.
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * 0x01C00000..0x01C06000 holds .ram_text (code that must run while the flash
 * is busy: the flash driver), then .c1_text (the second core's waiting loop, hal/fm1_cpu1.h: an idle core 1 never
 * fetches from the flash), then .hot_text (AUDIO_RAM, core.h: the render's hottest functions, out of the 32 KB
 * flash cache they would thrash); 0x01C06000..0x01C08000 stays for RAM-run debug.
 * The flash opens with the render's hottest flash functions (after crt0, 64-byte aligned, in the order listed below
 * in .text; chosen by `emu_test --footprint`, docs/fm1-emu-test.md): the cache maps each 64-byte line to a set by
 * its address, so with a fixed home an edit elsewhere no longer moves which of their lines share a set. .rodata
 * follows them at once, so SINE and the hot tables keep their sets too. -ffunction-sections gives each function its
 * section .text.<name>; tools/build.py fails the build when a listed function is missing from the block (renamed,
 * inlined away) or lands outside it.
 * RAM  : .data/.bss (72 KiB): what the audio and key-scan interrupts touch, small variables
 * POOL : big zero-initialised buffers (FX lines, canvases, the parts' engine memory, the main loop's work
 *        buffers), section .pool. The same SRAM: the RAM / POOL boundary only splits the budget (1.5: 96 / 336
 *        KiB -> 72 / 360 KiB); tools/build.py reads both lengths from here
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8E950
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x6000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 72K
    POOL   (rw)  : ORIGIN = 0x01C1A000, LENGTH = 0x5D600
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : {
        KEEP(*(.text.crt0))
        . = ALIGN(64); _flash_hot_start = .;
        *fm1_isr.o(.text*)
        *(.text.fm1_alnk0_irq) *(.text.fm1_timer5_irq) *(.text.dx_run) *(.text.track_render_audio)
        *(.text.mix_part) *(.text.perf_begin) *(.text.drum_render) *(.text.perf_out)
        *(.text.pfx_block) *(.text.ins_stage) *(.text.slicer_track_sd) *(.text.bus_run) *(.text.track_insert)
        *(.text.sl_run) *(.text.c1_parts) *(.text.fm6_block) *(.text.fx_master_run) *(.text.ins_run_st)
        *(.text.drum_bus) *(.text.click_render) *(.text.duck_block) *(.text.fm6_done) *(.text.fm6_sync)
        *(.text.fx_list_get) *(.text.ins_bus_join) *(.text.mix_split) *(.text.beat_samples) *(.text.bus_g_swap)
        *(.text.bus_own) *(.text.chord_live_flush) *(.text.eng_mem_claim) *(.text.mod_begin) *(.text.perf_take)
        *(.text.sl_trk_send) *(.text.tape_chain) *(.text.tape_pitch16_for) *(.text.perf_throw) *(.text.c1_layer)
        *(.text.dx_lane_muted) *(.text.dx_layer) *(.text.fx_target_ptr) *(.text.chord_live_tick)
        *(.text.delay_samples) *(.text.dx_fall) *(.text.esync_coef) *(.text.fx_order_ok) *(.text.ins_any)
        *(.text.ins_mix_of) *(.text.ins_param_src) *(.text.lfo_wave) *(.text.master_begin) *(.text.mod_active)
        *(.text.mod_bus_begin) *(.text.mod_bus_end) *(.text.perf_avail) *(.text.rev_run) *(.text.rev_side_mix)
        *(.text.shed_check) *(.text.sl_state) *(.text.sl_trk_chain) *(.text.sl_trk_used) *(.text.solo_mask)
        *(.text.tape_head_on) *(.text.tape_seq_speed_q16) *(.text.uac_service) *(.text.uac_render_start)
        *(.text.bus_c1) *(.text.div_samples) *(.text.drum_amp) *(.text.drum_lane_of) *(.text.dx_fm_lim)
        *(.text.dx_pow2) *(.text.fm1__sr_bits) *(.text.input_off) *(.text.ins_bus_tr) *(.text.ins_win_put)
        *(.text.kit_track) *(.text.lfo_inc) *(.text.lowest_held) *(.text.mchord_of) *(.text.memcpy)
        *(.text.midi_event) *(.text.midi_expression) *(.text.midi_local_held) *(.text.part_ticks)
        *(.text.perf_layer) *(.text.rev_use_chain_g) *(.text.seq_step) *(.text.slicer_busy) *(.text.slicer_track)
        *(.text.trk_note_on)
        _flash_hot_end = .;
        *(.rodata*) *(.text*) . = ALIGN(4); _etext = .;
    } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .c1_text : { _c1_start = .; KEEP(*(.c1_text*)) . = ALIGN(4); _c1_end = .; } > RAMTEXT AT > XIP
    _c1_load = LOADADDR(.c1_text);
    .hot_text : { _hot_start = .; *(.hot_text*) . = ALIGN(4); _hot_end = .; } > RAMTEXT AT > XIP
    _hot_load = LOADADDR(.hot_text);
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    /* 3.2: CPU0 user 12,288 B / supervisor 6,144 B (Melodee 1.5's audited sizes; static worst path
     * 5,652 / 1,868 B, docs/RAM_STACK_AUDIT_3.2.md). The 13,824 B freed are POOL. */
    _guard0 = 0x01C77600; _ustack_lo = 0x01C77700; _ustack_top = 0x01C7A700;
    _guard1 = 0x01C7A700; _sstack_lo = 0x01C7A800; _sstack_top = 0x01C7C000;
    ASSERT(_guard0 == _pool_start + LENGTH(POOL), "POOL must end at the first stack guard")
    /DISCARD/ : { *(.comment) *(.note*) }
}
