#ifndef __FIBRE_ACTIVE_STREAM_HPP
#define __FIBRE_ACTIVE_STREAM_HPP

#include <fibre/stream.hpp>

namespace fibre {

template<typename TWorker>
class ActiveStreamSource {
public:
    using callback_t = Callback<StreamSource::status_t, cbufptr_t>;

    /**
     * @brief Registers a callback that will be invoked every time new data comes in.
     * 
     * Spurious invokations are possible. That means the callback can be called
     * without any data being ready.
     * 
     * Only one callback can be registered at a time.
     * 
     * TODO: if a callback is registered while the source is ready, should it be
     * invoked? The easiest answer for the epoll architecture would be yes.
     */
    virtual int subscribe(TWorker* worker, callback_t* callback) = 0;
    virtual int unsubscribe() = 0;
};

template<typename TWorker>
class ActiveStreamSink {
public:
    using callback_t = Callback<StreamSink::status_t>;

    /**
     * @brief Registers a callback that will be invoked every time the
     * object is ready to accept new data.
     * 
     * Spurious invokations are possible.
     * 
     * Only one callback can be registered at a time.
     * 
     * TODO: if a callback is registered while the sink is ready, should it be
     * invoked? The easiest answer for the epoll architecture would be yes.
     */
    virtual int subscribe(TWorker* worker, callback_t* callback) = 0;
    virtual int unsubscribe() = 0;
};

}

#endif // __FIBRE_ACTIVE_STREAM_HPP