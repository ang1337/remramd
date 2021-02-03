#include "../inc/PipeWrapper.hpp"
#include "../../shared/inc/remramd_exception.hpp"
#include <unistd.h>
#include <iostream>

namespace remramd {
    namespace internal {
        PipeWrapper::PipeWrapper() : fd_ctx_bitmap(11) {
            if (pipe(pipe_fds)) {
                throw exception("Cannot create parent_to_child pipe");
            }
        }

        PipeWrapper::~PipeWrapper() {
            close(pipe_fds[0]);
            close(pipe_fds[1]);
        }

        int PipeWrapper::get_pipe_fd(const Action &io_action) const {
            if (validate_requested_fd(io_action)) {
                return io_action == Action::READ ? pipe_fds[0] : pipe_fds[1]; 
            } 

            throw exception("Invalid pipe file descriptor");
        }

        void PipeWrapper::close_pipe_end(const Action &io_action) { // V
            if (io_action == Action::READ) {
                close(pipe_fds[0]);
                fd_ctx_bitmap &= ~(1 << CtxBitPos::READ_END);
            } else {
                close(pipe_fds[1]);
                fd_ctx_bitmap &= ~(1 << CtxBitPos::WRITE_END);
            }
        }

        const bool PipeWrapper::validate_requested_fd(const Action &io_action) const {
            if (fd_ctx_bitmap == 11) {
                throw exception("Close the unnecessary pipe side before usage");
            }

            return io_action == Action::READ ? static_cast<bool>((fd_ctx_bitmap >> CtxBitPos::READ_END).to_ulong() & 0x1) :
                                               static_cast<bool>((fd_ctx_bitmap >> CtxBitPos::WRITE_END).to_ulong() & 0x1);
        }
    }
}