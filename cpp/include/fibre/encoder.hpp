#ifndef __FIBRE_ENCODER_HPP
#define __FIBRE_ENCODER_HPP

#include "stream.hpp"

namespace fibre {

/**
 * @brief Represents a stream source that encodes an object of type TVal.
 * The stream source shall close itself once the object is fully encoded.
 */
template<typename TVal>
class Encoder : public StreamSource {
public:
    /**
     * @brief Configures the encoder with the value pointed to by `val`.
     * This value must remain valid until the value is fully encoded.
     * The stream shall be closed prior to being configured.
     * If `val` is NULL, the encoder shall close itself immediately.
     */
    virtual void set(TVal* val) = 0;
};

}

#endif // __FIBRE_ENCODER_HPP
