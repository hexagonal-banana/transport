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
    const std::string sock_path = (argc > 1) ? argv[1] : "/tmp/transport_agent.sock";
    const std::string peer_ip = (argc > 2) ? argv[2] : "127.0.0.1";
    const uint16_t peer_port =
        (argc > 3) ? static_cast<uint16_t>(std::stoi(argv[3])) : 0;
    const uint32_t connection_id =
        (argc > 4) ? static_cast<uint32_t>(std::stoul(argv[4])) : 0;

    int fd = connect_uds(sock_path);
    if (fd < 0) {
        std::cerr << "failed to connect " << sock_path << "\n";
        return 1;
    }

    const std::string req = "SUBSCRIBE " + peer_ip + " " + std::to_string(peer_port) +
                            " " + std::to_string(connection_id) + "\n";
    if (::send(fd, req.data(), req.size(), 0) < 0) {
        std::cerr << "send subscribe failed\n";
        ::close(fd);
        return 1;
    }

    std::cout << "subscribed to peer " << peer_ip << ":" << peer_port << "#"
              << connection_id << "\n";

    std::vector<char> buf(4096);
    while (true) {
        ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
        if (n <= 0) {
            break;
        }
        std::cout.write(buf.data(), n);
        std::cout.flush();
    }

    ::close(fd);
    return 0;
}
