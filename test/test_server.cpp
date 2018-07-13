
#include <stdio.h>
#include <unistd.h>
#include <thread>
#include <signal.h>

#include <fibre/fibre.hpp>
#include <fibre/posix_tcp.hpp>
#include <fibre/posix_udp.hpp>


class TestClass {
public:
    float property1;
    float property2;

    float set_both(float arg1, float arg2) {
        property1 = arg1;
        property2 = arg2;
        return property1 + property2;
    }

//    FIBRE_EXPORTS(TestClass,
//        make_fibre_property("property1", &property1),
//        make_fibre_property("property2", &property2),
//        make_fibre_function("set_both", *obj, &TestClass::set_both, "arg1", "arg2")
//    );
};

FIBRE_EXPORT_TYPE(float);
FIBRE_EXPORT_TYPE(TestClass,
    FIBRE_PROPERTY(property1),
    FIBRE_PROPERTY(property2)
    //FIBRE_FUNCTION(set_both, "arg1", "arg2")
);

//FibreRefType<TestClass>* asd = new FibreRefType<TestClass>();

//template<typename TObj, typename TRet, typename ... TArgs>
//TRet invoke_function_with_tuple(TObj& obj, TRet(TObj::*func_ptr)(TArgs...), std::tuple<TArgs...> packed_args) {
//    return function_traits<TObj, TRet, TArgs...>::template invoke<0>(obj, func_ptr, packed_args);
//}

//void fibre_register_rectified_function(std::tuple<TOut...>(*func_ptr)(std::tuple<TIn...>)) {
//    FunctionPtr
//}
//
//void fibre_register<void (*Function)(int &)>() {
//    fibre_register_generic_function(call_function<Function>)
//}

void test_func_a(uint32_t arg, uint32_t arg2) {
    printf("test_function called with 0x%08x and 0x%08x\n", arg, arg2);
}
constexpr const char function_name_a[] = "test_func_a";
//constexpr const std::array<const char*,2> input_names_a {{"arg1", "arg2"}};
//constexpr const std::array<const char*,0> output_names_a {{}};
constexpr const std::tuple<const char (&)[12], const char (&)[5], const char (&)[5]> names_a("test_func_a", "arg1", "arg2");
//constexpr const std::tuple<const char (&)[2]> names_a("a");

uint32_t test_func_b(uint32_t arg) {
    printf("test_function called with %d\n", arg);
    return 8;
}


void test_wait_handle() {
    fibre::AutoResetEvent evt;
    evt.set();
    LOG_FIBRE("waiting...");
    evt.wait();
    LOG_FIBRE("done");
}

int main() {
    test_wait_handle();
    fibre::init();
    //auto a = fibre::FunctionStuff<std::tuple<uint32_t, uint32_t>, std::tuple<>, std::tuple<const char (&)[12], const char (&)[5], const char (&)[5]>>
    //    ::WithStaticNames<names_a>
    //    ::WithStaticFuncPtr<test_func_a>();
    //fibre::publish_function(&a);
    //auto b = fibre::FunctionStuff<std::tuple<uint32_t>, std::tuple<uint32_t>>::CompileTimeLocalEndpoint<test_func_b>();
    //fibre::publish_function(&b);

    printf("Starting Fibre server...\n");

    TestClass test_object = TestClass();

    // publish the object on Fibre
    //auto definitions = test_object.make_fibre_definitions;
    fibre::publish_object(test_object);

    uint8_t in_buf[] = {
        0x00, 0x00, // pipe-no
        0x00, 0x00, // offset
        ((CANONICAL_CRC16_INIT >> 0) & 0xff), ((CANONICAL_CRC16_INIT >> 8) & 0xff), // crc
        0x10, 0x00, // length * 2 | close_pipe
        0x00, 0x00, // endpoint id
        0x00, 0x00, // endpoint hash
        0x00, 0x00, 0x00, 0x00, // payload
        0x01, 0x02, 0x03, 0x04, // payload
        0x01, 0x00 // trailer
    };
    uint8_t out_buf[512];
    //fibre::MemoryStreamSink output(out_buf, sizeof(out_buf));
    //fibre::StreamBasedPacketSink packet_output(output);
    //fibre::InputChannel<5> input_channel;
    
    fibre::Uuid pseudo_remote_node_uuid = fibre::Uuid::from_string("d0dbe1f9-cba4-4a40-89f9-2f76da898746");
    fibre::RemoteNode* remote_node = fibre::get_remote_node(pseudo_remote_node_uuid);

    fibre::MemoryStreamSink output_stream(out_buf, sizeof(out_buf));
    fibre::OutputChannelFromStream output_channel(&output_stream);
    remote_node->add_output_channel(&output_channel);

    fibre::InputChannelDecoder input_decoder(remote_node);
    input_decoder.process_bytes(in_buf, sizeof(in_buf), nullptr);

    usleep(1000000 / 5);
    hexdump(out_buf, sizeof(out_buf));
    

    //using ref_type = FibreRefType<TestClass>;
    //auto asd = fibre::global_instance_of<ref_type>();

    // Expose Fibre objects on TCP and UDP
    std::thread server_thread_tcp(fibre::serve_on_tcp, 9910);
    //std::thread server_thread_udp(serve_on_udp, 9910);
    printf("Fibre server started.\n");

    // Dump property1 value
    while (1) {
        fprintf(stdout, "test_object.property1: %f\n", test_object.property1);
        usleep(1000000 / 5); // 5 Hz
    }

    return 0;
}
