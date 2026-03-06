#pragma once

#include <array>
#include <cstdint>

namespace transport {

constexpr uint32_t kMagic = 0x54525054;  // TRPT
constexpr uint16_t kVersion = 1;
constexpr std::size_t kMaxPayload = 1400;

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t connection_id;
    uint32_t seq;
    uint32_t ack;
    uint16_t payload_len;
};
#pragma pack(pop)

enum PacketType : uint16_t {
    kData = 1,
    kAck = 2,
    kControl = 3,
};

struct Packet {
    PacketHeader header{};
    std::array<char, kMaxPayload> payload{};
};

}  // namespace transport
