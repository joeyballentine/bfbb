#include "iSoundtrack.h"

#include "iHost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef BFBB_HAVE_FFMPEG
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

// FFmpeg replaced the channel count on AVCodecContext with a full channel
// layout in 7.x (libavutil 57), the same split iFMVDecoderFFmpeg.cpp spans.
#if LIBAVUTIL_VERSION_MAJOR >= 57
#define BFBB_NUM_CHANNELS(ctx) ((ctx)->ch_layout.nb_channels)
#else
#define BFBB_NUM_CHANNELS(ctx) ((ctx)->channels)
#endif
#endif

#define ISOUNDTRACK_MAX_ENTRIES 64
#define ISOUNDTRACK_MAX_PATH 1024

// A decoded track is held whole, so this is the ceiling on one file. Ten
// minutes of 48 kHz stereo is 115 MB; the cap is above that and well below the
// point where a mistyped path costs the process its address space.
#define ISOUNDTRACK_MAX_BYTES (256u * 1024u * 1024u)

struct st_entry
{
    U32 aid;
    char path[ISOUNDTRACK_MAX_PATH];
};

static st_entry sEntries[ISOUNDTRACK_MAX_ENTRIES];
static U32 sEntryCount;
static char sFolder[ISOUNDTRACK_MAX_PATH];
static bool sScanned;

// xStrHash, reproduced rather than included: this file is below the game's
// sources and must not reach up into xString.h for one function. iTextPatch.cpp
// carries the same copy for the same reason, and pc_selftest pins both against
// ids read out of the retail archives.
static U32 iAssetID(const char* name)
{
    U32 hash = 0;

    while (*name != '\0')
    {
        U32 c = (U8)*name;
        hash = ((c - (c & (S32)c >> 1 & 0x20)) & 0xff) + hash * 0x83;
        name++;
    }

    return hash;
}

static void iAddEntry(U32 aid, const char* path)
{
    if (aid == 0 || sEntryCount >= ISOUNDTRACK_MAX_ENTRIES)
    {
        return;
    }

    // A mapping file entry is read first and wins, so an id already claimed is
    // left alone rather than overwritten by a filename that happens to hash.
    for (U32 i = 0; i < sEntryCount; i++)
    {
        if (sEntries[i].aid == aid)
        {
            return;
        }
    }

    st_entry* e = &sEntries[sEntryCount++];
    e->aid = aid;
    snprintf(e->path, sizeof(e->path), "%s", path);
}

// Strip the directory and the extension: "Disc 2/01. Foo.flac" -> "01. Foo".
// What is left is what gets hashed, so a folder named after the assets works
// with no mapping file.
static void iStem(const char* name, char* out, size_t outsize)
{
    const char* base = name;

    for (const char* p = name; *p != '\0'; p++)
    {
        if (*p == '/' || *p == '\\')
        {
            base = p + 1;
        }
    }

    const char* dot = NULL;
    for (const char* p = base; *p != '\0'; p++)
    {
        if (*p == '.')
        {
            dot = p;
        }
    }

    size_t n = (dot != NULL) ? (size_t)(dot - base) : strlen(base);
    if (n >= outsize)
    {
        n = outsize - 1;
    }

    memcpy(out, base, n);
    out[n] = '\0';
}

static void iTrim(char* s)
{
    char* p = s;
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }

    if (p != s)
    {
        memmove(s, p, strlen(p) + 1);
    }

    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
    {
        s[--n] = '\0';
    }
}

// `soundtrack.txt`, if there is one: "asset name = file". Comments start with
// ';' or '#'. A line naming a file that is not there is reported rather than
// dropped -- a mapping is written by hand, and a typo in one is the whole
// reason the track someone expected did not play.
static void iReadMapping()
{
    char path[ISOUNDTRACK_MAX_PATH];
    snprintf(path, sizeof(path), "%s/soundtrack.txt", sFolder);

    FILE* f = fopen(path, "r");
    if (f == NULL)
    {
        return;
    }

    char line[ISOUNDTRACK_MAX_PATH];
    S32 missing = 0;

    while (fgets(line, sizeof(line), f) != NULL)
    {
        char* comment = strpbrk(line, ";#");
        if (comment != NULL)
        {
            *comment = '\0';
        }

        char* eq = strchr(line, '=');
        if (eq == NULL)
        {
            continue;
        }

        *eq = '\0';
        char* name = line;
        char* file = eq + 1;

        iTrim(name);
        iTrim(file);

        if (name[0] == '\0' || file[0] == '\0')
        {
            continue;
        }

        char full[ISOUNDTRACK_MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", sFolder, file);

        if (!iHostPathExists(full))
        {
            printf("bfbb: soundtrack: %s names '%s', which is not in the folder\n", path, file);
            missing++;
            continue;
        }

        iAddEntry(iAssetID(name), full);
    }

    fclose(f);

    if (missing > 0)
    {
        fflush(stdout);
    }
}

// What the by-name scan will pick up. A soundtrack folder is rarely only audio
// -- cover art, the mapping file, an editor's analysis sidecars -- and every
// one of those would otherwise take an entry and a hash of its own. The
// mapping file is not filtered by this: a name in there is taken at its word,
// so an extension this list has never heard of still works if it is said out
// loud.
static bool iLooksLikeAudio(const char* name)
{
    static const char* const kExts[] = { ".wav",  ".flac", ".ogg", ".oga", ".mp3", ".m4a",
                                         ".aac",  ".opus", ".wma", ".aif", ".aiff", ".w64" };

    const char* dot = NULL;
    for (const char* p = name; *p != '\0'; p++)
    {
        if (*p == '.')
        {
            dot = p;
        }
    }

    if (dot == NULL)
    {
        return false;
    }

    for (U32 i = 0; i < sizeof(kExts) / sizeof(kExts[0]); i++)
    {
        if (iHostStrCaseCmp(dot, kExts[i]) == 0)
        {
            return true;
        }
    }

    return false;
}

// Everything else in the folder, and one level down -- a rip that arrives as
// "Disc 1" and "Disc 2" should not have to be flattened by hand before it can
// be pointed at.
static void iScanDir(const char* dir, const char* prefix, S32 depth)
{
    iHostDir* d = iHostDirOpen(dir);
    if (d == NULL)
    {
        return;
    }

    const char* name;
    while ((name = iHostDirNext(d)) != NULL)
    {
        if (name[0] == '.')
        {
            continue;
        }

        char full[ISOUNDTRACK_MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", dir, name);

        iHostFileInfo info;
        if (iHostStat(full, &info) && info.is_dir)
        {
            if (depth > 0)
            {
                char sub[ISOUNDTRACK_MAX_PATH];
                snprintf(sub, sizeof(sub), "%s%s/", prefix, name);
                iScanDir(full, sub, depth - 1);
            }
            continue;
        }

        if (!iLooksLikeAudio(name))
        {
            continue;
        }

        char stem[256];
        iStem(name, stem, sizeof(stem));
        iAddEntry(iAssetID(stem), full);
    }

    iHostDirClose(d);
}

static void iScan()
{
    if (sScanned)
    {
        return;
    }

    sScanned = true;

    if (sFolder[0] == '\0')
    {
        return;
    }

    if (!iHostPathExists(sFolder))
    {
        printf("bfbb: soundtrack: '%s' does not exist; the game's own music is used\n", sFolder);
        fflush(stdout);
        return;
    }

    iReadMapping();
    iScanDir(sFolder, "", 1);

    printf("bfbb: soundtrack: %u override%s from %s\n", sEntryCount, sEntryCount == 1 ? "" : "s",
           sFolder);
    fflush(stdout);
}

void iSoundtrackSetFolder(const char* path)
{
    snprintf(sFolder, sizeof(sFolder), "%s", path != NULL ? path : "");
    iSoundtrackReset();
}

const char* iSoundtrackFolder()
{
    return sFolder[0] != '\0' ? sFolder : NULL;
}

const char* iSoundtrackFind(U32 assetID)
{
    if (sFolder[0] == '\0' || assetID == 0)
    {
        return NULL;
    }

    iScan();

    for (U32 i = 0; i < sEntryCount; i++)
    {
        if (sEntries[i].aid == assetID)
        {
            return sEntries[i].path;
        }
    }

    return NULL;
}

U32 iSoundtrackCount()
{
    iScan();
    return sEntryCount;
}

void iSoundtrackReset()
{
    memset(sEntries, 0, sizeof(sEntries));
    sEntryCount = 0;
    sScanned = false;
}

// ---------------------------------------------------------------------------
// Decoding
//
// RIFF/WAVE is read here whether or not FFmpeg is present, because a build
// without it should still be able to play something, and an uncompressed file
// is the one format that needs no library to read. Everything else goes
// through FFmpeg, which the port already links for the movies.

static S16 iClamp16(F32 v)
{
    if (v > 32767.0f)
    {
        return 32767;
    }

    if (v < -32768.0f)
    {
        return -32768;
    }

    return (S16)v;
}

static U32 iRd32(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
}

static U32 iRd16(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8);
}

// A WAVE file's fmt and data chunks, converted to interleaved S16. Handles the
// four widths a wave file is ever written at -- 8-bit unsigned, 16-bit signed,
// 24-bit signed and 32-bit float -- and refuses anything compressed, which is
// what FFmpeg is for.
static void* iDecodeWav(const U8* raw, U32 size, U32* channels, U32* rate, U32* bytes)
{
    if (size < 44 || memcmp(raw, "RIFF", 4) != 0 || memcmp(raw + 8, "WAVE", 4) != 0)
    {
        return NULL;
    }

    U32 fmtTag = 0;
    U32 ch = 0;
    U32 hz = 0;
    U32 bits = 0;
    const U8* data = NULL;
    U32 dataSize = 0;

    U32 off = 12;
    while (off + 8 <= size)
    {
        U32 id0 = off;
        U32 len = iRd32(raw + off + 4);
        U32 body = off + 8;

        if (body > size)
        {
            break;
        }

        if (len > size - body)
        {
            len = size - body;
        }

        if (memcmp(raw + id0, "fmt ", 4) == 0 && len >= 16)
        {
            fmtTag = iRd16(raw + body);
            ch = iRd16(raw + body + 2);
            hz = iRd32(raw + body + 4);
            bits = iRd16(raw + body + 14);

            // WAVE_FORMAT_EXTENSIBLE carries the real tag in its subformat's
            // first two bytes; a float file written by a DAW is usually this.
            if (fmtTag == 0xFFFE && len >= 40)
            {
                fmtTag = iRd16(raw + body + 24);
            }
        }
        else if (memcmp(raw + id0, "data", 4) == 0)
        {
            data = raw + body;
            dataSize = len;
        }

        off = body + len + (len & 1);
    }

    if (data == NULL || ch == 0 || ch > 8 || hz == 0 || dataSize == 0)
    {
        return NULL;
    }

    if (fmtTag != 1 && fmtTag != 3)
    {
        return NULL;
    }

    U32 srcFrameBytes = ch * (bits / 8);
    if (srcFrameBytes == 0)
    {
        return NULL;
    }

    U32 frames = dataSize / srcFrameBytes;
    U32 outCh = ch > 2 ? 2 : ch;

    if (frames == 0 || (U64)frames * outCh * sizeof(S16) > ISOUNDTRACK_MAX_BYTES)
    {
        return NULL;
    }

    S16* out = (S16*)malloc((size_t)frames * outCh * sizeof(S16));
    if (out == NULL)
    {
        return NULL;
    }

    for (U32 i = 0; i < frames; i++)
    {
        for (U32 c = 0; c < outCh; c++)
        {
            const U8* s = data + (size_t)i * srcFrameBytes + (size_t)c * (bits / 8);
            S16 v = 0;

            if (fmtTag == 3 && bits == 32)
            {
                F32 f;
                memcpy(&f, s, sizeof(f));
                v = iClamp16(f * 32767.0f);
            }
            else if (bits == 8)
            {
                v = (S16)(((S32)s[0] - 128) << 8);
            }
            else if (bits == 16)
            {
                v = (S16)(U16)iRd16(s);
            }
            else if (bits == 24)
            {
                S32 t = (S32)((U32)s[0] << 8 | (U32)s[1] << 16 | (U32)s[2] << 24);
                v = (S16)(t >> 16);
            }
            else
            {
                free(out);
                return NULL;
            }

            out[(size_t)i * outCh + c] = v;
        }
    }

    *channels = outCh;
    *rate = hz;
    *bytes = frames * outCh * (U32)sizeof(S16);
    return out;
}

static void* iReadWholeFile(const char* path, U32* size)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL)
    {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (n <= 0 || (U64)n > ISOUNDTRACK_MAX_BYTES)
    {
        fclose(f);
        return NULL;
    }

    void* mem = malloc((size_t)n);
    if (mem == NULL)
    {
        fclose(f);
        return NULL;
    }

    size_t got = fread(mem, 1, (size_t)n, f);
    fclose(f);

    if (got != (size_t)n)
    {
        free(mem);
        return NULL;
    }

    *size = (U32)n;
    return mem;
}

#ifdef BFBB_HAVE_FFMPEG

// Everything that is not a plain WAVE. The whole file is decoded here and now:
// the mixer plays from memory, so there is no streaming path to hand this to,
// and a track has to be complete before its voice starts.
static void* iDecodeFFmpeg(const char* path, U32* channels, U32* rate, U32* bytes)
{
    AVFormatContext* fmt = NULL;

    if (avformat_open_input(&fmt, path, NULL, NULL) < 0)
    {
        return NULL;
    }

    if (avformat_find_stream_info(fmt, NULL) < 0)
    {
        avformat_close_input(&fmt);
        return NULL;
    }

    int stream = -1;
    for (unsigned i = 0; i < fmt->nb_streams; i++)
    {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            stream = (int)i;
            break;
        }
    }

    if (stream < 0)
    {
        avformat_close_input(&fmt);
        return NULL;
    }

    const AVCodec* codec = avcodec_find_decoder(fmt->streams[stream]->codecpar->codec_id);
    if (codec == NULL)
    {
        avformat_close_input(&fmt);
        return NULL;
    }

    AVCodecContext* dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, fmt->streams[stream]->codecpar);

    if (avcodec_open2(dec, codec, NULL) < 0)
    {
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return NULL;
    }

    U32 inCh = (U32)BFBB_NUM_CHANNELS(dec);
    U32 outCh = (inCh == 0) ? 2 : (inCh > 2 ? 2 : inCh);
    U32 hz = (U32)dec->sample_rate;

    // To interleaved 16-bit at the file's own rate. The rate is left alone
    // because the mixer resamples anyway and doing it twice only loses; the
    // FORMAT is not, because most codecs decode planar and nothing downstream
    // reads planar.
    SwrContext* swr = NULL;
#if LIBAVUTIL_VERSION_MAJOR >= 57
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, (int)outCh);
    if (swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_S16, (int)hz, &dec->ch_layout,
                            dec->sample_fmt, dec->sample_rate, 0, NULL) < 0)
    {
        swr = NULL;
    }
    av_channel_layout_uninit(&out_layout);
#else
    int64_t in_layout = dec->channel_layout ? (int64_t)dec->channel_layout
                                            : av_get_default_channel_layout(dec->channels);
    swr = swr_alloc_set_opts(NULL, av_get_default_channel_layout((int)outCh), AV_SAMPLE_FMT_S16,
                             (int)hz, in_layout, dec->sample_fmt, dec->sample_rate, 0, NULL);
#endif

    if (swr == NULL || swr_init(swr) < 0)
    {
        if (swr != NULL)
        {
            swr_free(&swr);
        }
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return NULL;
    }

    // Grown as it fills rather than sized from the container's duration, which
    // is a hint and is wrong for a stream that was cut without rewriting it.
    U32 cap = hz * outCh * 2 * 60; // a minute, in samples
    S16* out = (S16*)malloc((size_t)cap * sizeof(S16));
    U32 written = 0;
    bool ok = (out != NULL);

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    while (ok && av_read_frame(fmt, pkt) >= 0)
    {
        if (pkt->stream_index == stream && avcodec_send_packet(dec, pkt) == 0)
        {
            while (avcodec_receive_frame(dec, frame) == 0)
            {
                int room = (int)av_rescale_rnd(swr_get_delay(swr, dec->sample_rate) +
                                                   frame->nb_samples,
                                               hz, dec->sample_rate, AV_ROUND_UP);

                if ((U64)(written + (U32)room * outCh) * sizeof(S16) > ISOUNDTRACK_MAX_BYTES)
                {
                    ok = false;
                    break;
                }

                if (written + (U32)room * outCh > cap)
                {
                    U32 want = cap * 2;
                    while (want < written + (U32)room * outCh)
                    {
                        want *= 2;
                    }

                    S16* bigger = (S16*)realloc(out, (size_t)want * sizeof(S16));
                    if (bigger == NULL)
                    {
                        ok = false;
                        break;
                    }

                    out = bigger;
                    cap = want;
                }

                U8* dst = (U8*)(out + written);
                int got = swr_convert(swr, &dst, room, (const U8**)frame->extended_data,
                                      frame->nb_samples);
                if (got > 0)
                {
                    written += (U32)got * outCh;
                }
            }
        }

        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);

    if (!ok || written == 0)
    {
        free(out);
        return NULL;
    }

    *channels = outCh;
    *rate = hz;
    *bytes = written * (U32)sizeof(S16);
    return out;
}

#endif

void* iSoundtrackDecode(const char* path, U32* channels, U32* rate, U32* bytes)
{
    if (path == NULL || channels == NULL || rate == NULL || bytes == NULL)
    {
        return NULL;
    }

    *channels = 0;
    *rate = 0;
    *bytes = 0;

    U32 size = 0;
    void* raw = iReadWholeFile(path, &size);

    if (raw != NULL)
    {
        void* pcm = iDecodeWav((const U8*)raw, size, channels, rate, bytes);
        free(raw);

        if (pcm != NULL)
        {
            return pcm;
        }
    }

#ifdef BFBB_HAVE_FFMPEG
    return iDecodeFFmpeg(path, channels, rate, bytes);
#else
    printf("bfbb: soundtrack: '%s' is not a WAVE file, and this build has no FFmpeg to decode "
           "anything else\n",
           path);
    fflush(stdout);
    return NULL;
#endif
}

const char* iSoundtrackDecoderName()
{
#ifdef BFBB_HAVE_FFMPEG
    return "wave + ffmpeg";
#else
    return "wave only";
#endif
}
