#ifndef __FIBRE_WINDOWS_UDP_HPP
#define __FIBRE_WINDOWS_UDP_HPP

#include <winsock2.h> // should be included before windows.h

#include <fibre/windows_socket.hpp>

namespace fibre {

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