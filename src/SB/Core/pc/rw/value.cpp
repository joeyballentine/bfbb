// The part of the RenderWare C API that touches only value types.
//
// RwV3d and rw::V3d are both three floats; RwMatrixTag and rw::Matrix are
// byte-identical, because librw mirrors RenderWare's matrix layout on purpose.
// So everything here crosses the seam by reinterpreting a pointer, with no
// conversion and no copy. See README.md -- the object types are NOT like this,
// and nothing in this file should grow to touch one.

#include <rwcore.h>

#include "rw.h"

#include <math.h>
#include <stddef.h>

// The claims the reinterpret_casts below rest on, checked rather than asserted
// in a comment. If librw rearranges any of these, this file stops compiling
// instead of silently reading the wrong bytes.
static_assert(sizeof(RwV3d) == sizeof(rw::V3d), "RwV3d and rw::V3d differ in size");
static_assert(offsetof(RwV3d, x) == offsetof(rw::V3d, x), "RwV3d.x moved");
static_assert(offsetof(RwV3d, y) == offsetof(rw::V3d, y), "RwV3d.y moved");
static_assert(offsetof(RwV3d, z) == offsetof(rw::V3d, z), "RwV3d.z moved");

static_assert(sizeof(RwMatrix) == sizeof(rw::Matrix), "RwMatrix and rw::Matrix differ in size");
static_assert(offsetof(RwMatrixTag, right) == offsetof(rw::Matrix, right), "matrix right moved");
static_assert(offsetof(RwMatrixTag, flags) == offsetof(rw::Matrix, flags), "matrix flags moved");
static_assert(offsetof(RwMatrixTag, up) == offsetof(rw::Matrix, up), "matrix up moved");
static_assert(offsetof(RwMatrixTag, at) == offsetof(rw::Matrix, at), "matrix at moved");
static_assert(offsetof(RwMatrixTag, pos) == offsetof(rw::Matrix, pos), "matrix pos moved");

// RwMatrixScale and RwMatrixTranslate pass the combine op straight through.
static_assert((int)rwCOMBINEREPLACE == (int)rw::COMBINEREPLACE, "combine op REPLACE differs");
static_assert((int)rwCOMBINEPRECONCAT == (int)rw::COMBINEPRECONCAT, "combine op PRECONCAT differs");
static_assert((int)rwCOMBINEPOSTCONCAT == (int)rw::COMBINEPOSTCONCAT, "combine op POSTCONCAT differs");

static inline rw::V3d* asV3d(RwV3d* v)
{
    return reinterpret_cast<rw::V3d*>(v);
}

static inline const rw::V3d* asV3d(const RwV3d* v)
{
    return reinterpret_cast<const rw::V3d*>(v);
}

static inline rw::Matrix* asMatrix(RwMatrix* m)
{
    return reinterpret_cast<rw::Matrix*>(m);
}

static inline const rw::Matrix* asMatrix(const RwMatrix* m)
{
    return reinterpret_cast<const rw::Matrix*>(m);
}

RwReal RwV3dLength(const RwV3d* in)
{
    return rw::length(*asV3d(in));
}

RwReal RwV3dNormalize(RwV3d* out, const RwV3d* in)
{
    // RenderWare returns the length it divided by, which callers use; librw's
    // normalize returns the vector, so the length is taken first.
    RwReal len = rw::length(*asV3d(in));
    if (len > 0.0f)
    {
        *asV3d(out) = rw::scale(*asV3d(in), 1.0f / len);
    }
    else
    {
        out->x = 0.0f;
        out->y = 0.0f;
        out->z = 0.0f;
    }
    return len;
}

RwV3d* RwV3dTransformPoints(RwV3d* pointsOut, const RwV3d* pointsIn, RwInt32 numPoints,
                            const RwMatrix* matrix)
{
    rw::V3d::transformPoints(asV3d(pointsOut), asV3d(pointsIn), numPoints, asMatrix(matrix));
    return pointsOut;
}

RwMatrix* RwMatrixInvert(RwMatrix* matrixOut, const RwMatrix* matrixIn)
{
    rw::Matrix::invert(asMatrix(matrixOut), asMatrix(matrixIn));
    return matrixOut;
}

RwMatrix* RwMatrixUpdate(RwMatrix* matrix)
{
    asMatrix(matrix)->update();
    return matrix;
}

RwMatrix* RwMatrixScale(RwMatrix* matrix, const RwV3d* scale, RwOpCombineType combineOp)
{
    asMatrix(matrix)->scale(asV3d(scale), (rw::CombineOp)combineOp);
    return matrix;
}

RwMatrix* RwMatrixTranslate(RwMatrix* matrix, const RwV3d* translation,
                            RwOpCombineType combineOp)
{
    asMatrix(matrix)->translate(asV3d(translation), (rw::CombineOp)combineOp);
    return matrix;
}

// Reciprocal square root.
//
// librw has no counterpart, and RenderWare's own is not in this repository
// either -- src/rwsdk/src/plcore/bavector.c is decompiled but does not contain
// it -- so this is written from what the callers need rather than matched
// against anything.
//
// **The zero case is load-bearing and must not be "fixed".** xCollide.cpp
// computes a triangle normal as a cross product, takes the squared length,
// calls this, scales by the result, and then asks:
//
//     recipLength = _rwInvSqrt(lengthSq);
//     RwV3dScaleMacro((RwV3d*)&xnorm, (RwV3d*)&xnorm, recipLength);
//     if (isnan(xnorm.x)) { return 0; }
//
// That isnan is the game's degenerate-triangle rejection, and it only fires
// because a zero-area triangle gives lengthSq == 0, this returns +infinity,
// and 0 * infinity is NaN. Guarding the division to return 0 on a zero input
// -- the obvious defensive edit -- would hand back a zero-length normal that
// isnan does not catch, and collision against degenerate triangles would
// silently start reporting hits with no surface direction. IEEE division by
// zero is the behaviour the caller is reading, so the division is left alone.
//
// The input is a squared length at all four call sites, so it is never
// negative and sqrtf is never asked for a NaN of its own.
//
// The console's version is a PowerPC frsqrte estimate plus a Newton step,
// which is a few ULP off a true reciprocal square root; this is exact to the
// float. Nothing here reproduces the estimate: it exists because the hardware
// makes it cheaper than a divide, not because the game wants its error.
RwReal _rwInvSqrt(const RwReal num)
{
    return 1.0f / sqrtf(num);
}
