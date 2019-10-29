#ifndef __FIBRE_POSIX_TCP_HPP
#define __FIBRE_POSIX_TCP_HPP

#include "posix_socket.hpp"

#include <netinet/in.h>

namespace fibre {

class PosixTcpRXChannel : public PosixSocketRXChannel {
    //int init(int, int, struct sockaddr_storage) = delete;
};

class PosixTcpTXChannel : public PosixSocketTXChannel {
    //int init(int, int, struct sockaddr_storage) = delete;
};

class PosixTcpServer {
public:
    using callback_t = Callback<PosixTcpRXChannel, PosixTcpTXChannel>;
    using rx_channel_t = PosixTcpRXChannel;
    using tx_channel_t = PosixTcpTXChannel;
    
    /**
     * @brief Initializes the TCP server by opening a socket using WSASocket()
     * and starts accepting connections on the specified local address.
     * 
     * @param local_addr: The local address to which the socket shall be bound.
     *        For instance "::" would accept connections on any IP address,
     *        "::1" would accept connections on the loopback address only.
     * @param worker: The worker on which the `connected_callback` should be ran.
     * @param connected_callback: If not null, this callback will be invoked for
     *        every client connection that is established. The ready-to-use RX
     *        and TX streams are passed as arguments to the callback. To close
     *        the client connection, the RX and TX streams must both be closed.
     */
    int init(std::tuple<std::string, int> local_addr, PosixSocketWorker* worker, callback_t* connected_callback);

    /**
     * @brief Stops the server from accepting new connections.
     * 
     * Connections which were already established but not yet closed will remain
     * open.
     */
    int deinit();

private:
    void accept_handler(uint32_t);

    PosixSocket socket_; // passive socket that accepts connections
    callback_t* connected_callback_;

    member_closure_t<decltype(&PosixTcpServer::accept_handler)> accept_handler_obj{&PosixTcpServer::accept_handler, this};
};

class PosixTcpClient {
public:
    using callback_t = Callback<bool, PosixTcpClient&>;

    /**
     * @brief Initializes the TCP client by connecting to a remote server using
     * the Posix connect() function.
     * 
     * Every start_connecting() call must be terminated with a stop_connecting()
     * call, regardless of whether it succeeded, failed or is still pending.
     * 
     * A new connection attempt must only be started after stop_connecting() is
     * called and both tx_channel and rx_channel are closed.
     * 
     * @param remote_addr: The remote address to which the client shall connect.
     * @param worker: The worker on which the `connected_callback` should be ran.
     * @param connected_callback: If not null, this callback will be invoked
     *        once connection attempt finished, whether successful or not.
     *        If the connection attempt is successful, "true" is passed to the
     *        callback and the tx_channel and rx_channel objects are
     *        ready-to-use.
     *        The user is responsible of calling deinit() on tx_channel and
     *        rx_channel if and only if the `connected_callback` is invoked with
     *        "true".
     *        The TCP connection is closed once both tx_channel and rx_channel
     *        are closed.
     */
    int start_connecting(std::tuple<std::string, int> remote_addr, PosixSocketWorker* worker, callback_t* connected_callback);

    /**
     * @brief Aborts any pending connection attempts.
     * 
     * After this function returns, the callback passed to connect() will no
     * longer be invoked.
     * 
     * If the connection attempt already succeeded, calling this function will
     * not close `tx_channel` or `rx_channel`.
     */
    int stop_connecting();

    PosixTcpTXChannel tx_channel_;
    PosixTcpRXChannel rx_channel_;

private:
    void connected_handler(uint32_t);

    PosixSocket socket_;
    callback_t* connected_callback_;
    PosixSocketWorker* worker_ = nullptr;

    member_closure_t<decltype(&PosixTcpClient::connected_handler)> connected_handler_obj{&PosixTcpClient::connected_handler, this};
};

}

#endif // __FIBRE_POSIX_TCP_HPP