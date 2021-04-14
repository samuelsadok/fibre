
#include <fibre/fibre.hpp>
#include <fibre/channel_discoverer.hpp>
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

#if FIBRE_ENABLE_EVENT_LOOP
#  ifdef __linux__
#    include "platform_support/epoll_event_loop.hpp"
using EventLoopImpl = fibre::EpollEventLoop;
#  else
#    error "No event loop implementation available for this operating system."
#  endif
#endif

using namespace fibre;

struct DiscoveryContext {
};

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

struct BackendInitializer {
    template<typename T>
    bool operator()(T& backend) {
        if (F_LOG_IF_ERR(ctx->logger, backend.init(ctx->event_loop, ctx->logger), "backend init failed")) {
            return false;
        }
        ctx->register_backend(backend.get_name(), &backend);
        return true;
    }
    Context* ctx;
};
struct BackendDeinitializer {
    template<typename T>
    bool operator()(T& backend) {
        ctx->deregister_backend(backend.get_name());
        if (F_LOG_IF_ERR(ctx->logger, backend.deinit(), "backend deinit failed")) {
            return false;
        }
        return true;
    }
    Context* ctx;
};

template<typename ... T, size_t ... Is>
bool all(std::tuple<T...> args, std::index_sequence<Is...>) {
    std::array<bool, sizeof...(T)> arr = { std::get<Is>(args) ... };
    return std::all_of(arr.begin(), arr.end(), [](bool val) { return val; });
}

template<typename ... T>
bool all(std::tuple<T...> args) {
    return all(args, std::make_index_sequence<sizeof...(T)>());
}

RichStatus fibre::open(EventLoop* event_loop, Logger logger, Context** p_ctx) {
    Context* ctx = my_alloc<Context>();
    ctx->logger = logger;
    F_RET_IF(!ctx, "already opened");

    ctx->event_loop = event_loop;
    auto static_backends_good = for_each_in_tuple(BackendInitializer{ctx},
            ctx->static_backends);

    F_RET_IF(!all(static_backends_good), "some backends failed to initialize");
    // TODO: shutdown backends on error

    if (p_ctx) {
        *p_ctx = ctx;
    }

    return RichStatus::success();
}


void fibre::close(Context* ctx) {
    F_LOG_IF(ctx->logger, ctx->n_domains, ctx->n_domains << " domains are still open");

    for_each_in_tuple(BackendDeinitializer{ctx},
            ctx->static_backends);

    Logger logger = ctx->logger;
    F_LOG_IF_ERR(logger, my_free<Context>(ctx), "failed to free context");
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

void fibre::log_to_stderr(const char* file, unsigned line, int level, uintptr_t info0, uintptr_t info1, const char* text) {
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
void fibre::log_to_stderr(const char* file, unsigned line, int level, uintptr_t info0, uintptr_t info1, const char* text) {
    // ignore
}
#endif

#if FIBRE_ALLOW_HEAP
Domain* Context::create_domain(std::string specs) {
    F_LOG_D(logger, "creating domain with path \"" << specs << "\"");

    Domain* domain = new Domain(); // deleted in close_domain
    domain->ctx = this;

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

void Context::close_domain(Domain* domain) {
    F_LOG_D(logger, "closing domain");
    for (auto& it: domain->channel_discovery_handles) {
        F_LOG_D(logger, "stopping discoverer");
        discoverers[it.first]->stop_channel_discovery(it.second);
    }
    domain->channel_discovery_handles.clear();
    delete domain;
    n_domains--;
}

void Context::register_backend(std::string name, ChannelDiscoverer* backend) {
    if (discoverers.find(name) != discoverers.end()) {
        F_LOG_E(logger, "Discoverer " << name << " already registered");
        return; // TODO: report status
    }
    
    discoverers[name] = backend;
}

void Context::deregister_backend(std::string name) {
    auto it = discoverers.find(name);
    if (it == discoverers.end()) {
        F_LOG_E(logger, "Discoverer " << name << " not registered");
        return; // TODO: report status
    }
    
    discoverers.erase(it);
}
#endif

#if FIBRE_ENABLE_CLIENT
void Domain::start_discovery(Callback<void, Object*, Interface*> on_found_object, Callback<void, Object*> on_lost_object) {
    on_found_object_ = on_found_object;
    on_lost_object_ = on_lost_object;
    for (auto& it: root_objects_) {
        on_found_object_.invoke(it.first, it.second);
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

void Domain::add_channels(ChannelDiscoveryResult result) {
    F_LOG_D(ctx->logger, "found channels!");

    if (result.status != kFibreOk) {
        F_LOG_E(ctx->logger, "discoverer stopped");
        return;
    }

    if (!result.rx_channel || !result.tx_channel) {
        F_LOG_E(ctx->logger, "unidirectional operation not supported yet");
        return;
    }

#if FIBRE_ENABLE_CLIENT || FIBRE_ENABLE_SERVER
    // Deleted during on_stopped()
    auto protocol = new fibre::LegacyProtocolPacketBased(this, result.rx_channel, result.tx_channel, result.mtu);
#if FIBRE_ENABLE_CLIENT
    protocol->start(MEMBER_CB(this, on_found_root_object), MEMBER_CB(this, on_lost_root_object), MEMBER_CB(this, on_stopped));
#else
    protocol->start(MEMBER_CB(this, on_stopped));
#endif
#endif
}

#if FIBRE_ENABLE_CLIENT
void Domain::on_found_root_object(LegacyObjectClient* obj_client, std::shared_ptr<LegacyObject> obj) {
    Object* root_object = reinterpret_cast<Object*>(obj.get());
    Interface* root_intf = reinterpret_cast<Interface*>(obj->intf.get());
    root_objects_[root_object] = root_intf;
    on_found_object_.invoke(root_object, root_intf);
}

void Domain::on_lost_root_object(LegacyObjectClient* obj_client, std::shared_ptr<LegacyObject> obj) {
    Object* root_object = reinterpret_cast<Object*>(obj.get());
    auto it = root_objects_.find(root_object);
    root_objects_.erase(it);
    on_lost_object_.invoke(root_object);
}
#endif

void Domain::on_stopped(LegacyProtocolPacketBased* protocol, StreamStatus status) {
    delete protocol;
}

#if FIBRE_ENABLE_SERVER
ServerFunctionDefinition* Domain::get_server_function(ServerFunctionId id) {
    if (id < n_static_server_functions) {
        return &static_server_function_table[id];
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
