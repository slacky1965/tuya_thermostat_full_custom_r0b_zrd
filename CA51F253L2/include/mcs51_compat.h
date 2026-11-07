/*******************************************************************************/
/* clang (mcs51-llvm) compatibility layer.                                     */
/*                                                                             */
/* Force-included (-include) ONLY by the clang build path of the makefile;     */
/* SDCC never sees this header. The underscored address-space macros           */
/* (__xdata/__code/...) are pre-defined by the mcs51 clang target, and the     */
/* bare `xdata`/`code` aliases live in include/stdint.h (compiler-agnostic).   */
/* What is left for this header:                                               */
/*   * `data`/`idata`/`pdata` locals - clang rejects address-space-qualified   */
/*     automatics ("automatic variable qualified with an address space"), and  */
/*     the only uses in this project are loop counters forced into IRAM for    */
/*     SDCC's model-large; plain locals are correct for clang;                 */
/*   * `bit` - SDCC type, clang only has the address_space(6) attribute, so    */
/*     the macro carries the type;                                             */
/*   * `__interrupt(n)` - SDCC spelling of the clang interrupt attribute.      */
/*******************************************************************************/
#ifndef _MCS51_COMPAT_H_
#define _MCS51_COMPAT_H_

#if defined(__clang__)
#define data
#define idata
#define pdata
/* `bit` maps to the clang spelling of the SDCC bit space. The macro carries
   the type (clang has no `__bit` keyword), so `bit name;` stays valid and
   becomes an unsigned char in address_space(6). Globals land in
   .mcs51.bit.bss (BIT_RAM: bit addresses 0..127 backed by IRAM 0x20..0x2F,
   zeroed by the startup IRAM pass) and invert as a 2-byte `cpl <bit>`;
   verified with doc_local/temp/test_bit_not.c. An older toolchain build
   miscompiled this mapping two ways: bit-space loads got CSE'd across
   basic blocks and the store wrote a stale ACC (the power hold never
   fired), and bit inversion came out as a plain copy with no CPL - do not
   test this mapping with such a build. clang rejects address-space-
   qualified automatics, so bit-space locals do not compile (keep such
   values in plain bytes - see the EA save in src/main.c). SFR bits are
   unaffected: they come from clang's builtin __sbit, which is only used
   for volatile hardware registers (EA, CY, ...). */
#define bit unsigned char __attribute__((address_space(6)))
#define __interrupt(n) __attribute__((interrupt(n)))
#endif

#endif
