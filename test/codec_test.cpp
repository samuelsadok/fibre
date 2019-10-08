

#include <fibre/decoders.hpp>
#include <fibre/print_utils.hpp>
#include "test_utils.hpp"

#include <stdlib.h>

using namespace fibre;

struct test_case_t {
    uint8_t encoded[10];
    size_t length;
    uint32_t decoded;
};

template<typename TEncoder, typename TDecoder, typename TVal>
TestContext codec_test(const uint8_t (&encoded)[], size_t length, TVal decoded) {
    TestContext context;

    static_assert(std::is_base_of<typename fibre::Decoder<TVal>, TDecoder>::value, "TDecoder must inherit Decoder<TVal>");

    // Feed the decoder byte by byte

    TDecoder decoder;
    size_t processed_bytes = 0;

    TEST_EQUAL(decoder.process_bytes(nullptr, 0, &processed_bytes), StreamSink::OK);
    for (size_t i = 0; i < length - 1; ++i) {
        TEST_EQUAL(decoder.process_bytes(encoded + i, 1, &processed_bytes), StreamSink::OK);
        TEST_EQUAL(decoder.process_bytes(encoded + i, 0, &processed_bytes), StreamSink::OK);
        TEST_EQUAL(processed_bytes, i + 1);
        TEST_ZERO(decoder.get());
    }
    TEST_EQUAL(decoder.process_bytes(encoded + length - 1, 1, &processed_bytes), StreamSink::CLOSED);
    TEST_EQUAL(processed_bytes, length);
    TEST_EQUAL(decoder.process_bytes(encoded + length - 1, 0, &processed_bytes), StreamSink::CLOSED);
    TEST_EQUAL(decoder.process_bytes(encoded + length - 1, 1, &processed_bytes), StreamSink::CLOSED);
    TEST_EQUAL(processed_bytes, length);

    TEST_NOT_NULL(decoder.get());
    TEST_EQUAL(*decoder.get(), decoded);


    // Feed the decoder in largest possible chunks. Feed it 1 more byte than
    // required to try to confuse it.

    StreamSink::status_t status = StreamSink::CLOSED;
    decoder = TDecoder{};
    processed_bytes = 0;
    uint8_t encoded_longer[length + 1];
    memcpy(encoded_longer, encoded, length);
    while (processed_bytes < length) {
        TEST_ZERO(decoder.get());
        size_t prev_processed_bytes = processed_bytes;
        status = decoder.process_bytes(encoded_longer + processed_bytes, length + 1 - processed_bytes, &processed_bytes);
        if (status == StreamSink::OK) {
            TEST_ASSERT(prev_processed_bytes > processed_bytes);
            TEST_ASSERT(processed_bytes < length);
        } else {
            TEST_EQUAL(status, StreamSink::CLOSED);
            TEST_EQUAL(processed_bytes, length);
        }
    }

    TEST_NOT_NULL(decoder.get());
    TEST_EQUAL(*decoder.get(), decoded);


    // TODO: test encoder
/*
    VarintEncoder<uint32_t> encoder = make_varint_encoder(test_case.decoded);
    uint8_t buffer[10];
    size_t generated_bytes = 0;
    status = encoder.get_bytes(buffer, sizeof(buffer), &generated_bytes);
    if (status) {
        return false;
    } else if ((generated_bytes != test_case.length) 
            || memcmp(buffer, test_case.encoded, test_case.length)) {
        printf("test %zu: expected:", i);
        hexdump(test_case.encoded, test_case.length);
        printf("got: ");
        hexdump(buffer, generated_bytes);
        return false;
    }
*/
    
    return context;
}

TestContext varint_codec_test(const uint8_t (&encoded)[], size_t length, uint32_t decoded) {
    return codec_test<void, VarintDecoder<uint32_t>>(encoded, length, decoded);
}

TestContext fixedint_le_codec_test(const uint8_t (&encoded)[], size_t length, uint32_t decoded) {
    return codec_test<void, FixedIntDecoder<uint32_t, false>>(encoded, length, decoded);
}

TestContext fixedint_be_codec_test(const uint8_t (&encoded)[], size_t length, uint32_t decoded) {
    return codec_test<void, FixedIntDecoder<uint32_t, true>>(encoded, length, decoded);
}


TestContext utf8_codec_test(const uint8_t (&encoded)[], size_t length, std::string decoded) {
    constexpr size_t max_size = 128;
    std::tuple<std::array<char, max_size>, size_t> decoded2;
    memcpy(std::get<0>(decoded2).data(), decoded.c_str(), std::min(max_size, decoded.size()));
    std::get<1>(decoded2) = decoded.size();
    return codec_test<void, UTF8Decoder<char, max_size>>(encoded, length, decoded2);
}

int main(int argc, const char** argv) {
    TestContext context;

    test_case_t test_case; // encoded, length, decoded

    TEST_ADD(varint_codec_test({0x00}, 1, 0));
    
    TEST_ADD(varint_codec_test({0x01}, 1, 1));
    TEST_ADD(varint_codec_test({0xff, 0x01}, 2, 0xff));
    TEST_ADD(varint_codec_test({0xAC, 0x02}, 2, 300));
    TEST_ADD(varint_codec_test({0xff, 0xff, 0xff, 0xff, 0xf}, 5, 0xffffffff));

    TEST_ADD(fixedint_le_codec_test({0x00, 0x00, 0x00, 0x00}, 4, 0));
    TEST_ADD(fixedint_le_codec_test({0x12, 0x34, 0x56, 0x78}, 4, 0x78563412));
    TEST_ADD(fixedint_le_codec_test({0xFF, 0xFF, 0xFF, 0xFF}, 4, 0xFFFFFFFF));

    TEST_ADD(fixedint_be_codec_test({0x00, 0x00, 0x00, 0x00}, 4, 0));
    TEST_ADD(fixedint_be_codec_test({0x12, 0x34, 0x56, 0x78}, 4, 0x12345678));
    TEST_ADD(fixedint_be_codec_test({0xFF, 0xFF, 0xFF, 0xFF}, 4, 0xFFFFFFFF));
    TEST_ADD(fixedint_be_codec_test({0xFF, 0xFF, 0xFF, 0xFF}, 4, 0xFFFFFFFF));

    TEST_ADD(utf8_codec_test({0x03, 0x61, 0x62, 0x63}, 4, "abc"));

    return context.summarize();
}
