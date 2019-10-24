#ifndef __FIBRE_WINDOWS_WORKER_HPP
#define __FIBRE_WINDOWS_WORKER_HPP

#include <fibre/closure.hpp>
#include <thread>
#include <unordered_map>
#include <ioapiset.h>

namespace fibre {

/**
 * @brief Implements a worker based on the Windows IOCP API.
 * 
 * The worker can therefore be used with any type of waitable object that is
 * represented as file or socket handle.
 * 
 * Thread safety: None of the public functions are thread-safe with respect to
 * each other. However they are thread safe with respect to the internal event
 * loop, that means register_event() and deregister_event() can be called from
 * within an event callback (which executes on the event loop thread), provided
 * those calls are properly synchronized with calls from other threads.
 */
class WindowsIOCPWorker {
public:
    using callback_t = Callback<int, LPOVERLAPPED>;

    int init();
    int deinit();

    /**
     * @brief Duplicates the given file or socket handle in order to register
     * it with this I/O completion port.
     * 
     * The duplication is necessary because a handle cannot be deregistered
     * without closing it.
     * 
     * The source file handle given to this function can be registered with
     * multiple workers/callbacks (e.g. once for write operations and once for
     * read operations) but the new handle returned by this function must not be
     * registered again.
     * 
     * @brief handle: A pointer to a valid file handle or socket ID. After
     *        successful registration of the handle, the variable will be
     *        overwritten by the duplicated handle. To trigger the worker, the
     *        new handle must be used for I/O operations.
     *        If the function fails, the handle will not be modified.
     * @brief callback: The callback to be invoked when the handle is ready.
     *        The callback must remain valid until the associated handle is
     *        closed.
     */
    int register_object(HANDLE* hFile, callback_t* callback);

    /**
     * @brief Unregisters the given file or socket handle.
     * 
     * The associated callback will not be invoked anymore after this function
     * completes.
     * 
     * @brief handle: A pointer to the file handle that was returned by
     *        register_object. After successful deregistration, the variable
     *        will be overwritten by the original file handle that was passed
     *        to register_object().
     */
    int deregister_object(HANDLE* hFile);

private:
    void event_loop();
    void stop_handler();

    HANDLE h_completion_port_ = NULL;
    //LinuxAutoResetEvent stop_signal_ = LinuxAutoResetEvent("stop");
    bool should_run_ = false;
    volatile unsigned int iterations_ = 0;
    std::thread* thread_ = nullptr;
    std::unordered_map<HANDLE, HANDLE> handles_;
    size_t n_events_ = 0; // number of registered events (for debugging only)
};

}

#endif // __FIBRE_WINDOWS_WORKER_HPP