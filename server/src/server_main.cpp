#include "../inc/Server.hpp"
#include "../../shared/inc/remramd_exception.hpp"
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <jail path> <server port> <backlog>\n";
        exit(1);
    }
    try {
        remramd::Server server_obj(argv[1], std::stoi(argv[2]), std::stoi(argv[3]));
        server_obj.run();
    } catch (const remramd::exception &e) {
        std::cerr << e.what() << std::endl;
    }
    return 0;
}
