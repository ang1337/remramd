#include "../inc/Protocol.hpp"
#include "../inc/remramd_exception.hpp"
#include "../../shared/inc/Utils.hpp"
#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <thread>
#include <iostream>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <random>

namespace remramd {
    namespace internal {
        // ClientData ctor, populates the object with default binaries that are exposed to all new clients
        Protocol::ClientData::ClientData() {
            exposed_binaries.push_back("/bin/bash");
            exposed_binaries.push_back("/bin/cp");
            exposed_binaries.push_back("/bin/mv");
            exposed_binaries.push_back("/bin/ls");
            exposed_binaries.push_back("/bin/cat");
        }

        // validates and converts a timeout value from seconds to milliseconds
        void Protocol::validate_timeout(int &timeout) {
            if (!timeout || timeout == -1) return;

            if (timeout < 0) {
                timeout = (~timeout) + 0x1u;
            }

            // wrap around the timeout value if it passes the protocol's limit
            if (timeout > timeout_limit) {
                timeout %= timeout_limit;
                timeout = timeout ? (timeout + 1) : timeout;
            }

            timeout *= 1000; // from seconds to milliseconds
        }

        // waits for new clients
        // return value: optional ClientData object
        std::optional<Protocol::ClientData> Protocol::wait_new_connection_request(int server_sock_fd, int client_response_timeout) {
                struct sockaddr_in client_addr {};
                unsigned client_addr_len { sizeof(client_addr) };
                struct pollfd pfds[1] {};
                pfds[0].events = POLLIN;

                ClientData curr_pend_conn {};

                if ((curr_pend_conn.pend_conn_sock_fd = accept(server_sock_fd, (struct sockaddr *)&client_addr, &client_addr_len)) == -1) {
                    std::cerr << "Cannot accept a new connection\n";
                    return {};
                }

                curr_pend_conn.ip = inet_ntoa(client_addr.sin_addr);

                std::cout << std::endl << "New connection request from IP: " << curr_pend_conn.ip << std::endl << Utils::menu_msg;

                validate_timeout(client_response_timeout);

                // poll 10 seconds for reading
                pfds[0].fd = curr_pend_conn.pend_conn_sock_fd;
                int events_num { poll(pfds, 1, client_response_timeout) };

                if (!events_num) {
                    close(curr_pend_conn.pend_conn_sock_fd);
                    std::cerr << "Timeout\n";
                    return {};
                } 

                int pollin_happened { pfds[0].revents & POLLIN };
                
                // check if I can read an actual data from client's socket
                if (!pollin_happened) {
                    close(curr_pend_conn.pend_conn_sock_fd);
                    std::cerr << "Unexpected poll event\n";
                    return {};
                }

                return curr_pend_conn;
        }

        // requests a connection from a given server
        // return value: Client request (= reverse shell port on a client side)
        const Protocol::ClientRequest Protocol::request_connection(const std::string &server_ip, const std::uint16_t server_port) {
            auto get_rand_port = []() -> ClientRequest {
                static constexpr std::uint16_t port_rng_start { 1024 },
                                               port_rng_end { 49151 };
                static std::default_random_engine def_rnd_eng { std::random_device()() };
                static std::uniform_int_distribution<std::uint16_t> ushort_distr(port_rng_start, port_rng_end);
                return ushort_distr(def_rnd_eng);
            };

            int client_sock { socket(AF_INET, SOCK_STREAM, 0) };

            if (client_sock == -1) {
                throw exception("Cannot open a socket for a client");
            }

            struct sockaddr_in sa_in {};
            sa_in.sin_family = AF_INET;
            sa_in.sin_port = htons(server_port);

            if (inet_pton(sa_in.sin_family, server_ip.c_str(), &sa_in.sin_addr) <= 0) {
                close(client_sock);
                throw exception("Invalid server IP address");
            }

            if (connect(client_sock, (struct sockaddr*)&sa_in, sizeof(sa_in))) {
                close(client_sock);
                throw exception("Cannot connect to the server");
            }

            ClientRequest rand_port { get_rand_port() };

            rand_port = htons(rand_port);

            write(client_sock, &rand_port, sizeof(rand_port));

            rand_port = ntohs(rand_port);

            ServerResponse server_response {};

            std::cout << "Waiting for the server response...\n";
            read(client_sock, &server_response, sizeof(server_response));

            ClientResponse client_response {};

            close(client_sock);

            switch (server_response) {
                case ServerResponse::YEP:
                    return rand_port;
                case ServerResponse::NOPE: 
                    throw exception("The server refused to establish a given connection");
                default:
                    throw exception("Unknown server response");
            }

        }
    }
}