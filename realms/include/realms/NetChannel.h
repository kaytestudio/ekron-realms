// Ekron's network channel (Quake 2's net_chan.c, with Quake 3's message
// fragments and numbered reliable commands). Transport free: transmit()
// hands out the datagrams to send, process() takes the ones that arrived.
//
// A message is one transmit(): every reliable message the peer has not
// acknowledged yet, then the unreliable bytes. A message too big for one
// datagram goes out in fragments; it is delivered when all of them have
// arrived. Messages older than the last delivered one are dropped, so the
// unreliable part is "latest wins" (snapshots); reliable messages are
// resent in every message until the peer acknowledges them and are
// delivered exactly once, in order.
//
// Datagram (little-endian):
//   u32 sequence     message number, 1, 2, ...
//   u32 ack          the newest message number received from the peer
//   u16 qport        the client's channel id (a NAT can change its port)
//   u16 fragment     this fragment's index
//   u16 fragments    how many the message has (1: not fragmented)
//   ...              the message's bytes for this fragment
// Message: varint reliable ack (the number of the last reliable message
// received from the peer), varint count, and when count > 0 a varint first
// number and count length-prefixed messages; then the unreliable bytes.
//
// Connectionless packets (the connect handshake, server status) start with
// 0xffffffff instead of a sequence.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace realms {

struct NetMessage {
    uint32_t sequence = 0;
    std::vector<std::vector<uint8_t>> reliable; // newly arrived, in order
    std::vector<uint8_t> unreliable;
};

class NetChannel {
public:
    static constexpr size_t kHeaderBytes = 14;
    // Payload per datagram: under a 1500 byte MTU with IP / UDP headers.
    static constexpr size_t kFragmentBytes = 1200;
    static constexpr size_t kMaxFragments = 1024;
    static constexpr size_t kMaxMessageBytes = kFragmentBytes * kMaxFragments;
    // Unacknowledged reliable bytes before the channel overflows (the peer
    // is gone or far behind: drop it).
    static constexpr size_t kMaxReliableBytes = 512 * 1024;

    explicit NetChannel(uint16_t qport = 0) : qport_(qport) {}
    void reset(uint16_t qport);
    uint16_t qport() const { return qport_; }

    // Queues a reliable message; false (and overflowed()) past kMaxReliableBytes.
    bool sendReliable(std::vector<uint8_t> message);
    // The next message: the pending reliable messages, then `unreliable`
    // (dropped when the whole would exceed kMaxMessageBytes), as datagrams.
    void transmit(const std::vector<uint8_t> &unreliable, std::vector<std::vector<uint8_t>> &datagrams, double now);
    // A datagram from the peer. True when it completes a message newer than
    // the last one delivered (`out`); false for old, duplicate, partial or
    // malformed ones.
    bool process(const uint8_t *data, size_t size, NetMessage &out, double now);

    uint32_t outgoingSequence() const { return outgoingSequence_; } // the last message sent
    uint32_t incomingSequence() const { return incomingSequence_; } // the last message delivered
    uint32_t incomingAcknowledged() const { return incomingAcknowledged_; } // ours the peer has
    bool overflowed() const { return overflowed_; }
    size_t reliableBacklog() const { return pendingBytes_; }
    size_t reliablePending() const { return pending_.size(); }
    // Messages from the peer that never arrived whole.
    uint32_t droppedMessages() const { return dropped_; }
    double lastReceived() const { return lastReceived_; }
    double lastSent() const { return lastSent_; }

    // 0xffffffff then `text`; `parseConnectionless` reads one back.
    static std::vector<uint8_t> connectionless(const std::string &text);
    static bool parseConnectionless(const uint8_t *data, size_t size, std::string &text);
    // The qport of a sequenced datagram (servers find the client by it).
    static bool peekQport(const uint8_t *data, size_t size, uint16_t &qport);

private:
    uint16_t qport_ = 0;
    uint32_t outgoingSequence_ = 0;
    uint32_t incomingSequence_ = 0;
    uint32_t incomingAcknowledged_ = 0;
    // reliable messages out: numbers pendingFirst_ .. pendingFirst_ + size - 1
    std::deque<std::vector<uint8_t>> pending_;
    uint32_t pendingFirst_ = 1;
    size_t pendingBytes_ = 0;
    uint32_t reliableReceived_ = 0; // the last reliable number delivered
    bool overflowed_ = false;
    uint32_t dropped_ = 0;
    double lastReceived_ = 0.0, lastSent_ = 0.0;
    // the fragments of the message being received
    uint32_t fragSequence_ = 0;
    std::vector<std::vector<uint8_t>> fragments_;
    size_t fragmentsHave_ = 0;
};

} // namespace realms
