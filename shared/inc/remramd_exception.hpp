#pragma once
#include <exception>
#include <stdexcept>
#include <string>

namespace remramd {
    class exception : public std::exception {
        private:
            std::string _err_msg;
        public:
            exception(const std::string &err_msg);
            const char* what() const noexcept override;
    };
}