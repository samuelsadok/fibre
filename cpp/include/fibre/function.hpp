#ifndef __FIBRE_FUNCTION_HPP
#define __FIBRE_FUNCTION_HPP

#include <fibre/bufptr.hpp>
#include <fibre/callback.hpp>
#include <fibre/status.hpp>
#include <fibre/backport/optional.hpp>
#include <string>
#include <vector>

namespace fibre {

class Domain;
class Socket;

struct CallBuffers {
    Status status;
    cbufptr_t tx_buf;
    bufptr_t rx_buf;
};

struct CallBufferRelease {
    Status status;
    const uint8_t* tx_end;
    uint8_t* rx_end;
};

struct FunctionInfo {
    std::string name;
    std::vector<std::tuple<std::string, std::string>> inputs;
    std::vector<std::tuple<std::string, std::string>> outputs;
};

class Function {
public:
    Function() {}
    Function(const Function&) =
        delete;  // functions must not move around in memory

    /**
     * @brief Starts a call on this function.
     * 
     * The call is ended when it is closed in both directions.
     * 
     * @param domain: The domain on which the call is made.
     * @param call_frame: A buffer where the implementation can store the call
     *        state in case heap allocation is forbidden (FIBRE_ALLOW_HEAP == 0).
     *        The buffer must be be aligned on `std::max_align_t` and not move
     *        around or change size during an ongoing call.
     *        The buffer must remain valid until the call is ended.
     * @returns A duplex channel for the call.
     */
    virtual Socket* start_call(
        Domain* domain, bufptr_t call_frame, Socket* caller) const = 0;

    virtual FunctionInfo* get_info() const = 0;
    virtual void free_info(FunctionInfo* info) const = 0;
};

}

#endif // __FIBRE_FUNCTION_HPP
