
#if defined(_WIN32) || defined(_WIN64)
#include <fibre/platform_support/windows_tcp.hpp>
#endif

#if defined(__linux__)
#include <fibre/platform_support/posix_tcp.hpp>
#include <unistd.h>
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


template<typename TWorker, typename TTcpServer, typename TTcpClient>
TestContext test_impl() {
    using TTxChannel = typename TTcpServer::tx_channel_t;
    using TRxChannel = typename TTcpServer::rx_channel_t;

    TestContext context;

    int cl_conn_succeeded_cnt = 0;
    int cl_conn_failed_cnt = 0;
    int src_conn_cnt = 0;

    TTxChannel srv_tx_channel;
    TRxChannel srv_rx_channel;

    static auto client_connected_callback = make_lambda_closure(
        [&cl_conn_succeeded_cnt, &cl_conn_failed_cnt](bool success, TTcpClient& client) {
            if (success) {
                std::cout << "connected\n";
                cl_conn_succeeded_cnt++;
            } else {
                std::cout << "no success\n";
                cl_conn_failed_cnt++;
            }
        }
    );

    static auto server_connected_callback0 = make_lambda_closure(
        [&context, &src_conn_cnt](TRxChannel rx_channel, TTxChannel tx_channel) {
            src_conn_cnt++;
            TEST_ZERO(tx_channel.deinit());
            TEST_ZERO(rx_channel.deinit());
        }
    );

    static auto server_connected_callback1 = make_lambda_closure(
        [&srv_tx_channel, &srv_rx_channel, &src_conn_cnt](TRxChannel rx_channel, TTxChannel tx_channel) {
            srv_tx_channel = tx_channel;
            srv_rx_channel = rx_channel;
            src_conn_cnt++;
        }
    );

    // Server: Try open + close
    {
        TWorker worker;
        TEST_ZERO(worker.init());
        TTcpServer tcp_server;
        TEST_ZERO(tcp_server.init(kTestAddr, &worker, nullptr));
        TEST_ZERO(tcp_server.deinit());
        TEST_ZERO(worker.deinit());
    }

    // Server: Try open + reopen + close
    {
        TWorker worker;
        TEST_ZERO(worker.init());
        TTcpServer tcp_server, tcp_server2;
        TEST_ZERO(tcp_server.init(kTestAddr, &worker, nullptr));
        // This should fail with "Address already in use".
        // On Wine we might get "Unknown error", but the error code should be 10048 (WSAEADDRINUSE).
        // TODO: suppress error output
        TEST_NOT_NULL(tcp_server2.init(kTestAddr, &worker, nullptr));
        TEST_ZERO(tcp_server.deinit());
        TEST_ZERO(worker.deinit());
    }

    // Client: Try connecting (to closed port), then stop that attempt
    {
        TWorker worker;
        TEST_ZERO(worker.init());
        TTcpClient tcp_client;
        TEST_ZERO(tcp_client.start_connecting(kTestAddr, &worker, &client_connected_callback));
        TEST_ZERO(tcp_client.stop_connecting());
        TEST_EQUAL(cl_conn_failed_cnt, 1);
        TEST_EQUAL(cl_conn_succeeded_cnt, 0);
        TEST_ZERO(worker.deinit());
    }

    // Server + Client: Try connecting and disconnecting.
    {
        TWorker worker;
        TEST_ZERO(worker.init());
        TTcpServer tcp_server;
        TEST_ZERO(tcp_server.init(kTestAddr, &worker, &server_connected_callback0));
        TTcpClient tcp_client;
        TEST_ZERO(tcp_client.start_connecting(kTestAddr, &worker, &client_connected_callback));

        // Wait 1ms for connection to be established. Usually this works without
        // any wait.
        usleep(1000);

        TEST_EQUAL(cl_conn_failed_cnt, 1);
        TEST_EQUAL(cl_conn_succeeded_cnt, 1);
        TEST_EQUAL(src_conn_cnt, 1);

        TEST_ZERO(tcp_server.deinit());
        TEST_ZERO(tcp_client.stop_connecting());
        TEST_ZERO(tcp_client.tx_channel_.deinit());
        TEST_ZERO(tcp_client.rx_channel_.deinit());
        TEST_ZERO(worker.deinit());
    }

    // Server + Client: Connect, send data both ways, disconnect.
    {
        TWorker worker;
        TEST_ZERO(worker.init());
        TTcpServer tcp_server;
        TEST_ZERO(tcp_server.init(kTestAddr, &worker, &server_connected_callback1));
        TTcpClient tcp_client;
        TEST_ZERO(tcp_client.start_connecting(kTestAddr, &worker, &client_connected_callback));

        // Wait 1ms for connection to be established. Usually this works without
        // any wait.
        usleep(1000);
        
        TEST_EQUAL(cl_conn_failed_cnt, 1);
        TEST_EQUAL(cl_conn_succeeded_cnt, 2);
        TEST_EQUAL(src_conn_cnt, 2);

        TEST_ZERO(tcp_client.stop_connecting());

        TEST_ADD(test_tx(tcp_client.tx_channel_, "Hello from TCP client!"));
        usleep(1000);
        TEST_ADD(test_rx(srv_rx_channel, "Hello from TCP client!"));

        TEST_ADD(test_tx(srv_tx_channel, "Hello from TCP server!"));
        usleep(1000);
        TEST_ADD(test_rx(tcp_client.rx_channel_, "Hello from TCP server!"));

        TEST_ZERO(tcp_client.tx_channel_.deinit());
        TEST_ZERO(tcp_client.rx_channel_.deinit());
        TEST_ZERO(srv_tx_channel.deinit());
        TEST_ZERO(srv_rx_channel.deinit());

        TEST_ZERO(tcp_server.deinit());
        TEST_ZERO(worker.deinit());
    }

    // TODO: test active aspect of the TCP channels

    return context;
}


int main(int argc, const char** argv) {
    TestContext context;

#if defined(_WIN32) || defined(_WIN64)
    TEST_ADD((test_impl<WindowsUdpRxChannel, WindowsUdpTxChannel>()));
#endif

#if defined(__linux__)
    // TODO: this implementation is supposed to work more than just Linux
    TEST_ADD((test_impl<PosixSocketWorker, PosixTcpServer, PosixTcpClient>()));
#endif

    return context.summarize();
}
