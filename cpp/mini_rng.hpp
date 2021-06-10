#ifndef __MINI_RNG_HPP
#define __MINI_RNG_HPP

#include <stdint.h>
#include <fibre/bufptr.hpp>

namespace fibre {

// Source:
// https://www.electro-tech-online.com/threads/ultra-fast-pseudorandom-number-generator-for-8-bit.124249/
class MiniRng {
public:
    void seed(uint8_t s0, uint8_t s1, uint8_t s2, uint8_t s3) {
        x ^= s0;
        a ^= s1;
        b ^= s2;
        c ^= s3;
        next();
    }

    uint8_t next() {
        x++;
        a = (a ^ c ^ x);
        b = (b + a);
        c = ((c + (b >> 1)) ^ a);
        return c;
    }

    void get_random(bufptr_t buf) {
        for (; buf.size(); buf = buf.skip(1)) {
            *buf.begin() = next();
        }
    }

private:
    uint8_t a = 0, b = 0, c = 0, x = 0;
};

}  // namespace fibre

#endif  // __MINI_RNG_HPP
