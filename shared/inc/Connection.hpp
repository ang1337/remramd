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
    class Connection {
        private:
            int sock_fd;
            bool server_side;
            void connect(const std::string &server_ip, const std::uint16_t server_port, internal::PipeWrapper &pipe);
        public:
            //Connection(const std::string &target_ip, const std::uint16_t target_port);
            Connection(const Connection &) = delete;

            // server-side reverse shell connection (!!!)
            Connection(const internal::Protocol::ClientData &new_client, internal::PipeWrapper &pipe, Server &server_instance);
            // client-side regular classic connection

            template <typename T>
            static void send_data(int fd, const T &data);

            template <typename T>
            static T receive_data(int fd, int timeout = -1);

            int get_sock_fd() const noexcept;
            //std::optional<std::reference_wrapper<Protocol::ClientData>> get_connected_client() noexcept;
            ~Connection() noexcept;
    };

    template <typename T>
    void Connection::send_data(int fd, const T &data) {
        static_assert(std::is_enum<T>::value, "Only enum and enum class data types are allowed for writing to a socket");

        if (write(fd, &data, sizeof(data)) != sizeof(data)) {
            throw exception("Cannot send data through the socket");
        }
    }

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