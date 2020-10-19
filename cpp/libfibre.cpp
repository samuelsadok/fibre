
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

struct FIBRE_PRIVATE LibFibreCtx {
    ExternalEventLoop* event_loop;
    construct_object_cb_t on_construct_object;
    destroy_object_cb_t on_destroy_object;
    void* cb_ctx;
    size_t n_discoveries = 0;

#ifdef FIBRE_ENABLE_LIBUSB
    fibre::LibusbDiscoverer libusb_discoverer;
#endif
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

#ifdef FIBRE_ENABLE_LIBUSB
    fibre::LibusbDiscoverer::ChannelDiscoveryContext* libusb_discovery_ctx = nullptr;
#endif

    on_found_object_cb_t on_found_object;
    void* cb_ctx;
    LibFibreCtx* ctx;

    // A LibFibreDiscoveryCtx is created when the application starts discovery
    // and is deleted when the application stopped discovery _and_ all protocol
    // instances that arose from this discovery instance were also stopped.
    size_t use_count = 1;
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

    const size_t mtu = 64; // TODO: get MTU from channel specific data

    use_count++;

    auto protocol = new fibre::LegacyProtocolPacketBased(result.rx_channel, result.tx_channel, mtu);
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
            //FIBRE_LOG(D) << "constructing root object " << fibre::as_hex(reinterpret_cast<uintptr_t>(obj.get()));
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
    if (!register_event || !deregister_event) {
        FIBRE_LOG(E) << "invalid argument";
        return nullptr;
    }
    
    LibFibreCtx* ctx = new LibFibreCtx();
    ctx->event_loop = new ExternalEventLoop(post, register_event, deregister_event, call_later, cancel_timer);
    ctx->on_construct_object = construct_object;
    ctx->on_destroy_object = destroy_object;
    ctx->cb_ctx = cb_ctx;

#ifdef FIBRE_ENABLE_LIBUSB
    if (ctx->libusb_discoverer.init(ctx->event_loop) != 0) {
        delete ctx;
        FIBRE_LOG(E) << "failed to init libusb transport layer";
        return nullptr;
    }
#endif

    FIBRE_LOG(D) << "opened (" << fibre::as_hex((uintptr_t)ctx) << ")";
    return ctx;
}

void libfibre_close(LibFibreCtx* ctx) {
    if (ctx->n_discoveries) {
        FIBRE_LOG(W) << "there are still discovery processes ongoing";
    }

#ifdef FIBRE_ENABLE_LIBUSB
    ctx->libusb_discoverer.deinit();
#endif

    delete ctx->event_loop;
    delete ctx;

    FIBRE_LOG(D) << "closed (" << fibre::as_hex((uintptr_t)ctx) << ")";
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

        if (false) {
#ifdef FIBRE_ENABLE_LIBUSB
        } else if ((colon - prev_delim) == strlen("usb") && std::equal(prev_delim, colon, "usb")) {
            ctx->libusb_discoverer.start_channel_discovery(colon_end, next_delim - colon_end,
                    &discovery_ctx->libusb_discovery_ctx, *discovery_ctx);
#endif
        } else {
            FIBRE_LOG(W) << "transport layer \"" << std::string(prev_delim, colon - prev_delim) << "\" not implemented";
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

#ifdef FIBRE_ENABLE_LIBUSB
    if (discovery_ctx->libusb_discovery_ctx) {
        // TODO: implement "stopped" callback
        ctx->libusb_discoverer.stop_channel_discovery(discovery_ctx->libusb_discovery_ctx);
    }
#endif

    if (--discovery_ctx->use_count == 0) {
        FIBRE_LOG(D) << "deleting discovery context";
        delete discovery_ctx;
    }
}

const char* transform_codec(std::string& codec) {
    if (codec == "endpoint_ref") {
        return "object_ref";
    } else {
        return codec.data();
    }
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
        std::vector<const char*> input_names;
        std::vector<const char*> input_codecs;
        std::vector<const char*> output_names;
        std::vector<const char*> output_codecs;
        for (auto& arg: func.second.inputs) {
            input_names.push_back(arg.name.data());
            input_codecs.push_back(transform_codec(arg.codec));
        }
        for (auto& arg: func.second.outputs) {
            output_names.push_back(arg.name.data());
            output_codecs.push_back(transform_codec(arg.codec));
        }
        input_names.push_back(nullptr);
        input_codecs.push_back(nullptr);
        output_names.push_back(nullptr);
        output_codecs.push_back(nullptr);

        if (on_function_added) {
            (*on_function_added)(cb_ctx,
                reinterpret_cast<LibFibreFunction*>(&func.second), // corresponding reverse cast in libfibre_start_call()
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

FibreStatus convert_status(fibre::StreamStatus status) {
    switch (status) {
        case fibre::kStreamOk: return kFibreOk;
        case fibre::kStreamCancelled: return kFibreCancelled;
        case fibre::kStreamClosed: return kFibreClosed;
        default: return kFibreInternalError; // TODO: this may not always be appropriate
    }
}

struct FIBRE_PRIVATE LibFibreCallContext : fibre::Completer<FibreStatus>, fibre::Completer<fibre::WriteResult>, fibre::Completer<fibre::ReadResult> {
    void complete(FibreStatus status) final;
    void complete(fibre::WriteResult result) final;
    void complete(fibre::ReadResult result) final;

    template<typename Func>
    bool iterate_over_args_at(size_t encoded_offset, size_t max_encoded_length, size_t max_decoded_length, Func visitor);

    uint8_t n_active_transfers = 0;
    fibre::LegacyObject* obj = nullptr;
    fibre::LegacyFibreFunction* func = nullptr;
    on_call_completed_cb_t on_call_completed_ = nullptr;
    void* call_cb_ctx_ = nullptr;
    on_tx_completed_cb_t on_tx_completed_ = nullptr;
    void* tx_cb_ctx_ = nullptr;
    on_rx_completed_cb_t on_rx_completed_ = nullptr;
    void* rx_cb_ctx_ = nullptr;
    fibre::cbufptr_t tx_buf_; // application-owned buffer
    fibre::bufptr_t rx_buf_; // application-owned buffer
    std::vector<uint8_t> tx_vec_; // libfibre-owned buffer used after transcoding from application buffer
    std::vector<uint8_t> rx_vec_; // libfibre-owned buffer used before transcoding to application buffer
    size_t tx_offset_ = 0; // offset in the TX stream _after_ transcoding to application-facing format
    size_t rx_offset_ = 0; // offset in the RX stream _before_ transcoding to application-facing format
    fibre::LegacyObjectClient::CallContext* handle_ = nullptr;
    fibre::TransferHandle tx_handle_ = 0;
    fibre::TransferHandle rx_handle_ = 0;
};

void libfibre_start_call(LibFibreObject* obj, LibFibreFunction* func,
                         LibFibreCallContext** handle,
                         LibFibreTxStream** tx_stream,
                         LibFibreRxStream** rx_stream,
                         on_call_completed_cb_t on_completed, void* cb_ctx) {
    if (!obj || !func) {
        if (on_completed) {
            (*on_completed)(cb_ctx, kFibreInvalidArgument);
        }
        return;
    }

    fibre::LegacyObject* obj_cast = reinterpret_cast<fibre::LegacyObject*>(obj);
    fibre::LegacyFibreFunction* func_cast = reinterpret_cast<fibre::LegacyFibreFunction*>(func);

    bool is_member = std::find_if(obj_cast->intf->functions.begin(), obj_cast->intf->functions.end(),
        [&](std::pair<const std::string, fibre::LegacyFibreFunction>& kv) {
            return &kv.second == func_cast;
        }) != obj_cast->intf->functions.end();

    if (!is_member) {
        FIBRE_LOG(W) << "attempt to invoke function on an object that does not implement it";
        if (on_completed) {
            (*on_completed)(cb_ctx, kFibreInvalidArgument);
        }
        return;
    }


    auto completer = new LibFibreCallContext();
    completer->obj = obj_cast;
    completer->func = func_cast;
    completer->on_call_completed_ = on_completed;
    completer->call_cb_ctx_ = cb_ctx;

    if (handle) {
        *handle = completer;
    }
    if (tx_stream) {
        *tx_stream = reinterpret_cast<LibFibreTxStream*>(completer);
    }
    if (rx_stream) {
        *rx_stream = reinterpret_cast<LibFibreRxStream*>(completer);
    }

    obj_cast->client->start_call(obj_cast->ep_num, func_cast,
        &completer->handle_, *completer);
}

void libfibre_end_call(LibFibreCallContext* handle) {
    if (handle) {
        handle->obj->client->cancel_call(handle->handle_);
    }
}

void LibFibreCallContext::complete(FibreStatus status) {
    if (on_call_completed_) {
        (*on_call_completed_)(call_cb_ctx_, status);
    }
    delete this;
}

bool encode_for_transport(fibre::LegacyObjectClient* client, fibre::cbufptr_t src, fibre::bufptr_t dst, const fibre::LegacyFibreArg& arg) {
    if (arg.codec == "endpoint_ref") {
        if (src.size() < sizeof(uintptr_t) || dst.size() < 4) {
            return false;
        }

        uintptr_t val = *reinterpret_cast<const uintptr_t*>(src.begin());
        auto obj = reinterpret_cast<fibre::LegacyObject*>(val);
        write_le<uint16_t>(obj ? obj->ep_num : 0, &dst);
        write_le<uint16_t>(obj ? obj->client->json_crc_ : 0, &dst);
    } else {
        if (src.size() < arg.size || dst.size() < arg.size) {
            return false;
        }

        memcpy(dst.begin(), src.begin(), arg.size);
    }

    return true;
}

bool decode_from_transport(fibre::LegacyObjectClient* client, fibre::cbufptr_t src, fibre::bufptr_t dst, const fibre::LegacyFibreArg& arg) {
    if (arg.codec == "endpoint_ref") {
        if (src.size() < 4 || dst.size() < sizeof(uintptr_t)) {
            return false;
        }

        uint16_t ep_num = *read_le<uint16_t>(&src);
        uint16_t json_crc = *read_le<uint16_t>(&src);

        fibre::LegacyObject* obj_ptr = nullptr;

        if (ep_num && json_crc == client->json_crc_) {
            for (auto& known_obj: client->objects_) {
                if (known_obj->ep_num == ep_num) {
                    obj_ptr = known_obj.get();
                }
            }
        }

        FIBRE_LOG(D) << "placing transcoded ptr " << reinterpret_cast<uintptr_t>(obj_ptr);
        *reinterpret_cast<uintptr_t*>(dst.begin()) = reinterpret_cast<uintptr_t>(obj_ptr);

    } else {
        if (src.size() < arg.size || dst.size() < arg.size) {
            return false;
        }

        memcpy(dst.begin(), src.begin(), arg.size);
    }

    return true;
}

/**
 * @brief func: A functor that takes these arguments:
 *              - size_t rel_encoded_offset (relative to the starting pos described by encoded_offset)
 *              - size_t rel_decoded_offset (relative to the starting pos described by encoded_offset)
 *              - size_t encoded_length
 *              - size_t decoded_length
 */
template<typename Func>
bool LibFibreCallContext::iterate_over_args_at(size_t encoded_offset, size_t max_encoded_length, size_t max_decoded_length, Func visitor) {
    ssize_t arg_offset = 0;
    ssize_t len_diff = 0;

    for (auto& arg: func->outputs) {
        if (arg.size >= SIZE_MAX || arg_offset + arg.size > encoded_offset) {
            ssize_t rel_encoded_offset = arg_offset - (ssize_t)encoded_offset;
            ssize_t rel_decoded_offset = arg_offset - (ssize_t)encoded_offset - len_diff;

            if (rel_decoded_offset >= max_decoded_length || rel_encoded_offset >= max_encoded_length) {
                break;
            }

            size_t encoded_arg_size = arg.size;
            size_t decoded_arg_size = arg.codec == "endpoint_ref" ? sizeof(uintptr_t) : arg.size;
            len_diff += encoded_arg_size - decoded_arg_size;

            if (rel_encoded_offset < 0) {
                encoded_arg_size += rel_encoded_offset;
            }

            if (rel_decoded_offset < 0) {
                decoded_arg_size += rel_decoded_offset;
            }

            if (encoded_arg_size < SIZE_MAX && rel_encoded_offset + encoded_arg_size > max_encoded_length) {
                encoded_arg_size -= rel_encoded_offset + encoded_arg_size - max_encoded_length;
            }

            if (decoded_arg_size < SIZE_MAX && rel_decoded_offset + decoded_arg_size > max_decoded_length) {
                decoded_arg_size -= rel_decoded_offset + decoded_arg_size - max_decoded_length;
            }

            if (!visitor(arg, rel_encoded_offset, rel_decoded_offset, encoded_arg_size, decoded_arg_size)) {
                return false;
            }
        }

        arg_offset += arg.size;
    }

    return true;
}

void libfibre_start_tx(LibFibreTxStream* tx_stream,
        const uint8_t* tx_buf, size_t tx_len, on_tx_completed_cb_t on_completed,
        void* ctx) {
    LibFibreCallContext* call = reinterpret_cast<LibFibreCallContext*>(tx_stream);

    // Allocate libfibre-internal TX buffer into which the application buffer
    // will be encoded. The size can still change during transcoding.
    call->tx_vec_ = std::vector<uint8_t>{};
    call->tx_vec_.reserve(tx_len);
    
    // Transcode application buffer to stream buffer
    bool ok = call->iterate_over_args_at(call->tx_offset_, SIZE_MAX, tx_len, [&](
            const fibre::LegacyFibreArg& arg,
            ssize_t rel_encoded_offset, ssize_t rel_decoded_offset,
            size_t encoded_arg_size, size_t decoded_arg_size) {
        call->tx_vec_.resize(rel_encoded_offset + encoded_arg_size);
        fibre::bufptr_t encoded_buf = {call->tx_vec_.data() + rel_encoded_offset, encoded_arg_size};
        fibre::cbufptr_t decoded_buf = {tx_buf + rel_decoded_offset, decoded_arg_size};
        return encode_for_transport(call->obj->client, decoded_buf, encoded_buf, arg);
    });

    if (!ok) {
        FIBRE_LOG(W) << "Transcoding before TX failed. Note that partial transcoding of arguments is not supported.";
        if (on_completed) {
            (*on_completed)(ctx, tx_stream, kFibreInvalidArgument, tx_buf);
        }
        return;
    }

    call->n_active_transfers++;
    call->tx_buf_ = {tx_buf, tx_len};
    call->on_tx_completed_ = on_completed;
    call->tx_cb_ctx_ = ctx;
    
    call->handle_->start_write(call->tx_vec_, &call->tx_handle_, *call);
}

// Called by LegacyObjectClient when a TX operation completes
void LibFibreCallContext::complete(fibre::WriteResult result) {
    size_t n_sent = (result.end - tx_vec_.data());
    FIBRE_LOG(D) << "sent " << n_sent << " bytes ";

    if (n_sent > tx_vec_.size()) {
        FIBRE_LOG(E) << "internal error: sent more bytes than expected";
    }

    tx_offset_ += n_sent;
    n_active_transfers--;

    ssize_t len_diff = tx_vec_.size() - tx_buf_.size();
    const uint8_t* tx_end = tx_buf_.begin() + n_sent - len_diff;
    void* cb_ctx = tx_cb_ctx_;
    on_tx_completed_cb_t cb = on_tx_completed_;
    tx_buf_ = {};
    tx_vec_ = {};
    tx_cb_ctx_ = nullptr;
    on_tx_completed_ = nullptr;

    if (cb) {
        (*cb)(cb_ctx, reinterpret_cast<LibFibreTxStream*>(this), convert_status(result.status), tx_end);
    }
}

void libfibre_cancel_tx(LibFibreTxStream* tx_stream) {
    LibFibreCallContext* call = reinterpret_cast<LibFibreCallContext*>(tx_stream);
    call->handle_->cancel_write(call->tx_handle_);
}

void libfibre_start_rx(LibFibreRxStream* rx_stream,
        uint8_t* rx_buf, size_t rx_len, on_rx_completed_cb_t on_completed,
        void* ctx) {
    LibFibreCallContext* call = reinterpret_cast<LibFibreCallContext*>(rx_stream);
    
    size_t encoded_size = 0;

    bool ok = call->iterate_over_args_at(call->rx_offset_, SIZE_MAX, rx_len, [&](
            const fibre::LegacyFibreArg& arg,
            ssize_t rel_encoded_offset, ssize_t rel_decoded_offset,
            size_t encoded_arg_size, size_t decoded_arg_size) {
        encoded_size += encoded_arg_size;
        return true;
    });

    if (!ok) {
        FIBRE_LOG(W) << "Transcoding preparation before RX failed. Note that partial transcoding of arguments is not supported.";
        if (on_completed) {
            (*on_completed)(ctx, rx_stream, kFibreInvalidArgument, rx_buf);
        }
        return;
    }
    
    call->rx_vec_ = {};
    call->rx_vec_.resize(encoded_size);

    call->n_active_transfers++;
    call->rx_buf_ = {rx_buf, rx_len};
    call->on_rx_completed_ = on_completed;
    call->rx_cb_ctx_ = ctx;
    
    call->handle_->start_read(call->rx_vec_, &call->rx_handle_, *call);
}

// Called by LegacyObjectClient when an RX operation completes
void LibFibreCallContext::complete(fibre::ReadResult result) {
    size_t n_recv = result.end - rx_vec_.data();
    FIBRE_LOG(D) << "received " << n_recv << " bytes ";

    if (n_recv > rx_vec_.size()) {
        FIBRE_LOG(E) << "internal error: received more bytes than expected";
    }

    ssize_t len_diff = rx_vec_.size() - rx_buf_.size();
    uint8_t* rx_end = rx_buf_.begin() + n_recv - len_diff;
    void* cb_ctx = rx_cb_ctx_;
    on_rx_completed_cb_t cb = on_rx_completed_;

    size_t arg_offset = 0;

    // Transcode stream buffer to application buffer
    bool ok = iterate_over_args_at(rx_offset_, n_recv, rx_buf_.size(), [&](
            const fibre::LegacyFibreArg& arg,
            ssize_t rel_encoded_offset, ssize_t rel_decoded_offset,
            size_t encoded_arg_size, size_t decoded_arg_size) {
        fibre::cbufptr_t encoded_buf = {rx_vec_.data() + rel_encoded_offset, encoded_arg_size};
        fibre::bufptr_t decoded_buf = {rx_buf_.begin() + rel_decoded_offset, decoded_arg_size};
        return decode_from_transport(obj->client, encoded_buf, decoded_buf, arg);
    });

    if (!ok) {
        FIBRE_LOG(W) << "Transcoding after RX failed. Partial transcoding of arguments is not supported.";
        rx_end = rx_buf_.begin();
        result.status = fibre::kStreamError;
    } else if (rx_end > rx_buf_.end()) {
        FIBRE_LOG(E) << "miscalculated pointer: beyond buffer end";
        rx_end = rx_buf_.end();
        result.status = fibre::kStreamError;
    }

    rx_buf_ = {};
    rx_vec_ = {};
    rx_cb_ctx_ = nullptr;
    on_rx_completed_ = nullptr;
    rx_offset_ += n_recv;
    n_active_transfers--;

    if (cb) {
        (*cb)(cb_ctx, reinterpret_cast<LibFibreRxStream*>(this), convert_status(result.status), rx_end);
    }
}

void libfibre_cancel_rx(LibFibreRxStream* rx_stream) {
    LibFibreCallContext* call = reinterpret_cast<LibFibreCallContext*>(rx_stream);
    call->handle_->cancel_read(call->rx_handle_);
}
