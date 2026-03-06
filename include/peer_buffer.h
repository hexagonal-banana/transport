#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace transport {

struct PeerKey {
    std::string ip;
    uint16_t port{0};
    uint32_t connection_id{0};

    bool operator==(const PeerKey& other) const {
        return ip == other.ip && port == other.port && connection_id == other.connection_id;
    }
};

struct PeerKeyHash {
    std::size_t operator()(const PeerKey& key) const;
};

struct BufferedDatagram {
    uint64_t recv_ts_ms;
    std::vector<char> bytes;
};

class PeerBuffer {
public:
    explicit PeerBuffer(std::size_t max_bytes_per_peer);

    bool push(const PeerKey& key, std::vector<char> data);
    bool pop(const PeerKey& key, BufferedDatagram& out);
    std::size_t size(const PeerKey& key) const;

private:
    struct QueueState {
        std::deque<BufferedDatagram> queue;
        std::size_t bytes{0};
    };

    std::size_t max_bytes_per_peer_;
    std::unordered_map<PeerKey, QueueState, PeerKeyHash> buffers_;
};

}  // namespace transport
