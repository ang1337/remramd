#pragma once 
#include "Protocol.hpp"
#include "../../server/inc/Server.hpp"
#include "../../client/inc/Client.hpp"
#include "../../server/inc/PipeWrapper.hpp"
#include "remramd_exception.hpp"
#include <type_traits>
#include <poll.h>
#include <optional>
#include <unistd.h>

namespace remramd {
    namespace internal {
        class Connection {
            private:
                int sock_fd;
                void connect(const std::string &server_ip, const std::uint16_t server_port, internal::PipeWrapper &pipe);
            public:
                Connection(const Connection &) = delete;

                Connection(const internal::Protocol::ClientData &new_client, internal::PipeWrapper &pipe);

                template <typename T>
                static void send_data(int fd, const T &data);

                template <typename T>
                static T receive_data(int fd, int timeout = -1);

                ~Connection() noexcept;
        };

        // sends data over the given socket
        // args:
        // @ fd - socket file descriptor
        // @ data - data to be sent over a given socket
        template <typename T>
        void Connection::send_data(int fd, const T &data) {
            static_assert(std::is_enum<T>::value, "Only enum and enum class data types are allowed for writing to a socket");

            if (write(fd, &data, sizeof(data)) != sizeof(data)) {
                throw exception("Cannot send data through the socket");
            }
        }

        // receives data over the given socket
        // args:
        // @ fd - socket file descriptor
        // @ timeout - data read timeout (default -1, which blocks till some data is available)
        template <typename T>
        T Connection::receive_data(int fd, int timeout) {
            static_assert(std::is_enum<T>::value, "Only enum and enum class data types are allowed for reading from a socket");

            internal::Protocol::validate_timeout(timeout);

            struct pollfd pfds[1] {};
            pfds[0].fd = fd;
            pfds[0].events = POLLIN;

            int events_num { poll(pfds, 1, timeout) };

            if (!events_num) {
                throw exception("poll timeout on read from a socket");
            }

            int pollin_happened { pfds[0].revents & POLLIN };

            if (pollin_happened) {
                T data {};

                if (read(fd, &data, sizeof(data)) != sizeof(data)) {
                    throw exception("Failed to read the requested amount of data from a socket");
                }

                return data;
            }

            throw exception("Unknown read failure");
        }
    }
}