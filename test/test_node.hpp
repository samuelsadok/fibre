#ifndef __TEST_NODE_HPP
#define __TEST_NODE_HPP

#include <fibre/fibre.hpp>

namespace fibre {

struct SyncUnwrapper {
    fibre::Function* func;
    void* ctx = nullptr;
    cbufptr_t tx_bufptr;
    bufptr_t rx_bufptr;
    uint8_t tx_buf[128];
    uint8_t rx_buf[128];
    Callback<void, SyncUnwrapper*, Status, cbufptr_t> on_call_finished_;
    void call(
        bufptr_t inputs, size_t output_size,
        Callback<void, SyncUnwrapper*, Status, cbufptr_t> on_call_finished);
    std::optional<CallBuffers> on_continue(CallBufferRelease call_buffers);
};

}  // namespace fibre

struct TestNode {
    fibre::RichStatus start(fibre::EventLoop* event_loop,
                            std::string domain_path, bool enable_server,
                            bool enable_client, fibre::Logger logger);
    void on_found_object(fibre::Object* obj, fibre::Interface* intf);
    void on_lost_object(fibre::Object* obj);
    void on_finished_call(fibre::SyncUnwrapper* call, fibre::Status success,
                          fibre::cbufptr_t out);
    fibre::Logger logger_ = fibre::Logger::none();
    fibre::Domain* domain_ = nullptr;
};

#endif  // __TEST_NODE_HPP
