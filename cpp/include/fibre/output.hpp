#ifndef __FIBRE_OUTPUT_HPP
#define __FIBRE_OUTPUT_HPP

#ifndef __FIBRE_HPP
#error "This file should not be included directly. Include fibre.hpp instead."
#endif

namespace fibre {

/**
 * @brief Represents a pipe into which the local node can pump data to send it
 * to the corresponding remote node's input pipe.
 * 
 * An output pipe optionally keep track of the chunks of data that were not yet
 * acknowledged.
 */
class OutputPipe final : public StreamSink {
    /*
    * For now we say that the probability of successful delivery is monotonically
    * decreasing with increasing stream offset.
    * This means if a chunk is acknowledged before all of its preceeding bytes
    * are acknogledged, we simply ignore this.
    */

    RemoteNode* remote_node_;
    uint8_t buffer_[TX_BUF_SIZE];
    size_t buffer_pos_ = 0; /** write position relative to the buffer start */
    size_t pipe_pos_ = 0; /** position of the beginning of the buffer within the byte stream */
    uint16_t crc_init_ = CANONICAL_CRC16_INIT;
    monotonic_time_t next_due_time_ = monotonic_time_t::min();
    size_t id_; // last bit indicates server (1) or client (0)
public:
    bool guaranteed_delivery = false;

    OutputPipe(RemoteNode* remote_node, size_t idx, bool is_server)
        : remote_node_(remote_node),
        id_((idx << 1) | (is_server ? 1 : 0)) {}

    size_t get_id() const { return id_; }

    class chunk_t {
    public:
        chunk_t(OutputPipe *pipe) : pipe_(pipe) {}
        bool get_properties(size_t* offset, size_t* length, uint16_t* crc_init) {
            if (offset) *offset = pipe_ ? pipe_->pipe_pos_ : 0;
            if (length) *length = pipe_ ? pipe_->buffer_pos_ : 0;
            if (crc_init) *crc_init = pipe_ ? pipe_->crc_init_ : 0;
            return true;
        }
        bool write_to(StreamSink* output, size_t length) {
            if (length > pipe_->buffer_pos_)
                return false;
            size_t processed_bytes = 0;
            status_t status = output->process_bytes(pipe_->buffer_, pipe_->buffer_pos_, &processed_bytes);
            if (processed_bytes != length)
                return false;
            return status != ERROR;
        }
    private:
        OutputPipe *pipe_;
    };

    class chunk_list {
    public:
        chunk_list(OutputPipe* pipe) : pipe_(pipe) {}
        //chunk_t operator[] (size_t index);
        chunk_t operator[] (size_t index) {
            if (!pipe_) {
                return { 0 }; // TODO: empty iterator
            } else if (index == 0) {
                return chunk_t(pipe_);
            } else {
                return { 0 };
            }
        }

        using iterator = simple_iterator<chunk_list, chunk_t>;
        iterator begin() { return iterator(*this, 0); }
        iterator end() { return iterator(*this, pipe_->buffer_pos_ ? 1 : 0); }
    private:
        OutputPipe *pipe_;
    };

    status_t process_bytes(const uint8_t* buffer, size_t length, size_t *processed_bytes) final;

    //size_t get_n_due_bytes
    //void get_available_non_blocking_bytes(size_t* offset, size_t* length, uint16_t* crc) {
    //    
    //}

    chunk_list get_pending_chunks() {
        if (now() >= next_due_time_)
            return chunk_list(this);
        else
            return chunk_list(nullptr);
    }

    void drop_chunk(size_t offset, size_t length) {
        if (offset > pipe_pos_) {
            LOG_FIBRE(OUTPUT, "attempt to drop chunk at 0x", as_hex(offset), " but there's pending data before that at 0x", as_hex(pipe_pos_));
            return;
        }
        if (offset + length <= pipe_pos_) {
            LOG_FIBRE(OUTPUT, "already acknowledged");
            return;
        }
        if (offset < pipe_pos_) {
            offset += (pipe_pos_ - offset);
            length -= (pipe_pos_ - offset);
        }
        if (length > buffer_pos_) {
            LOG_FIBRE(OUTPUT, "ackowledged bytes that werent even available");
            return;
        }

        // shift buffer
        crc_init_ = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(crc_init_, buffer_, length);
        memmove(buffer_, buffer_ + length, buffer_pos_ - length);
        pipe_pos_ += length;
        buffer_pos_ = 0;
    }

    monotonic_time_t get_due_time() {
        return next_due_time_;
    }
    void set_due_time(size_t offset, size_t length, monotonic_time_t next_due_time) {
        // TODO: set due time for specific chunks
        next_due_time_ = next_due_time;
    }
};

class OutputChannel : public StreamSink {
public:
    std::chrono::duration<uint32_t, std::milli> resend_interval = std::chrono::milliseconds(100);
    //StreamSink operator & () { return }
    //virtual StreamSink* get_stream_sink() = 0;
    
    /**
     * @brief Returns the human readable name of the stream.
     * 
     * May return NULL.
     * 
     * The returned pointer shall not be freed by the caller and shall be valid
     * as long as the object exists.
     */
    virtual const char * get_name() const { return nullptr; }
};

__attribute__((unused))
static std::ostream& operator<<(std::ostream& stream, const OutputChannel& channel) {
    const char * name = channel.get_name();
    return stream << (name ? name : "[unnamed channel]");
}


//template<typename TStream>
class OutputChannelFromStream : public OutputChannel {
    StreamSink* output_stream_;
public:
    OutputChannelFromStream(StreamSink* stream) : output_stream_(stream) {}
    //operator TStream&() const { return output_stream_; }
    //StreamSink* get_stream_sink() { return output_stream_; }
    //const StreamSink* get_stream_sink() const { return output_stream_; }
    status_t process_bytes(const uint8_t* buffer, size_t length, size_t *processed_bytes) final {
        return output_stream_->process_bytes(buffer, length, processed_bytes);
    }
    size_t get_min_useful_bytes() const final {
        return output_stream_->get_min_useful_bytes();
    }
    size_t get_min_non_blocking_bytes() const final {
        return output_stream_->get_min_non_blocking_bytes();
    }
};

}

#endif // __FIBRE_OUTPUT_HPP
