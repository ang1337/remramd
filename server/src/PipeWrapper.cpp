#include "../inc/PipeWrapper.hpp"
#include "../../shared/inc/remramd_exception.hpp"
#include <unistd.h>
#include <iostream>

namespace remramd {
    namespace internal {
        // PipeWrapper ctor, initialized a file descriptor context bitmap and opens a pipe
        PipeWrapper::PipeWrapper() : fd_ctx_bitmap(11) {
            if (pipe(pipe_fds)) {
                throw exception("Cannot create parent_to_child pipe");
            }
        }

        PipeWrapper::~PipeWrapper() {
            close(pipe_fds[0]);
            close(pipe_fds[1]);
        }

        // closes a given pipe end and alters the file descriptor context bitmap accordingly
        // args:
        // @ io_action - read or write pipe end identifier
        void PipeWrapper::close_pipe_end(const Action &io_action) { 
            if (io_action == Action::READ) {
                close(pipe_fds[0]);
                fd_ctx_bitmap &= ~(1 << CtxBitPos::READ_END);
            } else {
                close(pipe_fds[1]);
                fd_ctx_bitmap &= ~(1 << CtxBitPos::WRITE_END);
            }
        }

        // validates the given file descriptor, returns FALSE if it's closed 
        const bool PipeWrapper::validate_requested_fd(const Action &io_action) const {
            if (fd_ctx_bitmap == 11) {
                throw exception("Close the unnecessary pipe side before usage");
            }

            return io_action == Action::READ ? static_cast<bool>((fd_ctx_bitmap >> CtxBitPos::READ_END).to_ulong() & 0x1) :
                                               static_cast<bool>((fd_ctx_bitmap >> CtxBitPos::WRITE_END).to_ulong() & 0x1);
        }
    }
}