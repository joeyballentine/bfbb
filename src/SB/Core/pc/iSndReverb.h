#ifndef ISNDREVERB_H
#define ISNDREVERB_H

#include <types.h>

#include "iSndHost.h"

// PC-only: the environmental reverb the Xbox release has in Mermalair and the
// caves, as an actual reverb rather than a parameter block handed to hardware.
//
// **Why this exists at all.** zScene.cpp's zSceneInitEnvironmentalSoundEffect
// already picks SND_EFFECT_CAVE for the right nine scenes, and xSnd already
// forwards it -- that chain is shipping code and it is complete. What is empty
// is the platform end: iSndSetEnvironmentalEffect has no body in GameCube
// retail, which is exactly why the console versions have no reverb and the
// Xbox does. The Xbox end is one DirectSound call, because the console has a
// reverb in its audio DSP. This port's backend is a software mixer feeding one
// WASAPI stream, so there is nothing to configure and the reverb has to be
// written.
//
// **What is faithful and what is not.** The parameters are exact: they are
// the twelve fields the Xbox build stores on its stack before calling
// SetI3DL2Listener, recovered from that code and reproduced in iSnd.cpp.
//
// The algorithm is a reconstruction, and here is exactly how good a one. The
// Xbox runs its reverb as microcode in the APU's global-processor DSP, loaded
// from the effects image the XDK calls dsstdfx -- DirectSound Standard Effects.
// That image is not on the game disc and not in the XBE, no disassembly of it
// has been published, and the emulators that run Xbox audio correctly do so by
// emulating the DSP and executing the microcode rather than by describing what
// it computes. So the Xbox's own topology is not available to copy, and nothing
// here samples-matches it.
//
// What IS available is the same company's I3DL2 reverb for the PC, shipped as
// a DirectSound Media Object in the same years, taking a parameter struct of
// the same name -- and that one has been reverse engineered, by OpenMPT, whose
// implementation this design is taken from. The two are siblings rather than
// the same thing, and the weak link in the chain is exactly there: the Xbox's
// standard effects are a DSP port of the PC's standard effects suite, but that
// is an inference from the shared name, API and parameter struct, not a
// verified fact.
//
// It is still a far better bet than a textbook reverb, which is what the first
// version of this file was.
//
// **The structure**, per output channel:
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
// It is a recirculating loop, not a bank of parallel combs: the last stage's
// output is what closes it. Diffusion is the allpass coefficient, density
// scales the delays, and the level and decay come out of the parameters.
//
// **Threading.** Nothing here is synchronised. iSndReverbProcess runs on the
// backend's render thread and iSndReverbSet on the game thread, so the caller
// owns the exclusion; iSndHostWin32 already holds its mixer lock across both.

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
