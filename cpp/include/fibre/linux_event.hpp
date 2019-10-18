#ifndef __FIBRE_LINUX_EVENT_HPP
#define __FIBRE_LINUX_EVENT_HPP

#include <fibre/closure.hpp>
#include <sys/eventfd.h>

namespace fibre {

// TODO: move to platform independent file
template<typename TWorker, typename ... TArgs>
class Event {
public:
    using callback_t = Callback<TArgs...>;

    virtual int subscribe(TWorker* worker, callback_t* callback);
    virtual int unsubscribe();
    // int wait(); TODO: do we need this? can we even provide this in all circumstances?
};


class LinuxWorker;

/**
 * @brief Provides an event that is based on a unix file descriptor.
 * The event can only be handled on a LinuxWorker
 */
class LinuxFdEvent : public Event<LinuxWorker> {
public:
    using callback_t = Callback<>;

    LinuxFdEvent() : name_("unnamed") {}
    explicit LinuxFdEvent(const char* name) : name_(name) {}

    int init(int fd, uint32_t event_mask);
    int deinit();

    int subscribe(LinuxWorker* worker, callback_t* callback) final;
    int unsubscribe() final;

    int get_fd() const { return fd_; }

    // set during subscribe() and reset during unsubscribe()
    callback_t* callback_ = nullptr;

private:
    virtual void event_handler(uint32_t) {
        if (callback_)
            (*callback_)();
    }

    // set during init() and reset during deinit()
    const char* name_;
    int fd_ = -1;
    unsigned int event_mask_ = 0;
    
    // set during subscribe() and reset during unsubscribe()
    LinuxWorker* worker_ = nullptr;

    member_closure_t<decltype(&LinuxFdEvent::event_handler)> signal_handler_obj{&LinuxFdEvent::event_handler, this};
};

class LinuxAutoResetEvent : public LinuxFdEvent {
public:
    LinuxAutoResetEvent() : name_("unnamed") {}
    explicit LinuxAutoResetEvent(const char* name) : name_(name) {}

    int init();
    int deinit();

    int set();

private:
    void event_handler(uint32_t) final;

    const char* name_;
};

}

#include <fibre/linux_worker.hpp>

#endif // __FIBRE_LINUX_EVENT_HPP