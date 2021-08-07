
#include <fibre/fibre.hpp>
#include <fibre/channel_discoverer.hpp>
#include <fibre/cpp_utils.hpp>
#include "legacy_protocol.hpp"
#include "print_utils.hpp"
#include <memory>
#include <algorithm>
#include <array>
#include "static_exports.hpp"

#if FIBRE_ALLOW_HEAP
#include <unordered_map>
#include <string>
#endif

#if FIBRE_ENABLE_TEXT_LOGGING
#include <chrono>
#endif

#if FIBRE_ENABLE_EVENT_LOOP
#  ifdef __linux__
#    include "platform_support/epoll_event_loop.hpp"
using EventLoopImpl = fibre::EpollEventLoop;
#  else
#    error "No event loop implementation available for this operating system."
#  endif
#endif

#if FIBRE_ENABLE_LIBUSB_BACKEND
#include "platform_support/libusb_backend.hpp"
#endif

#if FIBRE_ENABLE_WEBUSB_BACKEND
#include "platform_support/webusb_backend.hpp"
#endif

#if FIBRE_ENABLE_TCP_CLIENT_BACKEND
#include "platform_support/posix_tcp_backend.hpp"
#endif

#if FIBRE_ENABLE_SOCKET_CAN_BACKEND
#include "platform_support/socket_can.hpp"
#endif

using namespace fibre;

#if FIBRE_ALLOW_HEAP

template<typename T>
T* my_alloc() {
    return new T{};
}

template<typename T>
RichStatus my_free(T* ctx) {
    delete ctx;
    return RichStatus::success();
}

#else

template<typename T>
struct TheInstance {
    static T instance;
    static bool in_use;
};

template<typename T> T TheInstance<T>::instance{};
template<typename T> bool TheInstance<T>::in_use = false;

template<typename T>
T* my_alloc() {
    if (!TheInstance<T>::in_use) {
        TheInstance<T>::in_use = true;
        return &TheInstance<T>::instance;
    } else {
        return nullptr;
    }
}

template<typename T>
RichStatus my_free(T* ctx) {
    if (ctx == &TheInstance<T>::instance) {
        TheInstance<T>::in_use = false;
        return RichStatus::success();
    } else {
        return F_MAKE_ERR("bad instance");
    }
}

#endif

RichStatus fibre::launch_event_loop(Logger logger, Callback<void, EventLoop*> on_started) {
#if FIBRE_ENABLE_EVENT_LOOP
    EventLoopImpl* event_loop = my_alloc<EventLoopImpl>(); // TODO: free
    return event_loop->start(logger, [&](){ on_started.invoke(event_loop); });
#else
    return F_MAKE_ERR("event loop support not enabled");
#endif
}

RichStatus fibre::open(EventLoop* event_loop, Logger logger, Fibre** p_ctx) {
    Fibre* ctx = my_alloc<Fibre>();
    ctx->logger = logger;
    F_RET_IF(!ctx, "already opened");

    ctx->event_loop = event_loop;

    RichStatus status;
#if FIBRE_ENABLE_LIBUSB_BACKEND
    if (status.is_success()) {
        status = ctx->init_backend("usb", new LibUsbBackend{});
    }
#endif
#if FIBRE_ENABLE_WEBUSB_BACKEND
    if (status.is_success()) {
        status = ctx->init_backend("usb", new WebusbBackend{});
    }
#endif
#if FIBRE_ENABLE_TCP_CLIENT_BACKEND
    if (status.is_success()) {
        status = ctx->init_backend("tcp-client", new PosixTcpClientBackend{});
    }
#endif
#if FIBRE_ENABLE_TCP_SERVER_BACKEND
    if (status.is_success()) {
        status = ctx->init_backend("tcp-server", new PosixTcpServerBackend{});
    }
#endif
#if FIBRE_ENABLE_SOCKET_CAN_BACKEND
    if (status.is_success()) {
        status = ctx->init_backend("can", new SocketCanBackend{});
    }
#endif

    if (status.is_error()) {
        ctx->deinit_backends();
    } else {
        if (p_ctx) {
            *p_ctx = ctx;
        }
    }

    return status;
}


void fibre::close(Fibre* ctx) {
    F_LOG_IF(ctx->logger, ctx->n_domains, ctx->n_domains << " domains are still open");

    

    Logger logger = ctx->logger;
    F_LOG_IF_ERR(logger, my_free<Fibre>(ctx), "failed to free context");
}

#if FIBRE_ENABLE_TEXT_LOGGING
static std::string get_local_time() {
    auto now(std::chrono::system_clock::now());
    auto seconds_since_epoch(
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()));

    // Construct time_t using 'seconds_since_epoch' rather than 'now' since it is
    // implementation-defined whether the value is rounded or truncated.
    std::time_t now_t(
        std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::time_point(seconds_since_epoch)));

    char temp[10];
    if (!std::strftime(temp, 10, "%H:%M:%S.", std::localtime(&now_t))) {
        return "";
    }

    return std::string(temp) +
        std::to_string((now.time_since_epoch() - seconds_since_epoch).count());
}

void fibre::log_to_stderr(void* ctx, const char* file, unsigned line, int level, uintptr_t info0, uintptr_t info1, const char* text) {
    switch ((LogLevel)level) {
    case LogLevel::kDebug:
        //std::cerr << "\x1b[93;1m"; // yellow
        break;
    case LogLevel::kError:
        std::cerr << "\x1b[91;1m"; // red
        break;
    default:
        break;
    }
    std::cerr << get_local_time() << " [" << file << ":" << line << "] " << text << "\x1b[0m" << std::endl;
}
#else
void fibre::log_to_stderr(void* ctx, const char* file, unsigned line, int level, uintptr_t info0, uintptr_t info1, const char* text) {
    // ignore
}
#endif

#if FIBRE_ALLOW_HEAP
Domain* Fibre::create_domain(std::string specs, const uint8_t* node_id, F_CONFIG_ENABLE_CLIENT_T enable_client) {
    F_LOG_D(logger, "creating domain with path \"" << specs << "\"");

    Domain* domain = new Domain(); // deleted in close_domain
    domain->ctx = this;
#if FIBRE_ENABLE_CLIENT == F_RUNTIME_CONFIG
    domain->enable_client = enable_client;
#endif
    std::copy_n(node_id, 16, domain->node_id.begin());

    for (size_t i = 0; i < 16; i += 4) {
        domain->rng.seed(node_id[i], node_id[i + 1], node_id[i + 2], node_id[i + 3]);
    }

    std::string::iterator prev_delim = specs.begin();
    while (prev_delim < specs.end()) {
        auto next_delim = std::find(prev_delim, specs.end(), ';');
        auto colon = std::find(prev_delim, next_delim, ':');
        auto colon_end = std::min(colon + 1, next_delim);

        std::string name{prev_delim, colon};
        auto it = discoverers.find(name);

        if (it == discoverers.end()) {
            F_LOG_E(logger, "transport layer \"" << name << "\" not implemented");
        } else {
            domain->channel_discovery_handles[name] = nullptr;
            it->second->start_channel_discovery(domain,
                    &*colon_end, next_delim - colon_end,
                    &domain->channel_discovery_handles[name]);
        }

        prev_delim = std::min(next_delim + 1, specs.end());
    }

    n_domains++;
    return domain;
}

void Fibre::close_domain(Domain* domain) {
    F_LOG_D(logger, "closing domain");
    for (auto& it: domain->channel_discovery_handles) {
        F_LOG_D(logger, "stopping discoverer");
        F_LOG_IF_ERR(logger, discoverers[it.first]->stop_channel_discovery(it.second), "failed to stop discoverer");
    }
    domain->channel_discovery_handles.clear();
    delete domain;
    n_domains--;
}

RichStatus Fibre::init_backend(std::string name, Backend* backend) {
    F_RET_IF_ERR(backend->init(event_loop, logger), "init failed");
    F_RET_IF_ERR(register_backend(name, backend), "registering failed");
    return RichStatus::success();
}

RichStatus Fibre::deinit_backends() {
    RichStatus status;
    for (auto disc: discoverers) {
        Backend* backend = dynamic_cast<Backend*>(disc.second);
        if (backend) {
            RichStatus s = backend->deinit();
            if (status.is_success()) {
                status = s;
            }
            delete backend;
        }
    }
    return status;
}

RichStatus Fibre::register_backend(std::string name, ChannelDiscoverer* backend) {
    F_RET_IF(discoverers.find(name) != discoverers.end(),  "Discoverer " << name << " already registered");
    discoverers[name] = backend;
    return RichStatus::success();
}

RichStatus Fibre::deregister_backend(std::string name) {
    auto it = discoverers.find(name);
    F_RET_IF(it == discoverers.end(), "Discoverer " << name << " not registered");
    discoverers.erase(it);
    return RichStatus::success();
}
#endif

void Domain::show_device_dialog(std::string backend) {
    if (F_LOG_IF(ctx->logger, channel_discovery_handles.find(backend) == channel_discovery_handles.end(), backend << " not running")) {
        return;
    }
    F_LOG_IF_ERR(ctx->logger, ctx->discoverers[backend]->show_device_dialog(), "can't show device dialog");
}

#if FIBRE_ENABLE_CLIENT
void Domain::start_discovery(Callback<void, Object*, Interface*, std::string> on_found_object, Callback<void, Object*> on_lost_object) {
    on_found_object_ = on_found_object;
    on_lost_object_ = on_lost_object;
    for (auto& it: root_objects_) {
        on_found_object_.invoke(it.first, it.second.first, it.second.second);
    }
}

void Domain::stop_discovery() {
    auto on_lost_object = on_lost_object_;
    on_found_object_ = nullptr;
    on_lost_object_ = nullptr;
    for (auto& it: root_objects_) {
        on_lost_object.invoke(it.first);
    }
}
#endif

void Domain::add_legacy_channels(ChannelDiscoveryResult result, const char* name) {
    F_LOG_D(ctx->logger, "found channels!");

    if (result.status != kFibreOk) {
        F_LOG_E(ctx->logger, "discoverer stopped");
        return;
    }

    if (!result.rx_channel || !result.tx_channel) {
        F_LOG_E(ctx->logger, "unidirectional operation not supported yet");
        return;
    }

    if (result.mtu < 12) {
        F_LOG_E(ctx->logger, "MTU too small");
        return;
    }

#if FIBRE_ENABLE_CLIENT || FIBRE_ENABLE_SERVER
    if (result.packetized) {
        // Deleted during on_stopped_p()
        auto protocol = new fibre::LegacyProtocolPacketBased(this, result.rx_channel, result.tx_channel, result.mtu, name);
#if FIBRE_ENABLE_CLIENT
        protocol->start(MEMBER_CB(this, on_stopped_p));
#endif
    } else {
        // Deleted during on_stopped_s()
        auto protocol = new fibre::LegacyProtocolStreamBased(this, result.rx_channel, result.tx_channel, name);
#if FIBRE_ENABLE_CLIENT
        protocol->start(MEMBER_CB(this, on_stopped_s));
#endif  
    }
#endif
}


void Domain::on_found_node(const NodeId& node_id, FrameStreamSink* sink, const char* intf_name, Node** p_node) {
    Node* node;

    auto it = nodes.find(node_id);
    if (it != nodes.end()) {
        node = &it->second;
    } else {
        node = nodes.alloc(node_id);
        if (!node) {
            F_LOG_W(ctx->logger, "ignoring node (out of memory)");
            *p_node = nullptr;
            return;
        }
        node->id = node_id; // TODO: this is redundant in memory use
    }

    *p_node = node;

    if (std::find(node->sinks.begin(), node->sinks.end(), sink) != node->sinks.end()) {
        return; // this node/sink combination is already known
    }

    if (!node->sinks.alloc(sink)) {
        F_LOG_W(ctx->logger, "ignoring sink (out of memory)");
        return;
    }

#if FIBRE_ENABLE_CLIENT
#if FIBRE_ENABLE_CLIENT != F_RUNTIME_CONFIG
    bool enable_client = true;
#endif
    if (enable_client) {
        *p_node = node;

        F_LOG_D(ctx->logger, "connecting to node");

        std::array<uint8_t, 16> call_id;
        rng.get_random(call_id);
        std::array<uint8_t, 16> tx_call_id = call_id;
        tx_call_id[15] ^= 1;
        EndpointClientConnection* conn = client_connections.alloc(call_id, this, tx_call_id); // TODO: free

        auto client = new LegacyObjectClient{}; // TODO: free
        client->start(node, this, MEMBER_CB(conn, start_call), intf_name);
        if (!conn->open_tx_slot(sink, node)) {
            F_LOG_W(ctx->logger, "cannot connect connection with sink (either of the two out of memory)");
        }
    } else
#endif
    {
        F_LOG_D(ctx->logger, "ignoring node");
    }
}

void Domain::on_lost_node(Node* node, FrameStreamSink* sink) {
#if FIBRE_ENABLE_CLIENT
#if FIBRE_ENABLE_CLIENT != F_RUNTIME_CONFIG
    bool enable_client = true;
#endif
    if (enable_client) {
        for (auto& conn: client_connections) {
            F_LOG_D(ctx->logger, "disconnecting from node");
            conn.second.close_tx_slot(sink);
        }
    }
#endif
}

#if FIBRE_ENABLE_CLIENT
void Domain::on_found_root_object(Object* obj, Interface* intf, std::string path) {
    root_objects_[obj] = {intf, path};
    on_found_object_.invoke(obj, intf, path);
}

void Domain::on_lost_root_object(Object* obj) {
    auto it = root_objects_.find(obj);
    root_objects_.erase(it);
    on_lost_object_.invoke(obj);
}
#endif

void Domain::on_stopped_p(LegacyProtocolPacketBased* protocol, StreamStatus status) {
    delete protocol;
}

void Domain::on_stopped_s(LegacyProtocolPacketBased* protocol, StreamStatus status) {
    size_t offset = (size_t)&((LegacyProtocolStreamBased*)nullptr)->inner_protocol_;
    delete (LegacyProtocolStreamBased*)((uintptr_t)protocol - offset);
}

#if FIBRE_ENABLE_SERVER
const Function* Domain::get_server_function(ServerFunctionId id) {
    if (id < n_static_server_functions) {
        return static_server_function_table[id];
    }
    return nullptr;
}
ServerObjectDefinition* Domain::get_server_object(ServerObjectId id) {
    if (id < n_static_server_objects) {
        return &static_server_object_table[id];
    }
    return nullptr;
}
#endif

void Domain::open_call(const std::array<uint8_t, 16>& call_id, uint8_t protocol, FrameStreamSink* return_path, Node* return_node, ConnectionInputSlot** slot) {
    *slot = nullptr;

#if FIBRE_ENABLE_SERVER
    if (protocol == 0x00) {
        // inbound call stream

        Connection* conn = server_connections.get(call_id);
        if (!conn) {
            // TODO: check if the call was recently closed and if so then ignore
            // the chunk.

            std::array<uint8_t, 16> tx_call_id = call_id;
            tx_call_id[15] ^= 1;
            conn = server_connections.alloc(call_id, this, tx_call_id);
            if (!conn) {
                return; // TODO: log out-of-memory
            }
        }

        *slot = conn->open_rx_slot();

        auto stream_slot = conn->open_tx_slot(return_path, return_node);
    }
#endif
    
#if FIBRE_ENABLE_CLIENT
    if (protocol == 0x01) {
        // call return stream

        Connection* conn = client_connections.get(call_id);
        F_LOG_T(ctx->logger, "got response on call " << as_hex(call_id));
        if (!conn) {
            return; // TODO: log unexpected call (can happen if the call was recently closed)
        }

        *slot = conn->open_rx_slot();
    }
#endif


}

void Domain::close_call(ConnectionInputSlot* slot) {
    // TODO
}

