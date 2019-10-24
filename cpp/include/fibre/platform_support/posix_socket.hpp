#ifndef __FIBRE_POSIX_SOCKET_HPP
#define __FIBRE_POSIX_SOCKET_HPP

#include "linux_worker.hpp"
#include <fibre/stream.hpp>
#include <fibre/active_stream.hpp>
#include <iostream>

#include <netinet/in.h>

namespace fibre {


#if defined(__linux__)
using TWorker = LinuxWorker; // TODO: rename to EPollWorker or LinuxEPollWorker
using socket_id_t = int;
#elif defined(_WIN32) || defined(_WIN64)
using TWorker = PosixPollWorker;
using socket_id_t = SOCKET;
#else
using TWorker = KQueueWorker;
using socket_id_t = int;
#endif

#if defined(_Win32) || defined(_Win64)
#define IS_INVALID_SOCKET(socket_id)    (socket_id == INVALID_SOCKET)
#else
#define INVALID_SOCKET (-1)
#define IS_INVALID_SOCKET(socket_id)    (socket_id < 0)
#endif


/**
 * @brief Provides a StreamSource based on a Posix or WinSock socket ID.
 * 
 * Note: To make this work on Windows, a "poll"-based worker must be implemented.
 */
class PosixSocketRXChannel : public StreamSource, public ActiveStreamSource<TWorker> {
public:
    using callback_t = Callback<StreamSource::status_t, cbufptr_t>;

    /**
     * @brief Initializes the RX channel by opening a socket using the socket()
     * function.
     * 
     * The resulting socket will be bound to the address provided in `local_addr`.
     * 
     * @param type: Will be passed as 2nd argument to socket(). Can be for
     *        instance SOCK_DGRAM or SOCKET_STREAM.
     * @param protocol: Will be passed as 3rd argument to socket(). Can be for
     *        instance IPPROTO_UDP or IPPROTO_TCP.
     * @param local_addr: The local address to the socket should be bound. The
     *        ss_family field of this address is passed as 1st argument to
     *        socket().
     */
    int init(int type, int protocol, struct sockaddr_storage local_addr);

    /**
     * @brief Initializes the RX channel with the given socket ID.
     * 
     * The socket must be bound to a local address before this function is
     * called.
     * 
     * @param socket_id: For Unix-like systems this should be a file descriptor,
     *        for Windows this should be a Windows Socket ID (as returned by
     *        socket()).
     *        The socket must be in non-blocking mode (opened with O_NONBLOCK)
     *        The socket will internally be duplicated using dup() so deinit()
     *        can be called regardless of what overload of init() was used.
     */
    int init(socket_id_t socket_id);
    int deinit();

    int subscribe(TWorker* worker, callback_t* callback) final;
    int unsubscribe() final;

    status_t get_bytes(bufptr_t& buffer) final;

    socket_id_t get_socket_id() const { return socket_id_; }

    /** @brief Returns the remote address of the most recently received data */
    struct sockaddr_storage get_remote_address() const { return remote_addr_; }

private:
    void rx_handler(uint32_t);

    socket_id_t socket_id_ = INVALID_SOCKET;
    TWorker* worker_ = nullptr;
    callback_t* callback_ = nullptr;
    struct sockaddr_storage remote_addr_ = {0}; // updated after each get_bytes() call

    member_closure_t<decltype(&PosixSocketRXChannel::rx_handler)> rx_handler_obj{&PosixSocketRXChannel::rx_handler, this};
};

/**
 * @brief Provides a StreamSink based on a Posix or WinSock socket ID.
 * 
 * Note: To make this work on Windows, a "poll"-based worker must be implemented.
 */
class PosixSocketTXChannel : public StreamSink, public ActiveStreamSink<TWorker> {
public:
    using callback_t = Callback<StreamSink::status_t>;

    /**
     * @brief Initializes the TX channel by opening a socket using the socket()
     * function.
     * 
     * @param type: Will be passed as 2nd argument to socket(). Can be for
     *        instance SOCK_DGRAM or SOCKET_STREAM.
     * @param protocol: Will be passed as 3rd argument to socket(). Can be for
     *        instance IPPROTO_UDP or IPPROTO_TCP.
     * @param remote_addr: The remote address to which data should be sent. The
     *        ss_family field of this address is passed as 1st argument to
     *        socket().
     */
    int init(int type, int protocol, struct sockaddr_storage remote_addr);

    /**
     * @brief Initializes the TX channel with the given socket ID.
     * 
     * @param socket_id: For Unix-like systems this should be a file descriptor,
     *        for Windows this should be a Windows Socket ID (as returned by
     *        socket()).
     *        The socket must be in non-blocking mode (opened with O_NONBLOCK)
     *        The socket will internally be duplicated using dup() so deinit()
     *        can be called regardless of what overload of init() was used.
     */
    int init(socket_id_t socket_id, struct sockaddr_storage remote_addr);

    /**
     * @brief Deinits a socket that was initialized with init().
     */
    int deinit();

    int subscribe(TWorker* worker, callback_t* callback) final;
    int unsubscribe() final;

    status_t process_bytes(cbufptr_t& buffer) final;

    socket_id_t get_socket_id() const { return socket_id_; }

private:
    void tx_handler(uint32_t);

    socket_id_t socket_id_ = INVALID_SOCKET;
    TWorker* worker_ = nullptr;
    callback_t* callback_ = nullptr;
    struct sockaddr_storage remote_addr_;

    member_closure_t<decltype(&PosixSocketTXChannel::tx_handler)> tx_handler_obj{&PosixSocketTXChannel::tx_handler, this};
};

/**
 * @brief Tag type to print the last socket error.
 * 
 * This is very similar to sys_err(), except that on Windows it uses
 * WSAGetLastError() instead of `errno` to fetch the last error code.
 */
struct sock_err {};

}

namespace std {
std::ostream& operator<<(std::ostream& stream, const struct sockaddr_storage& val);
std::ostream& operator<<(std::ostream& stream, const fibre::sock_err&);
}

#endif // __FIBRE_POSIX_SOCKET_HPP