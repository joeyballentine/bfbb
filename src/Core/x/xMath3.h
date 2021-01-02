#ifndef XMATH3_H
#define XMATH3_H

#include "xVec3.h"
#include "xVec3Inlines.h"

// Size: 0x30
struct xMat3x3
{
    xVec3 right;
    int32 flags;
    xVec3 up;
    uint32 pad1;
    xVec3 at;
    uint32 pad2;

    xMat3x3& operator=(const xMat3x3&); // temp
};

// Size: 0x40
struct xMat4x3 : xMat3x3
{
    xVec3 pos;
    uint32 pad3;

    xMat4x3& operator=(const xMat4x3&); // temp
};

struct xSphere
{
    xVec3 center;
    float32 r;
};

struct xBox
{
    xVec3 upper;
    xVec3 lower;
};

struct xBBox
{
    xVec3 center;
    xBox box;
};

struct xCylinder
{
    xVec3 center;
    float32 r;
    float32 h;
};

struct xQuat
{
    xVec3 v;
    float32 s;

    xQuat& operator=(const xQuat&); // temp
};

struct xVec4
{
    float32 x;
    float32 y;
    float32 z;
    float32 w;
};

struct xRot
{
    xVec3 axis;
    float32 angle;
};

extern xVec3 g_O3;
extern xVec3 g_X3;
extern xVec3 g_Y3;
extern xVec3 g_Z3;
extern xMat4x3 g_I3;

// For some reason, this function is copied across 29 object files...
//   and each instance of this function has a LOCAL symbol (see symbols.txt)
// Declaring the function as 'static' here will both give it a LOCAL symbol
//   and allow it to be defined in multiple .cpp files without any link errors.
// We could also define it as static in each .cpp file, but it's not required.
static void xMat3x3RMulVec(xVec3* o, const xMat3x3* m, const xVec3* v);

void xMat3x3Copy(xMat3x3* o, const xMat3x3* m);
void xMat4x3Copy(xMat4x3* o, const xMat4x3* m);
void xMat4x3Mul(xMat4x3* o, const xMat4x3* a, const xMat4x3* b);
void xMat3x3Euler(xMat3x3* m, float32 yaw, float32 pitch, float32 roll);
void xRotCopy(xRot* o, const xRot* r);
void xMat4x3Toworld(xVec3* o, const xMat4x3* m, const xVec3* v);
void xMat3x3Rot(xMat3x3* m, const xVec3* a, float32 t);
void xMat3x3RotC(xMat3x3* m, float32 _x, float32 _y, float32 _z, float32 t);
void xMat4x3Identity(xMat4x3* m);
void xMat3x3Normalize(xMat3x3* o, const xMat3x3* m);
void xMat4x3Tolocal(xVec3* o, const xMat4x3* m, const xVec3* v);
void xMat3x3Tolocal(xVec3* o, const xMat3x3* m, const xVec3* v);
void xMat4x3MoveLocalRight(xMat4x3* m, float32 mag);
void xMat4x3MoveLocalAt(xMat4x3* m, float32 mag);
void xMat4x3MoveLocalUp(xMat4x3* m, float32 mag);
void xMat3x3GetEuler(const xMat3x3* m, xVec3* a);
void xMat3x3Euler(xMat3x3* m, const xVec3* ypr);
void xQuatToMat(const xQuat* q, xMat3x3* m);
void xQuatDiff(xQuat* o, const xQuat* a, const xQuat* b);
float32 xQuatGetAngle(const xQuat* q);
void xQuatFromMat(xQuat* q, const xMat3x3* m);
void xQuatConj(xQuat* o, const xQuat* q);
void xMat3x3LookAt(xMat3x3* m, const xVec3* pos, const xVec3* at);
float32 xMat3x3LookVec(xMat3x3* m, const xVec3* at);

#endif