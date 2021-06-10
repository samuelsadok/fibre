
#include <fibre/func_utils.hpp>

using namespace fibre;

Socket* FuncAsCoro::start_call(Domain* domain, bufptr_t call_frame,
                               Socket* caller) const {
    if (call_frame.size() < sizeof(FuncAsCoroCall)) {
        return nullptr;
    }
    FuncAsCoroCall* call =
        new ((FuncAsCoroCall*)call_frame.begin()) FuncAsCoroCall{};
    call->func = this;
    call->domain_ = domain;
    call->caller_ = caller;
    call->buf_end = call_frame.end();
    call->collector_or_emitter_ = ArgCollector{{(uint8_t*)(call + 1)}, 1, 0};
    return call;
}

WriteResult ArgCollector::write(WriteArgs args, bufptr_t storage) {
    while (args.buf.n_chunks()) {
        if (n_arg_dividers_ >=
            sizeof(arg_dividers_) / sizeof(arg_dividers_[0])) {
            return {kFibreOutOfMemory, args.buf.begin()};
        }

        Chunk chunk = args.buf.front();

        if (chunk.is_buf()) {
            // Copy caller's input buffer into call's state buffer
            if (chunk.buf().size() > storage.size() - offset_) {
                return {kFibreOutOfMemory, args.buf.begin()};
            }
            std::copy(chunk.buf().begin(), chunk.buf().end(),
                      storage.begin() + offset_);
            offset_ += chunk.buf().size();

        } else if (chunk.is_frame_boundary() && chunk.layer() == 0) {
            // Switch to next input arg
            arg_dividers_[n_arg_dividers_] = storage.begin() + offset_;
            n_arg_dividers_++;
        }

        args.buf = args.buf.skip_chunks(1);
    }
    return {args.status, args.buf.begin()};
}

void ArgEmitter::start(Status status, const uint8_t** arg_dividers,
                       size_t n_arg_dividers, Socket* sink) {
    // Convert output args to chunks
    BufChainBuilder builder{chunks_};
    if (2 * n_arg_dividers > sizeof(chunks_) / sizeof(chunks_[0]) + 2) {
        status_ = kFibreOutOfMemory;
    } else {
        status_ = status;
        write_iterator it{builder};
        for (size_t i = 1; i < n_arg_dividers; ++i) {
            it = Chunk(0, {arg_dividers[i - 1], arg_dividers[i]});
            it = Chunk::frame_boundary(0);
        }
    }

    tx_chain_ = builder;

    // Write output arguments until done or connection blocks
    WriteResult result = sink->write({tx_chain_, status_});
    while (!result.is_busy()) {
        tx_chain_ = tx_chain_.from(result.end);
        if (!tx_chain_.n_chunks()) {
            break;
        }
        result = sink->write({tx_chain_, status_});
    }
}

WriteArgs ArgEmitter::on_write_done(WriteResult result) {
    tx_chain_ = tx_chain_.from(result.end);
    return {tx_chain_, status_};
}

WriteResult FuncAsCoroCall::write(WriteArgs args) {
    bufptr_t arg_memory{(uint8_t*)(this + 1), buf_end};

    if (collector_or_emitter_.index() != 0) {
        // Bad source behavior
        return {kFibreInternalError, args.buf.end()};
    }

    ArgCollector& collector = std::get<0>(collector_or_emitter_);
    WriteResult result = collector.write(args, arg_memory);

    if (result.status == kFibreClosed) {
        const uint8_t* arg_dividers[8];
        size_t n_arg_dividers = 8;

        Status status = func->impl_.invoke(
            domain_, collector.arg_dividers_, collector.n_arg_dividers_,
            arg_dividers, &n_arg_dividers, arg_memory);

        collector_or_emitter_ = ArgEmitter{};
        std::get<1>(collector_or_emitter_)
            .start(status, arg_dividers, n_arg_dividers, caller_);

    } else if (result.status != kFibreOk) {
        collector_or_emitter_ = ArgEmitter{};
        std::get<1>(collector_or_emitter_)
            .start(result.status, nullptr, 0, caller_);
    }

    return result;
}

WriteArgs FuncAsCoroCall::on_write_done(WriteResult result) {
    return std::get<1>(collector_or_emitter_).on_write_done(result);
}

void CoroAsFunc::call(const cbufptr_t* inputs, size_t n_inputs,
                      Callback<void, Socket*, Status, const cbufptr_t*, size_t>
                          on_call_finished) {
    
    collector_ = ArgCollector{{rx_buf}, 1, 0};

    const uint8_t* arg_dividers[8] = {tx_buf};
    size_t n_arg_dividers = n_inputs + 1;

    // Convert input arguments to chunks
    size_t tx_buf_pos = 0;
    for (size_t i = 0; i < n_inputs; ++i) {
        if (inputs[i].size() > sizeof(tx_buf) - tx_buf_pos) {
            on_call_finished.invoke(this, kFibreOutOfMemory, nullptr, 0);
            return;
        }
        std::copy(inputs[i].begin(), inputs[i].end(), tx_buf + tx_buf_pos);
        arg_dividers[i] = tx_buf + tx_buf_pos;
        tx_buf_pos += inputs[i].size();
    }

    on_call_finished_ = on_call_finished;

    Socket* call = func->start_call(nullptr, call_frame, this);
    emitter_.start(kFibreClosed, arg_dividers, n_arg_dividers, call);
}

WriteArgs CoroAsFunc::on_write_done(WriteResult result) {
    return emitter_.on_write_done(result);
}

WriteResult CoroAsFunc::write(WriteArgs args) {
    WriteResult result = collector_.write(args, rx_buf);

    if (result.status != kFibreOk) {
        cbufptr_t chunks[collector_.n_arg_dividers_ / 2];
        for (size_t i = 1; i < collector_.n_arg_dividers_; ++i) {
            chunks[(i - 1) / 2] = {collector_.arg_dividers_[i - 1],
                                   collector_.arg_dividers_[i]};
        }

        on_call_finished_.invoke_and_clear(this, args.status, chunks,
                                           collector_.n_arg_dividers_ / 2);
    }

    return result;
}
