// The draw distance switch. What it reaches, and what it deliberately does not,
// is in iDrawDist.h.

#include "iDrawDist.h"

namespace
{
    // Retail's far clip, in iCamera.cpp, and the value the switch turns off to.
    const F32 kDefaultFarClip = 400.0f;

    // And what it turns on to. Not a number picked for headroom: the television
    // camera in zNPCTypePrawn.cpp already builds a 10000-unit frustum to see a
    // whole room at once, so this is a distance the game itself has decided is
    // beyond anything a level contains.
    //
    // Depth precision does not argue against it. With the D3D projection the
    // port uses, the depth range is governed by the NEAR plane -- half the
    // buffer covers near to 2*near whatever the far plane is, and far enters
    // only through far/(far - near), which moves from 1.00013 to 1.000005 over
    // this change. The near plane stays at 0.05 and so does the z-fighting.
    const F32 kUnlimitedFarClip = 10000.0f;

    S32 sUnlimited = FALSE;
}

S32 iDrawDistUnlimited()
{
    return sUnlimited;
}

F32 iDrawDistFarClip()
{
    return sUnlimited ? kUnlimitedFarClip : kDefaultFarClip;
}

void iDrawDistSetUnlimited(S32 unlimited)
{
    sUnlimited = unlimited;
}
