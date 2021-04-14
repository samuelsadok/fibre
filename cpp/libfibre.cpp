
#include <fibre/libfibre.h>
#include <fibre/fibre.hpp>
#include "print_utils.hpp"
#include "legacy_protocol.hpp" // TODO: remove this include
#include "legacy_object_client.hpp" // TODO: remove this include
#include <algorithm>
#include <string.h>

using namespace fibre;

struct LibFibreChannelDiscoveryCtx {
    fibre::Domain* domain;
};

LibFibreFunction* to_c(fibre::Function* ptr) {
    return reinterpret_cast<LibFibreFunction*>(ptr);
}
fibre::Function* from_c(LibFibreFunction* ptr) {
    return reinterpret_cast<fibre::Function*>(ptr);
}
void** from_c(LibFibreCallContext** ptr) {
    return reinterpret_cast<void**>(ptr);
}
LibFibreDomain* to_c(fibre::Domain* ptr) {
    return reinterpret_cast<LibFibreDomain*>(ptr);
}
fibre::Domain* from_c(LibFibreDomain* ptr) {
    return reinterpret_cast<fibre::Domain*>(ptr);
}
LibFibreObject* to_c(fibre::Object* ptr) {
    return reinterpret_cast<LibFibreObject*>(ptr);
}
fibre::Object* from_c(LibFibreObject* ptr) {
    return reinterpret_cast<fibre::Object*>(ptr);
}
LibFibreInterface* to_c(fibre::Interface* ptr) {
    return reinterpret_cast<LibFibreInterface*>(ptr);
}
fibre::Interface* from_c(LibFibreInterface* ptr) {
    return reinterpret_cast<fibre::Interface*>(ptr);
}
LibFibreStatus to_c(fibre::Status status) {
    return static_cast<LibFibreStatus>(status);
}
fibre::Status from_c(LibFibreStatus status) {
    return static_cast<fibre::Status>(status);
}
LibFibreChannelDiscoveryCtx* to_c(fibre::ChannelDiscoveryContext* ptr) {
    return reinterpret_cast<LibFibreChannelDiscoveryCtx*>(ptr);
}
fibre::ChannelDiscoveryContext* from_c(LibFibreChannelDiscoveryCtx* ptr) {
    return reinterpret_cast<fibre::ChannelDiscoveryContext*>(ptr);
}


static const struct LibFibreVersion libfibre_version = { 0, 2, 0 };

class FIBRE_PRIVATE ExternalEventLoop final : public fibre::EventLoop {
public:
    ExternalEventLoop(LibFibreEventLoop impl) : impl_(impl) {}

    RichStatus post(fibre::Callback<void> callback) final {
        F_RET_IF(!impl_.post, "not implemented");
        F_RET_IF((*impl_.post)(callback.get_ptr(), callback.get_ctx()) != 0,
                  "user provided post() failed");
        return RichStatus::success();
    }

    RichStatus register_event(int event_fd, uint32_t events, fibre::Callback<void, uint32_t> callback) final {
        F_RET_IF(!impl_.register_event, "not implemented");
        F_RET_IF((*impl_.register_event)(event_fd, events, callback.get_ptr(), callback.get_ctx()) != 0,
                 "user provided register_event() failed");
        return RichStatus::success();
    }

    RichStatus deregister_event(int event_fd) final {
        F_RET_IF(!impl_.deregister_event, "not implemented");
        F_RET_IF((*impl_.deregister_event)(event_fd) != 0,
                 "user provided deregister_event() failed");
        return RichStatus::success();
    }

    RichStatus call_later(float delay, fibre::Callback<void> callback, fibre::EventLoopTimer** p_timer) final {
        F_RET_IF(!impl_.call_later, "not implemented");
        fibre::EventLoopTimer* timer = (fibre::EventLoopTimer*)(*impl_.call_later)(delay, callback.get_ptr(), callback.get_ctx());
        if (p_timer) {
            *p_timer = timer;
        }
        F_RET_IF(!timer, "user provided call_later() failed");
        return RichStatus::success();
    }

    RichStatus cancel_timer(struct fibre::EventLoopTimer* timer) final {
        F_RET_IF(impl_.cancel_timer, "not implemented");
        F_RET_IF((*impl_.cancel_timer)((LibFibreEventLoopTimer*)timer) == 0,
                 "user provided cancel_timer() failed");
        return RichStatus::success();
    }

private:
    LibFibreEventLoop impl_;
};

class ExternalDiscoverer : public fibre::ChannelDiscoverer {
    void start_channel_discovery(
        fibre::Domain* domain,
        const char* specs, size_t specs_len,
        fibre::ChannelDiscoveryContext** handle) final;
    RichStatus stop_channel_discovery(fibre::ChannelDiscoveryContext* handle) final;
public:
    on_start_discovery_cb_t on_start_discovery;
    on_stop_discovery_cb_t on_stop_discovery;
    void* cb_ctx;
};


void ExternalDiscoverer::start_channel_discovery(fibre::Domain* domain, const char* specs, size_t specs_len, fibre::ChannelDiscoveryContext** handle) {
    LibFibreChannelDiscoveryCtx* ctx = new LibFibreChannelDiscoveryCtx{};
    if (handle) {
        *handle = from_c(ctx);
    }
    if (on_start_discovery) {
        (*on_start_discovery)(cb_ctx, to_c(domain), specs, specs_len);
    }
}

RichStatus ExternalDiscoverer::stop_channel_discovery(fibre::ChannelDiscoveryContext* handle) {
    LibFibreChannelDiscoveryCtx* ctx = to_c(handle);
    if (on_stop_discovery) {
        (*on_stop_discovery)(cb_ctx, to_c(ctx->domain));
    }
    delete ctx;
    return RichStatus::success();
}

namespace fibre {

class AsyncStreamLink final : public AsyncStreamSink, public AsyncStreamSource {
public:
    void start_write(cbufptr_t buffer, TransferHandle* handle, Callback<void, WriteResult> completer) final;
    void cancel_write(TransferHandle transfer_handle) final;
    void start_read(bufptr_t buffer, TransferHandle* handle, Callback<void, ReadResult> completer) final;
    void cancel_read(TransferHandle transfer_handle) final;
    void close(StreamStatus status);

    Callback<void, ReadResult> read_completer_;
    bufptr_t read_buf_;
    Callback<void, WriteResult> write_completer_;
    cbufptr_t write_buf_;
};

void AsyncStreamLink::start_write(cbufptr_t buffer, TransferHandle* handle, Callback<void, WriteResult> completer) {
    if (read_completer_) {
        size_t n_copy = std::min(read_buf_.size(), buffer.size());
        memcpy(read_buf_.begin(), buffer.begin(), n_copy);
        read_completer_.invoke_and_clear({kStreamOk, read_buf_.begin() + n_copy});
        completer.invoke({kStreamOk, buffer.begin() + n_copy});
    } else {
        if (handle) {
            *handle = reinterpret_cast<uintptr_t>(this);
        }
        write_buf_ = buffer;
        write_completer_ = completer;
    }
}

void AsyncStreamLink::cancel_write(TransferHandle transfer_handle) {
    write_completer_.invoke_and_clear({kStreamCancelled, write_buf_.begin()});
}

void AsyncStreamLink::start_read(bufptr_t buffer, TransferHandle* handle, Callback<void, ReadResult> completer) {
    if (write_completer_) {
        size_t n_copy = std::min(buffer.size(), write_buf_.size());
        memcpy(buffer.begin(), write_buf_.begin(), n_copy);
        write_completer_.invoke_and_clear({kStreamOk, write_buf_.begin() + n_copy});
        completer.invoke({kStreamOk, buffer.begin() + n_copy});
    } else {
        if (handle) {
            *handle = reinterpret_cast<uintptr_t>(this);
        }
        read_buf_ = buffer;
        read_completer_ = completer;
    }
}

void AsyncStreamLink::cancel_read(TransferHandle transfer_handle) {
    read_completer_.invoke_and_clear({kStreamCancelled, read_buf_.begin()});
}

void AsyncStreamLink::close(StreamStatus status) {
    write_completer_.invoke_and_clear({status, write_buf_.begin()});
    read_completer_.invoke_and_clear({status, read_buf_.begin()});
}

}

LibFibreStatus convert_status(StreamStatus status) {
    switch (status) {
        case fibre::kStreamOk: return LibFibreStatus::kFibreOk;
        case fibre::kStreamCancelled: return LibFibreStatus::kFibreCancelled;
        case fibre::kStreamClosed: return LibFibreStatus::kFibreClosed;
        default: return LibFibreStatus::kFibreInternalError; // TODO: this may not always be appropriate
    }
}

fibre::StreamStatus convert_status(LibFibreStatus status) {
    switch (status) {
        case LibFibreStatus::kFibreOk: return fibre::kStreamOk;
        case LibFibreStatus::kFibreCancelled: return fibre::kStreamCancelled;
        case LibFibreStatus::kFibreClosed: return fibre::kStreamClosed;
        default: return fibre::kStreamError; // TODO: this may not always be appropriate
    }
}

struct FIBRE_PRIVATE LibFibreCtx {
    ExternalEventLoop* event_loop;
    //size_t n_discoveries = 0;
    fibre::Context* fibre_ctx;
    //std::unordered_map<std::string, std::shared_ptr<fibre::ChannelDiscoverer>> discoverers;
};

struct FIBRE_PRIVATE LibFibreDiscoveryCtx {
    void on_found_object(fibre::Object* obj, fibre::Interface* intf);
    void on_lost_object(fibre::Object* obj);

    on_found_object_cb_t on_found_object_;
    on_lost_object_cb_t on_lost_object_;
    void* cb_ctx_;
    fibre::Domain* domain_;
};

struct LibFibreTxStream {
    void on_tx_done(fibre::WriteResult result) {
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

struct LibFibreRxStream {
    void on_rx_done(fibre::ReadResult result) {
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


void LibFibreDiscoveryCtx::on_found_object(fibre::Object* obj, fibre::Interface* intf) {
    if (on_found_object_) {
        F_LOG_D(domain_->ctx->logger, "discovered object " << fibre::as_hex(reinterpret_cast<uintptr_t>(obj)));
        (*on_found_object_)(cb_ctx_, to_c(obj), to_c(intf));
    }
}

void LibFibreDiscoveryCtx::on_lost_object(fibre::Object* obj) {
    if (on_lost_object_) {
        F_LOG_D(domain_->ctx->logger, "lost object " << fibre::as_hex(reinterpret_cast<uintptr_t>(obj)));
        (*on_lost_object_)(cb_ctx_, to_c(obj));
    }
}

const struct LibFibreVersion* libfibre_get_version() {
    return &libfibre_version;
}

LibFibreCtx* libfibre_open(LibFibreEventLoop event_loop, LibFibreLogger logger) {
    LibFibreCtx* ctx = new LibFibreCtx();
    ctx->event_loop = new ExternalEventLoop(event_loop);

    Logger fibre_logger = logger.log ? Logger{logger.log, (LogLevel)logger.verbosity} : Logger::none();

    if (F_LOG_IF_ERR(fibre_logger, fibre::open(ctx->event_loop, fibre_logger, &ctx->fibre_ctx), "failed to open fibre")) {
        delete ctx->event_loop;
        delete ctx;
        return nullptr;
    }

    return ctx;
}

void libfibre_close(LibFibreCtx* ctx) {
    if (!ctx) { // invalid argument but we can't log it
        return;
    }

    Logger logger = ctx->fibre_ctx->logger;

    fibre::close(ctx->fibre_ctx);
    ctx->fibre_ctx = nullptr;

    delete ctx->event_loop;
    delete ctx;

    F_LOG_D(logger, "closed (" << fibre::as_hex((uintptr_t)ctx) << ")");
}


void libfibre_register_backend(LibFibreCtx* ctx, const char* name, size_t name_length, on_start_discovery_cb_t on_start_discovery, on_stop_discovery_cb_t on_stop_discovery, void* cb_ctx) {
    auto disc = new ExternalDiscoverer();
    disc->on_start_discovery = on_start_discovery;
    disc->on_stop_discovery = on_stop_discovery;
    disc->cb_ctx = cb_ctx;
    ctx->fibre_ctx->register_backend({name, name + name_length}, disc);
}

FIBRE_PUBLIC LibFibreDomain* libfibre_open_domain(LibFibreCtx* ctx,
    const char* specs, size_t specs_len) {
    if (!ctx) {
        return nullptr; // invalid argument
    } else {
        F_LOG_D(ctx->fibre_ctx->logger, "opening domain");
        return to_c(ctx->fibre_ctx->create_domain({specs, specs_len}));
    }
}

void libfibre_close_domain(LibFibreDomain* domain) {
    if (!domain) {
        return; // invalid argument
    }
    F_LOG_D(from_c(domain)->ctx->logger, "closing domain");

    from_c(domain)->ctx->close_domain(from_c(domain));
}

void libfibre_add_channels(LibFibreDomain* domain, LibFibreRxStream** tx_channel, LibFibreTxStream** rx_channel, size_t mtu) {
    fibre::AsyncStreamLink* tx_link = new fibre::AsyncStreamLink(); // libfibre => backend
    fibre::AsyncStreamLink* rx_link = new fibre::AsyncStreamLink(); // backend => libfibre
    LibFibreRxStream* tx = new LibFibreRxStream(); // libfibre => backend
    LibFibreTxStream* rx = new LibFibreTxStream(); // backend => libfibre
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

    fibre::ChannelDiscoveryResult result = {fibre::kFibreOk, rx_link, tx_link, mtu};
    from_c(domain)->add_channels(result);
}

void libfibre_start_discovery(LibFibreDomain* domain, LibFibreDiscoveryCtx** handle,
        on_found_object_cb_t on_found_object, on_lost_object_cb_t on_lost_object,
        on_stopped_cb_t on_stopped, void* cb_ctx) {
    if (!domain) { // invalid argument
        if (on_stopped) {
            (*on_stopped)(cb_ctx, LibFibreStatus::kFibreInvalidArgument);
        }
        return;
    }

    // deleted in libfibre_stop_discovery()
    LibFibreDiscoveryCtx* discovery_ctx = new LibFibreDiscoveryCtx();
    discovery_ctx->on_found_object_ = on_found_object;
    discovery_ctx->on_lost_object_ = on_lost_object;
    discovery_ctx->cb_ctx_ = cb_ctx;
    discovery_ctx->domain_ = from_c(domain);

    if (handle) {
        *handle = discovery_ctx;
    }

    from_c(domain)->start_discovery(MEMBER_CB(discovery_ctx, on_found_object),
        MEMBER_CB(discovery_ctx, on_lost_object));
}

void libfibre_stop_discovery(LibFibreDiscoveryCtx* handle) {
    if (!handle) {
        return; // invalid argument
    }

    handle->domain_->stop_discovery();
    delete handle;
}

struct FunctionInfoContainer {
    Function* func;
    FunctionInfo* cpp;
    LibFibreFunctionInfo c;
};

LibFibreFunctionInfo* libfibre_get_function_info(LibFibreFunction* func) {
    auto cpp_info = from_c(func)->get_info();
    auto info = new FunctionInfoContainer{
        .func = from_c(func),
        .cpp = cpp_info,
        .c = LibFibreFunctionInfo{
            .name = cpp_info->name.c_str(),
            .name_length = cpp_info->name.size(),
            .input_names = new const char*[cpp_info->inputs.size() + 1],
            .input_codecs = new const char*[cpp_info->inputs.size() + 1],
            .output_names = new const char*[cpp_info->outputs.size() + 1],
            .output_codecs = new const char*[cpp_info->outputs.size() + 1],
        }
    };

    for (size_t i = 0; i < info->cpp->inputs.size(); ++i) {
        info->c.input_names[i] = std::get<0>(info->cpp->inputs[i]).c_str();
        info->c.input_codecs[i] = std::get<1>(info->cpp->inputs[i]).c_str();
    }
    for (size_t i = 0; i < info->cpp->outputs.size(); ++i) {
        info->c.output_names[i] = std::get<0>(info->cpp->outputs[i]).c_str();
        info->c.output_codecs[i] = std::get<1>(info->cpp->outputs[i]).c_str();
    }
    info->c.input_names[info->cpp->inputs.size()] = nullptr;
    info->c.input_codecs[info->cpp->inputs.size()] = nullptr;
    info->c.output_names[info->cpp->outputs.size()] = nullptr;
    info->c.output_codecs[info->cpp->outputs.size()] = nullptr;

    return &info->c;
}

void libfibre_free_function_info(LibFibreFunctionInfo* info) {
    const size_t offset = (uintptr_t)&((FunctionInfoContainer*)nullptr)->c;
    auto container = (FunctionInfoContainer*)((uintptr_t)info - offset);
    delete [] container->c.input_names;
    delete [] container->c.input_codecs;
    delete [] container->c.output_names;
    delete [] container->c.output_codecs;
    container->func->free_info(container->cpp);
    delete container;
}

struct InterfaceInfoContainer {
    Interface* intf;
    InterfaceInfo* cpp;
    LibFibreInterfaceInfo c;
};

LibFibreInterfaceInfo* libfibre_get_interface_info(LibFibreInterface* intf) {
    auto cpp_info = from_c(intf)->get_info();
    auto info = new InterfaceInfoContainer{
        .intf = from_c(intf),
        .cpp = cpp_info,
        .c = LibFibreInterfaceInfo{
            .name = cpp_info->name.c_str(),
            .name_length = cpp_info->name.size(),
            .attributes = new LibFibreAttributeInfo[cpp_info->attributes.size()],
            .n_attributes = cpp_info->attributes.size(),
            .functions = new LibFibreFunction*[cpp_info->functions.size()],
            .n_functions = cpp_info->functions.size(),
        }
    };

    for (size_t i = 0; i < info->cpp->functions.size(); ++i) {
        info->c.functions[i] = to_c(info->cpp->functions[i]);
    }

    for (size_t i = 0; i < info->cpp->attributes.size(); ++i) {
        info->c.attributes[i] = {
            .name = info->cpp->attributes[i].name.c_str(),
            .name_length = info->cpp->attributes[i].name.size(),
            .intf = to_c(info->cpp->attributes[i].intf)
        };
    }

    return &info->c;
}

void libfibre_free_interface_info(LibFibreInterfaceInfo* info) {
    const size_t offset = (uintptr_t)&((InterfaceInfoContainer*)nullptr)->c;
    auto container = (InterfaceInfoContainer*)((uintptr_t)info - offset);
    delete [] container->c.attributes;
    delete [] container->c.functions;
    container->intf->free_info(container->cpp);
    delete container;
}

LibFibreStatus libfibre_get_attribute(LibFibreInterface* intf, LibFibreObject* parent_obj, size_t attr_id, LibFibreObject** child_obj_ptr) {
    if (!intf || !parent_obj) {
        return LibFibreStatus::kFibreInvalidArgument;
    }

    RichStatusOr<Object*> child = from_c(intf)->get_attribute(from_c(parent_obj), attr_id);

    if (!child.has_value()) {
        // TODO: log error
        return LibFibreStatus::kFibreInvalidArgument;
    }

    if (child_obj_ptr) {
        *child_obj_ptr = to_c(child.value());
    }

    return LibFibreStatus::kFibreOk;
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

LibFibreStatus libfibre_call(LibFibreFunction* func, LibFibreCallContext** handle,
        LibFibreStatus status,
        const unsigned char* tx_buf, size_t tx_len,
        unsigned char* rx_buf, size_t rx_len,
        const unsigned char** tx_end,
        unsigned char** rx_end,
        libfibre_call_cb_t callback, void* cb_ctx) {
    bool valid_args = func && handle
                   && (!tx_len || tx_buf) // tx_buf valid
                   && (!rx_len || rx_buf) // rx_buf valid
                   && tx_end && rx_end // tx_end, rx_end valid
                   && ((status != LibFibreStatus::kFibreOk) || tx_len || rx_len || !handle); // progress
    if (!valid_args) {
        return LibFibreStatus::kFibreInvalidArgument;
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
            auto status = ctx->callback(ctx->ctx, to_c(result.status), result.tx_end, result.rx_end, &tx_buf, &tx_len, &rx_buf, &rx_len);
            if (status == LibFibreStatus::kFibreBusy) {
                delete ctx;
                return std::nullopt;
            } else {
                return fibre::CallBuffers{from_c(status), {tx_buf, tx_len}, {rx_buf, rx_len}};
            }
    }, ctx};


    auto response = from_c(func)->call(from_c(handle), {from_c(status), {tx_buf, tx_len}, {rx_buf, rx_len}}, cb);

    if (!response.has_value()) {
        return LibFibreStatus::kFibreBusy;
    } else {
        delete ctx;
        *tx_end = response->tx_end;
        *rx_end = response->rx_end;
        return to_c(response->status);
    }
}

void libfibre_start_tx(LibFibreTxStream* tx_stream,
        const uint8_t* tx_buf, size_t tx_len, on_tx_completed_cb_t on_completed,
        void* ctx) {
    tx_stream->on_completed = on_completed;
    tx_stream->ctx = ctx;
    tx_stream->sink->start_write({tx_buf, tx_len}, &tx_stream->handle, MEMBER_CB(tx_stream, on_tx_done));
}

void libfibre_cancel_tx(LibFibreTxStream* tx_stream) {
    tx_stream->sink->cancel_write(tx_stream->handle);
}

void libfibre_close_tx(LibFibreTxStream* tx_stream, LibFibreStatus status) {
    if (tx_stream->on_closed) {
        (tx_stream->on_closed)(tx_stream, tx_stream->on_closed_ctx, convert_status(status));
    }
}

void libfibre_start_rx(LibFibreRxStream* rx_stream,
        uint8_t* rx_buf, size_t rx_len, on_rx_completed_cb_t on_completed,
        void* ctx) {
    rx_stream->on_completed = on_completed;
    rx_stream->ctx = ctx;
    rx_stream->source->start_read({rx_buf, rx_len}, &rx_stream->handle, MEMBER_CB(rx_stream, on_rx_done));
}

void libfibre_cancel_rx(LibFibreRxStream* rx_stream) {
    rx_stream->source->cancel_read(rx_stream->handle);
}

void libfibre_close_rx(LibFibreRxStream* rx_stream, LibFibreStatus status) {
    if (rx_stream->on_closed) {
        (rx_stream->on_closed)(rx_stream, rx_stream->on_closed_ctx, convert_status(status));
    }
}
