#ifndef __FIBRE_WINDOWS_SOCKET_HPP
#define __FIBRE_WINDOWS_SOCKET_HPP

#include <winsock2.h> // should be included before windows.h

#include <fibre/windows_worker.hpp>
#include <fibre/stream.hpp>
#include <fibre/active_stream.hpp>
#include <iostream>

namespace fibre {

using TWorker = WindowsIOCPWorker;

/**
 * @brief Provides a StreamSource based on a WinSock socket ID.
 */
class WindowsSocketRXChannel : public StreamSource, public ActiveStreamSource<TWorker> {
public:
    using callback_t = Callback<StreamSource::status_t, cbufptr_t>;

    /**
     * @brief Initializes the RX channel by opening a socket using the
     * WSASocket() function.
     * 
     * The resulting socket will be bound to the address provided in `local_addr`.
     * 
     * @param type: Will be passed as 2nd argument to WSASocket(). Can be for
     *        instance SOCK_DGRAM or SOCKET_STREAM.
     * @param protocol: Will be passed as 3rd argument to WSASocket(). Can be for
     *        instance IPPROTO_UDP or IPPROTO_TCP.
     * @param local_addr: The local address to the socket should be bound. The
     *        ss_family field of this address is passed as 1st argument to
     *        WSASocket().
     */
    int init(int type, int protocol, struct sockaddr_storage local_addr);

    /**
     * @brief Initializes the RX channel with the given socket ID.
     * 
     * The socket must be bound to a local address before this function is
     * called.
     * 
     * @param socket_id: This should be a Windows Socket ID as returned by
     *        socket() or WSASocket().
     *        The socket must be in non-blocking mode.
     *        The socket will internally be duplicated using DuplicateHandle()
     *        so deinit() can be called regardless of what overload of init()
     *        was used.
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
     * @brief Initializes the TX channel by opening a socket using the
     * WSASocket() function.
     * 
     * @param type: Will be passed as 2nd argument to WSASocket(). Can be for
     *        instance SOCK_DGRAM or SOCKET_STREAM.
     * @param protocol: Will be passed as 3rd argument to WSASocket(). Can be for
     *        instance IPPROTO_UDP or IPPROTO_TCP.
     * @param remote_addr: The remote address to which data should be sent. The
     *        ss_family field of this address is passed as 1st argument to
     *        WSASocket().
     */
    int init(int type, int protocol, struct sockaddr_storage remote_addr);

    /**
     * @brief Initializes the TX channel with the given socket ID.
     * 
     * @param socket_id: This should be a Windows Socket ID as returned by
     *        socket() or WSASocket().
     *        The socket must be in non-blocking mode.
     *        The socket will internally be duplicated using DuplicateHandle()
     *        so deinit() can be called regardless of what overload of init()
     *        was used.
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

#endif // __FIBRE_WINDOWS_SOCKET_HPP