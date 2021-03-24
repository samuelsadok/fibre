/*
see protocol.md for the protocol specification
*/

#ifndef __PROTOCOL_HPP
#define __PROTOCOL_HPP

#include <functional>
#include <limits>
#include <cmath>
//#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <cstring>
#include <fibre/cpp_utils.hpp>
#include <fibre/bufptr.hpp>
#include <fibre/simple_serdes.hpp>


typedef struct {
    uint16_t json_crc;
    uint16_t endpoint_id;
} endpoint_ref_t;



/* ToString / FromString functions -------------------------------------------*/
/*
* These functions are currently not used by Fibre and only here to
* support the ODrive ASCII protocol.
* TODO: find a general way for client code to augment endpoints with custom
* functions
*/

template<typename T>
struct format_traits_t;

// template<> struct format_traits_t<float> { using type = void;
//     static constexpr const char * fmt = "%f";
//     static constexpr const char * fmtp = "%f";
// };
template<> struct format_traits_t<long long> { using type = void;
    static constexpr const char * fmt = "%lld";
    static constexpr const char * fmtp = "%lld";
};
template<> struct format_traits_t<unsigned long long> { using type = void;
    static constexpr const char * fmt = "%llu";
    static constexpr const char * fmtp = "%llu";
};
template<> struct format_traits_t<long> { using type = void;
    static constexpr const char * fmt = "%ld";
    static constexpr const char * fmtp = "%ld";
};
template<> struct format_traits_t<unsigned long> { using type = void;
    static constexpr const char * fmt = "%lu";
    static constexpr const char * fmtp = "%lu";
};
template<> struct format_traits_t<int> { using type = void;
    static constexpr const char * fmt = "%d";
    static constexpr const char * fmtp = "%d";
};
template<> struct format_traits_t<unsigned int> { using type = void;
    static constexpr const char * fmt = "%ud";
    static constexpr const char * fmtp = "%ud";
};
template<> struct format_traits_t<short> { using type = void;
    static constexpr const char * fmt = "%hd";
    static constexpr const char * fmtp = "%hd";
};
template<> struct format_traits_t<unsigned short> { using type = void;
    static constexpr const char * fmt = "%hu";
    static constexpr const char * fmtp = "%hu";
};
template<> struct format_traits_t<char> { using type = void;
    static constexpr const char * fmt = "%hhd";
    static constexpr const char * fmtp = "%d";
};
template<> struct format_traits_t<unsigned char> { using type = void;
    static constexpr const char * fmt = "%hhu";
    static constexpr const char * fmtp = "%u";
};

template<typename T, typename = typename format_traits_t<T>::type>
static bool to_string(const T& value, char * buffer, size_t length, int) {
    snprintf(buffer, length, format_traits_t<T>::fmtp, value);
    return true;
}
// Special case for float because printf promotes float to double, and we get warnings
template<typename T = float>
static bool to_string(const float& value, char * buffer, size_t length, int) {
    snprintf(buffer, length, "%f", (double)value);
    return true;
}
template<typename T = bool>
static bool to_string(const bool& value, char * buffer, size_t length, int) {
    buffer[0] = value ? '1' : '0';
    buffer[1] = 0;
    return true;
}
template<typename T>
static bool to_string(const T& value, char * buffer, size_t length, ...) {
    return false;
}

template<typename T, typename = typename format_traits_t<T>::type>
static bool from_string(const char * buffer, size_t length, T* property, int) {
    // Note for T == uint8_t: Even though we supposedly use the correct format
    // string sscanf treats our pointer as pointer-to-int instead of
    // pointer-to-uint8_t. To avoid an unexpected memory access we first read
    // into a union.
    union { T t; int i; } val;
    if (sscanf(buffer, format_traits_t<T>::fmt, &val.t) == 1) {
        *property = val.t;
        return true;
    } else {
        return false;
    }
}
// Special case for float because printf promotes float to double, and we get warnings
template<typename T = float>
static bool from_string(const char * buffer, size_t length, float* property, int) {
    return sscanf(buffer, "%f", property) == 1;
}
template<typename T = bool>
static bool from_string(const char * buffer, size_t length, bool* property, int) {
    int val;
    if (sscanf(buffer, "%d", &val) != 1)
        return false;
    *property = val;
    return true;
}
template<typename T>
static bool from_string(const char * buffer, size_t length, T* property, ...) {
    return false;
}


//template<typename T, typename = typename std>
//bool set_from_float_ex(float value, T* property) {
//    return false;
//}

namespace conversion {
//template<typename T>
template<typename T>
bool set_from_float_ex(float value, float* property, int) {
    return *property = value, true;
}
template<typename T>
bool set_from_float_ex(float value, bool* property, int) {
    return *property = (value >= 0.0f), true;
}
template<typename T, typename = std::enable_if_t<std::is_integral<T>::value && !std::is_const<T>::value>>
bool set_from_float_ex(float value, T* property, int) {
    return *property = static_cast<T>(std::round(value)), true;
}
template<typename T>
bool set_from_float_ex(float value, T* property, ...) {
    return false;
}
template<typename T>
bool set_from_float(float value, T* property) {
    return set_from_float_ex<T>(value, property, 0);
}
}



#endif
