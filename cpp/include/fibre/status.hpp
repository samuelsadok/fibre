#ifndef __FIBRE_STATUS_HPP
#define __FIBRE_STATUS_HPP

namespace fibre {

enum Status {
    kFibreOk,
    kFibreBusy, //<! The request will complete asynchronously
    kFibreCancelled, //!< The operation was cancelled due to a request by the application or the remote peer
    kFibreClosed, //!< The operation has finished orderly or shall be finished orderly
    kFibreInvalidArgument, //!< Bug in the application
    kFibreInternalError, //!< Bug in the local fibre implementation
    kFibreProtocolError, //!< A remote peer is misbehaving (indicates bug in the remote peer)
    kFibreHostUnreachable, //!< The remote peer can no longer be reached
    kFibreOutOfMemory, //!< There are not enough local resources to complete the request (this error can also pertain to statically sized buffers, not only heap memory)
    kFibreInsufficientData, // TODO: review if we need this status code
};

}

#endif // __FIBRE_STATUS_HPP