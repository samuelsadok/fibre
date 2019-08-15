
#include <fibre/worker.hpp>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <string.h>

using namespace fibre;

/**
 * @brief Starts the worker thread(s).
 * 
 * From this point on until deinit() the worker will start handling events that
 * are associated with this worker using register().
 */
int Worker::init() {
    if (thread)
        return 1;

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        printf("epoll_create1() failed\n");
        goto fail0;
    }

    stop_fd = eventfd(0, 0);
    if (stop_fd < 0) {
        printf("eventfd() failed\n");
        goto fail1;
    }

    if (register_event(stop_fd, EPOLLIN, nullptr) != 0) {
        printf("register_event(stop_fd) failed\n");
        goto fail2;
    }

    should_run = true;
    thread = new std::thread(&Worker::task, this);
    return 0;

fail2:
    close(stop_fd);
fail1:
    close(epoll_fd);
fail0:
    return -1;
}

/**
 * @brief Terminates all worker threads and closses the epoll instance.
 * 
 * If not all events are deregistered at the time of this call, the function
 * returns an error code and the behavior us undefined.
 */
int Worker::deinit() {
    if (!thread)
        return -1;

    int result = 0;

    should_run = false;
    const uint64_t val = 1;
    if (write(stop_fd, &val, sizeof(val)) != sizeof(val)) {
        printf("write() failed\n");
        result = -1;
    }

    thread->join();
    delete thread;

    if (deregister_event(stop_fd) != 0) {
        printf("deregister_event(stop_fd) failed\n");
        result = -1;
    }

    if (n_events) {
        printf("Warning: closed epoll instance before all events were deregistered.\n");
        result = -1;
    }

    if (close(stop_fd) != 0) {
        printf("close() failed\n");
        result = -1;
    }

    if (close(epoll_fd) != 0) {
        printf("close() failed\n");
        result = -1;
    }

    return result;
}

int Worker::register_event(int event_fd, short events, callback_t* callback) {
    if (event_fd < 0) {
        return -1;
    }

    struct epoll_event ev = {
        .events = events,
        .data = { .ptr = callback }
    };

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_fd, &ev) != 0) {
        printf("epoll_ctl() failed\n");
        return -1;
    }

    n_events++;
    return 0;
}

int Worker::deregister_event(int event_fd) {
    int result = 0;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event_fd, nullptr) != 0) {
        printf("epoll_ctl() failed\n");
        result = -1;
    }

    n_events--;
    return result;
}

void Worker::task() {
    const size_t max_events = 1; // max events that can handled with a single syscall
    struct epoll_event events[max_events];

    while (should_run) {
        //printf("epoll_wait...\n");
        int n_events;
        do {
            n_events = epoll_wait(epoll_fd, events, max_events, -1);
        } while (n_events < 0 && errno == EINTR); // ignore syscall interruptions. This happens for instance during suspend.

        if (n_events <= 0) {
            printf("epoll_wait() failed with %d (%s). Terminating worker thread.\n", n_events, strerror(errno));
            break;
        }

        for (int i = 0; i < n_events; ++i) {
            callback_t* callback = (callback_t*)events[i].data.ptr;
            if (callback && callback->callback) {
                callback->callback(callback->ctx);
            }
        }
    }
}
