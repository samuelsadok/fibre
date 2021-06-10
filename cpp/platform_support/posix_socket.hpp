#ifndef __FIBRE_POSIX_SOCKET_HPP
#define __FIBRE_POSIX_SOCKET_HPP

#include <fibre/config.hpp>

#if FIBRE_ENABLE_TCP_CLIENT_BACKEND || FIBRE_ENABLE_TCP_SERVER_BACKEND

#include <fibre/async_stream.hpp>
#include <fibre/bufptr.hpp>
#include <fibre/cpp_utils.hpp>
#include <fibre/event_loop.hpp>
#include <fibre/logging.hpp>
#include <fibre/rich_status.hpp>
#include <string>

#if defined(__linux__)
#include <netinet/in.h>
#elif defined(_WIN32) || defined(_WIN64)
#error "WinSock not supported yet"
#else
#error "sockets not supported yet on this platform"
#endif

namespace fibre {

#if defined(__linux__)
//using PosixSocketWorker = LinuxWorker; // TODO: rename to EPollWorker or LinuxEPollWorker
using socket_id_t = int;
#elif defined(_WIN32) || defined(_WIN64)
//using PosixSocketWorker = PosixPollWorker;
using socket_id_t = SOCKET;
#else
//using PosixSocketWorker = KQueueWorker;
using socket_id_t = int;
#endif

#if defined(_Win32) || defined(_Win64)
#define IS_INVALID_SOCKET(socket_id)    (socket_id == INVALID_SOCKET)
#else
#define INVALID_SOCKET (-1)
#define IS_INVALID_SOCKET(socket_id)    (socket_id < 0)
#endif

struct AddressResolutionContext;
struct ConnectionContext;

/**
 * @brief Starts resolving a hostname (such as www.google.com) to one or
 * multiple IP addresses.
 * 
 * If available, both IPv4 and IPv6 addresses are returned.
 * 
 * @param passive: If false, the returned address will be suitable for use with
 *        connect(2), sendto(2), or sendmsg(2).
 * @param callback: Invoked for every address that is found. Invoked with null
 *        if no more addresses are available, including in case of an error or
 *        cancellation.
 * 
 * @returns: false if the lookup could not be started. `callback` will not be
 *           called.
 */
RichStatus start_resolving_address(EventLoop* event_loop, Logger logger,
    std::pair<std::string, int> address, bool passive,
    AddressResolutionContext** handle,
    Callback<void, std::optional<cbufptr_t>> callback);

/**
 * @brief Cancels the ongoing address resolution.
 * 
 * The cancellation is complete once the associated callback is invoked with
 * null.
 */
void cancel_resolving_address(AddressResolutionContext* handle);

/**
 * @brief Starts connecting to the specified address
 * 
 * @param addr: The address to connect to. Usually this buffer contains an
 *        address of the type `struct sockaddr`. The family parameter of this
 *        address will be passed as 1st argument to socket().
 * @param type: Will be passed as 2nd argument to socket(). Can be for
 *        instance SOCK_DGRAM or SOCKET_STREAM.
 * @param protocol: Will be passed as 3rd argument to socket(). Can be for
 *        instance IPPROTO_UDP or IPPROTO_TCP.
 * @param on_connected: Called when the connection attempt succeeds or fails.
 *        If the connection was established, the socket ID is passed to the
 *        callback. This socket ID will only be valid for the duration of the
 *        callback and must be duplicated (dup) if the application intends to
 *        keep using it.
 *        If the connection failed, std::nullopt is passed.
 */
RichStatus start_connecting(EventLoop* event_loop, Logger logger, cbufptr_t addr, int type, int protocol, ConnectionContext** ctx, Callback<void, RichStatus, socket_id_t> on_connected);
void stop_connecting(ConnectionContext* ctx);

/**
 * @brief Starts listening and accepting connections on the specified local
 * address.
 *
 * @param addr: The local address to listen on. Usually this buffer contains an
 *        address of the type `struct sockaddr`. The family parameter of this
 *        address will be passed as 1st argument to socket().
 * @param type: Will be passed as 2nd argument to socket(). Can be for
 *        instance SOCK_DGRAM or SOCKET_STREAM.
 * @param protocol: Will be passed as 3rd argument to socket(). Can be for
 *        instance IPPROTO_UDP or IPPROTO_TCP.
 * @param on_connected: Called for every connection that is accepted. The new
 *        socket ID is passed to the callback. This socket ID will only be valid
 *        for the duration of the callback and must be duplicated (dup) if the
 *        application intends to keep using it.
 *        If the attempt to listen fails permanently or is cancelled,
 *        std::nullopt is passed.
 */
RichStatus start_listening(EventLoop* event_loop, Logger logger, cbufptr_t addr, int type, int protocol, ConnectionContext** ctx, Callback<void, RichStatus, socket_id_t> on_connected);
void stop_listening(ConnectionContext* ctx);

/**
 * @brief AsyncStreamSource and AsyncStreamSink based on a Posix or WinSock
 * socket ID.
 * 
 * Note: To make this work on Windows, a "poll"-based worker must be implemented.
 */
class PosixSocket final : public AsyncStreamSource, public AsyncStreamSink {
public:
    /**
     * @brief Initializes the object with the given socket ID.
     * 
     * The socket must be bound to a local address before this function is
     * called.
     * 
     * @param socket_id: For Unix-like systems this should be a file descriptor,
     *        for Windows this should be a Windows Socket ID (as returned by
     *        socket()).
     *        The socket must be in non-blocking mode (opened with O_NONBLOCK).
     *        The socket will internally be duplicated using dup() so it can be
     *        closed after this call.
     */
    RichStatus init(EventLoop* event_loop, Logger logger, socket_id_t socket_id);

    /**
     * @brief Deinits a socket that was initialized with init().
     */
    RichStatus deinit();

    void start_read(bufptr_t buffer, TransferHandle* handle, Callback<void, ReadResult> completer) final;
    void cancel_read(TransferHandle transfer_handle) final;

    void start_write(cbufptr_t buffer, TransferHandle* handle, Callback<void, WriteResult0> completer) final;
    void cancel_write(TransferHandle transfer_handle) final;

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
    std::optional<ReadResult> read_sync(bufptr_t buffer);
    std::optional<WriteResult0> write_sync(cbufptr_t buffer);
    void update_subscription();
    void on_event(uint32_t mask);

    int socket_id_ = INVALID_SOCKET;
    EventLoop* event_loop_ = nullptr;
    Logger logger_ = Logger::none();
    struct sockaddr_storage remote_addr_ = {0}; // updated after each RX event
    uint32_t mask_ = 0; // current event subscription mask
    bufptr_t rx_buf_{}; // valid while there is an RX request pending
    cbufptr_t tx_buf_{}; // valid while there is a TX request pending
    Callback<void, ReadResult> rx_callback_; // valid while there is an RX request pending
    Callback<void, WriteResult0> tx_callback_; // valid while there is a TX request pending
};

}

#include <iostream>

namespace std {
std::ostream& operator<<(std::ostream& stream, const struct sockaddr_storage& val);
}

#endif

#endif // __FIBRE_POSIX_SOCKET_HPP