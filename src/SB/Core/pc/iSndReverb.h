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
// SetI3DL2Listener, recovered from that code and reproduced in iSnd.cpp. The
// algorithm is not, and cannot be -- the Xbox's reverb is proprietary DSP
// microcode, so no reimplementation samples-matches it. What is reproduced is
// the description: an I3DL2 reverb driven by the game's own numbers.
//
// **The structure**, per output channel:
//
//   1. a tapped delay line for the early reflections, first tap at
//      reflections_delay
//   2. a pre-delay of reflections_delay + reverb_delay into the late network
//   3. eight parallel combs, each with a one-pole shelf in its feedback loop,
//      so that low and high frequencies can decay at different rates
//   4. four series allpasses, to smear what the combs leave periodic
//   5. summed as room+reflections and room+reverb, and added to the dry mix
//
// The comb and allpass delay lengths are Schroeder-Moorer's, which is a choice;
// everything about how loud and how long is derived from the parameters and is
// not.
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
