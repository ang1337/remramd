#pragma once
#include <string>
#include <sys/types.h>
#include <vector>
#include <map>
#include <utility>
#include <limits>
#include <iostream>
#include <unistd.h>
#include <queue>
#include <mutex>
#include <thread>

class Server {
    private:
    // this struct contains all required client data
    struct ClientMetadata {
        std::string ip_address;
        unsigned short port;
        pid_t client_pid;
        int uid;
        int gid;
        std::string chroot_path;
        std::vector<std::string> exposed_binaries;
    };

    struct PendingClient {
        std::string ip;
        unsigned short port;
    };

    using pending_conn_queue = std::queue<PendingClient>;

    struct ConnectionQueue {
        pending_conn_queue client_queue;
        std::mutex queue_mutex;
    };
    // All connections are stored in map where key is an IP address and value is ClientMetadata struct
    using clients_map_t = std::map<std::string, ClientMetadata>;
    using clients_map_iterator = clients_map_t::iterator;
    // prompt returns pair of client map iterator and numerical response flag
    using prompt_response = std::pair<clients_map_iterator, int>;
    // prompt responses' numeric codes 
    enum {
        RQ_PEND_CONN = 1,
        RQ_INFO,
        RQ_CONN_CLIENT,
        RQ_DROP_PEND_CONN,
        RQ_DROP_CLIENT,
        RQ_EXIT
    };

    enum {
        CHLD_SUCCESS,
        CHLD_TERM,
    };

    enum {
        CONN_DENY,
        CONN_ACC
    };

    const uint16_t server_port;
    const std::string py_interp_path;
    const unsigned backlog_capacity;
    std::string chroot_jail_builder_path;
    void rq_list_pend_conn(ConnectionQueue &) const; 
    void add_pending_connection(ConnectionQueue &);
    void rq_info_handler(clients_map_t &);
    clients_map_iterator rq_conn_handler(clients_map_t &, ConnectionQueue &);
    void rq_drop_handler(clients_map_t &);
    void build_chroot_jail(ClientMetadata &, const std::string &, const std::string &);
    template <typename InputGenType>
    void process_input(InputGenType &, const std::string &&);
    prompt_response prompt(clients_map_t &, ConnectionQueue &);
    void client_process(ClientMetadata &, int, const std::string, const std::string);
    void server_process(clients_map_t &, clients_map_iterator, int, pid_t);
    void send_conn_response(int, const PendingClient &);
    public:
        Server(uint16_t, const std::string, std::string);
        ~Server() = default;
        void run();
};

// Generic input handling procedure
template <typename InputGenType>
void Server::process_input(InputGenType &var_to_process, const std::string &&prompt_str) {
    bool bad_input {};
    do {
        std::cout << prompt_str;
        if (!(std::cin >> var_to_process)) {
            bad_input = true;
            std::cerr << "Bad input, try again" << std::endl;
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            sleep(1);
        } else {
            bad_input = false;
        }
    } while (bad_input);
}
