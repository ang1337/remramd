#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/wait.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/types.h>
#include "../include/remramd_server.hpp"

Server::Server(uint16_t port, 
               const std::string py_interp_path, 
               std::string chroot_jail_builder_path) 
    : server_port(port),
      backlog_capacity(10),
      py_interp_path(py_interp_path), 
      chroot_jail_builder_path(chroot_jail_builder_path) {}

// this function will be called within a thread and will accept new clients 
void Server::add_pending_connection(ConnectionQueue &conn_queue) {
    int sock_server_fd {};
    struct sockaddr_in server_addr {};
    sock_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_server_fd == -1) {
        std::cerr << "Listening thread socket fd cannot be created" << std::endl;
        exit(EXIT_FAILURE);
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port); 
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock_server_fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr))) {
        fprintf(stderr, "Port %hu cannot be bound\n", server_port);
        exit(EXIT_FAILURE);
    }
    if (listen(sock_server_fd, backlog_capacity)) {
        std::cerr << "Listening thread cannot listen" << std::endl;
        exit(EXIT_FAILURE);
    }
    for (;;) {
        int conn_fd {};
        struct sockaddr_in conn_addr {};
        PendingClient new_conn {};
        socklen_t sin_size { sizeof(struct sockaddr_in) };
        uint16_t client_port {};
        conn_fd = accept(sock_server_fd, (struct sockaddr *)&conn_addr, &sin_size);
        read(conn_fd, &client_port, sizeof(uint16_t));
        new_conn.ip = inet_ntoa(conn_addr.sin_addr);
        new_conn.port = ntohs(client_port);
        conn_queue.queue_mutex.lock();
        fprintf(stdout, "New client connection request from [ %s | %hu ]\n", new_conn.ip.data(), new_conn.port);
        conn_queue.client_queue.push(new_conn);
        close(conn_fd);
        conn_queue.queue_mutex.unlock();
    }
}

// Drops a given client
// Arguments:
// 1) reference to map of clients (key: client IP, value: ClientMetadata struct)
void Server::rq_drop_handler(clients_map_t &clients_map) {
    std::string ip {};
    process_input<std::string>(ip, "Enter the IP address of the client to drop: ");
    auto client_iter { clients_map.find(ip) };
    if (client_iter != clients_map.cend()) {
        kill(client_iter->second.client_pid, SIGKILL);
        fprintf(stderr, "[IP: %s | port: %hu] has been successfully dropped\n", client_iter->first.data(),
                                                                                client_iter->second.port);
        system(("rm -rf " + client_iter->second.chroot_path).data());
        clients_map.erase(client_iter);
    } else {
        std::cerr << "IP address is invalid or the client has been already disconnected\n";
    }
    sleep(2);
}

// This function sends an approval or denial code message to the current pending connection
void Server::send_conn_response(int conn_resp, const PendingClient &pending_conn) {
    int resp_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in client_info {};
    client_info.sin_port = htons(pending_conn.port);
    client_info.sin_family = AF_INET;
    inet_pton(AF_INET, pending_conn.ip.data(), &client_info.sin_addr);
    connect(resp_sock_fd, (struct sockaddr *)&client_info, sizeof(client_info));
    uint16_t response = static_cast<uint16_t>(conn_resp);
    response = htons(response);
    write(resp_sock_fd, &response, sizeof(uint16_t));
    close(resp_sock_fd);
}

// Connection establishment
// Arguments:
// 1) reference to map of clients (key: client IP, value: ClientMetadata struct)
// 2) Queue of pending connections
// Return value: iterator to clients map
Server::clients_map_iterator Server::rq_conn_handler(clients_map_t &clients_map, ConnectionQueue &conn_queue) {
    ClientMetadata new_client {};
    conn_queue.queue_mutex.lock();
    auto &curr_conn { conn_queue.client_queue.front() };
    fprintf(stdout, "The client to be connected -> [ %s | %hu ]\n", curr_conn.ip.data(), curr_conn.port);
    new_client.ip_address = curr_conn.ip;
    new_client.port = curr_conn.port;
    conn_queue.client_queue.pop();
    conn_queue.queue_mutex.unlock();
    send_conn_response(CONN_ACC, {new_client.ip_address, new_client.port});
    process_input<std::string>(new_client.chroot_path, "Client's chroot jail path (DON'T use home or root directory for this, RAM disk mount point is strongly recommended): ");
    std::string &curr_chroot_path = new_client.chroot_path;
    char last_chroot_path_char { curr_chroot_path.at(curr_chroot_path.size() - 1) };
    if (last_chroot_path_char != '/') {
        curr_chroot_path.resize(curr_chroot_path.size() + 1);
        curr_chroot_path.at(curr_chroot_path.size() - 1) = '/';
    }
    process_input<int>(new_client.uid, "Client's UID (please, NOT 0): ");
    process_input<int>(new_client.gid, "Client's GID: ");
    std::cout << "Desired exposed binaries to the client (press Enter twice to stop or once to keep the default set of binaries):\n";
    // default exposed binaries for the client for minimal interactive remote shell 
    new_client.exposed_binaries.push_back("/bin/bash");
    new_client.exposed_binaries.push_back("/bin/rm");
    new_client.exposed_binaries.push_back("/bin/touch");
    new_client.exposed_binaries.push_back("/bin/cat");
    new_client.exposed_binaries.push_back("/bin/ls");
    std::cout << "Default exposed binaries: ";
    for (const auto &bin : new_client.exposed_binaries) {
        std::cout << bin << ' ';
    }
    std::cout << std::endl;
    for (;;) {
        std::string bin_path {};
        std::cout << "Provide the ABSOLUTE path to the binary to be exposed for the client: ";
        // check if there is a redundant newline in std::cin
        if (std::cin.peek() == '\n') {
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
        std::getline(std::cin, bin_path);
        if (bin_path[0] == '\0') {
            break;
        }
        if (bin_path.at(bin_path.size() - 1) == '/') {
            bin_path.resize(bin_path.size() - 1);
        }
        new_client.exposed_binaries.push_back(bin_path);
    }
    auto new_client_iter_bool_pair { clients_map.insert({ new_client.ip_address, new_client }) }; 
    // this iterator will be used in main function in order to add client's pid to the ClientMetadata struct
    return new_client_iter_bool_pair.first;
}

// Prints out data about all connected clients
// arguments:
// 1) reference to map of clients (key: client IP address, value: ClientMetadata struct)
void Server::rq_info_handler(clients_map_t &clients_map) { 
    if (clients_map.empty()) {
        std::cout << "No existing connections\n";
    } else {
        auto map_iter { clients_map.cbegin() };
        while (map_iter != clients_map.cend()) {
            if (kill(map_iter->second.client_pid, 0)) {
                system(("rm -rf " + map_iter->second.chroot_path).data());
                // post-increment allows to avoid invalid iterator usage in remaining loop
                clients_map.erase(map_iter++);
            } else {
                map_iter++;
            }
        }
        if (map_iter == clients_map.cbegin()) {
            std::cout << "No existing connections\n";
        } else {
            unsigned client_cnt {};
            // displays the information about all current clients
            for (auto map_iter { clients_map.cbegin() }; map_iter != clients_map.cend(); ++map_iter) {
                std::cout << "Client #" << ++client_cnt << '\n';
                std::cout << "IP: " << map_iter->second.ip_address << '\n';
                std::cout << "Port: " << map_iter->second.port << '\n';
                std::cout << "Chroot path: " << map_iter->second.chroot_path << '\n';
                std::cout << "UID: " << map_iter->second.uid << '\n';
                std::cout << "GID: " << map_iter->second.gid << '\n';
                std::cout << "PID: " << map_iter->second.client_pid << '\n';
                std::cout << "Exposed binaries: ";
                for (const auto &exp_bins : map_iter->second.exposed_binaries) {
                    std::cout << exp_bins << ' ';
                }
                std::cout << "\n\n";
            }
        }
    }
    sleep(2);
}

// show pending connection
// Argument: pending connections queue
void Server::rq_list_pend_conn(ConnectionQueue & conn_queue) const {
    conn_queue.queue_mutex.lock();
    if (conn_queue.client_queue.empty()) {
        std::cout << "No pending connections at this moment\n";
    } else {
        auto &conn { conn_queue.client_queue.front() };
        fprintf(stdout, "IP: %s | port %hu\n", conn.ip.data(), conn.port);
    }
    conn_queue.queue_mutex.unlock();
    sleep(2);
}

// Asks the server to choose an appropriate action to perform
// arguments: 
// 1) reference to map of clients (key: client PID, value: ClientMetadata struct)
// return value: pair of map iterator and numeric prompt response (int via enum)
// 2) pending connections queue
Server::prompt_response Server::prompt(clients_map_t &clients_map, ConnectionQueue &conn_queue) {
    prompt_response curr_response {};
    // by default, the response returns iterator to the end of the map
    // it will return iterator with actual data only via RQ_CONN_CLIENT
    curr_response.first = clients_map.end();
    std::string prompt_str { "Choose an option (1-6):\n" };
    prompt_str += "1) Show current pending connection\n";
    prompt_str += "2) List existing connections\n";
    prompt_str += "3) Connect new client\n";
    prompt_str += "4) Drop pending connection\n";
    prompt_str += "5) Drop existing connction\n";
    prompt_str += "6) Exit\n\n> ";
    int option {};
    do {
        process_input<int>(option, std::move(prompt_str));
        switch (option) {
            case RQ_PEND_CONN:
                conn_queue.queue_mutex.lock();
                if (!conn_queue.client_queue.empty()) {
                    conn_queue.queue_mutex.unlock();
                    rq_list_pend_conn(conn_queue); 
                    curr_response.second = RQ_PEND_CONN;
                } else {
                    conn_queue.queue_mutex.unlock();
                    std::cerr << "No pending connections at this moment" << std::endl;
                    sleep(2);
                }
                break;
            case RQ_INFO:
                rq_info_handler(clients_map);
                curr_response.second = RQ_INFO;
                break;
            case RQ_CONN_CLIENT:
                conn_queue.queue_mutex.lock();
                if (conn_queue.client_queue.empty()) {
                    conn_queue.queue_mutex.unlock();
                    std::cerr << "No clients to connect at this moment\n";
                    sleep(2);
                } else {
                    conn_queue.queue_mutex.unlock();
                    curr_response.first = rq_conn_handler(clients_map, conn_queue);
                    curr_response.second = RQ_CONN_CLIENT;
                }
                break;
            case RQ_DROP_PEND_CONN: {
                conn_queue.queue_mutex.lock();
                if (conn_queue.client_queue.empty()) {
                    std::cerr << "No pending connections to drop\n";
                } else {
                    auto curr_pending_conn { conn_queue.client_queue.front() };
                    conn_queue.queue_mutex.unlock();                             
                    send_conn_response(CONN_DENY, curr_pending_conn);
                    fprintf(stdout, "[ %s | %hu ] connection has been denied\n", curr_pending_conn.ip.data(), 
                                                                                 curr_pending_conn.port);
                    conn_queue.queue_mutex.lock();
                    conn_queue.client_queue.pop();
                    curr_response.second = RQ_DROP_PEND_CONN;
                }
                conn_queue.queue_mutex.unlock();
                sleep(2);
            }
                break;
            case RQ_DROP_CLIENT:
                rq_drop_handler(clients_map);
                curr_response.second = RQ_DROP_CLIENT;
                break;
            case RQ_EXIT:
                curr_response.second = RQ_EXIT;
                break;
            default:
                std::cerr << "Invalid option\n";
                sleep(2);
                break;
        }
    } while (option < RQ_INFO && option > RQ_EXIT);
    return curr_response;
}

void Server::build_chroot_jail(ClientMetadata &curr_connection_data, 
                               const std::string &python_interp, 
                               const std::string &script_path) {
    std::string client_jail_dir { curr_connection_data.chroot_path + 
                                  curr_connection_data.ip_address + ':' +
                                  std::to_string(curr_connection_data.port)};
    curr_connection_data.chroot_path = client_jail_dir;
    // make sure the chroot jail builder script is executable
    system(("chmod +x " + script_path).data());
    std::string create_cleint_jail_root_subdir { "mkdir -p " + client_jail_dir };
    system(create_cleint_jail_root_subdir.data());
    std::string bin_copy_cmd { python_interp + ' ' + script_path + ' ' };
    bin_copy_cmd += (client_jail_dir + ' ');
    for (const auto &bin : curr_connection_data.exposed_binaries) {
        bin_copy_cmd += (bin + ' ');
    }
    std::cout << "Executing: " << bin_copy_cmd << std::endl;
    system(bin_copy_cmd.data());
}

// This code runs in client process after each fork syscall
void Server::client_process(ClientMetadata &curr_connection_data, 
                            int child_write_pipe_fd,
                            const std::string python_interp,
                            const std::string script_path) {
    int curr_child_exec_status { CHLD_TERM };
    build_chroot_jail(curr_connection_data, python_interp, script_path);
    fprintf(stdout, "Changing the chroot jail owner to %d:%d\n", curr_connection_data.uid,
                                                                 curr_connection_data.gid);
    const std::string change_own { "chown -R " + 
                                   std::to_string(curr_connection_data.uid) + 
                                   ":" + std::to_string(curr_connection_data.gid) + 
                                   " " + curr_connection_data.chroot_path };
    system(change_own.data());
    // cd into root jail directory
    if (chdir(curr_connection_data.chroot_path.data())) {
        std::cerr << "Cannot cd to chroot jail directory, check paths\n";
        write(child_write_pipe_fd, &curr_child_exec_status, sizeof(int));
        exit(EXIT_FAILURE);
    }
    // chroot to jail
    if (chroot(curr_connection_data.chroot_path.data())) {
        std::cerr << "Can't chroot, check process privileges" << std::endl;
        write(child_write_pipe_fd, &curr_child_exec_status, sizeof(int));
        exit(EXIT_FAILURE);
    }
    std::cout << "The client process is jailed" << std::endl;
    // set PATH environment variable to /bin
    // it allows the client to run executables without writing its' full path
    char env[] = "PATH=/bin";
    putenv(env);
    // drop privileges from root to regular user
    fprintf(stdout, "Dropping privileges from %d:%d to %d:%d\n", getuid(), getgid(), 
                                                                 curr_connection_data.uid,
                                                                 curr_connection_data.gid);
    if (setgid(curr_connection_data.gid) || setuid(curr_connection_data.uid)) {
        std::cerr << "Cannot drop root privileges, unknown fatal error, aborting the child process\n";
        exit(EXIT_FAILURE);
    }
    std::cout << "The privileges are successfully dropped\n";
    fprintf(stdout, "Opening a reverse shell for %s:%hu\n", curr_connection_data.ip_address.data(),
                                                            curr_connection_data.port);
    // pop the chrooted non-privileged reverse shell for the client
    struct sockaddr_in sockaddr {};
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = inet_addr(curr_connection_data.ip_address.data());
    sockaddr.sin_port = htons(curr_connection_data.port);
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        std::cerr << "Cannot open socket file descriptor" << std::endl;
        write(child_write_pipe_fd, &curr_child_exec_status, sizeof(int));
        exit(EXIT_FAILURE);
    }
    int conn_status = connect(sock_fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    if (conn_status < 0) {
        fprintf(stderr, "[IP: %s | port %hu] - the port is closed\n", curr_connection_data.ip_address.data(),
                                                                      curr_connection_data.port);
        write(child_write_pipe_fd, &curr_child_exec_status, sizeof(int));
        exit(EXIT_FAILURE);
    }
    std::cout << "The client is successfully connected, redirecting the I/O streams to the socket...\n";
    dup2(sock_fd, fileno(stdin));  
    dup2(sock_fd, fileno(stdout));
    dup2(sock_fd, fileno(stderr)); 
    curr_child_exec_status = CHLD_SUCCESS;
    write(child_write_pipe_fd, &curr_child_exec_status, sizeof(int));
    // give /bin/bash shell for the client 
    // -i argument stands for "interactive"
    execl("/bin/bash", "/bin/bash", "-i", nullptr);
    exit(EXIT_FAILURE);
}

// This code runs in parent process after each fork syscall
// Waits for notifications from the child via pipe and print an appropriate message
void Server::server_process(clients_map_t &clients_map,
                            clients_map_iterator curr_conn_iter,
                            int parent_read_pipe_fd,
                            pid_t child) {
    ClientMetadata &curr_connection_data { curr_conn_iter->second };
    curr_connection_data.client_pid = child;
    std::cout << "Connecting a client, please wait...\n";
    int child_ret_status {};
    read(parent_read_pipe_fd, &child_ret_status, sizeof(int));
    // let the child execl to succeed/fail
    sleep(1);
    // if the child process returned success code, but it turns out that the process is dead -> execl failure
    if ((child_ret_status == CHLD_SUCCESS) && kill(child, 0)) {
        fprintf(stderr, "Client (PID %d) execl failure\n", child);
        system(("rm -rf " + curr_connection_data.chroot_path).data());
        clients_map.erase(curr_conn_iter);
    } else if (child_ret_status == CHLD_TERM) { // if the child return 1 -> pre-execl failure
        fprintf(stderr, "The client %s:%hu has not been connected and terminated prior to execl\n", 
                                                         curr_connection_data.ip_address.data(),
                                                         curr_connection_data.port);
        system(("rm -rf " + curr_connection_data.chroot_path).data());
        clients_map.erase(curr_conn_iter);
    } else {
        fprintf(stderr, "The client %s:%hu has been successfully connected to %s chroot jail\n", 
                                                         curr_connection_data.ip_address.data(),
                                                         curr_connection_data.port,
                                                         curr_connection_data.chroot_path.data());
    }
    sleep(2);
}

void Server::run() {
    clients_map_t clients_map {};
    prompt_response curr_response {};
    // ignore the child signals, it will prevent zombies
    // in this implementation, the parent process doesn't really need to know
    // about child exit status, it simply confirms execl success or failure
    // so wait/waitpid syscalls are not needed in this implementation
    signal(SIGCHLD, SIG_IGN);
    ConnectionQueue conn_queue {};
    std::thread listening_thread(&Server::add_pending_connection, this, std::ref(conn_queue));
    // client acceptor thread will live by it's own
    listening_thread.detach();
    while ((curr_response = prompt(clients_map, conn_queue)).second != RQ_EXIT) {
        // if there is no new connection, go back to prompt
        if (curr_response.second != RQ_CONN_CLIENT) continue;
        ClientMetadata &curr_connection_data { curr_response.first->second };
        int execl_status_pipe[2];
        // open the new pipe for each fork cycle in order to avoid pipe blocks
        // previous pipes are destroyed, so no leaks
        if (pipe(execl_status_pipe)) {
            std::cerr << "Pipe creation failure\n" << std::endl;
            continue;
        }
        // instantiate the client (child) process 
        pid_t child = fork();
        switch (child) {
            case -1:
                std::cerr << "fork failure\n" << std::endl;
                break;
            case 0: // child (client) process
                close(execl_status_pipe[0]); // child doesn't read from the pipe
                client_process(curr_connection_data, execl_status_pipe[1], py_interp_path, chroot_jail_builder_path);
                break;
            default: // parent (server) process 
                close(execl_status_pipe[1]); // parent doesn't write to the pipe
                curr_connection_data.chroot_path += (
                            curr_connection_data.ip_address + ':' +
                            std::to_string(curr_connection_data.port)
                        );
                server_process(clients_map, curr_response.first, execl_status_pipe[0], child);
            break;
        }
    }
    // be sure that all child processes are dead
    for (auto client_map_iter {clients_map.cbegin()}; client_map_iter != clients_map.cend(); ++client_map_iter) {
        kill(client_map_iter->second.client_pid, SIGKILL);
        system(("rm -rf " + client_map_iter->second.chroot_path).data());
    }
}
