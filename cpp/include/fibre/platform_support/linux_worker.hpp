#ifndef __FIBRE_LINUX_WORKER_HPP
#define __FIBRE_LINUX_WORKER_HPP

#include "linux_event.hpp"
#include <fibre/closure.hpp>
#include <thread>
#include <sys/epoll.h>
#include <unordered_map>
#include <mutex>

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
class LinuxWorker {
public:
    using callback_t = Callback<uint32_t>;

    int init();
    int deinit();

    int register_event(int event_fd, uint32_t events, callback_t* callback);
    //int modify_event(int event_fd, uint32_t events, callback_t* callback);
    int deregister_event(int event_fd);

    template<typename TFunctor>
    int run_sync(const TFunctor& functor) {
        std::mutex mutex;
        
        std::unique_lock<std::mutex> lock(mutex); // = std::make_shared(new std::unique_lock<std::mutex>{mutex});

        LinuxAutoResetEvent event = LinuxAutoResetEvent("run_sync");
        auto closure = make_lambda_closure([&lock, &functor](){
            functor();
            lock = std::unique_lock<std::mutex>{};
        });
        
        event.init();
        event.subscribe(this, &closure);
        event.set();
        std::cout << "will wait";
        {
            std::unique_lock<std::mutex> lock(mutex);
        }
        std::cout << "did wait";
        event.unsubscribe();
        event.deinit();
        return 0;
    }

private:
    void event_loop();
    void stop_handler();

    int epoll_fd_ = -1;
    LinuxAutoResetEvent stop_signal_ = LinuxAutoResetEvent("stop");
    bool should_run_ = false;
    volatile unsigned int iterations_ = 0;
    std::thread* thread_ = nullptr;
    size_t n_events_ = 0; // number of registered events (for debugging only)

    std::unordered_map<int, callback_t*> fd_to_callback_map_; // required to deregister callbacks

    static const size_t max_triggered_events_ = 5; // max number of events that can be handled per iteration
    int n_triggered_events_ = 0;
    struct epoll_event triggered_events_[max_triggered_events_];

    member_closure_t<decltype(&LinuxWorker::stop_handler)> stop_handler_obj{&LinuxWorker::stop_handler, this};
};

}

#endif // __FIBRE_LINUX_WORKER_HPP