#include "../inc/Utils.hpp"
#include "../inc/remramd_exception.hpp"
#include <algorithm>
#include <filesystem>
#include <regex>

namespace remramd {
    namespace internal {
        // reads an output of ldd program
        // args:
        // @ exposed_program - program to be parsed via ldd
        const std::vector<std::string> Utils::dump_ldd_output(const std::string &exposed_program) {
            FILE* fp { popen(("ldd " + exposed_program).c_str(), "r") };
            if (!fp) {
                throw remramd::exception("Cannot invoke ldd for " + exposed_program); 
            }

            char popen_byte {};
            std::string full_ldd_output {};
            // read ldd output byte by byte
            while ((popen_byte = fgetc(fp)) != EOF) {
                full_ldd_output += popen_byte;
            }
            if (!full_ldd_output.size()) {
                pclose(fp);
                throw exception(exposed_program + " doesn't exist (or wrong path has been provided)");
            }

            std::stringstream ss(full_ldd_output);
            std::string curr_line {};
            std::vector<std::string> formatted_ldd_output {};

            while (std::getline(ss, curr_line, '\n')) {
                auto first_non_htab_char_idx { curr_line.find_first_not_of('\x09') };
                curr_line.erase(std::remove(curr_line.begin(), curr_line.begin() + first_non_htab_char_idx, '\x09'), 
                                curr_line.begin() + first_non_htab_char_idx);
                formatted_ldd_output.push_back(curr_line);
            } 

            pclose(fp);
            return formatted_ldd_output;
        }

        // parses all shared objects dependencies of a given binary
        // args:
        // @ exposed_program - binary to parse
        // return value: vector of parsed ldd data about a given binary
        std::vector<Utils::ldd_output_data> Utils::parse_so_dependencies(const std::string &exposed_program) {
            const auto ldd_output { dump_ldd_output(exposed_program) };
            // parse the given full ldd output of the given exposed program
            const std::regex lib_path_regex { ".*/.*" };
            std::smatch lib_path_smatch {};
            std::vector<ldd_output_data> so_dep_paths {};

            for (const auto &curr_line : ldd_output) {
                std::stringstream ss(curr_line);
                std::string curr_token {};

                while (std::getline(ss, curr_token, ' ')) {
                    if (std::regex_match(curr_token, lib_path_smatch, lib_path_regex)) {
                        auto last_forward_slash_idx { curr_token.find_last_of('/') };
                        so_dep_paths.push_back({ curr_token.substr(last_forward_slash_idx + 1), curr_token.substr(0, last_forward_slash_idx) });
                    }
                }
                
            }

            if (!so_dep_paths.size()) {
                throw exception("Failed to parse ldd output");
            }

            return so_dep_paths;
        }

        // populates the given client jail with all required binaries and its dependencies
        // args:
        // @ new_client - client to be jailed
        // @ jail_path - jail mount point of all jailed clients
        // return value: current client's jail path (fake root for chroot jail)
        const std::string Utils::populate_client_jail(const Protocol::ClientData &new_client, const std::string &jail_path) {
            const std::string curr_client_fakeroot_path { jail_path + '/' + new_client.ip };

            if (!std::filesystem::exists(curr_client_fakeroot_path)) {
                std::filesystem::create_directory(curr_client_fakeroot_path);
                std::cout << curr_client_fakeroot_path << " has been created\n";
            }

            // populate the client's jail directory with all relevant dependencies
            for (const auto &exposed_binary : new_client.exposed_binaries) {
                auto so_deps { internal::Utils::parse_so_dependencies(exposed_binary) };
                for (const auto &so_dependency : so_deps) {
                    const std::string destination_path { curr_client_fakeroot_path + so_dependency.second };
                    if (!std::filesystem::exists(destination_path)) {
                        std::filesystem::create_directories(destination_path);
                        std::cout << destination_path << " has been created\n";
                    }
                    const std::string source_path { so_dependency.second + '/' + so_dependency.first };
                    std::filesystem::copy(source_path, destination_path, std::filesystem::copy_options::skip_existing);
                    std::string exp_bin_dest_path { exposed_binary.substr(0, exposed_binary.find_last_of('/')) };
                    exp_bin_dest_path = curr_client_fakeroot_path + exp_bin_dest_path;
                    std::filesystem::create_directories(exp_bin_dest_path);
                    std::filesystem::copy(exposed_binary, exp_bin_dest_path, std::filesystem::copy_options::skip_existing);
                }
            }

            return curr_client_fakeroot_path;
        }
    }
}