
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fibre/fibre.hpp>
#include <fibre/udp_transport.hpp>

//#define UDP_RX_BUF_LEN	512
//#define UDP_TX_BUF_LEN	512

#define UDP_DEFAULT_ADDR "::FFFF:239.83.132.50"
#define UDP_DEFAULT_PORT 39245

// TODO: find out what happens if the packet is too big to deliver
#define UDP_MAX_TX_PACKET_SIZE 1400

DEFINE_LOG_TOPIC(UDP);
USE_LOG_TOPIC(UDP);

using namespace fibre;

namespace std {

static std::ostream& operator<<(std::ostream& stream, const struct sockaddr_in6& val) {
    char buf[128];
    if (inet_ntop(AF_INET6, &val, buf, sizeof(buf))) {
        return stream << buf;
    } else {
        return stream << "(invalid address)";
    }
}

}

size_t UDPTXChannel::get_mtu() { return UDP_MAX_TX_PACKET_SIZE; }

int UDPTXChannel::init(int socket_fd, struct sockaddr_in6 remote_addr) {
    socket_fd_ = socket_fd;
    remote_addr_ = remote_addr;
    return 0;
}

StreamSink::status_t UDPTXChannel::tx(const uint8_t* buffer, size_t length, size_t* processed_bytes) {
    // cannot send partial packets
    if (length > get_mtu()) {
        return StreamSink::TOO_LONG;
    }
    if (processed_bytes) {
        *processed_bytes += length;
    }

    int status = sendto(socket_fd_, buffer, length, 0, reinterpret_cast<struct sockaddr*>(&remote_addr_), sizeof(remote_addr_));
    return (status == -1) ? StreamSink::ERROR : StreamSink::OK;
}

int UDPRXChannel::init(Worker* worker_, int socket_fd) {
    input_channel_.init();
    worker_->register_event(socket_fd, EPOLLIN, nullptr);
    return 0;
}

int UDPRXChannel::deinit() {
    return 0;
}

struct buf_t {
    uint8_t* ptr;
    size_t length;
};

struct Context_t {

};


int sleep_for(size_t seconds) {

}

int call_function(fn_id_t fn_id, RXStream* input, TXStream* output) {

}

/*

Why not fragmented calls where the receiver reassembles fragments and gives them
as raw buffer or stream to the call handler?

 - It does not allow attaching of extension data (additional context such as log target) on a per-call basis.
 - it does not allow different reliability specs on a per-call basis NEED MORE EXAMPLES
    - transmitting audio, some arguments may need to be lossless, some not
 - May be better to allow multiple input and output streams, on per argument


What are the possible ways a user can specify a function?
 - a simple function with arguments that takes a long time <= **need threads (worker to run on)**
 - a function with arguments that takes a stream and then dequeues data from that stream, blocking as needed <= **need threads (worker to run on)**
 - a function with arguments that takes a stream and then dequeues data from that stream, failing if it's blocking >=
 - a stateful object that can be fed data <= works everywhere and everytime
 - a subscription on arriving data on a stream
 - an async function

*/

int series_of_fragmented_calls(RXStream* input) {

}

int crc32_checked(Context_t* ctx, buf_t buf) {
    buf_t crc32_buf = {
        .ptr = buf.ptr + buf.length - 4,
        .length = 4
    };

    buf.length -= 4;
    return partial_call.decode(buf);
}

using PartialCall = ;

void UDPRXChannel::rx_handler() {
    uint8_t buf[UDP_RX_BUF_LEN];
    struct sockaddr_in6 remote_addr;
    socklen_t slen = sizeof(remote_addr);
    ssize_t n_received = recvfrom(socket_fd_, buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr *>(&remote_addr), &slen);
    if (n_received < 0) {
        FIBRE_LOG(W) << "UDP read failed";
        return;
    }

    FIBRE_LOG(D) << "Received UDP packet from " << remote_addr <<
            "Data: " << buf << "\n";

    // TODO: open TX channel and register temporarily (like 5min)
    //UDPTXChannel udp_tx_channel(socket_fd, remote_addr);
    //fibre::register_output_channel(udp_tx_channel);

    // TODO: register output
    input_channel_.process_packet(buf, n_received /*, udp_packet_output*/);
}


int UDPDiscoverer::init() {
    return 0; // nothing to do
}

int UDPDiscoverer::deinit() {
    return 0; // nothing to do
}

int UDPDiscoverer::raise_effort_to_1() {
    socket1_fd_ = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

    if (socket1_fd_ < 0) {
        FIBRE_LOG(E) << "failed to open socket: " << sys_err();
        return -1;
    }

    struct sockaddr_in6 local_addr;
    memset((char *) &local_addr, 0, sizeof(local_addr));
    local_addr.sin6_family = AF_INET6;
    local_addr.sin6_port = htons(UDP_DEFAULT_PORT);
    local_addr.sin6_flowinfo = 0;
    local_addr.sin6_addr = in6addr_any;
    if (bind(socket1_fd_, reinterpret_cast<struct sockaddr *>(&local_addr), sizeof(local_addr)) != 0) {
        FIBRE_LOG(E) << "failed to bind socket: " << sys_err();
        goto fail;
    }

    return 0;

fail:
    close(socket1_fd_);
    socket1_fd_ = -1;
    return -1;
}

int UDPDiscoverer::raise_effort_to_2() {
    socket2_fd_ = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (socket2_fd_ < 0) {
        FIBRE_LOG(E) << "failed to open socket: " << sys_err();
        return -1;
    }

    struct sockaddr_in6 remote_addr;
    memset((char *) &remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin6_family = AF_INET6;
    remote_addr.sin6_port = htons(UDP_DEFAULT_PORT);
    remote_addr.sin6_flowinfo = 0;
    if (inet_pton(AF_INET6, UDP_DEFAULT_ADDR, &remote_addr.sin6_addr) != 1) {
        FIBRE_LOG(E) << "invalid IP address";
        goto fail;
    }

    if (tx_channel_.init(socket2_fd_, remote_addr) != 0) {
        goto fail;
    }

    // TODO: should we receive on the same socket? If we use the same address
    // this should already be covered by the effort level 1.

fail:
    close(socket2_fd_);
    socket2_fd_ = -1;
    return -1;
}

int UDPDiscoverer::drop_effort_from_2() {
    int result = 0;
    if (tx_channel_.deinit() != 0) {
        FIBRE_LOG(E) << "deinit failed";
        result = -1;
    }
    if (close(socket2_fd_) != 0) {
        FIBRE_LOG(E) << "close() failed: " << sys_err();
        result = -1;
    }
    socket2_fd_ = -1;
    return -1;
}

int UDPDiscoverer::drop_effort_from_1() {
    int result = 0;
    if (rx_channel_.deinit() != 0) {
        FIBRE_LOG(E) << "deinit failed";
        result = -1;
    }
    if (close(socket1_fd_) != 0) {
        FIBRE_LOG(E) << "close() failed: " << sys_err();
        result = -1;
    }
    socket1_fd_ = -1;
    return -1;

}



int serve_on_udp(unsigned int port) {
    for (;;) {
    }

    close(s);
}

