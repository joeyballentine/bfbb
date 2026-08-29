#ifndef ISNDREVERB_H
#define ISNDREVERB_H

#include <types.h>

#include "iSndHost.h"

// PC-only: the environmental reverb the Xbox release has in Mermalair and the
// caves, as an actual reverb rather than a parameter block handed to hardware.
//
// The game side is complete shipping code: zSceneInitEnvironmentalSoundEffect
// picks SND_EFFECT_CAVE for the right nine scenes and xSnd forwards it. What is
// empty is iSndSetEnvironmentalEffect, which has no body in GameCube retail --
// that is why the console versions have no reverb and the Xbox does. The Xbox
// end is one DirectSound call into a reverb in its audio DSP. This backend is a
// software mixer feeding one WASAPI stream, so the reverb has to be written.
//
// The parameters are exact: the twelve fields the Xbox build stores before
// calling SetI3DL2Listener, recovered from that code and reproduced in
// iSnd.cpp.
//
// The algorithm is a reconstruction. The Xbox's reverb is microcode in the
// APU's DSP, loaded from the XDK's dsstdfx image, which is not on the disc and
// has never been disassembled -- so nothing here samples-matches it. This
// follows the same company's PC I3DL2 reverb, a DirectSound Media Object taking
// a parameter struct of the same name, as reverse engineered by OpenMPT. That
// the two are the same design is an inference from the shared name, API and
// struct, not a verified fact.
//
// The structure, per output channel:
//
//   1. a one-pole room filter, set by room_hf
//   2. a delay line the early reflections are tapped from -- five taps spread
//      across the window between the first reflection and the late onset, with
//      fixed weights and alternating signs, then one short allpass
//   3. a sixth tap of the same line, at the late onset, feeding the late chain
//   4. an energy-preserving 2x2 matrix mixing the two channels' loops
//   5. six absorbent allpasses in series, delays in a geometric series from
//      67 ms down to 22 (75 to 25 on the right), each with a damping filter and
//      a decay gain in its path, plus one more delay in the middle of the chain
//   6. the chain's output summed from per-stage taps and fed back to step 4
//
// It recirculates rather than running parallel combs: the last stage's output
// closes the loop. Diffusion is the allpass coefficient, density scales the
// delays, and the level and decay come out of the parameters.
//
// Nothing here is synchronised. iSndReverbProcess runs on the backend's render
// thread and iSndReverbSet on the game thread, so the caller owns the
// exclusion; iSndHostWin32 holds its mixer lock across both.

// `rate` is the output rate the process step will be called at. Re-initialising
// at a new rate is allowed and throws away the tail.
void iSndReverbInit(U32 rate);
void iSndReverbExit();

// NULL removes the effect. Either way the change is ramped rather than applied
// at a block boundary, so a scene that loads while something is still audible
// does not click.
void iSndReverbSet(const iSndHostReverb* params);

// True when there is nothing to add and no tail left to add it from, so the
// caller may skip the process step entirely.
bool iSndReverbIdle();

// Adds the wet signal to `frames` frames of interleaved stereo, in place. The
// dry signal is left exactly as it arrived.
void iSndReverbProcess(F32* stereo, U32 frames);

#endif
