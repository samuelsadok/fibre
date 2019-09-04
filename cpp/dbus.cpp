
#include <fibre/dbus.hpp>
#include <fibre/timer.hpp>
#include <fibre/logging.hpp>
#include <sys/epoll.h>

using namespace fibre;

USE_LOG_TOPIC(DBUS);

struct watch_ctx_t {
    DBusConnectionWrapper* conn;
    DBusWatch* watch;
    Worker::callback_t callback;
};

struct timeout_ctx_t {
    DBusConnectionWrapper* conn;
    DBusTimeout* timeout;
    Timer timer;
    Timer::callback_t callback;
};


static void handle_watch(void* ctx, uint32_t events) {
    FIBRE_LOG(D) << "handle watch";
    watch_ctx_t* watch_ctx = (watch_ctx_t*)ctx;
    unsigned int flags = 0;
    if (events & EPOLLIN)
        flags |= DBUS_WATCH_READABLE;
    if (events & EPOLLOUT)
        flags |= DBUS_WATCH_WRITABLE;
    if (events & EPOLLHUP)
        flags |= DBUS_WATCH_HANGUP;
    if (events & EPOLLERR)
        flags |= DBUS_WATCH_ERROR;
    if (!dbus_watch_handle(watch_ctx->watch, flags)) {
        FIBRE_LOG(E) << "dbus_watch_handle() failed";
    }
    dbus_connection_dispatch(watch_ctx->conn->get_libdbus_ptr());
}

static void handle_timer(void* ctx) {
    FIBRE_LOG(D) << "handle timer";
    timeout_ctx_t* timeout_ctx = (timeout_ctx_t*)ctx;
    if (!dbus_timeout_handle(timeout_ctx->timeout)) {
        FIBRE_LOG(E) << "dbus_timeout_handle() failed";
    }
    dbus_connection_dispatch(timeout_ctx->conn->get_libdbus_ptr());
}

int DBusConnectionWrapper::init(Worker* worker) {
    if (!worker)
        return -1;
    worker_ = worker;
    
    dbus_bool_t status;

    // initialise the errors
    dbus_error_init(&err_);

    // connect to the bus
    conn_ = dbus_bus_get(DBUS_BUS_SYSTEM, &err_);
    if (dbus_error_is_set(&err_)) {
        FIBRE_LOG(E) << "dbus_bus_get() failed: " << err_.message;
        goto fail1;
    } else if (NULL == conn_) {
        FIBRE_LOG(E) << "dbus_bus_get() failed (retured NULL)";
        goto fail1;
    }

    /*// request a name on the bus
    ret = dbus_bus_request_name(conn_, "test.method.server", 
            DBUS_NAME_FLAG_ALLOW_REPLACEMENT, &err_);
    if (dbus_error_is_set(&err_)) {
        FIBRE_LOG(E) << "Name Error: " << err_.message;
        dbus_error_free(&err_);
    }
    if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret) {
        exit(1);
    }*/

    status = dbus_connection_set_watch_functions(conn_,
        [](DBusWatch* watch, void* data) { /* add_function */
            return ((DBusConnectionWrapper*)data)->handle_add_watch(watch) == 0 ?
                   (dbus_bool_t)TRUE : (dbus_bool_t)FALSE;
        },
        [](DBusWatch* watch, void* data) { /* remove_function */
            ((DBusConnectionWrapper*)data)->handle_remove_watch(watch);
        },
        [](DBusWatch* watch, void* data) { /* toggled_function */
            ((DBusConnectionWrapper*)data)->handle_toggle_watch(watch, dbus_watch_get_enabled(watch));
        },
        this, /* data */
        [](void*){} /* free_data_function */
    );
    if (!status) {
        FIBRE_LOG(E) << "dbus_connection_set_watch_functions() failed";
        goto fail2;
    }

    status = dbus_connection_set_timeout_functions(conn_,
        [](DBusTimeout* timeout, void* data) { /* */
            return ((DBusConnectionWrapper*)data)->handle_add_timeout(timeout) == 0 ?
                   (dbus_bool_t)TRUE : (dbus_bool_t)FALSE;
        },
        [](DBusTimeout* timeout, void* data) { /* remove_function */
            ((DBusConnectionWrapper*)data)->handle_remove_timeout(timeout);
        },
        [](DBusTimeout* timeout, void* data) { /* toggled_function */
            ((DBusConnectionWrapper*)data)->handle_toggle_timeout(timeout, dbus_timeout_get_enabled(timeout));
        },
        this, /* data */
        [](void*){} /* free_data_function */
    );
    if (!status) {
        FIBRE_LOG(E) << "dbus_connection_set_timeout_functions() failed";
        goto fail2;
    }

    if (dispatch_signal_.init(worker_, &dispatch_callback_obj_) != 0) {
        FIBRE_LOG(E) << "dispatch signal init failed";
        goto fail2;
    }

    // TODO: ask on mailing list what this is exactly supposed to do. What
    // assumption do they have about my mainloop? That it calls
    // dbus_connection_dispatch()?
    dbus_connection_set_wakeup_main_function(conn_,
        [](void* data) {
            ((DBusConnectionWrapper*)data)->dispatch_signal_.set();
        }, this, nullptr);

    dbus_connection_set_dispatch_status_function(conn_,
        [](DBusConnection* conn_, DBusDispatchStatus new_status, void* data) {
            if (new_status == DBUS_DISPATCH_DATA_REMAINS)
                ((DBusConnectionWrapper*)data)->dispatch_signal_.set();
        }, this, nullptr);

    if (dbus_connection_get_dispatch_status(conn_) == DBUS_DISPATCH_DATA_REMAINS)
        dispatch_signal_.set();

    return 0;

fail2:
    dbus_connection_close(conn_);
fail1:
    dbus_error_free(&err_);
    return -1;
}

int DBusConnectionWrapper::deinit() {
    int result = 0;

    // TODO: wait until DISCONNECT message is received
    FIBRE_LOG(D) << "will close connection";
    dbus_connection_close(conn_);
    dbus_connection_unref(conn_);
    FIBRE_LOG(D) << "connection closed";

    dbus_connection_set_watch_functions(conn_, nullptr, nullptr, nullptr, nullptr, nullptr);
    dbus_connection_set_timeout_functions(conn_, nullptr, nullptr, nullptr, nullptr, nullptr);
    dbus_connection_set_dispatch_status_function(conn_, nullptr, nullptr, nullptr);
    dbus_connection_set_wakeup_main_function(conn_, nullptr, nullptr, nullptr);

    if (dispatch_signal_.deinit() != 0) {
        FIBRE_LOG(E) << "signal deinit failed";
        result = -1;
    }

    dbus_error_free(&err_);
    return result;
}

int DBusConnectionWrapper::handle_add_watch(DBusWatch *watch) {
    FIBRE_LOG(D) << "add watch";
    /* TODO: The documentation says:
     * "dbus_watch_handle() cannot be called during the
     * DBusAddWatchFunction, as the connection will not be ready to
     * handle that watch yet."
     * The event could trigger as soon as the watch is registered.
     * Need to ask the developers if that's ok.
     */


    watch_ctx_t* ctx = new watch_ctx_t {
        .conn = this, .watch = watch, .callback = { handle_watch, nullptr }
    };
    ctx->callback.ctx = ctx;
    dbus_watch_set_data(watch, ctx, nullptr);

    // If the watch is already supposed to be enabled, toggle it on now
    if (dbus_watch_get_enabled(watch)) {
        return handle_toggle_watch(watch, true);
    } else {
        return 0;
    }
}

void DBusConnectionWrapper::handle_remove_watch(DBusWatch *watch) {
    FIBRE_LOG(D) << "remove watch";
    // If the watch was not already disabled, disable it now
    if (dbus_watch_get_enabled(watch)) {
        handle_toggle_watch(watch, false);
    }
    
    watch_ctx_t* ctx = (watch_ctx_t*)dbus_watch_get_data(watch);
    dbus_watch_set_data(watch, nullptr, nullptr);
    delete ctx;
}

int DBusConnectionWrapper::handle_toggle_watch(DBusWatch *watch, bool enable) {
    if (enable) {
        // DBusWatch was enabled - register it with the worker
        int fd = dbus_watch_get_unix_fd(watch);

        uint32_t events = 0;
        unsigned int flags = dbus_watch_get_flags(watch);
        if (flags & DBUS_WATCH_READABLE)
            events |= EPOLLIN;
        if (flags & DBUS_WATCH_WRITABLE)
            events |= EPOLLOUT;

        watch_ctx_t* ctx = (watch_ctx_t*)dbus_watch_get_data(watch);
        return worker_->register_event(fd, events, &ctx->callback);

    } else {
        // DBusWatch was disabled - remove it from the worker
        int fd = dbus_watch_get_unix_fd(watch);
        return worker_->deregister_event(fd);
    }
}

int DBusConnectionWrapper::handle_add_timeout(DBusTimeout* timeout) {
    FIBRE_LOG(D) << "add timeout";
    timeout_ctx_t* ctx = new timeout_ctx_t{
        this, timeout, {} /* timer */, { handle_timer, nullptr }
    };
    ctx->callback.ctx = ctx;

    ctx->timer.init(worker_);
    dbus_timeout_set_data(timeout, ctx, nullptr);

    // If the timeout is already supposed to be enabled, toggle it on now
    if (dbus_timeout_get_enabled(timeout)) {
        return handle_toggle_timeout(timeout, true);
    } else {
        return 0;
    }
}

void DBusConnectionWrapper::handle_remove_timeout(DBusTimeout* timeout) {
    FIBRE_LOG(D) << "remove timeout";
    if (dbus_timeout_get_enabled(timeout)) {
        handle_toggle_timeout(timeout, false);
    }

    timeout_ctx_t* ctx = (timeout_ctx_t*)dbus_timeout_get_data(timeout);
    dbus_timeout_set_data(timeout, nullptr, nullptr);
    ctx->timer.deinit();
    delete ctx;
}

int DBusConnectionWrapper::handle_toggle_timeout(DBusTimeout *timeout, bool enable) {
    timeout_ctx_t* ctx = (timeout_ctx_t*)dbus_timeout_get_data(timeout);
    if (enable) {
        // Timeout was enabled - start timer
        int interval_ms = dbus_timeout_get_interval(timeout);
        return ctx->timer.start(interval_ms, true, &ctx->callback);
    } else {
        return ctx->timer.stop();
    }
}

