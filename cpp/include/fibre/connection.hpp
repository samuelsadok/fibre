#ifndef __FIBRE_SERVER_STREAM_HPP
#define __FIBRE_SERVER_STREAM_HPP

namespace fibre {
class Connection;
struct ConnectionInputSlot;
}  // namespace fibre

#include <fibre/bufchain.hpp>
#include <fibre/pool.hpp>
#include <fibre/socket.hpp>
#include <fibre/tx_pipe.hpp>
#include <stdint.h>

namespace fibre {

class Domain;
struct FrameStreamSink;
struct Node;

struct Fifo {
    using TIndex = uint8_t;
    using TOffset = uint16_t;

    struct ReadIterator {
        ReadIterator(const Fifo* fifo, TIndex idx, TOffset offset)
            : fifo_(fifo), idx_(idx), offset_(offset) {}
        ReadIterator() : ReadIterator(nullptr, 0, 0) {}
        ReadIterator& operator++();
        bool operator!=(const ReadIterator& other) {
            return idx_ != other.idx_ || offset_ != other.offset_;
        }
        Chunk chunk();

        const Fifo* fifo_;
        TIndex idx_;
        TOffset offset_;
    };

    CBufIt append(BufChain chain);

    ReadIterator read_begin() const;
    ReadIterator read_end() const;
    bool has_data() const;
    ReadIterator read(ReadIterator it, write_iterator target) const;
    ReadIterator advance_it(ReadIterator it, std::array<uint16_t, 3> n_frames,
                            std::array<uint16_t, 3> n_bytes);
    ReadIterator advance_it(ReadIterator it, Chunk* c_begin, Chunk* c_end,
                            CBufIt end);
    void drop_until(ReadIterator it);
    void consume(size_t n_chunks);  // TODO: deprecate (?) (use iterators)
    bool fsck(TOffset it) const;
    bool fsck() const {
        return fsck(read_idx_);
    }

    uint8_t buf_[256];  // TODO: make customizable
    TIndex read_idx_ = 0;
    TIndex write_idx_ = 0;
    TOffset read_idx_offset_ = 0;
};

struct ConnectionPos {
    std::array<uint16_t, 3> frame_ids;
    std::array<uint16_t, 3> offsets;
};

struct ConnectionInputSlot {
    ConnectionInputSlot(Connection& conn) : conn_(conn) {}

    void process_sync(BufChain chain);

    Connection& conn_;

    uint8_t layer0_cache_[13];
    size_t layer0_cache_pos_ = 0;

    ConnectionPos pos_;
};

struct ConnectionOutputSlot final : TxPipe {
    ConnectionOutputSlot(Connection& conn);

    bool has_data() final;
    BufChain get_task() final;
    void release_task(CBufIt end) final;

    Connection& conn_;

    Chunk storage_[10];
    uint8_t pos_header_[13];
    uint8_t ack_buf_[13];
    bool sent_header_recently_ = false;
    bool sending_ = false;  // true while there is a send task pending
    Chunk* sending_storage_begin_;
    Chunk* sending_storage_end_;
    Fifo::ReadIterator tx_it_;
    Fifo::ReadIterator sending_tx_it_;
};

class Connection {
    friend struct ConnectionInputSlot;
    friend struct ConnectionOutputSlot;

public:
    Connection(Domain* domain, std::array<uint8_t, 16> tx_call_id,
               uint8_t tx_protocol)
        : domain_{domain}, tx_call_id_{tx_call_id}, tx_protocol_{tx_protocol} {}

    ConnectionInputSlot* open_rx_slot();
    void close_rx_slot(ConnectionInputSlot* slot);

    bool open_tx_slot(FrameStreamSink* sink, Node* node);
    void close_tx_slot(FrameStreamSink* sink);

protected:
    void handle_rx_not_empty();
    void handle_tx_not_empty();
    void handle_tx_not_full();

    void on_ack(ConnectionPos pos);
    WriteResult tx(WriteArgs args);
    virtual WriteArgs on_tx_done(WriteResult result) = 0;
    virtual WriteResult on_rx(WriteArgs args) = 0;
    WriteArgs rx_logic();
    WriteArgs rx_logic(WriteResult result);
    WriteArgs rx_done(WriteResult result);

    Domain* domain_;  // TODO: mignt not be required?
    std::array<uint8_t, 16> tx_call_id_;
    uint8_t tx_protocol_;
    bool send_ack_ =
        false;  // Indicates if an ack is to be sent. Becomes true
                // on any incoming payload chunk on any input slot. Becomes
                // false whenever an ack is sent on any output slot.

    ConnectionPos rx_tail_;
    ConnectionPos tx_head_;

    // TODO: customizable capacity
    Pool<ConnectionInputSlot, 1> input_slots_;
    Map<FrameStreamSink*, ConnectionOutputSlot, 1> output_slots_;

    Fifo rx_fifo_;
    Fifo tx_fifo_;

    WriteArgs pending_tx_;
    bool rx_busy_ = false;

    Chunk upcall_chunks_[8];
    Chunk* upcall_chunks_end_;
};

}  // namespace fibre

#endif  // __FIBRE_SERVER_STREAM_HPP
