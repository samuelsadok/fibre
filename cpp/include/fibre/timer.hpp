#ifndef __FIBRE_TIMER_HPP
#define __FIBRE_TIMER_HPP

#include <fibre/callback.hpp>

namespace fibre {

struct RichStatus;

enum class TimerMode {
    kNever,
    kOnce,
    kPeriodic,
};

class Timer {
public:
    /**
     * @brief Sets the timer state.
     * 
     * This can be called at any time while the timer is open, regardless
     * whether it is running or stopped.
     * 
     * @param interval: The delay from now when the timer should fire the next
     *        time. For periodic timers this also sets the interval between
     *        subsequent triggers. For TimerMode::kNever this parameter is
     *        ignored.
     *        Periodic timers will attempt to keep the exact interval, even if
     *        the callback takes a non-negligible time (due to CPU bound work).
     *        If the callback takes very long (on the order of an interval or
     *        longer the timer shall skip triggers as appropriate.
     * @param mode: If false, the timer will fire only once unless the
     *        set() function is called again. If true, the timer will fire
     *        repeatedly in intervals specified by `interval`.
     */
    virtual RichStatus set(float interval, TimerMode mode) = 0;
};

class TimerProvider {
public:
    /**
     * @brief Opens a new timer.
     * 
     * The timer starts in stopped state.
     * 
     * @param on_trigger: The callback that will be called whenever the timer
     *        fires.
     */
    virtual RichStatus open_timer(Timer** p_timer, Callback<void> on_trigger) = 0;

    /**
     * @brief Closes the specified timer.
     * 
     * This can be called regardless of whether the timer is running or not.
     * The associated callback will not be called again after (nor during) this
     * function.
     */
    virtual RichStatus close_timer(Timer* timer) = 0;
};

}

#endif // __FIBRE_TIMER_HPP