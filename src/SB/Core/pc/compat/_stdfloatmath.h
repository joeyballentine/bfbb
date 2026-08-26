#ifndef BFBB_PC_COMPAT_STDFLOATMATH_H
#define BFBB_PC_COMPAT_STDFLOATMATH_H

// MSL puts the f-suffixed math functions in namespace std, and the game's
// sources call them that way -- std::floorf, std::fabsf, std::powf. The C++
// standard keeps those names in the global namespace and gives std the
// overload set instead, so a host <cmath> has ::floorf but not std::floorf.
//
// Importing the global names is enough to make both spellings work, and it
// replaces nothing the host library already declares.
//
// This lives in its own header because BOTH compat/<cmath> and compat/<math.h>
// need it. The game reaches std::floorf through either spelling -- xHudMeter
// includes <math.h> and calls std::powf, xSpline includes neither directly and
// gets there transitively -- so injecting the names in <cmath> alone left five
// units failing on a host where the C++ headers do not pull each other in the
// way libstdc++ does.
//
// Include only after the global names are in scope.

#ifdef __cplusplus
namespace std
{
using ::acosf;
using ::asinf;
using ::atan2f;
using ::atanf;
using ::ceilf;
using ::cosf;
using ::expf;
using ::fabsf;
using ::floorf;
using ::fmodf;
using ::log10f;
using ::logf;
using ::powf;
using ::sinf;
using ::sqrtf;
using ::tanf;
} // namespace std
#endif

#endif
