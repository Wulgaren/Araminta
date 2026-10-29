<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (C) 2026 Mario Faúndez Vidal, kurogedelic. Adapted for Araminta from Felucca f9a98fc and Felucca-Engines. -->
# Signed audio scaling and phase wrapping

Strict UBSan exposes left shifts of negative audio/feedback values in the DC
blocker, low-cut filter, FM6 velocity and sine-LFO scaling, and in the output
scaling of ANALOG, PHASE, LOFI, NOISE, TRIO, WHEEL, FORMANT, GRAIN, SAMPLE, PHYS,
the drums and DRUM-X. It also finds signed multiplies that wrap in NOISE's pink
and brown poles. Replace the bounded power-of-two scalings with multiplication or
`shl()`, and the wrapping multiplies with `mul_wrap()`. Keep the existing
arithmetic right shifts, error-feedback values and parameter ranges. Both helpers
work in `uint32_t` and convert back to `int32_t`, so the bits stay the same.

FM6 modulation and feedback can also add across the signed phase boundary.
Perform those additions through `uint32_t` before converting back to the signed
phase representation, matching the phase accumulator's existing unsigned wrap.
No presets, coefficients, voice allocation or persistence formats change.

`tests/signed_audio_scaling_test.c` freezes DC/low-cut remainders and FM6
velocity/LFO results, plus two phase-boundary audio hashes from the original
1.6.1 implementation. `tests/phys_signed_test.c` excites the actual PHYS track
and checks negative and positive samples against its original render hash.
Both run normally and under strict ASan/UBSan without exclusions or recovery.
The original cases reproduce sanitizer failures on the unchanged baseline.

These focused checks supplement the existing renders, target budgets and
one-core/two-core regressions. They do not claim every DSP path is free of
undefined behavior.

Sources: Felucca `f9a98fc` (Mario Faúndez Vidal, GPL-3.0-only), Felucca-Engines
`e88bc70` (Mario Faúndez Vidal, MIT) and `7511c0a` (kurogedelic, MIT).
