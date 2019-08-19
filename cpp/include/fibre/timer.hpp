#ifndef __FIBRE_TIMER_HPP
#define __FIBRE_TIMER_HPP

#include <fibre/worker.hpp>
#include <sys/timerfd.h>

namespace fibre {

class Timer {
public:
    using callback_t = Callback<>;

    int init(Worker* worker);
    int deinit();

    int start(uint32_t interval_ms, bool repeat, callback_t* callback);
    int set_time(uint32_t interval_ms, bool repeat);
    int stop();

    bool is_initialized() const { return tim_fd_ >= 0; }
    bool is_started() const { return is_started_; }

private:
    void timer_handler();

    Worker* worker_ = nullptr;
    int tim_fd_ = -1;
    bool is_started_ = false;
    callback_t* callback_ = nullptr;

    Worker::callback_t timer_handler_obj = {
        .callback = [](void* ctx, uint32_t events){ ((Timer*)ctx)->timer_handler(); },
        .ctx = this
    };
};

}

#endif // __FIBRE_TIMER_HPP