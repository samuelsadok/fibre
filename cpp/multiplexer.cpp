
#include <fibre/multiplexer.hpp>
#include <fibre/channel_discoverer.hpp>
#include <fibre/tx_pipe.hpp>
#include <algorithm>

using namespace fibre;

void Multiplexer::add_source(TxPipe* pipe) {
    if (sending_pipe_) {
        queue_.push_back(pipe);
    } else {
        send_next(pipe);
    }
}

void Multiplexer::remove_source(TxPipe* pipe) {
    if (pipe == sending_pipe_) {
        sink_->cancel_write();
        maybe_send_next();
    } else {
        auto it = std::find(queue_.begin(), queue_.end(), pipe);
        if (it == queue_.end()) {
            return; // TODO: log error
        }
        queue_.erase(it);
    }
}

void Multiplexer::maybe_send_next() {
    if (queue_.size()) {
        // TODO: support sending from multiple sources at once
        TxPipe* pipe = queue_.front();
        queue_.erase(queue_.begin());
        send_next(pipe);
    } else {
        sending_pipe_ = nullptr;
    }
}

void Multiplexer::send_next(TxPipe* pipe) {
    TxTask jobs[1];
    auto job = pipe->get_task();
    jobs[0].pipe = pipe;
    jobs[0].slot_id = pipe->backend_slot_id;
    jobs[0].begin_ = job.c_begin();
    jobs[0].end_ = job.c_end();
    sending_pipe_ = pipe;
    sink_->start_write({jobs, jobs + 1});
}

void Multiplexer::on_sent(TxPipe* pipe, CBufIt end) {
    pipe->release_task(end);

    if (pipe->has_data()) {
        queue_.push_back(pipe);
    } else {
        pipe->multiplexer_ = this;
    }

    maybe_send_next();
}

void Multiplexer::on_cancelled(TxPipe* pipe, CBufIt end) {
    pipe->release_task(end);
}
