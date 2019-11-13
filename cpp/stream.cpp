
#include <fibre/stream.hpp>

USE_LOG_TOPIC(STREAM);

namespace fibre {
stream_copy_result_t stream_copy(StreamSink* dst, StreamSource* src) {
    const size_t max_copy_size = 1; // TODO: customize based on stack size heuristic
    uint8_t buf[max_copy_size];

    stream_copy_result_t status;

    bufptr_t bufptr = { .ptr = buf, .length = max_copy_size };
    status.src_status = src->get_bytes(bufptr);
    cbufptr_t cbufptr = { .ptr = buf, .length = max_copy_size - bufptr.length };
    status.dst_status = dst->process_bytes(cbufptr);

    //if (max_copy_size <= bufptr.length) {
    //    FIBRE_LOG(E) << "no progress";
    //    return kStreamError;
    //}

    if (cbufptr.length > bufptr.length) {
        FIBRE_LOG(E) << "not all bytes processed";
        return {kStreamError, kStreamError};
    }
    return status;
}

stream_copy_result_t stream_copy(StreamSink* dst, OpenStreamSource* src) {
    stream_copy_result_t status;
    
    cbufptr_t buf = { .ptr = nullptr, .length = SIZE_MAX };;
    if (src->get_buffer(&buf) != kStreamOk) {
        return {kStreamError, kStreamError};
    }
    FIBRE_LOG(D) << "got " << buf.length << " bytes at " << (void*)buf.ptr << " from source";
    size_t old_length = buf.length;
    status.dst_status = dst->process_bytes(buf);
    status.src_status = src->consume(old_length - buf.length);

    if ((status.src_status == kStreamOk) && (status.dst_status == kStreamOk) && (old_length <= buf.length)) {
        FIBRE_LOG(E) << "no progress: buf len went from " << old_length << " to " << buf.length;
        return {kStreamError, kStreamError};
    }
    return status;
}
}
