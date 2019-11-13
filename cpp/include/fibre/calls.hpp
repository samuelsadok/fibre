#ifndef __FIBRE_CALLS_HPP
#define __FIBRE_CALLS_HPP

#include <fibre/stream.hpp>
#include <fibre/decoder.hpp>
#include <fibre/encoder.hpp>
#include <fibre/context.hpp>
#include <fibre/uuid.hpp>
#include <fibre/logging.hpp>
#include <fibre/local_endpoint.hpp>
#include <fibre/fragmenter.hpp>
#include <fibre/closure.hpp>
#include <fibre/callback_list.hpp>
#include <fibre/platform_support/linux_timer.hpp>

DEFINE_LOG_TOPIC(CALLS);
#define current_log_topic LOG_TOPIC_CALLS

namespace fibre {

class CallDecoder : public StreamSink {
public:
    CallDecoder(Context* ctx)
        : ctx_(ctx), endpoint_id_decoder_(alloc_decoder<Uuid>(ctx))
    {}

    ~CallDecoder() {
        // TODO: provide close function to dealloc decoders
        /*if (endpoint_id_decoder_) {
            dealloc_decoder(endpoint_id_decoder_);
            endpoint_id_decoder_ = nullptr;
        }*/
    }

    StreamStatus process_bytes(cbufptr_t& buffer) final {
        if (endpoint_id_decoder_) {
            StreamStatus status;
            if ((status = endpoint_id_decoder_->process_bytes(buffer)) != kStreamClosed) {
                return status;
            }
            FIBRE_LOG(D) << "decoded endpoint ID: " << *endpoint_id_decoder_->get();
            endpoint_ = get_endpoint(*endpoint_id_decoder_->get());
            if (!endpoint_) {
                FIBRE_LOG(W) << "received call for unknown endpoint ID " << *endpoint_id_decoder_->get();
            }
            dealloc_decoder(endpoint_id_decoder_);
            endpoint_id_decoder_ = nullptr;
            decoder_ = endpoint_ ? endpoint_->open(ctx_) : nullptr;
        }
        if (endpoint_ && decoder_) {
            return decoder_->process_bytes(buffer);
        } else {
            return kStreamError;
        }
    }

private:
    //Uuid endpoint_id_{};
    bool have_endpoint_id_ = false;
    Decoder<Uuid>* endpoint_id_decoder_ = nullptr;
    LocalEndpoint* endpoint_ = nullptr;
    StreamSink* decoder_ = nullptr;
    Context* ctx_;
};

class CallEncoder : public StreamSource {
public:
    CallEncoder(Context* ctx, Uuid remote_endpoint_id, StreamSource* encoder) :
            ctx_(ctx), remote_endpoint_id_(remote_endpoint_id), encoder_(encoder) {
        endpoint_id_encoder_ = alloc_encoder<Uuid>(ctx);
        endpoint_id_encoder_->set(&remote_endpoint_id_);
    }

    StreamStatus get_bytes(bufptr_t& buffer) final {
        StreamStatus status;
        if (endpoint_id_encoder_) {
            if ((status = endpoint_id_encoder_->get_bytes(buffer)) != kStreamClosed) {
                return status;
            }
            dealloc_encoder(endpoint_id_encoder_);
            endpoint_id_encoder_ = nullptr;
        }
        return encoder_->get_bytes(buffer);
    }

private:
    Uuid remote_endpoint_id_;
    Context* ctx_;
    Encoder<Uuid>* endpoint_id_encoder_ = nullptr;
    StreamSource* encoder_ = nullptr;
};


using call_id_t = Uuid;


struct incoming_call_t {
    Context ctx;
    CallDecoder decoder; // TODO: put actual call decoder (call id + forward content)
    // TODO: what if we want a buffer size that depends on the call context?
    FixedBufferDefragmenter<1024> fragment_sink;
};

// TODO: ensure that cancellation token cannot trigger twice
using CancellationToken = CallbackList<>;

class TimedCancellationToken : public CallbackList<> {
public:
    TimedCancellationToken(LinuxWorker* worker)
        : worker_(worker) {}
    
    int init(uint32_t delay_ms) {
        if (timer_.init(worker_)) {
            return -1;
        }
        if (timer_.start(delay_ms, false, &timeout_handler_obj)) {
            timer_.deinit();
            return -1;
        }
        return 0;
    }

    int deinit() {
        int result = 0;
        if (timer_.stop()) {
            result = -1;
        }
        if (timer_.deinit()) {
            result = -1;
        }
        return result;
    }

private:
    void timeout_handler() {
        FIBRE_LOG(D) << "cancellation token timed out";
        trigger();
    }

    LinuxTimer timer_;
    LinuxWorker* worker_;

    member_closure_t<decltype(&TimedCancellationToken::timeout_handler)> timeout_handler_obj{&TimedCancellationToken::timeout_handler, this};
};

struct outgoing_call_t;
void dispose(std::shared_ptr<outgoing_call_t> call);

struct outgoing_call_t {
    Uuid uuid;
    Context* ctx;
    CallEncoder encoder; // TODO: put actual call encoder (call id + forward content)
    FixedBufferFragmenter<1024> fragment_source;
    CancellationToken* cancellation_token;
    Callback<>* finished_callback;

    Closure<decltype(&dispose), std::tuple<std::shared_ptr<outgoing_call_t>>, std::tuple<>, void> cancel_obj;
};

int start_or_get_call(Context* ctx, call_id_t call_id, incoming_call_t** call);
int end_call(call_id_t call_id);

int decode_fragment(Context* ctx, StreamSource* source);
int encode_fragment(outgoing_call_t call, StreamSink* sink);


class MultiFragmentEncoder {
public:
    virtual int encode_fragment(outgoing_call_t* calls, size_t n_calls) = 0;
};

// Recommended way to encode fragments on UDP
class CRCMultiFragmentEncoder : public MultiFragmentEncoder {
public:
    CRCMultiFragmentEncoder(std::shared_ptr<StreamSink> sink, std::shared_ptr<Context> ctx)
        : sink_(sink), ctx_(ctx) {}

    std::tuple<size_t, size_t> get_size_function() {
        return {16, 2};
    }

    int encode_fragment(outgoing_call_t* calls, size_t n_calls) final {
        // TODO: the windows API allows passing fragmented buffers (WSABUF)
        // to the socket. We should honor this to reduce the number of copy
        // operations.
        uint8_t buffer[mtu_];
        const size_t footer_length = 2;
        bufptr_t bufptr = {.ptr = buffer, .length = sizeof(buffer)};

        // Each chunk has the following format:
        //  1 byte: length of the following fields:
        //      16 bytes: call UUID
        //      ? bytes: offset
        //      ? bytes: data

        for (size_t i = 0; i < n_calls; ++i) {
            if (bufptr.length < 1 + footer_length) {
                break; // not enough space to hold next chunk
            }
            size_t max_chunk_length = std::min(bufptr.length - 1 - footer_length, (size_t)255);
            bufptr_t chunk_bufptr = {.ptr = bufptr.ptr + 1, .length = max_chunk_length};

            // Enqueue call ID
            if (encode(calls[i].uuid, chunk_bufptr, ctx_.get()) != 0) {
                FIBRE_LOG(W) << "could not encode chunk";
                continue;
            }

            size_t offset;
            cbufptr_t chunk = {.ptr = nullptr, .length = chunk_bufptr.length};
            calls[i].fragment_source.get_chunk(chunk, &offset);

            // Enqueue offset
            if (encode(offset, chunk_bufptr, ctx_.get()) != 0) {
                FIBRE_LOG(W) << "could not encode chunk";
                continue;
            }

            // Write chunk content
            size_t actual_chunk_length = std::min(chunk.length, chunk_bufptr.length);
            memcpy(chunk_bufptr.ptr, chunk.ptr, actual_chunk_length);
            chunk_bufptr += actual_chunk_length;

            // Write chunk length
            *bufptr.ptr = (max_chunk_length - chunk_bufptr.length);
            bufptr += (max_chunk_length - chunk_bufptr.length) + 1;

            FIBRE_LOG(D) << "added fragment: call ID " << calls[i].uuid << ", chunk " << offset << " length " << chunk.length;
        }

        // TODO: add CRC
        bufptr += 2;

        cbufptr_t cbufptr = {.ptr = buffer, .length = sizeof(buffer) - bufptr.length};
        return sink_->process_bytes(cbufptr); // TODO: error handling
    }


    std::shared_ptr<StreamSink> sink_;
    std::shared_ptr<Context> ctx_;
    size_t mtu_ = 1024; // TODO: read dynamically from sink
};

// Recommended way to decode fragments on UDP
class CRCMultiFragmentDecoder {
public:
    static int decode_fragments(cbufptr_t bufptr, Context* ctx) {
        // TODO: CRC check
        if (bufptr.length >= 2) {
            bufptr.length -= 2;
        } else {
            return -1;
        }

        while (bufptr.length > 0) {
            FIBRE_LOG(D) << "incoming chunk";
            FIBRE_LOG(D) << bufptr.length << " bytes left in buffer, len field is " << (size_t)bufptr.ptr[0];
            size_t total_chunk_length = std::min(bufptr.length - 1, (size_t)bufptr.ptr[0]);
            cbufptr_t chunk_bufptr = {.ptr = bufptr.ptr + 1, .length = total_chunk_length};

            call_id_t call_id;
            if (decode(chunk_bufptr, &call_id, ctx)) {
                FIBRE_LOG(W) << "failed to decode call ID";
                bufptr += total_chunk_length + 1;
                continue;
            }

            FIBRE_LOG(D) << "call ID is " << call_id;

            size_t offset;
            if (decode(chunk_bufptr, &offset, ctx)) {
                FIBRE_LOG(W) << "failed to decode offset";
                bufptr += total_chunk_length + 1;
                continue;
            }

            incoming_call_t* call;
            start_or_get_call(ctx, call_id, &call);

            FIBRE_LOG(D) << "incoming fragment on stream " << call_id << ", offset " << offset;

            //FIBRE_LOG(D) << "got " << (sizeof(buf) - bufptr.length) << " bytes from the source";
            call->fragment_sink.process_chunk(chunk_bufptr, offset);
            FIBRE_LOG(D) << "processed chunk";


            // Shovel data from the defragmenter to the actual handler
            stream_copy_result_t copy_result = stream_copy_all(&call->decoder, &call->fragment_sink);
            if (copy_result.src_status == kStreamError) {
                FIBRE_LOG(E) << "defragmenter failed";
            } else if ((copy_result.dst_status == kStreamError) || (copy_result.dst_status == kStreamClosed)) {
                end_call(call_id);
            }

            bufptr += total_chunk_length + 1;
        }

        return 0;
    }

    static const size_t mtu_ = 1024; // TODO: read dynamically from sink
};


}

#undef current_log_topic

#endif // __FIBRE_CALLS_HPP
