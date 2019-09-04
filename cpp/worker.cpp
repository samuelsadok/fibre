
#include <fibre/worker.hpp>
#include <fibre/logging.hpp>

#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

using namespace fibre;

DEFINE_LOG_TOPIC(WORKER);
USE_LOG_TOPIC(WORKER);

/**
 * @brief Starts the worker thread(s).
 * 
 * From this point on until deinit() the worker will start handling events that
 * are associated with this worker using register().
 */
int Worker::init() {
    if (thread_)
        return 1;

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        FIBRE_LOG(E) << "epoll_create1() failed";
        goto fail0;
    }

    if (stop_signal.init(this, &stop_handler_obj) != 0) {
        FIBRE_LOG(E) << "signal init failed";
        goto fail1;
    }

    should_run_ = true;
    thread_ = new std::thread(&Worker::event_loop, this);
    return 0;

fail1:
    close(epoll_fd_);
fail0:
    return -1;
}

/**
 * @brief Terminates all worker threads and closses the epoll instance.
 * 
 * If not all events are deregistered at the time of this call, the function
 * returns an error code and the behavior us undefined.
 */
int Worker::deinit() {
    if (!thread_)
        return -1;

    int result = 0;

    should_run_ = false;
    if (stop_signal.set() != 0) {
        FIBRE_LOG(E) << "failed to set stop signal";
        result = -1;
    }

    FIBRE_LOG(D) << "wait for worker thread...";
    thread_->join();
    delete thread_;
    FIBRE_LOG(D) << "worker thread finished";

    if (stop_signal.deinit() != 0) {
        FIBRE_LOG(E) << "stop signal deinit failed";
        result = -1;
    }

    if (n_events_) {
        FIBRE_LOG(W) << "closed epoll instance before all events were deregistered.";
        result = -1;
    }

    if (close(epoll_fd_) != 0) {
        FIBRE_LOG(E) << "close() failed";
        result = -1;
    }

    return result;
}

/**
 * @brief Registers the event with this worker.
 * 
 * @param event_fd: A waitable UNIX file descriptor.
 * @param events: A bit mask that describes what type of events to wait for (readable/writable/...)
 * @param callback: Will be invoked when the event triggers. The callback will
 *        run on this worker's event loop thread. The memory pointed to by this
 *        argument must remain valid until deregister_event() for the
 *        corresponding event has returned.
 */
int Worker::register_event(int event_fd, uint32_t events, callback_t* callback) {
    if (event_fd < 0) {
        return -1;
    }

    n_events_++;
    fd_to_callback_map_[event_fd] = callback;

    struct epoll_event ev = {
        .events = events,
        .data = { .ptr = callback }
    };

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd, &ev) != 0) {
        FIBRE_LOG(E) << "epoll_ctl() failed: " << sys_err();
        n_events_--;
        return -1;
    }

    return 0;
}

/*int Worker::modify_event(int event_fd, uint32_t events, callback_t* callback) {
    if (event_fd < 0) {
        return -1;
    }

    struct epoll_event ev = {
        .events = events,
        .data = { .ptr = callback }
    };

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, event_fd, &ev) != 0) {
        FIBRE_LOG(E) << "epoll_ctl() failed: " << sys_err();
        return -1;
    }
    
    return 0;
    
}*/

/**
 * @brief Deregisters the given event so that the callback is no longer invoked.
 * 
 * This function blocks until it is guaranteed that the last invokation of the
 * event's callback has returned.
 */
int Worker::deregister_event(int event_fd) {
    int result = 0;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, event_fd, nullptr) != 0) {
        FIBRE_LOG(E) << "epoll_ctl() failed: " << sys_err();
        result = -1;
    }

    if (should_run_) {
        if (std::this_thread::get_id() == thread_->get_id()) {
            callback_t* callback = fd_to_callback_map_[event_fd];
            // We are running in the event loop. Just make sure that the
            // callback can no longer be executed in this iteration of the loop.
            for (int i = 0; i < n_triggered_events_; ++i) {
                if (triggered_events_[i].data.ptr == callback)
                    triggered_events_[i].data.ptr = nullptr;
            }

        } else {
            // We are on another thread than the event loop. Synchronize with
            // the event loop to ensure that it doesn't execute the callback
            // anymore.

            // Trigger one iteration of the event loop, so that will_enter_epoll_ is
            // encountered.
            if (stop_signal.set() != 0) {
                FIBRE_LOG(E) << "stop signal set failed";
                result = -1;
            }

            // TODO: this is a very hacky spin to wait for the event_loop to pass a
            // certain point.
            unsigned int iterations = iterations_;
            while (iterations == iterations_)
                usleep(1000);
        }
    }
    
    fd_to_callback_map_.erase(event_fd);
    
    n_events_--;
    return result;
}

void Worker::event_loop() {
    while (should_run_) {
        iterations_++;

        do {
            FIBRE_LOG(D) << "epoll_wait...";
            n_triggered_events_ = epoll_wait(epoll_fd_, triggered_events_, max_triggered_events_, -1);
        } while (n_triggered_events_ < 0 && errno == EINTR); // ignore syscall interruptions. This happens for instance during suspend.

        if (n_triggered_events_ <= 0) {
            FIBRE_LOG(E) << "epoll_wait() failed with " <<  n_triggered_events_ << ": " << sys_err() << " - Terminating worker thread.";
            break;
        }

        for (int i = 0; i < n_triggered_events_; ++i) {
            callback_t* callback = (callback_t*)triggered_events_[i].data.ptr;
            if (callback && callback->callback) {
                callback->callback(callback->ctx, triggered_events_[i].events);
            }
        }
    }

    iterations_++; // unblock deregister_event(stop_fd)
}

void Worker::stop_handler() {
    FIBRE_LOG(D) << "stop handler\n";
}
