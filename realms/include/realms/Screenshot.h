// Quake 2's "screenshot" command, as Yamagi Quake II has it (vid.c
// VID_WriteScreenshot, gl1_misc.c R_ScreenShot):
//
//   screenshot [tga|bmp|png|jpg] [quality]
//
// The format defaults to tga (any case); the number is the png compression
// level (0..9, 7 when left out) or the jpg quality (1..100, 85 when left out).
// The picture goes to <writable root>/scrnshot/q2_NNNN.<ext>, the first free
// number from 0000 to 9999, and the console says "Wrote q2_NNNN.<ext>".
//
// yquake2 writes the files with stb_image_write; Ekron has its own small
// encoders here instead (uncompressed 24-bit TGA and BMP, PNG with its own
// deflate, baseline JPEG with 4:4:4 sampling), so nothing new is vendored.
//
// The command only checks its arguments: the frame is read back where the
// host draws it. Screenshots::pending() tells the host to read the finished
// frame before presenting it, and Screenshots::deliver() writes the file.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace realms {

class Engine;

enum class ScreenshotFormat { Tga, Bmp, Png, Jpg };

struct ScreenshotRequest {
    ScreenshotFormat format = ScreenshotFormat::Tga;
    int quality = 85; // yquake2's default; png reads anything over 9 as 7
};

constexpr const char *kScreenshotDir = "scrnshot";
constexpr int kScreenshotMaxIndex = 9999;

// "tga", "bmp", "png" or "jpg".
const char *screenshotExtension(ScreenshotFormat format);
// args as the command gets them (args[0] is "screenshot"). False, with
// yquake2's message in `error` (one or two lines, no trailing newline), for
// an unknown format or a quality that isn't all digits.
bool parseScreenshotArgs(const std::vector<std::string> &args, ScreenshotRequest &out, std::string &error);
// "q2_0042.tga"
std::string screenshotFileName(int index, ScreenshotFormat format);
// The first index whose file isn't in `dir`, or -1 when 0000..9999 are all taken.
int nextScreenshotIndex(const std::string &dir, ScreenshotFormat format);

// Tightly packed 8-bit RGB, top row first, out of a host's read back pixels:
// `comps` 3 (RGB) or 4 (RGBA, alpha dropped), `bgr` for BGR(A) order (Metal),
// `bottomUp` when the first row is the bottom one (glReadPixels).
std::vector<uint8_t> screenshotRgb(const uint8_t *pixels, int width, int height, int comps, bool bgr, bool bottomUp);

// The file's bytes for top-down RGB pixels (width * height * 3 of them).
// False for an empty or too large image (TGA and BMP sizes are 16 / 32 bit).
bool encodeTga(int width, int height, const uint8_t *rgb, std::vector<uint8_t> &out);
bool encodeBmp(int width, int height, const uint8_t *rgb, std::vector<uint8_t> &out);
bool encodePng(int width, int height, const uint8_t *rgb, int level, std::vector<uint8_t> &out);
bool encodeJpg(int width, int height, const uint8_t *rgb, int quality, std::vector<uint8_t> &out);
bool encodeScreenshot(const ScreenshotRequest &request, int width, int height, const uint8_t *rgb,
                      std::vector<uint8_t> &out);

// zlib's checksums, as PNG uses them (exposed for the tests).
uint32_t screenshotCrc32(const uint8_t *data, size_t size, uint32_t crc = 0);
uint32_t screenshotAdler32(const uint8_t *data, size_t size, uint32_t adler = 1);

class Screenshots {
public:
    // Adds "screenshot" to the engine; this object must outlive it.
    void registerCommand(Engine &engine);
    // A screenshot was asked for and the next drawn frame should be read.
    bool pending() const { return pending_.load(); }
    // The finished frame, before it is presented (see screenshotRgb for the
    // layout). Writes the file and prints the result; returns true when written.
    bool deliver(Engine &engine, int width, int height, const uint8_t *pixels, int comps, bool bgr, bool bottomUp);
    // The frame couldn't be read: drops the request with a message.
    void cancel(Engine &engine, const std::string &why);
    // The console line of the last deliver(), e.g. "Wrote q2_0000.tga".
    std::string lastMessage() const;

    // The command's work without the engine's command buffer (the tests use it).
    bool request(Engine &engine, const std::vector<std::string> &args);

private:
    mutable std::mutex mutex_;
    std::atomic<bool> pending_{false};
    ScreenshotRequest request_;
    std::string lastMessage_;
};

} // namespace realms
