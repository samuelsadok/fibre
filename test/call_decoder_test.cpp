
#include <fibre/local_function.hpp>
#include <fibre/closure.hpp>
//#include <fibre/input.hpp>
#include <fibre/uuid.hpp>
#include "test_utils.hpp"

using namespace fibre;

volatile uint32_t called_functions = 0;

void fn1(Context* ctx, uint32_t arg1) {
    std::cout << "CALLED\n";
    called_functions ^= 1;
}


int main(int argc, const char** argv) {
    TestContext context;

    Context ctx;
    auto fn1_obj = make_closure(fn1);
    InputOnlyLocalEndpoint<decltype(fn1_obj),
        std::tuple<MAKE_SSTRING("arg1")>,
        std::tuple<uint32_t>> fn1_endpoint{fn1_obj, {}};

    StreamSink* stream = fn1_endpoint.open(&ctx);
    TEST_NOT_NULL(stream);

    uint8_t encoded[] = {0x04, 0x61, 0x72, 0x67, 0x31, 0x01, 0x04, 0x61, 0x72, 0x67, 0x32, 0x02};
    size_t processed_bytes = 0;
    TEST_EQUAL(stream->process_bytes_({encoded, sizeof(encoded)}, &processed_bytes), StreamSink::CLOSED);
    TEST_EQUAL(called_functions, (uint32_t)1); // ensure function was called

    fn1_endpoint.close(stream);

    Uuid uuid{"b40a8aa3-d5ab-4453-bb4e-9bfbd7a59a9c"};
    TEST_ZERO(fibre::register_endpoint(uuid, &fn1_endpoint));
    TEST_ZERO(fibre::unregister_endpoint(uuid));

    return context.summarize();
}
