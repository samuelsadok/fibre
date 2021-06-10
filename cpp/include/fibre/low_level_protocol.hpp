#ifndef __FIBRE_LOW_LEVEL_PROTOCOL
#define __FIBRE_LOW_LEVEL_PROTOCOL

#include <fibre/bufchain.hpp>
#include <fibre/cpp_utils.hpp>
#include <algorithm>

namespace fibre {

struct SenderState {
    uint16_t frame_ids[kMaxLayers] = {};
    uint16_t offsets[kMaxLayers] = {};

    void inc(uint8_t layer) {
        for (uint8_t i = layer; i < kMaxLayers; ++i) {
            frame_ids[i]++;
            offsets[i] = 0;
        }
    }
};

using ReceiverState = SenderState;

struct LowLevelProtocol {
    /**
     * @brief Packs the data chain into a packet.
     *
     * TODO: chain should have the argument BufChain
     */
    static CBufIt pack(SenderState& state, BufChain chain, bufptr_t* p_packet);

    /**
     * @brief Unpacks a packet into a chain of data chunks.
     *
     * The actual data is not copied and therefore the original packet must be
     * kept valid until the resulting chain is no longer used.
     *
     * @param packet: The packet to decode.
     * @param chain: The memory-backed buffer chain to store the decoded chunk
     *        boundaries.
     * @returns false: If the packet is malformed (too short or a reserved bit
     * is set).
     */
    static bool unpack(ReceiverState& state, cbufptr_t packet,
                       uint8_t* reset_layer, write_iterator it);
};

inline CBufIt LowLevelProtocol::pack(SenderState& state, BufChain chain,
                                     bufptr_t* p_packet) {
    if (!p_packet) {
        return CBufIt::null();
    }

    bufptr_t& packet = *p_packet;

    if (chain.begin() == chain.end()) {
        return chain.begin();  // must not be empty (TODO: is this an error?)
    }

    uint8_t max_layer = 0;
    std::bitset<kMaxLayers> include_offsets;

    BufChain chain_copy = chain;
    while (chain_copy.n_chunks()) {
        Chunk chunk = chain_copy.front();
        if (chunk.layer() >= kMaxLayers) {
            return chain.begin();  // illegal layer
        }
        max_layer = std::max(chunk.layer(), max_layer);
        if (chunk.is_buf() && state.offsets[chunk.layer()]) {
            include_offsets[chunk.layer()] = true;
        }
        chain_copy = chain_copy.skip_chunks(1);
    }

    if (packet.size() < 1 + (max_layer + 1) + include_offsets.count()) {
        return chain.begin();  // packet too short for header
    }

    packet[0] = (uint8_t)((1 << (max_layer + 1)) - 1);
    packet = packet.skip(1);

    for (size_t i = 0; i <= max_layer; ++i) {
        // packet size checked before for-loop
        packet[0] = (state.frame_ids[i] << 1) | (include_offsets[i] ? 1 : 0);
        packet = packet.skip(1);
        if (include_offsets[i]) {
            if (state.offsets[i] & 0x80) {
                return chain.end();  // TODO: support offset rollover
            }
            packet[0] = (state.offsets[i] & 0x7f);
            packet = packet.skip(1);
        }
    }

    // Serialize chunks
    uint8_t layer = max_layer;

    uint8_t* length_field = nullptr;

    while (chain.n_chunks()) {
        Chunk chunk = chain.front();

        // Coalesce frame boundaries into previous chunk header if possible
        if (chunk.is_frame_boundary() && length_field &&
            chunk.layer() + (*length_field & 0x3) == layer &&
            layer - chunk.layer() <= 1) {
            *length_field =
                (*length_field & 0x7c) | (layer - chunk.layer() + 1);
            state.inc(chunk.layer());
            chain = chain.skip_chunks(1);
            continue;
        }

        if (chunk.layer() != layer) {
            if (packet.size() < 1) {
                return chain.begin();  // packet full
            }
            layer = chunk.layer();
            packet[0] = 0x80 | layer;  // insert layer marker
            packet = packet.skip(1);
        }

        if (chunk.is_buf()) {
            if (chunk.buf().size()) {
                if (packet.size() < 1) {
                    return chain.begin();  // packet full
                }
                length_field = packet.begin();
                packet = packet.skip(1);
                size_t n_copy;
                if (chunk.buf().size() >= packet.size()) {
                    *length_field = 0x1f << 2;
                    n_copy = packet.size();
                } else {
                    *length_field = std::min(chunk.buf().size(), (size_t)0x1eUL) << 2;
                    n_copy = std::min(chunk.buf().size(), (size_t)0x1eUL);
                }
                std::copy_n(chunk.buf().begin(), n_copy, packet.begin());
                packet = packet.skip(n_copy);
                chain = chain.skip_bytes(n_copy);
            } else {
                chain = chain.skip_chunks(1);
            }

        } else {
            if (packet.size() < 1) {
                return chain.begin();  // packet full
            }

            packet[0] = 1;  // close frame
            packet = packet.skip(1);
            // TODO: could pack two frame boundaries into one byte
            state.inc(chunk.layer());

            length_field = nullptr;
            chain = chain.skip_chunks(1);
        }
    }

    return chain.begin();
}

inline bool LowLevelProtocol::unpack(ReceiverState& state, cbufptr_t packet,
                              uint8_t* reset_layer, write_iterator it) {
    if (packet.size() < 1) {
        return false;
    }

    uint8_t flags = packet[0];
    packet = packet.skip(1);

    if (flags & 0x80) {
        return false;  // reserved bit set - discard packet
    }

    std::bitset<kMaxLayers> frame_ids_present = flags & 0x7f;

    uint8_t lowest_layer = find_first(frame_ids_present);

    uint8_t layer = 0;

    *reset_layer = 0xff;

    for (size_t i = 0; i < 7; ++i) {
        if (frame_ids_present[i]) {
            layer = i;
            if (i >= kMaxLayers) {
                return false;  // resource exhaustion
            }
            if (packet.size() < 1) {
                return false;  // malformed packet
            }

            bool has_offset = packet[0] & 1;

            uint8_t new_frame_id = (uint16_t)(packet[0] >> 1);
            packet = packet.skip(1);

            if (new_frame_id != state.frame_ids[i]) {
                if (i == lowest_layer) {
                    return true;  // insufficient information to resume
                }
                *reset_layer = std::min(*reset_layer, (uint8_t)i);
            }

            state.frame_ids[i] = new_frame_id;

            if (has_offset) {
                if (packet.size() < 1) {
                    return false;  // malformed packet
                }
                if (packet[0] & 0x80) {
                    return false;  // reserved bit set
                }
                state.offsets[i] = packet[0];  // TODO: unroll offset
                packet = packet.skip(1);
            }
        }
    }

    while (packet.size()) {
        if (packet[0] & 0x80) {
            if (packet[0] & 0x70) {
                // reserved bit set - discard this chunk and the rest of the
                // packet (don't know where the next chunk starts)
                return true;
            }
            layer = packet[0] & 0xf;
            packet = packet.skip(1);
        } else {
            uint8_t n_close = packet[0] & 0x03;
            uint8_t size = (packet[0] >> 2) & 0x1f;
            packet = packet.skip(1);

            if (size == 0x1f) {
                size = packet.size();
            } else if (size > packet.size()) {
                return false;  // malformed packet
            }

            if (n_close > layer + 1) {
                return false;  // malformed packet
            }

            if (size) {
                if (!it.has_free_space()) {
                    return true;  // out of memory - ignore the remaining packet
                }
                it = Chunk(layer, packet.take(size));
                packet = packet.skip(size);
            }

            for (size_t i = 0; i < n_close; ++i) {
                if (!it.has_free_space()) {
                    return true;  // out of memory - ignore the remaining packet
                }
                it = Chunk::frame_boundary(layer - i);
                state.inc(layer - i);
            }
        }
    }

    return true;  // packet fully processed
}

}  // namespace fibre

#endif  // __FIBRE_LOW_LEVEL_PROTOCOL
