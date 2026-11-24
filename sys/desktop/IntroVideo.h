#pragma once
// The startup intro video (assets/intro.mp4), as on Android and iOS.
//
// Backends, picked at build time:
//   * macOS: AVFoundation (IntroVideoApple.mm), sound through AVPlayer;
//   * Windows: Media Foundation (IntroVideoMediaFoundation.cpp), sound
//     through an IntroAudioSink;
//   * elsewhere: FFmpeg's libavformat/libavcodec/libswscale/libswresample
//     (IntroVideoFFmpeg.cpp) when found, sound through an IntroAudioSink;
//   * otherwise none (IntroVideoNone.cpp): the game starts without the intro.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace ekron::desktop {

// Receives the soundtrack (FFmpeg and Media Foundation backends). DesktopAudio mixes it over the
// game sound; tests use a fake that "plays" on demand.
class IntroAudioSink {
public:
    virtual ~IntroAudioSink() = default;
    virtual int sampleRate() const = 0;                       // 0: no output, play silently
    virtual void queueStereo(const float *samples, int frames) = 0; // interleaved L R
    virtual int64_t playedFrames() const = 0;                 // consumed by the output so far
    virtual int64_t queuedFrames() const = 0;                 // waiting to be played
    virtual void clearQueue() = 0;
};

struct IntroFrame {
    const uint8_t *pixels = nullptr; // top row first; null before the first frame
    int width = 0;
    int height = 0;
    int stride = 0;                  // bytes per row
    bool bgra = false;               // BGRA (AVFoundation, Media Foundation) rather than RGBA
    uint64_t serial = 0;             // changes whenever the pixels change
};

class IntroVideo {
public:
    virtual ~IntroVideo() = default;
    // Advances playback; returns false once the video has ended or failed.
    virtual bool update() = 0;
    // The frame to show now. Pixels stay valid until the next update().
    virtual IntroFrame frame() const = 0;
};

struct IntroOptions {
    IntroAudioSink *audio = nullptr; // FFmpeg / Media Foundation; null = no sound
    std::function<double()> now;     // seconds, monotonic; empty = steady clock
};

// "AVFoundation", "MediaFoundation", "FFmpeg" or "none".
const char *introVideoBackend();

// Opens and starts the video. Returns null with `error` set when the file is
// missing or can't be decoded, or when no backend is compiled in.
std::unique_ptr<IntroVideo> openIntroVideo(const std::string &path, const IntroOptions &options,
                                           std::string &error);

} // namespace ekron::desktop
