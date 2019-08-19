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
    void signal_handler();

    const char* name_;
    Worker* worker_ = nullptr;
    int event_fd_ = -1;
    callback_t* callback_ = nullptr;

    using Worker_callback_t = Callback<uint32_t>;
    Worker_callback_t signal_handler_obj = {
        .callback = [](void* ctx, uint32_t events){ ((Signal*)ctx)->signal_handler(); },
        .ctx = this
    };
};

}

#include <fibre/worker.hpp>

#endif // __FIBRE_SIGNAL_HPP