#ifndef __FIBRE_DISPATCHER_HPP
#define __FIBRE_DISPATCHER_HPP

#include <fibre/closure.hpp>
#include <fibre/logging.hpp>
#include <fibre/stream.hpp>
#include <fibre/decoder.hpp>
#include <fibre/uuid.hpp>
#include <fibre/context.hpp>
#include <fibre/named_tuple.hpp>
#include <fibre/local_endpoint.hpp>
#include <fibre/calls.hpp>
#include <fibre/platform_support/linux_worker.hpp>
#include <fibre/platform_support/linux_event.hpp>

#include <algorithm>

DEFINE_LOG_TOPIC(DISPATCHER);
#define current_log_topic LOG_TOPIC_DISPATCHER

namespace fibre {

class Dispatcher {
public:
    int init() {
        if (worker_.init()) {
            return -1;
        }
        if (trigger_dispatcher_.init()) {
            return -1;
        }
        if (trigger_dispatcher_.subscribe(&worker_, &dispatch_obj)) {
            return -1;
        }
        return 0;
    }

    /**
     * Adds a call to the dispatcher. The call is removed when the cancellation
     * token is asserted.
     */
    void add_call(std::shared_ptr<outgoing_call_t> call) {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_calls_.push_back(call);
        trigger_dispatcher_.set();
    }

    void remove_call(std::shared_ptr<outgoing_call_t> call) {
        auto it = std::find_if(ready_calls_.begin(), ready_calls_.end(), [call](const std::shared_ptr<outgoing_call_t>& arg){ return arg->uuid == call->uuid; });
        ready_calls_.erase(it);
    }

    void add_tx_channel(std::shared_ptr<MultiFragmentEncoder> sink) {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_tx_channels_.push_back(sink);
        trigger_dispatcher_.set();
    }

private:
    void dispatch() {
        // TODO: acquire mutex
        // TODO: make dispatcher work
        FIBRE_LOG(D) << "will dispatch";
        for (auto& tx_it : ready_tx_channels_) {
            for (auto& call_it : ready_calls_) {
                FIBRE_LOG(D) << "dispatch";

                // Copy as much of the call as available to the fragmenter
                stream_copy_all(&call_it->fragment_source, &call_it->encoder);

                tx_it->encode_fragment(call_it.get(), 1);
                // TODO: we need to block this TX channel for a while for the chunk we just sent
                // Otherwise we would fire 100s of UDP packets.
                auto asd = call_it->fragment_source;
                bufptr_t buf = {.ptr = nullptr, .length = 1};
                asd.get_buffer(&buf);
                if (buf.length == 0) {
                    FIBRE_LOG(D) << "call finished";
                }
            }
        }
    }

    LinuxWorker worker_;
    LinuxAutoResetEvent trigger_dispatcher_{"dispatcher"};
    std::mutex mutex_;
    std::vector<std::shared_ptr<outgoing_call_t>> ready_calls_;
    std::vector<std::shared_ptr<MultiFragmentEncoder>> ready_tx_channels_;


    member_closure_t<decltype(&Dispatcher::dispatch)> dispatch_obj{&Dispatcher::dispatch, this};
};

extern Dispatcher main_dispatcher;

}

#undef current_log_topic

#endif // __FIBRE_DISPATCHER_HPP
