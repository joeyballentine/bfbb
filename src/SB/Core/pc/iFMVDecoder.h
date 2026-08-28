#ifndef IFMVDECODER_H
#define IFMVDECODER_H

#include <types.h>

// PC-only, and the same shape as iSndHost.h and iPadHost.h for the same reason:
// what decodes a movie is not the game's business and is not the same on every
// host. iFMV.cpp owns the PLAYBACK -- the loop, the pacing, the skip button,
// the quad the frame is drawn on, the audio device -- and this is the seam it
// pulls frames through.
//
// The split matters because the decoder is the part with an external dependency
// in it. The port's movies are Xbox .xmv: an XMV container carrying WMV2 video
// with IMA ADPCM, raw PCM, or no audio at all. WMV2 is a DCT codec in the H.263
// family and writing one is not a thing anybody should do for eight videos, so
// the first backend hands the work to FFmpeg. A second backend that demuxes XMV
// itself and decodes WMV2 through Media Foundation would drop that dependency
// on Windows, and it would implement exactly this interface.
//
// A build with no decoder is expected and supported: iFMVDecoderOpen returns
// NULL, iFMV.cpp reports the movie as having run to the end, and the game moves
// on. That is what happens today for every movie.

struct iFMVDecoder;

// The shape of what comes out. Filled by Open and constant for the movie.
struct iFMVDecoderInfo
{
    // Of the picture itself. Movies in this game are 640x480, 720x480 and
    // 720x486, so the caller cannot assume the video is the shape of the
    // screen and must letterbox rather than stretch.
    U32 width;
    U32 height;

    // Seconds per frame, already sanitised. RWLogo.xmv declares 1000 fps, which
    // is not a frame rate, so a backend that cannot get a believable one is
    // expected to substitute something sane rather than pass the header through.
    F32 frame_time;

    // 0 when the movie has no audio track -- RWLogo.xmv has none. The caller
    // opens no audio device in that case rather than one it never feeds.
    U32 sample_rate;
    U32 channels;
};

// NULL if the file will not open or nothing here can decode it, which is not an
// error the game should see: a missing or unplayable movie is reported to the
// game as one that finished.
iFMVDecoder* iFMVDecoderOpen(const char* path, iFMVDecoderInfo* info);
void iFMVDecoderClose(iFMVDecoder* dec);

// The next video frame, as tightly packed BGRA8888 -- the order a D3D9 raster
// wants, so the caller can hand the rows straight to a locked texture.
//
// `pitch` is the stride in bytes, which is NOT width*4 in general. Returns
// FALSE at the end of the movie, and the pixels belong to the decoder until the
// next call.
S32 iFMVDecoderNextFrame(iFMVDecoder* dec, const void** pixels, U32* pitch, F32* pts);

// Decoded audio, interleaved 16-bit at the rate Open reported, written into the
// caller's buffer. Returns the number of SAMPLE FRAMES written, 0 at the end of
// the stream.
//
// Pulled rather than pushed because the audio device asks for what it needs and
// the video is paced against the audio clock; a decoder that pushed would make
// the caller buffer for it.
U32 iFMVDecoderReadAudio(iFMVDecoder* dec, S16* out, U32 frames);

// Names the backend that was linked in, for the startup log. "none" when there
// is no decoder, which is what makes a build without one obvious in a log
// rather than a silent skip.
const char* iFMVDecoderName();

#endif
