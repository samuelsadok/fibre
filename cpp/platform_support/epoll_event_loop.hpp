#ifndef __FIBRE_LINUX_EVENT_LOOP_HPP
#define __FIBRE_LINUX_EVENT_LOOP_HPP

//#include <thread>
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>
#include <mutex>
//#include <algorithm>

#include <fibre/event_loop.hpp>
#include <fibre/logging.hpp>

namespace fibre {

/**
 * @brief Event loop based on the Linux-specific `epoll()` infrastructure.
 * 
 * Thread safety: None of the public functions are thread-safe with respect to
 * each other. However they are thread safe with respect to the internal event
 * loop, that means register_event() and deregister_event() can be called from
 * within an event callback (which executes on the event loop thread), provided
 * those calls are properly synchronized with calls from other threads.
 */
class EpollEventLoop final : public EventLoop {
public:

    /**
     * @brief Starts the event loop on the current thread and places the
     * specified start callback on the event queue.
     * 
     * The function returns when the event loop becomes empty or if a platform
     * error occurs.
     */
    RichStatus start(Logger logger, Callback<void> on_started);

    RichStatus post(Callback<void> callback) final;
    RichStatus register_event(int fd, uint32_t events, Callback<void, uint32_t> callback) final;
    RichStatus deregister_event(int fd) final;
    RichStatus open_timer(Timer** p_timer, Callback<void> on_trigger) final;
    RichStatus close_timer(Timer* timer) final;

private:
    struct EventContext {
        //int fd;
        Callback<void, uint32_t> callback;
    };

    struct TimerContext final : Timer {
        RichStatus set(float interval, TimerMode mode) final;
        void on_timer(uint32_t);
        EpollEventLoop* parent;
        int fd;
        Callback<void> callback;
    };

    std::unordered_map<int, EventContext*>::iterator drop_events(int event_fd);
    void run_callbacks(uint32_t);
    void on_timer(TimerContext* ctx);

    int epoll_fd_ = -1;
    Logger logger_ = Logger::none();
    int post_fd_ = -1;
    unsigned int iterations_ = 0;

    std::unordered_map<int, EventContext*> context_map_; // required to deregister callbacks

    static const size_t max_triggered_events_ = 16; // max number of events that can be handled per iteration
    int n_triggered_events_ = 0;
    struct epoll_event triggered_events_[max_triggered_events_];

    // List of callbacks that were submitted through post().
    std::vector<Callback<void>> pending_callbacks_;

    // Mutex to protect pending_callbacks_
    std::mutex pending_callbacks_mutex_;
};

}

#endif // __FIBRE_LINUX_EVENT_LOOP_HPP