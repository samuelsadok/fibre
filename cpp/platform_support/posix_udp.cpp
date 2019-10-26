
#include <fibre/platform_support/posix_udp.hpp>
#include <fibre/logging.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

DEFINE_LOG_TOPIC(UDP);
USE_LOG_TOPIC(UDP);

using namespace fibre;

/* PosixUdpRxChannel implementation ------------------------------------------*/

int PosixUdpRxChannel::open(std::tuple<std::string, int> local_address) {
    struct sockaddr_storage posix_local_addr = to_posix_socket_addr(local_address, true);
    if (posix_local_addr.ss_family) {
        return PosixSocketRXChannel::init(SOCK_DGRAM, IPPROTO_UDP, posix_local_addr);
    } else {
        return -1;
    }
}

int PosixUdpRxChannel::open(const PosixUdpTxChannel& tx_channel) {
    // TODO: add check if anything was sent yet.
    return PosixSocketRXChannel::init(tx_channel.get_socket_id());
}

int PosixUdpRxChannel::close() {
    return PosixSocketRXChannel::deinit();
}


/* PosixUdpTxChannel implementation ------------------------------------------*/

int PosixUdpTxChannel::open(std::tuple<std::string, int> remote_address) {
    struct sockaddr_storage posix_remote_addr = to_posix_socket_addr(remote_address, false);
    if (posix_remote_addr.ss_family) {
        return PosixSocketTXChannel::init(SOCK_DGRAM, IPPROTO_UDP, posix_remote_addr);
    } else {
        return -1;
    }
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
