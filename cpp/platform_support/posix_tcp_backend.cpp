
#include "posix_tcp_backend.hpp"

#if FIBRE_ENABLE_TCP_CLIENT_BACKEND || FIBRE_ENABLE_TCP_SERVER_BACKEND

#include "posix_socket.hpp"
#include <fibre/fibre.hpp>
#include <signal.h>
#include <unistd.h>
#include <algorithm>
#include <string.h>

using namespace fibre;

RichStatus PosixTcpBackend::init(EventLoop* event_loop, Logger logger) {
    F_RET_IF(event_loop_, "already initialized");
    F_RET_IF(!event_loop, "invalid argument");
    event_loop_ = event_loop;
    logger_ = logger;
    return RichStatus::success();
}

RichStatus PosixTcpBackend::deinit() {
    F_RET_IF(!event_loop_, "not initialized");
    F_LOG_IF(logger_, n_discoveries_, "some discoveries still ongoing");
    event_loop_ = nullptr;
    logger_ = Logger::none();
    return RichStatus::success();
}

void PosixTcpBackend::start_channel_discovery(Domain* domain, const char* specs, size_t specs_len, ChannelDiscoveryContext** handle) {
    const char* address_begin;
    const char* address_end;
    int port;

    if (!event_loop_) {
        F_LOG_E(logger_, "not initialized");
        //on_found_channels.invoke({kFibreInvalidArgument, nullptr, nullptr, 0});
        return; // TODO: error reporting
    }

    if (!try_parse_key(specs, specs + specs_len, "address", &address_begin, &address_end)) {
        F_LOG_E(logger_, "no address specified");
        //on_found_channels.invoke({kFibreInvalidArgument, nullptr, nullptr, 0});
        return; // TODO: error reporting
    }

    if (!try_parse_key(specs, specs + specs_len, "port", &port)) {
        F_LOG_E(logger_, "no port specified");
        //on_found_channels.invoke({kFibreInvalidArgument, nullptr, nullptr, 0});
        return; // TODO: error reporting
    }

    n_discoveries_++;

    TcpChannelDiscoveryContext* ctx = new TcpChannelDiscoveryContext(); // TODO: free
    
    if (F_LOG_IF_ERR(logger_, event_loop_->open_timer(&ctx->timer, MEMBER_CB(ctx, resolve_address)), "failed to open timer")) {
        delete ctx;
        return;
    }

    ctx->parent = this;
    ctx->address = {{address_begin, address_end}, port};
    ctx->display_name = "TCP (" + ctx->address.first + ":" + std::to_string(port) + ")";
    ctx->domain = domain;
    ctx->addr_resolution_ctx = nullptr;
    ctx->resolve_address();
}

RichStatus PosixTcpBackend::stop_channel_discovery(ChannelDiscoveryContext* handle) {
    // TODO
    n_discoveries_--;
    return RichStatus::success();
}

void PosixTcpBackend::TcpChannelDiscoveryContext::resolve_address() {
    if (F_LOG_IF(parent->logger_, addr_resolution_ctx, "already resolving")) {
        return;
    }
    F_LOG_IF_ERR(parent->logger_,
            start_resolving_address(parent->event_loop_, parent->logger_,
            address, false, &addr_resolution_ctx, MEMBER_CB(this, on_found_address)),
            "cannot start address resolution");
}

void PosixTcpBackend::TcpChannelDiscoveryContext::on_found_address(std::optional<cbufptr_t> addr) {
    F_LOG_D(parent->logger_, "found address");

    if (addr.has_value()) {
        // Resolved an address. If it wasn't already known, try to connect to it.
        std::vector<uint8_t> vec{addr->begin(), addr->end()};
        bool is_known = std::find_if(known_addresses.begin(), known_addresses.end(),
            [&](AddrContext& val){ return val.addr == vec; }) != known_addresses.end();

        if (!is_known) {
            AddrContext ctx = {.addr = vec};
            if (!F_LOG_IF_ERR(parent->logger_,
                    parent->start_opening_connections(parent->event_loop_,
                        parent->logger_, *addr, SOCK_STREAM, IPPROTO_TCP,
                        &ctx.connection_ctx, MEMBER_CB(this, on_connected)),
                    "failed to connect")) {
                known_addresses.push_back(ctx);
            } else {
                // TODO
            }
        }
    } else {
        // No more addresses.
        addr_resolution_ctx = nullptr;
        if (known_addresses.size() == 0) {
            // No addresses could be found. Try again using exponential backoff.
            // TODO: cancel timer on shutdown
            F_LOG_IF_ERR(parent->logger_, timer->set(lookup_period, TimerMode::kOnce),
                         "failed to set timer");
            lookup_period = std::min(lookup_period * 3.0f, 3600.0f); // exponential backoff with at most 1h period
        } else {
            // Some addresses are known from this lookup or from a previous
            // lookup. Resolve addresses again in 1h.
            // TODO: cancel timer on shutdown
            F_LOG_IF_ERR(parent->logger_, timer->set(3600.0, TimerMode::kOnce),
                         "failed to set timer");
        }
    }
}

void PosixTcpBackend::TcpChannelDiscoveryContext::on_connected(RichStatus status, socket_id_t socket_id) {
    if (!status.is_error()) {
        auto socket = new PosixSocket{}; // TODO: free
        status = socket->init(parent->event_loop_, parent->logger_, socket_id);
        if (!status.is_error()) {
            domain->add_legacy_channels({kFibreOk, socket, socket, SIZE_MAX, false}, display_name.data());
            return;
        }
        delete socket;
    }

    F_LOG_IF_ERR(parent->logger_, status, "failed to connect - will retry");

    // Try to reconnect soon
    lookup_period = 1.0f;
    resolve_address();
}

void PosixTcpBackend::TcpChannelDiscoveryContext::on_disconnected() {
    lookup_period = 1.0f; // reset exponential backoff
    resolve_address();
}

#endif
