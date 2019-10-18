#ifndef __FIBRE_LINUX_WORKER_HPP
#define __FIBRE_LINUX_WORKER_HPP

#include <fibre/closure.hpp>
#include <thread>
#include <unordered_map>
#include <ioapiset.h>

namespace fibre {

/**
 * @brief Implements a worker based on the Windows IOCP API.
 * 
 * The worker can therefore be used with any type of waitable object that is
 * represented as file or socket handle.
 * 
 * Thread safety: None of the public functions are thread-safe with respect to
 * each other. However they are thread safe with respect to the internal event
 * loop, that means register_event() and deregister_event() can be called from
 * within an event callback (which executes on the event loop thread), provided
 * those calls are properly synchronized with calls from other threads.
 */
class WindowsIOCPWorker {
public:
    using callback_t = Callback<>;

    int init();
    int deinit();

    int register_event(int event_fd, uint32_t events, callback_t* callback);
    //int modify_event(int event_fd, uint32_t events, callback_t* callback);
    int deregister_event(int event_fd);

private:
    void event_loop();
    void stop_handler();

    HANDLE h_completion_port_ = NULL;
    //LinuxAutoResetEvent stop_signal_ = LinuxAutoResetEvent("stop");
    bool should_run_ = false;
    volatile unsigned int iterations_ = 0;
    std::thread* thread_ = nullptr;
    size_t n_events_ = 0; // number of registered events (for debugging only)
};

}

#endif // __FIBRE_LINUX_WORKER_HPP