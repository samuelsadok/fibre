
#if defined(_WIN32) || defined(_WIN64)
#include <fibre/platform_support/windows_udp.hpp>
#endif

#if defined(__linux__)
#include <fibre/platform_support/posix_udp.hpp>
#endif

#include <fibre/print_utils.hpp>

#include "test_utils.hpp"

using namespace fibre;

const std::tuple<std::string, int> kTestAddr = {"::1", 54344};


TestContext test_tx(StreamSink& sink, std::string str) {
    TestContext context;

    cbufptr_t cbufptr = { .ptr = (const uint8_t*)str.c_str(), .length = str.size() };
    TEST_EQUAL(sink.process_all_bytes(cbufptr), StreamSink::kOk); // technically a return value of "kBusy" would also comply to the specs
    TEST_ZERO(cbufptr.length);

    return context;
}

TestContext test_rx(StreamSource& source, std::string str) {
    TestContext context;

    uint8_t recv_buf[str.size() + 2];
    bufptr_t bufptr = { .ptr = recv_buf, .length = sizeof(recv_buf) };
    
    TEST_EQUAL(source.get_all_bytes(bufptr), StreamSource::kBusy); // technically a return value of "kOk" would also comply to the specs
    TEST_EQUAL(sizeof(recv_buf) - bufptr.length, str.size());

    TEST_EQUAL(std::string((const char *)recv_buf, str.size()), str);

    return context;
}


template<typename TRxChannel, typename TTxChannel>
TestContext test_impl() {
    TestContext context;

    // Server: Try open + close
    {
        TRxChannel udp_receiver;
        TEST_ZERO(udp_receiver.open(kTestAddr)); // local address
        TEST_ZERO(udp_receiver.close());
    }

    // Server: Try open + reopen + close
    {
        TRxChannel udp_receiver, udp_receiver2;
        TEST_ZERO(udp_receiver.open(kTestAddr)); // local address
        // This should fail with "Address already in use".
        // On Wine we might get "Unknown error", but the error code should be 10048 (WSAEADDRINUSE).
        // TODO: suppress error output
        TEST_NOT_NULL(udp_receiver2.open(kTestAddr)); // local address
        TEST_ZERO(udp_receiver.close());
    }

    // Client: Try open + close
    {
        TTxChannel udp_sender;
        TEST_ZERO(udp_sender.open(kTestAddr)); // remote address
        TEST_ZERO(udp_sender.close());
    }

    // Server + Client: Try to send a packet
    {
        TRxChannel udp_receiver;
        TEST_ZERO(udp_receiver.open(kTestAddr)); // local address

        // At this point, no data should be available yet.
        uint8_t recv_buf[128] = {0};
        bufptr_t bufptr = { .ptr = recv_buf, .length = sizeof(recv_buf) };
        TEST_EQUAL(udp_receiver.get_bytes(bufptr), StreamSource::kBusy);
        TEST_EQUAL(sizeof(recv_buf) - bufptr.length, (size_t)0);

        TTxChannel udp_sender;
        TEST_ZERO(udp_sender.open(kTestAddr)); // remote address
       
        TEST_ADD(test_tx(udp_sender, "Hello UDP!"));
        // TODO: if the following tests fail we may need to add a small delay.
        TEST_ADD(test_rx(udp_receiver, "Hello UDP!"));
        
        TEST_ZERO(udp_receiver.close());
        TEST_ZERO(udp_sender.close());
    }

    // TODO: test active aspect of the UDP channels

    return context;
}


int main(int argc, const char** argv) {
    TestContext context;

#if defined(_WIN32) || defined(_WIN64)
    TEST_ADD((test_impl<WindowsUdpRxChannel, WindowsUdpTxChannel>()));
#endif

#if defined(__linux__)
    // TODO: this implementation is supposed to work more than just Linux
    TEST_ADD((test_impl<PosixUdpRxChannel, PosixUdpTxChannel>()));
#endif

    return context.summarize();
}
