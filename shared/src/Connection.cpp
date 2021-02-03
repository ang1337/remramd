#include "../inc/Connection.hpp"
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <functional>

namespace remramd {
        Connection::~Connection() {
            close(sock_fd);
        }

        void Connection::connect(const std::string &target_ip, const std::uint16_t target_port, internal::PipeWrapper &pipe) {
            sock_fd = socket(AF_INET, SOCK_STREAM, 0);
            
            if (sock_fd == -1) {
                throw exception("cannot open a socket for a requested connection");
            }

            struct sockaddr_in client_info {};
            client_info.sin_family = AF_INET;
            client_info.sin_addr.s_addr = inet_addr(target_ip.c_str());
            client_info.sin_port = htons(target_port);

            if (::connect(sock_fd, (struct sockaddr *)&client_info, sizeof(client_info))) {
                close(sock_fd);
                throw exception("Failed to connect");
            }

            //if (this->server_side) {
                dup2(sock_fd, fileno(stdin));
                dup2(sock_fd, fileno(stdout));
                dup2(sock_fd, fileno(stderr));
                pipe.write(internal::Protocol::PipeResponse::CHILD_SUCCESS);
                execl("/bin/bash", "/bin/bash", "-i", nullptr);
           // }

        }
        // server-side constructor for reverse shell connection preparation
        Connection::Connection(const internal::Protocol::ClientData &new_client, 
                               internal::PipeWrapper &pipe, 
                               Server &server_instance) : server_side(true) {

            const std::string chown_cmd {
                "chown -R " + std::to_string(new_client.uid) + ':'
                            + std::to_string(new_client.gid) + ' '
                            + new_client.fakeroot_path
            };

            std::system(chown_cmd.c_str());

            std::cout << "Changing the current working directory to " << new_client.fakeroot_path << std::endl;
            if (chdir(new_client.fakeroot_path.c_str())) {
                throw exception("Cannot change the current directory to the client's fakeroot");
            }

            if (chroot(new_client.fakeroot_path.c_str())) {
                throw exception("Failed to chroot into the client's jail");
            }

            if (setgid(new_client.gid) || setuid(new_client.uid)) {
                throw exception("Cannot drop client's permissions for the given chroot jail");
            }

            char env[] = "PATH=/bin";
            putenv(env);

            this->connect(new_client.ip, new_client.reverse_shell_port, pipe);
        }

        int Connection::get_sock_fd() const noexcept {
            return sock_fd;
        }

        /*std::optional<std::reference_wrapper<Protocol::ClientData>> Connection::get_connected_client() noexcept {
            if (server_side) {
                return std::ref(connected_client);
            }
            return {};
        }*/
}