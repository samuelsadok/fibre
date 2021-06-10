#ifndef __FIBRE_BASE_TYPES_HPP
#define __FIBRE_BASE_TYPES_HPP

#include <fibre/config.hpp>
#include <stdint.h>
#include <array>

namespace fibre {

#if FIBRE_ENABLE_SERVER
typedef uint8_t ServerFunctionId;
typedef uint8_t ServerInterfaceId;

// TODO: Use pointer instead? The codec that decodes the object still needs a table
// to prevent arbitrary memory access.
typedef uint8_t ServerObjectId;

struct ServerObjectDefinition {
    void* ptr;
    ServerInterfaceId interface; // TODO: use pointer instead of index? Faster but needs more memory
};
#endif

using NodeId = std::array<unsigned char, 16>;

}

#endif // __FIBRE_BASE_TYPES_HPP
