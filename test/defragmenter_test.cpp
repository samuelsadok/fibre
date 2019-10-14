

#include <fibre/input.hpp>
#include "test_utils.hpp"

using namespace fibre;

TestContext try_append(Defragmenter* defragmenter, const char * buf, size_t offset, size_t expected_processed_bytes) {
    TestContext context;

    cbufptr_t buffer = {(const uint8_t*)buf, strlen(buf)};
    defragmenter->process_chunk(buffer, offset);
    TEST_EQUAL((size_t)(buffer.ptr - (const uint8_t*)buf), expected_processed_bytes);

    return context;
}

TestContext try_get(StreamSource* defragmenter, const char * expected) {
    TestContext context;

    uint8_t buf[strlen(expected)];
    size_t size = 0;

    TEST_EQUAL(defragmenter->get_bytes_({buf, sizeof(buf)}, &size), StreamSource::OK);
    TEST_EQUAL(size, sizeof(buf));
    
    std::string received_str{(char*)buf, sizeof(buf)};
    std::string expected_str{expected};
    TEST_EQUAL(received_str, expected_str);

    return context;
}

TestContext try_append(StreamSink* fragmenter, const char * buf, size_t expected_processed_bytes) {
    TestContext context;

    cbufptr_t buffer = {(const uint8_t*)buf, strlen(buf)};
    fragmenter->process_bytes(buffer);
    TEST_EQUAL((size_t)(buffer.ptr - (const uint8_t*)buf), expected_processed_bytes);

    return context;
}

TestContext try_get(Fragmenter* fragmenter, const char * expected, size_t expected_offset) {
    TestContext context;

    cbufptr_t buf = {nullptr, strlen(expected) + 1};
    size_t offset;
    fragmenter->get_chunk(buf, &offset);
    TEST_EQUAL(buf.length, strlen(expected));
    TEST_EQUAL(offset, expected_offset);
    
    std::string received_str{(char*)buf.ptr, buf.length};
    std::string expected_str{expected};
    TEST_EQUAL(received_str, expected_str);

    fragmenter->acknowledge_chunk(offset, buf.length);

    return context;
}

int main(int argc, const char** argv) {
    TestContext context;

    FixedBufferDefragmenter<10> defragmenter;

    TEST_ADD(try_get(&defragmenter, ""));

    // completely new chunk
    TEST_ADD(try_append(&defragmenter, "12", 0, 2));
    TEST_ADD(try_get(&defragmenter, "12"));

    // another completely new chunk
    TEST_ADD(try_append(&defragmenter, "345", 2, 3));
    TEST_ADD(try_get(&defragmenter, "345"));

    // partially new chunk
    TEST_ADD(try_append(&defragmenter, "4567", 3, 4));
    TEST_ADD(try_get(&defragmenter, "67"));

    // completely old (known) chunk
    TEST_ADD(try_append(&defragmenter, "67", 5, 2));
    TEST_ADD(try_get(&defragmenter, ""));

    // completely new chunk, crossing the internal buffer size
    TEST_ADD(try_append(&defragmenter, "89abc", 7, 5));
    TEST_ADD(try_get(&defragmenter, "89a"));
    TEST_ADD(try_get(&defragmenter, "bc"));

    TEST_ADD(try_append(&defragmenter, "There was no ice cream in the freezer, nor did they have money to go to the store.", 0, 22));
    TEST_ADD(try_get(&defragmenter, " ice cre"));
    TEST_ADD(try_get(&defragmenter, "am"));


    FixedBufferFragmenter<10> fragmenter;

    TEST_ADD(try_get(&fragmenter, "", 0));

    // completely new chunk
    TEST_ADD(try_append(&fragmenter, "12", 2));
    TEST_ADD(try_get(&fragmenter, "12", 0));
    TEST_ADD(try_get(&fragmenter, "", 2));

    // another completely new chunk
    TEST_ADD(try_append(&fragmenter, "345", 3));
    TEST_ADD(try_get(&fragmenter, "345", 2));
    TEST_ADD(try_get(&fragmenter, "", 5));

    // TODO: stress test fragmenter and defragmenter in combination

    return context.summarize();
}
