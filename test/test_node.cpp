
#include "test_node.hpp"
#include <fibre/fibre.hpp>
#include <autogen/interfaces.hpp>
#include <algorithm>
#include <iostream>
#include <stdio.h>
#include <unistd.h>

class Subclass {
public:
    uint32_t subfunc() {
        std::cout << "subfunc called" << std::endl;
        return 321;
    }
};

class TestClass {
public:
    Subclass subobj_;

    uint32_t prop_uint32_ = 135;
    uint32_t prop_uint32_rw_ = 246;

    void func00() {
        std::cout << "func00 called" << std::endl;
    }

    uint32_t func01() {
        std::cout << "func01 called" << std::endl;
        return 123;
    }

    std::tuple<uint32_t, uint32_t> func02() {
        std::cout << "func02 called" << std::endl;
        return {456, 789};
    }

    void func10(uint32_t) {
        std::cout << "func10 called" << std::endl;
    }

    uint32_t func11(uint32_t) {
        std::cout << "func11 called" << std::endl;
        return 123;
    }

    std::tuple<uint32_t, uint32_t> func12(uint32_t) {
        std::cout << "func12 called" << std::endl;
        return {456, 789};
    }

    void func20(uint32_t, uint32_t) {
        std::cout << "func20 called" << std::endl;
    }

    uint32_t func21(uint32_t, uint32_t) {
        std::cout << "func21 called" << std::endl;
        return 123;
    }

    std::tuple<uint32_t, uint32_t> func22(uint32_t, uint32_t) {
        std::cout << "func22 called" << std::endl;
        return {456, 789};
    }
};

TestClass test_object;

TestIntf1Wrapper<TestClass> test_object_wrapper{test_object};
TestIntf1Intf* test_object_ptr = &test_object_wrapper;

using namespace fibre;

RichStatus TestNode::start(fibre::EventLoop* event_loop,
                           std::string domain_path, bool enable_server,
                           bool enable_client, fibre::Logger logger) {
    logger_ = logger;
    fibre::Context* fibre_ctx;
    F_RET_IF_ERR(fibre::open(event_loop, logger, &fibre_ctx),
                 "failed to open fibre");

    domain_ = fibre_ctx->create_domain(domain_path);

    if (enable_server) {
        // TODO: implement dynamic publishing of objects. Currently
        // objects can only be published statically.
        // domain_->publish_object<TestIntf1Intf>(test_object);
    }

    if (enable_client) {
#if FIBRE_ENABLE_CLIENT
        domain_->start_discovery(MEMBER_CB(this, on_found_object),
                                 MEMBER_CB(this, on_lost_object));
#else
        return F_MAKE_ERR("client support not compiled in");
#endif
    }

    return RichStatus::success();
}

void SyncUnwrapper::call(
    bufptr_t inputs, size_t output_size,
    Callback<void, SyncUnwrapper*, Status, cbufptr_t> on_call_finished) {
    if (inputs.size() > sizeof(tx_buf)) {
        on_call_finished.invoke(this, kFibreOutOfMemory, {});
    } else if (output_size > sizeof(rx_buf)) {
        on_call_finished.invoke(this, kFibreOutOfMemory, {});
    }
    std::copy(inputs.begin(), inputs.end(), tx_buf);

    on_call_finished_ = on_call_finished;
    tx_bufptr = {tx_buf, inputs.size()};
    rx_bufptr = {rx_buf, output_size};

    func->call(&ctx, {kFibreClosed, tx_bufptr, rx_bufptr},
               MEMBER_CB(this, on_continue));
}

std::optional<CallBuffers> SyncUnwrapper::on_continue(
    CallBufferRelease call_buffers) {
    tx_bufptr.begin() = call_buffers.tx_end;
    rx_bufptr.begin() = call_buffers.rx_end;

    if (call_buffers.status != kFibreOk) {
        on_call_finished_.invoke(this, call_buffers.status,
                                 {rx_buf, rx_bufptr.begin()});
        return std::nullopt;
    }

    return CallBuffers{kFibreClosed, tx_bufptr, rx_bufptr};
}

void TestNode::on_found_object(fibre::Object* obj, fibre::Interface* intf) {
    F_LOG_D(logger_, "discovered Object!");
    fibre::InterfaceInfo* info = intf->get_info();

    auto it = std::find_if(info->functions.begin(), info->functions.end(),
                           [](fibre::Function* func) {
                               auto info = func->get_info();
                               bool match = info->name == "func00";
                               func->free_info(info);
                               return match;
                           });

    if (it == info->functions.end()) {
        F_LOG_E(logger_, "function not found");
    } else {
        F_LOG_D(logger_, "calling func00...");

        SyncUnwrapper* call = new SyncUnwrapper{*it};
        uint8_t tx_buf[sizeof(Object*)];
        *(Object**)tx_buf = obj;
        call->call(tx_buf, 0, MEMBER_CB(this, on_finished_call));
    }

    intf->free_info(info);
}

void TestNode::on_lost_object(fibre::Object* obj) {}

void TestNode::on_finished_call(SyncUnwrapper* call, Status status,
                                cbufptr_t out) {
    F_LOG_D(logger_, "call finished");
    delete call;
}

#if STANDALONE_NODE

int main(int argc, const char** argv) {
    bool enable_server = false;
    bool enable_client = false;
    std::optional<std::string> domain_path;

    while (argv++, --argc) {
        if (std::string{*argv} == "--server") {
            enable_server = true;
        } else if (std::string{*argv} == "--client") {
            enable_client = true;
        } else if (std::string{*argv} == "--domain") {
            if (!(argv++, --argc)) {
                printf("expected domain string after --domain\n");
                return -1;
            }
            domain_path = std::string{*argv};
        } else {
            printf("invalid argument: %s\n", *argv);
            return -1;
        }
    }

    if (!domain_path.has_value()) {
        printf("domain string must be provided with --domain\n");
        return -1;
    }

    printf("Starting Fibre node...\n");

    TestNode node;
    fibre::Logger logger = fibre::Logger{{fibre::log_to_stderr, nullptr},
                                         fibre::get_log_verbosity()};

    fibre::RichStatus result =
        fibre::launch_event_loop(logger, [&](fibre::EventLoop* event_loop) {
            printf("Hello from event loop...\n");
            auto res2 = node.start(event_loop, *domain_path, enable_server,
                                   enable_client, logger);
            F_LOG_IF_ERR(logger, res2, "failed to start node");
        });

    bool failed = F_LOG_IF_ERR(logger, result, "event loop failed");

    printf("test server terminated %s\n",
           failed ? "with an error" : "nominally");

    return failed ? 1 : 0;
}

#endif
