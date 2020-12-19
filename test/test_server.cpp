
#include <stdio.h>
#include <unistd.h>
#include <iostream>

#include <fibre/fibre.hpp>

#include "autogen/interfaces.hpp" // TODO: remove this include

class TestClass : public TestIntf1Intf {
public:
    void func00() {
        std::cout << "func00 called" << std::endl;
    }
    uint32_t func01() {
        std::cout << "func01 called" << std::endl;
        return 123;
    }
};

TestClass test_object;

int main() {
    printf("Starting Fibre server...\n");

    bool ok = fibre::launch_event_loop([](fibre::EventLoop* event_loop) {
        printf("Hello from event loop...\n");
        auto fibre_ctx = fibre::open(event_loop);
        //fibre_ctx->create_domain("tcp-client:address=innovation-labs.appinstall.ch,port=3214");
        fibre_ctx->create_domain("tcp-server:address=localhost,port=14220");
        //fibre::publish(test_object, "tcp-server:address=localhost,port=14220");
    });

    printf("test server terminated %s\n", ok ? "nominally" : "with an error");

    return ok ? 0 : 1;
}


#include <fibre/crc.hpp>
#include <fibre/protocol.hpp>
#include "autogen/function_stubs.hpp"
TestClass& ep_root = test_object;
#include "autogen/endpoints.hpp"
