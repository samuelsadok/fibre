#ifndef __FIBRE_ACTIVE_STREAM_HPP
#define __FIBRE_ACTIVE_STREAM_HPP

#include <fibre/stream.hpp>
#include <fibre/closure.hpp>

DEFINE_LOG_TOPIC(ASTREAM);
#define current_log_topic LOG_TOPIC_ASTREAM

namespace fibre {

template<typename TWorker>
class StreamPusher { // user-provided buffer
public:
    using get_buffer_callback_t = Callable<StreamStatus, bufptr_t*>;
    using commit_callback_t = Callable<StreamStatus, size_t>;
    using completed_callback_t = Callable<void, StreamStatus>;

    /**
     * @brief Registers the callbacks that will invoked every time new data
     * becomes available.
     * 
     * Only one subscriber can be set at a time.
     * 
     * If there is already data available when subscribe() is called, the
     * callbacks are invoked for that data.
     * 
     * All callbacks, including completed_callback() may be invoked even before
     * the subscribe() function returns.
     * 
     * @param get_buffer_callback: Invoked whenever the StreamPusher needs a new
     *        buffer where it can store the incoming data. For every (successful)
     *        call to get_buffer_callback, a corresponding call to
     *        commit_callback will be issued. The buffer that the application
     *        provides must remain valid until the commit_callback is invoked.
     *        If this callback does not return kStreamOk, the StreamPusher
     *        terminates operation and invokes completed_callback with kStreamOk.
     * @param commit_callback: Invoked whenever the StreamPusher has completed
     *        receiving a chunk of data. The chunk may be smaller than the
     *        buffer provided by the application.
     *        If this callback does not return kStreamOk, the StreamPusher
     *        terminates operation and invokes completed_callback with kStreamOk.
     * @param completed_callback: Invoked when the StreamPusher stops to operate
     *        for any reason, including unsubscribe() being called. The status
     *        code passed to this callback indicates the reason. Note that if
     *        operation stops because of a user-side error (e.g. 
     *        get_buffer_callback or commit_callback unsuccessful), the status
     *        code in this callback will be kStreamOk because the application is
     *        assumed to already know about the termination cause.
     * 
     *        Note that the fact that this callback is invoked does not free the
     *        user from the responsibility of calling unsibscribe().
     */
    virtual int subscribe(TWorker* worker, get_buffer_callback_t* get_buffer_callback, commit_callback_t* commit_callback, completed_callback_t* completed_callback) {
        if (get_buffer_callback_) {
            FIBRE_LOG(E) << "already subscribed";
            return -1;
        }
        if (!get_buffer_callback || !commit_callback || !completed_callback) {
            FIBRE_LOG(E) << "invalid argument";
            return -1;
        }

        get_buffer_callback_ = get_buffer_callback;
        commit_callback_ = commit_callback;
        completed_callback_ = completed_callback;
        return 0;
    }

    virtual int unsubscribe() {
        int result = 0;
        if (!get_buffer_callback_) {
            FIBRE_LOG(E) << "not subscribed";
            result = -1;
        }
        get_buffer_callback_ = nullptr;
        commit_callback_ = nullptr;
        completed_callback_ = nullptr;
        return result;
    }

protected:
    get_buffer_callback_t* get_buffer_callback_ = nullptr;
    commit_callback_t* commit_callback_ = nullptr;
    completed_callback_t* completed_callback_ = nullptr;
};

template<typename TWorker>
class StreamPusherIntBuffer : public StreamPusher<TWorker> { // driver-provided buffer
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
        if (!process_bytes_callback_) {
            FIBRE_LOG(E) << "invalid argument";
            return -1;
        }

        process_bytes_callback_ = process_bytes;
        return 0;
    }

    virtual int subscribe(TWorker* worker, typename StreamPusher<TWorker>::get_buffer_callback_t* get_buffer, typename StreamPusher<TWorker>::commit_callback_t* commit) override {
        if (StreamPusher<TWorker>::subscribe(worker, get_buffer, commit)) {
            return -1;
        } else if (subscribe(worker, process_bytes_obj)) {
            StreamPusher<TWorker>::unsubscribe(worker, get_buffer, commit);
            return -1;
        }
        return 0;
    }

    virtual int unsubscribe() {
        int result = 0;
        if (!process_bytes_callback_) {
            FIBRE_LOG(E) << "not subscribed";
            result = -1;
        }
        process_bytes_callback_ = nullptr;
        if (StreamPusher<TWorker>::get_buffer_callback_ || StreamPusher<TWorker>::commit_callback_) {
            if (!StreamPusher<TWorker>::unsubscribe()) {
                result = -1;
            }
        }
        return result;
    }

protected:
    process_bytes_callback_t* process_bytes_callback_ = nullptr;

private:
    StreamStatus process_bytes(cbufptr_t& buffer) {
        bufptr_t internal_range = {nullptr, buffer.length};
        if ((*StreamPusher<TWorker>::get_buffer_callback_)(&internal_range) != kStreamOk) {
            return kStreamError;
        }
        if (internal_range.length > buffer.length) {
            FIBRE_LOG(E) << "got larger buffer than requested";
            internal_range.length = buffer.length;
        }
        memcpy(internal_range.ptr, buffer.ptr, internal_range.length);
        buffer += internal_range.length;
        return (*StreamPusher<TWorker>::commit_callback_)(internal_range.length);
    }

    member_closure_t<decltype(&StreamPusherIntBuffer::process_bytes)> process_bytes_obj{&StreamPusherIntBuffer::process_bytes, this};
};

template<typename TWorker>
class StreamPuller {
public:
    using get_buffer_callback_t = Callable<StreamStatus, cbufptr_t*>;
    using consume_callback_t = Callable<StreamStatus, size_t>;
    using completed_callback_t = Callable<void, StreamStatus>;

    ~StreamPuller() {}

    /**
     * @brief Registers the callbacks that will be invoked every time the
     * object wants to get new data.
     * 
     * Only one subscriber can be set at a time.
     * 
     * If there is already data being requested when subscribe() is called, the
     * callbacks are invoked to satisfy that request.
     */
    virtual int subscribe(TWorker* worker, get_buffer_callback_t* get_buffer_callback, consume_callback_t* consume_callback, completed_callback_t* completed_callback) {
        if (get_buffer_callback_) {
            FIBRE_LOG(E) << "already subscribed";
            return -1;
        }
        if (!get_buffer_callback || !consume_callback || !completed_callback) {
            FIBRE_LOG(E) << "invalid argument";
            return -1;
        }

        get_buffer_callback_ = get_buffer_callback;
        consume_callback_ = consume_callback;
        completed_callback_ = completed_callback;
        return 0;
    }

    virtual int unsubscribe() {
        int result = 0;
        if (!get_buffer_callback_) {
            FIBRE_LOG(E) << "not subscribed";
            result = -1;
        }
        get_buffer_callback_ = nullptr;
        consume_callback_ = nullptr;
        completed_callback_ = nullptr;
        return result;
    }

protected:
    get_buffer_callback_t* get_buffer_callback_ = nullptr;
    consume_callback_t* consume_callback_ = nullptr;
    completed_callback_t* completed_callback_ = nullptr;
};


}

#undef current_log_topic

#endif // __FIBRE_ACTIVE_STREAM_HPP