
#include <fibre/function_endpoints.hpp>
#include <fibre/closure.hpp>
#include <fibre/uuid.hpp>
#include "test_utils.hpp"

#include <random>

using namespace fibre;

volatile uint32_t called_functions = 0;

// Simple fire-and-forget function
void fn1(Context* ctx, uint32_t arg1) {
    std::cout << "fn1 called\n";
    called_functions ^= 1;
}

/*// Function that sends a reply to a well-known endpoint
void fn1(Context* ctx, uint32_t arg1) {
    std::cout << "fn2 called\n";
    called_functions ^= 2;
}*/

int main(int argc, const char** argv) {
    TestContext context;

    // Test SimplexLocalFuncEndpoint

    Context ctx;
    auto fn1_obj = make_closure(fn1);
    SimplexLocalFuncEndpoint<decltype(fn1_obj),
        std::tuple<MAKE_SSTRING("arg1")>,
        std::tuple<uint32_t>> fn1_endpoint{fn1_obj, {}};

    StreamSink* stream = fn1_endpoint.open(&ctx);
    TEST_NOT_NULL(stream);
    uint8_t encoded[] = {0x04, 0x61, 0x72, 0x67, 0x31, 0x01, 0x04, 0x61, 0x72, 0x67, 0x32, 0x02};
    size_t processed_bytes = 0;
    TEST_EQUAL(stream->process_bytes_({encoded, sizeof(encoded)}, &processed_bytes), kStreamClosed);
    TEST_EQUAL(called_functions, (uint32_t)1); // ensure function was called
    fn1_endpoint.close(stream);

    Uuid uuid{"b40a8aa3-d5ab-4453-bb4e-9bfbd7a59a9c"};
    TEST_ZERO(fibre::register_endpoint(uuid, &fn1_endpoint));


    // Test invoking a function via a memory buffer channel

    SimplexRemoteFuncEndpoint<void,
        std::tuple<MAKE_SSTRING("arg1")>,
        std::tuple<uint32_t>> fn1_remote_endpoint{uuid, {}};

    std::tuple<uint32_t> args{123};
    auto* arg_encoder = fn1_remote_endpoint.invoke(&ctx, &args);
    
    /*fibre::outgoing_call_t call = {
        .uuid{uuid_buffer},
        .ctx = nullptr,
        .encoder = CallEncoder(nullptr, uuid, arg_encoder),
        .cancel_obj{&dispose, nullptr}
    };

    uint8_t tmp_buf[128];
    auto sink = MemoryStreamSink{tmp_buf, sizeof(tmp_buf)};

    TEST_ZERO(encode_fragment(call, &sink));
    std::cout << as_hex(tmp_buf);

    auto source = MemoryStreamSource{tmp_buf, sizeof(tmp_buf) - sink.get_length()};
    TEST_ZERO(decode_fragment(nullptr, &source));
    TEST_EQUAL(called_functions, (uint32_t)0); // ensure function was called
*/
    TEST_ZERO(fibre::unregister_endpoint(uuid));

    return context.summarize();
}
