#ifndef IPAD_H
#define IPAD_H

#include <types.h>

struct _tagiPad
{
    S32 port;
};

struct _tagxPad;
struct _tagxRumble;
struct xModelInstance;

S32 iPadInit();
_tagxPad* iPadEnable(_tagxPad* pad, S16 port);
S32 iPadConvStick(float value);

// PC-only. The GameCube gets this shape from PADClamp, which a host build has
// no equivalent of. See iPad.cpp.
void iPadShapeStick(F32 inX, F32 inY, F32 max, F32 xy, F32* outX, F32* outY);
S32 iPadUpdate(_tagxPad* pad, U32* on);
S32 iPadConvFromGCN(U32 a, U32 b, U32 c);
void iPadRumbleFx(_tagxPad* p, _tagxRumble* r, F32 time_passed);
void iPadStopRumble(_tagxPad* pad);
void iPadStopRumble();
void iPadStartRumble(_tagxPad* pad, _tagxRumble* rumble);
void iPadKill();

// The pad bits a talk box's links were authored against, from the bits the pad
// reads. The Xbox archives transpose O and TRIANGLE. See iPad.cpp.
U32 iPadTalkBoxButtons(U32 pressed);

// Draws the talk prompt over an NPC as the button that talks on the player's
// own controller or keyboard. Begin points `model` at that model for one draw
// and End points it back; `talk` is whether the glyph is a talk prompt at all.
// Defined in iPadGlyph.cpp.
void iPadGlyphModelBegin(xModelInstance* model, S32 talk);
void iPadGlyphModelEnd(xModelInstance* model);

// The texture to draw for a UI sprite whose texture is `textureID`: the
// player's glyph when that is one of font.HIP's button textures, else `disc`.
// Defined in iPadGlyph.cpp.
struct RwTexture;
RwTexture* iPadGlyphTexture(U32 textureID, RwTexture* disc);

// Whether vibration starts on: config.ini's input.vibration, which on PC is
// one setting for the install rather than a value in each save file.
U8 iPadVibrationDefault();

// PC-only. Called once a frame on port 0 with whether a menu is up, after the
// devices are read and before anything sees the buttons; what it returns is
// added to them. The menu pointer lives behind it (menu/iMenuMouse.h), which
// needs the game's UI and so is set from iSystem.cpp rather than called here.
typedef U32 (*iPadMenuHookFn)(S32 inMenu);
void iPadSetMenuHook(iPadMenuHookFn fn);

#endif
