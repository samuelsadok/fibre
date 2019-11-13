#ifndef __FIBRE_ACTIVE_STREAM_HPP
#define __FIBRE_ACTIVE_STREAM_HPP

#include <fibre/stream.hpp>
#include <fibre/closure.hpp>

DEFINE_LOG_TOPIC(ASTREAM);
#define current_log_topic LOG_TOPIC_ASTREAM

namespace fibre {

template<typename TWorker>
class ActiveStreamSource {
public:
    using callback_t = Callback<StreamStatus, cbufptr_t>;

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
    using callback_t = Callback<StreamStatus>;

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

template<typename TWorker>
class StreamPusher0 { // user-provided buffer
public:
    using get_buffer_callback_t = Callable<StreamStatus, cbufptr_t>;
    using commit_callback_t = Callable<StreamStatus, cbufptr_t>;

    /**
     * @brief Registers the callbacks that will invoked every time new data
     * becomes available.
     * 
     * Only one subscriber can be set at a time.
     * 
     * If there is already data available when subscribe() is called, the
     * callbacks are invoked for that data.
     */
    virtual int subscribe(TWorker* worker, get_buffer_callback_t* get_buffer, commit_callback_t* commit) {
        if (get_buffer_callback_ || commit_callback_) {
            FIBRE_LOG(E) << "already subscribed";
            return -1;
        }

        get_buffer_callback_ = get_buffer;
        commit_callback_ = commit;
        return 0;
    }

    virtual int unsubscribe() {
        int result = 0;
        if (!get_buffer_callback_ && !commit_callback_) {
            FIBRE_LOG(E) << "not subscribed";
            result = -1;
        }
        get_buffer_callback_ = nullptr;
        commit_callback_ = nullptr;
        return result;
    }

protected:
    get_buffer_callback_t* get_buffer_callback_ = nullptr;
    commit_callback_t* commit_callback_ = nullptr;
};

template<typename TWorker>
class StreamPusher { // driver-provided buffer
public:
    using process_bytes_callback_t = Callable<StreamStatus, cbufptr_t&>;

    /**
     * @brief Registers the callback that will invoked every time new data
     * becomes available.
     * 
     * Only one subscriber can be set at a time.
     * 
     * If there is already data available when subscribe() is called, the
     * callbacks are invoked for that data.
     */
    virtual int subscribe(TWorker* worker, process_bytes_callback_t* process_bytes) {
        if (process_bytes_callback_) {
            FIBRE_LOG(E) << "already subscribed";
            return -1;
        }

        process_bytes_callback_ = process_bytes;
        return 0;
    }

    virtual int unsubscribe() {
        int result = 0;
        if (!process_bytes_callback_) {
            FIBRE_LOG(E) << "not subscribed";
            result = -1;
        }
        process_bytes_callback_ = nullptr;
        return result;
    }

protected:
    process_bytes_callback_t* process_bytes_callback_ = nullptr;
};

template<typename TWorker>
class StreamPuller {
public:
    ~StreamPuller() {}

    using get_buffer_callback_t = Callable<StreamStatus, cbufptr_t*>;
    using consume_callback_t = Callable<StreamStatus, size_t>;

    /**
     * @brief Registers the callbacks that will be invoked every time the
     * object wants to get new data.
     * 
     * Only one subscriber can be set at a time.
     * 
     * If there is already data being requested when subscribe() is called, the
     * callbacks are invoked to satisfy that request.
     */
    virtual int subscribe(TWorker* worker, get_buffer_callback_t* get_buffer, consume_callback_t* consume) {
        if (get_buffer_callback_ || consume_callback_) {
            FIBRE_LOG(E) << "already subscribed";
            return -1;
        }

        get_buffer_callback_ = get_buffer;
        consume_callback_ = consume;
        return 0;
    }

    virtual int unsubscribe() {
        int result = 0;
        if (!get_buffer_callback_ && !consume_callback_) {
            FIBRE_LOG(E) << "not subscribed";
            result = -1;
        }
        get_buffer_callback_ = nullptr;
        consume_callback_ = nullptr;
        return result;
    }

protected:
    get_buffer_callback_t* get_buffer_callback_ = nullptr;
    consume_callback_t* consume_callback_ = nullptr;
};


}

#undef current_log_topic

#endif // __FIBRE_ACTIVE_STREAM_HPP