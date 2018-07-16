
#include <fibre/fibre.hpp>

using namespace fibre;

OutputPipe::status_t OutputPipe::process_bytes(const uint8_t* buffer, size_t length, size_t *processed_bytes) {
    LOG_FIBRE(OUTPUT, "processing ", length, " bytes");
    size_t chunk = std::min(length, sizeof(buffer_) - buffer_pos_);
    memcpy(buffer_ + buffer_pos_, buffer, chunk);
    buffer_pos_ += chunk;
    if (processed_bytes)
        *processed_bytes += chunk;
    remote_node_->notify_output_pipe_ready();
    return chunk < length ? BUSY : OK;
}
