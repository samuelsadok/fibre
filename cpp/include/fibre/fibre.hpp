#ifndef __FIBRE_HPP
#define __FIBRE_HPP

#include <fibre/callback.hpp>
#include <fibre/libfibre.h>

namespace fibre {

struct CallBuffers {
    FibreStatus status;
    cbufptr_t tx_buf;
    bufptr_t rx_buf;
};

struct CallBufferRelease {
    FibreStatus status;
    const uint8_t* tx_end;
    uint8_t* rx_end;
};

struct Function {
    virtual std::optional<CallBufferRelease>
    call(void**, CallBuffers, Callback<std::optional<CallBuffers>, CallBufferRelease>) = 0;
};

}

#endif // __FIBRE_HPP