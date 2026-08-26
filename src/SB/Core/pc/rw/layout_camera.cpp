// Layout assertions for the camera, light and frustum types.
//
// Same job as layout.cpp -- see the comment at the top of that file -- split
// out because this group is worked on separately. Every offset below was taken
// from the compiler with a throwaway offsetof program, not read off librw's
// header: rw::Camera and rw::Light both start with PLUGINBASE, which expands to
// nothing but static members, and both end in fields RenderWare does not have.
// Eyeballing either one gets it wrong.

#include <rwcore.h>
#include <rpworld.h>

#include "rw.h"

#include <stddef.h>

#define SAME_SIZE(ours, theirs)                                                                    \
    static_assert(sizeof(ours) == sizeof(theirs), #ours " and " #theirs " differ in size")

#define SAME_OFFSET(ours, ourfield, theirs, theirfield)                                            \
    static_assert(offsetof(ours, ourfield) == offsetof(theirs, theirfield),                        \
                  #ours "." #ourfield " is not where " #theirs "." #theirfield " is")

// --- the building blocks ---------------------------------------------------
//
// RwObjectHasFrame is what a camera and a light both start with, so an error in
// it would move every field in both.

SAME_SIZE(RwObjectHasFrame, rw::ObjectWithFrame);
SAME_OFFSET(RwObjectHasFrame, object, rw::ObjectWithFrame, object);
SAME_OFFSET(RwObjectHasFrame, lFrame, rw::ObjectWithFrame, inFrame);
SAME_OFFSET(RwObjectHasFrame, sync, rw::ObjectWithFrame, syncCB);

SAME_SIZE(RwPlane, rw::Plane);
SAME_OFFSET(RwPlane, normal, rw::Plane, normal);
SAME_OFFSET(RwPlane, distance, rw::Plane, distance);

// The size assertion is the load-bearing one here: RenderWare spells the fourth
// byte out as `pad` and librw leaves it to the compiler, so if librw ever adds
// a field there the stride of frustumPlanes[] changes under iCamera.cpp and
// iModel.cpp, both of which index the array.
SAME_SIZE(RwFrustumPlane, rw::FrustumPlane);
SAME_OFFSET(RwFrustumPlane, plane, rw::FrustumPlane, plane);
SAME_OFFSET(RwFrustumPlane, closestX, rw::FrustumPlane, closestX);
SAME_OFFSET(RwFrustumPlane, closestY, rw::FrustumPlane, closestY);
SAME_OFFSET(RwFrustumPlane, closestZ, rw::FrustumPlane, closestZ);

SAME_SIZE(RwBBox, rw::BBox);
SAME_OFFSET(RwBBox, sup, rw::BBox, sup);
SAME_OFFSET(RwBBox, inf, rw::BBox, inf);

SAME_SIZE(RwV2d, rw::V2d);
SAME_SIZE(RwSphere, rw::Sphere);
SAME_SIZE(RwRGBAReal, rw::RGBAf);
SAME_OFFSET(RwRGBAReal, red, rw::RGBAf, red);
SAME_OFFSET(RwRGBAReal, green, rw::RGBAf, green);
SAME_OFFSET(RwRGBAReal, blue, rw::RGBAf, blue);
SAME_OFFSET(RwRGBAReal, alpha, rw::RGBAf, alpha);

// --- RwCamera --------------------------------------------------------------
//
// The reordering is substantial: RenderWare puts projectionType and the two
// update callbacks straight after the object and the rasters up near the front,
// while librw runs the scalars first, the derived matrices next and the rasters
// last. `projectionType` is librw's `projection`. The last six have no
// RenderWare counterpart at all and are asserted so that adding one to librw
// fails the build rather than silently shifting nothing.

SAME_SIZE(RwCamera, rw::Camera);
SAME_OFFSET(RwCamera, object, rw::Camera, object);
SAME_OFFSET(RwCamera, beginUpdate, rw::Camera, beginUpdateCB);
SAME_OFFSET(RwCamera, endUpdate, rw::Camera, endUpdateCB);
SAME_OFFSET(RwCamera, viewWindow, rw::Camera, viewWindow);
SAME_OFFSET(RwCamera, viewOffset, rw::Camera, viewOffset);
SAME_OFFSET(RwCamera, nearPlane, rw::Camera, nearPlane);
SAME_OFFSET(RwCamera, farPlane, rw::Camera, farPlane);
SAME_OFFSET(RwCamera, fogPlane, rw::Camera, fogPlane);
SAME_OFFSET(RwCamera, projectionType, rw::Camera, projection);
SAME_OFFSET(RwCamera, viewMatrix, rw::Camera, viewMatrix);
SAME_OFFSET(RwCamera, zScale, rw::Camera, zScale);
SAME_OFFSET(RwCamera, zShift, rw::Camera, zShift);
SAME_OFFSET(RwCamera, frustumPlanes, rw::Camera, frustumPlanes);
SAME_OFFSET(RwCamera, frustumCorners, rw::Camera, frustumCorners);
SAME_OFFSET(RwCamera, frustumBoundBox, rw::Camera, frustumBoundBox);
SAME_OFFSET(RwCamera, frameBuffer, rw::Camera, frameBuffer);
SAME_OFFSET(RwCamera, zBuffer, rw::Camera, zBuffer);
SAME_OFFSET(RwCamera, devView, rw::Camera, devView);
SAME_OFFSET(RwCamera, devProj, rw::Camera, devProj);
SAME_OFFSET(RwCamera, clump, rw::Camera, clump);
SAME_OFFSET(RwCamera, inClump, rw::Camera, inClump);
SAME_OFFSET(RwCamera, world, rw::Camera, world);
SAME_OFFSET(RwCamera, originalSync, rw::Camera, originalSync);
SAME_OFFSET(RwCamera, originalBeginUpdate, rw::Camera, originalBeginUpdate);
SAME_OFFSET(RwCamera, originalEndUpdate, rw::Camera, originalEndUpdate);

// The array members are asserted by size as well, because SAME_OFFSET on
// frustumPlanes only pins where the array starts. iCamera.cpp walks all six
// planes and iModel.cpp walks all eight corners.
static_assert(sizeof(((RwCamera*)0)->frustumPlanes) ==
                  sizeof(((rw::Camera*)0)->frustumPlanes),
              "RwCamera.frustumPlanes is not the same array as rw::Camera's");
static_assert(sizeof(((RwCamera*)0)->frustumCorners) ==
                  sizeof(((rw::Camera*)0)->frustumCorners),
              "RwCamera.frustumCorners is not the same array as rw::Camera's");
static_assert(sizeof(((RwCamera*)0)->devView) == sizeof(((rw::Camera*)0)->devView),
              "RwCamera.devView is not the same size as rw::Camera's RawMatrix");

// --- RpLight ---------------------------------------------------------------

SAME_SIZE(RpLight, rw::Light);
SAME_OFFSET(RpLight, object, rw::Light, object);
SAME_OFFSET(RpLight, radius, rw::Light, radius);
SAME_OFFSET(RpLight, color, rw::Light, color);
SAME_OFFSET(RpLight, minusCosAngle, rw::Light, minusCosAngle);
SAME_OFFSET(RpLight, inWorld, rw::Light, inWorld);
SAME_OFFSET(RpLight, clump, rw::Light, clump);
SAME_OFFSET(RpLight, inClump, rw::Light, inClump);
SAME_OFFSET(RpLight, world, rw::Light, world);
SAME_OFFSET(RpLight, originalSync, rw::Light, originalSync);

// --- the enumerations ------------------------------------------------------
//
// These are not layouts but they are the same kind of claim -- a value handed
// across the seam unconverted -- and the same kind of silent failure if librw
// renumbers. The shim casts each of these straight through, so every value it
// can pass is checked here rather than at the call.

static_assert((int)rwPERSPECTIVE == (int)rw::Camera::PERSPECTIVE, "camera projection renumbered");
static_assert((int)rwPARALLEL == (int)rw::Camera::PARALLEL, "camera projection renumbered");

static_assert((int)rwCAMERACLEARIMAGE == (int)rw::Camera::CLEARIMAGE, "clear mode renumbered");
static_assert((int)rwCAMERACLEARZ == (int)rw::Camera::CLEARZ, "clear mode renumbered");
static_assert((int)rwCAMERACLEARSTENCIL == (int)rw::Camera::CLEARSTENCIL, "clear mode renumbered");

static_assert((int)rwSPHEREOUTSIDE == (int)rw::Camera::SPHEREOUTSIDE, "frustum result renumbered");
static_assert((int)rwSPHEREBOUNDARY == (int)rw::Camera::SPHEREBOUNDARY, "frustum result renumbered");
static_assert((int)rwSPHEREINSIDE == (int)rw::Camera::SPHEREINSIDE, "frustum result renumbered");

static_assert((int)rwRASTERFLIPWAITVSYNC == (int)rw::Raster::FLIPWAITVSYNCH, "flip flag moved");

static_assert((int)rpLIGHTDIRECTIONAL == (int)rw::Light::DIRECTIONAL, "light type renumbered");
static_assert((int)rpLIGHTAMBIENT == (int)rw::Light::AMBIENT, "light type renumbered");
static_assert((int)rpLIGHTPOINT == (int)rw::Light::POINT, "light type renumbered");
static_assert((int)rpLIGHTSPOT == (int)rw::Light::SPOT, "light type renumbered");
static_assert((int)rpLIGHTSPOTSOFT == (int)rw::Light::SOFTSPOT, "light type renumbered");

static_assert((int)rpLIGHTLIGHTATOMICS == (int)rw::Light::LIGHTATOMICS, "light flag moved");
static_assert((int)rpLIGHTLIGHTWORLD == (int)rw::Light::LIGHTWORLD, "light flag moved");

// Both enumerations run NA, LINELIST, POLYLINE, TRILIST, TRISTRIP, TRIFAN,
// POINTLIST, so RwIm2D/RwIm3D cast the type straight through.
static_assert((int)rwPRIMTYPENAPRIMTYPE == (int)rw::PRIMTYPENONE, "primitive type renumbered");
static_assert((int)rwPRIMTYPELINELIST == (int)rw::PRIMTYPELINELIST, "primitive type renumbered");
static_assert((int)rwPRIMTYPEPOLYLINE == (int)rw::PRIMTYPEPOLYLINE, "primitive type renumbered");
static_assert((int)rwPRIMTYPETRILIST == (int)rw::PRIMTYPETRILIST, "primitive type renumbered");
static_assert((int)rwPRIMTYPETRISTRIP == (int)rw::PRIMTYPETRISTRIP, "primitive type renumbered");
static_assert((int)rwPRIMTYPETRIFAN == (int)rw::PRIMTYPETRIFAN, "primitive type renumbered");
static_assert((int)rwPRIMTYPEPOINTLIST == (int)rw::PRIMTYPEPOINTLIST, "primitive type renumbered");

// RwIm3DTransform's flags are passed unconverted, and half the call sites in
// xFX.cpp and zFX.cpp pass the bits as a literal (0x19, 0x1b) rather than by
// name, so a renumbering on either side would not even change the source.
static_assert((int)rwIM3D_VERTEXUV == (int)rw::im3d::VERTEXUV, "im3d flag moved");
static_assert((int)rwIM3D_ALLOPAQUE == (int)rw::im3d::ALLOPAQUE, "im3d flag moved");
static_assert((int)rwIM3D_NOCLIP == (int)rw::im3d::NOCLIP, "im3d flag moved");
static_assert((int)rwIM3D_VERTEXXYZ == (int)rw::im3d::VERTEXXYZ, "im3d flag moved");
static_assert((int)rwIM3D_VERTEXRGBA == (int)rw::im3d::VERTEXRGBA, "im3d flag moved");

// The render-state mode enumerations. RwRenderStateSet casts each of these
// through, so this is where the claim lives. RenderWare numbers each from 1
// after an "NA" zero, and librw numbers each from 1 with no zero, which is why
// they line up.
static_assert((int)rwBLENDZERO == (int)rw::BLENDZERO, "blend function renumbered");
static_assert((int)rwBLENDONE == (int)rw::BLENDONE, "blend function renumbered");
static_assert((int)rwBLENDSRCALPHA == (int)rw::BLENDSRCALPHA, "blend function renumbered");
static_assert((int)rwBLENDINVSRCALPHA == (int)rw::BLENDINVSRCALPHA, "blend function renumbered");
static_assert((int)rwBLENDDESTCOLOR == (int)rw::BLENDDESTCOLOR, "blend function renumbered");
static_assert((int)rwBLENDINVDESTCOLOR == (int)rw::BLENDINVDESTCOLOR, "blend function renumbered");
static_assert((int)rwBLENDSRCALPHASAT == (int)rw::BLENDSRCALPHASAT, "blend function renumbered");

static_assert((int)rwTEXTUREADDRESSWRAP == (int)rw::Texture::WRAP, "address mode renumbered");
static_assert((int)rwTEXTUREADDRESSMIRROR == (int)rw::Texture::MIRROR, "address mode renumbered");
static_assert((int)rwTEXTUREADDRESSCLAMP == (int)rw::Texture::CLAMP, "address mode renumbered");
static_assert((int)rwTEXTUREADDRESSBORDER == (int)rw::Texture::BORDER, "address mode renumbered");

static_assert((int)rwFILTERNEAREST == (int)rw::Texture::NEAREST, "filter mode renumbered");
static_assert((int)rwFILTERLINEAR == (int)rw::Texture::LINEAR, "filter mode renumbered");
static_assert((int)rwFILTERMIPNEAREST == (int)rw::Texture::MIPNEAREST, "filter mode renumbered");
static_assert((int)rwFILTERMIPLINEAR == (int)rw::Texture::MIPLINEAR, "filter mode renumbered");
static_assert((int)rwFILTERLINEARMIPNEAREST == (int)rw::Texture::LINEARMIPNEAREST,
              "filter mode renumbered");
static_assert((int)rwFILTERLINEARMIPLINEAR == (int)rw::Texture::LINEARMIPLINEAR,
              "filter mode renumbered");

static_assert((int)rwCULLMODECULLNONE == (int)rw::CULLNONE, "cull mode renumbered");
static_assert((int)rwCULLMODECULLBACK == (int)rw::CULLBACK, "cull mode renumbered");
static_assert((int)rwCULLMODECULLFRONT == (int)rw::CULLFRONT, "cull mode renumbered");

static_assert((int)rwSTENCILOPERATIONKEEP == (int)rw::STENCILKEEP, "stencil op renumbered");
static_assert((int)rwSTENCILOPERATIONZERO == (int)rw::STENCILZERO, "stencil op renumbered");
static_assert((int)rwSTENCILOPERATIONREPLACE == (int)rw::STENCILREPLACE, "stencil op renumbered");
static_assert((int)rwSTENCILOPERATIONINCRSAT == (int)rw::STENCILINCSAT, "stencil op renumbered");
static_assert((int)rwSTENCILOPERATIONDECRSAT == (int)rw::STENCILDECSAT, "stencil op renumbered");
static_assert((int)rwSTENCILOPERATIONINVERT == (int)rw::STENCILINVERT, "stencil op renumbered");
static_assert((int)rwSTENCILOPERATIONINCR == (int)rw::STENCILINC, "stencil op renumbered");
static_assert((int)rwSTENCILOPERATIONDECR == (int)rw::STENCILDEC, "stencil op renumbered");

static_assert((int)rwSTENCILFUNCTIONNEVER == (int)rw::STENCILNEVER, "stencil func renumbered");
static_assert((int)rwSTENCILFUNCTIONLESS == (int)rw::STENCILLESS, "stencil func renumbered");
static_assert((int)rwSTENCILFUNCTIONEQUAL == (int)rw::STENCILEQUAL, "stencil func renumbered");
static_assert((int)rwSTENCILFUNCTIONLESSEQUAL == (int)rw::STENCILLESSEQUAL,
              "stencil func renumbered");
static_assert((int)rwSTENCILFUNCTIONGREATER == (int)rw::STENCILGREATER, "stencil func renumbered");
static_assert((int)rwSTENCILFUNCTIONNOTEQUAL == (int)rw::STENCILNOTEQUAL,
              "stencil func renumbered");
static_assert((int)rwSTENCILFUNCTIONGREATEREQUAL == (int)rw::STENCILGREATEREQUAL,
              "stencil func renumbered");
static_assert((int)rwSTENCILFUNCTIONALWAYS == (int)rw::STENCILALWAYS, "stencil func renumbered");
