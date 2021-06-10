
#include "print_utils.hpp"
#include <fibre/channel_discoverer.hpp>
#include <fibre/connection.hpp>
#include <fibre/domain.hpp>
#include <fibre/fibre.hpp>
#include <fibre/simple_serdes.hpp>
#include <algorithm>
#include <cstddef>

using namespace fibre;

struct Header {
    bool is_frame_boundary;
    uint8_t layer;
    Fifo::TOffset length;
};

static_assert(sizeof(Fifo::buf_) % sizeof(Header) == 0,
              "buffer size must be a multiple of the header size");
static_assert(sizeof(Fifo::buf_) <= std::numeric_limits<Fifo::TOffset>::max(),
              "buffer too long");
static_assert(sizeof(Fifo::buf_) / sizeof(Header) <=
                  std::numeric_limits<Fifo::TIndex>::max(),
              "buffer too long");
static_assert((std::alignment_of<Fifo>::value +
               offsetof(Fifo, buf_)) %
                      std::alignment_of<Header>::value ==
                  0,
              "buffer misaligned");

    constexpr Fifo::TIndex kFifoNumBlocks = sizeof(Fifo::buf_) / sizeof(Header);

namespace std {
/**
 * @brief Dumps the state of the FIFO for debugging purposes
 */
static inline __attribute__((unused)) std::ostream& operator<<(std::ostream& stream, const Fifo& fifo) {
    auto it = fifo.read_begin();
    while (it != fifo.read_end()) {
        stream << "\n\t\t" << it.chunk();
        ++it;
    }
    return stream;
}
}  // namespace std

CBufIt Fifo::append(BufChain chain) {
    while (chain.n_chunks()) {
        Chunk chunk = chain.front();
        // write_idx_ must never catch up with read_idx_ and there must be space
        // for at least one more header
        if ((kFifoNumBlocks + read_idx_ - write_idx_ - 1) % kFifoNumBlocks <
            2) {
            return chain.begin();
        }

        Header& header = *(Header*)&buf_[write_idx_ * sizeof(Header)];

        size_t payload_blocks = 0;

        if (chunk.is_buf()) {
            // can be 0 (this will generate an padding header block)
            TIndex max_data_blocks = std::min(
                kFifoNumBlocks - write_idx_ - 1,
                (read_idx_ + kFifoNumBlocks - write_idx_ - 2) % kFifoNumBlocks);

            TOffset n_copy =
                std::min(max_data_blocks * sizeof(Header), chunk.buf().size());

            header = {.is_frame_boundary = false,
                      .layer = chunk.layer(),
                      .length = n_copy};

            std::copy_n(chunk.buf().begin(), n_copy,
                        &buf_[(write_idx_ + 1) * sizeof(Header)]);

            chain = chain.skip_bytes(n_copy);

            payload_blocks = (n_copy + sizeof(Header) - 1) / sizeof(Header);
        } else {
            header = {
                .is_frame_boundary = true, .layer = chunk.layer(), .length = 0};

            chain = chain.skip_chunks(1);
        }

        write_idx_ = (write_idx_ + 1 + payload_blocks) % kFifoNumBlocks;
    }

    // TODO: coalesce frames (trades code size for RAM efficiency)

    return chain.begin();
}

Fifo::ReadIterator Fifo::read_begin() const {
    return {this, read_idx_, read_idx_offset_};
}

Fifo::ReadIterator Fifo::read_end() const {
    return {this, write_idx_, 0};
}

bool Fifo::has_data() const {
    return read_begin() != read_end();
}

Fifo::ReadIterator Fifo::read(ReadIterator it, write_iterator target) const {
    while (target.has_free_space() && it != read_end()) {
        target = it.chunk();
        ++it;
    }
    return it;
}

Fifo::ReadIterator Fifo::advance_it(ReadIterator it,
                                    std::array<uint16_t, 3> n_frames,
                                    std::array<uint16_t, 3> n_bytes) {
    while (it != read_end()) {
        if (it.chunk().is_frame_boundary()) {
            if (n_frames[it.chunk().layer()]) {
                n_frames[it.chunk().layer()]--;
            } else {
                return it;
            }
        } else if (n_frames[it.chunk().layer()]) {
            // walk over chunk
        } else {
            if (n_bytes[it.chunk().layer()] >= it.chunk().buf().size()) {
                // walk over chunk
                n_bytes[it.chunk().layer()] -= it.chunk().buf().size();
            } else {
                // walk into chunk
                return ReadIterator{
                    this, it.idx_,
                    (TOffset)(it.offset_ + n_bytes[it.chunk().layer()])};
            }
        }
        ++it;
    }

    // if n_frames or n_bytes still contain a non-zero value at this point, the
    // input was invalid

    return it;
}

Fifo::ReadIterator Fifo::advance_it(ReadIterator it, Chunk* c_begin,
                                    Chunk* c_end, CBufIt end) {
    for (size_t i = 0; i < (size_t)(end.chunk - c_begin); ++i) {
        ++it;
    }
    if (end.chunk != c_end) {
        it.offset_ += end.byte - end.chunk->buf().begin();
    }
    return it;
}

void Fifo::drop_until(ReadIterator it) {
    read_idx_ = it.idx_;
    read_idx_offset_ = it.offset_;
}

void Fifo::consume(size_t n_chunks) {
    while (n_chunks--) {
        Header& header = *(Header*)&buf_[read_idx_ * sizeof(Header)];

        size_t payload;
        if (header.is_frame_boundary) {
            payload = 0;
        } else {
            payload = (header.length + sizeof(Header) - 1) / sizeof(Header);
        }
        read_idx_ = (read_idx_ + 1 + payload) % kFifoNumBlocks;
    }
}

bool Fifo::fsck(TOffset it) const {
    if (read_idx_ >= kFifoNumBlocks || write_idx_ >= kFifoNumBlocks) {
        return false;
    }

    bool found_it = false;
    TIndex idx = read_idx_;

    while (idx != write_idx_) {
        Header& header = *(Header*)&buf_[idx * sizeof(Header)];

        bool is_valid =
            ((idx + 1) * sizeof(Header) + header.length <= sizeof(buf_)) &&
            (header.layer < kMaxLayers) &&
            ((header.length == 0) == header.is_frame_boundary ||
             (idx == kFifoNumBlocks - 1));  // last block can be empty (padding)
        if (!is_valid) {
            return false;
        }

        if (it == idx) {
            found_it = true;
        }

        idx =
            (idx + 1 + (header.length + sizeof(Header) - 1) / sizeof(Header)) %
            kFifoNumBlocks;
    }

    return found_it || (it == idx);
}

Fifo::ReadIterator& Fifo::ReadIterator::operator++() {
    Header& header = *(Header*)&fifo_->buf_[idx_ * sizeof(Header)];
    idx_ = (idx_ + 1 + (header.length + sizeof(Header) - 1) / sizeof(Header)) %
           kFifoNumBlocks;
    offset_ = 0;
    return *this;
}

Chunk Fifo::ReadIterator::chunk() {
    Header& header = *(Header*)&fifo_->buf_[idx_ * sizeof(Header)];
    if (header.is_frame_boundary) {
        return Chunk::frame_boundary(header.layer);
    } else {
        return Chunk{header.layer,
                     {&fifo_->buf_[(idx_ + 1) * sizeof(Header)] + offset_,
                      (size_t)(header.length - offset_)}};
    }
}

void ConnectionInputSlot::process_sync(BufChain chain) {
    while (chain.n_chunks()) {
        Chunk chunk = chain.front();

        if (chunk.layer() == 0) {
            if (chunk.is_buf()) {
                size_t n_copy =
                    std::min(sizeof(layer0_cache_) - layer0_cache_pos_,
                             chunk.buf().size());
                std::copy_n(chunk.buf().begin(), n_copy,
                            layer0_cache_ + layer0_cache_pos_);
                layer0_cache_pos_ += n_copy;

            } else {
                ConnectionPos pos;

                if (layer0_cache_pos_ >= 13) {
                    for (size_t i = 0; i < 3; ++i) {
                        pos.frame_ids[i] =
                            read_le<uint16_t>(&layer0_cache_[4 * i + 1]);
                        pos.offsets[i] =
                            read_le<uint16_t>(&layer0_cache_[4 * i + 3]);
                    }

                    if (layer0_cache_[0] == 0) {
                        pos_ = pos;
                    } else {
                        F_LOG_D(conn_.domain_->ctx->logger, "got ack ");
                        conn_.on_ack(pos);
                    }
                }
                layer0_cache_pos_ = 0;
            }

            chain = chain.skip_chunks(1);

        } else if (conn_.rx_tail_.frame_ids == pos_.frame_ids &&
                   conn_.rx_tail_.offsets[chunk.layer() - 1] >
                       pos_.offsets[chunk.layer() - 1] &&
                   chunk.is_buf()) {
            size_t n_skip =
                std::min((size_t)(conn_.rx_tail_.offsets[chunk.layer() - 1] -
                                  pos_.offsets[chunk.layer() - 1]),
                         chunk.buf().size());
            pos_.offsets[chunk.layer() - 1] += n_skip;
            chain = chain.skip_bytes(n_skip);
            conn_.send_ack_ = true;

        } else {
            if (conn_.rx_tail_.frame_ids == pos_.frame_ids &&
                conn_.rx_tail_.offsets[chunk.layer() - 1] ==
                    pos_.offsets[chunk.layer() - 1]) {
                Chunk ch = chunk.elevate(-1);
                conn_.rx_fifo_.append({&ch, &ch + 1});

                if (chunk.is_buf()) {
                    conn_.rx_tail_.offsets[chunk.layer() - 1] +=
                        chunk.buf().size();
                } else {
                    conn_.rx_tail_.frame_ids[chunk.layer() - 1]++;
                    conn_.rx_tail_.offsets[chunk.layer() - 1] = 0;
                }
            }

            if (chunk.is_buf()) {
                pos_.offsets[chunk.layer() - 1] += chunk.buf().size();
            } else {
                pos_.frame_ids[chunk.layer() - 1]++;
                pos_.offsets[chunk.layer() - 1] = 0;
            }

            chain = chain.skip_chunks(1);
            conn_.send_ack_ = true;
        }
    }

    // TODO: make this optional for efficiency reasons
    if (!conn_.rx_fifo_.fsck()) {
        F_LOG_E(conn_.domain_->ctx->logger, "RX fifo inconsistent");
        // TODO: handle
    }

    // For efficiency reasons we only handle the RX fifo data once per
    // process_sync() call. This means process_sync() cannot consume large
    // amounts of data (larger than the FiFo size) at once even if the actual RX
    // handler could.
    conn_.handle_rx_not_empty();
    conn_.handle_tx_not_full();
    conn_.handle_tx_not_empty();
}

ConnectionOutputSlot::ConnectionOutputSlot(Connection& conn_)
    : conn_(conn_), tx_it_(conn_.tx_fifo_.read_begin()) {}

bool ConnectionOutputSlot::has_data() {
    return !sending_ &&
           (!sent_header_recently_ || tx_it_ != conn_.tx_fifo_.read_end() ||
            conn_.send_ack_);
}

BufChain ConnectionOutputSlot::get_task() {
    BufChainBuilder builder{storage_};
    write_iterator it{builder};

    if (!sent_header_recently_) {
        sent_header_recently_ = true;
        pos_header_[0] = 0;
        for (size_t i = 0; i < 3; ++i) {
            write_le<uint16_t>(conn_.tx_head_.frame_ids[i],
                               &pos_header_[4 * i + 1]);
            write_le<uint16_t>(conn_.tx_head_.offsets[i],
                               &pos_header_[4 * i + 3]);
        }

        it = Chunk(1, {&conn_.tx_protocol_, 1});
        it = Chunk(1, conn_.tx_call_id_);
        it = Chunk::frame_boundary(1);
        it = Chunk(2, pos_header_);
        it = Chunk::frame_boundary(2);
    }

    if (conn_.send_ack_) {
        conn_.send_ack_ = false;

        ack_buf_[0] = 1;
        for (size_t i = 0; i < 3; ++i) {
            write_le<uint16_t>(conn_.rx_tail_.frame_ids[i],
                               &ack_buf_[4 * i + 1]);
            write_le<uint16_t>(conn_.rx_tail_.offsets[i], &ack_buf_[4 * i + 3]);
        }

        it = Chunk(2, ack_buf_);
        it = Chunk::frame_boundary(2);
    }

    sending_storage_begin_ = builder.used_end_;
    sending_tx_it_ = conn_.tx_fifo_.read(tx_it_, it.elevate(3));
    sending_storage_end_ = builder.used_end_;

    sending_ = true;

    F_LOG_T(conn_.domain_->ctx->logger, "create TX task");

    return {builder};
}

void ConnectionOutputSlot::release_task(CBufIt end) {
    sending_ = false;
    F_LOG_T(conn_.domain_->ctx->logger, "release TX task");
    if (end.chunk >= sending_storage_begin_) {
        if (end == CBufIt{sending_storage_end_, nullptr}) {
            tx_it_ = sending_tx_it_;
        } else {
            // Succeeded in sending some of the payload
            tx_it_ = conn_.tx_fifo_.advance_it(tx_it_, sending_storage_begin_,
                                               sending_storage_end_, end);
        }
    } else {
        // Sent only some (but not all) of the header chunks
    }
}

ConnectionInputSlot* Connection::open_rx_slot() {
    return input_slots_.alloc(*this);
}

void Connection::close_rx_slot(ConnectionInputSlot* slot) {
    input_slots_.free(slot);
}

bool Connection::open_tx_slot(FrameStreamSink* sink, Node* node) {
    uintptr_t slot_id;
    if (!sink->open_output_slot(&slot_id, node)) {
        return false;
    }

    ConnectionOutputSlot* slot = output_slots_.alloc(sink, *this);
    if (!slot) {
        sink->close_output_slot(slot_id);
        return false;
    }

    slot->backend_slot_id = slot_id;

    if (slot->has_data()) {
        sink->multiplexer_.add_source(slot);
    } else {
        slot->multiplexer_ = &sink->multiplexer_;
    }

    return true;
}

void Connection::close_tx_slot(FrameStreamSink* sink) {
    auto it = output_slots_.find(sink);
    if (it != output_slots_.end()) {
        ConnectionOutputSlot& slot = it->second;
        uintptr_t slot_id = slot.backend_slot_id;

        if (slot.multiplexer_) {
            slot.multiplexer_ = nullptr;
        } else {
            sink->multiplexer_.remove_source(&slot);
        }

        output_slots_.erase(it);
        sink->close_output_slot(slot_id);
    }
}

void Connection::handle_rx_not_empty() {
    if (rx_busy_) {
        // The connection handler is already busy handling data and will
        // eventually return via rx_done().
        return;
    }

    WriteArgs args = rx_logic();

    while (!args.is_busy()) {  // args is busy if the RX buffer runs empty
        WriteResult result = on_rx(args);

        if (result.is_busy()) {
            rx_busy_ = true;
            break;
        }

        args = rx_logic(result);
    }
}

void Connection::handle_tx_not_empty() {
    for (auto& kv : output_slots_) {
        ConnectionOutputSlot& slot = kv.second;
        if (slot.has_data() && slot.multiplexer_) {
            Multiplexer* multiplexer = slot.multiplexer_;
            slot.multiplexer_ = nullptr;
            multiplexer->add_source(&slot);
        }
    }
}

void Connection::handle_tx_not_full() {
    WriteArgs args = pending_tx_;
    for (;;) {
        CBufIt tx_end = tx_fifo_.append(args.buf);
        if (tx_end == args.buf.begin()) {
            pending_tx_ = args;
            return;
        } else {
            pending_tx_ = {};
            args = on_tx_done({kFibreOk, tx_end});
            if (args.is_busy()) {
                return;
            }
        }
    }
}

void Connection::on_ack(ConnectionPos pos) {
    std::array<uint16_t, 3> n_frames;
    std::array<uint16_t, 3> offsets;
    for (size_t i = 0; i < 3; ++i) {
        int16_t diff = (int16_t)(pos.frame_ids[i] - tx_head_.frame_ids[i]);
        if (diff < 0) {
            n_frames[i] = 0;
            offsets[i] = 0;
        } else if (diff == 0) {
            n_frames[i] = 0;
            offsets[i] = std::max(pos.offsets[i] - tx_head_.offsets[i], 0);
        } else {
            n_frames[i] = diff;
            offsets[i] = pos.offsets[i];
        }
    }

    tx_fifo_.drop_until(
        tx_fifo_.advance_it(tx_fifo_.read_begin(), n_frames, offsets));

    tx_head_ = pos;

    // TODO: A malicious sender could send an ack that is ahead of what we've
    // already sent. In this case the TX slot's tx_it_ must be advanced
    // accordingly. Currently this is not handled and only results in the error
    // log below.

    for (auto& kv : output_slots_) {
        ConnectionOutputSlot& slot = kv.second;
        if (!tx_fifo_.fsck(slot.tx_it_.idx_)) {
            F_LOG_E(domain_->ctx->logger, "TX fifo inconsistent: ");
            // TODO: handle
        }
    }
}

WriteResult Connection::tx(WriteArgs args) {
    CBufIt tx_end = tx_fifo_.append(args.buf);
    if (tx_end == args.buf.begin()) {
        // resumed in handle_tx_not_full
        pending_tx_ = args;
        return WriteResult::busy();
    } else {
        pending_tx_ = {};
        handle_tx_not_empty();
        return {kFibreOk, tx_end};
    }
}

WriteArgs Connection::rx_logic() {
    if (!rx_fifo_.has_data()) {
        return WriteArgs::busy();
    }

    BufChainBuilder builder{upcall_chunks_};
    write_iterator it{builder};
    rx_fifo_.read(rx_fifo_.read_begin(), it);
    upcall_chunks_end_ = builder.used_end_;
    return {builder, Status::kFibreOk};
}

WriteArgs Connection::rx_logic(WriteResult result) {
    rx_fifo_.drop_until(rx_fifo_.advance_it(
        rx_fifo_.read_begin(), upcall_chunks_, upcall_chunks_end_, result.end));
    return rx_logic();
}

WriteArgs Connection::rx_done(WriteResult result) {
    WriteArgs args = rx_logic(result);
    rx_busy_ = !args.is_busy();
    // handle_rx_not_full(); TODO: unblock reception after buffer was full
    return args;
}
