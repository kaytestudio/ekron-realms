// Intro video through Windows Media Foundation (see IntroVideo.h).
//
// Windows 10 and 11 decode H.264 and AAC in Media Foundation out of the box
// (the "N" editions need the Media Feature Pack; without it the intro fails
// to open and the game starts straight away). mfplat.dll and mfreadwrite.dll
// are loaded at run time for that reason: linked normally, the game itself
// would not start on such a system. A synchronous source reader
// converts the picture to 32-bit BGRX and the sound to float PCM. Timing works
// like the FFmpeg backend: decoding runs on the caller's thread, a little
// ahead of the clock (picture and sound are read separately, so neither
// waits for the other's interleaving in the file), and the clock is the soundtrack's position (frames the
// audio output has consumed), interpolated with wall time between audio
// callbacks and after the soundtrack ends; without sound it is wall time.

#include "IntroVideo.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <deque>
#include <vector>

namespace ekron::desktop {

namespace {

constexpr size_t kMaxQueuedFrames = 12;   // decoded ahead (BGRX, ~3.7 MB each at 720p)
constexpr double kVideoLead = 0.20;       // decode this far past the clock
constexpr double kAudioLead = 0.40;       // keep this much sound queued
constexpr int kMaxSamplesPerUpdate = 256; // never stall a frame on decoding

double steadySeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Minimal COM pointer (no WRL / ATL dependency, so MinGW builds it too).
template <typename T> class Com {
public:
    Com() = default;
    ~Com() { reset(); }
    Com(const Com &o) : p_(o.p_) {
        if (p_) p_->AddRef();
    }
    Com &operator=(const Com &o) {
        if (this != &o) {
            reset();
            p_ = o.p_;
            if (p_) p_->AddRef();
        }
        return *this;
    }
    Com(Com &&o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    Com &operator=(Com &&o) noexcept {
        if (this != &o) {
            reset();
            p_ = o.p_;
            o.p_ = nullptr;
        }
        return *this;
    }
    void reset() {
        if (p_) p_->Release();
        p_ = nullptr;
    }
    T *get() const { return p_; }
    T *operator->() const { return p_; }
    T **put() {
        reset();
        return &p_;
    }
    explicit operator bool() const { return p_ != nullptr; }

private:
    T *p_ = nullptr;
};

std::wstring widen(const std::string &s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(std::max(n, 0)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string hresult(HRESULT hr) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08lx", static_cast<unsigned long>(hr));
    return buf;
}

// The Media Foundation entry points, from System32 at run time.
struct MfApi {
    HRESULT(WINAPI *Startup)(ULONG, DWORD) = nullptr;
    HRESULT(WINAPI *Shutdown)() = nullptr;
    HRESULT(WINAPI *CreateAttributes)(IMFAttributes **, UINT32) = nullptr;
    HRESULT(WINAPI *CreateMediaType)(IMFMediaType **) = nullptr;
    HRESULT(WINAPI *CreateSourceReaderFromURL)(LPCWSTR, IMFAttributes *, IMFSourceReader **) = nullptr;
    std::string error;
};

template <typename Fn> bool bind(HMODULE lib, const char *name, Fn &fn) {
    fn = lib ? reinterpret_cast<Fn>(reinterpret_cast<void *>(GetProcAddress(lib, name))) : nullptr;
    return fn != nullptr;
}

const MfApi &mf() {
    static const MfApi api = [] {
        MfApi a;
        // Kept loaded for the life of the process (as if linked).
        const HMODULE plat = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        const HMODULE rw = LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!plat || !rw) {
            a.error = "Media Foundation is not installed (Windows N: install the Media Feature Pack)";
        } else if (!bind(plat, "MFStartup", a.Startup) || !bind(plat, "MFShutdown", a.Shutdown) ||
                   !bind(plat, "MFCreateAttributes", a.CreateAttributes) ||
                   !bind(plat, "MFCreateMediaType", a.CreateMediaType) ||
                   !bind(rw, "MFCreateSourceReaderFromURL", a.CreateSourceReaderFromURL)) {
            a.error = "Media Foundation is incomplete on this system";
            a.Startup = nullptr;
        }
        return a;
    }();
    return api;
}

constexpr DWORD kVideo = DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr DWORD kAudio = DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

struct QueuedFrame {
    double pts = 0.0;
    Com<IMFSample> sample;
};

class MediaFoundationIntro final : public IntroVideo {
public:
    MediaFoundationIntro() = default;
    MediaFoundationIntro(const MediaFoundationIntro &) = delete;
    MediaFoundationIntro &operator=(const MediaFoundationIntro &) = delete;

    ~MediaFoundationIntro() override {
        if (audioOut_) audioOut_->clearQueue();
        queue_.clear();
        reader_.reset();
        if (mfStarted_) mf().Shutdown();
        if (comStarted_) CoUninitialize();
    }

    bool open(const std::string &path, const IntroOptions &options, std::string &error) {
        now_ = options.now ? options.now : std::function<double()>(steadySeconds);
        // Single-threaded apartment, like SDL on this thread; if the thread is
        // already in the other mode that works too (a synchronous source
        // reader runs in either).
        const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comStarted_ = SUCCEEDED(co);
        if (FAILED(co) && co != RPC_E_CHANGED_MODE) {
            error = "COM initialisation failed (" + hresult(co) + ")";
            return false;
        }
        if (!mf().Startup) {
            error = mf().error;
            return false;
        }
        HRESULT hr = mf().Startup(MF_VERSION, MFSTARTUP_NOSOCKET);
        if (FAILED(hr)) {
            error = "Media Foundation is not available (" + hresult(hr) + ")";
            return false;
        }
        mfStarted_ = true;

        {
            const DWORD attrs = GetFileAttributesW(widen(path).c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                error = "cannot open " + path;
                return false;
            }
        }
        Com<IMFAttributes> attributes;
        if (FAILED(mf().CreateAttributes(attributes.put(), 2))) {
            error = "out of memory";
            return false;
        }
        // YUV to RGB in the reader (software is fine for a 720p intro).
        attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        hr = mf().CreateSourceReaderFromURL(widen(path).c_str(), attributes.get(), reader_.put());
        if (FAILED(hr)) {
            error = "cannot open " + path + " (" + hresult(hr) + ")";
            return false;
        }
        reader_->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        if (FAILED(reader_->SetStreamSelection(kVideo, TRUE)) || !configureVideo()) {
            error = "no decodable video stream in " + path;
            return false;
        }
        if (options.audio && options.audio->sampleRate() > 0) {
            outRate_ = options.audio->sampleRate();
            if (SUCCEEDED(reader_->SetStreamSelection(kAudio, TRUE)) && configureAudio()) {
                audioOut_ = options.audio;
                audioOut_->clearQueue();
                playedBase_ = audioOut_->playedFrames();
            } else {
                reader_->SetStreamSelection(kAudio, FALSE); // play the picture without sound
                audioEnded_ = true;
            }
        } else {
            audioEnded_ = true;
        }

        // Decode the first picture (and some sound) now, so a broken file fails here.
        for (int i = 0; i < 64 && queue_.empty() && !videoEnded_; ++i) readVideo();
        if (queue_.empty()) {
            error = "no decodable video frames in " + path;
            return false;
        }
        for (int i = 0; i < 256 && !audioEnded_ && audioQueuedSeconds() < kAudioLead; ++i) readAudio();
        return true;
    }

    bool update() override {
        if (!started_) {
            started_ = true;
            anchorTime_ = now_();
        }
        const double t = clock();
        for (int i = 0; i < kMaxSamplesPerUpdate; ++i) {
            if (needVideo(t)) readVideo();
            else if (needAudio()) readAudio();
            else break;
        }

        // Show the latest frame that is due; frames we're late for are dropped.
        Com<IMFSample> due;
        while (!queue_.empty() && queue_.front().pts <= t) {
            due = std::move(queue_.front().sample);
            lastShownPts_ = queue_.front().pts;
            queue_.pop_front();
        }
        if (due) convert(due.get());

        if (!endOfFile() || !queue_.empty()) return true;
        if (audioOut_ && audioOut_->queuedFrames() > 0) return true;
        return t < lastShownPts_ + frameDuration_;
    }

    IntroFrame frame() const override {
        IntroFrame f;
        if (bgra_.empty()) return f;
        f.pixels = bgra_.data();
        f.width = width_;
        f.height = height_;
        f.stride = width_ * 4;
        f.bgra = true;
        f.serial = serial_;
        return f;
    }

private:
    bool configureVideo() {
        Com<IMFMediaType> type;
        if (FAILED(mf().CreateMediaType(type.put()))) return false;
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (FAILED(reader_->SetCurrentMediaType(kVideo, nullptr, type.get()))) return false;
        return readVideoFormat();
    }

    // Frame size, the visible area, row order and frame rate of the output.
    bool readVideoFormat() {
        Com<IMFMediaType> type;
        if (FAILED(reader_->GetCurrentMediaType(kVideo, type.put()))) return false;
        UINT32 w = 0, h = 0;
        if (FAILED(MFGetAttributeSize(type.get(), MF_MT_FRAME_SIZE, &w, &h)) || w == 0 || h == 0) return false;
        frameWidth_ = int(w);
        frameHeight_ = int(h);
        // Decoders pad to whole macroblocks (1080 -> 1088): show the aperture.
        cropX_ = cropY_ = 0;
        visibleWidth_ = frameWidth_;
        visibleHeight_ = frameHeight_;
        MFVideoArea area{};
        if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8 *>(&area), sizeof area,
                                    nullptr))) {
            const int x = area.OffsetX.value, y = area.OffsetY.value;
            const int aw = int(area.Area.cx), ah = int(area.Area.cy);
            if (x >= 0 && y >= 0 && aw > 0 && ah > 0 && x + aw <= frameWidth_ && y + ah <= frameHeight_) {
                cropX_ = x;
                cropY_ = y;
                visibleWidth_ = aw;
                visibleHeight_ = ah;
            }
        }
        // RGB is bottom-up unless the stride says otherwise.
        defaultStride_ = -frameWidth_ * 4;
        UINT32 stride = 0;
        if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) defaultStride_ = int(INT32(stride));
        UINT32 num = 0, den = 0;
        if (SUCCEEDED(MFGetAttributeRatio(type.get(), MF_MT_FRAME_RATE, &num, &den)) && num > 0 && den > 0)
            frameDuration_ = double(den) / double(num);
        return true;
    }

    // Float stereo at the output rate if the reader converts it (Windows 8+
    // resamples in the source reader); otherwise float at the file's own
    // rate and channel count, converted here.
    bool configureAudio() {
        for (int attempt = 0; attempt < 2; ++attempt) {
            Com<IMFMediaType> type;
            if (FAILED(mf().CreateMediaType(type.put()))) return false;
            type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
            if (attempt == 0) {
                type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
                type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, UINT32(outRate_));
                type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
                type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 8);
                type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, UINT32(outRate_) * 8);
            }
            if (FAILED(reader_->SetCurrentMediaType(kAudio, nullptr, type.get()))) continue;
            Com<IMFMediaType> actual;
            if (FAILED(reader_->GetCurrentMediaType(kAudio, actual.put()))) continue;
            GUID subtype{};
            UINT32 channels = 0, rate = 0, bits = 0;
            actual->GetGUID(MF_MT_SUBTYPE, &subtype);
            actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
            actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
            actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
            if (subtype != MFAudioFormat_Float || bits != 32 || channels == 0 || channels > 32 || rate == 0) continue;
            inChannels_ = int(channels);
            inRate_ = int(rate);
            return true;
        }
        return false;
    }

    bool endOfFile() const { return videoEnded_ && audioEnded_; }

    double clock() {
        const double wall = now_();
        if (audioOut_) {
            const int64_t played = audioOut_->playedFrames() - playedBase_;
            if (played != lastPlayed_) {
                lastPlayed_ = played;
                anchorClock_ = double(played) / double(outRate_);
                anchorTime_ = wall;
                return anchorClock_;
            }
        }
        return anchorClock_ + std::max(0.0, wall - anchorTime_);
    }

    double audioQueuedSeconds() const {
        return audioOut_ && !audioEnded_ ? double(audioOut_->queuedFrames()) / double(outRate_) : 1e9;
    }

    bool needVideo(double t) const {
        if (videoEnded_ || queue_.size() >= kMaxQueuedFrames) return false;
        return queue_.empty() || queue_.back().pts < t + kVideoLead;
    }

    bool needAudio() const { return audioOut_ && !audioEnded_ && audioQueuedSeconds() < kAudioLead; }

    // One sample of `stream`; false when there was none (end, gap or error).
    bool readFrom(DWORD stream, Com<IMFSample> &sample, LONGLONG &timestamp, bool &ended) {
        DWORD actual = 0, flags = 0;
        const HRESULT hr = reader_->ReadSample(stream, 0, &actual, &flags, &timestamp, sample.put());
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) { // a read or decode error ends playback
            videoEnded_ = audioEnded_ = true;
            return false;
        }
        if (stream == kVideo && (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)) readVideoFormat();
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) ended = true;
        return bool(sample);
    }

    void readVideo() {
        Com<IMFSample> sample;
        LONGLONG timestamp = 0;
        if (readFrom(kVideo, sample, timestamp, videoEnded_)) queueVideo(std::move(sample), timestamp);
    }

    void readAudio() {
        Com<IMFSample> sample;
        LONGLONG timestamp = 0;
        const bool wasEnded = audioEnded_;
        if (readFrom(kAudio, sample, timestamp, audioEnded_) && audioOut_) queueAudio(sample.get());
        if (audioEnded_ && !wasEnded && audioOut_) flushResampler();
    }

    void queueVideo(Com<IMFSample> sample, LONGLONG timestamp) {
        double pts = double(timestamp) / 1e7; // 100 ns units
        if (queue_.empty() && lastQueuedPts_ < 0.0) pts = std::max(pts, 0.0);
        lastQueuedPts_ = pts;
        QueuedFrame q;
        q.pts = pts;
        q.sample = std::move(sample);
        queue_.push_back(std::move(q));
    }

    void queueAudio(IMFSample *sample) {
        Com<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(buffer.put()))) return;
        BYTE *data = nullptr;
        DWORD bytes = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &bytes))) return;
        const int frames = int(bytes / (sizeof(float) * DWORD(inChannels_)));
        if (frames > 0) push(reinterpret_cast<const float *>(data), frames);
        buffer->Unlock();
    }

    // Interleaved input -> stereo at the output rate (linear interpolation
    // when the reader didn't resample; channels past the first two dropped).
    void push(const float *in, int frames) {
        stereo_.resize(size_t(frames) * 2);
        for (int i = 0; i < frames; ++i) {
            const float *s = in + size_t(i) * size_t(inChannels_);
            const float l = s[0], r = inChannels_ > 1 ? s[1] : s[0];
            stereo_[size_t(i) * 2] = std::isfinite(l) ? l : 0.0f;
            stereo_[size_t(i) * 2 + 1] = std::isfinite(r) ? r : 0.0f;
        }
        if (inRate_ == outRate_) {
            audioOut_->queueStereo(stereo_.data(), frames);
            return;
        }
        // Source position of each output frame, continuing across buffers;
        // the last input frame is kept to interpolate across the boundary.
        const double step = double(inRate_) / double(outRate_);
        pcm_.clear();
        for (;;) {
            const double pos = resamplePos_; // relative to the previous buffer's last frame (-1)
            const int i0 = int(std::floor(pos));
            if (i0 + 1 >= frames) break;
            const double f = pos - i0;
            const float l0 = i0 < 0 ? lastL_ : stereo_[size_t(i0) * 2];
            const float r0 = i0 < 0 ? lastR_ : stereo_[size_t(i0) * 2 + 1];
            const float l1 = stereo_[size_t(i0 + 1) * 2], r1 = stereo_[size_t(i0 + 1) * 2 + 1];
            pcm_.push_back(float(l0 + (l1 - l0) * f));
            pcm_.push_back(float(r0 + (r1 - r0) * f));
            resamplePos_ += step;
        }
        resamplePos_ -= frames;
        lastL_ = stereo_[size_t(frames - 1) * 2];
        lastR_ = stereo_[size_t(frames - 1) * 2 + 1];
        if (!pcm_.empty()) audioOut_->queueStereo(pcm_.data(), int(pcm_.size() / 2));
    }

    void flushResampler() {
        if (inRate_ == outRate_ || inRate_ == 0) return;
        const float tail[2] = {lastL_, lastR_};
        audioOut_->queueStereo(tail, 1);
    }

    // The sample's BGRX rows -> top-down BGRA with opaque alpha, cropped to
    // the visible area.
    void convert(IMFSample *sample) {
        Com<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(buffer.put()))) return;
        BYTE *base = nullptr;
        LONG pitch = 0;
        Com<IMF2DBuffer> buffer2d;
        bool locked2d = false;
        DWORD length = 0;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(buffer2d.put()))) &&
            SUCCEEDED(buffer2d->Lock2D(&base, &pitch))) {
            locked2d = true; // pitch < 0: bottom-up, base is the top row
        } else if (SUCCEEDED(buffer->Lock(&base, nullptr, &length))) {
            pitch = defaultStride_;
            const size_t rowBytes = size_t(std::abs(pitch));
            if (length < rowBytes * size_t(frameHeight_)) {
                buffer->Unlock();
                return;
            }
            if (pitch < 0) base += rowBytes * size_t(frameHeight_ - 1); // top row of a bottom-up image
        } else {
            return;
        }
        width_ = visibleWidth_;
        height_ = visibleHeight_;
        bgra_.resize(size_t(width_) * size_t(height_) * 4);
        for (int y = 0; y < height_; ++y) {
            const BYTE *src = base + ptrdiff_t(y + cropY_) * ptrdiff_t(pitch) + ptrdiff_t(cropX_) * 4;
            uint8_t *dst = bgra_.data() + size_t(y) * size_t(width_) * 4;
            std::memcpy(dst, src, size_t(width_) * 4);
            for (int x = 0; x < width_; ++x) dst[size_t(x) * 4 + 3] = 255; // X is undefined in RGB32
        }
        if (locked2d) buffer2d->Unlock2D();
        else buffer->Unlock();
        ++serial_;
    }

    bool comStarted_ = false;
    bool mfStarted_ = false;
    Com<IMFSourceReader> reader_;
    int frameWidth_ = 0, frameHeight_ = 0;
    int visibleWidth_ = 0, visibleHeight_ = 0;
    int cropX_ = 0, cropY_ = 0;
    int defaultStride_ = 0;

    IntroAudioSink *audioOut_ = nullptr;
    int outRate_ = 0;
    int inRate_ = 0;
    int inChannels_ = 2;
    double resamplePos_ = 0.0;
    float lastL_ = 0.0f, lastR_ = 0.0f;
    std::vector<float> stereo_, pcm_;
    int64_t playedBase_ = 0;
    int64_t lastPlayed_ = 0;
    std::function<double()> now_;
    bool started_ = false;
    double anchorClock_ = 0.0;
    double anchorTime_ = 0.0;

    std::deque<QueuedFrame> queue_;
    double frameDuration_ = 1.0 / 30.0;
    double lastQueuedPts_ = -1.0;
    double lastShownPts_ = 0.0;
    bool videoEnded_ = false;
    bool audioEnded_ = false;

    std::vector<uint8_t> bgra_;
    int width_ = 0;
    int height_ = 0;
    uint64_t serial_ = 0;
};

} // namespace

const char *introVideoBackend() { return "MediaFoundation"; }

std::unique_ptr<IntroVideo> openIntroVideo(const std::string &path, const IntroOptions &options, std::string &error) {
    auto video = std::make_unique<MediaFoundationIntro>();
    if (!video->open(path, options, error)) return nullptr;
    return video;
}

} // namespace ekron::desktop
