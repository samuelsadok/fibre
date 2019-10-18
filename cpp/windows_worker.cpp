
#include <fibre/windows_worker.hpp>
#include <fibre/logging.hpp>

#include <sys/types.h>
#include <windows.h>
#include <string.h>

using namespace fibre;

DEFINE_LOG_TOPIC(WORKER);
USE_LOG_TOPIC(WORKER);

/**
 * @brief Starts the worker thread(s).
 * 
 * From this point on until deinit() the worker will start handling events that
 * are associated with this worker using register().
 */
int WindowsIOCPWorker::init() {
    if (thread_)
        return 1;

    h_completion_port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (h_completion_port_ == NULL) {
        FIBRE_LOG(E) << "CreateIoCompletionPort() failed: " << sys_err();
        goto fail0;
    }

    should_run_ = true;
    thread_ = new std::thread(&WindowsIOCPWorker::event_loop, this);
    return 0;

fail0:
    return -1;
}

/**
 * @brief Terminates all worker threads and closses the epoll instance.
 * 
 * If not all events are deregistered at the time of this call, the function
 * returns an error code and the behavior us undefined.
 */
int WindowsIOCPWorker::deinit() {
    if (!thread_)
        return -1;

    int result = 0;

    should_run_ = false;
    if (!PostQueuedCompletionStatus(h_completion_port_, 0, 0, NULL)) {
        FIBRE_LOG(E) << "PostQueuedCompletionStatus() failed: " << sys_err();
        result = -1;
    }

    FIBRE_LOG(D) << "wait for worker thread...";
    thread_->join();
    delete thread_;
    FIBRE_LOG(D) << "worker thread finished";

    if (n_events_) {
        FIBRE_LOG(W) << "closed epoll instance before all events were deregistered.";
        result = -1;
    }

    if (CloseHandle(h_completion_port_) != 0) {
        FIBRE_LOG(E) << "CloseHandle() failed: " << sys_err();
        result = -1;
    }
    h_completion_port_ = INVALID_HANDLE_VALUE;

    return result;
}

void WindowsIOCPWorker::event_loop() {
    while (should_run_) {
        iterations_++;

        uintptr_t completion_key;
        if (!GetQueuedCompletionStatus(h_completion_port_, NULL, &completion_key, NULL, INFINITE)) {
            FIBRE_LOG(E) << "GetQueuedCompletionStatus() failed: " << sys_err() << " - Terminating worker thread.";
            break;
        }

        callback_t* callback = (callback_t*)completion_key;
        if (callback) {
            (*callback)();
        }
    }
}
