
#include <fibre/platform_support/posix_socket.hpp>
#include <fibre/logging.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

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


/* PosixSocketRXChannel implementation ---------------------------------------*/

int PosixSocketRXChannel::init(int type, int protocol, struct sockaddr_storage local_addr) {
    if (!IS_INVALID_SOCKET(socket_id_)) {
        FIBRE_LOG(E) << "already initialized";
        return -1;
    }

    socket_id_t socket_id = socket(local_addr.ss_family, type | SOCK_NONBLOCK, protocol);
    if (IS_INVALID_SOCKET(socket_id)) {
        FIBRE_LOG(E) << "failed to open socket: " << sock_err();
        return -1;
    }

    if (bind(socket_id, reinterpret_cast<struct sockaddr*>(&local_addr), sizeof(local_addr))) {
        FIBRE_LOG(E) << "failed to bind socket: " << sock_err();
        goto fail;
    }

    socket_id_ = socket_id;
    return 0;

fail:
    close(socket_id);
    return -1;
}

int PosixSocketRXChannel::init(socket_id_t socket_id) {
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

int PosixSocketRXChannel::deinit() {
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

int PosixSocketRXChannel::subscribe(TWorker* worker, callback_t* callback) {
    if (IS_INVALID_SOCKET(socket_id_)) {
        FIBRE_LOG(E) << "not initialized";
        return -1;
    }
    if (worker_) {
        FIBRE_LOG(E) << "already subscribed";
        return -1;
    }

    if (worker_->register_event(socket_id_, EPOLLIN, &rx_handler_obj)) {
        return -1;
    }

    worker_ = worker;
    callback_ = callback;
    return 0;
}

int PosixSocketRXChannel::unsubscribe() {
    if (!worker_) {
        FIBRE_LOG(E) << "not subscribed";
        return -1;
    }
    int result = worker_->deregister_event(socket_id_);
    socket_id_ = INVALID_SOCKET;
    return result;
}

void PosixSocketRXChannel::rx_handler(uint32_t) {
    uint8_t internal_buffer[POSIX_SOCKET_RX_BUFFER_SIZE];
    bufptr_t bufptr = { .ptr = internal_buffer, .length = sizeof(bufptr_t) };
    status_t status = get_bytes(bufptr);
    cbufptr_t cbufptr = { .ptr = internal_buffer, .length = sizeof(bufptr_t) - bufptr.length };
    if (callback_)
        (*callback_)(status, cbufptr);
}

StreamSource::status_t PosixSocketRXChannel::get_bytes(bufptr_t& buffer) {
    socklen_t slen = sizeof(remote_addr_);
    ssize_t n_received = recvfrom(socket_id_, buffer.ptr, buffer.length, 0,
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

    FIBRE_LOG(D) << "Received data from " << remote_addr_;

    buffer += n_received;
    return StreamSource::kOk;
}


/* PosixSocketTXChannel implementation ---------------------------------------*/

int PosixSocketTXChannel::init(int type, int protocol, struct sockaddr_storage remote_addr) {
    if (!IS_INVALID_SOCKET(socket_id_)) {
        FIBRE_LOG(E) << "already initialized";
        return -1;
    }

    int socket_id = socket(remote_addr.ss_family, type | SOCK_NONBLOCK, protocol);
    if (IS_INVALID_SOCKET(socket_id)) {
        FIBRE_LOG(E) << "failed to open socket: " << sock_err();
        return -1;
    }

    socket_id_ = socket_id;
    remote_addr_ = remote_addr;
    return 0;
}

int PosixSocketTXChannel::init(socket_id_t socket_id, struct sockaddr_storage remote_addr) {
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
    remote_addr_ = remote_addr;
    return 0;
}

int PosixSocketTXChannel::deinit() {
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
    remote_addr_ = {0};
    return result;
}

int PosixSocketTXChannel::subscribe(TWorker* worker, callback_t* callback) {
    if (IS_INVALID_SOCKET(socket_id_)) {
        FIBRE_LOG(E) << "not initialized";
        return -1;
    }
    if (worker_) {
        FIBRE_LOG(E) << "already subscribed";
        return -1;
    }

    if (worker_->register_event(socket_id_, EPOLLOUT, &tx_handler_obj)) {
        return -1;
    }

    worker_ = worker;
    callback_ = callback;
    return 0;
}

int PosixSocketTXChannel::unsubscribe() {
    if (!worker_) {
        FIBRE_LOG(E) << "not subscribed";
        return -1;
    }
    int result = worker_->deregister_event(socket_id_);
    socket_id_ = INVALID_SOCKET;
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
    int n_sent = sendto(socket_id_, buffer.ptr, buffer.length, 0, reinterpret_cast<struct sockaddr*>(&remote_addr_), sizeof(remote_addr_));
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

    FIBRE_LOG(D) << "Sent data to " << remote_addr_;
    return StreamSink::kOk;
}
