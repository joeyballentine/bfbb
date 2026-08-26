// The executable's own translation unit. The game's entry point is its own --
// main() is in src/SB/Game/zMain.cpp, and it is the retail one.
//
// Two jobs, both of which have to happen before main() runs, which is why they
// are a static constructor rather than a function anything calls:
//
//   1. Unbuffer stdout. When the port's output is redirected to a file, stdout
//      is FULLY buffered, so a crash discards everything printed since the last
//      4KB. The first attempt to boot this executable produced an empty log and
//      a segfault, and the empty log was the buffering rather than an early
//      crash -- iSystemInit's own startup line had already run. With eleven
//      platform modules still stubbed, "which line came last" is the entire
//      diagnosis, so losing the tail is losing the answer.
//
//   2. Say what this build is. A stub-linked port that crashes looks exactly
//      like a broken port unless the log says up front that most of the
//      platform layer is not implemented yet.
//
// This file is also what gives the CMake executable a source of its own, and
// what makes the compiler driver name the CRT -- linking the archives with no
// driver input leaves eight ordinary libc symbols unresolved. tools/pclink.py
// does the same thing for the same reason.

#include <stdio.h>

namespace
{
    struct StartupBanner
    {
        StartupBanner()
        {
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
            printf("bfbb: PC port. Eleven platform modules are STUBS -- each reports itself\n");
            printf("bfbb: the first time it is called. See src/SB/Core/pc/iStub.h.\n");
        }
    };

    StartupBanner sBanner;
}
