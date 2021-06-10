#ifndef __FIBRE_HPP
#define __FIBRE_HPP

namespace fibre {

struct Fibre;

}

#include <fibre/config.hpp>
#include <fibre/domain.hpp>
#include <fibre/event_loop.hpp>
#include <fibre/interface.hpp>
#include <fibre/logging.hpp>
#include <string>
#include <memory>

namespace fibre {

struct Backend; // defined in channel_discoverer.hpp
class ChannelDiscoverer; // defined in channel_discoverer.hpp

struct Fibre {
    size_t n_domains = 0;
    EventLoop* event_loop;
    Logger logger = Logger::none();

#if FIBRE_ALLOW_HEAP
    std::unordered_map<std::string, ChannelDiscoverer*> discoverers;
#endif

    /**
     * @brief Creates a domain on which objects can subsequently be published
     * and discovered.
     * 
     * This potentially starts looking for channels on this domain.
     */
    Domain* create_domain(std::string specs, const uint8_t* node_id, F_CONFIG_ENABLE_SERVER_T enable_server);
    void close_domain(Domain* domain);

    RichStatus register_backend(std::string name, ChannelDiscoverer* backend);
    RichStatus deregister_backend(std::string name);

    // internal use
    RichStatus init_backend(std::string name, Backend* backend);
    RichStatus deinit_backends();
};




/**
 * @brief Opens and initializes a Fibre context.
 * 
 * If FIBRE_ALLOW_HEAP=0 only one Fibre context can be open at a time.
 * 
 * @param logger: A logger that receives debug/warning/error events from Fibre.
 *        If compiled with FIBRE_ENABLE_TEXT_LOGGING=0 the text parameter is
 *        always NULL.
 *        If you don't have special logging needs consider passing
 *        `fibre::log_to_stderr`.
 * @returns: A non-null pointer on success, null otherwise.
 */
RichStatus open(EventLoop* event_loop, Logger logger, Fibre** p_ctx);

void close(Fibre*);

/**
 * @brief Logs an event to stderr.
 * 
 * If Fibre is compiled with FIBRE_ENABLE_TEXT_LOGGING=1 this function logs the
 * event to stderr. Otherwise it does nothing.
 */
void log_to_stderr(void* ctx, const char* file, unsigned line, int level, uintptr_t info0, uintptr_t info1, const char* text);

/**
 * @brief Launches an event loop on the current thread.
 * 
 * This function returns when the event loop becomes empty.
 * 
 * If FIBRE_ALLOW_HEAP=0 only one event loop can be running at a time.
 * 
 * This function returns false if Fibre was compiled with
 * FIBRE_ENABLE_EVENT_LOOP=0.
 * 
 * @param on_started: This function is the first event that is placed on the
 *        event loop. This function usually creates further events, for instance
 *        by calling open().
 * @returns: true if the event loop ran to completion. False if this function is
 *           not implemented on this operating system or if another error
 *           occurred.
 */
RichStatus launch_event_loop(Logger logger, Callback<void, EventLoop*> on_started);

}

#endif // __FIBRE_HPP