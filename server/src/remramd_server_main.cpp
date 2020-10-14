#include "../include/remramd_server.hpp"
#include <iostream>
#include <memory>

int main(int argc, char **argv) {
    if (getuid()) {
        std::cerr << "Run it as root\n";
        exit(EXIT_FAILURE);
    }
    if (argc != 4) {
        fprintf(stderr, "Usage: sudo %s <TCP port> <path to current python interpreter> <path to chroot jail builder script>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    uint16_t port { static_cast<uint16_t>(std::stoi(argv[1])) };
    std::unique_ptr<Server> server { std::make_unique<Server>(port, argv[2], argv[3]) };
    server->run();
    return 0;
}
