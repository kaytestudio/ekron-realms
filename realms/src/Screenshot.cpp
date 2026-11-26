#include "realms/Screenshot.h"

#include "realms/Engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace realms {

namespace {

constexpr const char *kFormatNames[] = {"tga", "bmp", "png", "jpg"};

bool equalsNoCase(const std::string &a, const char *b) {
    const size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = a[i];
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (c != b[i]) return false;
    }
    return true;
}

void put16le(std::vector<uint8_t> &out, uint32_t v) {
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
}
void put32le(std::vector<uint8_t> &out, uint32_t v) {
    put16le(out, v & 0xFFFFu);
    put16le(out, v >> 16);
}
void put32be(std::vector<uint8_t> &out, uint32_t v) {
    out.push_back(uint8_t(v >> 24));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

bool sizeOk(int width, int height, const uint8_t *rgb) {
    return rgb && width > 0 && height > 0 && uint64_t(width) * uint64_t(height) <= (uint64_t(1) << 28);
}

// ---- deflate (RFC 1951): stored blocks for level 0, else one block with the
// fixed Huffman codes and LZ77 matches from a hash chain.

class BitWriter {
public:
    explicit BitWriter(std::vector<uint8_t> &out) : out_(out) {}
    // `count` bits of `value`, least significant first.
    void bits(uint32_t value, int count) {
        acc_ |= uint64_t(value) << fill_;
        fill_ += count;
        while (fill_ >= 8) {
            out_.push_back(uint8_t(acc_));
            acc_ >>= 8;
            fill_ -= 8;
        }
    }
    // A Huffman code, which deflate stores most significant bit first.
    void code(uint32_t code, int length) {
        uint32_t reversed = 0;
        for (int i = 0; i < length; ++i) reversed |= ((code >> i) & 1u) << (length - 1 - i);
        bits(reversed, length);
    }
    void flush() {
        if (fill_ > 0) out_.push_back(uint8_t(acc_));
        acc_ = 0;
        fill_ = 0;
    }

private:
    std::vector<uint8_t> &out_;
    uint64_t acc_ = 0;
    int fill_ = 0;
};

constexpr uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                      2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void fixedLiteral(BitWriter &w, int symbol) {
    if (symbol < 144) w.code(uint32_t(0x30 + symbol), 8);
    else if (symbol < 256) w.code(uint32_t(0x190 + symbol - 144), 9);
    else if (symbol < 280) w.code(uint32_t(symbol - 256), 7);
    else w.code(uint32_t(0xC0 + symbol - 280), 8);
}

void fixedMatch(BitWriter &w, int length, int distance) {
    int l = 28;
    while (kLengthBase[l] > length) --l;
    fixedLiteral(w, 257 + l);
    if (kLengthExtra[l]) w.bits(uint32_t(length - kLengthBase[l]), kLengthExtra[l]);
    int d = 29;
    while (kDistBase[d] > distance) --d;
    w.code(uint32_t(d), 5);
    if (kDistExtra[d]) w.bits(uint32_t(distance - kDistBase[d]), kDistExtra[d]);
}

void deflate(const std::vector<uint8_t> &in, int level, std::vector<uint8_t> &out) {
    if (level <= 0) {
        size_t pos = 0;
        do {
            const size_t n = std::min<size_t>(65535, in.size() - pos);
            const bool last = pos + n == in.size();
            out.push_back(last ? 1 : 0); // BFINAL, BTYPE 00, byte aligned
            put16le(out, uint32_t(n));
            put16le(out, uint32_t(~n & 0xFFFFu));
            out.insert(out.end(), in.begin() + std::ptrdiff_t(pos), in.begin() + std::ptrdiff_t(pos + n));
            pos += n;
        } while (pos < in.size());
        return;
    }
    static constexpr int kChain[10] = {0, 4, 6, 8, 16, 24, 32, 64, 128, 256};
    const int maxChain = kChain[std::min(level, 9)];
    constexpr int kWindow = 32768, kHashBits = 15, kMinMatch = 3, kMaxMatch = 258;
    std::vector<int32_t> head(size_t(1) << kHashBits, -1), prev(kWindow, -1);
    auto hashAt = [&](size_t i) {
        const uint32_t v = uint32_t(in[i]) | uint32_t(in[i + 1]) << 8 | uint32_t(in[i + 2]) << 16;
        return (v * 2654435761u) >> (32 - kHashBits);
    };
    auto insert = [&](size_t i) {
        if (i + kMinMatch > in.size()) return;
        const uint32_t h = hashAt(i);
        prev[i % kWindow] = head[h];
        head[h] = int32_t(i);
    };

    BitWriter w(out);
    w.bits(1, 1); // BFINAL
    w.bits(1, 2); // BTYPE 01: fixed Huffman codes
    size_t i = 0;
    while (i < in.size()) {
        int bestLen = 0, bestDist = 0;
        if (i + kMinMatch <= in.size()) {
            const size_t limit = std::min<size_t>(kMaxMatch, in.size() - i);
            int32_t cand = head[hashAt(i)];
            for (int chain = 0; cand >= 0 && chain < maxChain; ++chain) {
                const size_t dist = i - size_t(cand);
                if (dist > size_t(kWindow) - 1) break;
                if (in[size_t(cand) + size_t(bestLen)] == in[i + size_t(bestLen)] || bestLen == 0) {
                    size_t len = 0;
                    while (len < limit && in[size_t(cand) + len] == in[i + len]) ++len;
                    if (int(len) > bestLen) {
                        bestLen = int(len);
                        bestDist = int(dist);
                        if (len == limit) break;
                    }
                }
                const int32_t next = prev[size_t(cand) % kWindow];
                if (next >= cand) break; // overwritten slot: older than the window
                cand = next;
            }
        }
        if (bestLen >= kMinMatch) {
            fixedMatch(w, bestLen, bestDist);
            for (int k = 0; k < bestLen; ++k) insert(i + size_t(k));
            i += size_t(bestLen);
        } else {
            fixedLiteral(w, in[i]);
            insert(i);
            ++i;
        }
    }
    fixedLiteral(w, 256); // end of block
    w.flush();
}

void pngChunk(std::vector<uint8_t> &out, const char type[4], const std::vector<uint8_t> &data) {
    put32be(out, uint32_t(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32be(out, screenshotCrc32(out.data() + start, out.size() - start));
}

uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return uint8_t(a);
    return uint8_t(pb <= pc ? b : c);
}

// ---- baseline JPEG (ITU T.81 with the Annex K tables), 4:4:4.

constexpr uint8_t kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
constexpr uint8_t kLumaQuant[64] = {16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
                                    14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
                                    18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
                                    49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
constexpr uint8_t kChromaQuant[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
                                      24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                                      99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                      99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

constexpr uint8_t kDcLumaBits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t kDcChromaBits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
constexpr uint8_t kDcValues[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr uint8_t kAcLumaBits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
constexpr uint8_t kAcLumaValues[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14,
    0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09,
    0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
    0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65,
    0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
    0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
    0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca,
    0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
    0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
constexpr uint8_t kAcChromaBits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
constexpr uint8_t kAcChromaValues[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32,
    0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16,
    0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64,
    0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86,
    0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8,
    0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
    0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

struct HuffTable {
    std::array<uint16_t, 256> code{};
    std::array<uint8_t, 256> size{};
};

HuffTable buildHuff(const uint8_t bits[16], const uint8_t *values) {
    HuffTable t;
    uint32_t code = 0;
    size_t k = 0;
    for (int len = 1; len <= 16; ++len) {
        for (int n = 0; n < bits[len - 1]; ++n, ++k) {
            t.code[values[k]] = uint16_t(code++);
            t.size[values[k]] = uint8_t(len);
        }
        code <<= 1;
    }
    return t;
}

class JpegBits {
public:
    explicit JpegBits(std::vector<uint8_t> &out) : out_(out) {}
    void put(uint32_t value, int count) { // most significant bit first
        if (count <= 0) return;
        acc_ = (acc_ << count) | (value & ((1u << count) - 1u));
        fill_ += count;
        while (fill_ >= 8) {
            const uint8_t byte = uint8_t(acc_ >> (fill_ - 8));
            out_.push_back(byte);
            if (byte == 0xFF) out_.push_back(0); // byte stuffing
            fill_ -= 8;
        }
        acc_ &= (1u << fill_) - 1u;
    }
    void flush() { // pad with 1 bits
        if (fill_ > 0) put(0x7Fu, 8 - fill_);
    }

private:
    std::vector<uint8_t> &out_;
    uint32_t acc_ = 0;
    int fill_ = 0;
};

int bitLength(int v) {
    int n = 0;
    for (v = std::abs(v); v; v >>= 1) ++n;
    return n;
}

void putMarkerSegment(std::vector<uint8_t> &out, uint8_t marker, const std::vector<uint8_t> &data) {
    out.push_back(0xFF);
    out.push_back(marker);
    out.push_back(uint8_t((data.size() + 2) >> 8));
    out.push_back(uint8_t(data.size() + 2));
    out.insert(out.end(), data.begin(), data.end());
}

void encodeBlock(JpegBits &bits, const float block[64], const float divisors[64], int &prevDc, const HuffTable &dc,
                 const HuffTable &ac) {
    // 2D DCT-II: F(u,v) = 1/4 C(u) C(v) sum f(x,y) cos((2x+1)u pi/16) cos((2y+1)v pi/16)
    static const std::array<float, 64> cosTable = [] {
        std::array<float, 64> t{};
        for (int x = 0; x < 8; ++x)
            for (int u = 0; u < 8; ++u)
                t[size_t(x * 8 + u)] = float(std::cos((2.0 * x + 1.0) * u * 3.14159265358979323846 / 16.0));
        return t;
    }();
    float rows[64];
    for (int y = 0; y < 8; ++y)
        for (int u = 0; u < 8; ++u) {
            float s = 0.0f;
            for (int x = 0; x < 8; ++x) s += block[y * 8 + x] * cosTable[size_t(x * 8 + u)];
            rows[y * 8 + u] = s;
        }
    int quant[64];
    for (int v = 0; v < 8; ++v)
        for (int u = 0; u < 8; ++u) {
            float s = 0.0f;
            for (int y = 0; y < 8; ++y) s += rows[y * 8 + u] * cosTable[size_t(y * 8 + v)];
            const float cu = u ? 1.0f : 0.70710678f, cv = v ? 1.0f : 0.70710678f;
            const float f = 0.25f * cu * cv * s;
            quant[v * 8 + u] = int(std::lround(f / divisors[v * 8 + u]));
        }

    const int dcValue = quant[0];
    const int diff = dcValue - prevDc;
    prevDc = dcValue;
    const int dcSize = bitLength(diff);
    bits.put(dc.code[size_t(dcSize)], dc.size[size_t(dcSize)]);
    bits.put(uint32_t(diff < 0 ? diff - 1 : diff), dcSize);

    int run = 0;
    for (int k = 1; k < 64; ++k) {
        const int v = quant[kZigzag[k]];
        if (v == 0) {
            ++run;
            continue;
        }
        while (run > 15) {
            bits.put(ac.code[0xF0], ac.size[0xF0]);
            run -= 16;
        }
        const int size = bitLength(v);
        const int symbol = (run << 4) | size;
        bits.put(ac.code[size_t(symbol)], ac.size[size_t(symbol)]);
        bits.put(uint32_t(v < 0 ? v - 1 : v), size);
        run = 0;
    }
    if (run > 0) bits.put(ac.code[0x00], ac.size[0x00]); // end of block
}

bool writeFile(const fs::path &path, const std::vector<uint8_t> &data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
    f.close();
    return bool(f);
}

} // namespace

const char *screenshotExtension(ScreenshotFormat format) { return kFormatNames[int(format)]; }

bool parseScreenshotArgs(const std::vector<std::string> &args, ScreenshotRequest &out, std::string &error) {
    out = ScreenshotRequest{};
    error.clear();
    if (args.size() > 1) {
        int i = 0;
        for (; i < 4; ++i)
            if (equalsNoCase(args[1], kFormatNames[i])) break;
        if (i == 4) {
            error = "the (optional) second argument to 'screenshot' is the format, one of \"tga\", \"bmp\", \"png\", \"jpg\"";
            return false;
        }
        out.format = ScreenshotFormat(i);
        if (args.size() > 2) {
            const std::string &q = args[2];
            for (char c : q)
                if (c < '0' || c > '9') {
                    error = "The (optional!) third argument to 'screenshot' is jpg quality, a number between 1 and 100\n"
                            "  or png compression level, between 0 and 9!";
                    return false;
                }
            // atoi, without its overflow: anything past 3 digits is clamped anyway
            long value = 0;
            for (char c : q) value = std::min<long>(value * 10 + (c - '0'), 100000);
            out.quality = int(value);
            if (out.format == ScreenshotFormat::Png) out.quality = std::clamp(out.quality, 0, 9);
            else if (out.format == ScreenshotFormat::Jpg) out.quality = std::clamp(out.quality, 1, 100);
        }
    }
    return true;
}

std::string screenshotFileName(int index, ScreenshotFormat format) {
    char name[32];
    std::snprintf(name, sizeof name, "q2_%04d.%s", index, screenshotExtension(format));
    return name;
}

int nextScreenshotIndex(const std::string &dir, ScreenshotFormat format) {
    for (int i = 0; i <= kScreenshotMaxIndex; ++i) {
        std::error_code ec;
        if (!fs::exists(fs::path(dir) / screenshotFileName(i, format), ec) && !ec) return i;
    }
    return -1;
}

std::vector<uint8_t> screenshotRgb(const uint8_t *pixels, int width, int height, int comps, bool bgr, bool bottomUp) {
    if (!pixels || width <= 0 || height <= 0 || (comps != 3 && comps != 4)) return {};
    std::vector<uint8_t> rgb(size_t(width) * size_t(height) * 3);
    for (int y = 0; y < height; ++y) {
        const uint8_t *src = pixels + size_t(bottomUp ? height - 1 - y : y) * size_t(width) * size_t(comps);
        uint8_t *dst = rgb.data() + size_t(y) * size_t(width) * 3;
        for (int x = 0; x < width; ++x, src += comps, dst += 3) {
            dst[0] = bgr ? src[2] : src[0];
            dst[1] = src[1];
            dst[2] = bgr ? src[0] : src[2];
        }
    }
    return rgb;
}

bool encodeTga(int width, int height, const uint8_t *rgb, std::vector<uint8_t> &out) {
    out.clear();
    if (!sizeOk(width, height, rgb) || width > 0xFFFF || height > 0xFFFF) return false;
    // uncompressed true colour, origin at the bottom left (rows bottom up)
    const uint8_t header[12] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    out.insert(out.end(), header, header + 12);
    put16le(out, uint32_t(width));
    put16le(out, uint32_t(height));
    out.push_back(24);
    out.push_back(0);
    out.reserve(out.size() + size_t(width) * size_t(height) * 3);
    for (int y = height - 1; y >= 0; --y) {
        const uint8_t *p = rgb + size_t(y) * size_t(width) * 3;
        for (int x = 0; x < width; ++x, p += 3) {
            out.push_back(p[2]);
            out.push_back(p[1]);
            out.push_back(p[0]);
        }
    }
    return true;
}

bool encodeBmp(int width, int height, const uint8_t *rgb, std::vector<uint8_t> &out) {
    out.clear();
    if (!sizeOk(width, height, rgb)) return false;
    const size_t stride = (size_t(width) * 3 + 3) & ~size_t(3);
    const uint64_t imageSize = uint64_t(stride) * uint64_t(height);
    if (imageSize + 54 > 0xFFFFFFFFull) return false;
    out.push_back('B');
    out.push_back('M');
    put32le(out, uint32_t(54 + imageSize));
    put32le(out, 0);
    put32le(out, 54);
    put32le(out, 40); // BITMAPINFOHEADER
    put32le(out, uint32_t(width));
    put32le(out, uint32_t(height)); // positive: rows bottom up
    put16le(out, 1);
    put16le(out, 24);
    put32le(out, 0); // BI_RGB
    put32le(out, uint32_t(imageSize));
    put32le(out, 2835); // 72 dpi
    put32le(out, 2835);
    put32le(out, 0);
    put32le(out, 0);
    out.reserve(out.size() + size_t(imageSize));
    for (int y = height - 1; y >= 0; --y) {
        const uint8_t *p = rgb + size_t(y) * size_t(width) * 3;
        for (int x = 0; x < width; ++x, p += 3) {
            out.push_back(p[2]);
            out.push_back(p[1]);
            out.push_back(p[0]);
        }
        for (size_t pad = size_t(width) * 3; pad < stride; ++pad) out.push_back(0);
    }
    return true;
}

uint32_t screenshotCrc32(const uint8_t *data, size_t size, uint32_t crc) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

uint32_t screenshotAdler32(const uint8_t *data, size_t size, uint32_t adler) {
    uint32_t a = adler & 0xFFFFu, b = adler >> 16;
    for (size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return b << 16 | a;
}

bool encodePng(int width, int height, const uint8_t *rgb, int level, std::vector<uint8_t> &out) {
    out.clear();
    if (!sizeOk(width, height, rgb)) return false;
    level = std::clamp(level, 0, 9);
    // Filtered scanlines: none at level 0, otherwise the filter with the
    // smallest sum of absolute differences per row (the usual heuristic).
    const size_t rowBytes = size_t(width) * 3;
    std::vector<uint8_t> raw;
    raw.reserve((rowBytes + 1) * size_t(height));
    std::vector<uint8_t> candidate(rowBytes), best(rowBytes);
    for (int y = 0; y < height; ++y) {
        const uint8_t *row = rgb + size_t(y) * rowBytes;
        const uint8_t *up = y > 0 ? row - rowBytes : nullptr;
        if (level == 0) {
            raw.push_back(0);
            raw.insert(raw.end(), row, row + rowBytes);
            continue;
        }
        uint64_t bestScore = ~uint64_t(0);
        int bestFilter = 0;
        for (int filter = 0; filter < 5; ++filter) {
            uint64_t score = 0;
            for (size_t i = 0; i < rowBytes; ++i) {
                const int a = i >= 3 ? row[i - 3] : 0, b = up ? up[i] : 0, c = (up && i >= 3) ? up[i - 3] : 0;
                int predicted = 0;
                switch (filter) {
                case 1: predicted = a; break;
                case 2: predicted = b; break;
                case 3: predicted = (a + b) / 2; break;
                case 4: predicted = paeth(a, b, c); break;
                default: break;
                }
                const uint8_t v = uint8_t(row[i] - predicted);
                candidate[i] = v;
                score += uint64_t(std::abs(int(int8_t(v))));
            }
            if (score < bestScore) {
                bestScore = score;
                bestFilter = filter;
                best.swap(candidate);
            }
        }
        raw.push_back(uint8_t(bestFilter));
        raw.insert(raw.end(), best.begin(), best.end());
    }

    std::vector<uint8_t> zlib;
    zlib.push_back(0x78); // deflate, 32K window
    zlib.push_back(level == 0 ? 0x01 : (level < 6 ? 0x5E : (level == 6 ? 0x9C : 0xDA)));
    deflate(raw, level, zlib);
    put32be(zlib, screenshotAdler32(raw.data(), raw.size()));

    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    out.insert(out.end(), signature, signature + 8);
    std::vector<uint8_t> ihdr;
    put32be(ihdr, uint32_t(width));
    put32be(ihdr, uint32_t(height));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(2); // RGB
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    pngChunk(out, "IHDR", ihdr);
    pngChunk(out, "IDAT", zlib);
    pngChunk(out, "IEND", {});
    return true;
}

bool encodeJpg(int width, int height, const uint8_t *rgb, int quality, std::vector<uint8_t> &out) {
    out.clear();
    if (!sizeOk(width, height, rgb) || width > 0xFFFF || height > 0xFFFF) return false;
    quality = std::clamp(quality, 1, 100);
    const int scale = quality < 50 ? 5000 / quality : 200 - quality * 2; // libjpeg's scaling
    uint8_t lumaQ[64], chromaQ[64];
    float lumaDiv[64], chromaDiv[64];
    for (int i = 0; i < 64; ++i) {
        lumaQ[i] = uint8_t(std::clamp((kLumaQuant[i] * scale + 50) / 100, 1, 255));
        chromaQ[i] = uint8_t(std::clamp((kChromaQuant[i] * scale + 50) / 100, 1, 255));
        lumaDiv[i] = float(lumaQ[i]);
        chromaDiv[i] = float(chromaQ[i]);
    }

    out.push_back(0xFF);
    out.push_back(0xD8); // SOI
    putMarkerSegment(out, 0xE0, {'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0}); // JFIF 1.01, 1:1
    std::vector<uint8_t> dqt;
    dqt.push_back(0);
    for (int k = 0; k < 64; ++k) dqt.push_back(lumaQ[kZigzag[k]]);
    dqt.push_back(1);
    for (int k = 0; k < 64; ++k) dqt.push_back(chromaQ[kZigzag[k]]);
    putMarkerSegment(out, 0xDB, dqt);
    putMarkerSegment(out, 0xC0,
                     {8, uint8_t(height >> 8), uint8_t(height), uint8_t(width >> 8), uint8_t(width), 3, 1, 0x11, 0, 2,
                      0x11, 1, 3, 0x11, 1});
    std::vector<uint8_t> dht;
    auto addTable = [&](uint8_t classId, const uint8_t bits[16], const uint8_t *values) {
        dht.push_back(classId);
        int count = 0;
        for (int i = 0; i < 16; ++i) {
            dht.push_back(bits[i]);
            count += bits[i];
        }
        dht.insert(dht.end(), values, values + count);
    };
    addTable(0x00, kDcLumaBits, kDcValues);
    addTable(0x10, kAcLumaBits, kAcLumaValues);
    addTable(0x01, kDcChromaBits, kDcValues);
    addTable(0x11, kAcChromaBits, kAcChromaValues);
    putMarkerSegment(out, 0xC4, dht);
    putMarkerSegment(out, 0xDA, {3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0});

    static const HuffTable dcLuma = buildHuff(kDcLumaBits, kDcValues);
    static const HuffTable acLuma = buildHuff(kAcLumaBits, kAcLumaValues);
    static const HuffTable dcChroma = buildHuff(kDcChromaBits, kDcValues);
    static const HuffTable acChroma = buildHuff(kAcChromaBits, kAcChromaValues);

    JpegBits bits(out);
    int prevY = 0, prevCb = 0, prevCr = 0;
    float yBlock[64], cbBlock[64], crBlock[64];
    for (int by = 0; by < height; by += 8)
        for (int bx = 0; bx < width; bx += 8) {
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    // edge blocks repeat the last row / column
                    const int sx = std::min(bx + x, width - 1), sy = std::min(by + y, height - 1);
                    const uint8_t *p = rgb + (size_t(sy) * size_t(width) + size_t(sx)) * 3;
                    const float r = p[0], g = p[1], b = p[2];
                    yBlock[y * 8 + x] = 0.299f * r + 0.587f * g + 0.114f * b - 128.0f;
                    cbBlock[y * 8 + x] = -0.168736f * r - 0.331264f * g + 0.5f * b;
                    crBlock[y * 8 + x] = 0.5f * r - 0.418688f * g - 0.081312f * b;
                }
            encodeBlock(bits, yBlock, lumaDiv, prevY, dcLuma, acLuma);
            encodeBlock(bits, cbBlock, chromaDiv, prevCb, dcChroma, acChroma);
            encodeBlock(bits, crBlock, chromaDiv, prevCr, dcChroma, acChroma);
        }
    bits.flush();
    out.push_back(0xFF);
    out.push_back(0xD9); // EOI
    return true;
}

bool encodeScreenshot(const ScreenshotRequest &request, int width, int height, const uint8_t *rgb,
                      std::vector<uint8_t> &out) {
    switch (request.format) {
    case ScreenshotFormat::Tga: return encodeTga(width, height, rgb, out);
    case ScreenshotFormat::Bmp: return encodeBmp(width, height, rgb, out);
    case ScreenshotFormat::Png: return encodePng(width, height, rgb, request.quality < 10 ? request.quality : 7, out);
    case ScreenshotFormat::Jpg: return encodeJpg(width, height, rgb, request.quality, out);
    }
    return false;
}

void Screenshots::registerCommand(Engine &engine) {
    Engine *e = &engine;
    engine.addCommand("screenshot", [this, e](const CommandArgs &args) { request(*e, args); });
}

bool Screenshots::request(Engine &engine, const std::vector<std::string> &args) {
    ScreenshotRequest parsed;
    std::string error;
    if (!parseScreenshotArgs(args, parsed, error)) {
        engine.print(error);
        return false;
    }
    if (engine.writableRoot().empty()) {
        engine.print("screenshot: no writable folder for screenshots", REALMS_PRINT_WARNING);
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    request_ = parsed;
    pending_.store(true);
    return true;
}

void Screenshots::cancel(Engine &engine, const std::string &why) {
    if (!pending_.exchange(false)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    lastMessage_ = "screenshot: " + why;
    engine.print(lastMessage_, REALMS_PRINT_WARNING);
}

bool Screenshots::deliver(Engine &engine, int width, int height, const uint8_t *pixels, int comps, bool bgr,
                          bool bottomUp) {
    if (!pending_.exchange(false)) return false;
    ScreenshotRequest req;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        req = request_;
    }
    auto report = [&](const std::string &line, int level) {
        engine.print(line, level);
        std::lock_guard<std::mutex> lock(mutex_);
        lastMessage_ = line;
    };
    if (engine.writableRoot().empty()) {
        report("screenshot: no writable folder for screenshots", REALMS_PRINT_WARNING);
        return false;
    }
    const fs::path dir = fs::path(engine.writableRoot()) / kScreenshotDir;
    std::error_code ec;
    fs::create_directories(dir, ec); // FS_InitFilesystem makes it in yquake2
    const int index = nextScreenshotIndex(dir.string(), req.format);
    if (index < 0) {
        report("SCR_ScreenShot_f: Couldn't create a file", REALMS_PRINT_WARNING);
        return false;
    }
    const std::string name = screenshotFileName(index, req.format);
    const std::vector<uint8_t> rgb = screenshotRgb(pixels, width, height, comps, bgr, bottomUp);
    std::vector<uint8_t> file;
    if (rgb.empty() || !encodeScreenshot(req, width, height, rgb.data(), file) || !writeFile(dir / name, file)) {
        report("SCR_ScreenShot_f: Couldn't write " + name, REALMS_PRINT_WARNING);
        return false;
    }
    report("Wrote " + name, REALMS_PRINT_INFO);
    return true;
}

std::string Screenshots::lastMessage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastMessage_;
}

} // namespace realms
