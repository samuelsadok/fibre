#ifndef __FIBRE_THREADING_UTILS_HPP
#define __FIBRE_THREADING_UTILS_HPP

#ifndef __FIBRE_HPP
#error "This file should not be included directly. Include fibre.hpp instead."
#endif

#ifdef CONFIG_USE_STL_CLOCK
#include <chrono>

typedef std::chrono::time_point<std::chrono::steady_clock> monotonic_time_t;
__attribute__((unused))
static monotonic_time_t now() {
    return std::chrono::steady_clock::now();
}

__attribute__((unused))
static bool is_in_the_future(monotonic_time_t time_point) {
    return time_point > std::chrono::steady_clock::now();
}

#else
#error "Not implemented"
#endif

#ifdef CONFIG_USE_STL_THREADING
#include <thread>
#include <mutex>
#include <condition_variable>

namespace fibre {

template<bool AutoReset>
class EventWaitHandle {
public:
    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_variable_.wait(lock, [this]{
            bool was_set = is_set_;
            if (AutoReset)
                is_set_ = false;
            return was_set;
        });
    }

    void set() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            is_set_ = true;
        }
        condition_variable_.notify_all();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        is_set_ = false;
    }

    bool is_set() {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_set_;
    }
private:
    std::mutex mutex_;
    std::condition_variable condition_variable_;
    bool is_set_ = false;
};

using ManualResetEvent = EventWaitHandle<false>;
using AutoResetEvent = EventWaitHandle<true>;

}
#else
#error "Not implemented"
#endif

#endif // __FIBRE_THREADING_UTILS_HPP
