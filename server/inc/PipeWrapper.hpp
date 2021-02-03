#pragma once
#include <type_traits>
#include "../../shared/inc/remramd_exception.hpp"
#include <unistd.h>
#include <bitset>
#include <cstdint>
#include <iostream>

namespace remramd {
    namespace internal {
        class PipeWrapper {
            public:
                enum class Action {
                    READ,
                    WRITE
                };


                PipeWrapper();
                // disallow copy constructor and assignment
                PipeWrapper(const PipeWrapper&) = delete;
                PipeWrapper& operator = (const PipeWrapper&) = delete;
                // allow move constructor and operator =
                PipeWrapper(PipeWrapper&&) noexcept = default;
                PipeWrapper& operator = (PipeWrapper&&) noexcept = default;
                ~PipeWrapper();

                void close_pipe_end(const Action &io_action); // V
                int get_pipe_fd(const Action &io_action) const;

                template <typename T>
                void write(const T data) const;

                template <typename T>
                T read() const;
            private:
                // 11 - default fd_ctx_bitmap
                // 2 LSB bits are parent fd ctx
                // 2 MSB bits are child fd ctx
                // ex: 0110 - @child - read end closed, write end opened; @parent - read end opened, write end closed
                std::bitset<2> fd_ctx_bitmap;
                enum CtxBitPos : std::uint8_t {
                    WRITE_END,
                    READ_END
                };
                int pipe_fds[2];
                const bool validate_requested_fd(const Action &io_action) const;
        };

        template <typename T>
        void PipeWrapper::write(const T data) const {
            static_assert(std::is_enum<T>::value, "Only enum and enum class data type are allowed to be written into the bidirectional pipe");

            if (validate_requested_fd(Action::WRITE)) {
                if (::write(pipe_fds[1], &data, sizeof(data)) != sizeof(data)) {
                    throw exception("Cannot write data to a pipe");
                }
            } else {
                throw exception("Pipe write file descriptor is closed for this process"); 
            }

        }

        template <typename T>
        T PipeWrapper::read() const {
            static_assert(std::is_enum<T>::value, "Only enum and enum class data types are allowed to be read from the bidirectional pipe");

            if (validate_requested_fd(Action::READ)) {
                T data {};

                if (::read(pipe_fds[0], &data, sizeof(data)) != sizeof(data)) {
                    throw exception("Cannot read data from a pipe");
                }

                return data;
            }

            throw exception("Pipe read file descriptor is closed for this process");
        }
    }
}