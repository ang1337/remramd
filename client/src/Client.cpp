#include "../inc/Client.hpp"
#include "../../shared/inc/Protocol.hpp"
#include "../../shared/inc/remramd_exception.hpp"
#include <unistd.h>
#include <iostream>

namespace remramd {
    // attempts to connect to the server
    void Client::connect(const std::string &server_ip, const std::uint16_t server_port) {
        std::cout << "Server => IP: " << server_ip << " | Port: " << server_port << std::endl; 
        const auto reverse_shell_port { internal::Protocol::request_connection(server_ip, server_port) };
        run_netcat_tcp_listener(reverse_shell_port);
    }

    // invokes netcat TCP listener that is ready to receive a reverse shell from a server
    void Client::run_netcat_tcp_listener(const std::uint16_t reverse_shell_port) {
        const std::string nc_path { "/bin/nc" },
                          nc_flags { "-nvlp" };
        const char* const args[] { nc_path.c_str(), nc_flags.c_str(), std::to_string(reverse_shell_port).c_str(), nullptr };
        execve(nc_path.c_str(), (char* const*)args, nullptr);
        std::cerr << "execve failure\n";
    }
}
