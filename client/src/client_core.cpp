#include <cstdlib>
#include <vector>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <string.h>
#include <random>
#include "../include/client_core.hpp"

uint16_t brute_valid_port() {
    std::default_random_engine rand_gen { std::random_device()() };
    std::uniform_int_distribution<uint16_t> ushort_distribution(MIN_TCP_PORT, MAX_TCP_PORT);
    uint16_t random_port {};
    int check_port {};
    do {
        random_port = ushort_distribution(rand_gen);
        int test_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (test_sock == -1) {
            std::cerr << "Cannot open socket fd" << std::endl;
            exit(EXIT_FAILURE);
        }
        struct sockaddr_in test_sock_info {};
        test_sock_info.sin_addr.s_addr = INADDR_ANY;
        test_sock_info.sin_port = htons(random_port);
        test_sock_info.sin_family = AF_INET;
        check_port = bind(test_sock, (struct sockaddr *)&test_sock_info, sizeof(struct sockaddr_in));
        shutdown(test_sock, SHUT_RDWR);
        close(test_sock);
    } while (check_port);
    return random_port;
}

uint16_t send_random_port(const std::string &server_ip, const uint16_t server_port) {
    int ping_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ping_fd == -1) {
        std::cerr << "Cannot create socket fd" << std::endl;
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in server_info {};
    server_info.sin_family = AF_INET;
    server_info.sin_port = htons(server_port); 
    if (inet_pton(AF_INET, server_ip.data(), &server_info.sin_addr) != 1) {
        std::cerr << "inet_pton IP address convertion failure" << std::endl;
        exit(EXIT_FAILURE);
    }
    if (connect(ping_fd, (struct sockaddr *)&server_info, sizeof(struct sockaddr_in))) {
        fprintf(stderr, "Cannot connect to the server %s that listening on port %hu, the server is probably down at this moment\n", server_ip.data(),
                                                                                                                               server_port);
        exit(EXIT_FAILURE);
    }
    // 2) Randomize TCP listening port 
    uint16_t random_port { brute_valid_port() };
    // 3) Send the randomized TCP port to the server and close the connection
    uint16_t bigend_rand_port { htons(random_port) }; // host byte order (either big or little endian) to network byte order (always big endian)
    std::cout << "Random port for remote shell is " << random_port << std::endl;
    if (write(ping_fd, &bigend_rand_port, sizeof(uint16_t)) == -1) {
        std::cerr << "Cannot send random port to the server" << std::endl;
        exit(EXIT_FAILURE);
    }
    std::string port_string = std::to_string(random_port);
    char *port_string_arg = new char[port_string.size() + 1];
    memcpy(port_string_arg, &port_string[0], port_string.size() + 1);
    shutdown(ping_fd, SHUT_RDWR);
    close(ping_fd);
    return random_port;
}

uint16_t wait_server_response(uint16_t random_port) {
    int resp_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (resp_sock_fd == -1) {
        std::cerr << "Cannot open socket fd" << std::endl;
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in curr_client_info {};
    curr_client_info.sin_addr.s_addr = INADDR_ANY;
    curr_client_info.sin_port = htons(random_port);
    curr_client_info.sin_family = AF_INET;
    if (bind(resp_sock_fd, (struct sockaddr *)&curr_client_info, sizeof(struct sockaddr))) {
        fprintf(stderr, "Cannot bind port #%hu\n", random_port);
        exit(EXIT_FAILURE);
    }
    if (listen(resp_sock_fd, 10)) {
        std::cerr << "Cannot listen\n" << std::endl;
        exit(EXIT_FAILURE);
    }
    socklen_t socklen = sizeof(struct sockaddr_in);
    std::cout << "Waiting for connection approval..." << std::endl;
    int serv_resp_fd = accept(resp_sock_fd, (struct sockaddr *)&curr_client_info, &socklen);
    if (serv_resp_fd == -1) {
        std::cerr << "Cannot accept the incoming connection" << std::endl;
        exit(EXIT_FAILURE);
    }
    uint16_t serv_resp {};
    if (read(serv_resp_fd, &serv_resp, sizeof(uint16_t)) == -1) {
        fprintf(stderr, "Cannot read the server response from the socket fd #%hu\n", random_port);
        exit(EXIT_FAILURE);
    }
    serv_resp = ntohs(serv_resp);
    shutdown(serv_resp_fd, SHUT_RDWR);
    shutdown(resp_sock_fd, SHUT_RDWR);
    close(serv_resp_fd);
    close(resp_sock_fd);
    return serv_resp;
}

void invoke_tcp_listener(const std::string server_ip, uint16_t random_port, char **envp) {
    std::cout << "The server has accepted your connection request! Wait for the remote shell..." << std::endl;
    const std::string random_port_str { std::to_string(random_port) },
                      nc_flags { "-nvlp" };
    // run netcat TCP listener
    const char* const args[] = { "/bin/nc", nc_flags.data(), random_port_str.data(), nullptr };
    execve("/bin/nc", (char* const *)args, envp);
    std::cerr << "client's execve failed, check if netcat is installed" << std::endl;
    exit(EXIT_FAILURE);
}

