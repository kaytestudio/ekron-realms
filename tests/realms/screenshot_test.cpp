// realms/Screenshot.h: Quake 2's "screenshot" command as Yamagi Quake II has
// it: the arguments and their messages, q2_NNNN file names, and the TGA, BMP,
// PNG and JPEG encoders, each read back with stb_image.
#include "realms/Engine.h"
#include "realms/Screenshot.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "../../sys/android/app/src/main/cpp/third_party/stb_image.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int gFailures = 0;
int gChecks = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++gChecks;                                                                      \
        if (!(cond)) {                                                                  \
            ++gFailures;                                                                \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                               \
    } while (0)

// A picture whose corners differ, so a flipped or mirrored file shows.
std::vector<uint8_t> testImage(int w, int h) {
    std::vector<uint8_t> rgb(size_t(w) * size_t(h) * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t *p = &rgb[(size_t(y) * size_t(w) + size_t(x)) * 3];
            p[0] = uint8_t(x * 255 / std::max(1, w - 1));
            p[1] = uint8_t(y * 255 / std::max(1, h - 1));
            p[2] = uint8_t((x / 4 + y / 4) % 2 ? 200 : 40);
        }
    return rgb;
}

struct Decoded {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;
};

Decoded decode(const std::vector<uint8_t> &file) {
    Decoded d;
    int comps = 0;
    unsigned char *px = stbi_load_from_memory(file.data(), int(file.size()), &d.w, &d.h, &comps, 3);
    if (px) {
        d.rgb.assign(px, px + size_t(d.w) * size_t(d.h) * 3);
        stbi_image_free(px);
    }
    return d;
}

// Largest per-channel difference and the mean one.
void compare(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, int &maxDiff, double &meanDiff) {
    maxDiff = 255;
    meanDiff = 255.0;
    if (a.size() != b.size() || a.empty()) return;
    maxDiff = 0;
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const int d = std::abs(int(a[i]) - int(b[i]));
        maxDiff = std::max(maxDiff, d);
        sum += d;
    }
    meanDiff = sum / double(a.size());
}

uint32_t le32(const std::vector<uint8_t> &f, size_t at) {
    return uint32_t(f[at]) | uint32_t(f[at + 1]) << 8 | uint32_t(f[at + 2]) << 16 | uint32_t(f[at + 3]) << 24;
}

std::vector<std::string> args(std::initializer_list<const char *> list) {
    std::vector<std::string> out;
    for (const char *s : list) out.emplace_back(s);
    return out;
}

} // namespace

int main() {
    using namespace realms;

    // ---- arguments, as VID_WriteScreenshot reads them
    {
        ScreenshotRequest r;
        std::string error;
        CHECK(parseScreenshotArgs(args({"screenshot"}), r, error) && r.format == ScreenshotFormat::Tga &&
              r.quality == 85 && error.empty());
        CHECK(parseScreenshotArgs(args({"screenshot", "BMP"}), r, error) && r.format == ScreenshotFormat::Bmp);
        CHECK(parseScreenshotArgs(args({"screenshot", "Png"}), r, error) && r.format == ScreenshotFormat::Png &&
              r.quality == 85); // encodePng then uses level 7
        CHECK(parseScreenshotArgs(args({"screenshot", "png", "3"}), r, error) && r.quality == 3);
        CHECK(parseScreenshotArgs(args({"screenshot", "png", "12"}), r, error) && r.quality == 9);
        CHECK(parseScreenshotArgs(args({"screenshot", "jpg"}), r, error) && r.format == ScreenshotFormat::Jpg &&
              r.quality == 85);
        CHECK(parseScreenshotArgs(args({"screenshot", "jpg", "0"}), r, error) && r.quality == 1);
        CHECK(parseScreenshotArgs(args({"screenshot", "jpg", "250"}), r, error) && r.quality == 100);
        CHECK(parseScreenshotArgs(args({"screenshot", "jpg", "99999999999999999999"}), r, error) && r.quality == 100);
        CHECK(parseScreenshotArgs(args({"screenshot", "tga", "40"}), r, error) && r.quality == 40); // unused
        CHECK(parseScreenshotArgs(args({"screenshot", "jpg", ""}), r, error) && r.quality == 1);    // atoi("") is 0

        CHECK(!parseScreenshotArgs(args({"screenshot", "gif"}), r, error));
        CHECK(error ==
              "the (optional) second argument to 'screenshot' is the format, one of \"tga\", \"bmp\", \"png\", \"jpg\"");
        CHECK(!parseScreenshotArgs(args({"screenshot", "jpg", "-5"}), r, error));
        CHECK(error == "The (optional!) third argument to 'screenshot' is jpg quality, a number between 1 and 100\n"
                       "  or png compression level, between 0 and 9!");
        CHECK(!parseScreenshotArgs(args({"screenshot", "png", "7x"}), r, error));
    }

    // ---- names
    CHECK(screenshotFileName(0, ScreenshotFormat::Tga) == "q2_0000.tga");
    CHECK(screenshotFileName(42, ScreenshotFormat::Jpg) == "q2_0042.jpg");
    CHECK(screenshotFileName(9999, ScreenshotFormat::Png) == "q2_9999.png");
    CHECK(std::string(screenshotExtension(ScreenshotFormat::Bmp)) == "bmp");

    // ---- checksums against known values
    {
        const std::string s = "123456789";
        CHECK(screenshotCrc32(reinterpret_cast<const uint8_t *>(s.data()), s.size()) == 0xCBF43926u);
        const std::string w = "Wikipedia";
        CHECK(screenshotAdler32(reinterpret_cast<const uint8_t *>(w.data()), w.size()) == 0x11E60398u);
    }

    // ---- read back pixels: GL's bottom-up RGBA and Metal's top-down BGRA
    {
        const uint8_t glRows[] = {1, 2, 3, 255, 4, 5, 6, 255,     // bottom row
                                  7, 8, 9, 255, 10, 11, 12, 255}; // top row
        const std::vector<uint8_t> fromGl = screenshotRgb(glRows, 2, 2, 4, false, true);
        CHECK((fromGl == std::vector<uint8_t>{7, 8, 9, 10, 11, 12, 1, 2, 3, 4, 5, 6}));
        const uint8_t metal[] = {3, 2, 1, 255, 6, 5, 4, 255};
        CHECK((screenshotRgb(metal, 2, 1, 4, true, false) == std::vector<uint8_t>{1, 2, 3, 4, 5, 6}));
        const uint8_t rgb[] = {1, 2, 3};
        CHECK((screenshotRgb(rgb, 1, 1, 3, false, false) == std::vector<uint8_t>{1, 2, 3}));
        CHECK(screenshotRgb(rgb, 1, 1, 2, false, false).empty());
        CHECK(screenshotRgb(nullptr, 1, 1, 3, false, false).empty());
    }

    // ---- encoders: odd sizes (BMP row padding, partial JPEG blocks)
    const int w = 37, h = 21;
    const std::vector<uint8_t> image = testImage(w, h);
    std::vector<uint8_t> file;
    {
        CHECK(encodeTga(w, h, image.data(), file));
        CHECK(file.size() == 18 + size_t(w) * h * 3);
        CHECK(file[2] == 2 && file[12] == w && file[13] == 0 && file[14] == h && file[16] == 24 && file[17] == 0);
        // bottom-left origin: the first pixel stored is the bottom-left one, as BGR
        const uint8_t *bottomLeft = &image[size_t(h - 1) * w * 3];
        CHECK(file[18] == bottomLeft[2] && file[19] == bottomLeft[1] && file[20] == bottomLeft[0]);
        const Decoded d = decode(file);
        CHECK(d.w == w && d.h == h && d.rgb == image);
    }
    {
        CHECK(encodeBmp(w, h, image.data(), file));
        const size_t stride = (size_t(w) * 3 + 3) & ~size_t(3);
        CHECK(stride == 112);
        CHECK(file.size() == 54 + stride * h);
        CHECK(file[0] == 'B' && file[1] == 'M' && le32(file, 2) == file.size() && le32(file, 10) == 54);
        CHECK(le32(file, 18) == uint32_t(w) && le32(file, 22) == uint32_t(h) && file[28] == 24);
        const Decoded d = decode(file);
        CHECK(d.w == w && d.h == h && d.rgb == image);
    }
    for (int level : {0, 1, 7, 9}) {
        CHECK(encodePng(w, h, image.data(), level, file));
        CHECK(file.size() > 33 && file[0] == 0x89 && file[1] == 'P' && file[2] == 'N' && file[3] == 'G');
        const Decoded d = decode(file);
        CHECK(d.w == w && d.h == h && d.rgb == image);
    }
    {
        // a flat picture compresses: the matches work, and long runs past 258 bytes
        const std::vector<uint8_t> flat(size_t(320) * 200 * 3, 90);
        CHECK(encodePng(320, 200, flat.data(), 7, file));
        CHECK(file.size() < 2000);
        const Decoded d = decode(file);
        CHECK(d.w == 320 && d.h == 200 && d.rgb == flat);
        std::vector<uint8_t> stored;
        CHECK(encodePng(320, 200, flat.data(), 0, stored));
        CHECK(stored.size() > flat.size()); // level 0 doesn't compress
        CHECK(decode(stored).rgb == flat);
        // random bytes: no matches to find, literals throughout
        std::vector<uint8_t> noise(size_t(64) * 64 * 3);
        uint32_t seed = 12345;
        for (uint8_t &b : noise) {
            seed = seed * 1103515245u + 12345u;
            b = uint8_t(seed >> 16);
        }
        CHECK(encodePng(64, 64, noise.data(), 9, file));
        CHECK(decode(file).rgb == noise);
    }
    {
        int maxDiff = 0;
        double meanDiff = 0.0;
        CHECK(encodeJpg(w, h, image.data(), 95, file));
        CHECK(file[0] == 0xFF && file[1] == 0xD8 && file[file.size() - 2] == 0xFF && file[file.size() - 1] == 0xD9);
        Decoded d = decode(file);
        CHECK(d.w == w && d.h == h);
        compare(d.rgb, image, maxDiff, meanDiff);
        CHECK(meanDiff < 4.0);
        std::printf("jpg q95: mean %.2f, max %d\n", meanDiff, maxDiff);

        std::vector<uint8_t> low;
        CHECK(encodeJpg(w, h, image.data(), 10, low));
        CHECK(low.size() < file.size());
        d = decode(low);
        compare(d.rgb, image, maxDiff, meanDiff);
        CHECK(d.w == w && d.h == h && meanDiff < 30.0);

        CHECK(encodeJpg(w, h, image.data(), 100, file));
        d = decode(file);
        compare(d.rgb, image, maxDiff, meanDiff);
        CHECK(meanDiff < 2.0);
        // saturated colours stay put (range of the DC and AC categories)
        std::vector<uint8_t> blocks(size_t(16) * 16 * 3);
        for (size_t i = 0; i < blocks.size(); i += 3) {
            const bool on = ((i / 3) % 16 < 8) != ((i / 3) / 16 < 8);
            blocks[i] = on ? 255 : 0;
            blocks[i + 1] = on ? 0 : 255;
            blocks[i + 2] = on ? 255 : 0;
        }
        CHECK(encodeJpg(16, 16, blocks.data(), 100, file));
        d = decode(file);
        compare(d.rgb, blocks, maxDiff, meanDiff);
        CHECK(d.w == 16 && d.h == 16 && meanDiff < 3.0);
        // a 1x1 picture
        const uint8_t one[3] = {200, 100, 50};
        CHECK(encodeJpg(1, 1, one, 90, file));
        d = decode(file);
        CHECK(d.w == 1 && d.h == 1 && std::abs(int(d.rgb[0]) - 200) < 6 && std::abs(int(d.rgb[1]) - 100) < 6);
    }
    CHECK(!encodeTga(0, 4, image.data(), file));
    CHECK(!encodeBmp(4, 4, nullptr, file));
    CHECK(!encodeTga(70000, 1, image.data(), file));
    {
        ScreenshotRequest r;
        r.format = ScreenshotFormat::Png;
        r.quality = 85; // no level given: 7
        std::vector<uint8_t> seven;
        CHECK(encodeScreenshot(r, w, h, image.data(), file) && encodePng(w, h, image.data(), 7, seven) &&
              file == seven);
    }

    // ---- the command
    const fs::path root = fs::temp_directory_path() /
                          ("ekron-screenshot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    {
        EngineConfig cfg;
        cfg.backend = BackendPreference::Native;
        cfg.searchRoots = {root.string()};
        cfg.writableRoot = root.string();
        std::vector<std::string> printed;
        cfg.printSink = [&](int, const std::string &text) { printed.push_back(text); };
        Engine engine;
        CHECK(engine.init(cfg));
        Screenshots shots;
        shots.registerCommand(engine);
        CHECK(!shots.pending());

        engine.exec("screenshot gif\n");
        CHECK(!shots.pending());
        CHECK(!printed.empty() && printed.back().find("second argument to 'screenshot'") != std::string::npos);

        // nothing is written until the host hands over a frame
        const uint8_t glPixels[] = {10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255};
        CHECK(!shots.deliver(engine, 2, 2, glPixels, 4, false, true));

        engine.exec("screenshot\n");
        CHECK(shots.pending());
        CHECK(shots.deliver(engine, 2, 2, glPixels, 4, false, true));
        CHECK(!shots.pending());
        CHECK(printed.back() == "Wrote q2_0000.tga" && shots.lastMessage() == "Wrote q2_0000.tga");
        const fs::path first = root / kScreenshotDir / "q2_0000.tga";
        CHECK(fs::is_regular_file(first));
        {
            std::ifstream in(first, std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const Decoded d = decode(bytes);
            // GL's bottom row comes out at the bottom
            CHECK(d.w == 2 && d.h == 2 &&
                  (d.rgb == std::vector<uint8_t>{70, 80, 90, 100, 110, 120, 10, 20, 30, 40, 50, 60}));
        }

        engine.exec("screenshot\n");
        CHECK(shots.deliver(engine, 2, 2, glPixels, 4, false, true) && printed.back() == "Wrote q2_0001.tga");
        // each format counts on its own; the first free number is used
        engine.exec("screenshot PNG 0\n");
        CHECK(shots.deliver(engine, 2, 2, glPixels, 4, false, true) && printed.back() == "Wrote q2_0000.png");
        fs::remove(first);
        engine.exec("screenshot tga\n");
        CHECK(shots.deliver(engine, 2, 2, glPixels, 4, false, true) && printed.back() == "Wrote q2_0000.tga");
        CHECK(nextScreenshotIndex((root / kScreenshotDir).string(), ScreenshotFormat::Tga) == 2);
        engine.exec("screenshot jpg 90\n");
        CHECK(shots.deliver(engine, 2, 2, glPixels, 4, false, true) && printed.back() == "Wrote q2_0000.jpg");
        engine.exec("screenshot bmp\n");
        CHECK(shots.deliver(engine, 2, 2, glPixels, 4, false, true) && printed.back() == "Wrote q2_0000.bmp");
        CHECK(fs::is_regular_file(root / kScreenshotDir / "q2_0000.bmp"));

        // a frame that couldn't be read
        engine.exec("screenshot\n");
        shots.cancel(engine, "couldn't read the frame");
        CHECK(!shots.pending() && printed.back() == "screenshot: couldn't read the frame");
        shots.cancel(engine, "again"); // nothing pending: quiet
        CHECK(printed.back() == "screenshot: couldn't read the frame");
        // an empty frame isn't written
        engine.exec("screenshot\n");
        CHECK(!shots.deliver(engine, 0, 0, glPixels, 4, false, true) &&
              printed.back() == "SCR_ScreenShot_f: Couldn't write q2_0002.tga");
        engine.shutdown();
    }
    {
        // without a writable folder the command refuses at once
        EngineConfig cfg;
        cfg.backend = BackendPreference::Native;
        cfg.searchRoots = {root.string()};
        std::vector<std::string> printed;
        cfg.printSink = [&](int, const std::string &text) { printed.push_back(text); };
        Engine engine;
        CHECK(engine.init(cfg));
        Screenshots shots;
        shots.registerCommand(engine);
        engine.exec("screenshot\n");
        CHECK(!shots.pending() && !printed.empty() &&
              printed.back() == "screenshot: no writable folder for screenshots");
        engine.shutdown();
    }
    std::error_code ec;
    fs::remove_all(root, ec);

    std::printf("screenshot: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
