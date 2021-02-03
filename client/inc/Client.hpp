#pragma once
#include <string>
#include <cstdint>

namespace remramd {
    class Client {
        private:
            void run_netcat_tcp_listener(const std::uint16_t reverse_shell_port);
        public:
            void connect(const std::string &server_ip, const std::uint16_t server_port);
            Client(const Client&) = delete;
            Client& operator = (const Client&) = delete;
    };
}