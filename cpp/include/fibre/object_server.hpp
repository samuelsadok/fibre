#ifndef __FIBRE_OBJECT_SERVER_HPP
#define __FIBRE_OBJECT_SERVER_HPP

#include <fibre/base_types.hpp>
#include "../../static_exports.hpp"
#include "../../codecs.hpp"
#include <algorithm>

namespace fibre {

template<typename T>
constexpr ServerObjectDefinition make_obj(T* obj) {
    return {obj, get_interface_id<T>()};
}

template<typename T>
constexpr ServerObjectDefinition make_obj(const T* obj) {
    return {const_cast<T*>(obj), get_interface_id<const T>()};
}


}

#endif // __FIBRE_OBJECT_SERVER_HPP
