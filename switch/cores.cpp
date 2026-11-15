// Where Horizon runs each thread (core_layout = true in the settings), as sa2-nx's core layout does: the title thread
// alone on core 0, the render thread on core 1, every other thread (guest threads, audio, the shader compiler) on cores
// 1 and 2, preferring 2. Without it every thread has mask 7 at the same priority, and the title thread, the frame's
// bottleneck, shares its core with whatever woke up. Each thread sets its own mask: setting another running thread's
// can wait for it to leave its core, and with busy loader threads hang (sa2-nx's emulator runs).
// CT3-nx: the other threads on core 2 only (sa2-nx let them onto the render thread's core too), on by default. Crazy
// Taxi 3's render thread is the frame's limit (busy 96% of the time in the city, sixth Switch run, 2026-11-15) and
// had every thread's mask: 13 guest threads, some at higher priorities (the title's time critical threads map to
// 0x30, ApplyPriority), which Horizon always lets run first on whatever core they wake on. ShaolinMonks-NX
// (XboxXBERecomp, 60 fps on the Switch) keeps its renderer alone on a core the same way (port/nx/threads.cpp, read
// as a reference, no code taken).
#include <switch.h>

#include "core/log.h"
#include "host.h"

// role: 0 the title thread (the one that presents), 1 the render thread, 2 any other
extern "C" void ct3_place_thread(int role) {
    if (!host::CoreLayout()) return;
    static constexpr struct { s32 core; u32 mask; const char* name; } kPlaces[] = {
        {0, 0b001, "title"}, {1, 0b010, "render"}, {2, 0b100, "other"}};
    if (role < 0 || role > 2) return;
    const auto& p = kPlaces[role];
    Result rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, p.core, p.mask);
    if (role != 2 || R_FAILED(rc))
        LOGR("threads: %s thread to core %d (mask %X)%s", p.name, int(p.core), unsigned(p.mask),
             R_FAILED(rc) ? " refused" : "");
}
