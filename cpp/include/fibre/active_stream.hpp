#ifndef __FIBRE_ACTIVE_STREAM_HPP
#define __FIBRE_ACTIVE_STREAM_HPP

#include <fibre/stream.hpp>
#include <fibre/closure.hpp>

DEFINE_LOG_TOPIC(ASTREAM);
#define current_log_topic LOG_TOPIC_ASTREAM

namespace fibre {

using completed_callback_t = Callable<void, StreamStatus>;

class StreamPusher { // sink-provided buffer
public:

    /**
     * @brief Registers the StreamSink into which data will pushed as it becomes
     * available.
     * 
     * Only one subscriber can be set at a time.
     * 
     * If there is already data available when subscribe() is called, the data
     * is pushed to the sink too.
     * 
     * The sink's member functions and completed_callback() may be invoked
     * even before the subscribe() function returns.
     * 
     * @param sink: The sink into which data shall be pushed.
     *        For every (successful) call to sink->get_buffer(), a corresponding
     *        call to sink->commit() will be issued. The buffer that the sink
     *        provides must remain valid until sink->commit() is called.
     *        If sink->get_buffer() or sink->commit() does not return kStreamOk,
     *        the StreamPusher terminates operation and invokes
     *        completed_callback with kStreamOk.
     * @param completed_callback: Invoked when the StreamPusher stops to operate
     *        for any reason, including unsubscribe() being called. The status
     *        code passed to this callback indicates the reason. Note that if
     *        operation stops because of a user-side error (e.g. 
     *        sink->get_buffer() or sink->commit() unsuccessful), the status
     *        code in this callback will be kStreamOk because the application is
     *        assumed to already know about the termination cause.
     * 
     *        Note that the fact that this callback is invoked does not free the
     *        user from the responsibility of calling unsubscribe().
     */
    virtual int subscribe(StreamSinkIntBuffer* sink, completed_callback_t* completed_callback) {
        if (sink_) {
            FIBRE_LOG(E) << "already subscribed";
            return -1;
        }
        if (!sink || !completed_callback) {
            FIBRE_LOG(E) << "invalid argument";
            return -1;
        }

        sink_ = sink;
        completed_callback_ = completed_callback;
        return 0;
    }

    virtual int unsubscribe() {
        int result = 0;
        if (!sink_) {
            FIBRE_LOG(E) << "not subscribed";
            result = -1;
        }
        sink_ = nullptr;
        completed_callback_ = nullptr;
        return result;
    }

protected:
    StreamSinkIntBuffer* sink_ = nullptr;
    completed_callback_t* completed_callback_ = nullptr;
};

class StreamPusherIntBuffer : public StreamPusher { // pusher-provided buffer
public:

    /**
     * @brief Registers the StreamSink into which data will pushed as it becomes
     * available.
     * 
     * This function is equivalent to as `StreamPusher::subscribe()`, except
     * that the pusher provides its own buffer and the sink does therefore not
     * need to provide access to an internal buffer.
     */
    virtual int subscribe(StreamSink* sink, completed_callback_t* completed_callback) {
        if (sink_) {
            FIBRE_LOG(E) << "already subscribed";
            return -1;
        }
        if (!sink || !completed_callback) {
            FIBRE_LOG(E) << "invalid argument";
            return -1;
        }

        sink_ = sink;
        completed_callback_ = completed_callback;
        return 0;
    }

    virtual int subscribe(StreamSinkIntBuffer* sink, completed_callback_t* completed_callback) override {
        return subscribe((StreamSink*)sink, completed_callback);
    }

    virtual int unsubscribe() {
        int result = 0;
        if (!sink_) {
            FIBRE_LOG(E) << "not subscribed";
            result = -1;
        }
        sink_ = nullptr;
        completed_callback_ = nullptr;
        return result;
    }

protected:
    StreamSink* sink_ = nullptr;
};

class StreamPuller {
public:

    /**
     * @brief Registers the StreamSource from which data will pulled as required.
     * 
     * Only one subscriber can be set at a time.
     * 
     * If there is already data being requested when subscribe() is called, the
     * source is invoked to satisfy that request.
     * 
     * The source's member functions and completed_callback() may be invoked
     * even before the subscribe() function returns.
     * 
     * @param source: The source from which data shall be pulled.
     *        For every (successful) call to source->get_buffer(), a corresponding
     *        call to source->consume() will be issued. The buffer that the source
     *        provides must remain valid until source->consume() is called.
     *        If source->get_buffer() or source->consume() does not return
     *        kStreamOk, the StreamPuller terminates operation and invokes
     *        completed_callback with kStreamOk.
     * @param completed_callback: Invoked when the StreamPuller stops to operate
     *        for any reason, including unsubscribe() being called. The status
     *        code passed to this callback indicates the reason. Note that if
     *        operation stops because of a user-side error (e.g. 
     *        source->get_buffer() or source->consume() unsuccessful), the status
     *        code in this callback will be kStreamOk because the application is
     *        assumed to already know about the termination cause.
     * 
     *        Note that the fact that this callback is invoked does not free the
     *        user from the responsibility of calling unsubscribe().
     */
    virtual int subscribe(StreamSourceIntBuffer* source, completed_callback_t* completed_callback) {
        if (source_) {
            FIBRE_LOG(E) << "already subscribed";
            return -1;
        }
        if (!source || !completed_callback) {
            FIBRE_LOG(E) << "invalid argument";
            return -1;
        }

        source_ = source;
        completed_callback_ = completed_callback;
        return 0;
    }

    virtual int unsubscribe() {
        int result = 0;
        if (!source_) {
            FIBRE_LOG(E) << "not subscribed";
            result = -1;
        }
        source_ = nullptr;
        completed_callback_ = nullptr;
        return result;
    }

protected:
    StreamSourceIntBuffer* source_ = nullptr;
    completed_callback_t* completed_callback_ = nullptr;
};

class StreamPullerIntBuffer : public StreamPuller { // puller-provided buffer
public:

    /**
     * @brief Registers the StreamSource from which data will pulled as required.
     * 
     * This function is equivalent to as `StreamPuller::subscribe()`, except
     * that the puller provides its own buffer and the source does therefore not
     * need to provide access to an internal buffer.
     */
    virtual int subscribe(StreamSource* source, completed_callback_t* completed_callback) {
        if (source_) {
            FIBRE_LOG(E) << "already subscribed";
            return -1;
        }
        if (!source || !completed_callback) {
            FIBRE_LOG(E) << "invalid argument";
            return -1;
        }

        source_ = source;
        completed_callback_ = completed_callback;
        return 0;
    }

    virtual int subscribe(StreamSourceIntBuffer* source, completed_callback_t* completed_callback) override {
        return subscribe((StreamSource*)source, completed_callback);
    }

    virtual int unsubscribe() {
        int result = 0;
        if (!source_) {
            FIBRE_LOG(E) << "not subscribed";
            result = -1;
        }
        source_ = nullptr;
        completed_callback_ = nullptr;
        return result;
    }

protected:
    StreamSource* source_ = nullptr;
};


static inline int connect_streams(StreamPuller* dst, StreamSourceIntBuffer* src, completed_callback_t* completed_callback) {
    return dst->subscribe(src, completed_callback);
}

static inline int connect_streams(StreamSink* dst, StreamPusherIntBuffer* src, completed_callback_t* completed_callback) {
    return src->subscribe(dst, completed_callback);
}


}

#undef current_log_topic

#endif // __FIBRE_ACTIVE_STREAM_HPP