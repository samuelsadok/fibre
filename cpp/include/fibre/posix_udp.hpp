#ifndef __FIBRE_POSIX_UDP_HPP
#define __FIBRE_POSIX_UDP_HPP

#include <fibre/linux_worker.hpp>
#include <fibre/linux_timer.hpp>
#include <fibre/channel_discoverer.hpp>
#include <fibre/context.hpp>
#include <fibre/stream.hpp>

#include <netinet/in.h>

namespace fibre {


#if defined(__linux__)
using TWorker = LinuxWorker; // TODO: rename to EPollWorker or LinuxEPollWorker
//#elif defined(_WIN32) || defined(_WIN64)
//using TWorker = PosixPollWorker;
#else
using TWorker = KQueueWorker;
#endif

#if defined(_Win32) || defined(_Win64)
#define IS_INVALID_SOCKET(socket_id)    (socket_id == INVALID_SOCKET)
#else
#define INVALID_SOCKET (-1)
#define IS_INVALID_SOCKET(socket_id)    (socket_id < 0)
#endif

// TODO: have the user allocate buffer space for posix sockets
#define POSIX_SOCKET_RX_BUFFER_SIZE 8192


template<typename TWorker>
class ActiveStreamSource {
public:
    using callback_t = Callback<StreamSource::status_t, cbufptr_t>;

    /**
     * @brief Registers a callback that will be invoked every time new data comes in.
     * 
     * Spurious invokations are possible. That means the callback can be called
     * without any data being ready.
     * 
     * Only one callback can be registered at a time.
     * 
     * TODO: if a callback is registered while the source is ready, should it be
     * invoked? The easiest answer for the epoll architecture would be yes.
     */
    virtual int subscribe(TWorker* worker, callback_t* callback) = 0;
    virtual int unsubscribe() = 0;
};

template<typename TWorker>
class ActiveStreamSink {
public:
    using callback_t = Callback<StreamSink::status_t>;

    /**
     * @brief Registers a callback that will be invoked every time the
     * object is ready to accept new data.
     * 
     * Spurious invokations are possible.
     * 
     * Only one callback can be registered at a time.
     * 
     * TODO: if a callback is registered while the sink is ready, should it be
     * invoked? The easiest answer for the epoll architecture would be yes.
     */
    virtual int subscribe(TWorker* worker, callback_t* callback) = 0;
    virtual int unsubscribe() = 0;
};

/**
 * @brief Provides a StreamSource based on a Posix or WinSock socket ID.
 * 
 * Note: To make this work on Windows, a "poll"-based worker must be implemented.
 */
class PosixSocketRXChannel : public StreamSource, public ActiveStreamSource<TWorker> {
public:
    using callback_t = Callback<StreamSource::status_t, cbufptr_t>;

    /**
     * @brief Initializes the RX channel with the given socket ID.
     * 
     * For Unix-like systems this should be a file descriptor, for Windows this
     * should be a Windows Socket ID (as returned by socket()).
     * 
     * The socket must be bound to a local address before this function is
     * called.
     */
    int init(int socket_id);
    int deinit();

    int subscribe(TWorker* worker, callback_t* callback) final;
    int unsubscribe() final;

    status_t get_bytes(bufptr_t& buffer) final;

    int get_socket_id() const { return socket_id_; }

    /** @brief Returns the remote address of the most recently received data */
    struct sockaddr_in6 get_remote_address() const { return remote_addr_; }

private:
    void rx_handler(uint32_t);

    int socket_id_ = INVALID_SOCKET;
    TWorker* worker_ = nullptr;
    callback_t* callback_ = nullptr;
    struct sockaddr_in6 remote_addr_ = {0}; // updated after each get_bytes() call

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
     * @brief Initializes the TX channel with the given socket ID.
     * 
     * @param socket_id: For Unix-like systems this should be a file descriptor,
     *        for Windows this should be a Windows Socket ID (as returned by
     *        socket()).
     *        The socket must be in non-blocking mode (opened with O_NONBLOCK)
     */
    int init(int socket_id, struct sockaddr_in6 remote_addr);
    int deinit();

    int subscribe(TWorker* worker, callback_t* callback) final;
    int unsubscribe() final;

    status_t process_bytes(cbufptr_t& buffer) final;

    int get_socket_id() const { return socket_id_; }

private:
    void tx_handler(uint32_t);

    int socket_id_ = INVALID_SOCKET;
    TWorker* worker_ = nullptr;
    callback_t* callback_ = nullptr;
    struct sockaddr_in6 remote_addr_;

    member_closure_t<decltype(&PosixSocketTXChannel::tx_handler)> tx_handler_obj{&PosixSocketTXChannel::tx_handler, this};
};


class PosixUdpRxChannel;
class PosixUdpTxChannel;

class PosixUdpRxChannel : public PosixSocketRXChannel {
    int init(int socket_id) = delete; // Use open() instead.
    int deinit() = delete; // Use close() instead.

public:
    /**
     * @brief Opens this channel for incoming UDP packets on the specified
     * local address.
     * 
     * The RX channel should eventually be closed using close().
     * 
     * @returns Zero on success or a non-zero error code otherwise.
     */
    int open(std::string local_address, int local_port);

    /**
     * @brief Opens this channel for incoming UDP packets using the same
     * underlying socket as the provided TX channel.
     * 
     * This will only succeed if the given TX channel is already open and has
     * been used at least once to send data. The local address of this RX
     * channel will be set to the same address and port that was used to send
     * the most recent UDP packet on the TX channel.
     * 
     * The RX channel should eventually be closed using close(). Doing so will
     * not affect the associated TX channel.
     * 
     * @param tx_channel: The TX channel based on which to initialized this RX
     *        channel.
     * @returns Zero on success or a non-zero error code otherwise.
     */
    int open(const PosixUdpTxChannel& tx_channel);

    /**
     * @brief Closes this channel.
     * This does not affect associated TX channels.
     */
    int close();
};

class PosixUdpTxChannel : public PosixSocketTXChannel {
    int init(int socket_id) = delete; // Use open() instead.
    int deinit() = delete; // Use close() instead.

public:
    /**
     * @brief Opens this channel for outgoing UDP packets to the specified
     * remote address.
     * 
     * The TX channel should eventually be closed using close().
     * 
     * @returns Zero on success or a non-zero error code otherwise.
     */
    int open(std::string remote_address, int remote_port);

    /**
     * @brief Opens this channel for outgoing UDP packets using the same
     * underlying socket as the provied RX channel.
     * 
     * This will only succeed if the given RX channel is already open and has
     * received data at least once. The remote address of this TX channel will
     * be initialized to the origin of the most recently received packet on the
     * RX channel ("received" in this context means actually read by the client).
     * 
     * The TX channel should eventually be closed using close(). Doing so will
     * not affect the associated RX channel.
     * 
     * @param rx_channel: The RX channel based on which to initialized this TX
     *        channel.
     * @returns Zero on success or a non-zero error code otherwise.
     */
    int open(const PosixUdpRxChannel& rx_channel);

    /**
     * @brief Closes this channel.
     * This does not affect associated TX channels.
     */
    int close();
};

}

#endif // __FIBRE_POSIX_UDP_HPP