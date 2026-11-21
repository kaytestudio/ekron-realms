#pragma once
// Ogg Vorbis decoding for the game sounds (stb_vorbis, public domain / MIT).
// Shared by the desktop client, Android and iOS.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ekron::audio {

// Interleaved 16-bit PCM.
struct Pcm {
    int rate = 0;
    int channels = 0;
    std::vector<int16_t> samples;
    size_t frames() const { return channels > 0 ? samples.size() / size_t(channels) : 0; }
};

// Decodes a whole .ogg held in memory. Mono and stereo only.
bool decodeOgg(const uint8_t *data, size_t size, Pcm &out, std::string *error = nullptr);

// Decodes an .ogg a block at a time (music, ambience): only the compressed
// bytes stay in memory. Not thread safe; the mixer owns one per voice.
class OggStream {
public:
    OggStream() = default;
    ~OggStream();
    OggStream(const OggStream &) = delete;
    OggStream &operator=(const OggStream &) = delete;

    bool open(std::shared_ptr<const std::vector<uint8_t>> bytes, std::string *error = nullptr);
    void close();
    bool isOpen() const { return handle_ != nullptr; }
    int rate() const { return rate_; }
    int channels() const { return channels_; }
    // Up to `frames` interleaved float frames; 0 at the end of the stream.
    int read(float *interleaved, int frames);
    void rewind();

private:
    std::shared_ptr<const std::vector<uint8_t>> bytes_;
    void *handle_ = nullptr; // stb_vorbis *
    int rate_ = 0, channels_ = 0;
};

// Rate / channels / length of an .ogg without decoding it all.
bool probeOgg(const uint8_t *data, size_t size, int &rate, int &channels, int64_t &frames);

} // namespace ekron::audio
