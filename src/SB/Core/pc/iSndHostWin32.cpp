#include "iSndHost.h"
#include "iHost.h"
#include "iSndReverb.h"

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The Windows audio backend: a software mixer feeding one WASAPI stream.
//
// The seam in iSndHost.h describes a device with 64 independent voices, each
// with its own rate, volume per side and pitch. No host audio API offers that
// directly -- WASAPI renders one stream, and the APIs that do offer voices
// (XAudio2, DirectSound) bring a submix graph, their own threading model and
// their own idea of what a voice is. Mixing here instead is both less code and
// a closer fit: the game's model becomes 64 float accumulations per output
// frame, and the only thing the operating system is asked for is somewhere to
// put the result.
//
// It also makes the pitch and rate handling exact rather than approximate.
// Every voice is resampled to the device rate by stepping a fractional read
// position, so a sound authored at 22050 Hz played at pitch 1.3 on a 48 kHz
// device is one multiply, and the point where it runs out is known to the
// sample rather than to the frame.
//
// **Failing to open a device is not an error.** A machine with no output, or
// with the endpoint in use exclusively, still has to run the game correctly,
// and a great deal of BFBB waits on sounds -- dialogue lines, cutscene beats,
// NPC barks. So the mixer runs either way: with a device the voices advance
// because the mixer consumed them, and without one they advance on the
// monotonic clock in iSndHostUpdate. The game cannot tell the difference, which
// is the whole point of iSndHostNull.cpp and is preserved here rather than
// replaced.

// ---------------------------------------------------------------------------
// COM identifiers
//
// Declared here rather than taken from uuid.lib. The library is not part of any
// other dependency this port has, and four GUIDs are not worth acquiring one
// for -- particularly as the linker error it produces when it is missing points
// nowhere near audio.

static const CLSID kCLSID_MMDeviceEnumerator = { 0xBCDE0395,
                                                 0xE52F,
                                                 0x467C,
                                                 { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69,
                                                   0x2E } };

static const IID kIID_IMMDeviceEnumerator = { 0xA95664D2,
                                              0x9614,
                                              0x4F35,
                                              { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };

static const IID kIID_IAudioClient = { 0x1CB9AD4C,
                                       0xDBFA,
                                       0x4C32,
                                       { 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2 } };

static const IID kIID_IAudioRenderClient = { 0xF294ACFC,
                                             0x3146,
                                             0x4483,
                                             { 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60,
                                               0xE2 } };

// The two WAVEFORMATEXTENSIBLE subformats that matter, from ksmedia.h. Compared
// by value so that header is not needed either.
static const GUID kSUBTYPE_PCM = { 0x00000001,
                                   0x0000,
                                   0x0010,
                                   { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };

static const GUID kSUBTYPE_IEEE_FLOAT = { 0x00000003,
                                          0x0000,
                                          0x0010,
                                          { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };

// ---------------------------------------------------------------------------
// Voices

struct hvoice
{
    bool acquired;
    bool playing;
    bool paused;
    bool looping;

    // Borrowed from iSndData's cache, which pins it for as long as the voice
    // holds it. NULL is a legal voice: it plays silently and still ends at the
    // right moment, which is what a missing or unreadable asset sounds like.
    const void* data;
    U32 frames;
    U32 channels;
    U32 bits;
    U32 rate;
    U32 loop_start;
    U32 loop_end;

    // Read position in source frames. Fractional because the source rate and
    // the device rate are unrelated.
    double pos;

    // Where the mix is heading, and where it is now. Ramped across a block
    // rather than applied at its start: the game moves volumes every frame as
    // an emitter approaches, and stepping the gain discontinuously at a block
    // boundary is audible as a click on exactly the sounds that move most.
    float volL;
    float volR;
    float curL;
    float curR;

    float pitch;
};

static hvoice sVoices[ISNDHOST_MAX_VOICES];
static CRITICAL_SECTION sLock;
static bool sLockReady;

// ---------------------------------------------------------------------------
// Device

static IMMDeviceEnumerator* sEnumerator;
static IMMDevice* sDevice;
static IAudioClient* sClient;
static IAudioRenderClient* sRender;
static HANDLE sBufferEvent;
static HANDLE sQuitEvent;
static HANDLE sThread;

static U32 sOutRate;
static U32 sOutChannels;
static bool sOutFloat;
static U32 sOutBits;
static U32 sBufferFrames;

static bool sDeviceUp;
static char sName[128];

// The silent path's clock. Only read and written by iSndHostUpdate, on the game
// thread, and only when there is no device.
static U64 sLastSilentNs;

// Scratch for one mixed block, in float. Sized on the endpoint buffer, which
// shared-mode WASAPI reports once at initialisation.
static float* sMixBuffer;
static U32 sMixBufferFrames;

// The environmental reverb the game last asked for. Kept here as well as in
// iSndReverb because the reverb's delay lengths depend on the output rate,
// which is not known until a device opens and changes if one is swapped: the
// game sets this once when a scene loads and must not have to set it again.
static iSndHostReverb sReverb;
static bool sReverbWanted;

// ---------------------------------------------------------------------------
// Mixing

static bool iVoiceValid(S32 voice)
{
    return voice >= 0 && voice < ISNDHOST_MAX_VOICES && sVoices[voice].acquired;
}

// Entering an uninitialised CRITICAL_SECTION is undefined rather than merely
// wrong, and the seam does not promise that iSndHostInit runs first -- the null
// backend tolerates being called cold, so this one has to as well. Only the
// game thread reaches these entry points, so a plain flag is enough.
static void iEnsureLock()
{
    if (!sLockReady)
    {
        InitializeCriticalSection(&sLock);
        sLockReady = true;
    }
}

// One source frame, as a float in -1..1, with linear interpolation between the
// two frames it falls between. `ch` is 0 for left and 1 for right; a mono
// source answers the same value to both.
static inline float iSampleAt(const hvoice* v, double pos, U32 ch)
{
    if (v->data == NULL)
    {
        return 0.0f;
    }

    U32 i0 = (U32)pos;
    if (i0 >= v->frames)
    {
        return 0.0f;
    }

    U32 i1 = i0 + 1;
    if (i1 >= v->frames)
    {
        // Past the end, hold the last frame rather than wrapping into whatever
        // follows in the cache.
        i1 = i0;
    }

    float frac = (float)(pos - (double)i0);
    U32 c = (v->channels > 1 && ch < v->channels) ? ch : 0;
    U32 stride = v->channels;

    if (v->bits == 16)
    {
        const S16* p = (const S16*)v->data;
        float a = (float)p[i0 * stride + c] * (1.0f / 32768.0f);
        float b = (float)p[i1 * stride + c] * (1.0f / 32768.0f);
        return a + (b - a) * frac;
    }

    const U8* p = (const U8*)v->data;
    float a = ((float)p[i0 * stride + c] - 128.0f) * (1.0f / 128.0f);
    float b = ((float)p[i1 * stride + c] - 128.0f) * (1.0f / 128.0f);
    return a + (b - a) * frac;
}

// Advance one voice past the end of its data, looping if it should. Returns
// false when the voice has finished.
static bool iWrap(hvoice* v)
{
    // A looping voice may turn round before the data runs out, which is what a
    // soundtrack override does. Everything else ends where the samples do.
    U32 end = v->frames;
    if (v->looping && v->loop_end != 0 && v->loop_end <= v->frames)
    {
        end = v->loop_end;
    }

    if (v->pos < (double)end)
    {
        return true;
    }

    if (!v->looping || end == 0)
    {
        return false;
    }

    U32 start = v->loop_start < end ? v->loop_start : 0;
    double span = (double)(end - start);

    if (span <= 0.0)
    {
        v->pos = (double)start;
        return true;
    }

    // A loop shorter than one output step would spin here, so this subtracts
    // repeatedly rather than once.
    while (v->pos >= (double)end)
    {
        v->pos -= span;
    }

    return true;
}

// Mix `frames` output frames into sMixBuffer as interleaved stereo. Caller
// holds the lock.
static void iMixLocked(U32 frames)
{
    memset(sMixBuffer, 0, sizeof(float) * 2 * frames);

    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        hvoice* v = &sVoices[i];

        if (!v->acquired || !v->playing || v->paused)
        {
            continue;
        }

        // A voice with no data still has to advance, so that it finishes when
        // its sound would have. Skipping the mix but not the clock is the
        // difference between a silent sound and a stuck one.
        double step = ((double)v->rate / (double)sOutRate) * (double)v->pitch;
        if (step <= 0.0)
        {
            step = 0.0;
        }

        if (v->data == NULL)
        {
            v->pos += step * (double)frames;
            if (!iWrap(v))
            {
                v->playing = false;
            }
            continue;
        }

        float gL = v->curL;
        float gR = v->curR;
        float dL = (v->volL - gL) / (float)frames;
        float dR = (v->volR - gR) / (float)frames;

        U32 n = 0;
        for (; n < frames; n++)
        {
            if (!iWrap(v))
            {
                v->playing = false;
                break;
            }

            sMixBuffer[n * 2 + 0] += iSampleAt(v, v->pos, 0) * gL;
            sMixBuffer[n * 2 + 1] += iSampleAt(v, v->pos, 1) * gR;

            v->pos += step;
            gL += dL;
            gR += dR;
        }

        // Only claim to have reached the target gain if the whole block ran;
        // a voice that ended early keeps where its ramp got to, which matters
        // if the same slot is restarted before the next block.
        if (n == frames)
        {
            v->curL = v->volL;
            v->curR = v->volR;

            // The loop tests for the end BEFORE reading, which it has to. A
            // voice whose last frame is the block's last frame would therefore
            // stay "playing" until the next block noticed -- a whole endpoint
            // period of slack in every timing the game derives from a sound.
            // Settle it here instead.
            if (!iWrap(v))
            {
                v->playing = false;
            }
        }
        else
        {
            v->curL = gL;
            v->curR = gR;
        }
    }

    // The environmental reverb, over the finished mix. Here rather than in the
    // render thread so that it is inside the same lock the parameters are set
    // under, and so that the test hook exercises it too.
    //
    // Post-mix is also the only place it can go. It is a send off the whole
    // mix, and the voices above have already been panned and summed; per-voice
    // sends would need a wet level per emitter, which nothing in the game
    // computes and nothing in this seam carries.
    iSndReverbProcess(sMixBuffer, frames);
}

// Write the mixed block into the endpoint buffer, in whatever format the device
// asked for, spreading stereo across its first two channels.
static void iEmit(BYTE* dst, U32 frames)
{
    U32 outch = sOutChannels;

    for (U32 n = 0; n < frames; n++)
    {
        float l = sMixBuffer[n * 2 + 0];
        float r = sMixBuffer[n * 2 + 1];

        // A mono endpoint gets the sum rather than the left channel alone, or
        // everything panned right disappears.
        if (outch == 1)
        {
            l = (l + r) * 0.5f;
            r = l;
        }

        if (l > 1.0f) l = 1.0f;
        else if (l < -1.0f) l = -1.0f;
        if (r > 1.0f) r = 1.0f;
        else if (r < -1.0f) r = -1.0f;

        // Channels past the first two are left silent. Surround placement is
        // the game's business and the game does not have one: xSnd computes a
        // stereo pan and nothing else, so upmixing here would be inventing a
        // mix rather than reproducing it.
        if (sOutFloat)
        {
            float* p = (float*)dst;
            for (U32 c = 0; c < outch; c++)
            {
                p[n * outch + c] = (c == 0) ? l : (c == 1 ? r : 0.0f);
            }
        }
        else
        {
            S16* p = (S16*)dst;
            S16 sl = (S16)(l * 32767.0f);
            S16 sr = (S16)(r * 32767.0f);
            for (U32 c = 0; c < outch; c++)
            {
                p[n * outch + c] = (c == 0) ? sl : (c == 1 ? sr : 0);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// The render thread

static DWORD WINAPI iRenderThread(LPVOID)
{
    // MTA: nothing here pumps a message loop, and the interfaces are used from
    // this thread only.
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool com = SUCCEEDED(hr);

    // A short, steady deadline; the endpoint buffer is only ~30 ms.
    HANDLE mmcss = NULL;
    DWORD taskIndex = 0;
    HMODULE avrt = LoadLibraryA("avrt.dll");
    typedef HANDLE(WINAPI * PFN_SetMmThreadCharacteristicsA)(LPCSTR, LPDWORD);
    typedef BOOL(WINAPI * PFN_RevertMmThreadCharacteristics)(HANDLE);
    PFN_SetMmThreadCharacteristicsA setChars = NULL;
    PFN_RevertMmThreadCharacteristics revertChars = NULL;

    if (avrt != NULL)
    {
        setChars =
            (PFN_SetMmThreadCharacteristicsA)GetProcAddress(avrt, "AvSetMmThreadCharacteristicsA");
        revertChars =
            (PFN_RevertMmThreadCharacteristics)GetProcAddress(avrt,
                                                              "AvRevertMmThreadCharacteristics");
        if (setChars != NULL)
        {
            mmcss = setChars("Pro Audio", &taskIndex);
        }
    }

    HANDLE waits[2] = { sQuitEvent, sBufferEvent };

    for (;;)
    {
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, 200);

        if (w == WAIT_OBJECT_0)
        {
            break;
        }

        if (w == WAIT_FAILED)
        {
            break;
        }

        // A timeout means the endpoint stopped signalling -- a device change,
        // usually. Keep waiting; the game is unaffected either way, and voices
        // are retired by the fallback clock if the stream never comes back.
        if (w == WAIT_TIMEOUT)
        {
            continue;
        }

        UINT32 padding = 0;
        if (FAILED(sClient->GetCurrentPadding(&padding)))
        {
            continue;
        }

        UINT32 avail = sBufferFrames - padding;
        if (avail == 0)
        {
            continue;
        }

        if (avail > sMixBufferFrames)
        {
            avail = sMixBufferFrames;
        }

        BYTE* dst = NULL;
        if (FAILED(sRender->GetBuffer(avail, &dst)))
        {
            continue;
        }

        EnterCriticalSection(&sLock);
        iMixLocked(avail);
        LeaveCriticalSection(&sLock);

        iEmit(dst, avail);

        sRender->ReleaseBuffer(avail, 0);
    }

    if (mmcss != NULL && revertChars != NULL)
    {
        revertChars(mmcss);
    }

    if (avrt != NULL)
    {
        FreeLibrary(avrt);
    }

    if (com)
    {
        CoUninitialize();
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Device setup

static void iTeardownDevice()
{
    if (sThread != NULL)
    {
        SetEvent(sQuitEvent);
        WaitForSingleObject(sThread, 2000);
        CloseHandle(sThread);
        sThread = NULL;
    }

    if (sClient != NULL)
    {
        sClient->Stop();
    }

    if (sRender != NULL)
    {
        sRender->Release();
        sRender = NULL;
    }

    if (sClient != NULL)
    {
        sClient->Release();
        sClient = NULL;
    }

    if (sDevice != NULL)
    {
        sDevice->Release();
        sDevice = NULL;
    }

    if (sEnumerator != NULL)
    {
        sEnumerator->Release();
        sEnumerator = NULL;
    }

    if (sBufferEvent != NULL)
    {
        CloseHandle(sBufferEvent);
        sBufferEvent = NULL;
    }

    if (sQuitEvent != NULL)
    {
        CloseHandle(sQuitEvent);
        sQuitEvent = NULL;
    }

    free(sMixBuffer);
    sMixBuffer = NULL;
    sMixBufferFrames = 0;

    // After the render thread has joined, which is the only other user of it.
    // sReverbWanted deliberately survives: a device coming back should come
    // back with the effect the current scene asked for.
    iSndReverbExit();

    sDeviceUp = false;
}

// True if the mix format is one iEmit can write. WASAPI shared mode always
// hands back the format the mixer is already running in, so this is a check
// rather than a negotiation -- there is nothing to fall back to if it is
// something else, and saying so is better than emitting noise.
static bool iFormatUsable(const WAVEFORMATEX* wf)
{
    if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        return wf->wBitsPerSample == 32;
    }

    if (wf->wFormatTag == WAVE_FORMAT_PCM)
    {
        return wf->wBitsPerSample == 16;
    }

    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22)
    {
        const WAVEFORMATEXTENSIBLE* we = (const WAVEFORMATEXTENSIBLE*)wf;

        if (memcmp(&we->SubFormat, &kSUBTYPE_IEEE_FLOAT, sizeof(GUID)) == 0)
        {
            return wf->wBitsPerSample == 32;
        }

        if (memcmp(&we->SubFormat, &kSUBTYPE_PCM, sizeof(GUID)) == 0)
        {
            return wf->wBitsPerSample == 16;
        }
    }

    return false;
}

static bool iFormatIsFloat(const WAVEFORMATEX* wf)
{
    if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        return true;
    }

    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22)
    {
        const WAVEFORMATEXTENSIBLE* we = (const WAVEFORMATEXTENSIBLE*)wf;
        return memcmp(&we->SubFormat, &kSUBTYPE_IEEE_FLOAT, sizeof(GUID)) == 0;
    }

    return false;
}

// Everything here reports and returns false rather than failing hard: see the
// note at the top about a missing device being a configuration, not an error.
static bool iBringUpDevice()
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool com_here = SUCCEEDED(hr);

    // RPC_E_CHANGED_MODE means someone already initialised this thread as an
    // STA, which is fine -- the objects are created here and used on the render
    // thread, which does its own CoInitializeEx.
    if (!com_here && hr != RPC_E_CHANGED_MODE)
    {
        printf("iSndHost: CoInitializeEx failed (0x%08lx); no audio\n", (unsigned long)hr);
        return false;
    }

    hr = CoCreateInstance(kCLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, kIID_IMMDeviceEnumerator,
                          (void**)&sEnumerator);
    if (FAILED(hr))
    {
        printf("iSndHost: no device enumerator (0x%08lx); no audio\n", (unsigned long)hr);
        return false;
    }

    hr = sEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &sDevice);
    if (FAILED(hr))
    {
        printf("iSndHost: no default output device (0x%08lx); no audio\n", (unsigned long)hr);
        return false;
    }

    hr = sDevice->Activate(kIID_IAudioClient, CLSCTX_ALL, NULL, (void**)&sClient);
    if (FAILED(hr))
    {
        printf("iSndHost: could not activate the audio client (0x%08lx); no audio\n",
               (unsigned long)hr);
        return false;
    }

    WAVEFORMATEX* wf = NULL;
    hr = sClient->GetMixFormat(&wf);
    if (FAILED(hr) || wf == NULL)
    {
        printf("iSndHost: could not read the mix format (0x%08lx); no audio\n",
               (unsigned long)hr);
        return false;
    }

    if (!iFormatUsable(wf))
    {
        printf("iSndHost: the endpoint mix format is %d-bit tag %d, which this backend does not "
               "write; no audio\n",
               (int)wf->wBitsPerSample, (int)wf->wFormatTag);
        CoTaskMemFree(wf);
        return false;
    }

    sOutRate = wf->nSamplesPerSec;
    sOutChannels = wf->nChannels;
    sOutBits = wf->wBitsPerSample;
    sOutFloat = iFormatIsFloat(wf);

    // 30 ms, in 100-nanosecond units. Shared mode treats this as a request; the
    // buffer that comes back is whatever the engine period allows, which is
    // what sBufferFrames is read back for.
    REFERENCE_TIME duration = 300000;

    hr = sClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, duration,
                             0, wf, NULL);
    CoTaskMemFree(wf);

    if (FAILED(hr))
    {
        printf("iSndHost: could not initialise the audio client (0x%08lx); no audio\n",
               (unsigned long)hr);
        return false;
    }

    hr = sClient->GetBufferSize(&sBufferFrames);
    if (FAILED(hr) || sBufferFrames == 0)
    {
        printf("iSndHost: the endpoint reported no buffer (0x%08lx); no audio\n",
               (unsigned long)hr);
        return false;
    }

    hr = sClient->GetService(kIID_IAudioRenderClient, (void**)&sRender);
    if (FAILED(hr))
    {
        printf("iSndHost: no render client (0x%08lx); no audio\n", (unsigned long)hr);
        return false;
    }

    sBufferEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
    sQuitEvent = CreateEventA(NULL, TRUE, FALSE, NULL);

    if (sBufferEvent == NULL || sQuitEvent == NULL)
    {
        printf("iSndHost: could not create the render events; no audio\n");
        return false;
    }

    hr = sClient->SetEventHandle(sBufferEvent);
    if (FAILED(hr))
    {
        printf("iSndHost: could not attach the render event (0x%08lx); no audio\n",
               (unsigned long)hr);
        return false;
    }

    sMixBufferFrames = sBufferFrames;
    sMixBuffer = (float*)malloc(sizeof(float) * 2 * sMixBufferFrames);
    if (sMixBuffer == NULL)
    {
        printf("iSndHost: could not allocate the mix buffer; no audio\n");
        return false;
    }

    // The reverb's delay lengths are in samples, so it can only be built once
    // the endpoint has said what rate it runs at. If the game already asked for
    // an effect -- it sets one when a scene loads, which can be before a device
    // is available -- it is applied now rather than lost.
    iSndReverbInit(sOutRate);
    if (sReverbWanted)
    {
        iSndReverbSet(&sReverb);
    }

    hr = sClient->Start();
    if (FAILED(hr))
    {
        printf("iSndHost: the stream would not start (0x%08lx); no audio\n", (unsigned long)hr);
        return false;
    }

    sThread = CreateThread(NULL, 0, iRenderThread, NULL, 0, NULL);
    if (sThread == NULL)
    {
        printf("iSndHost: could not start the render thread; no audio\n");
        return false;
    }

    snprintf(sName, sizeof(sName), "WASAPI (%u Hz, %u ch, %s, %u-frame buffer)", sOutRate,
             sOutChannels, sOutFloat ? "float32" : "int16", sBufferFrames);

    return true;
}

// ---------------------------------------------------------------------------
// The seam

void iSndHostInit()
{
    iEnsureLock();

    EnterCriticalSection(&sLock);
    memset(sVoices, 0, sizeof(sVoices));
    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        sVoices[i].pitch = 1.0f;
    }
    LeaveCriticalSection(&sLock);

    if (sDeviceUp)
    {
        return;
    }

    // BFBB_AUDIO=0 forces the silent path, which is the quickest way to tell a
    // bug in the mixer apart from a bug in the game logic that waits on it.
    const char* off = getenv("BFBB_AUDIO");
    if (off != NULL && off[0] == '0')
    {
        snprintf(sName, sizeof(sName), "win32 (silenced by BFBB_AUDIO=0, but keeps time)");
        sOutRate = 48000;
        sLastSilentNs = iHostMonotonicNs();
        return;
    }

    sDeviceUp = iBringUpDevice();

    if (!sDeviceUp)
    {
        iTeardownDevice();
        snprintf(sName, sizeof(sName), "win32 (no device; silent, but keeps time)");

        // The fallback clock needs a rate to convert its elapsed time into
        // frames. Any value works as long as it matches what the voices are
        // stepped against; 48 kHz is what a device would most likely have been.
        sOutRate = 48000;
        sLastSilentNs = iHostMonotonicNs();
    }
}

void iSndHostExit()
{
    iTeardownDevice();

    if (sLockReady)
    {
        EnterCriticalSection(&sLock);
        memset(sVoices, 0, sizeof(sVoices));
        LeaveCriticalSection(&sLock);
    }
}

S32 iSndHostAcquire(U32 priority)
{
    iEnsureLock();

    EnterCriticalSection(&sLock);

    S32 got = -1;
    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        if (!sVoices[i].acquired)
        {
            memset(&sVoices[i], 0, sizeof(sVoices[i]));
            sVoices[i].acquired = true;
            sVoices[i].pitch = 1.0f;
            got = i;
            break;
        }
    }

    LeaveCriticalSection(&sLock);

    // Every voice busy. Retail's AXAcquireVoice evicts a lower-priority one
    // here; the mixer has exactly as many voices as the game's own table, so
    // running out means the game already decided all 64 were worth keeping, and
    // there is nothing better to evict than what it chose. Failing is a state
    // the game handles.
    return got;
}

void iSndHostRelease(S32 voice)
{
    iEnsureLock();

    EnterCriticalSection(&sLock);

    if (iVoiceValid(voice))
    {
        memset(&sVoices[voice], 0, sizeof(sVoices[voice]));
    }

    LeaveCriticalSection(&sLock);
}

void iSndHostStart(S32 voice, const iSndHostSample* sample)
{
    if (sample == NULL)
    {
        return;
    }

    iEnsureLock();

    EnterCriticalSection(&sLock);

    if (iVoiceValid(voice))
    {
        hvoice* v = &sVoices[voice];

        U32 channels = sample->channels != 0 ? sample->channels : 1;
        U32 bits = sample->bits != 0 ? sample->bits : 16;

        // What the caller says the sample is, against what it actually handed
        // over. A short block would be read past the end at the last frame, so
        // the byte count wins.
        U32 frame_bytes = channels * (bits / 8);
        U32 frames = sample->num_samples;

        if (sample->data != NULL && frame_bytes != 0)
        {
            U32 have = sample->bytes / frame_bytes;
            if (frames == 0 || frames > have)
            {
                frames = have;
            }
        }

        v->data = sample->data;
        v->frames = frames;
        v->channels = channels;
        v->bits = bits;
        v->rate = sample->sample_rate != 0 ? sample->sample_rate : 22050;
        v->looping = sample->looping;
        v->loop_start = sample->loop_start;
        v->loop_end = sample->loop_end;
        v->pos = 0.0;
        v->playing = frames > 0;
        v->paused = false;

        // Start at the target gain rather than ramping up from silence: the
        // first block of a sound is its attack, and fading it in blunts every
        // percussive effect in the game.
        v->curL = v->volL;
        v->curR = v->volR;
    }

    LeaveCriticalSection(&sLock);
}

void iSndHostStop(S32 voice)
{
    iEnsureLock();

    EnterCriticalSection(&sLock);

    if (iVoiceValid(voice))
    {
        sVoices[voice].playing = false;
        sVoices[voice].paused = false;

        // Drop the borrowed pointer here, not just the playing flag. The caller
        // is free to release the sample as soon as this returns, and the mixer
        // must have no way to reach it afterwards.
        sVoices[voice].data = NULL;
        sVoices[voice].frames = 0;
    }

    LeaveCriticalSection(&sLock);
}

void iSndHostPause(S32 voice, bool paused)
{
    iEnsureLock();

    EnterCriticalSection(&sLock);

    if (iVoiceValid(voice) && sVoices[voice].playing)
    {
        sVoices[voice].paused = paused;
    }

    LeaveCriticalSection(&sLock);
}

void iSndHostSetVol(S32 voice, F32 left, F32 right)
{
    if (left < 0.0f) left = 0.0f;
    else if (left > 1.0f) left = 1.0f;
    if (right < 0.0f) right = 0.0f;
    else if (right > 1.0f) right = 1.0f;

    iEnsureLock();

    EnterCriticalSection(&sLock);

    if (iVoiceValid(voice))
    {
        sVoices[voice].volL = left;
        sVoices[voice].volR = right;
    }

    LeaveCriticalSection(&sLock);
}

void iSndHostSetPitch(S32 voice, F32 pitch)
{
    if (pitch <= 0.0f)
    {
        return;
    }

    iEnsureLock();

    EnterCriticalSection(&sLock);

    if (iVoiceValid(voice))
    {
        sVoices[voice].pitch = pitch;
    }

    LeaveCriticalSection(&sLock);
}

bool iSndHostIsPlaying(S32 voice)
{
    iEnsureLock();

    EnterCriticalSection(&sLock);

    bool playing = iVoiceValid(voice) && sVoices[voice].playing;

    LeaveCriticalSection(&sLock);

    return playing;
}

void iSndHostUpdate()
{
    if (sDeviceUp)
    {
        // The render thread advanced every voice as it consumed it, and retires
        // them itself. Nothing to do.
        return;
    }

    // No device: advance the voices on the wall clock instead, so that
    // everything the game derives from a sound's length still happens on time.
    U64 now = iHostMonotonicNs();
    U64 elapsed = now - sLastSilentNs;
    sLastSilentNs = now;

    // A long stall -- a breakpoint, a scene load -- would otherwise skip a
    // sound entirely. Cap the step at a quarter second, which is what a device
    // would effectively have done by dropping the frames it could not fill.
    if (elapsed > 250000000ULL)
    {
        elapsed = 250000000ULL;
    }

    double frames = ((double)elapsed / 1000000000.0) * (double)sOutRate;

    iEnsureLock();

    EnterCriticalSection(&sLock);

    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        hvoice* v = &sVoices[i];

        if (!v->acquired || !v->playing || v->paused)
        {
            continue;
        }

        double step = ((double)v->rate / (double)sOutRate) * (double)v->pitch;
        v->pos += step * frames;

        if (!iWrap(v))
        {
            v->playing = false;
        }
    }

    LeaveCriticalSection(&sLock);
}

void iSndHostSetReverb(const iSndHostReverb* params)
{
    iEnsureLock();

    EnterCriticalSection(&sLock);

    if (params != NULL)
    {
        sReverb = *params;
        sReverbWanted = true;
        iSndReverbSet(&sReverb);
    }
    else
    {
        sReverbWanted = false;
        iSndReverbSet(NULL);
    }

    LeaveCriticalSection(&sLock);
}

// ---------------------------------------------------------------------------
// Test hook
//
// Deliberately not in iSndHost.h: nothing in the game may call this, and its
// only caller is src/SB/Core/pc/tests/selftest.cpp. It exists because the
// interesting half of this file -- the resampling, the gain ramp, the loop
// wrap -- is otherwise only reachable through a real audio endpoint, which a
// self-test must not require and could not make deterministic if it did.
//
// Mixes one block at `rate` into `out` as interleaved stereo, exactly as the
// render thread would, and advances the voices by it.
void iSndHostWin32TestMix(U32 rate, float* out, U32 frames)
{
    if (out == NULL || frames == 0 || rate == 0)
    {
        return;
    }

    iEnsureLock();

    if (sMixBuffer == NULL || frames > sMixBufferFrames)
    {
        free(sMixBuffer);
        sMixBuffer = (float*)malloc(sizeof(float) * 2 * frames);
        sMixBufferFrames = sMixBuffer != NULL ? frames : 0;
    }

    if (sMixBuffer == NULL)
    {
        return;
    }

    U32 saved = sOutRate;
    sOutRate = rate;

    EnterCriticalSection(&sLock);
    iMixLocked(frames);
    LeaveCriticalSection(&sLock);

    sOutRate = saved;

    memcpy(out, sMixBuffer, sizeof(float) * 2 * frames);
}

const char* iSndHostName()
{
    return sName[0] != '\0' ? sName : "win32 (not initialised)";
}
