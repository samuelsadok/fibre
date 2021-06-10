
#include "legacy_object_server.hpp"
#include "codecs.hpp"
#include <fibre/fibre.hpp>
#include <algorithm>

using namespace fibre;



// Returns part of the JSON interface definition.
RichStatus endpoint0_handler(cbufptr_t* input_buffer, bufptr_t* output_buffer) {
    // The request must contain a 32 bit integer to specify an offset
    std::optional<uint32_t> offset = read_le<uint32_t>(input_buffer);
    
    if (!offset.has_value()) {
        // Didn't receive any offset
        return F_MAKE_ERR("offset missing");
    } else if (*offset == 0xffffffff) {
        // If the offset is special value 0xFFFFFFFF, send back the JSON version ID instead
        return write_le<uint32_t>(json_version_id_, output_buffer) ? RichStatus::success() : F_MAKE_ERR("decoding failed");
    } else if (*offset >= embedded_json_length) {
        // Attempt to read beyond the buffer end - return empty response
        return RichStatus::success();
    } else {
        // Return part of the json file
        size_t n_copy = std::min(output_buffer->size(), embedded_json_length - (size_t)*offset);
        memcpy(output_buffer->begin(), embedded_json + *offset, n_copy);
        *output_buffer = output_buffer->skip(n_copy);
        return RichStatus::success();
    }
}

RichStatus LegacyObjectServer::endpoint_handler(Domain* domain, int idx, cbufptr_t* input_buffer, bufptr_t* output_buffer) {
    F_RET_IF(idx < 0 || idx >= n_endpoints, "invalid endpoint");

    if (idx == 0) {
        return endpoint0_handler(input_buffer, output_buffer);
    }

    EndpointDefinition& ep = endpoint_table[idx];

    if (ep.type == EndpointType::kRoProperty || ep.type == EndpointType::kRwProperty) {
        F_RET_IF(ep.type == EndpointType::kRoProperty && input_buffer->size() != 0,
                 "size mismatch");

        ServerObjectId obj = ep.type == EndpointType::kRoProperty ? ep.ro_property.object_id : ep.rw_property.object_id;
        ServerFunctionId fn = ep.type == EndpointType::kRoProperty ? ep.ro_property.read_function_id :
                              input_buffer->size() ? ep.rw_property.exchange_function_id : ep.rw_property.read_function_id;

        // Wrate object ID into RX buf as first argument
        bufptr_t outbuf{rx_buf_};
        // TODO: this codec doesn't work like that
        fibre::Codec<ServerObjectId>::encode(obj, &outbuf);
        rx_pos_ = outbuf.begin() - rx_buf_;

        // Write argument into RX buf as second argument
        size_t n_copy = std::min(input_buffer->size(), sizeof(rx_buf_) - rx_pos_);
        std::copy_n(input_buffer->begin(), n_copy, rx_buf_ + rx_pos_);
        rx_pos_ += n_copy;
        *input_buffer = input_buffer->skip(n_copy);

        // invoke function
        auto* func = domain->get_server_function(fn);
        F_RET_IF(!func, "invalid function");

        uint8_t call_frame[256];
        std::optional<CallBufferRelease> call_buffer_release /* TODO = func->call(
            domain,
            true, // start
            call_frame,
            {kFibreClosed, {rx_buf_, rx_pos_}, *output_buffer}, // call buffers
            {} // continuation
        )*/;

        F_RET_IF(!call_buffer_release.has_value(),
                 "legacy protocol used to call function that did not return synchronously");

        *output_buffer = output_buffer->skip(call_buffer_release->tx_end - output_buffer->begin());

        F_RET_IF(call_buffer_release->status != kFibreClosed,
                 "legacy protocol return error " << call_buffer_release->status << " but legacy protocol does not support error reporting");

        return RichStatus::success();

    } else {
        // This endpoint access is part of a function call

        if (idx != expected_ep_) {
            reset(); // reset function call state

            // Find the end of the function and determine its arg sizes
            for (size_t i = idx + 1; i < n_endpoints; ++i) {
                if (endpoint_table[i].type == EndpointType::kFunctionInput) {
                    n_inputs_++;
                } else if (endpoint_table[i].type == EndpointType::kFunctionOutput) {
                    output_size_ += endpoint_table[i].function_output.size;
                    n_outputs_++;
                } else {
                    break;
                }
            }

            bool correct_first_ep_access = 
                    (ep.type == EndpointType::kFunctionTrigger && n_inputs_ == 0) // functions with no in args
                 || (ep.type == EndpointType::kFunctionInput && endpoint_table[idx - 1].type == EndpointType::kFunctionTrigger); // functions with some in args

            F_RET_IF(!correct_first_ep_access, "incorrect endpoint access");

            trigger_ep_ = ep.type == EndpointType::kFunctionTrigger ? idx : (idx - 1);

            if (ep.type != EndpointType::kFunctionTrigger) {
                n_inputs_++;
            }
            
            // Wrate object ID into RX buf as first argument
            bufptr_t outbuf{rx_buf_};
            // TODO: this codec doesn't work like that
            fibre::Codec<ServerObjectId>::encode(endpoint_table[trigger_ep_].function_trigger.object_id, &outbuf);
            rx_pos_ = outbuf.begin() - rx_buf_;
        }


        if (ep.type == EndpointType::kFunctionInput) {
            F_RET_IF(input_buffer->size() != ep.function_input.size || output_buffer->size() != 0,
                     "size mismatch");

            // Copy input buffer into scratch buffer
            std::copy_n(input_buffer->begin(), input_buffer->size(), rx_buf_ + rx_pos_);
            rx_pos_ += input_buffer->size();
            *input_buffer = input_buffer->skip(input_buffer->size());

            // advance progress (to next input or trigger ep)
            expected_ep_ = (idx + 1 - trigger_ep_) % (n_inputs_ + 1) + trigger_ep_;

        } else if (ep.type == EndpointType::kFunctionTrigger) {
            F_RET_IF(input_buffer->size() != 0 || output_buffer->size() != 0,
                     "size mismatch");

            // invoke function
            auto* func = domain->get_server_function(ep.function_trigger.function_id);
            F_RET_IF(!func, "invalid function");

            uint8_t call_frame[256];
            std::optional<CallBufferRelease> call_buffer_release /* TODO = func->call(
                domain,
                true, // start
                call_frame,
                {kFibreClosed, {rx_buf_, rx_pos_}, {tx_buf_, output_size_}}, // call buffers
                {} // continuation
            )*/;

            F_RET_IF(!call_buffer_release.has_value(),
                     "legacy protocol used to call function that did not return synchronously");

            F_RET_IF(call_buffer_release->status != kFibreClosed,
                     "legacy protocol return error " << call_buffer_release->status << " but legacy protocol does not support error reporting");

            // advance progress (to first output ep or to 0 if there are no outputs)
            expected_ep_ = (trigger_ep_ + n_inputs_ + 1) % (trigger_ep_ + n_inputs_ + 1 + n_outputs_);

        } else if (ep.type == EndpointType::kFunctionOutput) {
            // copy to output buffer
            F_RET_IF(input_buffer->size() != 0 || output_buffer->size() != ep.function_output.size,
                     "size mismatch");

            // Copy scratch buffer into output buffer
            std::copy_n(tx_buf_ + tx_pos_, output_buffer->size(), output_buffer->begin());
            tx_pos_ += output_buffer->size();
            *output_buffer = output_buffer->skip(output_buffer->size());

            // advance progress (to next output or 0)
            expected_ep_ = (idx + 1) % (trigger_ep_ + n_inputs_ + 1 + n_outputs_);
        }

        return RichStatus::success();
    }
}
