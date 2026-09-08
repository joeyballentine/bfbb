// Which of the linked render backends runs. The interface, and why the answer
// is needed before the window exists, are in backend.h.

#include <rwcore.h>

#include "rw.h"

#include "backend.h"

#include <stdio.h>

void iBackendResolve(void)
{
    // Once. RenderWareInit calls this before the window opens and RwEngineOpen
    // calls it again, and between them a run must not report a refused setting
    // twice. A flag rather than a test on the answer, because resolving TO the
    // null backend is a real answer and looks exactly like not having resolved.
    static bool resolved = false;

    if (resolved)
    {
        return;
    }
    resolved = true;

    // In preference order, and the same order iScreen.h lists them: the D3D
    // backend if this build has one, then GL3, then no device at all.
    static const iScreenBackend kOrder[] = {
#if defined(RW_D3D9) || defined(RW_D3D8)
        iSCREENBACKEND_D3D9,
#endif
#ifdef RW_D3D11
        iSCREENBACKEND_D3D11,
#endif
#ifdef RW_GL3
        iSCREENBACKEND_GL3,
#endif
        iSCREENBACKEND_NULL
    };
    const int kCount = (int)(sizeof(kOrder) / sizeof(kOrder[0]));

    iScreenBackend want = iScreenGetBackend();

    if (want != iSCREENBACKEND_AUTO)
    {
        int i;
        for (i = 0; i < kCount; i++)
        {
            if (kOrder[i] == want)
            {
                break;
            }
        }

        if (i == kCount)
        {
            // Named a backend this build was not compiled with. Fall back
            // rather than refuse: the alternative is a game that will not
            // start over a setting, and the message says what happened.
            printf("bfbb: video.backend = %s, which this build does not carry; "
                   "using %s\n",
                   iScreenBackendName(want), iScreenBackendName(kOrder[0]));
            fflush(stdout);
            want = iSCREENBACKEND_AUTO;
        }
    }

    if (want == iSCREENBACKEND_AUTO)
    {
        want = kOrder[0];
    }

    iScreenSetBackend(want);

    // librw has one PLATFORM_D3D9 for both Direct3D backends, so this is what
    // tells it which of the two to open. It has to be set before Engine::open,
    // which is where renderDevice() is first asked.
#if defined(RW_D3D9) && defined(RW_D3D11)
    rw::d3d::useD3D11 = (want == iSCREENBACKEND_D3D11);
#endif

    switch (want)
    {
    case iSCREENBACKEND_D3D9:
    case iSCREENBACKEND_D3D11:
        // Both, because librw has one PLATFORM_D3D9: D3D11 reads and writes
        // the same native data and registers the same pipelines. Which of the
        // two devices opens is rw::d3d::useD3D11, set above.
        rw::platform = rw::PLATFORM_D3D9;
        break;
    case iSCREENBACKEND_GL3:
        rw::platform = rw::PLATFORM_GL3;
        break;
    default:
        rw::platform = rw::PLATFORM_NULL;
        break;
    }
}
