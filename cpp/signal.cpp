
#include <fibre/signal.hpp>
#include <sys/epoll.h>
#include <unistd.h>
#include <stdio.h>

using namespace fibre;

int Signal::init(Worker* worker, callback_t* callback) {
    if (event_fd_ >= 0)
        return -1;
    
    worker_ = worker;
    callback_ = callback;

    event_fd_ = eventfd(0, 0);
    if (event_fd_ < 0) {
        printf("eventfd() failed\n");
        goto fail1;
    }

    if (worker_->register_event(event_fd_, EPOLLIN, &signal_handler_obj) != 0) {
        printf("register_event() failed\n");
        goto fail2;
    }

    return 0;

fail2:
    close(event_fd_);
    event_fd_ = -1;
fail1:
    callback_ = nullptr;
    return -1;
}

int Signal::deinit() {
    if (event_fd_ < 0)
        return -1;

    int result = 0;
    
    if (worker_->deregister_event(event_fd_) != 0) {
        printf("deregister_event() failed\n");
        result = -1;
    }

    if (close(event_fd_) != 0) {
        printf("close() failed\n");
        result = -1;
    }
    event_fd_ = -1;

    callback_ = nullptr;
    worker_ = nullptr;

    return result;
}

int Signal::set() {
    if (event_fd_ < 0)
        return -1;
        
    const uint64_t val = 1;
    if (write(event_fd_, &val, sizeof(val)) != sizeof(val)) {
        printf("write() failed\n");
        return -1;
    }
    return 0;
}

void Signal::signal_handler() {
    printf("signal [%s] handler\n", name_);
    uint64_t val;
    //read(event_fd_, &val, sizeof(val));

    // TODO: warn if read fails
    callback_t* callback = callback_; // TODO: make callback_ volatile
    if (read(event_fd_, &val, sizeof(val)) == sizeof(val)) {
        if (callback)
            callback->callback(callback->ctx);
    }
    
    printf("signal [%s] handler completed\n", name_);
}
