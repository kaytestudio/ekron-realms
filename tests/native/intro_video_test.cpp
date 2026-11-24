// Intro video backend test (FFmpeg and Media Foundation builds): plays
// assets/intro.mp4 against a
// fake audio output and a fake clock, so it runs without a sound card and
// faster than real time.
//
//   intro_video_test <path/to/intro.mp4> [frame.ppm]
//
// The optional second argument writes the frame shown at 5 s, for a visual check.

#include "IntroVideo.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using ekron::desktop::IntroAudioSink;
using ekron::desktop::IntroFrame;
using ekron::desktop::IntroOptions;
using ekron::desktop::openIntroVideo;

namespace {

int failures = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

// An audio output that plays only when the test says so.
class FakeSink final : public IntroAudioSink {
public:
    int sampleRate() const override { return 48000; }
    void queueStereo(const float *samples, int frames) override {
        for (int i = 0; i < frames * 2; ++i) {
            peak = std::max(peak, std::fabs(samples[i]));
            if (!std::isfinite(samples[i])) ++nonFinite;
        }
        queued += frames;
        total += frames;
    }
    int64_t playedFrames() const override { return played; }
    int64_t queuedFrames() const override { return queued; }
    void clearQueue() override { queued = 0; ++clears; }

    void play(int64_t frames) {
        const int64_t n = std::min(frames, queued);
        queued -= n;
        played += frames; // the device keeps running (silence) when we underflow
    }

    int64_t played = 0, queued = 0, total = 0;
    float peak = 0.0f;
    int nonFinite = 0, clears = 0;
};

bool writePpm(const std::string &path, const IntroFrame &f) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P6\n" << f.width << " " << f.height << "\n255\n";
    for (int y = 0; y < f.height; ++y)
        for (int x = 0; x < f.width; ++x) {
            const uint8_t *p = f.pixels + y * f.stride + x * 4;
            const char rgb[3] = {char(p[f.bgra ? 2 : 0]), char(p[1]), char(p[f.bgra ? 0 : 2])};
            out.write(rgb, 3);
        }
    return bool(out);
}

double meanLuma(const IntroFrame &f) {
    double sum = 0.0;
    for (int y = 0; y < f.height; y += 4)
        for (int x = 0; x < f.width; x += 4) {
            const uint8_t *p = f.pixels + y * f.stride + x * 4;
            const int r = p[f.bgra ? 2 : 0], b = p[f.bgra ? 0 : 2];
            sum += 0.2126 * r + 0.7152 * p[1] + 0.0722 * b;
        }
    return sum / (double((f.height + 3) / 4) * double((f.width + 3) / 4));
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: intro_video_test <intro.mp4> [frame.ppm]\n";
        return 2;
    }
    const std::string path = argv[1];
    const std::string backend = ekron::desktop::introVideoBackend();
    std::cout << "backend: " << backend << "\n";
    const bool expectBgra = backend == "MediaFoundation"; // FFmpeg converts to RGBA

    // --- Full playback, clocked by the (fake) soundtrack --------------------
    {
        FakeSink sink;
        double fakeNow = 0.0;
        IntroOptions options;
        options.audio = &sink;
        options.now = [&] { return fakeNow; }; // frozen: only the audio clock moves
        std::string error;
        auto video = openIntroVideo(path, options, error);
        CHECK(video != nullptr);
        if (!video) {
            std::cerr << "open failed: " << error << "\n";
            return 1;
        }
        CHECK(sink.queued > 0); // priming decoded some sound already

        const int64_t step = 48000 / 24; // one video frame of sound per update
        uint64_t lastSerial = 0;
        int shown = 0, steps = 0, maxGap = 0, sinceChange = 0;
        bool savedFrame = false;
        while (video->update()) {
            const IntroFrame f = video->frame();
            CHECK(f.pixels != nullptr);
            if (f.pixels && f.serial != lastSerial) {
                lastSerial = f.serial;
                ++shown;
                maxGap = std::max(maxGap, sinceChange);
                sinceChange = 0;
                CHECK(f.width == 1280 && f.height == 720 && f.stride == 1280 * 4 && f.bgra == expectBgra);
            } else {
                ++sinceChange;
            }
            if (!savedFrame && steps == 5 * 24) {
                savedFrame = true;
                const double luma = meanLuma(f);
                std::cout << "frame at 5 s: mean luma " << luma << "\n";
                CHECK(luma > 8.0 && luma < 250.0); // a picture, not black or white
                // opaque, whatever the decoder leaves in the fourth byte
                CHECK(f.pixels[3] == 255 && f.pixels[size_t(f.height - 1) * size_t(f.stride) + size_t(f.width - 1) * 4 + 3] == 255);
                if (argc > 2) CHECK(writePpm(argv[2], f));
            }
            sink.play(step);
            if (++steps > 40 * 24) break; // 30 s video: never loop forever
        }
        const double soundSeconds = double(sink.total) / 48000.0;
        std::cout << "shown " << shown << " frames in " << steps << " steps, sound " << soundSeconds
                  << " s, peak " << sink.peak << "\n";
        CHECK(steps >= 29 * 24 && steps <= 31 * 24);   // ends with the 30 s video
        CHECK(shown >= 700 && shown <= 721);           // 720 frames at 24 fps
        CHECK(maxGap <= 1);                            // no frame shown for more than ~2 steps
        CHECK(soundSeconds > 29.5 && soundSeconds < 30.5);
        CHECK(sink.peak > 0.05f && sink.peak <= 1.5f); // real sound, sensible level
        CHECK(sink.nonFinite == 0);
        const int clearsBefore = sink.clears;
        video.reset();
        CHECK(sink.clears == clearsBefore + 1);        // skipping silences the soundtrack
    }

    // --- Without sound: wall clock (faked), stop early like a skip ----------
    {
        double fakeNow = 100.0;
        IntroOptions options;
        options.now = [&] { return fakeNow; };
        std::string error;
        auto video = openIntroVideo(path, options, error);
        CHECK(video != nullptr);
        if (video) {
            uint64_t lastSerial = 0;
            int shown = 0;
            for (int i = 0; i < 48 && video->update(); ++i) { // 2 s at 24 fps
                if (video->frame().serial != lastSerial) { lastSerial = video->frame().serial; ++shown; }
                fakeNow += 1.0 / 24.0;
            }
            CHECK(shown >= 46 && shown <= 48);
            // A big jump (the window was stalled): frames are dropped, not replayed.
            fakeNow += 10.0;
            CHECK(video->update());
            const uint64_t afterJump = video->frame().serial;
            fakeNow += 1.0 / 24.0;
            CHECK(video->update());
            CHECK(video->frame().serial == afterJump + 1);
            // Past the end: finished.
            fakeNow += 60.0;
            bool running = true;
            for (int i = 0; i < 4096 && running; ++i) running = video->update();
            CHECK(!running);
        }
    }

    // --- Missing and corrupt files fail cleanly ----------------------------
    {
        std::string error;
        CHECK(openIntroVideo(path + ".missing", IntroOptions{}, error) == nullptr);
        CHECK(!error.empty());

        const std::string junk = "intro_video_test_junk.mp4";
        {
            std::ofstream out(junk, std::ios::binary);
            uint32_t x = 0x12345678u;
            for (int i = 0; i < 65536; ++i) {
                x = x * 1664525u + 1013904223u;
                out.put(char(x >> 24));
            }
        }
        error.clear();
        CHECK(openIntroVideo(junk, IntroOptions{}, error) == nullptr);
        CHECK(!error.empty());

        // A real file cut short: plays what's there, then ends.
        {
            std::ifstream in(path, std::ios::binary);
            std::vector<char> head(1 << 20);
            in.read(head.data(), std::streamsize(head.size()));
            std::ofstream out(junk, std::ios::binary | std::ios::trunc);
            out.write(head.data(), in.gcount());
        }
        error.clear();
        double fakeNow = 0.0;
        IntroOptions options;
        options.now = [&] { return fakeNow; };
        auto video = openIntroVideo(junk, options, error); // moov at the end: may not open
        if (video) {
            bool running = true;
            for (int i = 0; i < 2000 && running; ++i) {
                running = video->update();
                fakeNow += 0.1;
            }
            CHECK(!running);
        } else {
            CHECK(!error.empty());
        }
        video.reset(); // Windows: the reader holds the file open
        std::remove(junk.c_str());
    }

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "Intro video playback OK\n";
    return 0;
}
