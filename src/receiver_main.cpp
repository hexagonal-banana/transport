#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int connect_uds(const std::string& path) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "usage: receiver <sock_path> <peer_ip> <peer_port> <connection_id>\n";
        return 1;
    }

    const std::string sock_path = argv[1];
    const std::string peer_ip = argv[2];
    const uint16_t peer_port = static_cast<uint16_t>(std::stoi(argv[3]));
    const uint32_t connection_id = static_cast<uint32_t>(std::stoul(argv[4]));

    int fd = connect_uds(sock_path);
    if (fd < 0) {
        std::cerr << "[receiver] connect failed: " << sock_path << "\n";
        return 1;
    }

    const std::string req = "SUBSCRIBE " + peer_ip + " " + std::to_string(peer_port) + " " +
                            std::to_string(connection_id) + "\n";
    if (::send(fd, req.data(), req.size(), 0) < 0) {
        std::cerr << "[receiver] subscribe send failed\n";
        ::close(fd);
        return 1;
    }

    std::cout << "[receiver] subscribed key=" << peer_ip << ":" << peer_port << "#"
              << connection_id << "\n";

    std::vector<char> buf(4096);
    while (true) {
        const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
        if (n <= 0) {
            break;
        }
        std::cout << "[receiver] recv bytes=" << n << "\n";
        std::cout.write(buf.data(), n);
        std::cout.flush();
    }

    ::close(fd);
    return 0;
}
