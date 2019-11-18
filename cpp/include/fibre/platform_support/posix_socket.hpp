#ifndef __FIBRE_POSIX_SOCKET_HPP
#define __FIBRE_POSIX_SOCKET_HPP

#include "linux_worker.hpp"
#include <fibre/stream.hpp>
#include <fibre/active_stream.hpp>
#include <iostream>

#include <netinet/in.h>

namespace fibre {


#if defined(__linux__)
using PosixSocketWorker = LinuxWorker; // TODO: rename to EPollWorker or LinuxEPollWorker
using socket_id_t = int;
#elif defined(_WIN32) || defined(_WIN64)
using PosixSocketWorker = PosixPollWorker;
using socket_id_t = SOCKET;
#else
using PosixSocketWorker = KQueueWorker;
using socket_id_t = int;
#endif

#if defined(_Win32) || defined(_Win64)
#define IS_INVALID_SOCKET(socket_id)    (socket_id == INVALID_SOCKET)
#else
#define INVALID_SOCKET (-1)
#define IS_INVALID_SOCKET(socket_id)    (socket_id < 0)
#endif

struct sockaddr_storage to_posix_socket_addr(std::tuple<std::string, int> address, bool passive);

/**
 * @brief Base class for various types of Posix sockets.
 * 
 * Note: To make this work on Windows, a "poll"-based worker must be implemented.
 */
class PosixSocket {
public:
    /**
     * @brief Initializes the socket by using the socket() function.
     * 
     * @param family: Will be passed as 1st argument to socket(). Can be for
     *        instance AF_INET or AF_INET6.
     * @param type: Will be passed as 2nd argument to socket(). Can be for
     *        instance SOCK_DGRAM or SOCKET_STREAM.
     * @param protocol: Will be passed as 3rd argument to socket(). Can be for
     *        instance IPPROTO_UDP or IPPROTO_TCP.
     */
    int init(int family, int type, int protocol);

    /**
     * @brief Initializes the socket with the given socket ID.
     * 
     * @param socket_id: For Unix-like systems this should be a file descriptor,
     *        for Windows this should be a Windows Socket ID (as returned by
     *        socket()).
     *        The socket must be in non-blocking mode (opened with O_NONBLOCK)
     *        The socket will internally be duplicated using dup() so deinit()
     *        can be called regardless of what overload of init() was used.
     */
    int init(socket_id_t socket_id);

    /**
     * @brief Deinits a socket that was initialized with init().
     */
    int deinit();

    int subscribe(PosixSocketWorker* worker, int events, PosixSocketWorker::callback_t* callback);
    int unsubscribe();

    socket_id_t get_socket_id() const { return socket_id_; }

private:
    socket_id_t socket_id_ = INVALID_SOCKET;
    PosixSocketWorker* worker_ = nullptr;
};

/**
 * @brief StreamSource based on a Posix or WinSock socket ID.
 * 
 * Note: To make this work on Windows, a "poll"-based worker must be implemented.
 */
class PosixSocketRXChannel : public PosixSocket, public StreamSource, public StreamPusher<PosixSocketWorker> {
public:
    int init(int, int, int) = delete;
    using PosixSocket::init;

    /**
     * @brief Initializes the RX channel by opening a socket using the socket()
     * and bind() functions.
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

#if 0
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
#endif

    /**
     * @brief Deinits a socket that was initialized with init().
     */
    int deinit();

    int subscribe(PosixSocketWorker* worker, get_buffer_callback_t* get_buffer_callback, commit_callback_t* commit_callback, completed_callback_t* completed_callback) final;
    int unsubscribe() final;

    StreamStatus get_bytes(bufptr_t& buffer) final;

    /**
     * @brief Returns the remote address of this socket.
     * 
     * For connectionless sockets this is origin of the most recently received
     * data and it is only valid from the moment something was actually received.
     * 
     * For connection-oriented sockets this address is valid as soon as the
     * socket is initialized.
     */
    struct sockaddr_storage get_remote_address() const { return remote_addr_; }

private:
    void rx_handler(uint32_t);

    struct sockaddr_storage remote_addr_ = {0}; // updated after each get_bytes() call

    member_closure_t<decltype(&PosixSocketRXChannel::rx_handler)> rx_handler_obj{&PosixSocketRXChannel::rx_handler, this};
};

/**
 * @brief StreamSink based on a Posix or WinSock socket ID.
 * 
 * Note: To make this work on Windows, a "poll"-based worker must be implemented.
 */
class PosixSocketTXChannel : public PosixSocket, public StreamSink, public StreamPuller<PosixSocketWorker> {
public:
    int init(int, int, int) = delete;
    int init(int) = delete;

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

    int subscribe(PosixSocketWorker* worker, get_buffer_callback_t* get_buffer_callback, consume_callback_t* consume_callback, completed_callback_t* completed_callback) final;
    int unsubscribe() final;

    StreamStatus process_bytes(cbufptr_t& buffer) final;

private:
    void tx_handler(uint32_t);

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