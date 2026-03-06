#include "peer_buffer.h"
#include "protocol.h"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr const char* kDefaultSockPath = "/tmp/transport_agent.sock";

int create_udp_socket(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "udp bind failed: " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }
    return fd;
}

int create_uds_server(const std::string& path) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    ::unlink(path.c_str());

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "uds bind failed at " << path << ": " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }

    if (::listen(fd, 64) != 0) {
        std::cerr << "uds listen failed at " << path << ": " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }

    return fd;
}

std::string key_to_text(const transport::PeerKey& key) {
    return key.ip + ":" + std::to_string(key.port) + "#" + std::to_string(key.connection_id);
}

bool parse_subscribe_line(const std::string& line, transport::PeerKey& key) {
    std::istringstream iss(line);
    std::string cmd;
    if (!(iss >> cmd >> key.ip >> key.port >> key.connection_id)) {
        return false;
    }
    return cmd == "SUBSCRIBE";
}

bool parse_send_line(const std::string& line, transport::PeerKey& key, std::string& payload) {
    std::istringstream iss(line);
    std::string cmd;
    if (!(iss >> cmd >> key.ip >> key.port >> key.connection_id)) {
        return false;
    }
    if (cmd != "SEND") {
        return false;
    }
    std::getline(iss, payload);
    if (!payload.empty() && payload[0] == ' ') {
        payload.erase(payload.begin());
    }
    return true;
}

void send_buffered_to_one_client(int fd,
                                 const transport::PeerKey& key,
                                 transport::PeerBuffer& peer_buffer) {
    transport::BufferedDatagram d{};
    while (peer_buffer.pop(key, d)) {
        const std::string header = "FROM " + key_to_text(key) + " " + std::to_string(d.bytes.size()) + "\n";
        if (::send(fd, header.data(), header.size(), MSG_NOSIGNAL) < 0) {
            return;
        }
        if (!d.bytes.empty() && ::send(fd, d.bytes.data(), d.bytes.size(), MSG_NOSIGNAL) < 0) {
            return;
        }
        const char end = '\n';
        if (::send(fd, &end, 1, MSG_NOSIGNAL) < 0) {
            return;
        }
    }
}

void broadcast_key(const transport::PeerKey& key,
                   transport::PeerBuffer& peer_buffer,
                   const std::unordered_map<transport::PeerKey, std::vector<int>, transport::PeerKeyHash>& subscribers) {
    auto it = subscribers.find(key);
    if (it == subscribers.end()) {
        return;
    }

    for (int fd : it->second) {
        send_buffered_to_one_client(fd, key, peer_buffer);
    }
}

bool send_udp_payload(int udp_fd, const transport::PeerKey& key, const std::string& payload) {
    if (udp_fd < 0) {
        return false;
    }

    transport::PacketHeader header{};
    header.magic = transport::kMagic;
    header.version = transport::kVersion;
    header.type = transport::kData;
    header.connection_id = key.connection_id;
    header.seq = 0;
    header.ack = 0;
    header.payload_len = static_cast<uint16_t>(std::min<std::size_t>(payload.size(), transport::kMaxPayload));

    std::vector<char> packet(sizeof(header) + header.payload_len);
    std::memcpy(packet.data(), &header, sizeof(header));
    if (header.payload_len > 0) {
        std::memcpy(packet.data() + sizeof(header), payload.data(), header.payload_len);
    }

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(key.port);
    if (::inet_pton(AF_INET, key.ip.c_str(), &dst.sin_addr) != 1) {
        return false;
    }

    const ssize_t n = ::sendto(udp_fd,
                               packet.data(),
                               packet.size(),
                               0,
                               reinterpret_cast<sockaddr*>(&dst),
                               sizeof(dst));
    return n == static_cast<ssize_t>(packet.size());
}

}  // namespace

int main(int argc, char** argv) {
    bool ipc_only = false;
    uint16_t udp_port = 9000;
    std::string sock_path = kDefaultSockPath;

    if (argc > 1 && std::string(argv[1]) == "--ipc-only") {
        ipc_only = true;
        if (argc > 2) {
            sock_path = argv[2];
        }
    } else {
        if (argc > 1) {
            udp_port = static_cast<uint16_t>(std::stoi(argv[1]));
        }
        if (argc > 2) {
            sock_path = argv[2];
        }
    }

    int udp_fd = -1;
    if (!ipc_only) {
        udp_fd = create_udp_socket(udp_port);
        if (udp_fd < 0) {
            std::cerr << "failed to create udp socket on port " << udp_port << "\n";
            return 1;
        }
    }

    int uds_fd = create_uds_server(sock_path);
    if (uds_fd < 0) {
        std::cerr << "failed to create uds server at " << sock_path << "\n";
        if (udp_fd >= 0) {
            ::close(udp_fd);
        }
        return 1;
    }

    transport::PeerBuffer peer_buffer(1 << 20);
    std::unordered_map<transport::PeerKey, std::vector<int>, transport::PeerKeyHash> subscribers;
    std::unordered_map<int, transport::PeerKey> client_peer;

    std::cout << "[agent] started: udp=" << (ipc_only ? "disabled" : std::to_string(udp_port))
              << " ipc=" << sock_path << "\n";

    while (true) {
        std::vector<pollfd> fds;
        if (udp_fd >= 0) {
            fds.push_back({udp_fd, POLLIN, 0});
        }
        fds.push_back({uds_fd, POLLIN, 0});

        for (const auto& kv : client_peer) {
            fds.push_back({kv.first, POLLIN, 0});
        }

        int rc = ::poll(fds.data(), fds.size(), -1);
        if (rc <= 0) {
            continue;
        }

        const std::size_t udp_idx = (udp_fd >= 0) ? 0 : static_cast<std::size_t>(-1);
        const std::size_t uds_idx = (udp_fd >= 0) ? 1 : 0;
        const std::size_t clients_start_idx = (udp_fd >= 0) ? 2 : 1;

        if (udp_fd >= 0 && (fds[udp_idx].revents & POLLIN)) {
            std::vector<char> buf(2048);
            sockaddr_in src{};
            socklen_t src_len = sizeof(src);
            const ssize_t n = ::recvfrom(udp_fd, buf.data(), buf.size(), 0,
                                         reinterpret_cast<sockaddr*>(&src), &src_len);
            if (n > 0) {
                buf.resize(static_cast<std::size_t>(n));
                char ip[INET_ADDRSTRLEN] = {0};
                ::inet_ntop(AF_INET, &src.sin_addr, ip, sizeof(ip));

                uint32_t connection_id = 0;
                if (buf.size() >= sizeof(transport::PacketHeader)) {
                    transport::PacketHeader header{};
                    std::memcpy(&header, buf.data(), sizeof(header));
                    if (header.magic == transport::kMagic && header.version == transport::kVersion) {
                        connection_id = header.connection_id;
                    }
                }

                const transport::PeerKey key{std::string(ip), ntohs(src.sin_port), connection_id};
                peer_buffer.push(key, std::move(buf));
                std::cout << "[agent] udp in key=" << key_to_text(key) << "\n";
                broadcast_key(key, peer_buffer, subscribers);
            }
        }

        if (fds[uds_idx].revents & POLLIN) {
            int client = ::accept(uds_fd, nullptr, nullptr);
            if (client >= 0) {
                char first_line[512] = {0};
                const ssize_t n = ::recv(client, first_line, sizeof(first_line) - 1, 0);
                if (n > 0) {
                    transport::PeerKey key{};
                    std::string payload;
                    const std::string line(first_line, static_cast<std::size_t>(n));

                    if (parse_subscribe_line(line, key)) {
                        subscribers[key].push_back(client);
                        client_peer[client] = key;
                        const std::string ok = "OK\n";
                        ::send(client, ok.data(), ok.size(), MSG_NOSIGNAL);
                        std::cout << "[agent] receiver subscribed key=" << key_to_text(key) << " fd=" << client << "\n";
                        send_buffered_to_one_client(client, key, peer_buffer);
                    } else if (parse_send_line(line, key, payload)) {
                        const std::string ok = "OK\n";
                        ::send(client, ok.data(), ok.size(), MSG_NOSIGNAL);

                        std::vector<char> data(payload.begin(), payload.end());
                        peer_buffer.push(key, std::move(data));
                        const bool udp_sent = send_udp_payload(udp_fd, key, payload);
                        std::cout << "[agent] sender msg key=" << key_to_text(key)
                                  << " bytes=" << payload.size()
                                  << " udp_sent=" << (udp_sent ? "yes" : "no") << "\n";
                        broadcast_key(key, peer_buffer, subscribers);
                        ::close(client);
                    } else {
                        const std::string err =
                            "ERR use: SUBSCRIBE <ip> <port> <conn_id> OR SEND <ip> <port> <conn_id> <payload>\\n\n";
                        ::send(client, err.data(), err.size(), MSG_NOSIGNAL);
                        ::close(client);
                    }
                } else {
                    ::close(client);
                }
            }
        }

        for (std::size_t i = clients_start_idx; i < fds.size(); ++i) {
            if (!(fds[i].revents & (POLLHUP | POLLERR | POLLNVAL))) {
                continue;
            }
            const int dead_fd = fds[i].fd;
            auto it = client_peer.find(dead_fd);
            if (it != client_peer.end()) {
                auto sit = subscribers.find(it->second);
                if (sit != subscribers.end()) {
                    auto& vec = sit->second;
                    vec.erase(std::remove(vec.begin(), vec.end(), dead_fd), vec.end());
                    if (vec.empty()) {
                        subscribers.erase(sit);
                    }
                }
                std::cout << "[agent] receiver disconnected fd=" << dead_fd << "\n";
                client_peer.erase(it);
            }
            ::close(dead_fd);
        }
    }

    return 0;
}
