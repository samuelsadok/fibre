
#include <fibre/config.hpp>

#if FIBRE_ENABLE_EVENT_LOOP

#include "epoll_event_loop.hpp"
#include <fibre/rich_status.hpp>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <string.h>

using namespace fibre;

RichStatus EpollEventLoop::start(Logger logger, Callback<void> on_started) {
    F_RET_IF(epoll_fd_ >= 0, "already started");

    epoll_fd_ = epoll_create1(0);
    F_RET_IF(epoll_fd_ < 0, "epoll_create1() failed" << sys_err{});
    logger_ = logger;

    RichStatus status = RichStatus::success();

    post_fd_ = eventfd(0, 0);
    
    if (post_fd_ < 0) {
        status = F_MAKE_ERR("failed to create an event for posting callbacks onto the event loop");
        goto done0;
    }

    if ((status = register_event(post_fd_, EPOLLIN, MEMBER_CB(this, run_callbacks))).is_error()) {
        status = F_AMEND_ERR(status, "failed to register event");
        goto done1;
    }
    
    if ((status = post(on_started)).is_error()) {
        status = F_AMEND_ERR(status, "post() failed");
        goto done2;
    }

    // Run for as long as there are callbacks pending posted or there's at least
    // one file descriptor other than post_fd_ registerd.
    while (pending_callbacks_.size() || (context_map_.size() > 1)) {
        iterations_++;

        do {
            F_LOG_T(logger, "epoll_wait...");
            n_triggered_events_ = epoll_wait(epoll_fd_, triggered_events_, max_triggered_events_, -1);
            F_LOG_T(logger, "epoll_wait unblocked by " << n_triggered_events_ << " events");
            if (errno == EINTR) {
                F_LOG_D(logger, "interrupted");
            }
        } while (n_triggered_events_ < 0 && errno == EINTR); // ignore syscall interruptions. This happens for instance during suspend.

        if (n_triggered_events_ <= 0) {
            status = F_MAKE_ERR("epoll_wait() failed with " <<  n_triggered_events_ << ": " << sys_err() << " - Terminating worker thread.");
            break;
        }

        // Handle events
        for (int i = 0; i < n_triggered_events_; ++i) {
            EventContext* ctx = (EventContext*)triggered_events_[i].data.ptr;
            if (ctx) {
                try { // TODO: not sure if using "try" without throwing exceptions will do unwanted things with the stack
                    ctx->callback.invoke(triggered_events_[i].events);
                } catch (...) {
                    F_LOG_E(logger, "worker callback threw an exception.");
                }
            }
        }
    }

    F_LOG_D(logger, "epoll loop exited");

done2:
    if (deregister_event(post_fd_).is_error()) {
        status = F_MAKE_ERR("deregister_event() failed");
    }

done1:
    if (close(post_fd_) != 0) {
        status = F_AMEND_ERR(status, "close() failed: " << sys_err());
    }
    post_fd_ = -1;

done0:
    if (close(epoll_fd_) != 0) {
        status = F_AMEND_ERR(status, "close() failed: " << sys_err());
    }
    epoll_fd_ = -1;

    return status;
}

RichStatus EpollEventLoop::post(Callback<void> callback) {
    F_RET_IF(epoll_fd_ < 0, "not started");

    {
        std::unique_lock<std::mutex> lock(pending_callbacks_mutex_);
        pending_callbacks_.push_back(callback);
    }

    const uint64_t val = 1;
    F_RET_IF(write(post_fd_, &val, sizeof(val)) != sizeof(val),
             "write() failed: " << sys_err());
    return RichStatus::success();
}

RichStatus EpollEventLoop::register_event(int event_fd, uint32_t events, Callback<void, uint32_t> callback) {
    F_RET_IF(epoll_fd_ < 0, "not initialized");
    F_RET_IF(event_fd < 0, "invalid argument");

    EventContext* ctx = new EventContext{callback};
    struct epoll_event ev = {
        .events = events,
        .data = { .ptr = ctx }
    };
    context_map_[event_fd] = ctx;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd, &ev) != 0) {
        delete ctx;
        return F_MAKE_ERR("epoll_ctl(" << event_fd << "...) failed: " << sys_err());
    }

    F_LOG_T(logger_, "registered epoll event " << event_fd);

    return RichStatus::success();
}

std::unordered_map<int, EpollEventLoop::EventContext*>::iterator EpollEventLoop::drop_events(int event_fd) {
    auto it = context_map_.find(event_fd);
    if (it == context_map_.end()) {
        return it;
    }

    for (int i = 0; i < n_triggered_events_; ++i) {
        if ((EventContext*)(triggered_events_[i].data.ptr) == it->second) {
            triggered_events_[i].data.ptr = nullptr;
        }
    }

    return it;
}

RichStatus EpollEventLoop::deregister_event(int event_fd) {
    F_RET_IF(epoll_fd_ < 0, "not initialized");

    RichStatus status = RichStatus::success();

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, event_fd, nullptr) != 0) {
        status = F_MAKE_ERR("epoll_ctl() failed: " << sys_err());
    }

    auto it = drop_events(event_fd);
    F_RET_IF(it == context_map_.end(), "event context not found");
    context_map_.erase(it);
    
    return status;
}

RichStatus EpollEventLoop::open_timer(Timer** p_timer, Callback<void> on_trigger) {
    if (p_timer) {
        *p_timer = nullptr;
    }

    int fd = timerfd_create(CLOCK_BOOTTIME, 0);
    F_RET_IF(fd < 0, "timerfd_create() failed: " << sys_err{});

    TimerContext* timer = new TimerContext{}; // deleted in close_timer()
    timer->parent = this;
    timer->fd = fd;
    timer->callback = on_trigger;

    RichStatus status;

    if ((status = register_event(fd, EPOLLIN, MEMBER_CB(timer, on_timer))).is_error()) {
        goto fail;
    }
    
    if (p_timer) {
        *p_timer = timer;
    }
    return RichStatus::success();

fail:
    close(fd);
    delete timer;
    return status;
}

RichStatus EpollEventLoop::TimerContext::set(float interval, TimerMode mode) {
    struct itimerspec timerspec = {};

    if (mode != TimerMode::kNever) {
        timerspec.it_value = {
            .tv_sec = (long)interval,
            .tv_nsec = (long)((interval - (float)(long)interval) * 1e9f)
        };
        if (mode == TimerMode::kPeriodic) {
            timerspec.it_interval = timerspec.it_value;
        }
    }

    parent->drop_events(fd);

    if (timerfd_settime(fd, 0, &timerspec, nullptr) != 0) {
        return F_MAKE_ERR("timerfd_settime() failed: " << sys_err{});
    }

    return RichStatus::success();
}

void EpollEventLoop::TimerContext::on_timer(uint32_t mask) {
    if (mask & EPOLLIN) {
        uint64_t n_triggers;
        if (read(fd, (uint8_t*)&n_triggers, sizeof(n_triggers)) == -1) {
            F_LOG_E(parent->logger_, "failed to read timer: " << sys_err{});
            return;
        }

        callback.invoke();
    }

    if (mask & ~(EPOLLIN)) {
        F_LOG_E(parent->logger_, "unexpected event " << mask);
        return;
    }
}

RichStatus EpollEventLoop::close_timer(Timer* timer) {
    TimerContext* ctx = static_cast<TimerContext*>(timer);
    deregister_event(ctx->fd);
    close(ctx->fd);
    delete ctx;
    return RichStatus::success();
}

void EpollEventLoop::run_callbacks(uint32_t) {
    // TODO: warn if read fails
    uint64_t val;
    F_LOG_IF(logger_, read(post_fd_, &val, sizeof(val)) != sizeof(val),
             "failed to read from post file descriptor");

    std::vector<Callback<void>> pending_callbacks;

    {
        std::unique_lock<std::mutex> lock(pending_callbacks_mutex_);
        std::swap(pending_callbacks, pending_callbacks_);
    }

    for (auto& cb: pending_callbacks) {
        cb.invoke();
    }
}

#endif
