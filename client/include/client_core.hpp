#pragma once 
#include <cstdint>
#include <string>

#define MIN_TCP_PORT 1024
#define MAX_TCP_PORT 49152

enum {
    SERV_DECL,
    SERV_ACC
};

uint16_t brute_valid_port();
uint16_t send_random_port(const std::string &, const uint16_t);
uint16_t wait_server_response(uint16_t);
void invoke_tcp_listener(const std::string, uint16_t, char **);

