
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
    struct sockaddr_in& local_addr_4 = *reinterpret_cast<struct sockaddr_in *>(&posix_local_addr);
    struct sockaddr_in6& local_addr_6 = *reinterpret_cast<struct sockaddr_in6 *>(&posix_local_addr);

    if (!posix_local_addr.ss_family) {
        return -1;
    }

    struct sockaddr_storage bind_local_addr;
    if (posix_local_addr.ss_family == AF_INET6 && (local_addr_6.sin6_addr.__in6_u.__u6_addr8[0] == 0xff)) {
        FIBRE_LOG(W) << "will bind to generic address because this is multicast";
        bind_local_addr.ss_family = AF_INET6;
        reinterpret_cast<struct sockaddr_in6 *>(&bind_local_addr)->sin6_port = reinterpret_cast<struct sockaddr_in6 *>(&posix_local_addr)->sin6_port;
    } else {
        bind_local_addr = posix_local_addr;
    }
    
    if (PosixSocketRXChannel::init(SOCK_DGRAM, IPPROTO_UDP, bind_local_addr)) {
        return -1;
    }

    // Classify address

    // Address in 224.0.0.0/4?
    bool is_ipv4_multicast = (posix_local_addr.ss_family == AF_INET) &&
                             ((local_addr_4.sin_addr.s_addr & 0xf0) == 0xe0);

    // Address in ::ffff:224.0.0.0/100?
    bool is_ipv4_over_ipv6_multicast = (posix_local_addr.ss_family == AF_INET6) &&
                             (local_addr_6.sin6_addr.__in6_u.__u6_addr16[0] == 0x0) &&
                             (local_addr_6.sin6_addr.__in6_u.__u6_addr16[1] == 0x0) &&
                             (local_addr_6.sin6_addr.__in6_u.__u6_addr16[2] == 0x0) &&
                             (local_addr_6.sin6_addr.__in6_u.__u6_addr16[3] == 0x0) &&
                             (local_addr_6.sin6_addr.__in6_u.__u6_addr16[4] == 0x0) &&
                             (local_addr_6.sin6_addr.__in6_u.__u6_addr16[5] == 0xffff) &&
                             ((local_addr_6.sin6_addr.__in6_u.__u6_addr8[12] & 0xf0) == 0xe0);
    
    // Address in ff00::/8?
    bool is_ipv6_multicast = (posix_local_addr.ss_family == AF_INET6) &&
                             (local_addr_6.sin6_addr.__in6_u.__u6_addr8[0] == 0xff);

    // Join multicast group if applicable
    if (is_ipv4_multicast || is_ipv4_over_ipv6_multicast) {
        FIBRE_LOG(D) << (is_ipv4_over_ipv6_multicast ? "IPv4 multicast over IPv6" : "IPv4 multicast");

        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = (is_ipv4_over_ipv6_multicast ?
                                     local_addr_6.sin6_addr.__in6_u.__u6_addr32[3] :
                                     local_addr_4.sin_addr.s_addr);
        mreq.imr_interface.s_addr = INADDR_ANY; /* any interface */

        if (setsockopt(get_socket_id(), SOL_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
            FIBRE_LOG(E) << "failed to add multicast membership: " << sock_err();
            goto fail;
        }

    } else if (is_ipv6_multicast) {
        FIBRE_LOG(D) << "IPv6 multicast";

        struct ipv6_mreq mreq;
        mreq.ipv6mr_multiaddr = local_addr_6.sin6_addr;
        mreq.ipv6mr_interface = INADDR_ANY; /* any interface */

        if (setsockopt(get_socket_id(), SOL_IPV6, IPV6_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
            FIBRE_LOG(E) << "failed to add multicast membership: " << sock_err();
            goto fail;
        }

    } else {
        FIBRE_LOG(D) << "not a multicast address";
    }
    
    return 0;

fail:
    PosixSocketRXChannel::deinit();
    return -1;
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
    if (!posix_remote_addr.ss_family) {
        return -1;
    }
    if (PosixSocketTXChannel::init(SOCK_DGRAM, IPPROTO_UDP, posix_remote_addr)) {
        return -1;
    }

    if (posix_remote_addr.ss_family == AF_INET) {
        u_char ttl = 3; // default is 1
        if (setsockopt(get_socket_id(), SOL_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) != 0) {
            FIBRE_LOG(E) << "failed to change multicast TTL: " << sock_err();
            goto fail;
        }
    } else if (posix_remote_addr.ss_family == AF_INET6) {
        // TODO: check if this works for IPv4 over IPv6
        int ttl = 3; // default is 1
        if (setsockopt(get_socket_id(), SOL_IPV6, IPV6_MULTICAST_HOPS, &ttl, sizeof(ttl)) != 0) {
            FIBRE_LOG(E) << "failed to change multicast TTL: " << sock_err();
            goto fail;
        }
    } else {
        FIBRE_LOG(W) << "unable to set TTL for this protocol";
    }

    return 0;

fail:
    PosixSocketTXChannel::deinit();
    return -1;
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
