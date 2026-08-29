#include "iFMV.h"

#include "iFile.h"
#include "iFMVAudio.h"
#include "iFMVDecoder.h"
#include "iPadHost.h"
#include "iTime.h"
#include "iWindow.h"

#include <rwcore.h>

#include "iCamera.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Full-motion video.
//
// The GameCube build decodes Bink through RAD's rad3d straight into a GX
// framebuffer, and none of that ports: Bink is proprietary middleware that
// cannot be redistributed. It also is not needed. The port's assets are the
// Xbox release's, and the Xbox shipped its movies as .xmv -- an XMV container
// carrying WMV2 video with IMA ADPCM audio -- so there is no Bink file here to
// decode in the first place.
//
// What decodes WMV2 is behind iFMVDecoder.h, and what plays the sound is behind
// iFMVAudio.h. This file is the part that is the same whatever answers those
// two: find the file, pace the frames, watch for the skip button, put the
// picture on the screen, and give the game back the answer it expects.
//
// A build with no decoder still works. iFMVDecoderOpen returns NULL, and a
// movie that cannot be played is reported as one that finished -- which is what
// every caller already copes with, because a skipped movie and a finished movie
// are the same thing to the game.

namespace
{
    // zFMV.cpp:35 does sprintf(fullname, "%s%s", filename, ".bik") before
    // calling here, and that file is shared with the console, so the extension
    // cannot be fixed at the call site. It is swapped here instead.
    const char* const kWantedExtension = ".xmv";

    // Movies live beside the rest of the assets. The name arrives as
    // "FMV\HILogo.bik" -- a DOS separator, because that is what the asset table
    // in zFMV.cpp was written with.
    const char* const kMovieDirectory = "fmv/";

    bool sReportedNoDecoder;

    // Build "<assets>/fmv/<name>.xmv" out of what the game passed.
    //
    // Three things have to be undone. The backslash separator is not one on a
    // host that takes paths seriously; the directory part of the game's name is
    // dropped rather than translated, because every movie is in one place; and
    // the extension is swapped.
    void buildPath(char* out, size_t outsize, const char* root, const char* game_name)
    {
        // The leaf, after the last separator of either kind.
        const char* leaf = game_name;
        for (const char* p = game_name; *p; p++)
        {
            if (*p == '/' || *p == '\\')
            {
                leaf = p + 1;
            }
        }

        char stem[128];
        snprintf(stem, sizeof(stem), "%s", leaf);

        char* dot = strrchr(stem, '.');
        if (dot != NULL)
        {
            *dot = 0;
        }

        snprintf(out, outsize, "%s%s%s%s", root, kMovieDirectory, stem, kWantedExtension);
    }

    // Open the first spelling of the name that exists.
    //
    // The asset table asks for "THQLogo" and the file on disk is "thqlogo.xmv".
    // Windows does not care and this would work by accident there; a
    // case-sensitive filesystem does, and the port is meant to reach one. Three
    // spellings cover every mismatch in the shipped set without reading the
    // directory, which needs an API this file otherwise does not.
    iFMVDecoder* openAnyCase(char* path, size_t pathsize, const char* root,
                             const char* game_name, iFMVDecoderInfo* info)
    {
        buildPath(path, pathsize, root, game_name);

        iFMVDecoder* dec = iFMVDecoderOpen(path, info);
        if (dec != NULL)
        {
            return dec;
        }

        // The leaf, lowered and raised in turn. Only the stem is touched: the
        // directory came from us and the extension is a constant.
        size_t rootlen = strlen(root) + strlen(kMovieDirectory);
        for (int pass = 0; pass < 2; pass++)
        {
            char alt[512];
            snprintf(alt, sizeof(alt), "%s", path);
            for (char* p = alt + rootlen; *p && *p != '.'; p++)
            {
                *p = (pass == 0) ? (char)tolower((unsigned char)*p)
                                 : (char)toupper((unsigned char)*p);
            }
            dec = iFMVDecoderOpen(alt, info);
            if (dec != NULL)
            {
                snprintf(path, pathsize, "%s", alt);
                return dec;
            }
        }

        return NULL;
    }

    // Where the movie goes on a 640x480 screen, with its aspect kept.
    //
    // The movies are not all the shape of the screen: 640x480 is, and 720x480
    // and 720x486 are anamorphic NTSC that would be visibly stretched if the
    // quad were just the whole screen. So the picture is fitted and centred and
    // whatever is left over stays black, which is what a letterbox is.
    void fitRect(U32 vw, U32 vh, F32 sw, F32 sh, F32* x0, F32* y0, F32* x1, F32* y1)
    {
        // 720x480 and 720x486 are 4:3 content in non-square pixels; displaying
        // them at their pixel dimensions is the stretch, not the fix. Every
        // movie in this game is 4:3, so that is what they are fitted to.
        (void)vw;
        (void)vh;
        const F32 aspect = 4.0f / 3.0f;

        F32 w = sw;
        F32 h = sw / aspect;
        if (h > sh)
        {
            h = sh;
            w = sh * aspect;
        }

        *x0 = 0.5f * (sw - w);
        *y0 = 0.5f * (sh - h);
        *x1 = *x0 + w;
        *y1 = *y0 + h;
    }

    void drawFrame(RwRaster* raster, F32 sw, F32 sh, F32 x0, F32 y0, F32 x1, F32 y1)
    {
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
        RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);

        // The bars. Drawn rather than cleared so that this does not depend on
        // the camera's clear colour being black, and so a movie narrower than
        // the screen cannot leave the previous frame showing down the sides.
        if (x0 > 0.0f || y0 > 0.0f)
        {
            rwGameCube2DVertex bar[4];
            F32 bz = RwIm2DGetFarScreenZ();
            const F32 bx[4] = { 0.0f, 0.0f, sw, sw };
            const F32 by[4] = { 0.0f, sh, 0.0f, sh };
            for (int i = 0; i < 4; i++)
            {
                bar[i].x = bx[i];
                bar[i].y = by[i];
                bar[i].z = bz;
                bar[i].emissiveColor.red = 0;
                bar[i].emissiveColor.green = 0;
                bar[i].emissiveColor.blue = 0;
                bar[i].emissiveColor.alpha = 255;
                bar[i].u = 0.0f;
                bar[i].v = 0.0f;
            }
            RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, &bar[0], 4);
        }

        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)raster);

        rwGameCube2DVertex vx[4];
        F32 z = RwIm2DGetFarScreenZ();
        const F32 px[4] = { x0, x0, x1, x1 };
        const F32 py[4] = { y0, y1, y0, y1 };
        const F32 pu[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        const F32 pv[4] = { 0.0f, 1.0f, 0.0f, 1.0f };

        for (int i = 0; i < 4; i++)
        {
            vx[i].x = px[i];
            vx[i].y = py[i];
            vx[i].z = z;
            // White: the frame is the picture, not something being tinted.
            vx[i].emissiveColor.red = 255;
            vx[i].emissiveColor.green = 255;
            vx[i].emissiveColor.blue = 255;
            vx[i].emissiveColor.alpha = 255;
            vx[i].u = pu[i];
            vx[i].v = pv[i];
        }

        RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, &vx[0], 4);

        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    }

    // Copy one decoded frame into the raster.
    //
    // Row by row rather than one memcpy: the decoder's pitch and the raster's
    // stride are both padded, and by different amounts.
    void uploadFrame(RwRaster* raster, const void* pixels, U32 pitch, U32 w, U32 h)
    {
        RwUInt8* dst = RwRasterLock(raster, 0, rwRASTERLOCKWRITE | rwRASTERLOCKNOFETCH);
        if (dst == NULL)
        {
            return;
        }

        U32 stride = (U32)raster->stride;
        U32 rowbytes = w * 4;
        const RwUInt8* src = (const RwUInt8*)pixels;

        for (U32 y = 0; y < h; y++)
        {
            memcpy(dst + (size_t)y * stride, src + (size_t)y * pitch, rowbytes);
        }

        RwRasterUnlock(raster);
    }
}

U32 iFMVPlay(char* filename, U32 buttons, F32 time, bool skippable, bool lockController)
{
    (void)lockController;

    if (filename == NULL)
    {
        // Retail's own answer to a null filename.
        return 1;
    }

    const char* root = iFileAssetRoot();
    char rootbuf[512];
    if (root[0] == 0)
    {
        rootbuf[0] = 0;
    }
    else
    {
        size_t n = strlen(root);
        bool slash = (n > 0) && (root[n - 1] == '/' || root[n - 1] == '\\');
        snprintf(rootbuf, sizeof(rootbuf), "%s%s", root, slash ? "" : "/");
    }

    char path[512];
    iFMVDecoderInfo info;
    iFMVDecoder* dec = openAnyCase(path, sizeof(path), rootbuf, filename, &info);

    if (dec == NULL)
    {
        // Not an error the game should see. Two of the movies the asset table
        // names -- FMV\Tak and FMV\JN -- have no file in the Xbox set at all,
        // so this is a normal outcome and not only a "no decoder" one.
        if (!sReportedNoDecoder)
        {
            sReportedNoDecoder = true;
            printf("bfbb: no movie for '%s' (decoder: %s); movies that will not open are "
                   "reported as finished\n",
                   filename, iFMVDecoderName());
            fflush(stdout);
        }
        return 0;
    }

    // The screen, which is the virtual screen rather than the window: the
    // picture is composited at the size the game renders at and scaled to the
    // window with everything else at present time.
    RwVideoMode mode;
    RwEngineGetVideoModeInfo(&mode, RwEngineGetCurrentVideoMode());
    F32 sw = (F32)mode.width;
    F32 sh = (F32)mode.height;

    RwCamera* cam = RwCameraGetCurrentCamera();
    RwCamera* owned = NULL;
    if (cam == NULL)
    {
        // The boot logos play before the game has a camera. One is made for the
        // movie and destroyed with it.
        owned = iCameraCreate((S32)mode.width, (S32)mode.height, 0);
        cam = owned;
    }

    RwRaster* raster = RwRasterCreate((RwInt32)info.width, (RwInt32)info.height, 32,
                                      rwRASTERTYPETEXTURE);

    S32 audio = FALSE;
    if (info.sample_rate != 0 && info.channels != 0)
    {
        audio = iFMVAudioOpen(info.sample_rate, info.channels);
    }

    F32 x0, y0, x1, y1;
    fitRect(info.width, info.height, sw, sh, &x0, &y0, &x1, &y1);

    U32 ended_with = 0;
    U32 audio_written = 0;

    // Audio decoded but not yet accepted by the device.
    //
    // iFMVAudioWrite takes what fits in its free blocks and no more, so a read
    // that outruns it leaves a remainder. That remainder is the movie's sound:
    // dropping it would skip through the audio while the picture kept time, and
    // the two would come apart. It is held here and offered again next time.
    S16 abuf[8192 * 2];
    U32 apend = 0;
    U32 aoff = 0;

    // The audio clock stops when the track does, and the video usually outlasts
    // it by a frame or two. See the handover below.
    bool audio_drained = false;
    bool handed_over = false;
    F32 clock_at_handover = 0.0f;
    iTime wall_at_handover = 0;
    iTime started = iTimeGet();
    F32 next_frame_at = 0.0f;
    bool have_frame = false;

    if (cam != NULL && raster != NULL)
    {
        while (true)
        {
            iWindowPump();
            if (iWindowShouldClose())
            {
                break;
            }

            // Sound first: the device is what the picture is paced against, and
            // it must not be allowed to run dry while a frame is decoded.
            if (audio)
            {
                if (apend == 0)
                {
                    apend = iFMVDecoderReadAudio(dec, abuf, 8192);
                    aoff = 0;
                    if (apend == 0)
                    {
                        audio_drained = true;
                    }
                }
                if (apend > 0)
                {
                    U32 wrote = iFMVAudioWrite(abuf + (size_t)aoff * info.channels, apend);
                    audio_written += wrote;
                    aoff += wrote;
                    apend -= wrote;
                }
            }

            // The clock. The audio device's is used when there is one, because
            // that is the one the player hears; a movie with no sound falls
            // back to the wall clock.
            F32 now;
            if (audio && info.sample_rate != 0)
            {
                U32 queued = iFMVAudioQueued();
                U32 played = (audio_written > queued) ? (audio_written - queued) : 0;
                now = (F32)played / (F32)info.sample_rate;

                // Hand over to the wall clock when the sound runs out.
                //
                // This clock is everything written minus everything still
                // queued, so once the track has ended and the device has
                // drained it stops: played stops rising and queued is already
                // zero. The picture is normally a frame or two longer than the
                // sound, and a movie paced on a stopped clock never reaches
                // those frames -- so it never decodes, never sees the end of
                // the file, and sits on the last thing it drew. It hangs.
                //
                // Taking over from where the audio clock stopped rather than
                // from the start of the movie keeps the two continuous, so the
                // last frames play at the same rate as everything before them.
                if (audio_drained && queued == 0)
                {
                    if (!handed_over)
                    {
                        handed_over = true;
                        clock_at_handover = now;
                        wall_at_handover = iTimeGet();
                    }
                    now = clock_at_handover + iTimeDiffSec(wall_at_handover, iTimeGet());
                }
            }
            else
            {
                now = iTimeDiffSec(started, iTimeGet());
            }

            if (!have_frame || now >= next_frame_at)
            {
                const void* pixels = NULL;
                U32 pitch = 0;
                F32 pts = -1.0f;

                if (!iFMVDecoderNextFrame(dec, &pixels, &pitch, &pts))
                {
                    break;
                }

                uploadFrame(raster, pixels, pitch, info.width, info.height);
                have_frame = true;

                // The decoder's own timestamp when it has one, because a
                // container's frame durations are not always uniform; the
                // nominal frame time when it does not.
                next_frame_at = (pts >= 0.0f) ? (pts + info.frame_time)
                                              : (next_frame_at + info.frame_time);
            }

            RwCameraBeginUpdate(cam);
            drawFrame(raster, sw, sh, x0, y0, x1, y1);
            RwCameraEndUpdate(cam);
            RwCameraShowRaster(cam, NULL, rwRASTERFLIPWAITVSYNC);

            // The skip button, and the one place `time` is honoured.
            //
            // Every caller passes a `time` -- 2.0 for the boot logos, 0.1 for
            // the attract demo -- and the console passes it down into Bink's
            // player, where what it does is not visible from here. Read as the
            // period before a skip is accepted it explains both numbers: a logo
            // cannot be dismissed the instant it appears, and the demo can be.
            // Recorded as an interpretation rather than a fact, because the
            // Bink side is not in the decomp.
            if (skippable && buttons != 0 && iTimeDiffSec(started, iTimeGet()) >= time)
            {
                iPadHostPoll();
                const iPadHostState* pad = iPadHostGet(0);
                if (pad != NULL && pad->connected && (pad->buttons & buttons) != 0)
                {
                    ended_with = pad->buttons & buttons;
                    break;
                }
            }
        }
    }

    if (audio)
    {
        iFMVAudioClose();
    }
    if (raster != NULL)
    {
        RwRasterDestroy(raster);
    }
    if (owned != NULL)
    {
        iCameraDestroy(owned);
    }
    iFMVDecoderClose(dec);

    // 0 is "ran to the end", and the button that stopped it otherwise.
    return ended_with;
}
