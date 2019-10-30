#ifndef __FIBRE_LOCAL_ENDPOINT_HPP
#define __FIBRE_LOCAL_ENDPOINT_HPP

#include <fibre/context.hpp>
#include <fibre/uuid.hpp>

DEFINE_LOG_TOPIC(LOCAL_ENDPOINT);
#define current_log_topic LOG_TOPIC_LOCAL_ENDPOINT

namespace fibre {

class LocalEndpoint {
public:
    /**
     * @brief Shall initialize a decoder that will process an incoming byte
     * stream and generate an output byte stream.
     * 
     * To signify that no more data will be accepted (e.g. if all input
     * arguments of a function have been received), the stream sink shall return
     * kClosed.
     * 
     * @param ctx: The context in which to execute the endpoint action. This
     *        shall for instance contain the tx_stream field, a stream that can
     *        be used to return data to the caller.
     *        The object pointed to by this pointer must remain valid until
     *        close() is called on it. Note that ctx and its tx_stream may be
     *        required to live longer than the corresponding
     *        LocalEndpoint::close() call.
     * @returns Shall return NULL if the stream could not be opened, for
     *          instance because too many streams are already open.
     */
    virtual StreamSink* open(Context* ctx) = 0;

    /**
     * @brief Signifies to the local endpoint that no more data will be passed
     * to the given stream.
     * 
     * The local endpoint may chose to keep the stream object allocated if there
     * is still a processes going on. For instance if all arguments to a
     * function have been received, the input handler may call close()
     * but the invoked function may still be executing or sending a reply.
     * 
     * close() must be called at most once for each open() call.
     * 
     * @param stream_sink: Pointer to a stream sink that was previously returned
     *        by open().
     */
    virtual int close(StreamSink* stream_sink) = 0;
};

extern std::unordered_map<Uuid, LocalEndpoint*> local_endpoints; // TODO: fix dynamic allocation
int register_endpoint(Uuid uuid, LocalEndpoint* local_endpoint);
LocalEndpoint* get_endpoint(Uuid uuid);
int unregister_endpoint(Uuid uuid);

}

#undef current_log_topic

#endif // __FIBRE_LOCAL_ENDPOINT_HPP
