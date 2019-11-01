
#include <fibre/calls.hpp>
#include <fibre/logging.hpp>
#include <fibre/dispatcher.hpp>

USE_LOG_TOPIC(CALLS);

namespace fibre {

// TODO: replace with fixed size data structure
using fragmented_calls_t = std::unordered_map<call_id_t, incoming_call_t>;
fragmented_calls_t fragmented_calls{};

int start_or_get_call(Context* ctx, call_id_t call_id, incoming_call_t** call) {
    incoming_call_t* dummy;
    call = call ? call : &dummy;

    incoming_call_t new_call = {
        .ctx = Context{},
        .decoder = CallDecoder{&new_call.ctx}
    };
    auto it = fragmented_calls.emplace(call_id, new_call).first;
    *call = &it->second;

    // TODO: here we extend the lifetime of the reference to the TX channel beyond
    // the duration of the fragment processing. We must therefore somehow
    // ref-counf the TX channel so that it can be closed when the ref count
    // reaches 0.
    (*call)->ctx.add_tx_channel(ctx->preferred_tx_channel);
    return 0;
}

// TODO: error handling:
// Should mark this call finished but not deallocate yet. If we deallocate
// and get another fragment, the new fragment is indistinguishable from
// a new call.
int end_call(call_id_t call_id) {
    FIBRE_LOG(D) << "end call " << call_id;
    auto it = fragmented_calls.find(call_id);
    if (it != fragmented_calls.end())
        fragmented_calls.erase(it);
    return 0;
}


// Should remove this call from all dispatchers
void dispose(std::shared_ptr<outgoing_call_t> call) {
    FIBRE_LOG(D) << "disposing call " << call;
    *(call->cancellation_token) -= &call->cancel_obj;
    main_dispatcher.remove_call(call);
    if (call->finished_callback)
        (*call->finished_callback)();
}


}

