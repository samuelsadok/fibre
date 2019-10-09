
#include <fibre/local_function.hpp>
#include <fibre/closure.hpp>
//#include <fibre/input.hpp>
#include <fibre/uuid.hpp>
#include "test_utils.hpp"

using namespace fibre;


std::tuple<> fn1(uint32_t arg1) {
    std::cout << "CALLED\n";
    return {};
}

auto name1 = make_sstring("arg1");

int main(int argc, const char** argv) {
    TestContext context;

    Context ctx;
    Uuid uuid{"b40a8aa3-d5ab-4453-bb4e-9bfbd7a59a9c"};
    auto asd = make_closure(fn1);
    SimpleLocalEndpoint<decltype(asd),
        std::tuple<decltype(name1)>,
        std::tuple<uint32_t>,
        std::tuple<>,
        std::tuple<>> fn1_endpoint{asd, {name1}, std::tuple<>()};

    TEST_ZERO(fibre::register_endpoint(uuid, &fn1_endpoint));

    StreamSink* stream = fn1_endpoint.open(&ctx);
    TEST_NOT_NULL(stream);

    uint8_t encoded[] = {0x04, 0x61, 0x72, 0x67, 0x31, 0x01, 0x04, 0x61, 0x72, 0x67, 0x32, 0x02};
    size_t processed_bytes = 0;
    TEST_EQUAL(stream->process_bytes_({encoded, sizeof(encoded)}, &processed_bytes), StreamSink::CLOSED);
    //CallDecoder call_decoder;

    fn1_endpoint.close(stream);

    TEST_ZERO(fibre::unregister_endpoint(uuid));

    return context.summarize();
}
