
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fibre/protocol.hpp>


#define TCP_RX_BUF_LEN	512

class TCPBytesSender : public StreamSink {
public:
    TCPBytesSender(int socket_fd) :
        _socket_fd(socket_fd)
    {}

    int process_bytes(const uint8_t* buffer, size_t length, size_t& processed_bytes) {
        int bytes_sent = send(_socket_fd, buffer, length, 0);
        processed_bytes = (bytes_sent == -1) ? 0 : bytes_sent;
        return (bytes_sent == -1) ? -1 : 0;
    }

    size_t get_free_space() { return SIZE_MAX; }

private:
    int _socket_fd;
};



int serve_on_tcp(const Endpoint endpoints[], size_t n_endpoints, unsigned int port) {
    struct sockaddr_in si_me, si_other;
    int s;
    uint8_t buf[TCP_RX_BUF_LEN];


    if ((s=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
        return -1;

    memset((char *) &si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(port);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, reinterpret_cast<struct sockaddr *>(&si_me), sizeof(si_me)) == -1)
        return -1;

    listen(s, 128); // make this socket a passive socket
    for (;;) {
        memset(&si_other, 0, sizeof(si_other));

        socklen_t silen = sizeof(si_other);
        int client_portal_fd = accept(s, reinterpret_cast<sockaddr *>(&si_other), &silen); // blocking call
        // TODO: make this accept more than one connection
        memset(buf, 0, sizeof(buf));
        ssize_t n_received = recv(s, buf, sizeof(buf), 0); // blocking again, once a connection has been established
        if (n_received == -1)
            return -1;
        printf("Received packet from %s:%d\nData: %s\n\n",
            inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port), buf);

        // output stack
        TCPBytesSender tcp_packet_output(client_portal_fd);
        StreamBasedPacketSink packet2stream(tcp_packet_output);
        BidirectionalPacketBasedChannel channel(endpoints, n_endpoints, packet2stream);

        // input processing stack
        // TODO: keep track of how many bytes were already processed
        size_t processed = 0;
        StreamToPacketSegmenter stream2packet(channel);
        stream2packet.process_bytes(buf, n_received, processed);
    }

    close(s);
}

