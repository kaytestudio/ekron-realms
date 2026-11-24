// Realms Engine - 8-bit PCX images (see Image.h).
#include "realms/Image.h"

#include <cstdint>

namespace realms {
namespace {

uint16_t rdUShort(const unsigned char *p) { return uint16_t(uint16_t(p[0]) | uint16_t(p[1]) << 8); }

} // namespace

bool decodePcx(const unsigned char *data, size_t size, RgbaImage &out, std::string &error, int transparentIndex) {
    out = RgbaImage{};
    constexpr size_t kPcxHeader = 128, kPalette = 769;
    if (!data || size < kPcxHeader + kPalette) {
        error = "file too small";
        return false;
    }
    const int manufacturer = data[0], version = data[1], encoding = data[2], bpp = data[3];
    const int xmin = rdUShort(data + 4), ymin = rdUShort(data + 6);
    const int xmax = rdUShort(data + 8), ymax = rdUShort(data + 10);
    const int planes = data[65];
    const int bytesPerLine = rdUShort(data + 66);
    if (manufacturer != 0x0a || version != 5 || encoding != 1 || bpp != 8 || planes != 1) {
        error = "not an 8-bit RLE PCX";
        return false;
    }
    if (xmax < xmin || ymax < ymin || xmax - xmin >= 4096 || ymax - ymin >= 4096) {
        error = "bad image size";
        return false;
    }
    const int w = xmax - xmin + 1, h = ymax - ymin + 1;
    if (bytesPerLine < w) {
        error = "bad bytes per line";
        return false;
    }
    const unsigned char *palette = data + size - 768;
    if (palette[-1] != 0x0c) {
        error = "no 256-colour palette";
        return false;
    }
    std::vector<unsigned char> indices(size_t(w) * size_t(h));
    const unsigned char *p = data + kPcxHeader;
    const unsigned char *end = data + size - kPalette;
    for (int y = 0; y < h; y++) {
        int x = 0;
        while (x < bytesPerLine) {
            if (p >= end) {
                error = "truncated image data";
                return false;
            }
            int b = *p++, run = 1;
            if ((b & 0xc0) == 0xc0) {
                run = b & 0x3f;
                if (p >= end) {
                    error = "truncated image data";
                    return false;
                }
                b = *p++;
            }
            // a run may cross the end of the line; the excess is padding
            for (int r = 0; r < run && x < bytesPerLine; r++, x++)
                if (x < w) indices[size_t(y) * size_t(w) + size_t(x)] = uint8_t(b);
        }
    }
    out.width = w;
    out.height = h;
    out.rgba.resize(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < indices.size(); i++) {
        const unsigned char idx = indices[i];
        out.rgba[i * 4] = palette[idx * 3];
        out.rgba[i * 4 + 1] = palette[idx * 3 + 1];
        out.rgba[i * 4 + 2] = palette[idx * 3 + 2];
        out.rgba[i * 4 + 3] = (transparentIndex >= 0 && idx == transparentIndex) ? 0 : 255;
    }
    error.clear();
    return true;
}

} // namespace realms
