
#include <fibre/dbus.hpp>
#include <fibre/timer.hpp>
#include <fibre/logging.hpp>
#include <sys/epoll.h>

using namespace fibre;

USE_LOG_TOPIC(DBUS);


int DBusConnectionWrapper::init(Worker* worker, bool system_bus) {
    if (!worker)
        return -1;
    worker_ = worker;
    
    dbus_bool_t status;

    // initialise the errors
    dbus_error_init(&err_);

    // connect to the bus
    conn_ = dbus_bus_get(system_bus ? DBUS_BUS_SYSTEM : DBUS_BUS_SESSION, &err_);
    if (dbus_error_is_set(&err_)) {
        FIBRE_LOG(E) << "dbus_bus_get() failed: " << err_.message;
        goto fail1;
    } else if (NULL == conn_) {
        FIBRE_LOG(E) << "dbus_bus_get() failed (retured NULL)";
        goto fail1;
    }

    FIBRE_LOG(D) << "my name on the bus is " << dbus_bus_get_unique_name(conn_);

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

    if (dispatch_signal_.init(worker_, &handle_dispatch_obj_) != 0) {
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

    status = dbus_connection_add_filter(conn_, handle_method_call_stub, this, nullptr);
    if (!status) {
        FIBRE_LOG(E) << "failed to add filter";
        return -1;
    }

    if (dbus_connection_get_dispatch_status(conn_) == DBUS_DISPATCH_DATA_REMAINS)
        dispatch_signal_.set();

    return 0;

fail2:
    dbus_connection_unref(conn_);
fail1:
    dbus_error_free(&err_);
    return -1;
}

int DBusConnectionWrapper::deinit() {
    int result = 0;

    // TODO: ensure all method calls are finished

    dbus_connection_remove_filter(conn_, handle_method_call_stub, this);
    dbus_connection_set_watch_functions(conn_, nullptr, nullptr, nullptr, nullptr, nullptr);
    dbus_connection_set_timeout_functions(conn_, nullptr, nullptr, nullptr, nullptr, nullptr);
    dbus_connection_set_dispatch_status_function(conn_, nullptr, nullptr, nullptr);
    dbus_connection_set_wakeup_main_function(conn_, nullptr, nullptr, nullptr);
    
    FIBRE_LOG(D) << "will close connection";
    //dbus_connection_close(conn_);
    dbus_connection_unref(conn_);
    FIBRE_LOG(D) << "connection closed";

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


    watch_ctx_t* ctx = new watch_ctx_t{&DBusConnectionWrapper::handle_watch, {this, watch}};
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
        return worker_->register_event(fd, events, ctx);

    } else {
        // DBusWatch was disabled - remove it from the worker
        int fd = dbus_watch_get_unix_fd(watch);
        return worker_->deregister_event(fd);
    }
}

void DBusConnectionWrapper::handle_watch(DBusWatch* watch, uint32_t events) {
    FIBRE_LOG(D) << "handle watch";
    unsigned int flags = 0;
    if (events & EPOLLIN)
        flags |= DBUS_WATCH_READABLE;
    if (events & EPOLLOUT)
        flags |= DBUS_WATCH_WRITABLE;
    if (events & EPOLLHUP)
        flags |= DBUS_WATCH_HANGUP;
    if (events & EPOLLERR)
        flags |= DBUS_WATCH_ERROR;
    if (!dbus_watch_handle(watch, flags)) {
        FIBRE_LOG(E) << "dbus_watch_handle() failed";
    }
    dbus_connection_dispatch(get_libdbus_ptr());
}

int DBusConnectionWrapper::handle_add_timeout(DBusTimeout* timeout) {
    FIBRE_LOG(D) << "add timeout";
    timeout_ctx_t* ctx = new timeout_ctx_t{
        {} /* timer */, { &DBusConnectionWrapper::handle_timeout, {this, timeout}}
    };

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

void DBusConnectionWrapper::handle_timeout(DBusTimeout* timeout) {
    FIBRE_LOG(D) << "handle timer";
    if (!dbus_timeout_handle(timeout)) {
        FIBRE_LOG(E) << "dbus_timeout_handle() failed";
    }
    dbus_connection_dispatch(get_libdbus_ptr());
}

void DBusConnectionWrapper::handle_dispatch() {
    do {
        FIBRE_LOG(D) << "dispatch";
    } while (dbus_connection_dispatch(get_libdbus_ptr()) == DBUS_DISPATCH_DATA_REMAINS);
};

DBusHandlerResult DBusConnectionWrapper::handle_method_call(DBusMessage* rx_msg) {
    if (dbus_message_get_type(rx_msg) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
        //FIBRE_LOG(D) << "rx_msg: " << *rx_msg;
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    FIBRE_LOG(D) << "method call received: " << *rx_msg;

    const char* interface_name = dbus_message_get_interface(rx_msg);
    const char* method_name = dbus_message_get_member(rx_msg);
    const char* object_path = dbus_message_get_path(rx_msg);
    const char* signature = dbus_message_get_signature(rx_msg);

    if (!interface_name || !method_name || !object_path) {
        FIBRE_LOG(W) << "malformed method call: " << *rx_msg;
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    ExportTableBase* interface = interface_table[interface_name];
    if (!interface) {
        FIBRE_LOG(W) << "method call for unknown interface " << interface_name << " received";
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // Fetch object pointer and type ID of object
    obj_table_entry_t& entry = object_table[object_path];
    if (entry.type_id == dbus_type_id_t{} || entry.ptr == nullptr) {
        FIBRE_LOG(W) << "object " << object_path << " unknown";
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // Find the function pointer that implements the function for this object
    FunctionImplTable method_table = (*interface)[method_name]; // TODO: this generates an instance of a dictionary, which may be undesired
    auto unpack_invoke_pack = method_table[entry.type_id];
    if (!unpack_invoke_pack) {
        FIBRE_LOG(W) << "method " << interface_name << "." << method_name << " not implemented for object " << object_path << " (internal type " << entry.type_id << ")";
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // Prepare reply message
    DBusMessage* tx_msg = dbus_message_new_method_return(rx_msg);
    if (!tx_msg) {
        FIBRE_LOG(E) << "reply msg NULL. Will not send reply.";
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }

    if (unpack_invoke_pack(entry.ptr, rx_msg, tx_msg) != 0) {
        FIBRE_LOG(W) << "method call failed";
        dbus_message_unref(tx_msg);
        tx_msg = dbus_message_new_error(rx_msg, "io.fibre.DBusServerError", "the method call failed on the server");
    }

    if (!dbus_connection_send(get_libdbus_ptr(), tx_msg, nullptr)) {
        FIBRE_LOG(E) << "failed to send reply";
    } else {
        FIBRE_LOG(D) << "method call was handled successfully";
    }

    dbus_message_unref(tx_msg);
    return DBUS_HANDLER_RESULT_HANDLED;
}
