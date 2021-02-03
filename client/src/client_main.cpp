#include "../inc/Client.hpp"
#include "../../shared/inc/Connection.hpp"
#include "../../shared/inc/remramd_exception.hpp"
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <server IP> <server port>\n";
        exit(1);
    }
    remramd::Client client_obj {};
    try {
        client_obj.connect(argv[1], std::stoi(argv[2]));
    } catch (const remramd::exception &e) {
        std::cerr << e.what() << std::endl;
    }
    return 0;
}