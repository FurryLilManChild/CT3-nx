// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }
//
// (street-nx's EA sound guard fix lived here; sheroes-nx had none.)
#include <chrono>
#include <thread>

#include "kernel/kernel.h"
#include "xdk/hle.h"

// CT3-nx: CRI's idle thread (0x000B33D0): `while (![0x002E00DC]) [0x002E00C4]++;` then it sets 0x002E00E0 and exits
// (0x000DE1E4). On the Xbox it runs only while CRI's sound lock is held (the lock resumes it, the unlock suspends it)
// and only when nothing else wants the CPU. Here it ran flat out on a core of its own for the whole run (first Switch
// profile, 2026-11-14: 58.4 s busy in 58.4 s, all in this loop), one of the Switch's three cores. Same loop, with a
// 1 ms sleep on each pass; the count stays an idle count (CRI's measure of free CPU), only slower.
// (main handled the same loop with a pre-emption point, src/recomp_manual.c there.)
X86_EXTERN_FUNC(__imp__sub_000B33D0);
X86_FUNC(sub_000B33D0) {
    while (kernel::R32(0x002E00DCu) == 0) {
        kernel::W32(0x002E00C4u, kernel::R32(0x002E00C4u) + 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    __imp__sub_000B33D0(ctx, base);  // the original: sees the flag set, takes its exit path
}
