#include "../inc/Server.hpp"
#include "../../shared/inc/remramd_exception.hpp"
#include "../../shared/inc/Connection.hpp"
#include "../inc/PipeWrapper.hpp"
#include <filesystem>
#include <type_traits>
#include <csignal>
#include <thread>
#include <iostream>
#include <chrono>
#include <unistd.h>
#include <sys/socket.h>
#include <cstdlib>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <cstring>

namespace remramd {
    // ctor args:
    // @ chroot_jail_path - chroot jail path for all clients
    // @ server_port - server's TCP port
    // @ queued_conn_num - backlog of queued pending connections
    Server::Server(const std::string chroot_jail_path, 
                   const std::uint16_t server_port, 
                   const unsigned queued_conn_num)
        : jail_path(std::move(chroot_jail_path)),
          backlog(queued_conn_num),
          port(server_port),
          server_is_on(true),
          conn_recv_worker_is_on(false) {

        // server must be run as root
        if (getuid()) {
            throw exception("Server needs to be run as root");
        }

        if (!jail_path.size()) {
            throw exception("Chroot jail path is empty");
        }

        // creates jail path for all further clients
        if (!std::filesystem::exists(jail_path)) {
            std::filesystem::create_directory(jail_path);
        }
    }

    Server::~Server() {
        drop_all_clients();
        erase_jail_dir();
    }

    std::ostream& operator << (std::ostream &os, const internal::Protocol::ClientData &pend_conn) {
        os << "IP: " << pend_conn.ip << '\n';
        return os;
    }

    // prints clients map
    // args:
    // @ os - output stream
    // @ clients_map - map of all currently jailed clients
    std::ostream& operator << (std::ostream &os, Server::clients_map_t &clients_map) {
        unsigned long clients_cnt {};

        auto c_map_iter { clients_map.cbegin() };

        while (c_map_iter != clients_map.cend()) {
            auto tmp_map_iter { c_map_iter };

            if (!kill(c_map_iter->second.pid, 0)) {
                os << ++clients_cnt << ") IP: " << c_map_iter->first << '\n'
                   << "Reverse shell port: " << c_map_iter->second.reverse_shell_port << '\n'
                   << "PID: " << c_map_iter->second.pid << '\n'
                   << "UID: " << c_map_iter->second.uid << '\n'
                   << "GID: " << c_map_iter->second.gid << '\n'
                   << "Exposed binaries:\n";
                unsigned long bin_cnt {};
                for (const auto &exposed_binary : c_map_iter->second.exposed_binaries) {
                    os << ++bin_cnt << ") " << exposed_binary << '\n';
                } 
                os << '\n';
                c_map_iter++;
            } else {
                kill(c_map_iter->second.pid, SIGKILL);
                std::filesystem::remove_all(c_map_iter->second.fakeroot_path);
                c_map_iter++;
                clients_map.erase(tmp_map_iter);
            }

        }

        return os;
    }

    // cleans the jail path from all former clients' jail directories
    void Server::erase_jail_dir() {
        for (auto &fs_iter : std::filesystem::directory_iterator(jail_path)) {
            std::filesystem::remove_all(fs_iter);
        }
    }

    // allows to input enum class related to prompt choice
    // args:
    // @ ss - current stringstream 
    // @ p_choice - enum class related to prompt choice
    std::stringstream& operator >> (std::stringstream &ss, Server::PromptChoice &p_choice) {
        std::underlying_type_t<Server::PromptChoice> val {};
        ss >> val;
        p_choice = static_cast<Server::PromptChoice>(val);
        return ss;
    }

    // this thread receives new connection requests
    void Server::connection_receiver_worker() {
        auto init_server = [this]() -> int {
            int server_fd { socket(AF_INET, SOCK_STREAM, 0) };

            if (server_fd == -1) {
                throw exception("Cannot open socket for connection receiver worker");
            }

            struct sockaddr_in serv_addr {};
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
            serv_addr.sin_port = htons(port);

            if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
                close(server_fd);
                throw exception("Cannot bind a socket to port #" + std::to_string(port));
            }

            if (listen(server_fd, backlog) != 0) {
                close(server_fd);
                throw exception("Cannot listen on a binded socket #" + std::to_string(server_fd) + " backlog: " + std::to_string(backlog));
            }

            return server_fd;
        };

        int server_fd {};

        try {

            server_fd = init_server();

        } catch (const exception &e) {
            std::cerr << e.what() << std::endl;
            exit(1);
        }

        while (conn_recv_worker_is_on.load()) {
            // listen for a new connections
            auto curr_pend_conn { internal::Protocol::wait_new_connection_request(server_fd, 10) };

            if (curr_pend_conn.has_value()) {
                // reads connection request payload (= reverse shell TCP port)
                auto reverse_shell_port { internal::Protocol::receive_data<internal::Protocol::ClientRequest>(*curr_pend_conn) };

                if (reverse_shell_port.has_value()) {
                    curr_pend_conn->reverse_shell_port = ntohs(*reverse_shell_port);
                    // add the given pending connection to the queue
                    std::lock_guard<std::mutex> lock(pend_conn_q_mut);
                    conn_q.push(std::move(*curr_pend_conn));
                }

            }

        }

        close(server_fd);
    }

    // shows current pending connection
    void Server::show_pending_connection() {
        pend_conn_q_mut.lock();
        if (conn_q.size()) {
            std::cout << conn_q.front();
        } else {
            pend_conn_q_mut.unlock();
            std::cerr << "No pending connections at this moment\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            return;
        }
        pend_conn_q_mut.unlock();
    }

    // fetches current pending connections from the queue
    std::optional<internal::Protocol::ClientData> Server::obtain_pending_connection() {
        std::lock_guard<std::mutex> lock(pend_conn_q_mut);
        if (conn_q.size()) {
            auto pend_conn { std::move(conn_q.front()) };      
            conn_q.pop();
            return pend_conn;
        }
        return {};
    }

    // displays currently jailed clients
    void Server::display_current_clients() {
        clients_map_mut.lock();
        unsigned long client_cnt {};
        if (c_map.size()) {
            std::cout << c_map;
        } else {
            clients_map_mut.unlock();
            std::cerr << "No connected clients at this moment\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            return; // prevents possible double-unlock 
        }
        clients_map_mut.unlock();
    }

    // enjails current pending connection
    // args:
    // @ new_client - pending connection to be jailed and turned into the new client
    // @ pipe - pipe for communication with parent (server) process
    void Server::enjail_new_client(internal::Protocol::ClientData &new_client, 
                                   internal::PipeWrapper &pipe) {

        try {

            internal::Connection conn_obj(new_client, pipe);

        } catch (const exception &e) {
            std::cerr << e.what() << std::endl;
            pipe.write(internal::Protocol::PipeResponse::CHILD_FAILED);
            exit(EXIT_FAILURE);
        }
    }

    // adds new client, forking it off to the separate process with different permissions
    // args:
    // @ new_client - connection to be accepted and jailed
    void Server::add_new_client(internal::Protocol::ClientData &&new_client) {
        internal::PipeWrapper pipe {};

        // ClientData struct data needs to be complemented with additional metadata (uid, gid and exposed binaries list)
        std::cout << "New client | IP: " << new_client.ip << std::endl;
        internal::Utils::process_input<uid_t>(new_client.uid, "UID: ");
        internal::Utils::process_input<gid_t>(new_client.gid, "GID: ");
        std::string exposed_binary {};
        std::cout << "Provide a full path to all binaries that need to be exposed for current client (enter 'stop' to stop):\n";

        for (;;) {
            std::string exposed_binary {};
            internal::Utils::process_input<std::string>(exposed_binary, "Enter absolute path: ");
            if (exposed_binary != "stop") {
                new_client.exposed_binaries.push_back(std::move(exposed_binary));
            } else {
                break;
            }
        }

        // fill the current client's jail with all required binaries and its dependencies
        new_client.fakeroot_path = internal::Utils::populate_client_jail(new_client, jail_path);

        // forks out a new client 
        pid_t child_pid { fork() };

        switch (child_pid) {
            case -1:
                throw exception("Fork failed");
            case 0: // child process
                pipe.close_pipe_end(internal::PipeWrapper::Action::READ);
                enjail_new_client(new_client, pipe);
                break;
            default: { // parent process
                // prevents zombie processes after SIGKILL
                std::signal(SIGCHLD,SIG_IGN);
                pipe.close_pipe_end(internal::PipeWrapper::Action::WRITE);
                // wait for a response 
                const auto child_response { pipe.read<internal::Protocol::PipeResponse>() }; 
                // sleep for 1.5 seconds to be sure that execve actually has been successful
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                // check if process is alive (will fail if execve in child process failed)
                if (!kill(child_pid, 0) && child_response == internal::Protocol::PipeResponse::CHILD_SUCCESS) {
                    new_client.pid = child_pid;
                    // adds a new successfully connected client to the clients map
                    std::lock_guard<std::mutex> lock(clients_map_mut);
                    c_map[new_client.ip] = std::move(new_client);
                } else {
                    std::cerr << "Cannot enjail a client\n";
                    std::filesystem::remove_all(new_client.fakeroot_path);
                }
                close(new_client.pend_conn_sock_fd);
            }
                break;
        }
    }

    // handle current connection according to the request
    // args:
    // @ accept - decision flag. If true - accept, else decline the current pending connection
    void Server::handle_pending_connection(const bool accept) {
        auto pend_conn { obtain_pending_connection() };
        if (pend_conn.has_value()) {
            // if the pending connection should be accepted
            if (accept) {
                clients_map_mut.lock();
                // check if such client already connected to the server
                if (c_map.find(pend_conn->ip) != c_map.end()) {
                    clients_map_mut.unlock();
                    std::cerr << "Client with IP " << pend_conn->ip << " is already connected\n";
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    return;
                }
                clients_map_mut.unlock();
                // send approval to the client
                internal::Protocol::send_data(*pend_conn, internal::Protocol::ServerResponse::YEP);
                add_new_client(std::move(*pend_conn));
            } else { // if the pending connection should be declined
                internal::Protocol::send_data(*pend_conn, internal::Protocol::ServerResponse::NOPE);
                close(pend_conn->pend_conn_sock_fd);
            }
        } else {
            std::cerr << "No pending connection at this moment\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }
    }

    // drops all pending connections
    void Server::decline_all_pending_connections() {
        std::lock_guard<std::mutex> lock(pend_conn_q_mut);
        while (!conn_q.empty()) {
            auto curr_pend_conn { std::move(conn_q.front()) };
            close(curr_pend_conn.pend_conn_sock_fd);
            conn_q.pop();
        }
    }

    // drops specific jailed client
    void Server::drop_specific_client() {
        display_current_clients();

        clients_map_mut.lock();

        if (c_map.size()) {
            clients_map_mut.unlock();
            std::string ip_addr {};
            internal::Utils::process_input<std::string>(ip_addr, "Enter the client's IP address: ");
            clients_map_mut.lock();
            clients_map_t::const_iterator found_client_iter { c_map.find(ip_addr) };

            if (found_client_iter != c_map.cend()) {
                close(found_client_iter->second.reverse_shell_port);
                kill(found_client_iter->second.pid, SIGKILL);
                std::filesystem::remove_all(jail_path + '/' + found_client_iter->second.ip);
                c_map.erase(found_client_iter);
            } else {
                clients_map_mut.unlock();
                std::cerr << "No client with such IP address\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                return; // prevents possible mutex double unlock
            }

        }

        clients_map_mut.unlock();
    }

    // drops all jailed clients
    void Server::drop_all_clients() {
        clients_map_mut.lock();

        if (c_map.size()) {
            auto map_iter { c_map.cbegin() };
            while (map_iter != c_map.cend()) {
                // close the client's socket
                close(map_iter->second.reverse_shell_port);
                kill(map_iter->second.pid, SIGKILL);
                std::filesystem::remove_all(jail_path + '/' + map_iter->second.ip);
                // save the current iterator
                auto tmp_iter { map_iter };
                // move the main iterator to the next map node
                map_iter++;
                // erase the irrelevant node via the previously saved iterator
                c_map.erase(tmp_iter);
            }
        } else {
            clients_map_mut.unlock();
            std::cerr << "No clients at this moments\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            return;
        }

        clients_map_mut.unlock();
    }

    // runs the entire server
    void Server::run() {
        // disable stdout buffering
        std::setvbuf(stdout, NULL, _IONBF, 0);

        conn_recv_worker_is_on.store(true);

        // invoke connection receiver thread
        std::thread conn_work_t(&Server::connection_receiver_worker, this);
        conn_work_t.detach();

        // every 30 seconds this thread checks if any client has disconnected. If yes, cleans up the jail of all disconnected clients
        std::thread dir_sweeper_thread([this] {

            for (;;) {
                {
                    std::lock_guard<std::mutex> lock(clients_map_mut);

                    for (const auto &client : c_map) {
                        if (kill(client.second.pid, 0)) {
                            close(client.second.reverse_shell_port);
                            std::filesystem::remove_all(client.second.fakeroot_path);
                        }
                    }

                }
                std::this_thread::sleep_for(std::chrono::seconds(30));
            }

        });

        dir_sweeper_thread.detach();

        while (server_is_on.load() && conn_recv_worker_is_on.load()) {
            PromptChoice choice {};
            internal::Utils::process_input<PromptChoice>(choice, internal::Utils::menu_msg);

            switch (choice) {
                case PromptChoice::SHOW_PEND_CONN: 
                    show_pending_connection(); 
                    break;
                case PromptChoice::SHOW_CURR_CLIENTS:
                    display_current_clients(); 
                    break;
                case PromptChoice::ACCEPT_PEND_CONN:
                    handle_pending_connection(true); 
                    break;
                case PromptChoice::DECLINE_PEND_CONN:
                    handle_pending_connection(false);
                    break;
                case PromptChoice::DECLINE_ALL_CURR_CONN:
                    decline_all_pending_connections();
                    break;
                case PromptChoice::DROP_SPECIFIC_CLIENT:
                    drop_specific_client();
                    break;
                case PromptChoice::DROP_ALL_CLIENTS:
                    drop_all_clients();
                    break;
                case PromptChoice::EXIT:
                    server_is_on.store(false);
                    conn_recv_worker_is_on.store(false);
                    break;
                default:
                    std::cout << "Invalid option\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                    break;
            } 

        }

    }
}