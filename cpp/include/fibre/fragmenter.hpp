#ifndef __FIBRE_FRAGMENTER_HPP
#define __FIBRE_FRAGMENTER_HPP

#include <fibre/stream.hpp>
#include <fibre/logging.hpp>
#include <limits.h>

DEFINE_LOG_TOPIC(FRAG);
#define current_log_topic LOG_TOPIC_FRAG

namespace fibre {

/**
 * @brief Can be fed chunks of bytes in a random order.
 */
class Defragmenter {
public:
    /**
     * @brief Processes a chunk of data.
     * 
     * @param buffer: The chunk of data to process.
     *        The buffer will be advanced to reflect which bytes the caller can
     *        consider as handled (or cached). This includes data that was
     *        already known from a previous call to this function.
     *        If the defragmenter decides to handle/cache bytes in the middle of
     *        the chunk (as opposed to the beginning), this will not be reflected
     *        to the caller.
     *        The only reason why not all of the chunk would be processed
     *        is that the internal buffer is (temporarily) full.
     */
    virtual void process_chunk(cbufptr_t& buffer, size_t offset) = 0;
};

class Fragmenter {
public:
    virtual void get_chunk(cbufptr_t& buffer, size_t* offset) = 0;

    /**
     * @brief Marks the given range as acknowledged, meaning that the data will
     * be discarded.
     * 
     * If the stream sink was previously full, this might clear up new space.
     */
    virtual void acknowledge_chunk(size_t offset, size_t length) = 0;
};

/**
 * @brief Implements a very simple defragmenter with a fixed size internal buffer.
 */
template<size_t I>
class FixedBufferDefragmenter : public Defragmenter, public OpenStreamSource {
public:
    void process_chunk(cbufptr_t& buffer, size_t offset) final {
        // prune start of chunk
        if (offset < read_ptr_) {
            size_t diff = read_ptr_ - offset;
            if (diff >= buffer.length) {
                buffer += buffer.length;
                return; // everything in this chunk was already received and consumed before
            }
            FIBRE_LOG(D) << "discarding " << diff << " bytes at beginning of chunk";
            buffer += diff;
            offset += diff;
        }

        // prune end of chunk
        if (offset + buffer.length > read_ptr_ + I) {
            size_t diff = offset + buffer.length - (read_ptr_ + I);
            if (diff >= buffer.length) {
                return; // the chunk starts so far into the future that we can't use any of it
            }
            FIBRE_LOG(D) << "discarding " << diff << " bytes at end of chunk";
            buffer.length -= diff;
        }

        size_t dst_offset = (offset % I);

        // copy usable part of chunk to internal buffer
        // may need two copy operations because buf_ is a circular buffer
        if (buffer.length > I - dst_offset) {
            memcpy(buf_ + dst_offset, buffer.ptr, I - dst_offset);
            set_valid_table(dst_offset, I - dst_offset);
            buffer += (I - dst_offset);
            dst_offset = 0;
        }
        memcpy(buf_ + dst_offset, buffer.ptr, buffer.length);
        set_valid_table(dst_offset, buffer.length);
        buffer += buffer.length;
    }

    status_t get_buffer(cbufptr_t* buf) final {
        if (buf) {
            buf->ptr = buf_ + (read_ptr_ % I);
            buf->length = count_valid_table(1, read_ptr_ % I, std::min(buf->length, I - (read_ptr_ % I)));
        }
        return kOk;
    }

    status_t consume(size_t length) final {
        FIBRE_LOG(D) << "consume " << length << " bytes";
        clear_valid_table(read_ptr_ % I, length);
        read_ptr_ += length;
        return count_valid_table(1, read_ptr_ % I, 1) ? kOk : kBusy;
    }

private:
    static constexpr size_t bits_per_int = sizeof(unsigned int) * CHAR_BIT;

    /** @brief Flips a consecutive chunk of bits in valid_table_ to 1 */
    void set_valid_table(size_t offset, size_t length) {
        for (size_t bit = offset; bit < offset + length; ++bit) {
            valid_table_[bit / bits_per_int] |= ((unsigned int)1 << (bit % bits_per_int));
        }
    }

    /** @brief Flips a consecutive chunk of bits in valid_table_ to 0 */
    void clear_valid_table(size_t offset, size_t length) {
        for (size_t bit = offset; bit < offset + length; ++bit) {
            valid_table_[bit / bits_per_int] &= ~((unsigned int)1 << (bit % bits_per_int));
        }
    }

    /** @brief Counts how many bits consecutive bits in valid_table_ are 1, starting at offset */
    size_t count_valid_table(unsigned int expected_val, size_t offset, size_t length) {
        FIBRE_LOG(D) << "counting valid table from " << offset << ", length " << length;
        for (size_t bit = offset; bit < offset + length; ++bit) {
            if (((valid_table_[bit / bits_per_int] >> (bit % bits_per_int)) & 1) != expected_val) {
                FIBRE_LOG(D) << "invalid at " << bit;
                return bit - offset;
            }
        }
        FIBRE_LOG(D) << "all " << length << " valid";
        return length;
    }

    unsigned int valid_table_[(I + bits_per_int - 1) / bits_per_int] = {0};
    uint8_t buf_[I];
    size_t read_ptr_ = 0;
};

/**
 * @brief Implements a very simple fragmenter with a fixed size internal buffer.
 * 
 * This fragmenter always emits the chunk which represents the oldest data that
 * wasn't acknowledged yet.
 */
template<size_t I>
class FixedBufferFragmenter : public Fragmenter, public OpenStreamSink {
public:
    void get_chunk(cbufptr_t& buffer, size_t* offset) final {
        size_t read_ptr = write_ptr_ - I + count_fresh_table(0, write_ptr_ % I, I - (write_ptr_ % I));
        if ((read_ptr % I) == 0) {
            read_ptr += count_fresh_table(0, 0, write_ptr_ - read_ptr);
        }
        
        buffer = {
            .ptr = &buf_[read_ptr % I],
            .length = count_fresh_table(1, read_ptr % I, std::min(buffer.length, I - (read_ptr % I))) % I
        };
        if (offset)
            *offset = read_ptr;
    }

    void acknowledge_chunk(size_t offset, size_t length) final {
        // prune end of chunk
        if (offset + length > write_ptr_) {
            size_t diff = offset + length - write_ptr_;
            FIBRE_LOG(E) << "received ack for future bytes";
            if (diff >= length) {
                return; // the ack starts so far into the future that we can't use any of it
            }
            length -= diff;
        }

        // prune start of chunk
        if (offset + I < write_ptr_) {
            size_t diff = write_ptr_ - (offset + I);
            if (diff >= length) {
                return; // everything in this chunk was already acknowledged before
            }
            FIBRE_LOG(D) << "received redundant ack for " << diff << " bytes";
            offset += diff;
            length -= diff;
        }

        FIBRE_LOG(D) << "received ack for " << length << " bytes";

        size_t dst_offset = (offset % I);
        
        if (length > I - dst_offset) {
            clear_fresh_table(dst_offset, I - dst_offset);
            length -= (I - dst_offset);
            dst_offset = 0;
        }
        clear_fresh_table(dst_offset, length);
    }

    status_t get_buffer(bufptr_t* buf) final {
        if (buf) {
            buf->ptr = buf_ + (write_ptr_ % I);
            buf->length = count_fresh_table(0, write_ptr_ % I, std::min(buf->length, I - (write_ptr_ % I)));
        }
        return kOk;
    }

    status_t commit(size_t length) final {
        FIBRE_LOG(D) << "commit " << length << " bytes";
        set_fresh_table(write_ptr_ % I, length);
        write_ptr_ += length;
        return kOk;
    }

private:
    static constexpr size_t bits_per_int = sizeof(unsigned int) * CHAR_BIT;

    /** @brief Flips a consecutive chunk of bits in fresh_table_ to 1 */
    void set_fresh_table(size_t offset, size_t length) {
        for (size_t bit = offset; bit < offset + length; ++bit) {
            fresh_table_[bit / bits_per_int] |= ((unsigned int)1 << (bit % bits_per_int));
        }
    }

    /** @brief Flips a consecutive chunk of bits in fresh_table_ to 0 */
    void clear_fresh_table(size_t offset, size_t length) {
        for (size_t bit = offset; bit < offset + length; ++bit) {
            fresh_table_[bit / bits_per_int] &= ~((unsigned int)1 << (bit % bits_per_int));
        }
    }

    /** @brief Counts how many bits consecutive bits in fresh_table_ are 1, starting at offset */
    size_t count_fresh_table(unsigned int expected_val, size_t offset, size_t length) {
        FIBRE_LOG(D) << "counting fresh table from " << offset << ", length " << length;
        for (size_t bit = offset; bit < offset + length; ++bit) {
            if (((fresh_table_[bit / bits_per_int] >> (bit % bits_per_int)) & 1) != expected_val) {
                FIBRE_LOG(D) << "non-match at " << bit;
                return bit - offset;
            }
        }
        FIBRE_LOG(D) << "all " << length << " fresh";
        return length;
    }

    unsigned int fresh_table_[(I + bits_per_int - 1) / bits_per_int] = {0}; // marks which bytes are fresh (valid and not yet acknowledged)
    uint8_t buf_[I];
    size_t write_ptr_ = 0;
};

}

#undef current_log_topic

#endif // __FIBRE_FRAGMENTER_HPP
