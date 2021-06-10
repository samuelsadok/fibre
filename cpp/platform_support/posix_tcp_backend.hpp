#ifndef __FIBRE_POSIX_TCP_BACKEND_HPP
#define __FIBRE_POSIX_TCP_BACKEND_HPP

#include <fibre/config.hpp>

#if FIBRE_ENABLE_TCP_CLIENT_BACKEND || FIBRE_ENABLE_TCP_SERVER_BACKEND

#include "posix_socket.hpp"
#include <fibre/channel_discoverer.hpp>
#include <fibre/event_loop.hpp>
#include <fibre/logging.hpp>
#include <string>
#include <netdb.h>

namespace fibre {

/**
 * TCP client and TCP server implementations are identical up to the function
 * that is used to convert an address to one or more connected socket IDs.
 * The client uses the posix function `connect` to do so, while the server uses
 * the posix functions `listen` and `accept`.
 */
class PosixTcpBackend : public Backend {
public:
    RichStatus init(EventLoop* event_loop, Logger logger) final;
    RichStatus deinit() final;

    void start_channel_discovery(Domain* domain, const char* specs, size_t specs_len, ChannelDiscoveryContext** handle) final;
    RichStatus stop_channel_discovery(ChannelDiscoveryContext* handle) final;

private:
    struct TcpChannelDiscoveryContext {
        PosixTcpBackend* parent;
        Timer* timer;
        std::pair<std::string, int> address;
        std::string display_name;
        Domain* domain;
        AddressResolutionContext* addr_resolution_ctx;
        ConnectionContext* connection_ctx;
        float lookup_period = 1.0f; // wait 1s for next address resolution

        struct AddrContext {
            std::vector<uint8_t> addr;
            ConnectionContext* connection_ctx;
        };

        std::vector<AddrContext> known_addresses;
        void resolve_address();
        void on_found_address(std::optional<cbufptr_t> addr);
        void on_connected(RichStatus status, socket_id_t socket_id);
        void on_disconnected();
    };

    virtual RichStatus start_opening_connections(EventLoop* event_loop, Logger logger, cbufptr_t addr, int type, int protocol, ConnectionContext** ctx, Callback<void, RichStatus, socket_id_t> on_connected) = 0;
    virtual void cancel_opening_connections(ConnectionContext* ctx) = 0;

    EventLoop* event_loop_ = nullptr;
    Logger logger_ = Logger::none();
    size_t n_discoveries_ = 0;
};

class PosixTcpClientBackend : public PosixTcpBackend {
public:
    RichStatus start_opening_connections(EventLoop* event_loop, Logger logger, cbufptr_t addr, int type, int protocol, ConnectionContext** ctx, Callback<void, RichStatus, socket_id_t> on_connected) final {
        return start_connecting(event_loop, logger, addr, type, protocol, ctx, on_connected);
    }
    void cancel_opening_connections(ConnectionContext* ctx) final {
        stop_connecting(ctx);
    }
};

class PosixTcpServerBackend : public PosixTcpBackend {
public:
    RichStatus start_opening_connections(EventLoop* event_loop, Logger logger, cbufptr_t addr, int type, int protocol, ConnectionContext** ctx, Callback<void, RichStatus, socket_id_t> on_connected) final {
        return start_listening(event_loop, logger, addr, type, protocol, ctx, on_connected);
    }
    void cancel_opening_connections(ConnectionContext* ctx) final {
        stop_listening(ctx);
    }
};

}

#endif

#endif // __FIBRE_POSIX_TCP_BACKEND_HPP