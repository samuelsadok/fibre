#ifndef __LEGACY_OBJECT_SERVER_HPP
#define __LEGACY_OBJECT_SERVER_HPP

#include <stdint.h>
#include <stdlib.h>
#include <fibre/bufptr.hpp>

namespace fibre {

class Domain;

struct LegacyObjectServer {
    uint8_t rx_buf_[128];
    size_t rx_pos_;
    uint8_t tx_buf_[128];
    size_t tx_pos_;
    size_t expected_ep_ = 0; // 0 while no call in progress
    size_t trigger_ep_;
    size_t n_inputs_;
    size_t n_outputs_;
    size_t output_size_;

    uint8_t call_state_[256];

    void reset() {
        rx_pos_ = 0;
        tx_pos_ = 0;
        expected_ep_ = 0;
        trigger_ep_ = 0;
        n_inputs_ = 0;
        n_outputs_ = 0;
        output_size_ = 0;
    }

    bool endpoint_handler(Domain* domain, int idx, cbufptr_t* input_buffer, bufptr_t* output_buffer);
};

extern const uint16_t json_crc_;
extern const uint32_t json_version_id_;

}

#endif // __LEGACY_OBJECT_SERVER_HPP
