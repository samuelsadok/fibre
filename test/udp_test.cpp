
#if defined(_WIN32) || defined(_WIN64)
#include <fibre/platform_support/windows_udp.hpp>
#endif

#if defined(__linux__)
#include <fibre/platform_support/posix_udp.hpp>
#endif

#include <fibre/print_utils.hpp>

#include "test_utils.hpp"

using namespace fibre;

template<typename TRxChannel, typename TTxChannel>
TestContext test_impl() {
    TestContext context;

    // Server: Try open + close
    {
        TRxChannel udp_receiver;
        TEST_ZERO(udp_receiver.open("::1", 54344)); // local address
        TEST_ZERO(udp_receiver.close());
    }

    // Server: Try open + reopen + close
    {
        TRxChannel udp_receiver, udp_receiver2;
        TEST_ZERO(udp_receiver.open("::1", 54344)); // local address
        // This should fail with "Address already in use".
        // On Wine we might get "Unknown error", but the error code should be 10048 (WSAEADDRINUSE).
        // TODO: suppress error output
        TEST_NOT_NULL(udp_receiver2.open("::1", 54344)); // local address
        TEST_ZERO(udp_receiver.close());
    }

    // Client: Try open + close
    {
        TTxChannel udp_sender;
        TEST_ZERO(udp_sender.open("::1", 54344)); // remote address
        TEST_ZERO(udp_sender.close());
    }

    // Server + Client: Try to send a packet
    {
        TRxChannel udp_receiver;
        TEST_ZERO(udp_receiver.open("::1", 54344)); // local address

        // At this point, no data should be available yet.
        uint8_t recv_buf[128] = {0};
        bufptr_t bufptr = { .ptr = recv_buf, .length = sizeof(recv_buf) };
        TEST_EQUAL(udp_receiver.get_bytes(bufptr), StreamSource::kBusy);
        TEST_EQUAL(sizeof(recv_buf) - bufptr.length, (size_t)0);

        TTxChannel udp_sender;
        TEST_ZERO(udp_sender.open("::1", 54344)); // remote address
       
        std::string data = "Hello UDP!";
        cbufptr_t cbufptr = { .ptr = (const uint8_t*)data.c_str(), .length = data.size() };
        TEST_EQUAL(udp_sender.process_bytes_(cbufptr, nullptr), StreamSink::kOk);
     
        // TODO: if the following tests fail we may need to add a small delay.
        bufptr = { .ptr = recv_buf, .length = sizeof(recv_buf) };
        TEST_EQUAL(udp_receiver.get_bytes(bufptr), StreamSource::kOk); // technically a return value of "kBusy" would also comply to the specs
        TEST_EQUAL(sizeof(recv_buf) - bufptr.length, data.size());

        TEST_EQUAL(std::string((const char *)recv_buf), data);

        // Receiver should now be busy (no new data)
        TEST_EQUAL(udp_receiver.get_bytes(bufptr), StreamSource::kBusy);
        TEST_EQUAL(sizeof(recv_buf) - bufptr.length, data.size());
        
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
