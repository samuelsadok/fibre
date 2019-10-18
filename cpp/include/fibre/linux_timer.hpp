#ifndef __FIBRE_LINUX_TIMER_HPP
#define __FIBRE_LINUX_TIMER_HPP

#include <fibre/linux_worker.hpp>
#include <sys/timerfd.h>

namespace fibre {

class LinuxTimer {
public:
    using callback_t = Callback<>;

    int init(LinuxWorker* worker);
    int deinit();

    int start(uint32_t interval_ms, bool repeat, callback_t* callback);
    int set_time(uint32_t interval_ms, bool repeat);
    int stop();

    bool is_initialized() const { return tim_fd_ >= 0; }
    bool is_started() const { return is_started_; }

private:
    void timer_handler(uint32_t);

    LinuxWorker* worker_ = nullptr;
    int tim_fd_ = -1;
    bool is_started_ = false;
    callback_t* callback_ = nullptr;

    member_closure_t<decltype(&LinuxTimer::timer_handler)> timer_handler_obj{&LinuxTimer::timer_handler, this};
};

}

#endif // __FIBRE_LINUX_TIMER_HPP