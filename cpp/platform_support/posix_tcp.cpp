
#include <fibre/platform_support/posix_tcp.hpp>
#include <fibre/logging.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <thread>
#include <future>
#include <vector>

#define TCP_RX_BUF_LEN	512
#define MAX_CONCURRENT_CONNECTIONS 128

DEFINE_LOG_TOPIC(TCP);
USE_LOG_TOPIC(TCP);

namespace fibre {

/* PosixTcpServer implementation ---------------------------------------------*/

int PosixTcpServer::init(std::tuple<std::string, int> local_addr, PosixSocketWorker* worker, callback_t* connected_callback) {
    struct sockaddr_storage addr = to_posix_socket_addr(local_addr, true);

    if (!addr.ss_family) {
        return -1;
    }

    FIBRE_LOG(D) << "open TCP server on " << addr;

    if (socket_.init(addr.ss_family, SOCK_STREAM, IPPROTO_TCP)) {
        return -1;
    }

    // Reuse local address.
    // This helps reusing ports that were previously not closed cleanly and
    // are therefore still lingering in the TIME_WAIT state.
    int flag = 1;
    if (setsockopt(socket_.get_socket_id(), SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag))) {
        FIBRE_LOG(E) << "failed to make socket reuse addresses: " << sock_err();
        goto fail;
    }
   
    if (bind(socket_.get_socket_id(), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))) {
        FIBRE_LOG(E) << "failed to bind socket: " << sock_err();
        goto fail;
    }

    // make this socket a passive socket
    if (listen(socket_.get_socket_id(), MAX_CONCURRENT_CONNECTIONS) != 0) {
        FIBRE_LOG(E) << "failed to listen on TCP: " << sys_err();
        goto fail;
    }

    connected_callback_ = connected_callback;
    if (socket_.subscribe(worker, EPOLLIN, &accept_handler_obj)) {
        goto fail;
    }

    return 0;

fail:
    connected_callback_ = nullptr;
    socket_.deinit();
    return -1;
}

int PosixTcpServer::deinit() {
    int result = 0;
    if (socket_.unsubscribe()) {
        FIBRE_LOG(E) << "failed to unsubscribe";
        result = -1;
    }
    connected_callback_ = nullptr;
    if (socket_.deinit()) {
        FIBRE_LOG(E) << "failed to deinit()";
        result = -1;
    }
    return result;
}

void PosixTcpServer::accept_handler(uint32_t) {
    struct sockaddr_storage remote_addr;
    socklen_t slen = sizeof(remote_addr);

    FIBRE_LOG(D) << "incoming TCP connection";
    int socket_id = accept(socket_.get_socket_id(), reinterpret_cast<struct sockaddr *>(&remote_addr), &slen);
    if (IS_INVALID_SOCKET(socket_id)) {
        FIBRE_LOG(E) << "accept() returned invalid socket: " << sock_err();
        return;
    }
    
    const int mode = 1;
    PosixTcpRXChannel rx_channel;
    PosixTcpTXChannel tx_channel;

    // Make socket non-blocking
    if (ioctl(socket_id, FIONBIO, &mode)) {
        FIBRE_LOG(E) << "ioctlsocket() failed: " << sock_err();
        goto fail1;
    }

    if (connected_callback_) {
        if (rx_channel.init(socket_id)) {
            FIBRE_LOG(E) << "failed to create RX channel for accepted TCP connection";
            goto fail1;
        }
        if (tx_channel.init(socket_id, remote_addr)) {
            FIBRE_LOG(E) << "failed to create TX channel for accepted TCP connection";
            goto fail2;
        }

        (*connected_callback_)(rx_channel, tx_channel);
    }

    // The socket handle was duplicated in rx_channel.init() and tx_channel.init(),
    // we can therefore safely close it here.
    close(socket_id);
    return;

fail2:
    rx_channel.deinit();
fail1:
    close(socket_id);
}

/* PosixTcpClient implementation ---------------------------------------------*/

int PosixTcpClient::start_connecting(std::tuple<std::string, int> remote_addr, PosixSocketWorker* worker, callback_t* connected_callback) {
    struct sockaddr_storage addr = to_posix_socket_addr(remote_addr, true);

    if (!IS_INVALID_SOCKET(rx_channel_.get_socket_id()) || !IS_INVALID_SOCKET(tx_channel_.get_socket_id())) {
        FIBRE_LOG(E) << "previous connection still open";
        return -1;
    }

    if (!addr.ss_family) {
        return -1;
    }

    FIBRE_LOG(D) << "client: start connecting to " << addr;

    if (socket_.init(addr.ss_family, SOCK_STREAM, IPPROTO_TCP)) {
        return -1;
    }
   
    if (connect(socket_.get_socket_id(), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
        if (errno != EINPROGRESS) {
            FIBRE_LOG(E) << "connect() failed: " << sock_err();
            goto fail;
        }
    }

    worker_ = worker;
    connected_callback_ = connected_callback;
    if (socket_.subscribe(worker, EPOLLOUT | EPOLLHUP | EPOLLERR, &connected_handler_obj)) {
        goto fail;
    }

    return 0;

fail:
    worker_ = nullptr;
    connected_callback_ = nullptr;
    socket_.deinit();
    return -1;
}

int PosixTcpClient::stop_connecting() {
    FIBRE_LOG(D) << "client: stop connecting";

    int result = worker_->run_sync([this](){
        if (connected_callback_) {
            connected_handler(EPOLLHUP);
        }
    });

    if (result) {
        FIBRE_LOG(E) << "failed to terminate connection attempt";
    }

    worker_ = nullptr;
    connected_callback_ = nullptr; // should already be the case

    if (socket_.deinit()) {
        result = -1;
    }
    return result;
}

void PosixTcpClient::connected_handler(uint32_t mask) {
    FIBRE_LOG(D) << "connected_handler";

    if (connected_callback_) {
        bool is_success = !(mask & EPOLLHUP);

        if (socket_.unsubscribe()) {
            FIBRE_LOG(E) << "failed to unsubscribe after connecting";
        }

        if (is_success) {
            if (rx_channel_.init(socket_.get_socket_id())) {
                FIBRE_LOG(E) << "failed to create RX channel for TCP connection";
                goto fail1;
            }
            if (tx_channel_.init(socket_.get_socket_id(), {0})) {
                FIBRE_LOG(E) << "failed to create TX channel for TCP connection";
                goto fail2;
            }
        }

        (*connected_callback_)(is_success, *this);
        connected_callback_ = nullptr;
    }

    return;

fail2:
    rx_channel_.deinit();
fail1:
    return;
}

}
