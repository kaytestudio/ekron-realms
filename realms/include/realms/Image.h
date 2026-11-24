// RGBA images and Quake 2's 8-bit PCX format (skins, pics, sky faces).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace realms {

struct RgbaImage {
    int width = 0, height = 0;
    std::vector<unsigned char> rgba; // width * height * 4, top row first
};

// 8-bit, 1-plane RLE PCX with a 256-colour palette (0x0C + 768 bytes at the
// end). `transparentIndex` >= 0 gets alpha 0 (255 for Quake 2 pics).
bool decodePcx(const unsigned char *data, size_t size, RgbaImage &out, std::string &error,
               int transparentIndex = -1);
inline bool decodePcx(const std::vector<unsigned char> &data, RgbaImage &out, std::string &error,
                      int transparentIndex = -1) {
    return decodePcx(data.data(), data.size(), out, error, transparentIndex);
}

} // namespace realms
