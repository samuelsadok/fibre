
#include <fibre/platform_support/windows_socket.hpp>
#include <fibre/logging.hpp>

#include <winsock2.h>
#include <ws2tcpip.h>

DEFINE_LOG_TOPIC(SOCKET);
USE_LOG_TOPIC(SOCKET);

// TODO: have the user allocate buffer space for windows sockets
#define WINDOWS_SOCKET_RX_BUFFER_SIZE 8192

using namespace fibre;

namespace std {
std::ostream& operator<<(std::ostream& stream, const struct sockaddr_storage& val) {
    CHAR buf[128];
    if (GetNameInfoA((struct sockaddr *)&val, sizeof(val), buf, sizeof(buf), NULL, 0, 0 /* flags */) == 0) {
        return stream << buf;
    } else {
        return stream << "(invalid address)";
    }
}

std::ostream& operator<<(std::ostream& stream, const sock_err&) {
    // TODO: the correct thing would be to use WSAGetLastError(), however this
    // seems to always return 0 on Wine.
    auto error_number = GetLastError();
    char buf[256];
    int rc = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error_number, 0, buf, sizeof(buf), NULL);
    if (rc) {
        return stream << buf << " (" << error_number << ")";
    } else {
        return stream << "[unknown error (" << GetLastError() << ")] (" << error_number << ")";
    }
}
}

int wsa_start() {
    WSADATA data;
    int rc = WSAStartup(MAKEWORD(2, 2), &data); // Not sure what version is needed
    if (rc) {
        FIBRE_LOG(E) << "WSAStart() failed: " << rc;
        return -1;
    }
    if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2) {
        FIBRE_LOG(E) << "incompatible winsock version (or is it?)";
        return -1;
    }
    return 0;
}

int wsa_stop() {
    if (WSACleanup() != 0) {
        FIBRE_LOG(E) << "WSACleanup() failed: " << sock_err();
        return -1;
    }
    return 0;
}

/* WindowsSocketRXChannel implementation -------------------------------------*/

int WindowsSocketRXChannel::init(int type, int protocol, struct sockaddr_storage local_addr) {
    if (wsa_start())
        return -1;

    if (socket_id_ != INVALID_SOCKET) {
        FIBRE_LOG(E) << "already initialized";
        return -1;
    }

    SOCKET socket_id = WSASocketW(local_addr.ss_family, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (socket_id == INVALID_SOCKET) {
        FIBRE_LOG(E) << "failed to open socket: " << sock_err();
        return -1;
    }

    // Make socket non-blocking
    u_long iMode = 1; // non-blocking mode
    if (ioctlsocket(socket_id, FIONBIO, &iMode)) {
        FIBRE_LOG(E) << "ioctlsocket() failed: " << sock_err();
        goto fail;
    }

    if (bind(socket_id, reinterpret_cast<struct sockaddr*>(&local_addr), sizeof(local_addr))) {
        FIBRE_LOG(E) << "failed to bind socket: " << sock_err();
        goto fail;
    }

    socket_id_ = socket_id;
    return 0;

fail:
    CloseHandle((HANDLE)socket_id);
    return -1;
}

int WindowsSocketRXChannel::init(SOCKET socket_id) {
    if (wsa_start())
        return -1;

    if (socket_id_ != INVALID_SOCKET) {
        FIBRE_LOG(E) << "already initialized";
        return -1;
    }

    // Duplicate socket ID in order to make the OS's internal ref count work
    // properly.
    HANDLE new_socket_id;
    BOOL result = DuplicateHandle(GetCurrentProcess(), (HANDLE)socket_id,
            GetCurrentProcess(), &new_socket_id,
            0, FALSE, DUPLICATE_SAME_ACCESS);

    if (!result || !new_socket_id) {
        FIBRE_LOG(E) << "DuplicateHandle() failed: " << sys_err();
        return -1;
    }

    socket_id_ = (SOCKET)new_socket_id;
    return 0;
}

int WindowsSocketRXChannel::deinit() {
    if (socket_id_ == INVALID_SOCKET) {
        FIBRE_LOG(E) << "not initialized";
        return -1;
    }

    int result = 0;
    if (!CloseHandle((HANDLE)socket_id_)) {
        FIBRE_LOG(E) << "CloseHandle() failed: " << sock_err();
        result = -1;
    }

    if (wsa_stop())
        result = -1;

    socket_id_ = INVALID_SOCKET;
    return result;
}

int WindowsSocketRXChannel::subscribe(TWorker* worker, callback_t* callback) {
    if (socket_id_ == INVALID_SOCKET) {
        FIBRE_LOG(E) << "not initialized";
        return -1;
    }
    if (worker_) {
        FIBRE_LOG(E) << "already subscribed";
        return -1;
    }

    HANDLE h_socket_id = (HANDLE)socket_id_;
    if (worker->register_object(&h_socket_id, &rx_handler_obj)) {
        FIBRE_LOG(E) << "register_object() failed";
        return -1;
    }

    socket_id_ = (SOCKET)h_socket_id;
    worker_ = worker;
    callback_ = callback;

    return 0;
}

int WindowsSocketRXChannel::unsubscribe() {
    if (!worker_) {
        FIBRE_LOG(E) << "not subscribed";
        return -1;
    }

    HANDLE h_socket_id = (HANDLE)socket_id_;
    int result = worker_->deregister_object(&h_socket_id);
    socket_id_ = (SOCKET)h_socket_id;
    return result;
}

void WindowsSocketRXChannel::rx_handler(int error_code, LPOVERLAPPED overlapped) {
    uint8_t internal_buffer[WINDOWS_SOCKET_RX_BUFFER_SIZE];
    bufptr_t bufptr = { .ptr = internal_buffer, .length = sizeof(bufptr_t) };
    status_t status = get_bytes(bufptr);
    cbufptr_t cbufptr = { .ptr = internal_buffer, .length = sizeof(bufptr_t) - bufptr.length };
    if (callback_)
        (*callback_)(status, cbufptr);
}

StreamSource::status_t WindowsSocketRXChannel::get_bytes(bufptr_t& buffer) {
    socklen_t slen = sizeof(remote_addr_);
    recv_buf_.buf = reinterpret_cast<char*>(buffer.ptr);
    recv_buf_.len = buffer.length;
    unsigned long n_received = 0;
    DWORD flags = 0;

    int rc = WSARecvFrom(
            socket_id_, &recv_buf_, 1, &n_received, &flags /* dwFlags */,
            reinterpret_cast<struct sockaddr *>(&remote_addr_), &slen,
            NULL, NULL);

    if (rc != 0) {
        if (WSAGetLastError() == WSA_IO_PENDING /* for overlapped */ || WSAGetLastError() == WSAEWOULDBLOCK /* for non-overlapped */) {
            // An overlapped operation was initiated successfully.
            return StreamSource::kBusy;
        } else {
            FIBRE_LOG(E) << "Socket read failed: " << sock_err();
            return StreamSource::kError;
        }
    }
    
    // This is unexpected and would indicate a bug in the OS.
    if (n_received > buffer.length) {
        buffer += buffer.length;
        return StreamSource::kError;
    }

    buffer += n_received;

    FIBRE_LOG(D) << "Received data from " << remote_addr_;
    return StreamSource::kOk;
}


/* WindowsSocketTXChannel implementation -------------------------------------*/

int WindowsSocketTXChannel::init(int type, int protocol, struct sockaddr_storage remote_addr) {
    if (wsa_start())
        return -1;

    if (socket_id_ != INVALID_SOCKET) {
        FIBRE_LOG(E) << "already initialized";
        return -1;
    }

    SOCKET socket_id = WSASocketW(remote_addr.ss_family, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (socket_id == INVALID_SOCKET) {
        FIBRE_LOG(E) << "failed to open socket: " << sock_err();
        return -1;
    }

    // Make socket non-blocking
    u_long iMode = 1; // non-blocking mode
    if (ioctlsocket(socket_id, FIONBIO, &iMode)) {
        FIBRE_LOG(E) << "ioctlsocket() failed: " << sock_err();
        goto fail;
    }

    socket_id_ = socket_id;
    remote_addr_ = remote_addr;
    return 0;

fail:
    CloseHandle((HANDLE)socket_id);
    return -1;
}

int WindowsSocketTXChannel::init(SOCKET socket_id, struct sockaddr_storage remote_addr) {
    if (wsa_start())
        return -1;

    if (socket_id_ != INVALID_SOCKET) {
        FIBRE_LOG(E) << "already initialized";
        return -1;
    }

    // Duplicate socket ID in order to make the OS's internal ref count work
    // properly.
    HANDLE new_socket_id;
    BOOL result = DuplicateHandle(GetCurrentProcess(), (HANDLE)socket_id,
            GetCurrentProcess(), &new_socket_id,
            0, FALSE, DUPLICATE_SAME_ACCESS);

    if (!result || !new_socket_id) {
        FIBRE_LOG(E) << "DuplicateHandle() failed: " << sys_err();
        return -1;
    }

    socket_id_ = (SOCKET)new_socket_id;
    remote_addr_ = remote_addr;
    return 0;
}

int WindowsSocketTXChannel::deinit() {
    if (socket_id_ == INVALID_SOCKET) {
        FIBRE_LOG(E) << "not initialized";
        return -1;
    }

    int result = 0;
    if (!CloseHandle((HANDLE)socket_id_)) {
        FIBRE_LOG(E) << "CloseHandle() failed: " << sock_err();
        result = -1;
    }

    if (wsa_stop())
        result = -1;

    socket_id_ = INVALID_SOCKET;
    remote_addr_ = {0};
    return result;
}

int WindowsSocketTXChannel::subscribe(TWorker* worker, callback_t* callback) {
    if (socket_id_ == INVALID_SOCKET) {
        FIBRE_LOG(E) << "not initialized";
        return -1;
    }
    if (worker_) {
        FIBRE_LOG(E) << "already subscribed";
        return -1;
    }

    HANDLE h_socket_id = (HANDLE)socket_id_;
    if (worker->register_object(&h_socket_id, &tx_handler_obj)) {
        FIBRE_LOG(E) << "register_object() failed";
        return -1;
    }

    socket_id_ = (SOCKET)h_socket_id;
    worker_ = worker;
    callback_ = callback;

    return 0;
}

int WindowsSocketTXChannel::unsubscribe() {
    if (!worker_) {
        FIBRE_LOG(E) << "not subscribed";
        return -1;
    }

    HANDLE h_socket_id = (HANDLE)socket_id_;
    int result = worker_->deregister_object(&h_socket_id);
    socket_id_ = (SOCKET)h_socket_id;
    return result;
}

void WindowsSocketTXChannel::tx_handler(int error_code, LPOVERLAPPED overlapped) {
    // TODO: distinguish between error and closed (ERROR_NO_DATA == kClosed?).
    if (callback_)
        (*callback_)(error_code == ERROR_SUCCESS ? StreamSink::kOk : StreamSink::kError);
}

StreamSink::status_t WindowsSocketTXChannel::process_bytes(cbufptr_t& buffer) {
    // TODO: if the message is too large for the underlying protocol, sendto()
    // will return EMSGSIZE. This needs some testing if this correctly detects
    // the UDP message size.
    
    // WSASendTo takes a non-const pointer. Let's just cast away the const and
    // trust that it won't modify the buffer (the documentation seems to not 
    // explicitly promise that).
    send_buf_.buf = const_cast<char*>(reinterpret_cast<const char *>(buffer.ptr));
    send_buf_.len = buffer.length;
    unsigned long n_sent = 0;

    int rc = WSASendTo(
            socket_id_, &send_buf_, 1, &n_sent, 0 /* dwFlags */,
            reinterpret_cast<struct sockaddr*>(&remote_addr_), sizeof(remote_addr_),
            &overlapped_, NULL /* lpCompletionRoutine */);

    if (rc != 0) {
        if (WSAGetLastError() == WSA_IO_PENDING) {
            // An overlapped operation was initiated successfully.
            buffer += buffer.length;
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
