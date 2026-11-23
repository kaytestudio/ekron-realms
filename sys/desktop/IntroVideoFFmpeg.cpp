// Intro video through FFmpeg's libraries (see IntroVideo.h).
//
// Decoding runs on the caller's thread, a little ahead of the clock: queued
// frames stay in the decoder's YUV buffers and only the frame on screen is
// converted to RGBA. The clock is the soundtrack's position (frames the audio
// output has consumed), interpolated with wall time between audio callbacks
// and after the soundtrack ends; without sound it is wall time.

#include "IntroVideo.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59, 37, 100)
#error "The intro video needs FFmpeg 5.1 or newer (AVChannelLayout API)"
#endif

#include <algorithm>
#include <chrono>
#include <deque>
#include <vector>

namespace ekron::desktop {

namespace {

constexpr size_t kMaxQueuedFrames = 48;     // decoded ahead (YUV, ~1.4 MB each at 720p)
constexpr double kVideoLead = 0.20;         // decode this far past the clock
constexpr double kAudioLead = 0.40;         // keep this much sound queued
constexpr int kMaxPacketsPerUpdate = 256;   // never stall a frame on decoding

double steadySeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct QueuedFrame {
    double pts = 0.0;
    AVFrame *frame = nullptr;
};

class FfmpegIntro final : public IntroVideo {
public:
    FfmpegIntro() = default;
    FfmpegIntro(const FfmpegIntro &) = delete;
    FfmpegIntro &operator=(const FfmpegIntro &) = delete;

    ~FfmpegIntro() override {
        if (audioOut_) audioOut_->clearQueue();
        for (auto &q : queue_) av_frame_free(&q.frame);
        av_frame_free(&decoded_);
        av_packet_free(&packet_);
        sws_freeContext(sws_);
        swr_free(&swr_);
        avcodec_free_context(&videoDec_);
        avcodec_free_context(&audioDec_);
        avformat_close_input(&format_);
    }

    bool open(const std::string &path, const IntroOptions &options, std::string &error) {
        now_ = options.now ? options.now : std::function<double()>(steadySeconds);
        if (avformat_open_input(&format_, path.c_str(), nullptr, nullptr) < 0) {
            error = "cannot open " + path;
            return false;
        }
        if (avformat_find_stream_info(format_, nullptr) < 0) {
            error = "no stream information in " + path;
            return false;
        }
        videoStream_ = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (videoStream_ < 0 || !openDecoder(videoStream_, videoDec_)) {
            error = "no decodable video stream in " + path;
            return false;
        }
        const AVStream *vs = format_->streams[videoStream_];
        AVRational rate = av_guess_frame_rate(format_, const_cast<AVStream *>(vs), nullptr);
        frameDuration_ = rate.num > 0 && rate.den > 0 ? av_q2d(av_inv_q(rate)) : 1.0 / 30.0;

        if (options.audio && options.audio->sampleRate() > 0) {
            audioStream_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, videoStream_, nullptr, 0);
            if (audioStream_ >= 0 && openDecoder(audioStream_, audioDec_) && openResampler(options.audio->sampleRate())) {
                audioOut_ = options.audio;
                outRate_ = options.audio->sampleRate();
                audioOut_->clearQueue();
                playedBase_ = audioOut_->playedFrames();
            } else {
                audioStream_ = -1; // play the picture without sound
            }
        }
        for (unsigned i = 0; i < format_->nb_streams; ++i) {
            const bool used = int(i) == videoStream_ || int(i) == audioStream_;
            format_->streams[i]->discard = used ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
        }

        packet_ = av_packet_alloc();
        decoded_ = av_frame_alloc();
        if (!packet_ || !decoded_) {
            error = "out of memory";
            return false;
        }
        // Decode the first picture (and some sound) now, so a broken file fails here.
        for (int i = 0; i < 4096 && queue_.empty() && !endOfFile_; ++i) readPacket();
        if (queue_.empty()) {
            error = "no decodable video frames in " + path;
            return false;
        }
        return true;
    }

    bool update() override {
        if (!started_) {
            started_ = true;
            anchorTime_ = now_();
        }
        const double t = clock();
        for (int i = 0; i < kMaxPacketsPerUpdate && needMore(t); ++i) readPacket();

        // Show the latest frame that is due; frames we're late for are dropped.
        AVFrame *due = nullptr;
        while (!queue_.empty() && queue_.front().pts <= t) {
            if (due) av_frame_free(&due);
            due = queue_.front().frame;
            lastShownPts_ = queue_.front().pts;
            queue_.pop_front();
        }
        if (due) {
            convert(due);
            av_frame_free(&due);
        }

        if (!endOfFile_ || !queue_.empty()) return true;
        if (audioOut_ && audioOut_->queuedFrames() > 0) return true;
        return t < lastShownPts_ + frameDuration_;
    }

    IntroFrame frame() const override {
        IntroFrame f;
        if (rgba_.empty()) return f;
        f.pixels = rgba_.data();
        f.width = width_;
        f.height = height_;
        f.stride = width_ * 4;
        f.serial = serial_;
        return f;
    }

private:
    bool openDecoder(int streamIndex, AVCodecContext *&ctx) {
        const AVStream *stream = format_->streams[streamIndex];
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) return false;
        ctx = avcodec_alloc_context3(codec);
        if (!ctx || avcodec_parameters_to_context(ctx, stream->codecpar) < 0) return false;
        ctx->pkt_timebase = stream->time_base;
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            ctx->thread_count = 0; // one per core
            ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        }
        return avcodec_open2(ctx, codec, nullptr) >= 0;
    }

    bool openResampler(int outRate) {
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        AVChannelLayout in{};
        if (audioDec_->ch_layout.nb_channels > 0) av_channel_layout_copy(&in, &audioDec_->ch_layout);
        else av_channel_layout_default(&in, 2);
        const int rc = swr_alloc_set_opts2(&swr_, &stereo, AV_SAMPLE_FMT_FLT, outRate, &in, audioDec_->sample_fmt,
                                           audioDec_->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&in);
        return rc >= 0 && swr_init(swr_) >= 0;
    }

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
        return audioOut_ ? double(audioOut_->queuedFrames()) / double(outRate_) : 1e9;
    }

    bool needMore(double t) const {
        if (endOfFile_ || queue_.size() >= kMaxQueuedFrames) return false;
        if (queue_.empty() || queue_.back().pts < t + kVideoLead) return true;
        return audioQueuedSeconds() < kAudioLead;
    }

    void readPacket() {
        const int rc = av_read_frame(format_, packet_);
        if (rc < 0) { // end of file (or a read error): flush both decoders
            endOfFile_ = true;
            avcodec_send_packet(videoDec_, nullptr);
            receiveVideo();
            if (audioDec_) {
                avcodec_send_packet(audioDec_, nullptr);
                receiveAudio();
                flushResampler();
            }
            return;
        }
        // A packet the decoder rejects (corrupt data) is skipped; receive*()
        // always drains, so the decoder never refuses input with EAGAIN.
        if (packet_->stream_index == videoStream_) {
            avcodec_send_packet(videoDec_, packet_);
            receiveVideo();
        } else if (audioDec_ && packet_->stream_index == audioStream_) {
            avcodec_send_packet(audioDec_, packet_);
            receiveAudio();
        }
        av_packet_unref(packet_);
    }

    void receiveVideo() {
        const AVStream *stream = format_->streams[videoStream_];
        const int64_t start = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
        while (avcodec_receive_frame(videoDec_, decoded_) == 0) {
            double pts = lastQueuedPts_ + frameDuration_;
            if (decoded_->best_effort_timestamp != AV_NOPTS_VALUE)
                pts = double(decoded_->best_effort_timestamp - start) * av_q2d(stream->time_base);
            if (queue_.empty() && lastQueuedPts_ < 0.0) pts = std::max(pts, 0.0);
            lastQueuedPts_ = pts;
            QueuedFrame q;
            q.pts = pts;
            q.frame = av_frame_clone(decoded_);
            av_frame_unref(decoded_);
            if (q.frame) queue_.push_back(q);
        }
    }

    void receiveAudio() {
        while (avcodec_receive_frame(audioDec_, decoded_) == 0) {
            const int capacity = swr_get_out_samples(swr_, decoded_->nb_samples);
            if (capacity > 0) {
                pcm_.resize(size_t(capacity) * 2);
                uint8_t *out[1] = {reinterpret_cast<uint8_t *>(pcm_.data())};
                const int got = swr_convert(swr_, out, capacity,
                                            const_cast<const uint8_t **>(decoded_->extended_data),
                                            decoded_->nb_samples);
                if (got > 0) audioOut_->queueStereo(pcm_.data(), got);
            }
            av_frame_unref(decoded_);
        }
    }

    void flushResampler() {
        const int capacity = swr_get_out_samples(swr_, 0);
        if (capacity <= 0) return;
        pcm_.resize(size_t(capacity) * 2);
        uint8_t *out[1] = {reinterpret_cast<uint8_t *>(pcm_.data())};
        const int got = swr_convert(swr_, out, capacity, nullptr, 0);
        if (got > 0) audioOut_->queueStereo(pcm_.data(), got);
    }

    void convert(const AVFrame *f) {
        if (f->width <= 0 || f->height <= 0) return;
        const auto format = static_cast<AVPixelFormat>(f->format);
        sws_ = sws_getCachedContext(sws_, f->width, f->height, format, f->width, f->height, AV_PIX_FMT_RGBA,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_) return;
        // Untagged video: BT.709 for HD sizes, BT.601 below, limited range (what players assume).
        int space = SWS_CS_DEFAULT;
        if (f->colorspace == AVCOL_SPC_BT709 || (f->colorspace == AVCOL_SPC_UNSPECIFIED && f->height >= 720))
            space = SWS_CS_ITU709;
        else if (f->colorspace == AVCOL_SPC_BT2020_NCL || f->colorspace == AVCOL_SPC_BT2020_CL)
            space = SWS_CS_BT2020;
        const int fullRange = f->color_range == AVCOL_RANGE_JPEG || format == AV_PIX_FMT_YUVJ420P ? 1 : 0;
        sws_setColorspaceDetails(sws_, sws_getCoefficients(space), fullRange, sws_getCoefficients(SWS_CS_DEFAULT), 1,
                                 0, 1 << 16, 1 << 16);
        width_ = f->width;
        height_ = f->height;
        rgba_.resize(size_t(width_) * size_t(height_) * 4);
        uint8_t *dst[4] = {rgba_.data(), nullptr, nullptr, nullptr};
        int dstStride[4] = {width_ * 4, 0, 0, 0};
        sws_scale(sws_, f->data, f->linesize, 0, f->height, dst, dstStride);
        ++serial_;
    }

    AVFormatContext *format_ = nullptr;
    AVCodecContext *videoDec_ = nullptr;
    AVCodecContext *audioDec_ = nullptr;
    int videoStream_ = -1;
    int audioStream_ = -1;
    SwsContext *sws_ = nullptr;
    SwrContext *swr_ = nullptr;
    AVPacket *packet_ = nullptr;
    AVFrame *decoded_ = nullptr;

    IntroAudioSink *audioOut_ = nullptr;
    int outRate_ = 0;
    int64_t playedBase_ = 0;
    int64_t lastPlayed_ = 0;
    std::function<double()> now_;
    bool started_ = false;
    double anchorClock_ = 0.0;
    double anchorTime_ = 0.0;

    std::deque<QueuedFrame> queue_;
    std::vector<float> pcm_;
    double frameDuration_ = 1.0 / 30.0;
    double lastQueuedPts_ = -1.0;
    double lastShownPts_ = 0.0;
    bool endOfFile_ = false;

    std::vector<uint8_t> rgba_;
    int width_ = 0;
    int height_ = 0;
    uint64_t serial_ = 0;
};

} // namespace

const char *introVideoBackend() { return "FFmpeg"; }

std::unique_ptr<IntroVideo> openIntroVideo(const std::string &path, const IntroOptions &options, std::string &error) {
    av_log_set_level(AV_LOG_ERROR);
    auto video = std::make_unique<FfmpegIntro>();
    if (!video->open(path, options, error)) return nullptr;
    return video;
}

} // namespace ekron::desktop
