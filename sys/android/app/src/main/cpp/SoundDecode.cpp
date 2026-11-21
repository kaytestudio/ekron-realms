// Ogg Vorbis decoding (see SoundDecode.h). stb_vorbis is compiled here only.

#include "SoundDecode.h"

#include <cstdlib>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "third_party/stb_vorbis.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace ekron::audio {

namespace {
const char *vorbisError(int code) {
    switch (code) {
    case VORBIS_outofmem: return "out of memory";
    case VORBIS_unexpected_eof: return "truncated file";
    case VORBIS_invalid_setup: return "invalid Vorbis setup";
    case VORBIS_invalid_stream: return "invalid Ogg stream";
    case VORBIS_missing_capture_pattern: return "not an Ogg file";
    case VORBIS_invalid_first_page: return "invalid first page";
    default: return "cannot decode";
    }
}

bool sizeOk(size_t size, std::string *error) {
    if (size > 0 && size <= size_t(0x7fffffff)) return true;
    if (error) *error = size == 0 ? "empty file" : "file too large";
    return false;
}
} // namespace

bool decodeOgg(const uint8_t *data, size_t size, Pcm &out, std::string *error) {
    out = Pcm{};
    if (!data || !sizeOk(size, error)) return false;
    int err = 0;
    stb_vorbis *v = stb_vorbis_open_memory(data, int(size), &err, nullptr);
    if (!v) {
        if (error) *error = vorbisError(err);
        return false;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(v);
    if (info.channels < 1 || info.channels > 2 || info.sample_rate < 8000 || info.sample_rate > 192000) {
        stb_vorbis_close(v);
        if (error) *error = "unsupported channels / rate";
        return false;
    }
    out.rate = int(info.sample_rate);
    out.channels = info.channels;
    const unsigned total = stb_vorbis_stream_length_in_samples(v);
    if (total > 0 && total < 60u * 60u * 192000u) out.samples.reserve(size_t(total) * size_t(info.channels));
    short block[4096];
    for (;;) {
        const int frames = stb_vorbis_get_samples_short_interleaved(v, info.channels, block,
                                                                    int(sizeof(block) / sizeof(block[0])));
        if (frames <= 0) break;
        out.samples.insert(out.samples.end(), block, block + size_t(frames) * size_t(info.channels));
    }
    stb_vorbis_close(v);
    if (out.samples.empty()) {
        if (error) *error = "no audio";
        out = Pcm{};
        return false;
    }
    return true;
}

bool probeOgg(const uint8_t *data, size_t size, int &rate, int &channels, int64_t &frames) {
    if (!data || !sizeOk(size, nullptr)) return false;
    int err = 0;
    stb_vorbis *v = stb_vorbis_open_memory(data, int(size), &err, nullptr);
    if (!v) return false;
    const stb_vorbis_info info = stb_vorbis_get_info(v);
    rate = int(info.sample_rate);
    channels = info.channels;
    frames = int64_t(stb_vorbis_stream_length_in_samples(v));
    stb_vorbis_close(v);
    return channels >= 1 && channels <= 2 && rate >= 8000 && rate <= 192000;
}

OggStream::~OggStream() { close(); }

bool OggStream::open(std::shared_ptr<const std::vector<uint8_t>> bytes, std::string *error) {
    close();
    if (!bytes || !sizeOk(bytes->size(), error)) return false;
    int err = 0;
    stb_vorbis *v = stb_vorbis_open_memory(bytes->data(), int(bytes->size()), &err, nullptr);
    if (!v) {
        if (error) *error = vorbisError(err);
        return false;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(v);
    if (info.channels < 1 || info.channels > 2 || info.sample_rate < 8000 || info.sample_rate > 192000) {
        stb_vorbis_close(v);
        if (error) *error = "unsupported channels / rate";
        return false;
    }
    bytes_ = std::move(bytes);
    handle_ = v;
    rate_ = int(info.sample_rate);
    channels_ = info.channels;
    return true;
}

void OggStream::close() {
    if (handle_) stb_vorbis_close(static_cast<stb_vorbis *>(handle_));
    handle_ = nullptr;
    bytes_.reset();
    rate_ = channels_ = 0;
}

int OggStream::read(float *interleaved, int frames) {
    if (!handle_ || frames <= 0) return 0;
    const int got = stb_vorbis_get_samples_float_interleaved(static_cast<stb_vorbis *>(handle_), channels_,
                                                             interleaved, frames * channels_);
    return got > 0 ? got : 0;
}

void OggStream::rewind() {
    if (handle_) stb_vorbis_seek_start(static_cast<stb_vorbis *>(handle_));
}

} // namespace ekron::audio
