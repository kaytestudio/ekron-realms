// Realms Engine - the network channel (see NetChannel.h).
#include "realms/NetChannel.h"

#include <algorithm>
#include <cstring>

namespace realms {

namespace {

constexpr uint32_t kConnectionless = 0xffffffffu;

void putU16(std::vector<uint8_t> &b, uint16_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void putU32(std::vector<uint8_t> &b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> (8 * i)));
}
void putVar(std::vector<uint8_t> &b, uint64_t v) {
    while (v >= 0x80) {
        b.push_back(uint8_t(v | 0x80));
        v >>= 7;
    }
    b.push_back(uint8_t(v));
}
uint16_t getU16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t getU32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

struct Reader {
    const uint8_t *p, *end;
    bool ok = true;
    uint64_t var() {
        uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (p >= end) break;
            const uint8_t c = *p++;
            v |= uint64_t(c & 0x7f) << shift;
            if (!(c & 0x80)) return v;
        }
        ok = false;
        return 0;
    }
};

} // namespace

void NetChannel::reset(uint16_t qport) { *this = NetChannel(qport); }

bool NetChannel::sendReliable(std::vector<uint8_t> message) {
    if (overflowed_) return false;
    if (pendingBytes_ + message.size() > kMaxReliableBytes) {
        overflowed_ = true;
        return false;
    }
    pendingBytes_ += message.size();
    pending_.push_back(std::move(message));
    return true;
}

void NetChannel::transmit(const std::vector<uint8_t> &unreliable, std::vector<std::vector<uint8_t>> &datagrams,
                          double now) {
    datagrams.clear();
    outgoingSequence_++;
    lastSent_ = now;
    std::vector<uint8_t> msg;
    msg.reserve(16 + pendingBytes_ + unreliable.size());
    putVar(msg, reliableReceived_);
    putVar(msg, pending_.size());
    if (!pending_.empty()) {
        putVar(msg, pendingFirst_);
        for (const auto &r : pending_) {
            putVar(msg, r.size());
            msg.insert(msg.end(), r.begin(), r.end());
        }
    }
    if (msg.size() > kMaxMessageBytes) {
        // can't happen below kMaxReliableBytes; send the acks alone
        msg.clear();
        putVar(msg, reliableReceived_);
        putVar(msg, 0);
    }
    if (msg.size() + unreliable.size() <= kMaxMessageBytes) msg.insert(msg.end(), unreliable.begin(), unreliable.end());
    const size_t count = std::max<size_t>(1, (msg.size() + kFragmentBytes - 1) / kFragmentBytes);
    for (size_t i = 0; i < count; i++) {
        std::vector<uint8_t> d;
        const size_t from = i * kFragmentBytes, to = std::min(msg.size(), from + kFragmentBytes);
        d.reserve(kHeaderBytes + (to - from));
        putU32(d, outgoingSequence_);
        putU32(d, incomingSequence_);
        putU16(d, qport_);
        putU16(d, uint16_t(i));
        putU16(d, uint16_t(count));
        d.insert(d.end(), msg.begin() + std::ptrdiff_t(from), msg.begin() + std::ptrdiff_t(to));
        datagrams.push_back(std::move(d));
    }
}

bool NetChannel::process(const uint8_t *data, size_t size, NetMessage &out, double now) {
    if (!data || size < kHeaderBytes) return false;
    const uint32_t sequence = getU32(data);
    if (sequence == kConnectionless || sequence == 0) return false;
    const uint32_t ack = getU32(data + 4);
    const uint16_t qport = getU16(data + 8);
    const size_t fragment = getU16(data + 10), count = getU16(data + 12);
    if (qport != qport_ || count == 0 || count > kMaxFragments || fragment >= count) return false;
    lastReceived_ = now;
    if (ack > incomingAcknowledged_ && ack <= outgoingSequence_) incomingAcknowledged_ = ack;
    if (sequence <= incomingSequence_) return false; // old or a duplicate

    std::vector<uint8_t> whole;
    if (count == 1) {
        whole.assign(data + kHeaderBytes, data + size);
    } else {
        if (sequence < fragSequence_) return false; // a part of an older message
        if (sequence != fragSequence_ || fragments_.size() != count) {
            fragSequence_ = sequence;
            fragments_.assign(count, std::vector<uint8_t>());
            fragmentsHave_ = 0;
        }
        std::vector<uint8_t> &slot = fragments_[fragment];
        if (!slot.empty() || size == kHeaderBytes) return false;
        slot.assign(data + kHeaderBytes, data + size);
        if (++fragmentsHave_ < count) return false;
        for (const auto &f : fragments_) whole.insert(whole.end(), f.begin(), f.end());
        fragments_.clear();
        fragmentsHave_ = 0;
    }

    // the message: acks, reliable messages, then the unreliable bytes
    Reader in{whole.data(), whole.data() + whole.size()};
    const uint64_t reliableAck = in.var();
    const uint64_t n = in.var();
    uint64_t first = 0;
    std::vector<std::pair<const uint8_t *, size_t>> reliable;
    if (in.ok && n > 0) {
        first = in.var();
        if (n > (whole.size() + 1) || first == 0) in.ok = false;
        for (uint64_t i = 0; i < n && in.ok; i++) {
            const uint64_t len = in.var();
            if (!in.ok || len > uint64_t(in.end - in.p)) {
                in.ok = false;
                break;
            }
            reliable.emplace_back(in.p, size_t(len));
            in.p += len;
        }
    }
    const uint64_t pendingLast = uint64_t(pendingFirst_) + pending_.size() - 1;
    if (!in.ok || reliableAck > pendingLast) return false;
    // sent all the unacknowledged ones: never a gap after the last delivered
    if (n > 0 && (first > uint64_t(reliableReceived_) + 1 || first + n - 1 > UINT32_MAX)) return false;

    while (!pending_.empty() && pendingFirst_ <= reliableAck) {
        pendingBytes_ -= pending_.front().size();
        pending_.pop_front();
        pendingFirst_++;
    }
    out = NetMessage{};
    out.sequence = sequence;
    for (uint64_t i = 0; i < n; i++) {
        if (first + i != uint64_t(reliableReceived_) + 1) continue; // had it
        out.reliable.emplace_back(reliable[size_t(i)].first, reliable[size_t(i)].first + reliable[size_t(i)].second);
        reliableReceived_++;
    }
    out.unreliable.assign(in.p, in.end);
    dropped_ += sequence - incomingSequence_ - 1;
    incomingSequence_ = sequence;
    return true;
}

std::vector<uint8_t> NetChannel::connectionless(const std::string &text) {
    std::vector<uint8_t> d(4 + text.size(), 0xff); // the marker, then the text
    if (!text.empty()) std::memcpy(d.data() + 4, text.data(), text.size());
    return d;
}

bool NetChannel::parseConnectionless(const uint8_t *data, size_t size, std::string &text) {
    if (!data || size < 4 || getU32(data) != kConnectionless) return false;
    text.assign(reinterpret_cast<const char *>(data + 4), size - 4);
    return true;
}

bool NetChannel::peekQport(const uint8_t *data, size_t size, uint16_t &qport) {
    if (!data || size < kHeaderBytes || getU32(data) == kConnectionless) return false;
    qport = getU16(data + 8);
    return true;
}

} // namespace realms
