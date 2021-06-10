#ifndef __TEST_NODE_HPP
#define __TEST_NODE_HPP

#include <fibre/fibre.hpp>

struct TestNode {
    fibre::RichStatus start(fibre::EventLoop* event_loop,
                            const uint8_t* node_id, std::string domain_path,
                            bool enable_server, bool enable_client,
                            fibre::Logger logger);
    void on_found_object(fibre::Object* obj, fibre::Interface* intf,
                         std::string path);
    void on_lost_object(fibre::Object* obj);
    void on_finished_call(fibre::Socket* call, fibre::Status success,
                          const fibre::cbufptr_t* out, size_t n_out);
    fibre::Logger logger_ = fibre::Logger::none();
    fibre::Domain* domain_ = nullptr;
};

#endif  // __TEST_NODE_HPP
