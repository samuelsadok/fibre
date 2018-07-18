
#include <fibre/fibre.hpp>

#include <algorithm>

using namespace fibre;

std::pair<InputPipe*, OutputPipe*> RemoteNode::get_pipe_pair(size_t id, bool server_pool, bool* is_new) {
    std::unordered_map<size_t, std::pair<InputPipe, OutputPipe>>& pipe_pool =
        server_pool ? server_pipe_pairs_ : client_pipe_pairs_;
    // TODO: limit number of concurrent pipes
    auto emplace_result = pipe_pool.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(id >> 1),
        std::forward_as_tuple(
            std::piecewise_construct,
            std::forward_as_tuple(this, id >> 1, server_pool),
            std::forward_as_tuple(this, id >> 1, server_pool)
            ));
    std::pair<InputPipe, OutputPipe>& pipes = emplace_result.first->second;
    if (is_new)
        *is_new = emplace_result.second;
    return std::make_pair(&pipes.first, &pipes.second);
}

void RemoteNode::add_output_channel(OutputChannel* channel) {
    output_channels_.push_back(channel);
}

void RemoteNode::remove_output_channel(OutputChannel* channel) {
    output_channels_.erase(std::remove(output_channels_.begin(), output_channels_.end(), channel), output_channels_.end());
}

/**
 * @brief Notifies the scheduler that there is data in at least one output
 * pipe.
 */
void RemoteNode::notify_output_pipe_ready() {
#if CONFIG_SCHEDULER_MODE == SCHEDULER_MODE_GLOBAL_THREAD
    global_state.output_pipe_ready.set();
#elif CONFIG_SCHEDULER_MODE == SCHEDULER_MODE_PER_NODE_THREAD
    output_pipe_ready_.set();
#endif
}

/**
 * @brief Notifies the scheduler that at least one output channel can
 * consume data without blocking.
 */
void RemoteNode::notify_output_channel_ready() {
#if CONFIG_SCHEDULER_MODE == SCHEDULER_MODE_GLOBAL_THREAD
    global_state.output_channel_ready.set();
#elif CONFIG_SCHEDULER_MODE == SCHEDULER_MODE_PER_NODE_THREAD
    output_channel_ready_.set();
#endif
}

void RemoteNode::schedule() {
    constexpr size_t per_packet_overhead = 16 + 2;
    constexpr size_t per_chunk_overhead = 8;
    
    LOG_FIBRE(OUTPUT, "schedule for remote node ", uuid_);

    for (OutputChannel* channel : output_channels_) {
        size_t free_space = channel->get_min_non_blocking_bytes();
        if (free_space < per_packet_overhead) {
            LOG_FIBRE(OUTPUT, "channel ", (*channel), " is busy");
            continue;
        }
        LOG_FIBRE(OUTPUT, "channel ", (*channel), " has ", free_space, " free bytes");

        LOG_FIBRE(GENERAL, "array at ", as_hex(reinterpret_cast<uintptr_t>(&server_pipe_pairs_)));
        using pipe_entry_t = std::pair<const size_t, std::pair<InputPipe, OutputPipe>>;
        for (pipe_entry_t& pipe_entry : server_pipe_pairs_) { // TODO: include client
            OutputPipe& pipe = pipe_entry.second.second;
            LOG_FIBRE(OUTPUT, "looking at pipe ", pipe.get_id());

            monotonic_time_t due_time = pipe.get_due_time();
            if (due_time > now())
                continue;

            for (OutputPipe::chunk_t chunk : pipe.get_pending_chunks()) {
                size_t max_chunk_len = channel->get_min_non_blocking_bytes();
                if (max_chunk_len < per_chunk_overhead)
                    break;
                max_chunk_len = max_chunk_len != SIZE_MAX ? max_chunk_len - per_chunk_overhead : max_chunk_len;

                // send chunk header
                size_t offset = 0, length = 0;
                uint16_t crc_init = 0;
                if (!chunk.get_properties(&offset, &length, &crc_init)) {
                    LOG_FIBRE_W(OUTPUT, "get_properties failed");
                }
                length = std::min(length, max_chunk_len);
                uint8_t buffer[8];
                write_le<uint16_t>(pipe.get_id(), buffer, 2);
                write_le<uint16_t>(offset, buffer + 2, 2);
                write_le<uint16_t>(crc_init, buffer + 4, 2);
                write_le<uint16_t>(length, buffer + 6, 2);
                LOG_FIBRE(OUTPUT, "emitting pipe id ", pipe.get_id(), " chunk ", offset, " - ", offset + length - 1, ", crc ", as_hex(crc_init));
                size_t processed_bytes = 0;
                if (channel->process_bytes(buffer, sizeof(buffer), &processed_bytes) != StreamSink::OK) {
                    LOG_FIBRE_W(OUTPUT, "channel failed");
                    // TODO: remove channel
                    break;
                }
                if (processed_bytes != sizeof(buffer)) {
                    LOG_FIBRE_W(OUTPUT, "expected to process ", sizeof(buffer), " bytes but only processed ", processed_bytes, " bytes");
                    break;
                }

                // send chunk paylaod
                if (!chunk.write_to(channel, length)) {
                    LOG_FIBRE_W(OUTPUT, "the chunk could not be fully sent - get_min_non_blocking_bytes lied to us.");
                    break;
                }

                if (!pipe.guaranteed_delivery) {
                    pipe.drop_chunk(offset, length);
                } else {
                    monotonic_time_t next_due_time = due_time + channel->resend_interval;
                    due_time = std::max(next_due_time, now());
                    pipe.set_due_time(offset, length, due_time);
                }
            }

            auto chunks = pipe.get_pending_chunks();
            if (chunks.begin() < chunks.end())
                notify_output_pipe_ready();
        }
    }
}
