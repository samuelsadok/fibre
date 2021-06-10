#ifndef __FIBRE_TX_PIPE_HPP
#define __FIBRE_TX_PIPE_HPP

namespace fibre {

struct Multiplexer;
struct BufChain;
struct CBufIt;

struct TxPipe {
    Multiplexer* multiplexer_ = nullptr;
    bool waiting_for_multiplexer_ = false;
    uintptr_t backend_slot_id;
    virtual bool has_data() = 0;
    virtual BufChain get_task() = 0;
    virtual void release_task(CBufIt end) = 0;
};

}

#endif // __FIBRE_TX_PIPE_HPP