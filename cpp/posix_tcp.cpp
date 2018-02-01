
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include <future>

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


int serve_client(const Endpoint endpoints[], size_t n_endpoints, int sock_fd) {
    uint8_t buf[TCP_RX_BUF_LEN];

//    printf("initializing output stack\n");
    // initialize output stack for this client
    TCPBytesSender tcp_packet_output(sock_fd);
    StreamBasedPacketSink packet2stream(tcp_packet_output);
    BidirectionalPacketBasedChannel channel(endpoints, n_endpoints, packet2stream);

    StreamToPacketSegmenter stream2packet(channel);

    // now listen for it
    for (;;) {
        memset(buf, 0, sizeof(buf));
        // returns as soon as there is some data
        ssize_t n_received = recv(sock_fd, buf, sizeof(buf), 0);
        printf("recvd something: %d\n", n_received);
        if (n_received == -1 || n_received == 0) {// -1 indicates error and 0 means that the client gracefully terminated
            close(sock_fd);
            return n_received;
        }
//        printf("Received packet from %s:%d\n\n",
//            inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));

        // input processing stack
        size_t processed = 0;
        stream2packet.process_bytes(buf, n_received, processed);
    }
}

// function to check if a worker thread handling a single client is done
template<typename T>
bool future_is_ready(std::future<T>& t){
    return t.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

int serve_on_tcp(const Endpoint endpoints[], size_t n_endpoints, unsigned int port) {
    struct sockaddr_in si_me, si_other;
    int s;


    if ((s=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
        return -1;

    memset((char *) &si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(port);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, reinterpret_cast<struct sockaddr *>(&si_me), sizeof(si_me)) == -1)
        return -1;

    listen(s, 128); // make this socket a passive socket
    std::vector<std::future<int>> serv_pool;
    for (;;) {
        memset(&si_other, 0, sizeof(si_other));

        socklen_t silen = sizeof(si_other);
        printf("enter blocking accept\n");
        int client_portal_fd = accept(s, reinterpret_cast<sockaddr *>(&si_other), &silen); // blocking call
        printf("accepted client\n");
        serv_pool.push_back(std::async(std::launch::async, serve_client, endpoints, n_endpoints, client_portal_fd));
        // do a little clean up on the pool
        for (std::vector<std::future<int>>::iterator it = serv_pool.end()-1; it >= serv_pool.begin(); --it) {
            if (future_is_ready(*it)) {
                printf("erasing thread\n");
                // we can erase this thread
                serv_pool.erase(it);
            }
        }
    }

    close(s);
}

