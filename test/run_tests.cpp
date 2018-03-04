
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>

//#define DEBUG_PROTOCOL
void hexdump(const uint8_t* buf, size_t len);

#include <fibre/crc.hpp>
#include <fibre/decoders.hpp>
//#include <fibre/encoders.hpp>

void hexdump(const uint8_t* buf, size_t len) {
    for (size_t pos = 0; pos < len; ++pos) {
        printf(" %02x", buf[pos]);
        if ((((pos + 1) % 16) == 0) || ((pos + 1) == len))
            printf("\r\n");
        //osDelay(2);
    }
}



bool varint_decoder_test() {
    struct test_case_t {
        uint8_t raw_data[10];
        size_t length;
        uint32_t ground_truth;
    };
    const test_case_t test_cases[] = {
        // raw_data, length, expected output
        { { 0x00 }, 1, 0 },
        { { 0x01 }, 1, 1 },
        { { 0xff, 0x01 }, 2, 0xff },
        { { 0xAC, 0x02 }, 2, 300 },
        { { 0xff, 0xff, 0xff, 0xff, 0xf }, 5, 0xffffffff }
    };

    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); ++i) {
        uint32_t result;
        VarintStreamDecoder<uint32_t> decoder = make_varint_decoder(result);
        size_t processed_bytes = 0;
        int status = decoder.process_bytes(test_cases[i].raw_data, test_cases[i].length, &processed_bytes);
        if (status) {
            return false;
        } else if (result != test_cases[i].ground_truth) {
            printf("test %zu: expected %u but got %u\n", i, test_cases[i].ground_truth, result);
            return false;
        } else if (processed_bytes != test_cases[i].length) {
            printf("test %zu: expected to process %zu bytes but processed %zu bytes\n", i, test_cases[i].length, processed_bytes);
            return false;
        }
    }
    return true;
}



int main(void) {
    /* Decoder demo (remove or move somewhere else) */
    // prepare raw data
    uint8_t raw_data[] = { 0xBC, 0x03, 0xAC, 0xff, 0x02, 0x00, 0x00, 0xff };
    raw_data[3] = calc_crc8<CANONICAL_CRC8_POLYNOMIAL>(CANONICAL_CRC8_INIT, raw_data, 3);
    raw_data[7] = calc_crc8<CANONICAL_CRC8_POLYNOMIAL>(raw_data[3], raw_data + 4, 3);

    // instantiate decoder
    ReceiverState state;
    auto decoder = make_crc8_decoder<CANONICAL_CRC8_INIT, CANONICAL_CRC8_POLYNOMIAL>(
        make_decoder_chain(
            make_length_decoder(state),
            make_endpoint_id_decoder(state)
        )
    );

    // push the raw data through the decoder
    size_t processed_bytes = 0;
    int status = decoder.process_bytes(raw_data, sizeof(raw_data), &processed_bytes);
    
    // expected result: "length: 444, endpoint-id: 300, processed 8 bytes"
    if (status == 0)
        printf("length: %zu, endpoint-id: %zu, processed %zu bytes\n", state.length, state.endpoint_id, processed_bytes);
    else
        printf("decoder demo failed\n");



    /* run automated test */
    bool test_result = varint_decoder_test();
    if (test_result) {
        printf("all tests passed\n");
        return 0;
    } else {
        printf("some tests failed\n");
        return -1;
    }
}
