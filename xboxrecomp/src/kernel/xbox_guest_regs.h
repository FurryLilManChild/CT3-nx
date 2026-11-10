/*
 * xbox_guest_regs.h - the guest x86 registers in host CPU registers
 *
 * Adapted from OpenTrickyNX (DevCrashDay/OpenTrickyNX, xboxrecomp/src/kernel/
 * xbox_guest_regs.h, MIT like the rest of xboxrecomp), the SSX Tricky Switch
 * port that runs at 60 fps on this toolkit. CT3-nx, 2026-11-09 (Mark's
 * Strategy 1; docs/d3d-path.md).
 *
 * The translated code reads and writes eax..esp as g_eax..g_esp. By default
 * they are __thread variables (xbox_memory_layout.c). The translator already
 * keeps them in C locals inside a function, but every call, return and kernel
 * call stores all seven back and reloads them, and each function first asks
 * for their addresses: on the Switch that is a __aarch64_read_tp call
 * (-mtp=soft) and fourteen memory accesses per call.
 *
 * With XBOX_GUEST_REGS (GCC on AArch64) they are global register variables
 * instead, pinned to the callee-saved registers w19-w25. Each OS thread has
 * its own register file, so they stay per-thread like the __thread copies,
 * and the stores and reloads around a call disappear. The build force-includes
 * this header into every C file of the program (CMakeLists.txt), so none of
 * our code uses those registers for anything else; the libraries we link
 * (libnx, newlib, Mesa, SDL) see ordinary callee-saved registers and keep them.
 *
 * The other per-thread guest state the translated code touches at every
 * function (the x87 stack and control word, ebp and the SEH frame, the SSE and
 * MMX registers, the x87 compare state, the direction flag) moves into one
 * __thread struct, xbox_gtls. Its old names are macros for its fields: host
 * code reaches it as a __thread variable, the translated code through x26,
 * which holds its address on a thread that runs guest code. The x87 stack top
 * gets a register of its own: w27.
 *
 * What changes with them:
 *  - a thread that runs guest code must start through xbox_guest_regs_call(),
 *    which zeroes the registers (the __thread copies started at 0), sets x26
 *    and gives the caller back its own x19-x27 at the end;
 *  - longjmp() restores them to their setjmp() values;
 *  - their address cannot be taken.
 */
#ifndef XBOX_GUEST_REGS_H
#define XBOX_GUEST_REGS_H

#if defined(XBOX_GUEST_REGS) && defined(__aarch64__) && defined(__GNUC__) && !defined(__clang__) \
    && !defined(__cplusplus) && !defined(__ASSEMBLER__)
#define XBOX_GUEST_REGS_IN_CPU 1

/* No system header here: this is included ahead of a file's own
 * _GNU_SOURCE and the like. */
register __UINT32_TYPE__ g_eax __asm__("w19");
register __UINT32_TYPE__ g_ecx __asm__("w20");
register __UINT32_TYPE__ g_edx __asm__("w21");
register __UINT32_TYPE__ g_ebx __asm__("w22");
register __UINT32_TYPE__ g_esi __asm__("w23");
register __UINT32_TYPE__ g_edi __asm__("w24");
register __UINT32_TYPE__ g_esp __asm__("w25");
register struct xbox_gtls *g_xbox_gtls __asm__("x26");   /* only on guest threads */
register int g_fp_top __asm__("w27");                    /* the x87 stack top */

/* fn(arg) with the guest registers zeroed and x26 = this thread's xbox_gtls,
 * and the caller's x19-x27 back afterwards (xbox_guest_regs.c). */
void *xbox_guest_regs_call(void *(*fn)(void *), void *arg);

/* What were separate __thread variables (recomp_types.h describes each). */
struct xbox_gtls {
    double fp_stack[8];
    __UINT32_TYPE__ ebp, seh_ebp;
    __UINT16_TYPE__ fp_control_word, fp_cc;
    int fp_cmp, df;
    unsigned char mm[8][8] __attribute__((aligned(8)));     /* RecompMmx */
    unsigned char xmm[8][16] __attribute__((aligned(16)));  /* RecompXmm */
};
extern __thread struct xbox_gtls t_xbox_gtls;

#define XBOX_GTLS (&t_xbox_gtls)            /* the translated code: g_xbox_gtls (recomp_types.h) */
#define g_fp_stack        (XBOX_GTLS->fp_stack)
#define g_ebp             (XBOX_GTLS->ebp)
#define g_seh_ebp         (XBOX_GTLS->seh_ebp)
#define g_fp_control_word (XBOX_GTLS->fp_control_word)
#define g_fp_cc           (XBOX_GTLS->fp_cc)
#define g_fp_cmp          (XBOX_GTLS->fp_cmp)
#define g_df              (XBOX_GTLS->df)
#define g_mm0 (*(RecompMmx *)XBOX_GTLS->mm[0])
#define g_mm1 (*(RecompMmx *)XBOX_GTLS->mm[1])
#define g_mm2 (*(RecompMmx *)XBOX_GTLS->mm[2])
#define g_mm3 (*(RecompMmx *)XBOX_GTLS->mm[3])
#define g_mm4 (*(RecompMmx *)XBOX_GTLS->mm[4])
#define g_mm5 (*(RecompMmx *)XBOX_GTLS->mm[5])
#define g_mm6 (*(RecompMmx *)XBOX_GTLS->mm[6])
#define g_mm7 (*(RecompMmx *)XBOX_GTLS->mm[7])
#define g_xmm0 (*(RecompXmm *)XBOX_GTLS->xmm[0])
#define g_xmm1 (*(RecompXmm *)XBOX_GTLS->xmm[1])
#define g_xmm2 (*(RecompXmm *)XBOX_GTLS->xmm[2])
#define g_xmm3 (*(RecompXmm *)XBOX_GTLS->xmm[3])
#define g_xmm4 (*(RecompXmm *)XBOX_GTLS->xmm[4])
#define g_xmm5 (*(RecompXmm *)XBOX_GTLS->xmm[5])
#define g_xmm6 (*(RecompXmm *)XBOX_GTLS->xmm[6])
#define g_xmm7 (*(RecompXmm *)XBOX_GTLS->xmm[7])

#endif
#endif /* XBOX_GUEST_REGS_H */
