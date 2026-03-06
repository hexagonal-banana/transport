#include "peer_buffer.h"

#include <chrono>
#include <functional>
#include <utility>

namespace transport {

namespace {
uint64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

template <typename T>
void hash_combine(std::size_t& seed, const T& v) {
    seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}
}  // namespace

std::size_t PeerKeyHash::operator()(const PeerKey& key) const {
    std::size_t seed = 0;
    hash_combine(seed, key.ip);
    hash_combine(seed, key.port);
    hash_combine(seed, key.connection_id);
    return seed;
}

PeerBuffer::PeerBuffer(std::size_t max_bytes_per_peer)
    : max_bytes_per_peer_(max_bytes_per_peer) {}

bool PeerBuffer::push(const PeerKey& key, std::vector<char> data) {
    auto& state = buffers_[key];
    const std::size_t len = data.size();

    if (len > max_bytes_per_peer_) {
        return false;
    }

    while (state.bytes + len > max_bytes_per_peer_) {
        if (state.queue.empty()) {
            break;
        }
        state.bytes -= state.queue.front().bytes.size();
        state.queue.pop_front();
    }

    BufferedDatagram d{};
    d.recv_ts_ms = now_ms();
    d.bytes = std::move(data);
    state.bytes += d.bytes.size();
    state.queue.push_back(std::move(d));
    return true;
}

bool PeerBuffer::pop(const PeerKey& key, BufferedDatagram& out) {
    auto it = buffers_.find(key);
    if (it == buffers_.end() || it->second.queue.empty()) {
        return false;
    }

    auto& state = it->second;
    out = std::move(state.queue.front());
    state.bytes -= out.bytes.size();
    state.queue.pop_front();
    return true;
}

std::size_t PeerBuffer::size(const PeerKey& key) const {
    auto it = buffers_.find(key);
    if (it == buffers_.end()) {
        return 0;
    }
    return it->second.queue.size();
}

}  // namespace transport
