#pragma once
#include "../../shared/inc/remramd_exception.hpp"
#include <type_traits>
#include <cstdint>
#include <poll.h>
#include <unistd.h>
#include <utility>
#include <optional>
#include <string>
#include <vector>
#include <iostream>

namespace remramd {
    namespace internal {
        class Protocol {
            public:
                // client request code == reverse shell port
                using ClientRequest = std::uint16_t;
                
                // max timeout = 30 seconds
                static constexpr int timeout_limit { 30000 };

                struct ClientData {
                    std::string ip;
                    std::string fakeroot_path;
                    std::vector<std::string> exposed_binaries;
                    pid_t pid;
                    uid_t uid;
                    gid_t gid;
                    std::uint16_t reverse_shell_port; 
                    std::uint16_t pend_conn_sock_fd;
                    ClientData();
                };

                enum ClientResponse : std::uint8_t {
                    ACK = 2,
                    C_TIMEOUT
                };

                enum ServerResponse : std::uint8_t {
                    NOPE,
                    YEP
                };

                enum PipeResponse : std::uint8_t {
                    CHILD_FAILED,
                    CHILD_SUCCESS
                };
            
                static void validate_timeout(int &timeout);

                template <typename T>
                static void send_data(const Protocol::ClientData &pend_conn, const T &data);

                template <typename T>
                static std::optional<T> receive_data(const ClientData &pend_conn, int timeout = -1) noexcept;

                // server-side methods
                static std::optional<ClientData> wait_new_connection_request(int server_sock_fd, int client_response_timeout);
                // client-side methods
                static const std::uint16_t request_connection(const std::string &server_ip, const std::uint16_t server_port);
        };

        // sends data to a pending connection
        // args:
        // @ pend_conn - current pending connection object
        // @ data - data to be sent
        template <typename T>
        void Protocol::send_data(const Protocol::ClientData &pend_conn, const T &data) {
            static_assert(!std::is_pointer<T>::value, "Cannot operate with pointers");

            if (write(pend_conn.pend_conn_sock_fd, &data, sizeof(data)) != sizeof(data)) {
                close(pend_conn.pend_conn_sock_fd);
                throw exception("Cannot write data to the socket");
            }
        }

        // receives data from a pending connection
        // args:
        // @ pend_conn - current pending connection object
        // @ timeout - seconds to wait for data to be read
        // return value: optional data read from the socket
        template <typename T>
        std::optional<T> Protocol::receive_data(const Protocol::ClientData &pend_conn, int timeout) noexcept {
            static_assert(!std::is_pointer<T>::value && !std::is_reference<T>::value, "Cannot read from pointer and has no functionality to work with references");

            if (timeout != -1) {
                validate_timeout(timeout);
            }

            struct pollfd pfds[1] {};
            pfds[0].fd = pend_conn.pend_conn_sock_fd;
            pfds[0].events = POLLIN;

            int events_num { poll(pfds, 1, timeout) };

            if (!events_num) {
                std::cerr << "Timeout trying to read data from a socket, pending connection IP: " + pend_conn.ip << std::endl;
                return {};
            }

            int pollin_happened { pfds[0].revents & POLLIN };

            if (pollin_happened) {
                T data {};

                if (read(pend_conn.pend_conn_sock_fd, &data, sizeof(data)) != sizeof(data)) {
                    std::cerr << "Cannot read the requested amount of data from a socket" << std::endl;
                    return {};
                }

                return data;
            }

            std::cerr << "Unknown connection failure" << std::endl;
            return {};
        }
    }
}
