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

template<typename T>
struct generic_bufptr_t {
    T* ptr /*= nullptr*/;
    size_t length /*= 0*/;

    generic_bufptr_t& operator+=(size_t num) {
        if (num > length) {
            FIBRE_LOG(E) << "buffer underflow";
            num = length;
        }
        ptr += num;
        length -= num;
        return *this;
    }

    generic_bufptr_t operator++(int) {
        generic_bufptr_t result = *this;
        *this += 1;
        return result;
    }

    T& operator*() {
        return *ptr;
    }

    generic_bufptr_t take(size_t num) {
        if (num > length) {
            FIBRE_LOG(E) << "buffer underflow";
            num = length;
        }
        generic_bufptr_t result = {ptr, num};
        return result;
    }

    generic_bufptr_t skip(size_t num) {
        if (num > length) {
            FIBRE_LOG(E) << "buffer underflow";
            num = length;
        }
        return {ptr + num, length - num};
    }
};

using cbufptr_t = generic_bufptr_t<const uint8_t>;
using bufptr_t = generic_bufptr_t<uint8_t>;

/**
 * @brief Represents a class that can process a continuous stream of bytes.
 */
class StreamSink {
public:
    enum status_t {
        kOk,
        kBusy,
        kClosed,
        kError
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
     * @param buffer The buffer that should be processed. The buffer will be
     *        advanced by the number of bytes that were processed during the
     *        function call.
     *        For status kError the increment may not properly reflect the true
     *        number of processed bytes.
     * 
     * @retval kOk      Some of the given data was processed successfully and
     *                  the stream might potentially immediately accept more
     *                  data after this.
     *                  If `length` is non-zero, the stream MUST process a non-
     *                  zero number of bytes. This is so that the caller can
     *                  ensure progress.
     * @retval kBusy    Zero or more of the given bytes were processed and the
     *                  stream is now temporarily busy or full, but not yet
     *                  closed.
     *                  How to "unclog" the stream or how to detect that the
     *                  stream is writable again is implementation defined.
     * @retval kClosed  Zero or more of the given data was processed successfully
     *                  and the stream is now closed. Subsequent calls to this
     *                  function shall also return kClosed, unless the stream is
     *                  reset in some way.
     * @retval kError   Something went wrong.
     *                  *processed_bytes will still be incremented gracefully,
     *                  i.e. by at least zero and at most `length`.
     *                  Whether this error is permanent or temporary is
     *                  implementation defined.
     *                  In any case, subsequent calls to this function must be
     *                  handled gracefully.
     * 
     * TODO: the definition of this function was changed - check if implementations still comply.
     */
    virtual status_t process_bytes(cbufptr_t& buffer) = 0;

    /**
     * @brief Processes as much of the given data as possible.
     * 
     * @param buffer The buffer that should be processed. The buffer will be
     *        advanced by the number of bytes that were processed during the
     *        function call.
     *        For status kOk the buffer will be empty after the call.
     *        For status kError the increment may not properly reflect the true
     *        number of processed bytes.
     * 
     * @retval kOk      All of the given data was processed successfully and
     *                  the stream might potentially immediately accept more
     *                  data after this.
     *                  *processed_bytes will be incremented by `length`.
     * @retval kBusy    Zero or more of the given bytes were processed and the
     *                  stream is now temporarily busy or full, but not yet
     *                  closed.
     *                  How to "unclog" the stream or how to detect that the
     *                  stream is writable again is implementation defined.
     * @retval kClosed  Zero or more of the given data was processed successfully
     *                  and the stream is now closed. Subsequent calls to this
     *                  function shall also return kClosed, unless the stream is
     *                  reset in some way.
     * @retval kError   Something went wrong.
     *                  `buffer` will still be advanced gracefully,
     *                  i.e. by at least zero and at most `length`.
     *                  Whether this error is permanent or temporary is
     *                  implementation defined.
     *                  In any case, subsequent calls to this function must be
     *                  handled gracefully.
     */
    status_t process_all_bytes(cbufptr_t& buffer) {
        status_t status;
        // Note that we call the process_bytes function even if `length` is zero.
        // This is necessary to return the correct status.
        do {
            size_t old_length = buffer.length;
            if ((status = process_bytes(buffer)) != kOk) {
                return status;
            }
            if (buffer.length && (old_length <= buffer.length)) {
                // This is a violation of the specs of `process_bytes`.
                FIBRE_LOG(E) << "no progress in loop: old length " << old_length << ", new length " << buffer.length;
                return kError;
            }
        } while (buffer.length);

        return kOk;
    }

    status_t process_bytes_(cbufptr_t buffer, size_t* processed_bytes) {
        size_t old_length = buffer.length;
        status_t status = process_bytes(buffer);
        if (processed_bytes)
            (*processed_bytes) += old_length - buffer.length;
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
     * After the process_bytes returned kError or kClosed, the behavior
     * of this function is undefined.
     */
    virtual size_t get_min_useful_bytes() const { return 1; }

    /**
     * @brief Indicates the minimum number of bytes that this stream can
     * take immediately without blocking or going out-of-memory.
     * 
     * If the stream never blocks, this function may return SIZE_MAX.
     * 
     * After the process_bytes returned kError or kClosed, the behavior
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
        kOk,
        // temporary errors
        kBusy,
        // permanent stati
        kClosed,
        kError
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
     * TODO: spefify if on kError some data may be lost
     * 
     * @returns:
     *  kOk: The request succeeded.
     *  kClosed: The request succeeded and the stream is now permanently closed.
     *          If the stream was already closed before, zero bytes shall be returned.
     *  kError: The request failed and the buffer and *generated_bytes were not
     *         modified. Whether the stream is subsequently closed or still open
     *         is undefined.
     * 
     * @retval kOk      Some of the output buffer was filled successfully and
     *                  the stream might potentially immediately generate more
     *                  data after this.
     *                  If `length` is non-zero, the stream MUST return a non-
     *                  zero number of bytes. This is so that the caller can
     *                  ensure progress.
     * @retval kBusy    Zero or more of bytes were returned and the stream is
     *                  now temporarily busy or empty, but not yet closed.
     *                  How to "unclog" the stream or how to detect that the
     *                  stream is readable again is implementation defined.
     * @retval kClosed  Zero or more bytes were generated successfully and the
     *                  stream is now closed. Subsequent calls to this function
     *                  shall also return kClosed, unless the stream is reset in
     *                  some way.
     * @retval kError   Something went wrong.
     *                  buffer will still be advanced gracefully,
     *                  i.e. by at least zero and at most `length`.
     *                  Whether this error is permanent or temporary is
     *                  implementation defined.
     *                  In any case, subsequent calls to this function must be
     *                  handled gracefully.
     */
    virtual status_t get_bytes(bufptr_t& buffer) = 0;

    status_t get_all_bytes(bufptr_t& buffer) {
        status_t status;
        // Note that we call the get_bytes function even if `length` is zero.
        // This is necessary to return the correct status.
        do {
            size_t old_length = buffer.length;
            if ((status = get_bytes(buffer)) != kOk) {
                return status;
            }
            if (old_length <= buffer.length) {
                // This is a violation of the specs of `get_bytes`.
                FIBRE_LOG(E) << "no progress in loop: old length " << old_length << ", new length " << buffer.length;
                return kError;
            }
        } while (buffer.length);

        return kOk;
    }

    // @brief Returns the number of bytes that can still be written to the stream.
    // Shall return SIZE_MAX if the stream has unlimited lenght.
    // TODO: deprecate
    //virtual size_t get_free_space() = 0;

    status_t get_bytes_(bufptr_t buffer, size_t* generated_bytes) {
        size_t old_length = buffer.length;
        status_t status = get_bytes(buffer);
        if (generated_bytes)
            (*generated_bytes) += old_length - buffer.length;
        return status;
    }
};

/**
 * @brief Behaves like a StreamSource, but additionally grants access to the
 * internal buffer of the StreamSource.
 * 
 * If feasible, an implementer should prefer this interface over the StreamSource
 * interface as it can reduce copy operations.
 */
class OpenStreamSource : public StreamSource {
public:
    /**
     * @brief Shall return a readable range of the internal buffer.
     * 
     * @param buf: The variable being pointed to by this pointer shall be set to
     *        to the address and length of the next consecutive chunk of
     *        unconsumed data.
     *        The caller can read from the memory area indicated by this output
     *        variable and then consume the data by calling consume().
     *        If the function returns anything other than success the value
     *        shall not be modified.
     *        If this output is not needed, the pointer can be NULL.
     * 
     * @returns:
     *  kOk: The request could be handled or partially handled.
     *  kError: The request could not be handled.
     * TODO: update retvals based on StreamSource spec
     */
    virtual status_t get_buffer(cbufptr_t* buf) = 0;

    /**
     * @brief Shall advance the stream by the specified number of bytes.
     * @param length: The number of bytes by which to advance the stream.
     * TODO: specify what happens if this is more than available or more than a chunk.
     * 
     * @returns:
     *  kOk: The stream was advanced successfully.
     *  kClosed: The request succeeded and the stream is now permanently closed.
     *  kError: The request could not be handled.
     */
    virtual status_t consume(size_t length) = 0;

    status_t get_bytes(bufptr_t& buffer) final {
        cbufptr_t internal_range = {nullptr, buffer.length};
        if (get_buffer(&internal_range) != kOk) {
            return kError;
        }
        if (internal_range.length > buffer.length) {
            // TODO: this is not a bug according to the StreamSource spec
            FIBRE_LOG(E) << "got larger buffer than requested";
            internal_range.length = buffer.length;
        }
        memcpy(buffer.ptr, internal_range.ptr, internal_range.length);
        buffer += internal_range.length;
        return consume(internal_range.length);
    }
};

/**
 * @brief Behaves like a StreamSink, but additionally grants access to the
 * internal buffer of the StreamSink.
 * 
 * If feasible, an implementer should prefer this interface over the StreamSink
 * interface as it can reduce copy operations.
 */
class OpenStreamSink : public StreamSink {
public:
    /**
     * @brief Shall return a writable range of the internal buffer.
     * 
     * @param buf: The variable being pointed to by this pointer shall be set to
     *        to the address and length of the next consecutive chunk of
     *        uninitialized buffer space.
     *        The caller can write to the memory area indicated by this output
     *        variable and then commit the data by calling commit().
     *        If the function returns anything other than success the value
     *        shall not be modified.
     *        If this output is not needed, the pointer can be NULL.
     * 
     * @returns:
     *  kOk: The request could be handled or partially handled.
     *  kError: The request could not be handled.
     * TODO: update retvals based on StreamSink spec
     */
    virtual status_t get_buffer(bufptr_t* buf) = 0;

    /**
     * @brief Shall advance the stream by the specified number of bytes.
     * @param length: The number of bytes by which to advance the stream.
     * TODO: specify what happens if this is more than available or more than a chunk.
     * 
     * @returns:
     *  kOk: The stream was advanced successfully.
     *  kClosed: The request succeeded and the stream is now permanently closed.
     *  kError: The request could not be handled.
     */
    virtual status_t commit(size_t length) = 0;

    status_t process_bytes(cbufptr_t& buffer) final {
        bufptr_t internal_range = {nullptr, buffer.length};
        if (get_buffer(&internal_range) != kOk) {
            return kError;
        }
        if (internal_range.length > buffer.length) {
            FIBRE_LOG(E) << "got larger buffer than requested";
            internal_range.length = buffer.length;
        }
        memcpy(internal_range.ptr, buffer.ptr, internal_range.length);
        buffer += internal_range.length;
        return commit(internal_range.length);
    }
};

struct stream_copy_result_t {
    StreamSink::status_t dst_status;
    StreamSource::status_t src_status;
    //size_t copied_bytes = 0;
};

stream_copy_result_t stream_copy(StreamSink* dst, StreamSource* src);
stream_copy_result_t stream_copy(StreamSink* dst, OpenStreamSource* src);

template<typename TDst, typename TSrc>
stream_copy_result_t stream_copy_all(TDst* dst, TSrc* src) {
    static_assert(std::is_base_of<StreamSink, TDst>::value, "TDst must inherit from StreamSink");
    static_assert(std::is_base_of<StreamSource, TSrc>::value, "TSrc must inherit from StreamSink");

    stream_copy_result_t status;
    do {
        status = stream_copy(dst, src);
    } while (status.src_status == StreamSource::kOk && status.dst_status == StreamSink::kOk);
    return status;
}


///** @brief Returns true if the given status indicates that the stream is
// * permanently closed, false otherwise */
//__attribute__((unused))
//bool is_permanent(StreamSource::status_t status) {
//    return (status != kOk) && (status != kBusy) && (status != FULL);
//}
//
///** @brief Returns true if the given status indicates that the stream is
// * permanently closed, false otherwise */
//__attribute__((unused))
//bool is_permanent(StreamSink::status_t status) {
//    return (status != kOk) && (status != kBusy);
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
class MemoryStreamSink : public OpenStreamSink {
public:
    MemoryStreamSink(uint8_t *buffer, size_t length) :
        buffer_(buffer),
        length_(length) {}

    status_t get_buffer(bufptr_t* buf) final {
        if (buf) {
            buf->ptr = buffer_;
            buf->length = std::min(buf->length, length_);
        }
        return kOk;
    }

    status_t commit(size_t length) final {
        if (length > length_) {
            return kError;
        }
        buffer_ += length;
        length_ -= length;
        return length_ ? kOk : kClosed;
    }

    size_t get_length() { return length_; }

    size_t get_min_non_blocking_bytes() const final { return length_; }

private:
    uint8_t * buffer_;
    size_t length_;
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

    status_t get_buffer(cbufptr_t* buf) final {
        if (buf) {
            buf->ptr = buffer_;
            buf->length = std::min(buf->length, length_);
        }
        return kOk;
    }

    status_t consume(size_t length) final {
        if (length > length_) {
            return kError;
        }
        buffer_ += length;
        length_ -= length;
        return length_ ? kOk : kClosed;
    }

    size_t get_length() { return length_; }

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

    status_t process_bytes(cbufptr_t& buffer) override {
        size_t chunk = std::min(buffer.length, skip_);
        skip_ -= chunk;
        buffer += chunk;
        return skip_ ? kOk : kClosed;
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

    status_t process_bytes(cbufptr_t& buffer) final {
        FIBRE_LOG(D) << "static stream chain: process " << buffer.length << " bytes";
        while (buffer.length) {
            StreamSink *stream = get_stream(current_stream_idx_);
            if (!stream)
                return kClosed;
            size_t chunk = 0;
            status_t result = stream->process_bytes(buffer);
            //LOG_FIBRE("StaticStreamChain: processed %zu bytes", chunk);
            if (result != kClosed)
                return result;
            current_stream_idx_++;
        }
        return current_stream_idx_ < sizeof...(TStreams) ? kOk : kClosed;
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

    status_t process_bytes(cbufptr_t& buffer) final {
        FIBRE_LOG(D) << "dynamic stream chain: process " << buffer.length << " bytes";
        while (current_stream_) {
            status_t result = current_stream_->process_bytes(buffer);
            if (result != kClosed)
                return result;
            advance_state();
        }
        return kClosed;
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
            if (result != kClosed)
                return result;
            active_ = advance_state();
            if (active_) {
                stream_sink_ = TStreamSink(); // reset stream sink
            }
        }
        return active_ ? kOk : kClosed;
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
