#ifndef __FIBRE_PRINT_UTILS_HPP
#define __FIBRE_PRINT_UTILS_HPP

#include <iostream>
#include <vector>
#include <unordered_map>
#include "cpp_utils.hpp"

namespace std {

template<typename ... Ts>
static std::ostream& operator<<(std::ostream& stream, std::tuple<Ts...> val) {
    int dummy[sizeof...(Ts)] = { (stream << std::get<0>(val), 0) }; // TODO: print all values
    (void) dummy;
    return stream;
}

template<typename T>
static std::ostream& operator<<(std::ostream& stream, std::vector<T> val) {
    for (size_t i = 0; i < val.size(); ++i) {
        stream << val[i];
        if (i + 1 < val.size())
            stream << ", ";
    }
    return stream;
}

template<typename TKey, typename TVal>
static std::ostream& operator<<(std::ostream& stream, std::unordered_map<TKey, TVal> val) {
    stream << "{";
    for (auto it = val.begin(); it != val.end(); ++it) {
        if (it != val.begin())
            stream << ", ";
        stream << it->first << ": " << it->second;
    }
    return stream << "}";
}

struct print_functor {
    template<typename T>
    void operator()(T& val, std::ostream& stream) {
        stream << val;
    }
};


template<typename ... Ts>
static std::ostream& operator<<(std::ostream& stream, const std::variant<Ts...>& val) {
    stream << "[var " << val.index() << ": ";
    val.invoke(print_functor(), stream);
    //return val.index() ? (stream << std::get<0>(val)) : (stream << )
    //stream << "[variant " << val.index() << "]"; // TODO: print value
    return stream << "]";
}
}
//static std::ostream& operator<<(std::ostream& stream, std::variant<> val) {
//    stream << "malformed variant"; // TODO: print value
//    return stream;
//}


#endif // __FIBRE_PRINT_UTILS_HPP