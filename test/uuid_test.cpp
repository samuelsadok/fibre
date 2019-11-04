
#include <fibre/uuid.hpp>
#include <fibre/print_utils.hpp>
#include "test_utils.hpp"

#include <stdlib.h>

using namespace fibre;

TestContext uuid_test(Uuid uuid, const unsigned char (&data)[16], std::string str) {
    TestContext context;
    std::array<unsigned char, 16> data_arr;
    std::copy(std::begin(data), std::end(data), data_arr.begin());
    TEST_EQUAL(uuid.get_bytes(), data_arr);
    TEST_EQUAL(uuid.to_string(), str);
    return context;    
}

int main(int argc, const char** argv) {
    TestContext context;

    const char str1[] = "01234567-89ab-cdef-0f1e-2d3c4b5a6978";
    TEST_ADD(uuid_test({str1}, {
            0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
            0x0f, 0x1e, 0x2d, 0x3c, 0x4b, 0x5a, 0x69, 0x78}, str1));

    const char str2[] = "00000000-0000-0000-0000-000000000000";
    TEST_ADD(uuid_test({str2}, {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, str2));

    const char str3[] = "ffffffff-ffff-ffff-ffff-ffffffffffff";
    TEST_ADD(uuid_test({str3}, {
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, str3));

    return context.summarize();
}
