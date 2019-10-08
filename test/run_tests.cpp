// DEPRECATED
// TODO: absorb into other tests

#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>


#include <fibre/fibre.hpp>


int main(void) {
    /***** Decoder demo (remove or move somewhere else) *****/
    printf("Running decoder... ");
    // prepare raw data
    uint8_t raw_data[] = { 0xBC, 0x03, 0xAC, 0x5e, 0x02, 0x00, 0x00, 0xd1 };
    //raw_data[3] = calc_crc8<CANONICAL_CRC8_POLYNOMIAL>(CANONICAL_CRC8_INIT, raw_data, 3);
    //raw_data[7] = calc_crc8<CANONICAL_CRC8_POLYNOMIAL>(raw_data[3], raw_data + 4, 3);

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

    
    /***** Encoder demo (remove or move somewhere else) *****/    
    printf("Running encoder... ");
    // prepare request
    Request request = {
        .endpoint_id = 300,
        .length = 444,
    };

    // construct encoder for the request
    auto e2 = make_crc8_encoder<CANONICAL_CRC8_INIT, CANONICAL_CRC8_POLYNOMIAL>(
        make_encoder_chain(
            make_length_encoder(request),
            make_endpoint_id_encoder(request)
        )
    );

    // pull raw data out of the encoder
    uint8_t buffer[20];
    size_t generated_bytes = 0;
    status = e2.get_bytes(buffer, sizeof(buffer), &generated_bytes);
    if (status == 0) {
        printf("generated %zu bytes:\n", generated_bytes);
        hexdump(buffer, generated_bytes);
    } else {
        printf("encoder demo failed\n");
    }
}
