
#include <algorithm>
#include <fibre/libfibre.h>
#include <fibre/channel_discoverer.hpp>
#include <fibre/fibre.hpp>
#include "print_utils.hpp"
#include "legacy_protocol.hpp" // TODO: remove this include
#include "legacy_object_client.hpp" // TODO: remove this include
#include <algorithm>
#include <random>
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
const fibre::Chunk* from_c(const LibFibreChunk* ptr) {
    return reinterpret_cast<const fibre::Chunk*>(ptr);
}
const LibFibreChunk* to_c(const fibre::Chunk* ptr) {
    return reinterpret_cast<const LibFibreChunk*>(ptr);
}


static const struct LibFibreVersion libfibre_version = { 0, 3, 0 };

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

    struct ExternalTimer final : Timer {
        RichStatus set(float interval, TimerMode mode) {
            F_RET_IF(!parent->impl_.open_timer, "not implemented");
            F_RET_IF((*parent->impl_.set_timer)(timer, interval, (int)mode) != 0, "user provided set_timer() failed");
            return RichStatus::success();
        }
        ExternalEventLoop* parent;
        LibFibreEventLoopTimer* timer;
    };

    RichStatus open_timer(Timer** p_timer, Callback<void> on_trigger) final {
        F_RET_IF(!impl_.open_timer, "not implemented");
        LibFibreEventLoopTimer* id;
        F_RET_IF((*impl_.open_timer)(&id, on_trigger.get_ptr(), on_trigger.get_ctx()) != 0, "user provided open_timer() failed");
        ExternalTimer* t = new ExternalTimer{};
        t->parent = this;
        t->timer = id;
        if (p_timer) {
            *p_timer = t;
        }
        return RichStatus::success();
    }

    RichStatus close_timer(Timer* timer) final {
        ExternalTimer* t = static_cast<ExternalTimer*>(timer);
        F_RET_IF(!impl_.close_timer, "not implemented");
        F_RET_IF((*impl_.close_timer)(t->timer) != 0, "user provided close_timer() failed");
        delete t;
        return RichStatus::success();
    }

private:
    LibFibreEventLoop impl_;
};

namespace fibre {

class AsyncStreamLink final : public AsyncStreamSink, public AsyncStreamSource {
public:
    void start_write(cbufptr_t buffer, TransferHandle* handle, Callback<void, WriteResult0> completer) final;
    void cancel_write(TransferHandle transfer_handle) final;
    void start_read(bufptr_t buffer, TransferHandle* handle, Callback<void, ReadResult> completer) final;
    void cancel_read(TransferHandle transfer_handle) final;
    void close(StreamStatus status);

    Callback<void, ReadResult> read_completer_;
    bufptr_t read_buf_;
    Callback<void, WriteResult0> write_completer_;
    cbufptr_t write_buf_;
};

void AsyncStreamLink::start_write(cbufptr_t buffer, TransferHandle* handle, Callback<void, WriteResult0> completer) {
    if (read_completer_.has_value()) {
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
    if (write_completer_.has_value()) {
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

struct LibFibreCall final : Socket {
    WriteResult write(WriteArgs args) final;
    WriteArgs on_write_done(WriteResult result) final;
    void close_half(int side);
    LibFibreCallHandle handle;
    LibFibreCtx* ctx;
    Socket* call;
    bool closed[2] = {false, false};
};

struct FIBRE_PRIVATE LibFibreCtx {
    void enqueue_task(LibFibreCallHandle handle, WriteResult result);
    void enqueue_task(LibFibreCallHandle handle, WriteArgs args);
    void enqueue_task(LibFibreTask task);
    void dispatch_tasks_to_app();
    void handle_tasks(LibFibreTask* tasks, size_t n_tasks);

    ExternalEventLoop* event_loop;
    run_tasks_cb_t run_tasks_cb;
    //size_t n_discoveries = 0;
    fibre::Fibre* fibre_ctx;
    //std::unordered_map<std::string, std::shared_ptr<fibre::ChannelDiscoverer>> discoverers;

    bool in_dispatcher = false;
    bool autostart_dispatcher = true;

    std::unordered_map<LibFibreCallHandle, LibFibreCall*> calls;

    std::vector<LibFibreTask> task_queue;
    std::vector<LibFibreTask> shadow_task_queue;
};

struct FIBRE_PRIVATE LibFibreDiscoveryCtx {
    void on_found_object(fibre::Object* obj, fibre::Interface* intf, std::string path);
    void on_lost_object(fibre::Object* obj);

    on_found_object_cb_t on_found_object_;
    on_lost_object_cb_t on_lost_object_;
    void* cb_ctx_;
    fibre::Domain* domain_;
};


void LibFibreDiscoveryCtx::on_found_object(fibre::Object* obj, fibre::Interface* intf, std::string path) {
    if (on_found_object_) {
        F_LOG_D(domain_->ctx->logger, "discovered object " << fibre::as_hex(reinterpret_cast<uintptr_t>(obj)));
        (*on_found_object_)(cb_ctx_, to_c(obj), to_c(intf), path.data(), path.size());
    }
}

void LibFibreDiscoveryCtx::on_lost_object(fibre::Object* obj) {
    if (on_lost_object_) {
        F_LOG_D(domain_->ctx->logger, "lost object " << fibre::as_hex(reinterpret_cast<uintptr_t>(obj)));
        (*on_lost_object_)(cb_ctx_, to_c(obj));
    }
}

void LibFibreCtx::enqueue_task(LibFibreCallHandle handle, WriteResult result) {
    LibFibreTask task = {
        .type = kWriteDone,
        .handle = handle,
        .on_write_done = {
            .status = to_c(result.status),
            .c_end = to_c(result.end.chunk),
            .b_end = result.end.byte
        }
    };
    enqueue_task(task);
}

void LibFibreCtx::enqueue_task(LibFibreCallHandle handle, WriteArgs args) {
    LibFibreTask task = {
        .type = kWrite,
        .handle = handle,
        .write = {
            .b_begin = args.buf.n_chunks() ? args.buf.front().buf().begin() : nullptr,
            .c_begin = to_c(args.buf.c_begin()),
            .c_end = to_c(args.buf.c_end()),
            .elevation = (int8_t)(args.buf.n_chunks() ? (args.buf.front().layer() - args.buf.c_begin()->layer()) : 0),
            .status = to_c(args.status)
        }
    };
    enqueue_task(task);
}

void LibFibreCtx::enqueue_task(LibFibreTask task) {
    task_queue.push_back(task);

    if (autostart_dispatcher) {
        autostart_dispatcher = false;
        event_loop->post(MEMBER_CB(this, dispatch_tasks_to_app));
    }
}

void LibFibreCtx::dispatch_tasks_to_app() {
    in_dispatcher = true;

    while (task_queue.size()) {
        LibFibreTask* out_tasks;
        size_t n_out_tasks = 123;
        (*run_tasks_cb)(this, task_queue.data(), task_queue.size(), &out_tasks, &n_out_tasks);
        task_queue = {};
        handle_tasks(out_tasks, n_out_tasks);
    }

    in_dispatcher = false;
    autostart_dispatcher = true;
}

void LibFibreCtx::handle_tasks(LibFibreTask* tasks, size_t n_tasks) {
    for (size_t i = 0; i < n_tasks; ++i) {
        switch (tasks[i].type) {
            case kStartCall: {
                LibFibreCall* call = new LibFibreCall{}; // deleted in LibFibreCall::close_half()
                call->handle = tasks[i].handle;
                call->ctx = this;
                call->call = from_c(tasks[i].start_call.func)->start_call(from_c(tasks[i].start_call.domain), {}, call);
                calls[tasks[i].handle] = call;
                break;
            }

            case kWrite: {
                auto it = calls.find(tasks[i].handle);
                if (it == calls.end()) {
                    F_LOG_E(fibre_ctx->logger, "unknown call");
                    continue;
                }

                LibFibreCall* call = it->second;

                WriteResult result = call->call->write({
                    {tasks[i].write.b_begin, from_c(tasks[i].write.c_begin), from_c(tasks[i].write.c_end), tasks[0].write.elevation},
                    from_c(tasks[i].write.status)});
                
                if (result.is_busy()) {
                    // ignore
                } else {
                    enqueue_task(tasks[i].handle, result);
                }
                
                if (result.status != fibre::kFibreOk) {
                    call->close_half(1);
                }

                break;
            }

            case kWriteDone: {
                auto it = calls.find(tasks[i].handle);
                if (it == calls.end()) {
                    F_LOG_E(fibre_ctx->logger, "unknown call");
                    continue;
                }

                LibFibreCall* call = it->second;

                WriteResult result = {
                    from_c(tasks[i].on_write_done.status),
                    {from_c(tasks[i].on_write_done.c_end), tasks[i].on_write_done.b_end}};
                WriteArgs args = call->call->on_write_done(result);

                if (result.status != fibre::kFibreOk) {
                    // if the call returns a new non-empty buffer here that's an error
                    call->close_half(0);
                } else if (args.is_busy()) {
                    // ignore
                } else {
                    enqueue_task(tasks[i].handle, args);
                }
                
                break;
            }

            default: {
                F_LOG_E(fibre_ctx->logger, "unknown task ID " << tasks[i].type);
            }
        }
    }
}

WriteResult LibFibreCall::write(WriteArgs args) {
    ctx->enqueue_task(handle, args);
    return WriteResult::busy();
}

WriteArgs LibFibreCall::on_write_done(WriteResult result) {
    ctx->enqueue_task(handle, result);
    if (result.status != fibre::kFibreOk) {
        close_half(1);
    }
    return result.status == fibre::kFibreOk ? WriteArgs::busy() : WriteArgs{{}, result.status};
}

void LibFibreCall::close_half(int side) {
    closed[side] = true;
    if (closed[0] && closed[1]) {
        delete this;
    }
}

const struct LibFibreVersion* libfibre_get_version() {
    return &libfibre_version;
}

LibFibreCtx* libfibre_open(LibFibreEventLoop event_loop, run_tasks_cb_t run_tasks_cb, LibFibreLogger logger) {
    LibFibreCtx* ctx = new LibFibreCtx();
    ctx->event_loop = new ExternalEventLoop(event_loop);
    ctx->run_tasks_cb = run_tasks_cb;

    Logger fibre_logger = logger.log ? Logger{{logger.log, logger.ctx}, (LogLevel)logger.verbosity} : Logger::none();

    F_LOG_D(fibre_logger, "test log call");

    //return (LibFibreCtx*)((uintptr_t)logger.log);
    //return (LibFibreCtx*)logger.log;
    

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

FIBRE_PUBLIC LibFibreDomain* libfibre_open_domain(LibFibreCtx* ctx,
    const char* specs, size_t specs_len) {
    if (!ctx) {
        return nullptr; // invalid argument
    } else {
        std::random_device engine;
        unsigned node_id[(16 + sizeof(unsigned) - 1) / sizeof(unsigned)];
        for (size_t i = 0; i < sizeof(node_id) / sizeof(unsigned); ++i) {
            node_id[i] = engine();
        }

        F_LOG_D(ctx->fibre_ctx->logger, "opening domain with node ID " << as_hex(node_id));
        return to_c(ctx->fibre_ctx->create_domain({specs, specs_len}, (uint8_t*)node_id, {}));
    }
}

void libfibre_close_domain(LibFibreDomain* domain) {
    if (!domain) {
        return; // invalid argument
    }
    F_LOG_D(from_c(domain)->ctx->logger, "closing domain");

    from_c(domain)->ctx->close_domain(from_c(domain));
}

void libfibre_show_device_dialog(LibFibreDomain* domain, const char* backend) {
    from_c(domain)->show_device_dialog(backend);
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

void libfibre_run_tasks(LibFibreCtx* ctx, LibFibreTask* tasks, size_t n_tasks, LibFibreTask** out_tasks, size_t* n_out_tasks) {
    if (ctx->in_dispatcher) {
        F_LOG_E(ctx->fibre_ctx->logger, "libfibre_run_tasks must not be called from inside the libfibre_run_tasks_callback");
    }

    ctx->handle_tasks(tasks, n_tasks);

    // Move new tasks to the shadow task queue so they remain valid until the
    // next call to `libfibre_run_tasks()`.
    ctx->shadow_task_queue = {};
    std::swap(ctx->shadow_task_queue, ctx->task_queue);
    *out_tasks = ctx->shadow_task_queue.data();
    *n_out_tasks = ctx->shadow_task_queue.size();
}
