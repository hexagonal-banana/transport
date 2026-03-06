#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>

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
    if (argc < 6) {
        std::cerr << "usage: sender <sock_path> <peer_ip> <peer_port> <connection_id> <payload>\n";
        return 1;
    }

    const std::string sock_path = argv[1];
    const std::string peer_ip = argv[2];
    const uint16_t peer_port = static_cast<uint16_t>(std::stoi(argv[3]));
    const uint32_t connection_id = static_cast<uint32_t>(std::stoul(argv[4]));
    const std::string payload = argv[5];

    int fd = connect_uds(sock_path);
    if (fd < 0) {
        std::cerr << "[sender] connect failed: " << sock_path << "\n";
        return 1;
    }

    const std::string cmd = "SEND " + peer_ip + " " + std::to_string(peer_port) + " " +
                            std::to_string(connection_id) + " " + payload + "\n";

    if (::send(fd, cmd.data(), cmd.size(), 0) < 0) {
        std::cerr << "[sender] send command failed\n";
        ::close(fd);
        return 1;
    }

    std::cout << "[sender] sent via agent key=" << peer_ip << ":" << peer_port << "#"
              << connection_id << " payload=\"" << payload << "\"\n";

    char ack[64] = {0};
    ssize_t n = ::recv(fd, ack, sizeof(ack) - 1, 0);
    if (n > 0) {
        std::cout << "[sender] agent reply: " << std::string(ack, static_cast<std::size_t>(n));
    }

    ::close(fd);
    return 0;
}
