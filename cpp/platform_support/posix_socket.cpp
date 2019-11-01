
#include <fibre/platform_support/posix_socket.hpp>
#include <fibre/logging.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>

DEFINE_LOG_TOPIC(SOCKET);
USE_LOG_TOPIC(SOCKET);

// TODO: have the user allocate buffer space for posix sockets
#define POSIX_SOCKET_RX_BUFFER_SIZE 8192

using namespace fibre;

namespace std {
std::ostream& operator<<(std::ostream& stream, const struct sockaddr_storage& val) {
    char buf[128];
    if (inet_ntop(val.ss_family, &val, buf, sizeof(buf))) {
        return stream << buf;
    } else {
        return stream << "(invalid address)";
    }
}

std::ostream& operator<<(std::ostream& stream, const sock_err&) {
#if defined(_WIN32) || defined(_WIN64)
    auto error_number = WSAGetLastError();
#else
    auto error_number = errno;
#endif
    return stream << strerror(errno) << " (" << errno << ")";
}
}

struct sockaddr_storage fibre::to_posix_socket_addr(std::tuple<std::string, int> address, bool passive) {
    struct addrinfo hints = {
        .ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | (passive ? AI_PASSIVE : 0), // avoid name lookups
        .ai_family = AF_UNSPEC,
        .ai_socktype = 0, // this makes apparently no difference for numerical addresses
    };
    struct addrinfo* addr_list = nullptr;
    struct sockaddr_storage result = {0};

    // TODO: this can block during a DNS lookup, which is bad. Use getaddrinfo_a instead.
    // https://stackoverflow.com/questions/58069/how-to-use-getaddrinfo-a-to-do-async-resolve-with-glibc
    int rc = getaddrinfo(std::get<0>(address).c_str(), std::to_string(std::get<1>(address)).c_str(), &hints, &addr_list);
    if (rc == 0) {
        if (addr_list && addr_list->ai_addrlen <= sizeof(result)) {
            result = *reinterpret_cast<struct sockaddr_storage *>(addr_list->ai_addr);
        }
    } else {
        const char * errstr = gai_strerror(rc);
        FIBRE_LOG(E) << "invalid address \"" << std::get<0>(address) << "\": " << (errstr ? errstr : "[unknown error]") << " (" << rc << ")";
    }
    freeaddrinfo(addr_list);
    return result;

/*
    struct sockaddr_storage posix_addr = {0};
    struct sockaddr_in6 * posix_addr_in6 = reinterpret_cast<struct sockaddr_in6 *>(&posix_addr);
    posix_addr_in6->sin6_family = AF_INET6;
    posix_addr_in6->sin6_port = htons(std::get<1>(address));
    posix_addr_in6->sin6_flowinfo = 0;
    // TODO: use getaddrinfo() instead, which is more powerful and also allows
    // passing in hostnames (which can resolve to either IPv4 or IPv6 addresses).
    if (inet_pton(AF_INET6, std::get<0>(address).c_str(), &posix_addr_in6->sin6_addr) != 1) {
        FIBRE_LOG(E) << "invalid IP address: " << sock_err();
        return {0};
    }
    return posix_addr;
*/
}

/* PosixSocket implementation ---------------------------------------*/

int PosixSocket::init(int family, int type, int protocol) {
    if (!IS_INVALID_SOCKET(socket_id_)) {
        FIBRE_LOG(E) << "already initialized";
        return -1;
    }

    socket_id_t socket_id = socket(family, type | SOCK_NONBLOCK, protocol);
    if (IS_INVALID_SOCKET(socket_id)) {
        FIBRE_LOG(E) << "failed to open socket: " << sock_err();
        return -1;
    }

    socket_id_ = socket_id;
    return 0;
}

int PosixSocket::init(socket_id_t socket_id) {
    if (!IS_INVALID_SOCKET(socket_id_)) {
        FIBRE_LOG(E) << "already initialized";
        return -1;
    }

    // Duplicate socket ID in order to make the OS's internal ref count work
    // properly.
    socket_id = dup(socket_id);
    if (IS_INVALID_SOCKET(socket_id)) {
        FIBRE_LOG(E) << "failed to duplicate socket: " << sock_err();
        return -1;
    }

    socket_id_ = socket_id;
    return 0;
}

int PosixSocket::deinit() {
    if (IS_INVALID_SOCKET(socket_id_)) {
        FIBRE_LOG(E) << "not initialized";
        return -1;
    }

    int result = 0;
    if (::close(socket_id_)) {
        FIBRE_LOG(E) << "close() failed: " << sock_err();
        result = -1;
    }

    socket_id_ = INVALID_SOCKET;
    return result;
}

int PosixSocket::subscribe(PosixSocketWorker* worker, int events, PosixSocketWorker::callback_t* callback) {
    if (IS_INVALID_SOCKET(get_socket_id())) {
        FIBRE_LOG(E) << "not initialized";
        return -1;
    }
    if (worker_) {
        FIBRE_LOG(E) << "already subscribed";
        return -1;
    }
    if (!worker) {
        FIBRE_LOG(E) << "worker is NULL";
        return -1;
    }

    if (worker->register_event(get_socket_id(), events, callback)) {
        return -1;
    }

    worker_ = worker;
    return 0;
}

int PosixSocket::unsubscribe() {
    if (!worker_) {
        FIBRE_LOG(E) << "not subscribed";
        return -1;
    }
    int result = worker_->deregister_event(get_socket_id());
    worker_ = nullptr;
    return result;
}


/* PosixSocketRXChannel implementation ---------------------------------------*/

int PosixSocketRXChannel::init(int type, int protocol, struct sockaddr_storage local_addr) {
    if (PosixSocket::init(local_addr.ss_family, type, protocol)) {
        FIBRE_LOG(E) << "failed to open socket.";
        return -1;
    }

    if (bind(get_socket_id(), reinterpret_cast<struct sockaddr*>(&local_addr), sizeof(local_addr))) {
        FIBRE_LOG(E) << "failed to bind socket: " << sock_err();
        goto fail;
    }

    return 0;

fail:
    PosixSocket::deinit();
    return -1;
}

int PosixSocketRXChannel::deinit() {
    return PosixSocket::deinit();
}

int PosixSocketRXChannel::subscribe(PosixSocketWorker* worker, callback_t* callback) {
    callback_ = callback;
    if (PosixSocket::subscribe(worker, EPOLLIN, &rx_handler_obj)) {
        callback_ = nullptr;
        return -1;
    }
    return 0;
}

int PosixSocketRXChannel::unsubscribe() {
    int result = PosixSocket::unsubscribe();
    callback_ = nullptr;
    return result;
}

void PosixSocketRXChannel::rx_handler(uint32_t) {
    uint8_t internal_buffer[POSIX_SOCKET_RX_BUFFER_SIZE];
    bufptr_t bufptr = { .ptr = internal_buffer, .length = sizeof(internal_buffer) };
    status_t status = get_bytes(bufptr);
    cbufptr_t cbufptr = { .ptr = internal_buffer, .length = sizeof(internal_buffer) - bufptr.length };
    if (callback_)
        (*callback_)(status, cbufptr);
}

StreamSource::status_t PosixSocketRXChannel::get_bytes(bufptr_t& buffer) {
    socklen_t slen = sizeof(remote_addr_);
    ssize_t n_received = recvfrom(get_socket_id(), buffer.ptr, buffer.length, 0,
            reinterpret_cast<struct sockaddr *>(&remote_addr_), &slen);

    // If recvfrom returns -1, an errno is set to indicate the error.
    if (n_received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return StreamSource::kBusy;
        } else {
            FIBRE_LOG(E) << "Socket read failed: " << sock_err();
            buffer += buffer.length; // the function might have written to the buffer
            return StreamSource::kError;
        }
    }
    
    // If recvfrom returns 0 this can mean multiple things:
    //  1. a message of size 0 was received
    //  2. no more messages are pending and the socket was orderly closed by the
    //     peer.
    //  3. buffer.length was zero
    if (n_received == 0) {
        // Since UDP is connectionless, we know that the socket can't be closed
        // by the peer. Therefore we know that it must be reason 1. or 3. above.
        // TODO: find a way to query if the socket was closed or not, so that
        // this works for a broader class of sockets.
        return StreamSource::kOk;
    }

    // This is unexpected and would indicate a bug in the OS
    // or does it just mean that the buffer was too small? Not sure.
    if (n_received > buffer.length) {
        buffer += buffer.length;
        return StreamSource::kError;
    }

    FIBRE_LOG(D) << "Received " << n_received << " bytes from " << remote_addr_;

    buffer += n_received;
    return StreamSource::kOk;
}


/* PosixSocketTXChannel implementation ---------------------------------------*/

int PosixSocketTXChannel::init(int type, int protocol, struct sockaddr_storage remote_addr) {
    if (PosixSocket::init(remote_addr.ss_family, type, protocol)) {
        FIBRE_LOG(E) << "failed to open socket.";
        return -1;
    }

    remote_addr_ = remote_addr;
    return 0;
}

int PosixSocketTXChannel::init(socket_id_t socket_id, struct sockaddr_storage remote_addr) {
    if (PosixSocket::init(socket_id)) {
        FIBRE_LOG(E) << "failed to open socket.";
        return -1;
    }

    remote_addr_ = remote_addr;
    return 0;
}

int PosixSocketTXChannel::deinit() {
    remote_addr_ = {0};
    return PosixSocket::deinit();
}

int PosixSocketTXChannel::subscribe(PosixSocketWorker* worker, callback_t* callback) {
    callback_ = callback;
    if (PosixSocket::subscribe(worker, EPOLLOUT, &tx_handler_obj)) {
        callback_ = nullptr;
        return -1;
    }
    return 0;
}

int PosixSocketTXChannel::unsubscribe() {
    int result = PosixSocket::unsubscribe();
    callback_ = nullptr;
    return result;
}

void PosixSocketTXChannel::tx_handler(uint32_t) {
    // TODO: the uint32_t arg signifies if we were called because of a close event
    // In this case we should pass on the kClosed (or kError?) status.
    if (callback_)
        (*callback_)(StreamSink::kOk);
}

StreamSink::status_t PosixSocketTXChannel::process_bytes(cbufptr_t& buffer) {
    // TODO: if the message is too large for the underlying protocol, sendto()
    // will return EMSGSIZE. This needs some testing if this correctly detects
    // the UDP message size.
    //if (buffer.length > get_mtu()) {
    //    return StreamSink::kError;
    //}

    // TODO: if the socket is already closed, the process will receive a SIGPIPE,
    // which kills it if unhandled. That is very impolite and we should install
    // a signal handler to prevent the killing.
    int n_sent = sendto(get_socket_id(), buffer.ptr, buffer.length, 0, reinterpret_cast<struct sockaddr*>(&remote_addr_), sizeof(remote_addr_));
    if (n_sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return StreamSink::kBusy;
        } else {
            FIBRE_LOG(E) << "Socket write failed: " << sock_err();
            return StreamSink::kError;
        }
    }
    
    // This is unexpected and would indicate a bug in the OS.
    if (n_sent > buffer.length) {
        buffer += buffer.length;
        return StreamSink::kError;
    }

    buffer += n_sent;

    FIBRE_LOG(D) << "Sent " << n_sent << " bytes to " << remote_addr_;
    return StreamSink::kOk;
}
