#ifndef __FIBRE_SIGNAL_HPP
#define __FIBRE_SIGNAL_HPP

#include <fibre/callback.hpp>
#include <sys/eventfd.h>

namespace fibre {

class Worker;

class Signal {
public:
    using callback_t = Callback<>;

    Signal() : name_("unnamed") {}
    explicit Signal(const char* name) : name_(name) {}

    int init(Worker* worker, callback_t* callback);
    int deinit();

    int set();

private:
    void signal_handler(uint32_t);

    const char* name_;
    Worker* worker_ = nullptr;
    int event_fd_ = -1;
    callback_t* callback_ = nullptr;

    Closure<Signal, std::tuple<Signal*>, std::tuple<uint32_t>, void> signal_handler_obj{&Signal::signal_handler, this};
};

}

#include <fibre/worker.hpp>

#endif // __FIBRE_SIGNAL_HPP