
#include <fibre/udp_discoverer.hpp>
#include <fibre/function_endpoints.hpp>
#include "test_utils.hpp"

#include <random>
#include <unistd.h>

using namespace fibre;

volatile uint32_t fn1_called = 0;

// Simple fire-and-forget function
void fn1(Context* ctx, uint32_t arg1) {
    std::cout << "fn1 called\n";
    fn1_called += 1;
}


Uuid uuid{"b40a8aa3-d5ab-4453-bb4e-9bfbd7a59a9c"};

auto fn1_obj = make_closure(fn1);
SimplexLocalFuncEndpoint<decltype(fn1_obj),
    std::tuple<MAKE_SSTRING("arg1")>,
    std::tuple<uint32_t>> fn1_local_endpoint{fn1_obj, {}};

SimplexRemoteFuncEndpoint<void,
    std::tuple<MAKE_SSTRING("arg1")>,
    std::tuple<uint32_t>> fn1_remote_endpoint{uuid, {}};

int main(int argc, const char** argv) {
    TestContext context;

    // Export function to make it callable

    TEST_ZERO(fibre::register_endpoint(uuid, &fn1_local_endpoint));


    TEST_ZERO(main_dispatcher.init());
    {
        PosixSocketWorker worker;
        TEST_ZERO(worker.init());

        UDPDiscoverer discoverer;
        TEST_ZERO(discoverer.init(&worker));
        TEST_ZERO(discoverer.start_channel_discovery(nullptr, nullptr));


        std::tuple<uint32_t> args{123};
        
        int completed_cnt = 0;
        auto closure = make_lambda_closure([&completed_cnt](){ std::cout << "fn1 completed\n"; completed_cnt++; });
        TimedCancellationToken cancellation_token(&worker);
        TEST_ZERO(cancellation_token.init(1000));

        TEST_ZERO(fn1_remote_endpoint.invoke_async(&args, &cancellation_token, &closure));
        usleep(2000000);
        TEST_EQUAL(fn1_called, (uint32_t)1);
        TEST_EQUAL(completed_cnt, 1);

        TEST_ZERO(cancellation_token.deinit());

        TEST_ZERO(discoverer.stop_channel_discovery(nullptr));
        TEST_ZERO(discoverer.deinit());

        TEST_ZERO(worker.deinit());
    }

    TEST_ZERO(fibre::unregister_endpoint(uuid));

    return context.summarize();
}
