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
//   2. Say what this build is. All eleven platform modules are implemented now,
//      but two of them -- iFX and iFMV -- are deliberate refusals rather than
//      ports, so the banner names what is missing instead of letting someone
//      discover it by watching a cutscene that never plays.
//
// This file is also what gives the CMake executable a source of its own, and
// what makes the compiler driver name the CRT -- linking the archives with no
// driver input leaves eight ordinary libc symbols unresolved. tools/pclink.py
// does the same thing for the same reason.

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#include <windows.h>
#include <dbghelp.h>

// Named in the startup banner, so that a build says which movie decoder it
// actually has rather than the reader guessing.
#include "iFMVAudio.h"
#include "iFMVDecoder.h"

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

    // Which binary an address is in, and where in it. dbghelp answers the first
    // through the module base it already tracks for the stack walk, so this
    // costs nothing beyond a GetModuleFileName -- and it is the part of a
    // backtrace that never degrades, symbols or no symbols.
    const char* ModuleName(HANDLE process, DWORD64 address)
    {
        static char name[MAX_PATH];

        DWORD64 base = SymGetModuleBase64(process, address);
        if (base == 0)
        {
            return "?";
        }

        if (GetModuleFileNameA((HMODULE)(uintptr_t)base, name, sizeof(name)) == 0)
        {
            return "?";
        }

        // The leaf, because the directory is the same for all of them and the
        // line is long enough already.
        const char* slash = strrchr(name, '\\');
        return slash != NULL ? slash + 1 : name;
    }

    DWORD64 ModuleOffset(HANDLE process, DWORD64 address)
    {
        DWORD64 base = SymGetModuleBase64(process, address);
        return base != 0 ? address - base : 0;
    }

    // Walks one thread's stack and prints it, symbolised. Shared by the crash
    // handler and the watchdog: a crash and a hang want the same answer --
    // "where is it" -- and differ only in how they come by a CONTEXT.
    void PrintBacktrace(HANDLE thread, CONTEXT* context)
    {
        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(process, NULL, TRUE);

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
            if (!StackWalk64(IMAGE_FILE_MACHINE_I386, process, thread, &frame,
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
                // The module, and the address inside it, alongside the symbol.
                //
                // Without this a frame in a DLL that ships no symbols reads as
                // the nearest preceding EXPORT plus a five-digit offset --
                // `SetDependencyInfo + 0x262590` -- which names a function the
                // code has nothing to do with and hides the one thing that is
                // always knowable: which binary it was in. A crash on a worker
                // thread is mostly diagnosed by that alone.
                printf("bfbb:   #%-2d %s + 0x%llx   [%s+0x%llx]\n", depth, name,
                       (unsigned long long)displacement,
                       ModuleName(process, frame.AddrPC.Offset),
                       (unsigned long long)ModuleOffset(process, frame.AddrPC.Offset));
            }
        }

        fflush(stdout);
    }

    HANDLE sMainThread;

    // Recorded unconditionally, unlike sMainThread, which only the
    // watchdog needs and only sets when it is asked for. The crash handler
    // runs on whichever thread faulted and wants to say which that was.
    DWORD sMainThreadId;

    // The watchdog, for HANGS.
    //
    // A crash announces itself; a hang does not. The port reaches further into
    // startup with every module ported and then stops somewhere without saying
    // where, and lldb would not attach on this machine -- so the process reports
    // on itself: a thread that wakes every few seconds, suspends the main
    // thread, walks its stack with the same code the crash handler uses, and
    // prints it.
    //
    // Repeating rather than firing once is deliberate. One trace cannot tell a
    // deadlock from slow progress. Two identical traces can; two different ones
    // say it is running and simply not finishing.
    //
    // Off unless BFBB_WATCHDOG names a number of seconds. Suspending the main
    // thread is not free, and nothing should do it to a build that works.
    DWORD WINAPI WatchdogThread(LPVOID param)
    {
        const DWORD seconds = (DWORD)(uintptr_t)param;

        for (int sample = 1;; sample++)
        {
            Sleep(seconds * 1000);

            printf("\nbfbb: WATCHDOG sample %d -- main thread stack:\n", sample);

            if (SuspendThread(sMainThread) == (DWORD)-1)
            {
                printf("bfbb:   (could not suspend the main thread)\n");
                continue;
            }

            CONTEXT context;
            memset(&context, 0, sizeof(context));
            context.ContextFlags = CONTEXT_FULL;

            if (GetThreadContext(sMainThread, &context))
            {
                PrintBacktrace(sMainThread, &context);
            }
            else
            {
                printf("bfbb:   (could not read the main thread context)\n");
            }

            ResumeThread(sMainThread);
        }

        return 0;
    }

    // Aborts, which a crash handler does not see.
    //
    // An assert() inside librw ends in _wassert, which prints one line naming
    // the file and raises SIGABRT. No exception is raised, so neither of the
    // handlers below runs and the process dies having said only which assertion
    // failed -- not which of the game's calls tripped it, which is the part
    // worth knowing when the assertion is inside a library the game drives
    // through a shim.
    void AbortHandler(int)
    {
        printf("\nbfbb: ABORT -- assertion or abort() call\n");

        CONTEXT context;
        memset(&context, 0, sizeof(context));
        RtlCaptureContext(&context);

        // The first few frames are this handler and the CRT's raise path.
        PrintBacktrace(GetCurrentThread(), &context);

        fflush(stdout);
        _exit(3);
    }

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

        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(process, NULL, TRUE);

        const DWORD64 pc = (DWORD64)(uintptr_t)info->ExceptionRecord->ExceptionAddress;
        printf("\nbfbb: CRASH -- exception 0x%08lx at %p  [%s+0x%llx]\n", (unsigned long)code,
               info->ExceptionRecord->ExceptionAddress, ModuleName(process, pc),
               (unsigned long long)ModuleOffset(process, pc));

        // Which thread. A crash on a worker thread and one on the game's own
        // are different bugs, and the stack alone does not always say which
        // this was -- the main thread's is the one that ends in main().
        printf("bfbb:   thread %lu%s\n", (unsigned long)GetCurrentThreadId(),
               GetCurrentThreadId() == sMainThreadId ? " (the main thread)" : " (a worker)");

        if (code == EXCEPTION_ACCESS_VIOLATION)
        {
            // The second parameter is the address that was touched, and it is
            // usually the whole diagnosis: 0 is a null dereference, a small
            // value is a null plus a field offset, garbage is a wild pointer.
            printf("bfbb:   access violation %s address %p\n",
                   info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                   (void*)info->ExceptionRecord->ExceptionInformation[1]);
        }

        PrintBacktrace(GetCurrentThread(), info->ContextRecord);
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
            sMainThreadId = GetCurrentThreadId();

            AddVectoredExceptionHandler(1, FirstChanceHandler);
            signal(SIGABRT, AbortHandler);

            const char* watchdogSeconds = getenv("BFBB_WATCHDOG");
            if (watchdogSeconds != NULL)
            {
                DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                                &sMainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
                CreateThread(NULL, 0, WatchdogThread,
                             (LPVOID)(uintptr_t)atoi(watchdogSeconds), 0, NULL);
            }
            SetUnhandledExceptionFilter(CrashHandler);
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
            // A banner that names a gap which has since been filled is worse
            // than no banner, because it is the first thing anyone reads when
            // something does not work. iFX stopped being a refusal when the
            // animated-UV pipeline went in, and iFMV stopped being one when the
            // movie decoder did -- so this says which decoder is actually
            // linked rather than asserting there is none.
            printf("bfbb: PC port, D3D9. Movie decoder: %s, movie audio: %s.\n",
                   iFMVDecoderName(), iFMVAudioName());
            if (getenv("BFBB_TEST_CRASH")) { *(volatile int*)0 = 1; }
        }
    };

    StartupBanner sBanner;
}
