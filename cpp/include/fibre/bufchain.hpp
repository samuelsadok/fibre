#ifndef __FIBRE_BUF_CHAIN
#define __FIBRE_BUF_CHAIN

#include <fibre/bufptr.hpp>
#include <fibre/config.hpp>
#include <algorithm>
#include <bitset>

namespace fibre {

constexpr size_t kMaxLayers = 16;

struct Chunk {
public:
    Chunk() : layer_(), buf_() {}
    Chunk(unsigned char layer, cbufptr_t buf) : layer_(layer), buf_(buf) {}

    static Chunk frame_boundary(unsigned char layer) {
        return Chunk{layer, {nullptr, SIZE_MAX}};
    }

    bool is_buf() const {
        return buf_.size() != SIZE_MAX;
    }
    bool is_frame_boundary() const {
        return buf_.size() == SIZE_MAX;
    }
    uint8_t layer() const {
        return layer_;
    }
    uint8_t layer(uint8_t base_layer) const {
        return layer() - base_layer;
    }
    Chunk elevate(int8_t layers) {
        return Chunk{(uint8_t)(layer_ + layers), buf_};
    }
    const cbufptr_t& buf() const {
        // legal if and only if is_buf() is true
        return buf_;
    }
    cbufptr_t& buf() {
        // legal if and only if is_buf() is true
        return buf_;
    }

private:
    unsigned char layer_;
    cbufptr_t buf_;
};

struct CBufIt {
    static CBufIt null() {
        return {nullptr, nullptr};
    }
    bool operator==(CBufIt lhs) {
        return chunk == lhs.chunk && byte == lhs.byte;
    }
    bool operator!=(CBufIt lhs) {
        return chunk != lhs.chunk || byte != lhs.byte;
    }

    const Chunk* chunk;
    const unsigned char* byte;
};

struct BufChain {
    BufChain() : BufChain(nullptr, nullptr) {}
    BufChain(CBufIt begin, const Chunk* end, int8_t elevation = 0)
        : bbegin_(begin.byte),
          begin_(begin.chunk),
          end_(end),
          elevation_{elevation} {}
    BufChain(const Chunk* begin, const Chunk* end)
        : BufChain(begin == end ? nullptr : begin->buf().begin(), begin, end,
                   0) {}
    template<size_t Size> BufChain(const Chunk (&chunks)[Size])
        : BufChain(chunks, chunks + Size) {}
    BufChain(const unsigned char* bbegin, const Chunk* begin, const Chunk* end,
             int8_t elevation)
        : bbegin_(bbegin), begin_(begin), end_(end), elevation_{elevation} {}

    BufChain skip_bytes(size_t n) {
        if (bbegin_ + n >= begin_->buf().end()) {
            return {begin_ + 1 == end_ ? nullptr : (begin_ + 1)->buf().begin(),
                    begin_ + 1, end_, elevation_};
        } else {
            return {bbegin_ + n, begin_, end_, elevation_};
        }
    }

    BufChain skip_chunks(size_t n) {
        return {begin_ + n == end_ ? nullptr : (begin_ + n)->buf().begin(),
                begin_ + n, end_, elevation_};
    }

    size_t n_chunks() {
        return end_ - begin_;
    }

    const Chunk* c_begin() {
        return begin_;  // TODO: we probably shouldn't give direct access to
                        // this
    }

    const Chunk* c_end() {
        return end_;  // TODO: we probably shouldn't give direct access to this
    }

    Chunk front() {
        return {(uint8_t)(begin_->layer() + elevation_),
                {bbegin_, begin_->buf().end()}};
    }

    Chunk back() {
        return {(uint8_t)((end_ - 1)->layer() + elevation_),
                {end_ - 1 == begin_ ? bbegin_ : (end_ - 1)->buf().begin(),
                 (end_ - 1)->buf().end()}};
    }

    CBufIt begin() {
        return {begin_, bbegin_};
    }

    CBufIt end() {
        return {end_, nullptr};
    }

    // TODO: could this be replaced by find_chunk_on_layer?
    CBufIt find_layer0_bound() {
        const Chunk* ch = std::find_if(begin_, end_, [&](const Chunk& chunk) {
            return chunk.is_frame_boundary() &&
                   (uint8_t)(chunk.layer() + elevation_) == 0;
        });
        if (ch == begin_) {
            return {ch, bbegin_};
        } else if (ch == end_) {
            return {ch, nullptr};
        } else {
            return {ch, ch->buf().begin()};
        }
    }

    CBufIt find_chunk_on_layer(uint8_t layer) {
        const Chunk* ch = std::find_if(begin_, end_, [&](const Chunk& chunk) {
            return chunk.layer() <= layer;
        });
        if (ch == begin_) {
            return {ch, bbegin_};
        } else if (ch == end_) {
            return {ch, nullptr};
        } else {
            return {ch, ch->buf().begin()};
        }
    }

    BufChain elevate(int8_t layers) {
        return BufChain{bbegin_, begin_, end_, (int8_t)(elevation_ + layers)};
    }

    BufChain from(CBufIt begin) {
        if (begin.byte == 0 && begin.chunk != end_) {
            begin.byte = begin.chunk->buf().begin();
        }
        return BufChain{begin.byte, begin.chunk, end_, elevation_};
    }

    BufChain until(const Chunk* end) {
        return BufChain{bbegin_, begin_, end, elevation_};
    }

private:
    const unsigned char* bbegin_;
    const Chunk* begin_;
    const Chunk* end_;
    int8_t elevation_;
};

struct BufChainBuilder {
    template<size_t Size> BufChainBuilder(Chunk (&chunks)[Size])
        : begin_(chunks), used_end_(chunks), end_(chunks + Size) {}

    Chunk* begin_;
    Chunk* used_end_;
    Chunk* end_;

    operator BufChain() {
        return {begin_, used_end_};
    }
};

struct write_iterator {
    write_iterator(BufChainBuilder& builder, uint8_t elevation = 0)
        : builder_(builder), elevation_(elevation) {}

    void operator=(Chunk chunk) {
        *(builder_.used_end_++) = {(uint8_t)(chunk.layer() + elevation_),
                                   chunk.buf()};
    }

    bool has_free_space() {
        return builder_.used_end_ != builder_.end_;
    }

    write_iterator elevate(int8_t layers) {
        return write_iterator{builder_, (uint8_t)(elevation_ + layers)};
    }

    BufChainBuilder& builder_;
    uint8_t elevation_;
};

struct TxPipe;

struct TxTask {
    using const_iterator = const Chunk*;

    BufChain chain() const {
        return BufChain{begin_, end_};
    }

    TxPipe* pipe;
    uintptr_t slot_id;
    const Chunk* begin_;
    const Chunk* end_;
};

using TxTaskChain = generic_bufptr_t<TxTask>;

template<size_t Size> struct BufChainStorage {
    using iterator = Chunk*;

    bool append_chunk(uint8_t layer, cbufptr_t buf) {
        if (n_elements >= Size) {
            return false;
        } else {
            elements[n_elements++] = Chunk{layer, buf};
            return true;
        }
    }

    bool append_frame_boundary(uint8_t layer) {
        if (n_elements >= Size) {
            return false;
        } else {
            elements[n_elements++] = Chunk::frame_boundary(layer);
            return true;
        }
    }

    iterator begin() {
        return iterator{elements};
    }
    iterator end() {
        return iterator{elements + n_elements};
    }

    uintptr_t slot_id = 0;
    size_t n_elements = 0;
    Chunk elements[Size];
};

}  // namespace fibre

#endif  // __FIBRE_BUF_CHAIN
