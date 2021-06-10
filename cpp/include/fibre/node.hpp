#ifndef __FIBRE_NODE_HPP
#define __FIBRE_NODE_HPP

#include <fibre/base_types.hpp>
#include <fibre/pool.hpp>
#include <array>

namespace fibre {

struct FrameStreamSink;

struct Node {
    NodeId id;

    // TODO: configurable capacity
    Pool<FrameStreamSink*, 3> sinks;
};

}

#endif // __FIBRE_NODE_HPP
