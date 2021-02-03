#pragma once
#include "Protocol.hpp"
#include <string>
#include <sstream>
#include <iostream>
#include <vector>

namespace remramd {
    namespace internal {
        class Utils {
            private:
                static const std::vector<std::string> dump_ldd_output(const std::string &exposed_program);

            public:
                static inline const std::string menu_msg {
                    R"(1) Show pending connection)" "\n"
                    R"(2) Show current connected clients)" "\n"
                    R"(3) Accept pending connection)" "\n"
                    R"(4) Decline pending connection)" "\n"
                    R"(5) Decline all pending connections)" "\n"
                    R"(6) Drop specific client)" "\n"
                    R"(7) Drop all clients)" "\n"
                    R"(8) Exit)" "\n"
                    "\n"
                    R"(> )"
                };

                // first - library, second - directory where the library resides
                using ldd_output_data = std::pair<std::string, std::string>;

                template <typename InputType>
                static void process_input(InputType &input_var, const std::string &msg = "") {
                    if (msg.size()) {
                        std::cout << msg;
                    }
                    std::string input_str_container {};
                    std::getline(std::cin, input_str_container);
                    std::stringstream ss(input_str_container);
                    ss >> input_var;
                }

                static std::vector<ldd_output_data> parse_so_dependencies(const std::string &exposed_program);
                static const std::string populate_client_jail(const Protocol::ClientData &new_client, const std::string &jail_path);
        };
    }
}