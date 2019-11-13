
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

struct sockaddr_storage fibre::to_winsock_addr(std::tuple<std::string, int> address) {
    struct addrinfo hints = {
        .ai_flags = AI_NUMERICHOST | AI_NUMERICSERV, // avoid name lookups
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo* addr_list = nullptr;
    struct sockaddr_storage result = {0};

    // TODO: this can block during a DNS lookup, which is bad. Windows 7 and
    // earlier don't support async name lookups, so we need to create a lookup
    // thread.
    // https://docs.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfoexa?redirectedfrom=MSDN
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
*/
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


/* WindowsSocket implementation ---------------------------------------*/

int WindowsSocket::init(int family, int type, int protocol) {
    u_long iMode = 1; // non-blocking mode

    if (socket_id_ != INVALID_SOCKET) {
        FIBRE_LOG(E) << "already initialized";
        return -1;
    }

    if (wsa_start())
        return -1;

    SOCKET socket_id = WSASocketW(family, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (socket_id == INVALID_SOCKET) {
        FIBRE_LOG(E) << "failed to open socket: " << sock_err();
        goto fail1;
    }

    // Make socket non-blocking
    if (ioctlsocket(socket_id, FIONBIO, &iMode)) {
        FIBRE_LOG(E) << "ioctlsocket() failed: " << sock_err();
        goto fail2;
    }

    socket_id_ = socket_id;
    return 0;

fail2:
    CloseHandle((HANDLE)socket_id);
fail1:
    wsa_stop();
    return -1;
}

int WindowsSocket::init(SOCKET socket_id) {
    if (socket_id_ != INVALID_SOCKET) {
        FIBRE_LOG(E) << "already initialized";
        return -1;
    }

    if (wsa_start())
        return -1;

    // Duplicate socket ID in order to make the OS's internal ref count work
    // properly.
    HANDLE new_socket_id;
    BOOL result = DuplicateHandle(GetCurrentProcess(), (HANDLE)socket_id,
            GetCurrentProcess(), &new_socket_id,
            0, FALSE, DUPLICATE_SAME_ACCESS);

    if (!result || !new_socket_id) {
        FIBRE_LOG(E) << "DuplicateHandle() failed: " << sys_err();
        goto fail;
    }

    socket_id_ = (SOCKET)new_socket_id;
    return 0;

fail:
    wsa_stop();
    return -1;
}

int WindowsSocket::deinit() {
    if (socket_id_ == INVALID_SOCKET) {
        FIBRE_LOG(E) << "not initialized";
        return -1;
    }

    int result = 0;
    if (!CloseHandle((HANDLE)socket_id_)) {
        FIBRE_LOG(E) << "CloseHandle() failed: " << sock_err();
        result = -1;
    }
    socket_id_ = INVALID_SOCKET;

    if (wsa_stop())
        result = -1;

    return result;
}

int WindowsSocket::subscribe(WindowsSocketWorker* worker, WindowsSocketWorker::callback_t* callback) {
    if (socket_id_ == INVALID_SOCKET) {
        FIBRE_LOG(E) << "not initialized";
        return -1;
    }
    if (worker_) {
        FIBRE_LOG(E) << "already subscribed";
        return -1;
    }

    HANDLE h_socket_id = (HANDLE)socket_id_;
    if (worker->register_object(&h_socket_id, callback)) {
        FIBRE_LOG(E) << "register_object() failed";
        return -1;
    }

    socket_id_ = (SOCKET)h_socket_id;
    worker_ = worker;

    return 0;
}

int WindowsSocket::unsubscribe() {
    if (!worker_) {
        FIBRE_LOG(E) << "not subscribed";
        return -1;
    }

    HANDLE h_socket_id = (HANDLE)socket_id_;
    int result = worker_->deregister_object(&h_socket_id);
    socket_id_ = (SOCKET)h_socket_id;
    worker_ = nullptr;
    return result;
}

/* WindowsSocketRXChannel implementation -------------------------------------*/

int WindowsSocketRXChannel::init(int type, int protocol, struct sockaddr_storage local_addr) {
    if (WindowsSocket::init(local_addr.ss_family, type, protocol)) {
        FIBRE_LOG(E) << "failed to open socket.";
        return -1;
    }

    if (bind(get_socket_id(), reinterpret_cast<struct sockaddr*>(&local_addr), sizeof(local_addr))) {
        FIBRE_LOG(E) << "failed to bind socket: " << sock_err();
        goto fail;
    }

    return 0;

fail:
    WindowsSocket::deinit();
    return -1;
}

int WindowsSocketRXChannel::deinit() {
    return WindowsSocket::deinit();
}

int WindowsSocketRXChannel::subscribe(WindowsSocketWorker* worker, callback_t* callback) {
    callback_ = callback;
    if (WindowsSocket::subscribe(worker, &rx_handler_obj)) {
        callback_ = nullptr;
        return -1;
    }
    return 0;
}

int WindowsSocketRXChannel::unsubscribe() {
    int result = WindowsSocket::unsubscribe();
    callback_ = nullptr;
    return result;
}

void WindowsSocketRXChannel::rx_handler(int error_code, LPOVERLAPPED overlapped) {
    uint8_t internal_buffer[WINDOWS_SOCKET_RX_BUFFER_SIZE];
    bufptr_t bufptr = { .ptr = internal_buffer, .length = sizeof(bufptr_t) };
    StreamStatus status = get_bytes(bufptr);
    cbufptr_t cbufptr = { .ptr = internal_buffer, .length = sizeof(bufptr_t) - bufptr.length };
    if (callback_)
        (*callback_)(status, cbufptr);
}

StreamStatus WindowsSocketRXChannel::get_bytes(bufptr_t& buffer) {
    socklen_t slen = sizeof(remote_addr_);
    recv_buf_.buf = reinterpret_cast<char*>(buffer.ptr);
    recv_buf_.len = buffer.length;
    unsigned long n_received = 0;
    DWORD flags = 0;

    int rc = WSARecvFrom(
            get_socket_id(), &recv_buf_, 1, &n_received, &flags /* dwFlags */,
            reinterpret_cast<struct sockaddr *>(&remote_addr_), &slen,
            NULL, NULL);

    if (rc != 0) {
        if (WSAGetLastError() == WSA_IO_PENDING /* for overlapped */ || WSAGetLastError() == WSAEWOULDBLOCK /* for non-overlapped */) {
            // An overlapped operation was initiated successfully.
            return kStreamBusy;
        } else {
            FIBRE_LOG(E) << "Socket read failed: " << sock_err();
            return kStreamError;
        }
    }
    
    // This is unexpected and would indicate a bug in the OS.
    if (n_received > buffer.length) {
        buffer += buffer.length;
        return kStreamError;
    }

    buffer += n_received;

    FIBRE_LOG(D) << "Received data from " << remote_addr_;
    return kStreamOk;
}


/* WindowsSocketTXChannel implementation -------------------------------------*/

int WindowsSocketTXChannel::init(int type, int protocol, struct sockaddr_storage remote_addr) {
    if (WindowsSocket::init(remote_addr.ss_family, type, protocol)) {
        FIBRE_LOG(E) << "failed to open socket.";
        return -1;
    }

    remote_addr_ = remote_addr;
    return 0;
}

int WindowsSocketTXChannel::init(SOCKET socket_id, struct sockaddr_storage remote_addr) {
    if (WindowsSocket::init(socket_id)) {
        FIBRE_LOG(E) << "failed to open socket.";
        return -1;
    }

    remote_addr_ = remote_addr;
    return 0;
}

int WindowsSocketTXChannel::deinit() {
    remote_addr_ = {0};
    return WindowsSocket::deinit();
}

int WindowsSocketTXChannel::subscribe(WindowsSocketWorker* worker, callback_t* callback) {
    callback_ = callback;
    if (WindowsSocket::subscribe(worker, &tx_handler_obj)) {
        callback_ = nullptr;
        return -1;
    }
    return 0;
}

int WindowsSocketTXChannel::unsubscribe() {
    int result = WindowsSocket::unsubscribe();
    callback_ = nullptr;
    return result;
}

void WindowsSocketTXChannel::tx_handler(int error_code, LPOVERLAPPED overlapped) {
    // TODO: distinguish between error and closed (ERROR_NO_DATA == kStreamClosed?).
    if (callback_)
        (*callback_)(error_code == ERROR_SUCCESS ? kStreamOk : kStreamError);
}

StreamStatus WindowsSocketTXChannel::process_bytes(cbufptr_t& buffer) {
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
            get_socket_id(), &send_buf_, 1, &n_sent, 0 /* dwFlags */,
            reinterpret_cast<struct sockaddr*>(&remote_addr_), sizeof(remote_addr_),
            &overlapped_, NULL /* lpCompletionRoutine */);

    if (rc != 0) {
        if (WSAGetLastError() == WSA_IO_PENDING) {
            // An overlapped operation was initiated successfully.
            buffer += buffer.length;
            return kStreamBusy;
        } else {
            FIBRE_LOG(E) << "Socket write failed: " << sock_err();
            return kStreamError;
        }
    }
    
    // This is unexpected and would indicate a bug in the OS.
    if (n_sent > buffer.length) {
        buffer += buffer.length;
        return kStreamError;
    }

    buffer += n_sent;

    FIBRE_LOG(D) << "Sent data to " << remote_addr_;
    return kStreamOk;
}
