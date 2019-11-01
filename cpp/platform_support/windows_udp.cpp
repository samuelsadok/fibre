
#include <fibre/platform_support/windows_udp.hpp>
#include <fibre/logging.hpp>

#include <winsock2.h>
#include <ws2tcpip.h>

DEFINE_LOG_TOPIC(UDP);
USE_LOG_TOPIC(UDP);

// TODO: have the user allocate buffer space for windows sockets
#define WINDOWS_SOCKET_RX_BUFFER_SIZE 8192

using namespace fibre;


/* WindowsUdpRxChannel implementation ----------------------------------------*/

int WindowsUdpRxChannel::open(std::tuple<std::string, int> local_address) {
    struct sockaddr_storage win_local_addr = to_winsock_addr(local_address);
    if (win_local_addr.ss_family) {
        // TODO: join multicast group (same as in posix_udp.cpp)
        return WindowsSocketRXChannel::init(SOCK_DGRAM, IPPROTO_UDP, win_local_addr);
    } else {
        return -1;
    }
}

int WindowsUdpRxChannel::open(const WindowsUdpTxChannel& tx_channel) {
    // TODO: add check if anything was sent yet.
    return WindowsSocketRXChannel::init(tx_channel.get_socket_id());
}

int WindowsUdpRxChannel::close() {
    return WindowsSocketRXChannel::deinit();
}


/* WindowsUdpTxChannel implementation ----------------------------------------*/

int WindowsUdpTxChannel::open(std::tuple<std::string, int> remote_address) {
    struct sockaddr_storage win_remote_addr = to_winsock_addr(remote_address);
    if (win_remote_addr.ss_family) {
        return WindowsSocketTXChannel::init(SOCK_DGRAM, IPPROTO_UDP, win_remote_addr);
    } else {
        return -1;
    }
}

int WindowsUdpTxChannel::open(const WindowsUdpRxChannel& rx_channel) {
    struct sockaddr_storage remote_addr = rx_channel.get_remote_address();
    if (remote_addr.ss_family != AF_INET6) {
        FIBRE_LOG(E) << "RX channel has not received anything yet";
        return -1;
    }

    return WindowsSocketTXChannel::init(rx_channel.get_socket_id(), remote_addr);
}

int WindowsUdpTxChannel::close() {
    return WindowsSocketTXChannel::deinit();
}
