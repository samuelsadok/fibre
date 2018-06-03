#ifndef __STREAM_HPP
#define __STREAM_HPP

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// This value must not be larger than USB_TX_DATA_SIZE defined in usbd_cdc_if.h
constexpr uint16_t TX_BUF_SIZE = 32; // does not work with 64 for some reason
constexpr uint16_t RX_BUF_SIZE = 128; // larger values than 128 have currently no effect because of protocol limitations


class PacketSink {
public:
    // @brief Get the maximum packet length (aka maximum transmission unit)
    // A packet size shall take no action and return an error code if the
    // caller attempts to send an oversized packet.
    virtual size_t get_mtu() = 0;

    // @brief Processes a packet.
    // The blocking behavior shall depend on the thread-local deadline_ms variable.
    // @return: 0 on success, otherwise a non-zero error code
    // TODO: define what happens when the packet is larger than what the implementation can handle.
    virtual int process_packet(const uint8_t* buffer, size_t length) = 0;
};

class StreamSink {
public:
    // @brief Processes a chunk of bytes that is part of a continuous stream.
    // The blocking behavior shall depend on the thread-local deadline_ms variable.
    // @param processed_bytes: if not NULL, shall be incremented by the number of
    //        bytes that were consumed.
    // @return: 0 on success, otherwise a non-zero error code
    virtual int process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) = 0;

    // @brief Returns the number of bytes that can still be written to the stream.
    // Shall return SIZE_MAX if the stream has unlimited lenght.
    // TODO: deprecate
    virtual size_t get_free_space() = 0;

    /*int process_bytes(const uint8_t* buffer, size_t length) {
        size_t processed_bytes = 0;
        return process_bytes(buffer, length, &processed_bytes);
    }*/
};

class StreamSource {
public:
    // @brief Generate a chunk of bytes that are part of a continuous stream.
    // The blocking behavior shall depend on the thread-local deadline_ms variable.
    // @param generated_bytes: if not NULL, shall be incremented by the number of
    //        bytes that were written to buffer.
    // @return: 0 on success, otherwise a non-zero error code
    virtual int get_bytes(uint8_t* buffer, size_t length, size_t* generated_bytes) = 0;

    // @brief Returns the number of bytes that can still be written to the stream.
    // Shall return SIZE_MAX if the stream has unlimited lenght.
    // TODO: deprecate
    //virtual size_t get_free_space() = 0;
};

class StreamToPacketSegmenter : public StreamSink {
public:
    StreamToPacketSegmenter(PacketSink& output) :
        output_(output)
    {
    };

    int process_bytes(const uint8_t *buffer, size_t length, size_t* processed_bytes);
    
    size_t get_free_space() { return SIZE_MAX; }

private:
    uint8_t header_buffer_[3];
    size_t header_index_ = 0;
    uint8_t packet_buffer_[RX_BUF_SIZE];
    size_t packet_index_ = 0;
    size_t packet_length_ = 0;
    PacketSink& output_;
};


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

// @brief: Represents a stream sink that's based on an underlying packet sink.
// A single call to process_bytes may result in multiple packets being sent.
class PacketBasedStreamSink : public StreamSink {
public:
    PacketBasedStreamSink(PacketSink& packet_sink) : _packet_sink(packet_sink) {}
    ~PacketBasedStreamSink() {}

    int process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) {
        // Loop to ensure all bytes get sent
        while (length) {
            size_t chunk = length < _packet_sink.get_mtu() ? length : _packet_sink.get_mtu();
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
};

// Implements the StreamSink interface by writing into a fixed size
// memory buffer.
class MemoryStreamSink : public StreamSink {
public:
    MemoryStreamSink(uint8_t *buffer, size_t length) :
        buffer_(buffer),
        buffer_length_(length) {}

    // Returns 0 on success and -1 if the buffer could not accept everything because it became full
    int process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) {
        size_t chunk = length < buffer_length_ ? length : buffer_length_;
        memcpy(buffer_, buffer, chunk);
        buffer_ += chunk;
        buffer_length_ -= chunk;
        if (processed_bytes)
            *processed_bytes += chunk;
        return chunk == length ? 0 : -1;
    }

    size_t get_free_space() { return buffer_length_; }

private:
    uint8_t * buffer_;
    size_t buffer_length_;
};

// Implements the StreamSink interface by discarding the first couple of bytes
// and then forwarding the rest to another stream.
class NullStreamSink : public StreamSink {
public:
    NullStreamSink(size_t skip, StreamSink& follow_up_stream) :
        skip_(skip),
        follow_up_stream_(follow_up_stream) {}

    // Returns 0 on success and -1 if the buffer could not accept everything because it became full
    int process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) {
        if (skip_ < length) {
            buffer += skip_;
            length -= skip_;
            if (processed_bytes)
                *processed_bytes += skip_;
            skip_ = 0;
            return follow_up_stream_.process_bytes(buffer, length, processed_bytes);
        } else {
            skip_ -= length;
            if (processed_bytes)
                *processed_bytes += length;
            return 0;
        }
    }

    size_t get_free_space() { return skip_ + follow_up_stream_.get_free_space(); }

private:
    size_t skip_;
    StreamSink& follow_up_stream_;
};

#endif // __STREAM_HPP
