#ifndef __FIBRE_EVENT_LOOP_HPP
#define __FIBRE_EVENT_LOOP_HPP

#include <fibre/callback.hpp>
#include <fibre/timer.hpp>
#include <stdint.h>

namespace fibre {

/**
 * @brief Base class for event loops.
 * 
 * Thread-safety: The public functions of this class except for post() must not
 * be assumed to be thread-safe.
 * Generally the functions of an event loop are only safe to be called from the
 * event loop's thread itself.
 */
class EventLoop : public TimerProvider {
public:
    /**
     * @brief Registers a callback for immediate execution on the event loop
     * thread.
     * 
     * This function must be thread-safe.
     */
    virtual RichStatus post(Callback<void> callback) = 0;

    /**
     * @brief Registers the given file descriptor on this event loop.
     * 
     * This function is only implemented on Unix-like systems.
     * 
     * @param fd: A waitable Unix file descriptor on which to listen for events.
     * @param events: A bitfield that specifies the events to listen for.
     *        For instance EPOLLIN or EPOLLOUT.
     * @param callback: The callback to invoke every time the event triggers.
     *        A bitfield is passed to the callback to indicate which events were
     *        triggered. This callback must remain valid until
     *        deregister_event() is called for the same file descriptor.
     */
    virtual RichStatus register_event(int fd, uint32_t events, Callback<void, uint32_t> callback) = 0;

    /**
     * @brief Deregisters the given event.
     * 
     * Once this function returns, the associated callback will no longer be
     * invoked and its resources can be freed.
     */
    virtual RichStatus deregister_event(int fd) = 0;
};

}

#endif // __FIBRE_EVENT_LOOP_HPP