#ifndef __FIBRE_WINDOWS_UDP_HPP
#define __FIBRE_WINDOWS_UDP_HPP

#include <winsock2.h> // should be included before windows.h

#include <fibre/windows_worker.hpp>
#include <fibre/stream.hpp>
#include <fibre/active_stream.hpp>

namespace fibre {

using TWorker = WindowsIOCPWorker;

/**
 * @brief Provides a StreamSource based on a WinSock socket ID.
 */
class WindowsSocketRXChannel : public StreamSource, public ActiveStreamSource<TWorker> {
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
    int init(SOCKET socket_id);
    int deinit();

    int subscribe(TWorker* worker, callback_t* callback) final;
    int unsubscribe() final;

    status_t get_bytes(bufptr_t& buffer) final;

    SOCKET get_socket_id() const { return socket_id_; }

    /** @brief Returns the remote address of the most recently received data */
    struct sockaddr_storage get_remote_address() const { return remote_addr_; }

private:
    void rx_handler(int, LPOVERLAPPED);

    SOCKET socket_id_ = INVALID_SOCKET;
    TWorker* worker_ = nullptr;
    callback_t* callback_ = nullptr;
    struct sockaddr_storage remote_addr_ = {0}; // updated after each get_bytes() call
    WSABUF recv_buf_;
    WSAOVERLAPPED overlapped_ = {0};

    member_closure_t<decltype(&WindowsSocketRXChannel::rx_handler)> rx_handler_obj{&WindowsSocketRXChannel::rx_handler, this};
};

/**
 * @brief Provides a StreamSink based on a WinSock socket ID.
 */
class WindowsSocketTXChannel : public StreamSink, public ActiveStreamSink<TWorker> {
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
    int init(SOCKET socket_id, struct sockaddr_storage remote_addr);
    int deinit();

    int subscribe(TWorker* worker, callback_t* callback) final;
    int unsubscribe() final;

    status_t process_bytes(cbufptr_t& buffer) final;

    SOCKET get_socket_id() const { return socket_id_; }

private:
    void tx_handler(int, LPOVERLAPPED);

    SOCKET socket_id_ = INVALID_SOCKET;
    TWorker* worker_ = nullptr;
    callback_t* callback_ = nullptr;
    struct sockaddr_storage remote_addr_;
    WSABUF send_buf_;
    WSAOVERLAPPED overlapped_ = {0};

    member_closure_t<decltype(&WindowsSocketTXChannel::tx_handler)> tx_handler_obj{&WindowsSocketTXChannel::tx_handler, this};
};


class WindowsUdpRxChannel;
class WindowsUdpTxChannel;

class WindowsUdpRxChannel : public WindowsSocketRXChannel {
    int init(SOCKET socket_id) = delete; // Use open() instead.
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
    int open(const WindowsUdpTxChannel& tx_channel);

    /**
     * @brief Closes this channel.
     * This does not affect associated TX channels.
     */
    int close();
};

class WindowsUdpTxChannel : public WindowsSocketTXChannel {
    int init(SOCKET socket_id) = delete; // Use open() instead.
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
    int open(const WindowsUdpRxChannel& rx_channel);

    /**
     * @brief Closes this channel.
     * This does not affect associated TX channels.
     */
    int close();
};

}

#endif // __FIBRE_WINDOWS_UDP_HPP