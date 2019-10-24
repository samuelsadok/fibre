
#include <fibre/posix_udp.hpp>
#include <fibre/logging.hpp>
#include <fibre/input.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

DEFINE_LOG_TOPIC(UDP);
USE_LOG_TOPIC(UDP);

using namespace fibre;

/* PosixUdpRxChannel implementation ------------------------------------------*/

int PosixUdpRxChannel::open(std::string local_address, int local_port) {
    struct sockaddr_storage local_addr = {0};
    struct sockaddr_in6 * local_addr_in6 = reinterpret_cast<struct sockaddr_in6 *>(&local_addr);
    local_addr_in6->sin6_family = AF_INET6;
    local_addr_in6->sin6_port = htons(local_port);
    local_addr_in6->sin6_flowinfo = 0;
    if (inet_pton(AF_INET6, local_address.c_str(), &local_addr_in6->sin6_addr) != 1) {
        FIBRE_LOG(E) << "invalid IP address: " << sock_err();
        return -1;
    }

    return PosixSocketRXChannel::init(SOCK_DGRAM, IPPROTO_UDP, local_addr);
}

int PosixUdpRxChannel::open(const PosixUdpTxChannel& tx_channel) {
    // TODO: add check if anything was sent yet.
    return PosixSocketRXChannel::init(tx_channel.get_socket_id());
}

int PosixUdpRxChannel::close() {
    return PosixSocketRXChannel::deinit();
}


/* PosixUdpTxChannel implementation ------------------------------------------*/

int PosixUdpTxChannel::open(std::string remote_address, int remote_port) {
    struct sockaddr_storage remote_addr = {0};
    struct sockaddr_in6 * remote_addr_in6 = reinterpret_cast<struct sockaddr_in6 *>(&remote_addr);
    remote_addr_in6->sin6_family = AF_INET6;
    remote_addr_in6->sin6_port = htons(remote_port);
    remote_addr_in6->sin6_flowinfo = 0;
    if (inet_pton(AF_INET6, remote_address.c_str(), &remote_addr_in6->sin6_addr) != 1) {
        FIBRE_LOG(E) << "invalid IP address: " << sock_err();
        return -1;
    }

    return PosixSocketTXChannel::init(SOCK_DGRAM, IPPROTO_UDP, remote_addr);
}

int PosixUdpTxChannel::open(const PosixUdpRxChannel& rx_channel) {
    struct sockaddr_storage remote_addr = rx_channel.get_remote_address();
    if (remote_addr.ss_family != AF_INET6) {
        FIBRE_LOG(E) << "RX channel has not received anything yet";
        return -1;
    }

    return PosixSocketTXChannel::init(rx_channel.get_socket_id(), remote_addr);
}

int PosixUdpTxChannel::close() {
    return PosixSocketTXChannel::deinit();
}
