
#include <fibre/windows_udp.hpp>
#include <fibre/logging.hpp>
#include <fibre/input.hpp>

#include <winsock2.h>
#include <ws2tcpip.h>

DEFINE_LOG_TOPIC(UDP);
USE_LOG_TOPIC(UDP);

// TODO: have the user allocate buffer space for windows sockets
#define WINDOWS_SOCKET_RX_BUFFER_SIZE 8192

using namespace fibre;


/* WindowsUdpRxChannel implementation ----------------------------------------*/

int WindowsUdpRxChannel::open(std::string local_address, int local_port) {
    // TODO: this is equivalent in WindowsUdpTxChannel::open(). Move to function.
    struct sockaddr_storage local_addr = {0};
    struct sockaddr_in6 * local_addr_in6 = reinterpret_cast<struct sockaddr_in6 *>(&local_addr);
    local_addr_in6->sin6_family = AF_INET6;
    local_addr_in6->sin6_port = htons(local_port);
    local_addr_in6->sin6_flowinfo = 0;

#if _WIN32_WINNT < _WIN32_WINNT_VISTA
#error "InetPtonA not supported on Windows Vista or lower"
#endif

    if (InetPtonA(AF_INET6, local_address.c_str(), &local_addr_in6->sin6_addr) != 1) {
        FIBRE_LOG(E) << "invalid IP address: " << sock_err();
        return -1;
    }

    return WindowsSocketRXChannel::init(SOCK_DGRAM, IPPROTO_UDP, local_addr);
}

int WindowsUdpRxChannel::open(const WindowsUdpTxChannel& tx_channel) {
    // TODO: add check if anything was sent yet.
    return WindowsSocketRXChannel::init(tx_channel.get_socket_id());
}

int WindowsUdpRxChannel::close() {
    return WindowsSocketRXChannel::deinit();
}


/* WindowsUdpTxChannel implementation ----------------------------------------*/

int WindowsUdpTxChannel::open(std::string remote_address, int remote_port) {
    struct sockaddr_storage remote_addr = {0};
    struct sockaddr_in6 * remote_addr_in6 = reinterpret_cast<struct sockaddr_in6 *>(&remote_addr);
    remote_addr_in6->sin6_family = AF_INET6;
    remote_addr_in6->sin6_port = htons(remote_port);
    remote_addr_in6->sin6_flowinfo = 0;

#if _WIN32_WINNT < _WIN32_WINNT_VISTA
#error "InetPtonA not supported on Windows Vista or lower"
#endif

    if (InetPtonA(AF_INET6, remote_address.c_str(), &remote_addr_in6->sin6_addr) != 1) {
        FIBRE_LOG(E) << "invalid IP address: " << sock_err();
        return -1;
    }

    return WindowsSocketTXChannel::init(SOCK_DGRAM, IPPROTO_UDP, remote_addr);
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
