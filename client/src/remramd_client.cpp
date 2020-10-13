#include "../include/client_core.hpp"
#include <iostream>

int main(int argc, char **argv, char **envp) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server IP> <server TCP port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    uint16_t server_port { static_cast<uint16_t>(std::stoul(argv[2])) };
    std::string server_ip = argv[1];
    uint16_t random_port = send_random_port(server_ip, server_port);
    uint16_t server_resp = wait_server_response(random_port);
    if (server_resp == SERV_ACC) {
        invoke_tcp_listener(server_ip, random_port, envp);
    }
    std::cerr << "The server has refused to establish the connection, sorry." << std::endl;
    return 1;
}
