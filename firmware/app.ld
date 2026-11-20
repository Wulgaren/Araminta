/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA memory map.
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * 0x01C00000..0x01C08E00 holds .ram_text (code that must run while the flash
 * is busy: the flash driver), then .c1_text (the second core's waiting loop, hal/fm1_cpu1.h: an idle core 1 never
 * fetches from the flash), then .hot_text (AUDIO_RAM, core.h: the render's hottest functions, out of the 32 KB
 * flash cache they would thrash); crt0.S keeps its boot-probe words at 0x01C08E00..0x01C08F00, and RAM starts there.
 * The flash opens with the render's hottest flash functions (after crt0, 64-byte aligned, in the order listed
 * below in .text; the membership is `emu_test --footprint`'s, docs/fm1-emu-test.md): the cache maps each 64-byte
 * line to a set by its address, so with a fixed home an edit elsewhere no longer moves which of their lines share
 * a set. .rodata follows them at once, so the tables keep their sets too. The three hottest tables (SINE, ENV_EXP,
 * TANH_Q15: HOT_DATA, felucca.c) sit in RAM after the .hot_text code instead, copied with it at boot.
 * -ffunction-sections gives each function its section .text.<name>; tools/build.py fails the build when a listed
 * function is missing from the block (renamed, inlined away) or lands outside it.
 * The order of the list matters as much as its membership. The searched block ran 566 lines against the cache's
 * 512 ways, and the DSP code and tables outside it keep their own sets, so one set can hold five lines the render
 * fetches in the same half; how many hot lines a set holds (what a footprint or a sort by miss count sees) does not
 * predict that set's misses, only whether those lines run in the same stretch. The order below was searched by
 * replaying the emulator's XIP access trace through the cache, then building and measuring each candidate, with the
 * entry count and the block's size kept fixed so the flash outside it keeps its sets. Four of those entries now run
 * from .hot_text (RAMTEXT above), so the list holds 82. Reordering again moves every measurement; compare two orders
 * with tests/layout_sweep.sh and the medians, not one build (docs/fm1-emu-test.md).
 * RAM  : .data/.bss (~68 KiB after RAMTEXT 0x8E00): what the audio and key-scan interrupts touch, small variables
 * POOL : big zero-initialised buffers (FX lines, canvases, the parts' engine memory, the main loop's work
 *        buffers), section .pool. The same SRAM: the RAM / POOL boundary only splits the budget (1.5: 96 / 336
 *        KiB -> 72 / 360 KiB); tools/build.py reads both lengths from here
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8E950
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x8E00
    RAM    (rwx) : ORIGIN = 0x01C08F00, LENGTH = 0x11100
    POOL   (rw)  : ORIGIN = 0x01C1A000, LENGTH = 0x5D600
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : {
        KEEP(*(.text.crt0))
        . = ALIGN(64); _flash_hot_start = .;
        *fm1_isr.o(.text*)
        *(.text.fm1__sr_bits)         *(.text.fm1_alnk0_irq)         *(.text.dx_run)         *(.text.dx_fall)         *(.text.part_ticks)
        *(.text.dx_lane_muted)         *(.text.rev_run)         *(.text.beat_samples)         *(.text.uac_render_start)         *(.text.tape_pitch16_for)
        *(.text.dx_pow2)         *(.text.midi_local_held)         *(.text.drum_lane_of)         *(.text.perf_flanger)         *(.text.perf_phaser)         *(.text.dx_layer)
        *(.text.ins_bus_tr)         *(.text.shed_check)         *(.text.perf_layer)         *(.text.perf_avail)         *(.text.chord_live_tick)
        *(.text.track_insert)         *(.text.fx_master_run)         *(.text.ins_param_src)         *(.text.bus_own)         *(.text.lfo_inc)
        *(.text.tape_head_on)         *(.text.c1_layer)         *(.text.sl_trk_send)         *(.text.mod_bus_begin)         *(.text.ins_mix_of)
        *(.text.kit_track)         *(.text.lowest_held)         *(.text.mod_bus_end)         *(.text.delay_samples)         *(.text.midi_event)
        *(.text.input_off)         *(.text.div_samples)         *(.text.perf_out)         *(.text.lfo_wave)
        *(.text.mchord_of)         *(.text.perf_throw)         *(.text.fm6_sync)         *(.text.fm6_done)         *(.text.midi_expression)
        *(.text.ins_run_st)         *(.text.fm1_timer5_irq)         *(.text.mod_active)         *(.text.eng_mem_claim)         *(.text.sl_run)
        *(.text.fx_target_ptr)         *(.text.perf_harm)         *(.text.perf_glt)         *(.text.perf_glt_seg)         *(.text.pb_ring_f)         *(.text.tape_chain)         *(.text.fm6_block)
        *(.text.fx_list_get)         *(.text.chord_live_flush)         *(.text.ins_win_put)         *(.text.sl_state)         *(.text.sl_trk_used)
        *(.text.rev_side_mix)         *(.text.rev_use_chain_g)         *(.text.mod_begin)         *(.text.slicer_track)         *(.text.click_render)
        *(.text.dx_fm_lim)         *(.text.master_begin)         *(.text.perf_take)         *(.text.drum_amp)         *(.text.sl_trk_chain)
        *(.text.uac_service)         *(.text.pfx_block)         *(.text.ins_bus_join)         *(.text.slicer_track_sd)         *(.text.esync_coef)
        *(.text.seq_step)         *(.text.trk_note_on)         *(.text.bus_g_swap)         *(.text.memcpy)         *(.text.fx_order_ok)
        *(.text.solo_mask)         *(.text.ins_stage)
        _flash_hot_end = .;
        *(.rodata*) *(.text*) . = ALIGN(4); _etext = .;
    } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .c1_text : { _c1_start = .; KEEP(*(.c1_text*)) . = ALIGN(4); _c1_end = .; } > RAMTEXT AT > XIP
    _c1_load = LOADADDR(.c1_text);
    .hot_text : { _hot_start = .; *(.hot_text*) *(.hot_data*) . = ALIGN(4); _hot_end = .; } > RAMTEXT AT > XIP
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
