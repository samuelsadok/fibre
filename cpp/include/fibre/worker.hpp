#ifndef __FIBRE_WORKER_HPP
#define __FIBRE_WORKER_HPP

#include <fibre/signal.hpp>
#include <fibre/closure.hpp>
#include <thread>
#include <sys/epoll.h>
#include <unordered_map>

namespace fibre {

/**
 * @brief
 * 
 * Thread safety: None of the public functions are thread-safe with respect to
 * each other. However they are thread safe with respect to the internal event
 * loop, that means register_event() and deregister_event() can be called from
 * within an event callback (which executes on the event loop thread), provided
 * those calls are properly synchronized with calls from other threads.
 */
class Worker {
public:
    using callback_t = Callback<uint32_t>;

    int init();
    int deinit();

    int register_event(int event_fd, uint32_t events, callback_t* callback);
    //int modify_event(int event_fd, uint32_t events, callback_t* callback);
    int deregister_event(int event_fd);

private:
    void event_loop();
    void stop_handler();

    int epoll_fd_ = -1;
    Signal stop_signal = Signal("stop");
    bool should_run_ = false;
    volatile unsigned int iterations_ = 0;
    std::thread* thread_ = nullptr;
    size_t n_events_ = 0; // number of registered events (for debugging only)

    std::unordered_map<int, callback_t*> fd_to_callback_map_; // required to deregister callbacks

    static const size_t max_triggered_events_ = 5; // max number of events that can be handled per iteration
    int n_triggered_events_ = 0;
    struct epoll_event triggered_events_[max_triggered_events_];

    member_closure_t<decltype(&Worker::stop_handler)> stop_handler_obj{&Worker::stop_handler, this};
};

}

#endif // __FIBRE_WORKER_HPP