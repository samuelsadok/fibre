
#include <fibre/platform_support/linux_event.hpp>
#include <fibre/logging.hpp>
#include <sys/epoll.h>
#include <unistd.h>
#include <stdio.h>

using namespace fibre;

DEFINE_LOG_TOPIC(SIGNAL);
USE_LOG_TOPIC(SIGNAL);


int LinuxFdEvent::init(int fd, uint32_t event_mask) {
    if (fd_ >= 0)
        return -1;
    fd_ = fd;
    event_mask_ = event_mask;
    return 0;
}

int LinuxFdEvent::deinit() {
    if (worker_) {
        FIBRE_LOG(E) << "still subscribed";
        return -1;
    }
    if (fd_ < 0)
        return -1;
    fd_ = -1;
    event_mask_ = 0;
    return 0;
}


int LinuxFdEvent::subscribe(LinuxWorker* worker, callback_t* callback) {
    if (fd_ < 0) {
        FIBRE_LOG(E) << "invalid file descriptor" << sys_err();
        return -1;
    }
    if (worker_) {
        FIBRE_LOG(E) << "already subscribed";
        return -1;
    }
    if (!worker) {
        FIBRE_LOG(E) << "invalid argument";
        return -1;
    }

    worker_ = worker;
    callback_ = callback;

    if (worker_->register_event(fd_, event_mask_, &signal_handler_obj) != 0) {
        FIBRE_LOG(E) << "register_event() failed" << sys_err();
        goto fail;
    }

    return 0;

fail:
    worker_ = nullptr;
    callback_ = nullptr;
    return -1;
}

int LinuxFdEvent::unsubscribe() {
    if (!worker_) {
        FIBRE_LOG(E) << "not subscribed";
        return -1;
    }

    int result = 0;
    
    if (worker_->deregister_event(fd_) != 0) {
        FIBRE_LOG(E) << "deregister_event() failed" << sys_err();
        result = -1;
    }

    worker_ = nullptr;
    callback_ = nullptr;
    return result;
}

/*void LinuxFdEvent::event_handler(uint32_t) {
    FIBRE_LOG(D) << "\"" << name_ << "\" handler";
    if (callback_)
        (*callback_)();
    FIBRE_LOG(D) << "\"" << name_ << "\" handler completed";
}*/


int LinuxAutoResetEvent::init() {
    int fd = eventfd(0, 0);
    if (fd < 0) {
        FIBRE_LOG(E) << "eventfd() failed" << sys_err();
        goto fail1;
    }
    if (LinuxFdEvent::init(fd, EPOLLIN) != 0) {
        goto fail2;
    }

    return 0;

fail2:
    close(fd);
fail1:
    return -1;
}

int LinuxAutoResetEvent::deinit() {
    int fd = LinuxFdEvent::get_fd();
    if (fd < 0)
        return -1;

    int result = 0;
    
    if (LinuxFdEvent::deinit() != 0) {
        FIBRE_LOG(E) << "LinuxFdEvent::deinit() failed" << sys_err();
        result = -1;
    }

    if (close(fd) != 0) {
        FIBRE_LOG(E) << "close() failed" << sys_err();
        result = -1;
    }

    return result;
}


int LinuxAutoResetEvent::set() {
    int fd = get_fd();
    if (fd < 0)
        return -1;
        
    const uint64_t val = 1;
    if (write(fd, &val, sizeof(val)) != sizeof(val)) {
        FIBRE_LOG(E) << "write() failed" << sys_err();
        return -1;
    }
    return 0;
}

void LinuxAutoResetEvent::event_handler(uint32_t) {
    FIBRE_LOG(D) << "\"" << name_ << "\" handler";
    uint64_t val;

    // TODO: warn if read fails
    callback_t* callback = callback_; // TODO: make callback_ volatile
    if (read(get_fd(), &val, sizeof(val)) == sizeof(val)) {
        if (callback)
            (*callback)();
    }
    
    FIBRE_LOG(D) << "\"" << name_ << "\" handler completed";
}
