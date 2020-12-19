
#include <fibre/libfibre.h>
#include "logging.hpp"
#include "print_utils.hpp"
#include "legacy_protocol.hpp"
#include "legacy_object_client.hpp"
#include "event_loop.hpp"
#include "channel_discoverer.hpp"
#include "string.h"
#include <algorithm>
#include "fibre/simple_serdes.hpp"
#include "fibre/callback.hpp"

#ifdef FIBRE_ENABLE_LIBUSB
#include "platform_support/libusb_transport.hpp"
#endif

DEFINE_LOG_TOPIC(LIBFIBRE);
USE_LOG_TOPIC(LIBFIBRE);

static const struct LibFibreVersion libfibre_version = { 0, 1, 0 };

class FIBRE_PRIVATE ExternalEventLoop : public EventLoop {
public:
    ExternalEventLoop(post_cb_t post,
                      register_event_cb_t register_event,
                      deregister_event_cb_t deregister_event,
                      call_later_cb_t call_later,
                      cancel_timer_cb_t cancel_timer) :
        post_(post),
        register_event_(register_event),
        deregister_event_(deregister_event),
        call_later_(call_later),
        cancel_timer_(cancel_timer) {}

    int post(void (*callback)(void*), void *ctx) final {
        return (*post_)(callback, ctx);
    }

    int register_event(int event_fd, uint32_t events, void (*callback)(void*), void* ctx) final {
        return (*register_event_)(event_fd, events, callback, ctx);
    }

    int deregister_event(int event_fd) final {
        return (*deregister_event_)(event_fd);
    }

    struct EventLoopTimer* call_later(float delay, void (*callback)(void*), void *ctx) final {
        return (*call_later_)(delay, callback, ctx);
    }

    int cancel_timer(struct EventLoopTimer* timer) final {
        return (*cancel_timer_)(timer);
    }

private:
    post_cb_t post_;
    register_event_cb_t register_event_;
    deregister_event_cb_t deregister_event_;
    call_later_cb_t call_later_;
    cancel_timer_cb_t cancel_timer_;
};

namespace fibre {

class AsyncStreamLink : public AsyncStreamSink, public AsyncStreamSource {
public:
    void start_write(cbufptr_t buffer, TransferHandle* handle, Completer<WriteResult>& completer) final;
    void cancel_write(TransferHandle transfer_handle) final;
    void start_read(bufptr_t buffer, TransferHandle* handle, Completer<ReadResult>& completer) final;
    void cancel_read(TransferHandle transfer_handle) final;
    void close(StreamStatus status);

    Completer<ReadResult>* read_completer_ = nullptr;
    bufptr_t read_buf_;
    Completer<WriteResult>* write_completer_ = nullptr;
    cbufptr_t write_buf_;
};

void AsyncStreamLink::start_write(cbufptr_t buffer, TransferHandle* handle, Completer<WriteResult>& completer) {
    if (read_completer_) {
        size_t n_copy = std::min(read_buf_.size(), buffer.size());
        memcpy(read_buf_.begin(), buffer.begin(), n_copy);
        safe_complete(read_completer_, {kStreamOk, read_buf_.begin() + n_copy});
        completer.complete({kStreamOk, buffer.begin() + n_copy});
    } else {
        if (handle) {
            *handle = reinterpret_cast<uintptr_t>(this);
        }
        write_buf_ = buffer;
        write_completer_ = &completer;
    }
}

void AsyncStreamLink::cancel_write(TransferHandle transfer_handle) {
    safe_complete(write_completer_, {kStreamCancelled, write_buf_.begin()});
}

void AsyncStreamLink::start_read(bufptr_t buffer, TransferHandle* handle, Completer<ReadResult>& completer) {
    if (write_completer_) {
        FIBRE_LOG(W) << "start_read: completing writer";
        size_t n_copy = std::min(buffer.size(), write_buf_.size());
        memcpy(buffer.begin(), write_buf_.begin(), n_copy);
        safe_complete(write_completer_, {kStreamOk, write_buf_.begin() + n_copy});
        completer.complete({kStreamOk, buffer.begin() + n_copy});
    } else {
        //FIBRE_LOG(W) << "start_read: waiting for writer";
        if (handle) {
            *handle = reinterpret_cast<uintptr_t>(this);
        }
        read_buf_ = buffer;
        read_completer_ = &completer;
    }
}

void AsyncStreamLink::cancel_read(TransferHandle transfer_handle) {
    safe_complete(read_completer_, {kStreamCancelled, read_buf_.begin()});
}

void AsyncStreamLink::close(StreamStatus status) {
    safe_complete(write_completer_, {status, write_buf_.begin()});
    safe_complete(read_completer_, {status, read_buf_.begin()});
}

}

FibreStatus convert_status(fibre::StreamStatus status) {
    switch (status) {
        case fibre::kStreamOk: return kFibreOk;
        case fibre::kStreamCancelled: return kFibreCancelled;
        case fibre::kStreamClosed: return kFibreClosed;
        default: return kFibreInternalError; // TODO: this may not always be appropriate
    }
}

fibre::StreamStatus convert_status(FibreStatus status) {
    switch (status) {
        case kFibreOk: return fibre::kStreamOk;
        case kFibreCancelled: return fibre::kStreamCancelled;
        case kFibreClosed: return fibre::kStreamClosed;
        default: return fibre::kStreamError; // TODO: this may not always be appropriate
    }
}

struct FIBRE_PRIVATE LibFibreCtx {
    ExternalEventLoop* event_loop;
    construct_object_cb_t on_construct_object;
    destroy_object_cb_t on_destroy_object;
    void* cb_ctx;
    size_t n_discoveries = 0;

    std::unordered_map<std::string, std::shared_ptr<fibre::ChannelDiscoverer>> discoverers;
};

struct FIBRE_PRIVATE LibFibreDiscoveryCtx :
        fibre::Completer<fibre::ChannelDiscoveryResult>,
        fibre::Completer<fibre::LegacyObjectClient*, std::shared_ptr<fibre::LegacyObject>>,
        fibre::Completer<fibre::LegacyObjectClient*>,
        fibre::Completer<fibre::LegacyProtocolPacketBased*, fibre::StreamStatus>
{
    void complete(fibre::ChannelDiscoveryResult result) final;
    void complete(fibre::LegacyObjectClient* obj_client, std::shared_ptr<fibre::LegacyObject> intf) final;
    void complete(fibre::LegacyObjectClient* obj_client) final;
    void complete(fibre::LegacyProtocolPacketBased* protocol, fibre::StreamStatus status) final;

    std::unordered_map<std::string, fibre::ChannelDiscoveryContext*> context_handles;

    on_found_object_cb_t on_found_object;
    void* cb_ctx;
    LibFibreCtx* ctx;

    // A LibFibreDiscoveryCtx is created when the application starts discovery
    // and is deleted when the application stopped discovery _and_ all protocol
    // instances that arose from this discovery instance were also stopped.
    size_t use_count = 1;
};

struct LibFibreTxStream : fibre::Completer<fibre::WriteResult> {
    void complete(fibre::WriteResult result) {
        if (on_completed) {
            (*on_completed)(ctx, this, convert_status(result.status), result.end);
        }
    }

    fibre::AsyncStreamSink* sink;
    fibre::TransferHandle handle;
    on_tx_completed_cb_t on_completed;
    void* ctx;
    void (*on_closed)(LibFibreTxStream*, void*, fibre::StreamStatus);
    void* on_closed_ctx;
};

struct LibFibreRxStream : fibre::Completer<fibre::ReadResult> {
    void complete(fibre::ReadResult result) {
        if (on_completed) {
            (*on_completed)(ctx, this, convert_status(result.status), result.end);
        }
    }

    fibre::AsyncStreamSource* source;
    fibre::TransferHandle handle;
    on_rx_completed_cb_t on_completed;
    void* ctx;
    void (*on_closed)(LibFibreRxStream*, void*, fibre::StreamStatus);
    void* on_closed_ctx;
};


// Callback for start_channel_discovery()
void LibFibreDiscoveryCtx::complete(fibre::ChannelDiscoveryResult result) {
    FIBRE_LOG(D) << "found channels!";

    if (result.status != kFibreOk) {
        FIBRE_LOG(W) << "discoverer stopped";
        return;
    }

    if (!result.rx_channel || !result.tx_channel) {
        FIBRE_LOG(W) << "unidirectional operation not supported yet";
        return;
    }

    use_count++;

    auto protocol = new fibre::LegacyProtocolPacketBased(result.rx_channel, result.tx_channel, result.mtu);
    protocol->client_.user_data_ = ctx;
    protocol->start(*this, *this, *this);
}

// on_found_root_object callback for LegacyProtocolPacketBased::start()
void LibFibreDiscoveryCtx::complete(fibre::LegacyObjectClient* obj_client, std::shared_ptr<fibre::LegacyObject> obj) {
    auto obj_cast = reinterpret_cast<LibFibreObject*>(obj.get()); // corresponding reverse cast in libfibre_get_attribute()
    auto intf_cast = reinterpret_cast<LibFibreInterface*>(obj->intf.get()); // corresponding reverse cast in libfibre_subscribe_to_interface()

    for (auto& obj: obj_client->objects_) {
        // If the callback handler calls libfibre_get_attribute() before
        // all objects were announced to the application then it's possible
        // that during that function call some objects are already announced
        // on-demand.
        if (!obj->known_to_application) {
            obj->known_to_application = true;
            FIBRE_LOG(D) << "constructing root object " << fibre::as_hex(reinterpret_cast<uintptr_t>(obj.get()));
            if (ctx->on_construct_object) {
                (*ctx->on_construct_object)(ctx->cb_ctx,
                    reinterpret_cast<LibFibreObject*>(obj.get()),
                    reinterpret_cast<LibFibreInterface*>(obj->intf.get()),
                    obj->intf->name.size() ? obj->intf->name.data() : nullptr, obj->intf->name.size());
            }
        }
    }

    if (on_found_object) {
        FIBRE_LOG(D) << "announcing root object " << fibre::as_hex(reinterpret_cast<uintptr_t>(obj_cast));
        (*on_found_object)(cb_ctx, obj_cast);
    }
}

// on_lost_root_object for LegacyProtocolPacketBased::start()
void LibFibreDiscoveryCtx::complete(fibre::LegacyObjectClient* obj_client) {
    if (ctx->on_destroy_object) {
        for (auto obj: obj_client->objects_) {
            auto obj_cast = reinterpret_cast<LibFibreObject*>(obj.get());
            //FIBRE_LOG(D) << "destroying subobject " << fibre::as_hex(reinterpret_cast<uintptr_t>(obj_cast));
            (*ctx->on_destroy_object)(ctx->cb_ctx, obj_cast);
        }

        obj_client->objects_.clear();
    }
}

// on_stopped callback for LegacyProtocolPacketBased::start()
void LibFibreDiscoveryCtx::complete(fibre::LegacyProtocolPacketBased* protocol, fibre::StreamStatus status) {
    delete protocol;

    if (--use_count == 0) {
        FIBRE_LOG(D) << "deleting discovery context";
        delete this;
    }
}

const struct LibFibreVersion* libfibre_get_version() {
    return &libfibre_version;
}

LibFibreCtx* libfibre_open(
        post_cb_t post,
        register_event_cb_t register_event,
        deregister_event_cb_t deregister_event,
        call_later_cb_t call_later,
        cancel_timer_cb_t cancel_timer,
        construct_object_cb_t construct_object,
        destroy_object_cb_t destroy_object,
        void* cb_ctx)
{
    //if (!register_event || !deregister_event) {
    //    FIBRE_LOG(E) << "invalid argument";
    //    return nullptr;
    //}
    FIBRE_LOG(D) << "object constructor: " << reinterpret_cast<uintptr_t>(construct_object);
    LibFibreCtx* ctx = new LibFibreCtx();
    ctx->event_loop = new ExternalEventLoop(post, register_event, deregister_event, call_later, cancel_timer);
    ctx->on_construct_object = construct_object;
    ctx->on_destroy_object = destroy_object;
    ctx->cb_ctx = cb_ctx;

#ifdef FIBRE_ENABLE_LIBUSB
    auto libusb_discoverer = std::make_shared<fibre::LibusbDiscoverer>();
    if (libusb_discoverer->init(ctx->event_loop) != 0) {
        delete ctx;
        FIBRE_LOG(E) << "failed to init libusb transport layer";
        return nullptr;
    }
    ctx->discoverers["usb"] = libusb_discoverer;
#endif

    FIBRE_LOG(D) << "opened (" << fibre::as_hex((uintptr_t)ctx) << ")";
    return ctx;
}

void libfibre_close(LibFibreCtx* ctx) {
    if (ctx->n_discoveries) {
        FIBRE_LOG(W) << "there are still discovery processes ongoing";
    }

    ctx->discoverers.clear();

    delete ctx->event_loop;
    delete ctx;

    FIBRE_LOG(D) << "closed (" << fibre::as_hex((uintptr_t)ctx) << ")";
}

struct LibFibreChannelDiscoveryCtx : fibre::ChannelDiscoveryContext {
    fibre::Completer<fibre::ChannelDiscoveryResult>* completer;
};

class ExternalDiscoverer : public fibre::ChannelDiscoverer {
    void start_channel_discovery(
        const char* specs, size_t specs_len,
        fibre::ChannelDiscoveryContext** handle,
        fibre::Completer<fibre::ChannelDiscoveryResult>& on_found_channels) final;
    int stop_channel_discovery(fibre::ChannelDiscoveryContext* handle) final;
public:
    on_start_discovery_cb_t on_start_discovery;
    on_stop_discovery_cb_t on_stop_discovery;
    void* cb_ctx;
};

void ExternalDiscoverer::start_channel_discovery(const char* specs, size_t specs_len, fibre::ChannelDiscoveryContext** handle, fibre::Completer<fibre::ChannelDiscoveryResult>& on_found_channels) {
    LibFibreChannelDiscoveryCtx* ctx = new LibFibreChannelDiscoveryCtx{};
    ctx->completer = &on_found_channels;
    if (handle) {
        *handle = ctx;
    }
    if (on_start_discovery) {
        (*on_start_discovery)(cb_ctx, ctx, specs, specs_len);
    }
}

int ExternalDiscoverer::stop_channel_discovery(fibre::ChannelDiscoveryContext* handle) {
    LibFibreChannelDiscoveryCtx* ctx = static_cast<LibFibreChannelDiscoveryCtx*>(handle);
    if (on_stop_discovery) {
        (*on_stop_discovery)(cb_ctx, ctx);
    }
    delete ctx;
    return 0;
}

void libfibre_register_discoverer(LibFibreCtx* ctx, const char* name, size_t name_length, on_start_discovery_cb_t on_start_discovery, on_stop_discovery_cb_t on_stop_discovery, void* cb_ctx) {
    std::string name_str = {name, name + name_length};
    if (ctx->discoverers.find(name_str) != ctx->discoverers.end()) {
        FIBRE_LOG(W) << "Discoverer " << name_str << " already registered";
        return; // TODO: report status
    }

    auto disc = std::make_shared<ExternalDiscoverer>();
    disc->on_start_discovery = on_start_discovery;
    disc->on_stop_discovery = on_stop_discovery;
    disc->cb_ctx = cb_ctx;
    ctx->discoverers[name_str] = disc;
}

void libfibre_add_channels(LibFibreCtx* ctx, LibFibreChannelDiscoveryCtx* discovery_ctx, LibFibreRxStream** tx_channel, LibFibreTxStream** rx_channel, size_t mtu) {
    fibre::AsyncStreamLink* tx_link = new fibre::AsyncStreamLink();
    fibre::AsyncStreamLink* rx_link = new fibre::AsyncStreamLink();
    LibFibreRxStream* tx = new LibFibreRxStream();
    LibFibreTxStream* rx = new LibFibreTxStream();
    tx->source = tx_link;
    rx->sink = rx_link;

    tx->on_closed = [](LibFibreRxStream* stream, void* ctx, fibre::StreamStatus status) {
        auto link = reinterpret_cast<fibre::AsyncStreamLink*>(ctx);
        link->close(status);
        delete link;
        delete stream;
    };
    tx->on_closed_ctx = tx_link;
    rx->on_closed = [](LibFibreTxStream* stream, void* ctx, fibre::StreamStatus status) {
        auto link = reinterpret_cast<fibre::AsyncStreamLink*>(ctx);
        link->close(status);
        delete link;
        delete stream;
    };
    rx->on_closed_ctx = rx_link;

    if (tx_channel) {
        *tx_channel = tx;
    }

    if (rx_channel) {
        *rx_channel = rx;
    }

    fibre::ChannelDiscoveryResult result = {kFibreOk, rx_link, tx_link, mtu};
    discovery_ctx->completer->complete(result);
}

void libfibre_start_discovery(LibFibreCtx* ctx, const char* specs, size_t specs_len, struct LibFibreDiscoveryCtx** handle,
        on_found_object_cb_t on_found_object, on_stopped_cb_t on_stopped, void* cb_ctx) {
    if (!ctx) {
        FIBRE_LOG(E) << "invalid argument";
        if (on_stopped) {
            (*on_stopped)(cb_ctx, kFibreInvalidArgument);
        }
        return;
    }

    const char* prev_delim = specs;

    FIBRE_LOG(D) << "starting discovery with path \"" << std::string(specs, specs_len) << "\"";

    LibFibreDiscoveryCtx* discovery_ctx = new LibFibreDiscoveryCtx();
    discovery_ctx->on_found_object = on_found_object;
    discovery_ctx->cb_ctx = cb_ctx;
    discovery_ctx->ctx = ctx;

    if (handle) {
        *handle = discovery_ctx;
    }

    while (prev_delim < specs + specs_len) {
        const char* next_delim = std::find(prev_delim, specs + specs_len, ';');
        const char* colon = std::find(prev_delim, next_delim, ':');
        const char* colon_end = std::min(colon + 1, next_delim);

        std::string name{prev_delim, colon};
        auto it = ctx->discoverers.find(name);

        if (it == ctx->discoverers.end()) {
            FIBRE_LOG(W) << "transport layer \"" << name << "\" not implemented";
        } else {
            discovery_ctx->context_handles[name] = nullptr;
            it->second->start_channel_discovery(colon_end, next_delim - colon_end,
                    &discovery_ctx->context_handles[name], *discovery_ctx);
        }

        prev_delim = std::min(next_delim + 1, specs + specs_len);
    }

    ctx->n_discoveries++;
}

void libfibre_stop_discovery(LibFibreCtx* ctx, LibFibreDiscoveryCtx* discovery_ctx) {
    if (!ctx->n_discoveries) {
        FIBRE_LOG(W) << "stopping a discovery process but none is active";
    } else {
        ctx->n_discoveries--;
    }

    for (auto& it: discovery_ctx->context_handles) {
        ctx->discoverers[it.first]->stop_channel_discovery(it.second);
    }
    discovery_ctx->context_handles.clear();

    if (--discovery_ctx->use_count == 0) {
        FIBRE_LOG(D) << "deleting discovery context";
        delete discovery_ctx;
    }
}


LibFibreFunction* to_c(fibre::Function* ptr) {
    return reinterpret_cast<LibFibreFunction*>(ptr);
}
fibre::Function* from_c(LibFibreFunction* ptr) {
    return reinterpret_cast<fibre::Function*>(ptr);
}
void** from_c(LibFibreCallContext** ptr) {
    return reinterpret_cast<void**>(ptr);
}


void libfibre_subscribe_to_interface(LibFibreInterface* interface,
        on_attribute_added_cb_t on_attribute_added,
        on_attribute_removed_cb_t on_attribute_removed,
        on_function_added_cb_t on_function_added,
        on_function_removed_cb_t on_function_removed,
        void* cb_ctx)
{
    auto intf = reinterpret_cast<fibre::FibreInterface*>(interface); // corresponding reverse cast in LibFibreDiscoveryCtx::complete() and libfibre_subscribe_to_interface()

    for (auto& func: intf->functions) {
        std::vector<const char*> input_names = {"obj"};
        std::vector<const char*> input_codecs = {"object_ref"};
        std::vector<const char*> output_names;
        std::vector<const char*> output_codecs;
        for (auto& arg: func.second.inputs) {
            input_names.push_back(arg.name.data());
            input_codecs.push_back(arg.app_codec.data());
        }
        for (auto& arg: func.second.outputs) {
            output_names.push_back(arg.name.data());
            output_codecs.push_back(arg.app_codec.data());
        }
        input_names.push_back(nullptr);
        input_codecs.push_back(nullptr);
        output_names.push_back(nullptr);
        output_codecs.push_back(nullptr);

        if (on_function_added) {
            (*on_function_added)(cb_ctx,
                to_c(&func.second),
                func.first.data(), func.first.size(),
                input_names.data(), input_codecs.data(),
                output_names.data(), output_codecs.data());
        }
    }

    for (auto& attr: intf->attributes) {
        if (on_attribute_added) {
            (*on_attribute_added)(cb_ctx,
                reinterpret_cast<LibFibreAttribute*>(&attr.second), // corresponding reverse cast in libfibre_get_attribute()
                attr.first.data(), attr.first.size(),
                reinterpret_cast<LibFibreInterface*>(attr.second.object->intf.get()), // corresponding reverse cast in libfibre_subscribe_to_interface()
                attr.second.object->intf->name.size() ? attr.second.object->intf->name.data() : nullptr, attr.second.object->intf->name.size()
            );
        }
    }
}

FibreStatus libfibre_get_attribute(LibFibreObject* parent_obj, LibFibreAttribute* attr, LibFibreObject** child_obj_ptr) {
    if (!parent_obj || !attr) {
        return kFibreInvalidArgument;
    }

    fibre::LegacyObject* parent_obj_cast = reinterpret_cast<fibre::LegacyObject*>(parent_obj);
    fibre::LegacyFibreAttribute* attr_cast = reinterpret_cast<fibre::LegacyFibreAttribute*>(attr); // corresponding reverse cast in libfibre_subscribe_to_interface()
    auto& attributes = parent_obj_cast->intf->attributes;

    bool is_member = std::find_if(attributes.begin(), attributes.end(),
        [&](std::pair<const std::string, fibre::LegacyFibreAttribute>& kv) {
            return &kv.second == attr_cast;
        }) != attributes.end();

    if (!is_member) {
        FIBRE_LOG(W) << "attempt to fetch attribute from an object that does not implement it";
        return kFibreInvalidArgument;
    }

    LibFibreCtx* libfibre_ctx = reinterpret_cast<LibFibreCtx*>(parent_obj_cast->client->user_data_);
    fibre::LegacyObject* child_obj = attr_cast->object.get();

    if (!attr_cast->object->known_to_application) {
        attr_cast->object->known_to_application = true;

        if (libfibre_ctx->on_construct_object) {
            //FIBRE_LOG(D) << "constructing subobject " << fibre::as_hex(reinterpret_cast<uintptr_t>(child_obj));
            (*libfibre_ctx->on_construct_object)(libfibre_ctx->cb_ctx,
                reinterpret_cast<LibFibreObject*>(child_obj),
                reinterpret_cast<LibFibreInterface*>(child_obj->intf.get()),
                child_obj->intf->name.size() ? child_obj->intf->name.data() : nullptr, child_obj->intf->name.size());
        }
    }

    if (child_obj_ptr) {
        *child_obj_ptr = reinterpret_cast<LibFibreObject*>(child_obj);
    }

    return kFibreOk;
}

/**
 * @brief Inserts or removes the specified number of elements
 * @param delta: Positive value: insert elements, negative value: remove elements
 */
void resize_at(std::vector<uint8_t>& vec, size_t pos, ssize_t delta) {
    if (delta > 0) {
        std::fill_n(std::inserter(vec, vec.begin() + pos), delta, 0);
    } else {
        vec.erase(std::min(vec.begin() + pos, vec.end()),
                  std::min(vec.begin() + pos + -delta, vec.end()));
    }
}

FibreStatus libfibre_call(LibFibreFunction* func, LibFibreCallContext** handle,
        FibreStatus status,
        const unsigned char* tx_buf, size_t tx_len,
        unsigned char* rx_buf, size_t rx_len,
        const unsigned char** tx_end,
        unsigned char** rx_end,
        libfibre_call_cb_t callback, void* cb_ctx) {
    bool valid_args = func && handle
                   && (!tx_len || tx_buf) // tx_buf valid
                   && (!rx_len || rx_buf) // rx_buf valid
                   && tx_end && rx_end // tx_end, rx_end valid
                   && ((status != kFibreOk) || tx_len || rx_len || !handle); // progress
    if (!valid_args) {
        FIBRE_LOG(E) << "invalid argument";
        return kFibreInvalidArgument;
    }

    struct Ctx { libfibre_call_cb_t callback; void* ctx; };
    struct Ctx* ctx = new Ctx{callback, cb_ctx};

    fibre::Callback<std::optional<fibre::CallBuffers>, fibre::CallBufferRelease> cb{
        [](void* ctx_, fibre::CallBufferRelease result) -> std::optional<fibre::CallBuffers> {
            auto ctx = reinterpret_cast<Ctx*>(ctx_);
            const unsigned char* tx_buf;
            size_t tx_len;
            unsigned char* rx_buf;
            size_t rx_len;
            auto status = ctx->callback(ctx->ctx, result.status, result.tx_end, result.rx_end, &tx_buf, &tx_len, &rx_buf, &rx_len);
            if (status == kFibreBusy) {
                delete ctx;
                return std::nullopt;
            } else {
                return fibre::CallBuffers{status, {tx_buf, tx_len}, {rx_buf, rx_len}};
            }
    }, ctx};


    auto response = from_c(func)->call(from_c(handle), {status, {tx_buf, tx_len}, {rx_buf, rx_len}}, cb);

    if (!response.has_value()) {
        return kFibreBusy;
    } else {
        delete ctx;
        *tx_end = response->tx_end;
        *rx_end = response->rx_end;
        return response->status;
    }
}

void libfibre_start_tx(LibFibreTxStream* tx_stream,
        const uint8_t* tx_buf, size_t tx_len, on_tx_completed_cb_t on_completed,
        void* ctx) {
    tx_stream->on_completed = on_completed;
    tx_stream->ctx = ctx;
    tx_stream->sink->start_write({tx_buf, tx_len}, &tx_stream->handle, *tx_stream);
}

void libfibre_cancel_tx(LibFibreTxStream* tx_stream) {
    tx_stream->sink->cancel_write(tx_stream->handle);
}

void libfibre_close_tx(LibFibreTxStream* tx_stream, FibreStatus status) {
    if (tx_stream->on_closed) {
        (tx_stream->on_closed)(tx_stream, tx_stream->on_closed_ctx, convert_status(status));
    }
}

void libfibre_start_rx(LibFibreRxStream* rx_stream,
        uint8_t* rx_buf, size_t rx_len, on_rx_completed_cb_t on_completed,
        void* ctx) {
    rx_stream->on_completed = on_completed;
    rx_stream->ctx = ctx;
    rx_stream->source->start_read({rx_buf, rx_len}, &rx_stream->handle, *rx_stream);
}

void libfibre_cancel_rx(LibFibreRxStream* rx_stream) {
    rx_stream->source->cancel_read(rx_stream->handle);
}

void libfibre_close_rx(LibFibreRxStream* rx_stream, FibreStatus status) {
    if (rx_stream->on_closed) {
        (rx_stream->on_closed)(rx_stream, rx_stream->on_closed_ctx, convert_status(status));
    }
}
