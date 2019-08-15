#ifndef __FIBRE_WORKER_HPP
#define __FIBRE_WORKER_HPP

#include <thread>

namespace fibre {

class Worker {
public:
    struct callback_t {
        void (*callback)(void*);
        void* ctx;
    };

    int init();
    int deinit();
    int register_event(int event_fd, short events, callback_t* callback);
    int deregister_event(int event_fd);

private:
    void task();

    int epoll_fd = -1;
    int stop_fd = -1;
    bool should_run = false;
    std::thread* thread = nullptr;
    size_t n_events = 0; // number of registered events (for debugging only)
};

}

#endif // __FIBRE_WORKER_HPP