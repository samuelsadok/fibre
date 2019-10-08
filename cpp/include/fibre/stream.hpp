#ifndef __FIBRE_STREAM_HPP
#define __FIBRE_STREAM_HPP

#include <fibre/logging.hpp>
#include "cpp_utils.hpp"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

DEFINE_LOG_TOPIC(STREAM);
#define current_log_topic LOG_TOPIC_STREAM

namespace fibre {

/*
TODO: do we want something like this buffer struct?
Would simplify things like skipping data in the buffer with custom operator
overloads.

struct cbufptr_t {
    const uint8_t* buffer = nullptr;
    size_t length = 0;
}*/

/**
 * @brief Represents a class that can process a continuous stream of bytes.
 */
class StreamSink {
public:
    enum status_t {
        OK,
        BUSY,
        CLOSED,
        ERROR
    };
    virtual ~StreamSink() {}

    /**
     * @brief Processes the bytes given to the function.
     * 
     * There is no guarantee that this function processes all bytes, even if it
     * could. For that, use process_all_bytes(). TODO: if it turns out that this
     * guarantee is easily implemented for all implementations, we might change this.
     * 
     * TODO: "The blocking behavior shall depend on the thread-local deadline_ms variable." - rethink this when we have more implementations that do this.
     * 
     * @param buffer Pointer to the buffer that should be processed.
     *               Must not be null if length is non-zero.
     * @param length Length of the buffer. May be 0.
     * @param processed_bytes If not NULL, incremented by the number of bytes that were processed
     *        during the function call.
     *        For all status return values (including ERROR), the increment is at least zero and
     *        at most equal to length. For status OK the increment is always equal to length.
     *        For status ERROR the increment may not properly reflect the true number of processed bytes.
     * 
     * @retval OK       Some of the given data was processed successfully and
     *                  the stream might potentially immediately accept more
     *                  data after this.
     *                  If `length` is non-zero, the stream MUST process a non-
     *                  zero number of bytes. This is so that the caller can
     *                  ensure progress.
     * @retval BUSY     Zero or more of the given bytes were processed and the
     *                  stream is now temporarily busy or full, but not yet
     *                  closed.
     *                  How to "unclog" the stream or how to detect that the
     *                  stream is writable again is implementation defined.
     * @retval CLOSED   Zero or more of the given data was processed successfully
     *                  and the stream is now closed. Subsequent calls to this
     *                  function shall also return CLOSED, unless the stream is
     *                  reset in some way.
     * @retval ERROR    Something went wrong.
     *                  *processed_bytes will still be incremented gracefully,
     *                  i.e. by at least zero and at most `length`.
     *                  Whether this error is permanent or temporary is
     *                  implementation defined.
     *                  In any case, subsequent calls to this function must be
     *                  handled gracefully.
     * 
     * TODO: the definition of this function was changed - check if implementations still comply.
     */
    virtual status_t process_bytes(const uint8_t* buffer, size_t length, size_t *processed_bytes) = 0;

    /**
     * @brief Processes as much of the given data as possible.
     * 
     * @retval OK       All of the given data was processed successfully and
     *                  the stream might potentially immediately accept more
     *                  data after this.
     *                  *processed_bytes will be incremented by `length`.
     * @retval BUSY     Zero or more of the given bytes were processed and the
     *                  stream is now temporarily busy or full, but not yet
     *                  closed.
     *                  How to "unclog" the stream or how to detect that the
     *                  stream is writable again is implementation defined.
     * @retval CLOSED   Zero or more of the given data was processed successfully
     *                  and the stream is now closed. Subsequent calls to this
     *                  function shall also return CLOSED, unless the stream is
     *                  reset in some way.
     * @retval ERROR    Something went wrong.
     *                  *processed_bytes will still be incremented gracefully,
     *                  i.e. by at least zero and at most `length`.
     *                  Whether this error is permanent or temporary is
     *                  implementation defined.
     *                  In any case, subsequent calls to this function must be
     *                  handled gracefully.
     */
    status_t process_all_bytes(const uint8_t* buffer, size_t length, size_t *processed_bytes) {
        size_t pos = 0;
        status_t status;
        // Note that we call the process_bytes function even if `length` is zero.
        // This is necessary to return the correct status.
        do {
            size_t new_pos = pos;
            status = process_bytes(buffer + pos, length + pos, &new_pos);
            if (status != OK) {
                break;
            }
            if (new_pos <= pos) {
                // This is a violation of the specs of `process_bytes`.
                FIBRE_LOG(E) << "no progress in loop";
                break;
            }
            pos = new_pos;
        } while (length > pos);

        if (processed_bytes)
            (*processed_bytes) += pos;

        return status;
    }

    /**
     * @brief Indicates the minimum number of bytes that this stream can
     * take until there is an observable effect.
     * 
     * For example if this function returns 5, the stream promises that
     * there is no observable difference between the following sequences:
     * 
     * Sequence 1:
     *  delay 1 second
     *  process 5 bytes
     * 
     * Sequence 2:
     *  process 4 bytes
     *  delay 1 second
     *  process 1 bytes
     * 
     * After the process_bytes returned ERROR or CLOSED, the behavior
     * of this function is undefined.
     */
    virtual size_t get_min_useful_bytes() const { return 1; }

    /**
     * @brief Indicates the minimum number of bytes that this stream can
     * take immediately without blocking or going out-of-memory.
     * 
     * If the stream never blocks, this function may return SIZE_MAX.
     * 
     * After the process_bytes returned ERROR or CLOSED, the behavior
     * of this function is undefined.
     */
    virtual size_t get_min_non_blocking_bytes() const { return 0; }
};

// TODO: this is a good example where inheritance is inappropriate. Closable
// should have nothing to do with StreamSink.
class ClosableStreamSink : public StreamSink {
    virtual void close() = 0;
};

#if 0
class PacketSink : public StreamSink {
public:
    // @brief Get the maximum packet length (aka maximum transmission unit)
    // A packet size shall take no action and return an error code if the
    // caller attempts to send an oversized packet.
    //virtual size_t get_mtu() = 0;

    // @brief Processes a packet.
    // The blocking behavior shall depend on the thread-local deadline_ms variable.
    // @return: 0 on success, otherwise a non-zero error code
    // TODO: define what happens when the packet is larger than what the implementation can handle.
    virtual int process_packet(const uint8_t* buffer, size_t length) = 0;
};
#endif

class StreamSource {
public:
    typedef enum {
        OK,
        // temporary errors
        BUSY,
        // permanent stati
        CLOSED,
        ERROR
    } status_t;

    // @brief Generate a chunk of bytes that are part of a continuous stream.
    // The blocking behavior shall depend on the thread-local deadline_ms variable.
    // @param generated_bytes: if not NULL, shall be incremented by the number of
    //        bytes that were written to buffer.
    // @return: 0 on success, otherwise a non-zero error code

    /**
     * @brief Copies bytes into the given buffer.
     * 
     * @param buffer: A pointer to where the data shall be copied to.
     *        If the function returns anything other than success the buffer
     *        shall not be modified.
     * @param length: At most this many bytes may be copied to buffer.
     * @param generated_bytes: The variable pointed to by this pointer will be
     *        incremented by the number of bytes that were copied to the buffer.
     *        If the function returns anything other than success the value
     *        shall not be modified.
     *        If this output is not needed, the pointer can be NULL.
     * 
     * TODO: specify what happens if less than requested bytes were returned. Can probably be used to indicate "no more data immediately available"
     * TODO: spefify if on ERROR some data may be lost
     * 
     * @returns:
     *  OK: The request succeeded.
     *  CLOSED: The request succeeded and the stream is now permanently closed.
     *          If the stream was already closed before, zero bytes shall be returned.
     *  ERROR: The request failed and the buffer and *generated_bytes were not
     *         modified. Whether the stream is subsequently closed or still open
     *         is undefined.
     */
    virtual status_t get_bytes(uint8_t* buffer, size_t length, size_t* generated_bytes) = 0;

    // @brief Returns the number of bytes that can still be written to the stream.
    // Shall return SIZE_MAX if the stream has unlimited lenght.
    // TODO: deprecate
    //virtual size_t get_free_space() = 0;
};

/**
 * @brief Behaves like a stream source, but additionally grants access to the
 * internal buffer of the stream source.
 * 
 * If feasible, an implementer should prefer this interface over the StreamSource
 * interface as it can reduce copy operations.
 */
class OpenStreamSource : public StreamSource {
public:
    /**
     * @brief Shall return a pointer into the internal buffer.
     * 
     * @param buf: The pointer being pointed to by this pointer shall be set to
     *        to the address where the unconsumed data starts.
     *        If the function returns anything other than success the value
     *        shall not be modified.
     *        If this output is not needed, the pointer can be NULL.
     * @param length: The variable pointed to by this pointer shall be set to
     *        the length of consecutive unconsumed data available at *buf. This
     *        may be less than the number of available bytes, for instance if
     *        the internal buffer is segmented. However the returned length
     *        shall not be 0 unless there is currently no data available.
     *        If the function returns anything other than success the value
     *        shall not be modified.
     *        If this output is not needed, the pointer can be NULL.
     * 
     * @returns:
     *  OK: The request could be handled or partially handled.
     *  ERROR: The request could not be handled.
     */
    virtual status_t get_buffer(const uint8_t** buf, size_t* length) = 0;

    /**
     * @brief Shall advance the stream by the specified number of bytes.
     * @param length: The number of bytes by which to advance the stream.
     * TODO: specify what happens if this is more than available or more than a chunk.
     * 
     * @returns:
     *  OK: The stream was advanced successfully.
     *  CLOSED: The request succeeded and the stream is now permanently closed.
     *  ERROR: The request could not be handled.
     */
    virtual status_t consume(size_t length) = 0;

    status_t get_bytes(uint8_t* buffer, size_t length, size_t* generated_bytes) final {
        const uint8_t* buf = nullptr;
        size_t internal_length = length;
        if (get_buffer(&buf, &internal_length) != OK) {
            return ERROR;
        }
        if (internal_length > length) {
            FIBRE_LOG(E) << "got larger buffer than requested";
            internal_length = length;
        }
        memcpy(buffer, buf, internal_length);
        consume(internal_length);
        if (generated_bytes)
            *generated_bytes += internal_length;
        return OK;
    }
};


///** @brief Returns true if the given status indicates that the stream is
// * permanently closed, false otherwise */
//__attribute__((unused))
//bool is_permanent(StreamSource::status_t status) {
//    return (status != OK) && (status != BUSY) && (status != FULL);
//}
//
///** @brief Returns true if the given status indicates that the stream is
// * permanently closed, false otherwise */
//__attribute__((unused))
//bool is_permanent(StreamSink::status_t status) {
//    return (status != OK) && (status != BUSY);
//}

//class StreamToPacketSegmenter : public StreamSink {
//public:
//    StreamToPacketSegmenter(PacketSink& output) :
//        output_(output)
//    {
//    };
//
//    int process_bytes(const uint8_t *buffer, size_t length, size_t* processed_bytes);
//    
//    size_t get_free_space() { return SIZE_MAX; }
//
//private:
//    uint8_t header_buffer_[3];
//    size_t header_index_ = 0;
//    uint8_t packet_buffer_[RX_BUF_SIZE];
//    size_t packet_index_ = 0;
//    size_t packet_length_ = 0;
//    PacketSink& output_;
//};


#if 0
class StreamBasedPacketSink : public PacketSink {
public:
    StreamBasedPacketSink(StreamSink& output) :
        output_(output)
    {
    };
    
    size_t get_mtu() { return SIZE_MAX; }
    int process_packet(const uint8_t *buffer, size_t length);

private:
    StreamSink& output_;
};
#endif

/*// @brief: Represents a stream sink that's based on an underlying packet sink.
// A single call to process_bytes may result in multiple packets being sent.
class PacketBasedStreamSink : public StreamSink {
public:
    PacketBasedStreamSink(PacketSink& packet_sink) : _packet_sink(packet_sink) {}
    ~PacketBasedStreamSink() {}

    status_t process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) final {
        // Loop to ensure all bytes get sent
        while (length) {
            size_t chunk = length; // < _packet_sink.get_mtu() ? length : _packet_sink.get_mtu();
            // send chunk as packet
            if (_packet_sink.process_packet(buffer, chunk))
                return -1;
            buffer += chunk;
            length -= chunk;
            if (processed_bytes)
                *processed_bytes += chunk;
        }
        return 0;
    }

    size_t get_free_space() { return SIZE_MAX; }

private:
    PacketSink& _packet_sink;
};*/

/**
 * @brief Implements the StreamSink interface by writing into a
 * fixed size memory buffer.
 * When the end of buffer is reached the stream closes.
 */
class MemoryStreamSink : public StreamSink {
public:
    MemoryStreamSink(uint8_t *buffer, size_t length) :
        buffer_(buffer),
        buffer_length_(length) {}

    status_t process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) final {
        size_t chunk = std::min(length, buffer_length_);
        //LOG_FIBRE("copy %08zx bytes from %08zx to %08zx\n", length, buffer, buffer_);
        memcpy(buffer_, buffer, chunk);
        buffer_length_ -= chunk;
        buffer_ += chunk;
        if (processed_bytes)
            *processed_bytes += chunk;
        return buffer_length_ ? OK : CLOSED;
    }

    size_t get_min_non_blocking_bytes() const final { return buffer_length_; }

private:
    uint8_t * buffer_;
    size_t buffer_length_;
};

/**
 * @brief Implements a finite StreamSource by reading from a fixed size memory
 * buffer.
 */
class MemoryStreamSource : public OpenStreamSource {
public:
    MemoryStreamSource(const uint8_t *buffer, size_t length) :
        buffer_(buffer),
        length_(length) {}

    status_t get_buffer(const uint8_t** buf, size_t* length) override {
        if (buf)
            *buf = buffer_;
        if (length)
            *length = length_;
        return OK;
    }

    status_t consume(size_t length) override {
        length = std::min(length, length_);
        buffer_ += length;
        length_ -= length;
        return length_ ? OK : CLOSED;
    }

private:
    const uint8_t * buffer_;
    size_t length_;
};

/**
 * @brief Implements a StreamSink that discards a fixed number of
 * bytes and then closes.
 **/
class NullStreamSink : public StreamSink {
    void assert_non_abstract(void) {
        static_assert(!std::is_abstract<decltype(*this)>(), "this class should not be abstract");
    }
    //ASSERT_NON_ABSTRACT();
public:
    NullStreamSink(size_t skip) : skip_(skip) {}

    status_t process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) override {
        size_t chunk = std::min(length, skip_);
        skip_ -= chunk;
        if (processed_bytes)
            *processed_bytes += chunk;
        return skip_ ? OK : CLOSED;
    }

    size_t get_min_non_blocking_bytes() const final { return skip_; }

private:
    size_t skip_;
};

/**
 * @brief Implements a chain of statically known streams.
 */
// TODO: allow passing block decoders directly
template<typename ... TStreams>
class StaticStreamChain : public StreamSink {
public:
    template<size_t NStreams = sizeof...(TStreams), ENABLE_IF((NStreams >= 1))>
    explicit StaticStreamChain(TStreams&& ... decoders) :
        decoders_(std::forward<TStreams>(decoders)...)
    {
        //EXPECT_TYPE(TDecoder, StreamSink);
    }

    StaticStreamChain() :
        decoders_()
    {
        //EXPECT_TYPE(TDecoder, StreamSink);
    }

    status_t process_bytes(const uint8_t *buffer, size_t length, size_t* processed_bytes) final {
        FIBRE_LOG(D) << "static stream chain: process " << length << " bytes";
        while (length) {
            StreamSink *stream = get_stream(current_stream_idx_);
            if (!stream)
                return CLOSED;
            size_t chunk = 0;
            status_t result = stream->process_bytes(buffer, length, &chunk);
            //LOG_FIBRE("StaticStreamChain: processed %zu bytes", chunk);
            buffer += chunk;
            length -= chunk;
            if (processed_bytes)
                *processed_bytes += chunk;
            if (result != CLOSED)
                return result;
            current_stream_idx_++;
        }
        return current_stream_idx_ < sizeof...(TStreams) ? OK : CLOSED;
    }

    size_t get_min_useful_bytes() const final {
        const StreamSink *stream = get_stream(current_stream_idx_);
        return stream ? stream->get_min_useful_bytes() : 0;
    }

    size_t get_min_non_blocking_bytes() const final {
        size_t val = 0;
        for (size_t i = current_stream_idx_; i < sizeof...(TStreams); ++i)
            val += get_stream(i)->get_min_non_blocking_bytes();
        return val;
    }

    const StreamSink* get_stream(size_t i) const {
        return dynamic_get<const StreamSink, TStreams...>(i, decoders_);
    }
    StreamSink* get_stream(size_t i) {
        return dynamic_get<StreamSink, TStreams...>(i, decoders_);
    }

    template<size_t I>
    const std::tuple_element_t<I, std::tuple<TStreams...>>& get_stream() const {
        return std::get<I>(decoders_);
    }
    template<size_t I>
    std::tuple_element_t<I, std::tuple<TStreams...>>& get_stream() {
        return std::get<I>(decoders_);
    }

    const std::tuple<TStreams...>& get_all_streams() const {
        return decoders_;
    }
    std::tuple<TStreams...>& get_all_streams() {
        return decoders_;
    }

private:
    size_t current_stream_idx_ = 0;
    std::tuple<TStreams...> decoders_;
};

template<typename ... TDecoders>
inline StaticStreamChain<TDecoders...> make_decoder_chain(TDecoders&& ... decoders) {
    return StaticStreamChain<TDecoders...>(std::forward<TDecoders>(decoders)...);
}

/**
 * @brief Implements a chain of streams that can only be resolved at runtime.
 * 
 * The abstract function advance_state is invoked whenever the active stream
 * closes. Implementers can use this method to construct a new stream by
 * calling set_stream<T>().
 * set_stream<T>() should be called in the constructor of the deriving class
 * to set the initial stream.
 * 
 * The active stream is destroyed when it is evicted by another stream or when
 * the encapsulating DynamicStreamChain is destroyed (whichever comes first).
 * This class uses no dynamic memory.
 */
template<size_t BUFFER_SIZE>
class DynamicStreamChain : public StreamSink {
    // don't allow copy/move (we don't know if it's save to relocate the buffer)
    DynamicStreamChain(const DynamicStreamChain&) = delete;
    DynamicStreamChain& operator=(const DynamicStreamChain&) = delete;

public:
    DynamicStreamChain() { }

    ~DynamicStreamChain() {
        if (current_stream_)
            current_stream_->~StreamSink();
    }

    status_t process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) final {
        FIBRE_LOG(D) << "dynamic stream chain: process " << length << " bytes";
        while (current_stream_) {
            size_t chunk = 0;
            status_t result = current_stream_->process_bytes(buffer, length, &chunk);
            buffer += chunk;
            length -= chunk;
            if (processed_bytes)
                *processed_bytes += chunk;
            if (result != CLOSED)
                return result;
            advance_state();
        }
        return CLOSED;
    }

    size_t get_min_useful_bytes() const final {
        return current_stream_ ? current_stream_->get_min_useful_bytes() : 0;
    }

    size_t get_min_non_blocking_bytes() const final {
        return current_stream_ ? current_stream_->get_min_non_blocking_bytes() : 0;
    }

protected:
    /** @brief Should be called by advance_state() to set a new decoder.
     * The old decoder is destructed */
    template<typename TDecoder, typename ... TArgs>
    void set_stream(TArgs&& ... args) {
        static_assert(sizeof(TDecoder) <= BUFFER_SIZE, "TDecoder is too large. Increase BUFFER_SIZE.");
        set_stream(nullptr); // destruct old stream before overwriting the memory
        set_stream(new (buffer_) TDecoder(std::forward<TArgs>(args)...));
    }
    void set_stream(StreamSink* new_stream) {
        if (current_stream_)
            current_stream_->~StreamSink();
        current_stream_ = new_stream;
    }

    template<typename TDecoder>
    TDecoder* get_stream() {
        return current_stream_ ?
               static_cast<TDecoder*>(current_stream_) :
               nullptr;
    }
    template<typename TDecoder>
    const TDecoder* get_stream() const {
        return current_stream_ ?
               static_cast<TDecoder*>(current_stream_) :
               nullptr;
    }

private:
    /*
     * @brief Called whenever a decoder finishes, including when data is
     * received for the first time.
     * 
     * This function should call set_stream().
     * To terminate the decoder chain, call set_stream(nullptr).
     */
    virtual status_t advance_state() = 0;

    size_t state_ = 0;
    uint8_t buffer_[BUFFER_SIZE]; // Subdecoders are constructed in-place in this buffer
    StreamSink *current_stream_ = nullptr;
};


template<typename TStreamSink>
class StreamRepeater : public StreamSink {
public:
    status_t process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) final {
        FIBRE_LOG(D) << "stream repeater: process " << length << " bytes";
        while (length && active_) {
            size_t chunk = 0;
            status_t result = stream_sink_.process_bytes(buffer, length, &chunk);
            buffer += chunk;
            length -= chunk;
            if (processed_bytes)
                *processed_bytes += chunk;
            if (result != CLOSED)
                return result;
            active_ = advance_state();
            if (active_) {
                stream_sink_ = TStreamSink(); // reset stream sink
            }
        }
        return active_ ? OK : CLOSED;
    }

    size_t get_min_useful_bytes() const final {
        return active_ ? stream_sink_.get_min_useful_bytes() : 0;
    }

    size_t get_min_non_blocking_bytes() const final {
        return active_ ? stream_sink_.get_min_non_blocking_bytes() : 0;
    }

protected:
    TStreamSink stream_sink_;
    virtual bool advance_state() = 0;
private:
    bool active_ = true;
};

} // namespace fibre

#undef current_log_topic

#endif // __FIBRE_STREAM_HPP
