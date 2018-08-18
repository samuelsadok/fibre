
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

    uint32_t set_both(uint32_t arg1, uint32_t arg2) {
        property1 = arg1;
        property2 = arg2;
        return property1 + property2;
    }
};

//FIBRE_EXPORT_TYPE(float)
FIBRE_EXPORT_TYPE(TestClass,
    FIBRE_PROPERTY(property1),
    FIBRE_PROPERTY(property2)
    //FIBRE_FUNCTION(set_both, "arg1", "arg2")
);

FIBRE_EXPORT_MEMBER_FUNCTION(
    TestClass, set_both, INPUTS("obj", "arg1", "arg2"), OUTPUTS(/*"result"*/)
);

void test_func_a(uint32_t arg, uint32_t arg2) {
    printf("test_function called with 0x%08x and 0x%08x\n", arg, arg2);
}

uint32_t test_func_b(uint32_t arg, uint32_t& result) {
    printf("test_function called with %d\n", arg);
    return 8;
}

FIBRE_EXPORT_FUNCTION(
    test_func_b, INPUTS("arg"), OUTPUTS("result")
);


#define AS_TMPL(val) decltype(val), val


void test_wait_handle() {
    fibre::AutoResetEvent evt;
    evt.set();
    LOG_FIBRE(GENERAL, "waiting...");
    evt.wait();
    LOG_FIBRE(GENERAL, "done");
}

int main() {
    test_wait_handle();

    fibre::init();

    printf("Starting Fibre server...\n");

    TestClass test_object = TestClass();

//    // publish the object on Fibre
//    //auto definitions = test_object.make_fibre_definitions;
//    fibre::publish_object(test_object);

    // for testing
#if 0
    uint8_t in_buf[] = {
        0x00, 0x00, // pipe-no
        0x00, 0x00, // offset
        ((CANONICAL_CRC16_INIT >> 0) & 0xff), ((CANONICAL_CRC16_INIT >> 8) & 0xff), // crc
        0x11, 0x00, // length * 2 | packet_break
        0x00, 0x00, // endpoint id
        0x00, 0x00, // endpoint hash
        0x00, 0x00, 0x00, 0x00, // payload
        0x01, 0x02, 0x03, 0x04, // payload
        0x01, 0x00 // trailer
    };
    uint8_t out_buf[512];
    
    fibre::Uuid pseudo_remote_node_uuid = fibre::Uuid::from_string("d0dbe1f9-cba4-4a40-89f9-2f76da898746");
    fibre::RemoteNode* remote_node = fibre::get_remote_node(pseudo_remote_node_uuid);
    fibre::MemoryStreamSink output_stream(out_buf, sizeof(out_buf));
    fibre::OutputChannelFromStream output_channel(&output_stream);
    remote_node->add_output_channel(&output_channel);
    fibre::InputChannelDecoder input_decoder(remote_node);
    input_decoder.process_bytes(in_buf, sizeof(in_buf), nullptr);

    usleep(1000000 / 5);
#endif
    

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
