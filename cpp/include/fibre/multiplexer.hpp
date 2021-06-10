#ifndef __FIBRE_MULTIPLEXER_HPP
#define __FIBRE_MULTIPLEXER_HPP

#include <vector>

namespace fibre {

struct CBufIt;
struct FrameStreamSink;
struct TxPipe;

struct Multiplexer {
public:
    Multiplexer(FrameStreamSink* sink) : sink_{sink} {}
    void add_source(TxPipe* pipe);
    void remove_source(TxPipe* pipe);
    void maybe_send_next();
    void send_next(TxPipe* pipe);
    void on_sent(TxPipe* pipe, CBufIt end);
    void on_cancelled(TxPipe* pipe, CBufIt end);
    
    FrameStreamSink* sink_;
    std::vector<TxPipe*> queue_; // TODO: no dynamic allication
    TxPipe* sending_pipe_ = nullptr;
};

}

#endif // __FIBRE_MULTIPLEXER_HPP