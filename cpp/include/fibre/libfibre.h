/**
 * @brief Fibre C library
 * 
 * The library is fully asynchronous and runs on an application-managed event
 * loop. This integration happens with the call to libfibre_open(), where the
 * application must pass a couple of functions that libfibre will use to put
 * tasks on the event loop.
 * 
 * Some general things to note:
 *  - None of the library's functions are blocking.
 *  - None of the library's functions can be expected to be thread-safe, they
 *    should not be invoked from any other thread than the one that runs the
 *    event loop.
 *  - Callbacks that the user passes to a libfibre function are always executed
 *    on the event loop thread.
 *  - All of the library's functions can be expected reentry-safe. That means
 *    you can call into any libfibre function from any callback handler that
 *    libfibre invokes.
 * 
 * 
 * 
 * DEPRECATION NOTES:
 * 
 * The following aspects of the libfibre API are expected or proposed to change
 * in future versions of the API.
 * 
 *  - The ability to add backends will be removed or replaced because the
 *    updated protocol requires a more high level backend interface than a
 *    simple raw byte stream.
 *    Currently only FibreJS adds external backend so we will try to change that.
 *  - The ability to integrate with an external event loop will probably be
 *    removed.
 *    The task-passing architecture (libfibre_run_tasks()) is a good foundation
 *    to pass data across thread boundaries, therefore libfibre should start its
 *    own internal thread to better utilize multi-core CPUs, and simplify
 *    language bindings, especially in cases where the application's event loop
 *    cannot easily be accessed (e.g. Dart).
 *    We can have a single argument-less callback/function that can be used by
 *    libfibre to wake the application's event loop and the other way around.
 *  - More libfibre functions might be incorporated into the task passing
 *    architecture (libfibre_run_tasks()) for above reason.
 *  - Object paths will be dynamic.
 */

#ifndef __LIBFIBRE_H
#define __LIBFIBRE_H

#include <stdint.h>
#include <stdlib.h>

#if defined(_MSC_VER)
#   define DLL_EXPORT __declspec(dllexport)
#   define DLL_IMPORT __declspec(dllimport)
#elif defined(__GNUC__)
#   define DLL_EXPORT __attribute__((visibility("default")))
#   define DLL_IMPORT
#   if __GNUC__ > 4
#       define DLL_LOCAL __attribute__((visibility("hidden")))
#   else
#       define DLL_LOCAL
#   endif
#else
#   error("Don't know how to export shared object libraries")
#endif

#ifdef FIBRE_COMPILE
#   ifndef FIBRE_PUBLIC
#       define FIBRE_PUBLIC DLL_EXPORT
#   endif
#   define FIBRE_PRIVATE DLL_LOCAL
#else
#   define FIBRE_PUBLIC DLL_IMPORT
#endif


#define FIBRE_PRIVATE DLL_LOCAL

#ifdef __cplusplus
extern "C" {
#endif

struct LibFibreCtx;
struct LibFibreDiscoveryCtx;
struct LibFibreCallContext;
struct LibFibreObject;
struct LibFibreInterface;
struct LibFibreFunction;
struct LibFibreDomain;

// This enum must remain identical to fibre::Status.
enum LibFibreStatus {
    kFibreOk,
    kFibreBusy, //<! The request will complete asynchronously
    kFibreCancelled, //!< The operation was cancelled due to a request by the application or the remote peer
    kFibreClosed, //!< The operation has finished orderly or shall be finished orderly
    kFibreInvalidArgument, //!< Bug in the application
    kFibreInternalError, //!< Bug in the local fibre implementation
    kFibreProtocolError, //!< A remote peer is misbehaving (indicates bug in the remote peer)
    kFibreHostUnreachable, //!< The remote peer can no longer be reached
    //kFibreInsufficientData, // maybe we will introduce this to tell the caller that the granularity of the data is too small
};

struct LibFibreVersion {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
};

struct LibFibreAttributeInfo {
    const char* name; //!< ASCII-encoded name of the attribute. Remains valid
                      //!< for as long as the containing `LibFibreAttributeInfo`
                      //!< is valid.
    size_t name_length; //!< Length of `name`.
    LibFibreInterface* intf; //!< Interface implemented by this attribute.
                             //!< Remains valid for at least as long as the
                             //!< containing interface remains valid.
};

struct LibFibreFunctionInfo {
    const char* name; //!< ASCII-encoded name of the function. Remains valid
                      //!< for as long as the containing `LibFibreFunctionInfo`
                      //!< is valid.
    size_t name_length; //!< Length of `name`.
    const char** input_names; //!< Names of the input arguments.
                              //!< Null-terminated list of null-terminated
                              //!< ASCII-encoded strings. The list and the
                              //!< string buffers are only valid for as long as
                              //!< the containing `LibFibreFunctionInfo` is
                              //!< valid.
    const char** input_codecs; //!< Names of the input codecs. The same
                               //!< conventions as for `input_names` apply.
    const char** output_names; //!< Names of the output names. The same
                               //!< conventions as for `input_names` apply.
    const char** output_codecs; //!< Names of the output codecs. The same
                                //! conventions as for `input_names` apply.
};

struct LibFibreInterfaceInfo {
    const char* name; //!< ASCII-encoded name of the interface. Remains valid
                      //!< for as long as the containing `LibFibreInterfaceInfo`
                      //!< is valid.
    size_t name_length; //!< Length of `name`.
    LibFibreAttributeInfo* attributes; //!< List of attributes contained by the
                                       //!< interface.
    size_t n_attributes; //!< Length of `attributes`.
    LibFibreFunction** functions; //!< List of functions implemented by the
                                  //!< interface.
    size_t n_functions; //!< Length of `functions`.
};

typedef int (*post_cb_t)(void (*callback)(void*), void* cb_ctx);
typedef int (*register_event_cb_t)(int fd, uint32_t events, void (*callback)(void*, uint32_t), void* cb_ctx);
typedef int (*deregister_event_cb_t)(int fd);
typedef int (*open_timer_cb_t)(struct LibFibreEventLoopTimer** timer, void (*callback)(void*), void* cb_ctx);
typedef int (*set_timer_cb_t)(struct LibFibreEventLoopTimer* timer, float interval, int mode);
typedef int (*close_timer_cb_t)(struct LibFibreEventLoopTimer* timer);

struct LibFibreEventLoop {
    /**
     * @brief Called by libfibre when it wants the application to run a callback
     * on the application's event loop.
     * 
     * This is the only callback that libfibre can invoke from a different
     * thread than the event loop thread itself. The application must ensure
     * that this callback is thread-safe.
     * This allows libfibre to run other threads internally while keeping
     * threading promises made to the application.
     */
    post_cb_t post;

    /**
     * @brief TODO: this is a Unix specific callback. Need to use IOCP on Windows.
     */
    register_event_cb_t register_event;

    /**
     * @brief TODO: this is a Unix specific callback. Need to use IOCP on Windows.
     */
    deregister_event_cb_t deregister_event;

    /**
     * DEPRECATED! (see top of this file)
     * see libfibre.py for a reference implementation of this callback
     */
    open_timer_cb_t open_timer;

    /**
     * DEPRECATED! (see top of this file)
     * see libfibre.py for a reference implementation of this callback
     */
    set_timer_cb_t set_timer;

    /**
     * DEPRECATED! (see top of this file)
     * see libfibre.py for a reference implementation of this callback
     */
    close_timer_cb_t close_timer;
};

struct LibFibreLogger {
    int verbosity;

    // TODO: document (see fibre.hpp for now)
    void(*log)(void* ctx, const char* file, unsigned line, int level, uintptr_t info0, uintptr_t info1, const char* text);
    void* ctx;
};

typedef uintptr_t LibFibreCallHandle;

enum LibFibreTaskType {
    kStartCall,
    kWrite,
    kWriteDone,
};

struct LibFibreChunk {
    unsigned char layer;
    unsigned char* begin;
    unsigned char* end;
};

struct LibFibreTask {
    LibFibreTaskType type;
    LibFibreCallHandle handle; //!< Identifies the call on which the task is to
                               //!< be run. For kStartCall tasks, this handle
                               //!< be freely chosen by the creator of the task.
                               //!< (i.e. the application for client side
                               //!< calls and libfibre for server side calls).

    union {
        /**
         * @brief Starts a new call on the specified function.
         * 
         * The call's resources are released once the call is closed in both
         * directions, that is, a write task with an empty buffer and a
         * status different from kFibreOk has been issued.
         * 
         * This task corresponds to Function::start_call() in the C++ API.
         */
        struct {
            LibFibreFunction* func;
            LibFibreDomain* domain;
        } start_call;

        /**
         * @brief Writes data to the specified ongoing call.
         * 
         * This task corresponds to Socket::write() in the C++ API.
         */
        struct {
            const unsigned char* b_begin;
            const LibFibreChunk* c_begin;
            const LibFibreChunk* c_end;
            int8_t elevation;
            LibFibreStatus status; //!< The status of the data source.
                                   //!< This status pertains to after the
                                   //!< provided data, for example if the
                                   //!< status is kFibreClosed, it means that
                                   //!< the call should be closed only after the
                                   //!< sink has processed all provided chunks.
        } write;

        struct {
            LibFibreStatus status;
            const LibFibreChunk* c_end;
            const unsigned char* b_end;
        } on_write_done;
    };
};

/**
 * @brief Callback type for the libfibre_open() `run_tasks_cb` argument.
 * 
 * Used by libfibre to post a batch of tasks to the application. If this results
 * in the application generating new tasks for libfibre without blocking, it can
 * immediately return those tasks to libfibre.
 * 
 * The semantics of this function are symmetric to `libfibre_run_tasks()`.
 * 
 * @param tasks: Pointer to an array of LibFibreTask structures. The array
 *        itself is only valid for the duration of the callback however the
 *        chunk buffers referenced by the tasks (if any) will remain valid until
 *        their corresponding on_write_done task is returned by the application.
 * @param out_tasks: Pointer to a pointer that can be set to an array of
 *        LibFibreTask structures. This array must remain valid until the next
 *        invokation of `run_tasks_cb` or libfibre_close(), whichever happens
 *        first.
 */
typedef void (*run_tasks_cb_t)(LibFibreCtx* ctx, LibFibreTask* tasks, size_t n_tasks, LibFibreTask** out_tasks, size_t* n_out_tasks);

/**
 * @brief on_found_object callback type for libfibre_start_discovery().
 * @param obj: The object handle.
 * @param intf: The interface handle. Valid for as long as any handle of an
 *        object that implements it is valid.
 * @param path: A human-readable string that indicates the physical location /
 *        path of the object.
 */
typedef void (*on_found_object_cb_t)(void*, LibFibreObject* obj, LibFibreInterface* intf, const char* path, size_t path_length);

/**
 * @brief on_lost_object callback type for libfibre_start_discovery().
 */
typedef void (*on_lost_object_cb_t)(void*, LibFibreObject* obj);

typedef void (*on_stopped_cb_t)(void*, LibFibreStatus);

/**
 * @brief Callback type for libfibre_call().
 * 
 * For an overview of the coroutine call control flow see libfibre_call().
 * 
 * @param ctx: The context pointer that was passed to libfibre_call().
 * @param tx_end: End of the range of data that was accepted by libfibre. This
 *        is always in the interval [tx_buf, tx_buf + tx_len] where `tx_buf` and
 *        `tx_len` are the arguments of the corresponding libfibre_call() call.
 * @param tx_end: End of the range of data that was returned by libfibre. This
 *        is always in the interval [rx_buf, rx_buf + rx_len] where `rx_buf` and
 *        `rx_len` are the arguments of the corresponding libfibre_call() call.
 * @param tx_buf: The application should set this to the next buffer to
 *        transmit. The buffer must remain valid until the next callback
 *        invokation.
 * @param tx_len: The length of tx_buf. Must be zero if tx_buf is NULL.
 * @param rx_buf: The application should set this to the buffer into which data
 *        should be written. The buffer must remain allocated until the next
 *        callback invokation.
 * @param rx_len: The length of rx_buf. Must be zero if rx_buf is NULL.
 * 
 * @retval kFibreOk: The application set tx_buf and rx_buf to valid or empty
 *         buffers and libfibre should invoke the callback again when it has
 *         made progress.
 * @retval kFibreBusy: The application cannot provide a new tx_buf or rx_buf at
 *         the moment. The application will eventually call libfibre_call() for
 *         this coroutine call again.
 * @retval kFibreClosed: The application may have returned non-empty buffers and
 *         if libfibre manages to fully handle these buffers it shall consider
 *         the call ended.
 * @retval kFibreCancelled: The application did not set valid tx and rx buffers
 *         and libfibre should consider the call cancelled. Libfibre will not
 *         invoke the callback anymore.
 */
typedef LibFibreStatus (*libfibre_call_cb_t)(void* ctx,
        LibFibreStatus status,
        const unsigned char* tx_end, unsigned char* rx_end,
        const unsigned char** tx_buf, size_t* tx_len,
        unsigned char** rx_buf, size_t* rx_len);

/**
 * @brief Returns the version of the libfibre library.
 * 
 * The returned struct must not be freed.
 * 
 * The version adheres to Semantic Versioning, that means breaking changes of
 * the ABI can be detected by an increment of the major version number (unless
 * it's zero).
 * 
 * Even if breaking changes are introduced, we promise to keep this function
 * backwards compatible.
 */
FIBRE_PUBLIC const struct LibFibreVersion* libfibre_get_version();

/**
 * @brief Opens and initializes a Fibre context.
 * 
 * @param event_loop: The event loop on which libfibre will run. Some function
 *        of the event loop can be left unimplemented (set to NULL) depending on
 *        the platform and the backends used (TODO: elaborate).
 *        The event loop must be single threaded and all calls to libfibre must
 *        happen on the event loop thread.
 * @param run_tasks_cb: Used by libfibre to post tasks to the application.
 *        See `run_tasks_cb_t` for details.
 * @param logger: A struct that contains a log function an a log verbosity.
 *        This function is used by libfibre to log debug and error information.
 *        The log function can be null, in which case all log output is discarded.
 */
FIBRE_PUBLIC struct LibFibreCtx* libfibre_open(LibFibreEventLoop event_loop, run_tasks_cb_t run_tasks_cb, LibFibreLogger logger);

/**
 * @brief Closes a context that was previously opened with libfibre_open().
 *
 * This function must not be invoked before all ongoing discovery processes
 * are stopped and all channels are closed.
 */
FIBRE_PUBLIC void libfibre_close(struct LibFibreCtx* ctx);

/**
 * @brief Creates a communication domain from the specified spec string.
 * 
 * @param ctx: The libfibre context that was obtained from libfibre_open().
 * @param specs: Pointer to an ASCII string encoding the channel specifications.
 *        Must remain valid for the life time of the discovery.
 *        See README of the main Fibre repository for details.
 *        (https://github.com/samuelsadok/fibre/tree/devel).
 * @returns: An opaque handle which can be passed to libfibre_start_discovery().
 */
FIBRE_PUBLIC LibFibreDomain* libfibre_open_domain(LibFibreCtx* ctx,
    const char* specs, size_t specs_len);

/**
 * @brief Closes a domain that was previously opened with libfibre_open_domain().
 */
FIBRE_PUBLIC void libfibre_close_domain(LibFibreDomain* domain);

/**
 * @brief Opens a platform-specific interactive dialog to request access to a
 * device or resource.
 * 
 * This is only relevant in some sandboxed environments where libfibre doesn't
 * have access to all devices by default. For instance when running in a
 * webbrowser, libfibre's usb backend (WebUSB) doesn't have access to any USB
 * devices by default. Calling this function will display the browser's USB
 * device selection dialog. If the user selects a device, that device will be
 * included in the current ongoing (and future) discovery processes.
 * 
 * Usually this function must be called as a result of user interaction, such
 * as a button press.
 * 
 * @param backend: The backend for which to open the dialog.
 */
FIBRE_PUBLIC void libfibre_show_device_dialog(LibFibreDomain* domain, const char* backend);

/**
 * @brief Starts looking for Fibre objects that match the specifications.
 *
 * @param domain: The domain obtained from libfibre_open_domain() on which to
 *        discover objects.
 * @param on_found_object: Invoked for every matching object that is found.
 *        The application must expect the same object handle to appear more than
 *        once.
 *        libfibre increments the internal reference count of the object before
 *        this call and decrements it after the corresponding call to
 *        on_lost_object. When the reference count reaches zero the application
 *        must no longer use it. The reference count is always non-negative.
 * @param on_lost_object: Invoked when an object is lost.
 * @param on_stopped: Invoked when the discovery stops for any reason, including
 *        a corresponding call to libfibre_stop_discovery().
 * @param cb_ctx: Arbitrary user data passed to the callbacks.
 * @returns: An opaque handle which should be passed to libfibre_stop_discovery().
 */
FIBRE_PUBLIC void libfibre_start_discovery(LibFibreDomain* domain,
    LibFibreDiscoveryCtx** handle, on_found_object_cb_t on_found_object,
    on_lost_object_cb_t on_lost_object,
    on_stopped_cb_t on_stopped, void* cb_ctx);

/**
 * @brief Stops an ongoing discovery process that was previously started with
 * libfibre_start_discovery().
 *
 * The discovery is stopped asynchronously. That means it must still be
 * considered ongoing until the on_stopped callback which was passed to
 * libfibre_start_discovery() is invoked. Once this callback is invoked,
 * libfibre_stop_discovery() must no longer be called.
 */
FIBRE_PUBLIC void libfibre_stop_discovery(LibFibreDiscoveryCtx* handle);

/**
 * @brief Returns information about a function.
 * 
 * The returned object must eventually be freed by calling 
 * `libfibre_free_function_info()`.
 * 
 * @param func: A function handle that was obtained from
 *        libfibre_get_interface_info().
 * @returns: A pointer to a struct containing information about the function.
 *           Null if the information could not be retrieved.
 */
FIBRE_PUBLIC LibFibreFunctionInfo* libfibre_get_function_info(LibFibreFunction* func);

/**
 * @brief Frees a function info object created by libfibre_get_function_info().
 */
FIBRE_PUBLIC void libfibre_free_function_info(LibFibreFunctionInfo* info);

/**
 * @brief Returns information about an interface.
 * 
 * The returned object must eventually be freed by calling 
 * `libfibre_free_interface_info()`.
 * 
 * @param intf: An interface handle that was obtained in the callback of
 *        libfibre_start_discovery().
 * @returns: A pointer to a struct containing information about the interface.
 *           Null if the information could not be retrieved.
 */
FIBRE_PUBLIC LibFibreInterfaceInfo* libfibre_get_interface_info(LibFibreInterface* intf);

/**
 * @brief Frees an interface info object created by libfibre_get_interface_info().
 */
FIBRE_PUBLIC void libfibre_free_interface_info(LibFibreInterfaceInfo* info);

/**
 * @brief Returns the object that corresponds the the specified attribute of
 * another object.
 * 
 * This function runs purely locally and therefore returns a result immediately.
 * 
 * TODO: it might be useful to allow this operation to go through to the remote
 * device.
 * TODO: Specify whether the returned object handle must be identical for
 * repeated calls.
 * 
 * @param intf: The interface under which to look up the attribute. Currently
 *        each object only implements a single interface but this might change
 *        in the future.
 * @param parent_obj: An object handle that was obtained in the callback of
 *        libfibre_start_discovery() or from a previous call to
 *        libfibre_get_attribute().
 * @param attr_id: The attribute index pertaining to the attribute list returned
 *        by `libfibre_get_interface_info()`.
 * @param child_obj_ptr: If and only if the function succeeds, the variable that
 *        this argument points to is set to the requested subobject. The returned
 *        object handle is only guaranteed to remain valid for as long as the
 *        parent object handle is valid.
 * @returns: kFibreOk or kFibreInvalidArgument
 */
FIBRE_PUBLIC LibFibreStatus libfibre_get_attribute(LibFibreInterface* intf, LibFibreObject* parent_obj, size_t attr_id, LibFibreObject** child_obj_ptr);

/**
 * @brief Posts a batch of tasks to libfibre and receives a batch of return
 * tasks for the application.
 * 
 * The semantics of this function are symmetric to the `run_tasks_cb` argument
 * of `libfibre_open()`.
 * 
 * @param tasks: Pointer to an array of LibFibreTask structures. The array
 *        itself can be freed after the libfibre_run_tasks() call however the
 *        chunk buffers referenced by the tasks (if any) must remain valid until
 *        their corresponding on_write_done task is returned.
 * @param out_tasks: Pointer to a pointer that will be set to an array of
 *        LibFibreTask structures. This array will remain valid until the next
 *        call to libfibre_run_tasks() or libfibre_close(), whichever happens
 *        first.
 */
FIBRE_PUBLIC void libfibre_run_tasks(LibFibreCtx* ctx, LibFibreTask* tasks, size_t n_tasks, LibFibreTask** out_tasks, size_t* n_out_tasks);

#ifdef __cplusplus
}
#endif

#endif // __LIBFIBRE_H