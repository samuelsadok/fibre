
#include <fibre/timer.hpp>
#include <fibre/logging.hpp>
#include <sys/epoll.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <string.h>

using namespace fibre;

DEFINE_LOG_TOPIC(TIMER);
USE_LOG_TOPIC(TIMER);

int Timer::init(Worker* worker) {
    if (is_initialized())
        return -1;
    worker_ = worker;

    // Setting an interval of 0 disarms the timer
    struct itimerspec itimerspec = {
        .it_interval = { .tv_sec = 0, .tv_nsec = 0 },
        .it_value = { .tv_sec = 0, .tv_nsec = 0 },
    };

    tim_fd_ = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);
    if (tim_fd_ < 0) {
        FIBRE_LOG(E) << "timerfd_create() failed: " << sys_err();
        goto fail1;
    }

    if (timerfd_settime(tim_fd_, 0, &itimerspec, nullptr) != 0) {
        FIBRE_LOG(E) << "timerfd_settime() failed: " << sys_err();
        goto fail2;
    }

    if (!worker_ || (worker_->register_event(tim_fd_, EPOLLIN, &timer_handler_obj) != 0)) {
        FIBRE_LOG(E) << "register_event() failed: " << sys_err();
        goto fail2;
    }
    return 0;

fail2:
    close(tim_fd_);
    tim_fd_ = -1;
fail1:
    worker_ = nullptr;
    return -1;
}

int Timer::deinit() {
    if (!is_initialized() || is_started())
        return -1;

    int result = 0;

    if (!worker_ || (worker_->deregister_event(tim_fd_) != 0)) {
        FIBRE_LOG(E) << "deregister_event() failed";
        result = -1;
    }

    // TODO: wait until the timer is really stopped and it is guaranteed that no
    // more callbacks will be triggered.
    //usleep(100000);

    if (close(tim_fd_) != 0) {
        FIBRE_LOG(E) << "close() failed: " << sys_err();
        result = -1;
    }
    tim_fd_ = -1;

    worker_ = nullptr;

    return result;
}

/**
 * @brief Starts the timer with the given interval.
 * 
 * If this function succeeds, the timer must be stopped later using stop()
 * before it can be started again. This holds even if `repeat` is false.
 *
 * @param interval_ms: The interval in milliseconds. Must be non-zero.
 * @param repeat: If false the timer will fire only once, unless it is stopped
 *        with stop(). If true the timer will fire repeatedly at the given
 *        interval until it is stopped by stop().
 * @param callback: The callback to invoke when the timer fires. The memory
 *        pointed to by this argument must remain valid until the timer has been
 *        stopped using stop().
 */
int Timer::start(uint32_t interval_ms, bool repeat, callback_t* callback) {
    if (!is_initialized() || is_started())
        return -1;
    if (!interval_ms) // zero interval would disarm the timer
        return -1;
    is_started_ = true;
    callback_ = callback;
    set_time(interval_ms, repeat);
    return 0;
}

/**
 * @brief Stops the timer.
 * 
 * The timer must have been started with start() before for this function to
 * succeed.
 * The callback set by start() may be invoked up to one more time shortly after
 * this function is called.
 */
int Timer::stop() {
    if (!is_initialized() || !is_started())
        return -1;
    set_time(0, false);
    callback_ = nullptr;
    is_started_ = false;
    return 0;
}

/**
 * @brief Updates the interval of the timer.
 * 
 * The timer must be initialized when calling this.
 * An non-zero interval will start the timer if it was not already started.
 * An interval of 0 will stop the timer if it was started.
 */
int Timer::set_time(uint32_t interval_ms, bool repeat) {
    if (!is_initialized())
        return -1;

    time_t tv_sec = interval_ms / 1000;
    long tv_nsec = static_cast<uint64_t>(interval_ms % 1000) * 1000000ULL;
    struct itimerspec itimerspec = {
        .it_interval = { .tv_sec = repeat ? tv_sec : 0, .tv_nsec = repeat ? tv_nsec : 0 }, // periodic interval between timer events
        .it_value = { .tv_sec = tv_sec, .tv_nsec = tv_nsec }, // timeout until first timer event
    };

    int val = timerfd_settime(tim_fd_, 0, &itimerspec, nullptr);
    if (val != 0) {
        FIBRE_LOG(E) << "timerfd_settime() failed: " << sys_err();
        return -1;
    }

    return 0;
}

void Timer::timer_handler(uint32_t) {
    FIBRE_LOG(D) << "timer handler";
    uint64_t val;

    // If the timer was already disarmed by timerfd_settime(), read() may fail
    // with EAGAIN (given that we made it non-blocking)
    callback_t* callback = callback_; // TODO: make callback_ volatile
    if (read(tim_fd_, &val, sizeof(val)) == sizeof(val)) {
        if (callback) {
            (*callback)();
        }
    }

    FIBRE_LOG(D) << "timer handler completed";
}
