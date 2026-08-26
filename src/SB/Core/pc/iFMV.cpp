#include "iFMV.h"

#include <stdio.h>

// Full-motion video, and the port does not play any.
//
// This is a DECISION rather than a gap, and it is not going to be closed by
// porting gc/iFMV.cpp. That file decodes Bink through RAD's rad3d into a GX
// framebuffer; Bink is proprietary middleware that cannot be redistributed, so
// there is no version of "port this file" that ships. The plan of record is to
// convert the videos with ffmpeg ahead of time and play the results back with
// something else.
//
// Returning immediately is the least disruptive thing to do until that exists.
// zFMV.cpp:36-41 suspends sound, sets the game state to
// eGameOstrich_PlayingMovie, calls this, restores both, and returns what this
// returned. So a movie is "played" instantly, the game advances to whatever
// follows it, and nothing waits. Returning a button instead would tell the game
// the player skipped it, which is a different thing and one some callers branch
// on.
//
// THREE THINGS WHOEVER BUILDS THE REAL ONE WILL WANT, recorded here because
// they are cheap to note now and annoying to rediscover:
//
//   * **The filename arrives with ".bik" already on it.** zFMV.cpp:35 does
//     `sprintf(fullname, "%s%s", filename, ".bik")` before calling. Converted
//     videos will not be .bik, so the extension has to be swapped here rather
//     than at the call site -- zFMV.cpp is shared with the console.
//   * **`buttons` is a mask of what may skip playback**, and the return value is
//     which one did, or 0 for "ran to the end". `skippable` and
//     `lockController` gate whether input is read at all.
//   * **Sound is already suspended** around this call, so a real player owns
//     its own audio for the duration and must not assume the game's mixer is
//     running.
//
// It says so once, rather than every time: some sequences play several movies
// back to back, and the attract loop plays one on a timer forever.

U32 iFMVPlay(char* filename, U32 buttons, F32 time, bool skippable, bool lockController)
{
    static bool sReported = false;

    if (!sReported)
    {
        sReported = true;
        printf("[pcport] full-motion video is not implemented; movies are skipped\n");
        fflush(stdout);
    }

    if (filename == NULL)
    {
        // Retail's own answer to a null filename.
        return 1;
    }

    // 0 is "ran to the end", not "skipped".
    return 0;
}
