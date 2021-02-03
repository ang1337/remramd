#pragma once
#include "../../shared/inc/Utils.hpp"
#include "../../shared/inc/Protocol.hpp"
#include "PipeWrapper.hpp"
#include <mutex>
#include <vector>
#include <string>
#include <utility>
#include <cstdint>
#include <map>
#include <atomic>
#include <queue>
#include <optional>

namespace remramd {
    class Server {
        private:
            using clients_map_t = std::map<std::string, internal::Protocol::ClientData>;
            using conn_queue_t = std::queue<internal::Protocol::ClientData>;
            std::mutex pend_conn_q_mut;
            std::mutex clients_map_mut;
            conn_queue_t conn_q;
            clients_map_t c_map;
            std::string jail_path;
            std::atomic<bool> server_is_on, conn_recv_worker_is_on;
            enum PromptChoice : unsigned {
                SHOW_PEND_CONN = 1,
                SHOW_CURR_CLIENTS = 2,
                ACCEPT_PEND_CONN = 3,
                DECLINE_PEND_CONN = 4,
                DECLINE_ALL_CURR_CONN = 5,
                DROP_SPECIFIC_CLIENT = 6,
                DROP_ALL_CLIENTS = 7,
                EXIT = 8
            };
            const unsigned backlog;
            const std::uint16_t port;
            void connection_receiver_worker();
            void erase_jail_dir();
            void print_current_connections();
            std::optional<internal::Protocol::ClientData> obtain_pending_connection();
            void show_pending_connection();
            void display_current_clients();
            void handle_pending_connection(const bool accept);
            void decline_all_pending_connections();
            void drop_specific_client();
            void drop_all_clients();
            void enjail_new_client(internal::Protocol::ClientData &new_client, internal::PipeWrapper &pipe);
            void add_new_client(internal::Protocol::ClientData &&pend_conn);
            friend std::ostream& operator << (std::ostream &os, const internal::Protocol::ClientData &pend_conn);
            friend std::ostream& operator << (std::ostream &os, clients_map_t &clients_map);
        public:
            Server(std::string chroot_jail_path, 
                   const std::uint16_t server_port, 
                   const unsigned queued_conn_num);
            ~Server();
            Server(const Server&) = delete;
            Server& operator = (const Server&) = delete;
            Server(Server&&) noexcept = default;
            Server& operator = (Server&&) noexcept = default;
            void add_client_to_map(const std::string &ip_addr, const internal::Protocol::ClientData &&new_client) noexcept;
            void run();
    };
}