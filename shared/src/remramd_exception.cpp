#include "../inc/remramd_exception.hpp"

namespace remramd {
    exception::exception(const std::string &err_msg) : _err_msg(err_msg) {
        _err_msg = "remramd::exception: " + _err_msg;
    }

    const char* exception::what() const noexcept {
        return _err_msg.c_str();
    }
}
