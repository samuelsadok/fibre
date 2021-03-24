
#include "autogen/interfaces.hpp"
#include <fibre/../../logging.hpp>
#include <fibre/fibre.hpp>
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

int main() {
    printf("Starting Fibre server...\n");

    bool ok = fibre::launch_event_loop([](fibre::EventLoop* event_loop) {
        printf("Hello from event loop...\n");
        auto fibre_ctx = fibre::open(event_loop);
        fibre_ctx->create_domain("tcp-server:address=localhost,port=14220");
        // auto domain =
        // fibre_ctx->create_domain("tcp-server:address=localhost,port=14220");
        // domain->publish_function();
    });

    printf("test server terminated %s\n", ok ? "nominally" : "with an error");

    return ok ? 0 : 1;
}
