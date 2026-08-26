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
#include <stdlib.h>

#include <windows.h>
#include <dbghelp.h>

namespace
{
    // A crash handler, because the port is going to crash for a while yet and
    // the alternative is bisecting with printf.
    //
    // The GameCube build has Dolphin's GDB stub and OSPanic for this; a host
    // build has neither unless it brings its own. Without it a fault is a bare
    // "Segmentation fault" from the shell and a guess about which line -- which
    // is how the first three attempts at diagnosing xFXInit went, two of them
    // to wrong conclusions.
    //
    // Symbols come from the PDB via dbghelp, which is present on every Windows
    // and needs no build flags beyond the -g the port already compiles with.

    // A VECTORED handler as well as the unhandled-exception filter above.
    //
    // The filter never ran for the fault this was written to diagnose, and that
    // is itself a diagnosis: an unhandled-exception filter needs stack to run
    // on, so a STACK OVERFLOW skips it entirely. A vectored handler is called
    // first-chance, before unwinding, while the guard page is still doing its
    // job -- so it gets to say what happened when the filter cannot.
    LONG WINAPI FirstChanceHandler(EXCEPTION_POINTERS* info)
    {
        const DWORD code = info->ExceptionRecord->ExceptionCode;

        // Only the fatal ones. First-chance C++ exceptions and the debugger's
        // own breakpoints come through here too and are not interesting.
        if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW ||
            code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_INT_DIVIDE_BY_ZERO)
        {
            printf("\nbfbb: first-chance exception 0x%08lx at %p\n",
                   (unsigned long)code, info->ExceptionRecord->ExceptionAddress);

            if (code == EXCEPTION_STACK_OVERFLOW)
            {
                printf("bfbb:   STACK OVERFLOW -- runaway recursion, not a bad pointer\n");
            }

            fflush(stdout);
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    LONG WINAPI CrashHandler(EXCEPTION_POINTERS* info)
    {
        const DWORD code = info->ExceptionRecord->ExceptionCode;

        printf("\nbfbb: CRASH -- exception 0x%08lx at %p\n",
               (unsigned long)code, info->ExceptionRecord->ExceptionAddress);

        if (code == EXCEPTION_ACCESS_VIOLATION)
        {
            // The second parameter is the address that was touched, and it is
            // usually the whole diagnosis: 0 is a null dereference, a small
            // value is a null plus a field offset, garbage is a wild pointer.
            printf("bfbb:   access violation %s address %p\n",
                   info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                   (void*)info->ExceptionRecord->ExceptionInformation[1]);
        }

        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(process, NULL, TRUE);

        CONTEXT* context = info->ContextRecord;
        STACKFRAME64 frame;
        memset(&frame, 0, sizeof(frame));
        frame.AddrPC.Offset = context->Eip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context->Ebp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context->Esp;
        frame.AddrStack.Mode = AddrModeFlat;

        char symbolBuffer[sizeof(SYMBOL_INFO) + 512];
        SYMBOL_INFO* symbol = (SYMBOL_INFO*)symbolBuffer;
        memset(symbolBuffer, 0, sizeof(symbolBuffer));
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 500;

        for (int depth = 0; depth < 32; depth++)
        {
            if (!StackWalk64(IMAGE_FILE_MACHINE_I386, process, GetCurrentThread(), &frame,
                             context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            {
                break;
            }

            if (frame.AddrPC.Offset == 0)
            {
                break;
            }

            DWORD64 displacement = 0;
            const char* name = "?";
            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
            {
                name = symbol->Name;
            }

            IMAGEHLP_LINE64 line;
            memset(&line, 0, sizeof(line));
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;

            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line))
            {
                printf("bfbb:   #%-2d %s  (%s:%lu)\n", depth, name, line.FileName,
                       (unsigned long)line.LineNumber);
            }
            else
            {
                printf("bfbb:   #%-2d %s + 0x%llx\n", depth, name,
                       (unsigned long long)displacement);
            }
        }

        fflush(stdout);
        return EXCEPTION_EXECUTE_HANDLER;
    }
}


namespace
{
    struct StartupBanner
    {
        StartupBanner()
        {
            AddVectoredExceptionHandler(1, FirstChanceHandler);
            SetUnhandledExceptionFilter(CrashHandler);
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
            printf("bfbb: PC port. Eleven platform modules are STUBS -- each reports itself\n");
            printf("bfbb: the first time it is called. See src/SB/Core/pc/iStub.h.\n");
            if (getenv("BFBB_TEST_CRASH")) { *(volatile int*)0 = 1; }
        }
    };

    StartupBanner sBanner;
}
