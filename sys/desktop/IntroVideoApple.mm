// Intro video through AVFoundation on macOS (see IntroVideo.h). AVPlayer
// plays the soundtrack itself and keeps the clock; each update() copies the
// current picture out of an AVPlayerItemVideoOutput as BGRA. Compiled with ARC.

#include "IntroVideo.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <vector>

namespace ekron::desktop {

namespace {

double steadySeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

class AppleIntro final : public IntroVideo {
public:
    AppleIntro() = default;
    AppleIntro(const AppleIntro &) = delete;
    AppleIntro &operator=(const AppleIntro &) = delete;

    ~AppleIntro() override {
        @autoreleasepool {
            for (id observer in observers_) [[NSNotificationCenter defaultCenter] removeObserver:observer];
            [player_ pause];
            [item_ removeOutput:output_];
            observers_ = nil;
            output_ = nil;
            item_ = nil;
            player_ = nil;
        }
    }

    bool open(const std::string &path, const IntroOptions &options, std::string &error) {
        @autoreleasepool {
            now_ = options.now ? options.now : std::function<double()>(steadySeconds);
            NSString *file = [NSString stringWithUTF8String:path.c_str()];
            if (!file || ![[NSFileManager defaultManager] fileExistsAtPath:file]) {
                error = "cannot open " + path;
                return false;
            }
            AVURLAsset *asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:file] options:nil];
            item_ = [AVPlayerItem playerItemWithAsset:asset];
            NSDictionary *attributes = @{
                (__bridge NSString *)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
            };
            output_ = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:attributes];
            [item_ addOutput:output_];
            player_ = [AVPlayer playerWithPlayerItem:item_];
            player_.actionAtItemEnd = AVPlayerActionAtItemEndPause;

            auto ended = ended_;
            NSMutableArray *observers = [NSMutableArray array];
            for (NSString *name in @[ AVPlayerItemDidPlayToEndTimeNotification,
                                               AVPlayerItemFailedToPlayToEndTimeNotification ]) {
                id observer = [[NSNotificationCenter defaultCenter] addObserverForName:name
                                                                                object:item_
                                                                                 queue:nil
                                                                            usingBlock:^(NSNotification *note) {
                                                                              (void)note;
                                                                              ended->store(true);
                                                                            }];
                [observers addObject:observer];
            }
            observers_ = observers;
            [player_ play];
            openedAt_ = now_();
            return true;
        }
    }

    bool update() override {
        @autoreleasepool {
            if (ended_->load()) return false;
            const AVPlayerItemStatus status = item_.status;
            if (status == AVPlayerItemStatusFailed) return false;
            if (status == AVPlayerItemStatusUnknown) return now_() - openedAt_ < 5.0; // never got ready

            const CMTime time = item_.currentTime;
            if ([output_ hasNewPixelBufferForItemTime:time]) {
                CVPixelBufferRef buffer = [output_ copyPixelBufferForItemTime:time itemTimeForDisplay:nullptr];
                if (buffer) {
                    copyPixels(buffer);
                    CVPixelBufferRelease(buffer);
                }
            }
            // Ended without the notification (or it was missed): stop at the duration.
            const CMTime duration = item_.duration;
            if (CMTIME_IS_NUMERIC(duration) && CMTimeCompare(time, duration) >= 0 && player_.rate == 0.0f)
                return false;
            return true;
        }
    }

    IntroFrame frame() const override {
        IntroFrame f;
        if (pixels_.empty()) return f;
        f.pixels = pixels_.data();
        f.width = width_;
        f.height = height_;
        f.stride = width_ * 4;
        f.bgra = true;
        f.serial = serial_;
        return f;
    }

private:
    void copyPixels(CVPixelBufferRef buffer) {
        if (CVPixelBufferGetPixelFormatType(buffer) != kCVPixelFormatType_32BGRA) return;
        if (CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) return;
        const auto *base = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddress(buffer));
        const size_t rowBytes = CVPixelBufferGetBytesPerRow(buffer);
        const int w = int(CVPixelBufferGetWidth(buffer));
        const int h = int(CVPixelBufferGetHeight(buffer));
        if (base && w > 0 && h > 0 && rowBytes >= size_t(w) * 4) {
            width_ = w;
            height_ = h;
            pixels_.resize(size_t(w) * size_t(h) * 4);
            for (int y = 0; y < h; ++y)
                std::memcpy(pixels_.data() + size_t(y) * size_t(w) * 4, base + size_t(y) * rowBytes, size_t(w) * 4);
            ++serial_;
        }
        CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    }

    AVPlayer *player_ = nil;
    AVPlayerItem *item_ = nil;
    AVPlayerItemVideoOutput *output_ = nil;
    NSArray *observers_ = nil;
    std::shared_ptr<std::atomic<bool>> ended_ = std::make_shared<std::atomic<bool>>(false);
    std::function<double()> now_;
    double openedAt_ = 0.0;

    std::vector<uint8_t> pixels_;
    int width_ = 0;
    int height_ = 0;
    uint64_t serial_ = 0;
};

} // namespace

const char *introVideoBackend() { return "AVFoundation"; }

std::unique_ptr<IntroVideo> openIntroVideo(const std::string &path, const IntroOptions &options, std::string &error) {
    auto video = std::make_unique<AppleIntro>();
    if (!video->open(path, options, error)) return nullptr;
    return video;
}

} // namespace ekron::desktop
