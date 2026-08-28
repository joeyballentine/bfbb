// The FFmpeg backend for iFMVDecoder.h.
//
// The port's movies are the Xbox release's .xmv: an XMV container carrying WMV2
// video with IMA ADPCM audio, and two that do not follow the rule -- Intro.xmv
// is raw PCM and RWLogo.xmv has no audio track at all. FFmpeg demuxes XMV and
// decodes all of that already, which is the whole reason this backend exists:
// the alternative is a WMV2 decoder, and WMV2 is a DCT codec in the H.263
// family that nobody should write for eight videos.
//
// Linked only when CMake finds FFmpeg. Without it iFMVDecoderNull.cpp is built
// instead and movies are skipped, which is what the port did before any of this.

#include "iFMVDecoder.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

// No <deque>/<vector>: compat/ shadows several standard headers for the
// CodeWarrior extensions the game needs, and pulling the MSVC STL in behind
// that does not compile. Two hand-rolled buffers are less code than the
// workaround would be, and this file only needs a queue and a byte bucket.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FFmpeg replaced the channel count on AVCodecContext with a full channel
// layout in 7.x (libavutil 57). Both spellings are still in the wild, so the
// two places that care ask through these rather than picking one.
#if LIBAVUTIL_VERSION_MAJOR >= 57
#define BFBB_NUM_CHANNELS(ctx) ((ctx)->ch_layout.nb_channels)
#else
#define BFBB_NUM_CHANNELS(ctx) ((ctx)->channels)
#endif

struct iFMVDecoder
{
    AVFormatContext* fmt;

    AVCodecContext* vdec;
    AVCodecContext* adec;
    int vstream;
    int astream;

    SwsContext* sws;
    U8* rgb;
    int rgb_pitch;

    SwrContext* swr;

    AVPacket* pkt;
    AVFrame* frame;

    // Both queues are needed because one demux pass yields whichever stream the
    // container happened to interleave next: pulling a video frame decodes
    // audio on the way past, and pulling audio decodes video. Neither may be
    // thrown away just because the caller asked for the other one.
    //
    // Ring of decoded video frames. Sixteen is far more than the one or two
    // that are ever outstanding; it exists so that decoding audio cannot
    // overflow it, since one demux pass yields whichever stream the container
    // interleaved next.
    AVFrame* vq[16];
    int vq_head;
    int vq_count;

    // Decoded audio waiting to be read, and how much of it already has been.
    S16* aq;
    size_t aq_cap;
    size_t aq_len;
    size_t aq_read;

    bool eof;

    F32 time_base;
    U32 channels;
};

#define FMV_VQ_SIZE 16

static void pushVideo(iFMVDecoder* d, AVFrame* f)
{
    if (f == NULL)
    {
        return;
    }
    if (d->vq_count == FMV_VQ_SIZE)
    {
        // Full. Drop the OLDEST rather than refusing the newest: a frame this
        // far behind is one the caller has already run past, and keeping it
        // would stall the movie instead of skipping a frame of it.
        AVFrame* old = d->vq[d->vq_head];
        av_frame_free(&old);
        d->vq_head = (d->vq_head + 1) % FMV_VQ_SIZE;
        d->vq_count--;
    }
    d->vq[(d->vq_head + d->vq_count) % FMV_VQ_SIZE] = f;
    d->vq_count++;
}

static AVFrame* popVideo(iFMVDecoder* d)
{
    if (d->vq_count == 0)
    {
        return NULL;
    }
    AVFrame* f = d->vq[d->vq_head];
    d->vq_head = (d->vq_head + 1) % FMV_VQ_SIZE;
    d->vq_count--;
    return f;
}

// Room for `extra` more samples, returning where to write them. NULL if the
// buffer cannot grow, which the caller treats as 'decoded nothing'.
static S16* reserveAudio(iFMVDecoder* d, size_t extra)
{
    if (d->aq_len + extra > d->aq_cap)
    {
        size_t cap = (d->aq_cap != 0) ? d->aq_cap : 65536;
        while (cap < d->aq_len + extra)
        {
            cap *= 2;
        }
        S16* grown = (S16*)realloc(d->aq, cap * sizeof(S16));
        if (grown == NULL)
        {
            return NULL;
        }
        d->aq = grown;
        d->aq_cap = cap;
    }
    return d->aq + d->aq_len;
}

// Read one packet and decode it into whichever queue it belongs to.
//
// FALSE once the file is exhausted AND both decoders have been flushed. The
// flush matters: a decoder holds frames back for reordering, and a movie whose
// last frames are only released by a null packet would otherwise end early.
static bool pump(iFMVDecoder* d)
{
    if (d->eof)
    {
        return false;
    }

    int rc = av_read_frame(d->fmt, d->pkt);
    if (rc < 0)
    {
        // Flush both decoders, then stop.
        if (d->vdec)
        {
            avcodec_send_packet(d->vdec, NULL);
            while (avcodec_receive_frame(d->vdec, d->frame) == 0)
            {
                pushVideo(d, av_frame_clone(d->frame));
                av_frame_unref(d->frame);
            }
        }
        d->eof = true;
        return false;
    }

    AVCodecContext* dec = NULL;
    if (d->pkt->stream_index == d->vstream)
    {
        dec = d->vdec;
    }
    else if (d->pkt->stream_index == d->astream)
    {
        dec = d->adec;
    }

    if (dec == NULL)
    {
        av_packet_unref(d->pkt);
        return true;
    }

    if (avcodec_send_packet(dec, d->pkt) == 0)
    {
        while (avcodec_receive_frame(dec, d->frame) == 0)
        {
            if (dec == d->vdec)
            {
                pushVideo(d, av_frame_clone(d->frame));
            }
            else if (d->swr)
            {
                // Worst case out of the resampler, which can be more frames
                // than went in when the rates differ.
                int cap = (int)av_rescale_rnd(swr_get_delay(d->swr, d->adec->sample_rate) +
                                                  d->frame->nb_samples,
                                              d->adec->sample_rate, d->adec->sample_rate,
                                              AV_ROUND_UP);
                S16* dst = reserveAudio(d, (size_t)cap * d->channels);
                if (dst != NULL)
                {
                    U8* out = (U8*)dst;
                    int got = swr_convert(d->swr, &out, cap,
                                          (const U8**)d->frame->extended_data,
                                          d->frame->nb_samples);
                    if (got > 0)
                    {
                        d->aq_len += (size_t)got * d->channels;
                    }
                }
            }
            av_frame_unref(d->frame);
        }
    }

    av_packet_unref(d->pkt);
    return true;
}

iFMVDecoder* iFMVDecoderOpen(const char* path, iFMVDecoderInfo* info)
{
    if (path == NULL || info == NULL)
    {
        return NULL;
    }

    iFMVDecoder* d = new iFMVDecoder();
    memset(info, 0, sizeof(*info));
    d->vstream = -1;
    d->astream = -1;

    if (avformat_open_input(&d->fmt, path, NULL, NULL) < 0)
    {
        delete d;
        return NULL;
    }
    if (avformat_find_stream_info(d->fmt, NULL) < 0)
    {
        avformat_close_input(&d->fmt);
        delete d;
        return NULL;
    }

    for (unsigned i = 0; i < d->fmt->nb_streams; i++)
    {
        AVMediaType t = d->fmt->streams[i]->codecpar->codec_type;
        if (t == AVMEDIA_TYPE_VIDEO && d->vstream < 0)
        {
            d->vstream = (int)i;
        }
        else if (t == AVMEDIA_TYPE_AUDIO && d->astream < 0)
        {
            d->astream = (int)i;
        }
    }

    if (d->vstream < 0)
    {
        avformat_close_input(&d->fmt);
        delete d;
        return NULL;
    }

    // --- video ---------------------------------------------------------------
    AVStream* vs = d->fmt->streams[d->vstream];
    const AVCodec* vc = avcodec_find_decoder(vs->codecpar->codec_id);
    if (vc == NULL)
    {
        avformat_close_input(&d->fmt);
        delete d;
        return NULL;
    }
    d->vdec = avcodec_alloc_context3(vc);
    avcodec_parameters_to_context(d->vdec, vs->codecpar);
    if (avcodec_open2(d->vdec, vc, NULL) < 0)
    {
        avcodec_free_context(&d->vdec);
        avformat_close_input(&d->fmt);
        delete d;
        return NULL;
    }

    info->width = (U32)d->vdec->width;
    info->height = (U32)d->vdec->height;

    // The frame rate, but only if it is one.
    //
    // RWLogo.xmv declares 1000/1, which is not a frame rate; a movie paced on
    // that runs a thousand times too fast and is gone before it is seen. So a
    // rate outside what a video can plausibly be is replaced rather than
    // trusted, and the fallback is the rate most of these movies use.
    AVRational fr = av_guess_frame_rate(d->fmt, vs, NULL);
    F32 fps = (fr.num > 0 && fr.den > 0) ? (F32)fr.num / (F32)fr.den : 0.0f;
    if (!(fps > 1.0f && fps <= 120.0f))
    {
        fps = 30000.0f / 1001.0f;
    }
    info->frame_time = 1.0f / fps;
    d->time_base = (F32)av_q2d(vs->time_base);

    d->sws = sws_getContext(d->vdec->width, d->vdec->height, d->vdec->pix_fmt, d->vdec->width,
                            d->vdec->height, AV_PIX_FMT_BGRA, SWS_BILINEAR, NULL, NULL, NULL);
    d->rgb_pitch = d->vdec->width * 4;
    d->rgb = (U8*)av_malloc((size_t)d->rgb_pitch * d->vdec->height);
    if (d->sws == NULL || d->rgb == NULL)
    {
        iFMVDecoderClose(d);
        return NULL;
    }

    // --- audio, when there is any --------------------------------------------
    //
    // RWLogo.xmv has no audio stream. That is not a failure and not worth a
    // silent device: sample_rate stays 0 and the caller opens nothing.
    if (d->astream >= 0)
    {
        AVStream* as = d->fmt->streams[d->astream];
        const AVCodec* ac = avcodec_find_decoder(as->codecpar->codec_id);
        if (ac != NULL)
        {
            d->adec = avcodec_alloc_context3(ac);
            avcodec_parameters_to_context(d->adec, as->codecpar);
            if (avcodec_open2(d->adec, ac, NULL) == 0)
            {
                d->channels = (U32)BFBB_NUM_CHANNELS(d->adec);
                if (d->channels == 0)
                {
                    d->channels = 2;
                }

                // Everything is converted to interleaved 16-bit at the source
                // rate. The rate is left alone because these files are 44100
                // and 48000 and the device can take either; the FORMAT is not,
                // because ADPCM decodes to planar and no audio API wants that.
#if LIBAVUTIL_VERSION_MAJOR >= 57
                AVChannelLayout out_ch;
                av_channel_layout_default(&out_ch, (int)d->channels);
                if (swr_alloc_set_opts2(&d->swr, &out_ch, AV_SAMPLE_FMT_S16,
                                        d->adec->sample_rate, &d->adec->ch_layout,
                                        d->adec->sample_fmt, d->adec->sample_rate, 0,
                                        NULL) < 0)
                {
                    d->swr = NULL;
                }
                av_channel_layout_uninit(&out_ch);
#else
                int64_t in_layout = d->adec->channel_layout
                                        ? (int64_t)d->adec->channel_layout
                                        : av_get_default_channel_layout(d->adec->channels);
                d->swr = swr_alloc_set_opts(NULL, in_layout, AV_SAMPLE_FMT_S16,
                                            d->adec->sample_rate, in_layout,
                                            d->adec->sample_fmt, d->adec->sample_rate, 0, NULL);
#endif
                if (d->swr != NULL && swr_init(d->swr) == 0)
                {
                    info->sample_rate = (U32)d->adec->sample_rate;
                    info->channels = d->channels;
                }
                else
                {
                    if (d->swr)
                    {
                        swr_free(&d->swr);
                    }
                    avcodec_free_context(&d->adec);
                    d->astream = -1;
                }
            }
            else
            {
                avcodec_free_context(&d->adec);
                d->astream = -1;
            }
        }
    }

    d->pkt = av_packet_alloc();
    d->frame = av_frame_alloc();
    if (d->pkt == NULL || d->frame == NULL)
    {
        iFMVDecoderClose(d);
        return NULL;
    }

    return d;
}

void iFMVDecoderClose(iFMVDecoder* d)
{
    if (d == NULL)
    {
        return;
    }

    for (int i = 0; i < d->vq_count; i++)
    {
        AVFrame* f = d->vq[(d->vq_head + i) % FMV_VQ_SIZE];
        av_frame_free(&f);
    }
    d->vq_count = 0;

    if (d->aq != NULL)
    {
        free(d->aq);
        d->aq = NULL;
    }

    if (d->frame)
    {
        av_frame_free(&d->frame);
    }
    if (d->pkt)
    {
        av_packet_free(&d->pkt);
    }
    if (d->swr)
    {
        swr_free(&d->swr);
    }
    if (d->sws)
    {
        sws_freeContext(d->sws);
    }
    if (d->rgb)
    {
        av_free(d->rgb);
    }
    if (d->adec)
    {
        avcodec_free_context(&d->adec);
    }
    if (d->vdec)
    {
        avcodec_free_context(&d->vdec);
    }
    if (d->fmt)
    {
        avformat_close_input(&d->fmt);
    }

    delete d;
}

S32 iFMVDecoderNextFrame(iFMVDecoder* d, const void** pixels, U32* pitch, F32* pts)
{
    if (d == NULL)
    {
        return FALSE;
    }

    while (d->vq_count == 0)
    {
        if (!pump(d))
        {
            break;
        }
    }

    AVFrame* f = popVideo(d);
    if (f == NULL)
    {
        return FALSE;
    }

    U8* dst[4] = { d->rgb, NULL, NULL, NULL };
    int dstride[4] = { d->rgb_pitch, 0, 0, 0 };
    sws_scale(d->sws, f->data, f->linesize, 0, d->vdec->height, dst, dstride);

    if (pts != NULL)
    {
        int64_t t = (f->best_effort_timestamp != AV_NOPTS_VALUE) ? f->best_effort_timestamp
                                                                 : f->pts;
        *pts = (t == AV_NOPTS_VALUE) ? -1.0f : (F32)t * d->time_base;
    }

    av_frame_free(&f);

    if (pixels != NULL)
    {
        *pixels = d->rgb;
    }
    if (pitch != NULL)
    {
        *pitch = (U32)d->rgb_pitch;
    }
    return TRUE;
}

U32 iFMVDecoderReadAudio(iFMVDecoder* d, S16* out, U32 frames)
{
    if (d == NULL || out == NULL || d->swr == NULL || frames == 0)
    {
        return 0;
    }

    U32 want = frames * d->channels;

    while (d->aq_len - d->aq_read < want)
    {
        if (!pump(d))
        {
            break;
        }
    }

    size_t have = d->aq_len - d->aq_read;
    U32 give = (U32)((have < want) ? have : want);
    if (give > 0)
    {
        memcpy(out, d->aq + d->aq_read, give * sizeof(S16));
        d->aq_read += give;
    }

    // Reclaim once the consumed part is worth the move, rather than every call:
    // this buffer is drained in device-sized chunks and erasing from the front
    // each time would copy the remainder on every one.
    if (d->aq_read > 65536)
    {
        memmove(d->aq, d->aq + d->aq_read, (d->aq_len - d->aq_read) * sizeof(S16));
        d->aq_len -= d->aq_read;
        d->aq_read = 0;
    }

    return give / d->channels;
}

const char* iFMVDecoderName()
{
    return "ffmpeg";
}
