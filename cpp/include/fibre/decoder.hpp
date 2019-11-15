#ifndef __FIBRE_DECODER_HPP
#define __FIBRE_DECODER_HPP

#include "stream.hpp"

namespace fibre {

/**
 * @brief Represents a stream sink that decodes an object of type TVal.
 * The stream sink shall close itself once the object is fully decoded.
 */
template<typename TVal>
class Decoder : public StreamSink /* TODO: not clear if internal or external buffer is better. Should provide both. */ {
public:
    /**
     * @brief Shall return a pointer to the decoded value.
     * This value must remain valid for the rest of this object's lifetime.
     * Shall return null if the value is not yet fully available.
     */
    virtual const TVal* get() = 0;
};

}

#endif // __FIBRE_DECODER_HPP
