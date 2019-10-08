#ifndef __FIBRE_SIGNAL_HPP
#define __FIBRE_SIGNAL_HPP

#include <fibre/closure.hpp>
#include <sys/eventfd.h>

namespace fibre {

class Worker;

class Signal {
public:
    using callback_t = Callback<>;

    Signal() : name_("unnamed") {}
    explicit Signal(const char* name) : name_(name) {}

    int init(Worker* worker, callback_t* callback, int fd);
    int init(Worker* worker, callback_t* callback);
    int deinit();

    int set();

    int get_fd() { return event_fd_; }

private:
    void signal_handler(uint32_t);

    const char* name_;
    Worker* worker_ = nullptr;
    int event_fd_ = -1;
    callback_t* callback_ = nullptr;

    member_closure_t<decltype(&Signal::signal_handler)> signal_handler_obj{&Signal::signal_handler, this};
};

}

#include <fibre/worker.hpp>

#endif // __FIBRE_SIGNAL_HPP